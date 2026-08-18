#!/usr/bin/env python3
"""Multi-day fixed-θ skip-agent replay + compare (0811–0814).

Rules:
  - Require sensor_events.csv (else discard session)
  - Borrow baro within same-day near groups (gap <= 60s) when own baro thin
  - No agent; seed each arm with a recent final θ

Arms (recent 0812 agent finals):
  default              DEFAULT_THETA (no agent)
  guided_limited       v4 / range-clipped + guided recipe
  guided_nolimit       unbounded range + guided recipe
  explore_nolimit      unbounded + explore (DeepSeek)
  explore_qwen_nolimit unbounded + explore (Qwen)
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Optional

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from commute_baseline.engine import DEFAULT_THETA  # noqa: E402

# Reuse helpers from 0812 sequential replay.
sys.path.insert(0, str(ROOT / "examples"))
import replay_0812_full_agent as R  # noqa: E402

TZ = timezone(timedelta(hours=8))

ARMS: dict[str, dict[str, Any]] = {
    "default": {
        "label": "无agent（DEFAULT）",
        "limits": "n/a",
        "prompt": "none",
        "theta_src": None,
    },
    "guided_limited": {
        "label": "有范围限制 + 有策略提示（guided v4）",
        "limits": "yes",
        "prompt": "guided",
        "theta_src": ROOT / "output" / "real_0812_seq_agent_flow_v4" / "theta.json",
    },
    "guided_nolimit": {
        "label": "无范围限制 + 有策略提示（guided nolimit）",
        "limits": "no",
        "prompt": "guided",
        "theta_src": ROOT / "output" / "real_0812_seq_agent_flow_nolimit" / "theta.json",
    },
    "explore_nolimit": {
        "label": "无范围限制 + 无策略菜谱（explore DeepSeek）",
        "limits": "no",
        "prompt": "explore",
        "theta_src": ROOT / "output" / "real_0812_seq_agent_flow_explore" / "theta.json",
    },
    "explore_qwen_nolimit": {
        "label": "无范围限制 + 无策略菜谱（explore Qwen）",
        "limits": "no",
        "prompt": "explore_qwen",
        "theta_src": ROOT / "output" / "real_0812_seq_agent_flow_explore_qwen" / "theta.json",
    },
}

DAY_ROOTS = [
    (r"D:\0811", "/mnt/d/0811", "0811"),
    (r"D:\0812", "/mnt/d/0812", "0812"),
    (r"D:\0813", "/mnt/d/0813", "0813"),
    (r"D:\0814", "/mnt/d/0814", "0814"),
]


def list_sessions_require_sensor(data_root: Path) -> tuple[list[Path], list[str]]:
    ok: list[Path] = []
    discarded: list[str] = []
    for p in sorted(data_root.iterdir()):
        if not p.is_dir() or not p.name.startswith("2026"):
            continue
        if not (any(p.glob("location_data_*.csv")) or (p / "sensor_events.csv").is_file()):
            continue
        if not (p / "sensor_events.csv").is_file():
            discarded.append(p.name)
            continue
        ok.append(p)
    return ok, discarded


def load_theta_or_default(src: Optional[Path]) -> dict:
    if src is None:
        return dict(DEFAULT_THETA)
    return R.load_theta(src)


def classify_false_soft_hard(policy_history: Path) -> tuple[int, int]:
    """Count FALSE_PUSH episodes as soft/hard via obs_baro_lower_platform."""
    if not policy_history.is_file():
        return 0, 0
    eps: dict[int, list] = defaultdict(list)
    for ln in policy_history.read_text(encoding="utf-8").splitlines():
        if not ln.strip():
            continue
        o = json.loads(ln)
        ot = o.get("outcome_t_ms")
        if ot is None:
            continue
        eps[int(ot)].append(o)
    soft = hard = 0
    for ticks in eps.values():
        if not ticks or ticks[0].get("label") != "FALSE_PUSH":
            continue
        if any((t.get("obs_baro_lower_platform") or 0) >= 0.5 for t in ticks):
            soft += 1
        else:
            hard += 1
    return soft, hard


def summarize_arm(out_root: Path, arm_meta: dict, theta: dict, discarded: list[dict]) -> dict:
    pushes: list[dict] = []
    labels: list[dict] = []
    ep = out_root / "leave_episodes.jsonl"
    if ep.is_file():
        for ln in ep.read_text(encoding="utf-8").splitlines():
            if not ln.strip():
                continue
            o = json.loads(ln)
            if o.get("type") == "push" and o.get("scene") == "LEAVING_COMPANY":
                pushes.append(o)
            elif o.get("type") == "label":
                labels.append(o)

    label_by_push = {(l.get("t_push_ms"), l.get("label")) for l in labels}
    # map push -> label
    lab_map = {l.get("t_push_ms"): l for l in labels}
    label_counts = Counter(l.get("label") for l in labels)
    leads = [l.get("lead_s") for l in labels if isinstance(l.get("lead_s"), (int, float))]
    soft_n, hard_n = classify_false_soft_hard(out_root / "policy_history.jsonl")

    per_day: dict[str, dict] = defaultdict(lambda: {"pushes": 0, "FALSE_PUSH": 0, "CONFIRMED_LEAVE": 0})
    per_push = []
    for p in pushes:
        t = p.get("t_push_ms")
        lab = lab_map.get(t, {})
        day = ""
        if isinstance(t, (int, float)):
            day = datetime.fromtimestamp(int(t) / 1000, TZ).strftime("%Y%m%d")
        label = lab.get("label")
        per_day[day]["pushes"] += 1
        if label:
            per_day[day][label] = per_day[day].get(label, 0) + 1
        per_push.append(
            {
                "t_push_ms": t,
                "t_push": datetime.fromtimestamp(int(t) / 1000, TZ).isoformat() if t else None,
                "day": day,
                "label": label,
                "lead_s": lab.get("lead_s"),
                "truth_source": lab.get("truth_source"),
                "score_company": p.get("score_company"),
            }
        )

    seq = json.loads((out_root / "SEQ_SUMMARY.json").read_text(encoding="utf-8")) if (
        out_root / "SEQ_SUMMARY.json"
    ).is_file() else {}

    return {
        "arm": arm_meta,
        "theta": {k: theta.get(k) for k in (
            "enter_leave", "arm_delay_s", "w_walk", "w_pdr", "w_geo",
            "w_wifi", "w_cell", "w_ble", "w_time", "w_baro",
        )},
        "n_sessions_ok": seq.get("n_sessions_ok"),
        "failed": seq.get("failed", []),
        "discarded_no_sensor": discarded,
        "borrow_map": seq.get("borrow_map", {}),
        "n_pushes": len(pushes),
        "label_counts": dict(label_counts),
        "false_soft": soft_n,
        "false_hard": hard_n,
        "lead_s": {
            "n": len(leads),
            "min": min(leads) if leads else None,
            "mean": round(sum(leads) / len(leads), 1) if leads else None,
            "max": max(leads) if leads else None,
        },
        "per_day": dict(per_day),
        "per_push": per_push,
    }


def replay_arm(
    arm_name: str,
    arm_meta: dict,
    tick_s: float,
    group_gap_s: float,
    out_base: Path,
) -> dict:
    theta = load_theta_or_default(arm_meta["theta_src"])
    out_root = out_base / arm_name
    if out_root.exists():
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True, exist_ok=True)
    R.write_theta(out_root / "theta_seed.json", theta)
    R.write_theta(out_root / "theta.json", theta)

    discarded_all: list[dict] = []
    sessions: list[Path] = []
    baro_for: dict[str, Optional[Path]] = {}
    borrow_map: dict[str, str] = {}

    for win, wsl, day_tag in DAY_ROOTS:
        try:
            data_root = R.resolve_data_root(win, wsl)
        except FileNotFoundError as e:
            print(f"WARN skip day {day_tag}: {e}", flush=True)
            continue
        day_sessions, discarded = list_sessions_require_sensor(data_root)
        for name in discarded:
            discarded_all.append({"day": day_tag, "session": name, "reason": "no_sensor_events"})
            print(f"DISCARD {day_tag}/{name}: no sensor_events.csv", flush=True)
        groups = R.group_near_sessions(day_sessions, group_gap_s)
        for g in groups:
            donor = R.pick_baro_donor(g)
            members = ", ".join(f"{p.name}(baro={R.baro_row_count(p)})" for p in g)
            print(f"[{arm_name}] {day_tag} group: {members} donor={None if donor is None else donor.name}", flush=True)
            for p in g:
                own = R.baro_row_count(p)
                if own >= R.BARO_MIN_ROWS:
                    baro_for[p.name] = None
                elif donor is not None:
                    baro_for[p.name] = donor
                    borrow_map[p.name] = donor.name
                else:
                    baro_for[p.name] = None
                    print(f"  WARN: {p.name} no baro and no donor", flush=True)
        sessions.extend(day_sessions)

    sessions = sorted(sessions, key=lambda p: p.name)
    print(f"[{arm_name}] total sessions={len(sessions)} discarded={len(discarded_all)}", flush=True)

    (out_root / "BARO_BORROW.json").write_text(
        json.dumps(borrow_map, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (out_root / "DISCARDED.json").write_text(
        json.dumps(discarded_all, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    theta_path = out_root / "theta_seed.json"
    completed: list[Path] = []
    failed: list[str] = []
    session_out_prefix = f"multiday_{arm_name}_"

    for raw in sessions:
        out = ROOT / "output" / f"{session_out_prefix}{raw.name}"
        rc = R.replay_one(raw, out, tick_s, baro_for.get(raw.name), theta_path)
        if rc != 0:
            failed.append(raw.name)
            print("FAILED", raw.name, "rc", rc, flush=True)
            continue
        completed.append(out)

    if completed:
        R.stage_workspace(completed, out_root, theta, None)
        R.write_theta(out_root / "theta.json", theta)

    final = {
        "n_sessions_ok": len(completed),
        "failed": failed,
        "agent_runs": 0,
        "borrow_map": borrow_map,
        "final_theta": {k: theta.get(k) for k in (
            "enter_leave", "arm_delay_s", "w_walk", "w_pdr", "w_geo",
            "w_wifi", "w_cell", "w_ble", "w_time", "w_baro",
        )},
    }
    (out_root / "SEQ_SUMMARY.json").write_text(
        json.dumps(final, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return summarize_arm(out_root, {**arm_meta, "name": arm_name}, theta, discarded_all)


def write_compare_md(out_base: Path, results: list[dict]) -> Path:
    lines = [
        "# 多日固定θ回放对比（0811–0814）",
        "",
        "## 设定",
        "",
        "- 数据：`D:/0811` `D:/0812` `D:/0813` `D:/0814`",
        "- 缺 `sensor_events.csv` → **舍弃**该会话",
        "- 缺/过薄气压 → **同日近邻组（≤60s）借 Baro**",
        "- `--skip-agent`：用各臂 0812 训练得到的**最终θ**固定回放",
        "",
        "## 各臂 θ 来源",
        "",
        "| arm | 含义 | limits | prompt | θ 来源 |",
        "|---|---|---|---|---|",
    ]
    for r in results:
        a = r["arm"]
        src = a.get("theta_src")
        src_s = "DEFAULT_THETA" if not src else str(src).replace(str(ROOT) + "/", "").replace(str(ROOT) + "\\", "")
        if isinstance(src, Path):
            try:
                src_s = str(src.relative_to(ROOT)).replace("\\", "/")
            except Exception:
                src_s = str(src)
        lines.append(
            f"| `{a.get('name')}` | {a.get('label')} | {a.get('limits')} | {a.get('prompt')} | `{src_s}` |"
        )

    lines += [
        "",
        "## 总表",
        "",
        "| arm | sessions | pushes | CONFIRMED | FALSE | soft假 | hard假 | lead mean (n) |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for r in results:
        lc = r.get("label_counts") or {}
        lead = r.get("lead_s") or {}
        lead_s = f"{lead.get('mean')} ({lead.get('n')})" if lead.get("n") else "—"
        lines.append(
            f"| `{r['arm'].get('name')}` | {r.get('n_sessions_ok')} | {r.get('n_pushes')} | "
            f"{lc.get('CONFIRMED_LEAVE', 0)} | {lc.get('FALSE_PUSH', 0)} | "
            f"{r.get('false_soft', 0)} | {r.get('false_hard', 0)} | {lead_s} |"
        )

    lines += ["", "## θ 关键旋钮", "", "| arm | enter_leave | arm_delay | w_walk | w_wifi | w_baro | w_pdr | w_time |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for r in results:
        th = r.get("theta") or {}
        lines.append(
            f"| `{r['arm'].get('name')}` | {th.get('enter_leave')} | {th.get('arm_delay_s')} | "
            f"{th.get('w_walk')} | {th.get('w_wifi')} | {th.get('w_baro')} | {th.get('w_pdr')} | {th.get('w_time')} |"
        )

    lines += ["", "## 分日推送数", ""]
    days = sorted({d for r in results for d in (r.get("per_day") or {})})
    header = "| arm | " + " | ".join(days) + " |"
    sep = "|---|" + "|".join(["---:"] * len(days)) + "|"
    lines += [header, sep]
    for r in results:
        cells = []
        for d in days:
            pd = (r.get("per_day") or {}).get(d) or {}
            cells.append(str(pd.get("pushes", 0)))
        lines.append(f"| `{r['arm'].get('name')}` | " + " | ".join(cells) + " |")

    # discarded
    disc = results[0].get("discarded_no_sensor") if results else []
    lines += ["", "## 舍弃会话（无 sensor_events）", ""]
    if not disc:
        lines.append("（无）")
    else:
        for d in disc:
            lines.append(f"- `{d.get('day')}/{d.get('session')}`")

    lines += ["", "## 读数说明", ""]
    lines += [
        "- **soft假**：`FALSE_PUSH` 且窗口内出现 `obs_baro_lower_platform`（一楼/大堂正样本取向）",
        "- **hard假**：`FALSE_PUSH` 且无下层平台气压证据",
        "- soft/hard 按 `policy_history` 的 `outcome_t_ms` episode 计；sibling 同刻推送可能各算一次",
        "",
    ]

    # brief takeaway
    lines += ["## 简要对比", ""]
    if len(results) >= 2:
        by_name = {r["arm"]["name"]: r for r in results}
        g_lim = by_name.get("guided_limited")
        g_nl = by_name.get("guided_nolimit")
        ex = by_name.get("explore_nolimit") or by_name.get("explore_qwen_nolimit")
        base = by_name.get("default")
        if g_lim and g_nl:
            lines.append(
                f"- **范围限制**：guided_limited vs guided_nolimit → "
                f"CONFIRMED { (g_lim.get('label_counts') or {}).get('CONFIRMED_LEAVE',0) }/"
                f"{ (g_nl.get('label_counts') or {}).get('CONFIRMED_LEAVE',0) }，"
                f"FALSE { (g_lim.get('label_counts') or {}).get('FALSE_PUSH',0) }/"
                f"{ (g_nl.get('label_counts') or {}).get('FALSE_PUSH',0) }，"
                f"hard假 {g_lim.get('false_hard')}/{g_nl.get('false_hard')}。"
            )
        if g_nl and ex:
            lines.append(
                f"- **策略提示**：guided_nolimit vs explore → "
                f"CONFIRMED {(g_nl.get('label_counts') or {}).get('CONFIRMED_LEAVE',0)}/"
                f"{(ex.get('label_counts') or {}).get('CONFIRMED_LEAVE',0)}，"
                f"FALSE {(g_nl.get('label_counts') or {}).get('FALSE_PUSH',0)}/"
                f"{(ex.get('label_counts') or {}).get('FALSE_PUSH',0)}，"
                f"lead mean {(g_nl.get('lead_s') or {}).get('mean')}/{(ex.get('lead_s') or {}).get('mean')}。"
            )
        if base:
            lines.append(
                f"- **相对 DEFAULT**：各臂 pushes/CONFIRMED/FALSE 见总表；DEFAULT hard假={base.get('false_hard')}。"
            )

    path = out_base / "COMPARE_SUMMARY.md"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tick-s", type=float, default=5.0)
    ap.add_argument("--group-gap-s", type=float, default=60.0)
    ap.add_argument(
        "--out-base",
        default=str(ROOT / "output" / "multiday_theta_compare_0811_0814"),
    )
    ap.add_argument(
        "--arms",
        default="default,guided_limited,guided_nolimit,explore_nolimit,explore_qwen_nolimit",
        help="comma-separated arm names",
    )
    args = ap.parse_args()

    out_base = Path(args.out_base)
    out_base.mkdir(parents=True, exist_ok=True)

    # resolve relative theta_src strings in ARMS for display
    for meta in ARMS.values():
        src = meta.get("theta_src")
        if isinstance(src, Path) and not src.is_file():
            print(f"ERROR missing theta: {src}", file=sys.stderr)
            return 1

    wanted = [a.strip() for a in args.arms.split(",") if a.strip()]
    results = []
    for name in wanted:
        if name not in ARMS:
            print(f"ERROR unknown arm {name}", file=sys.stderr)
            return 1
        print(f"\n########## ARM {name} ##########", flush=True)
        meta = dict(ARMS[name])
        meta["name"] = name
        # store path as string for JSON
        if meta.get("theta_src") is not None:
            meta["theta_src"] = str(meta["theta_src"])
        r = replay_arm(name, {**ARMS[name], "name": name}, args.tick_s, args.group_gap_s, out_base)
        # json-friendly
        if isinstance(r["arm"].get("theta_src"), Path):
            r["arm"]["theta_src"] = str(r["arm"]["theta_src"])
        results.append(r)
        (out_base / name / "ARM_SUMMARY.json").write_text(
            json.dumps(r, ensure_ascii=False, indent=2, default=str) + "\n", encoding="utf-8"
        )

    (out_base / "COMPARE_SUMMARY.json").write_text(
        json.dumps(results, ensure_ascii=False, indent=2, default=str) + "\n", encoding="utf-8"
    )
    md = write_compare_md(out_base, results)
    print("\n======== COMPARE DONE ========", flush=True)
    print(md.read_text(encoding="utf-8"), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
