#!/usr/bin/env python3
"""Sequential D:/0812 replay + Agent with cumulative history.

For each session in time order:
  1) Replay with the *current* theta (starts from DEFAULT; updated after each Agent commit)
  2) Borrow baro within near-triplet groups when own baro is missing/thin
  3) Refresh product workspace so policy_history / leave_episodes include all sessions so far
  4) If this session had a company push → AFTER_PUSH Agent (eval history = all prior+current)
  5) Carry updated theta into the next session

maxTurn defaults to 80 (host); pass --max-turn to raise further if needed.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from collections import Counter
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from commute_baseline.engine import DEFAULT_THETA  # noqa: E402

BARO_MIN_ROWS = 50


def resolve_data_root(win: str, wsl: str) -> Path:
    wsl_p = Path(wsl)
    win_p = Path(win)
    if wsl_p.is_dir():
        return wsl_p
    if win_p.is_dir():
        return win_p
    raise FileNotFoundError(f"missing data root {wsl} / {win}")


def to_posix(path: Path | str) -> str:
    s = str(path).replace("\\", "/")
    if len(s) >= 2 and s[1] == ":":
        drive = s[0].lower()
        return f"/mnt/{drive}/" + s[3:].lstrip("/").replace("\\", "/")
    return s


def session_dt(name: str) -> datetime:
    return datetime.strptime(name, "%Y%m%d_%H%M%S")


def baro_row_count(sess: Path) -> int:
    n = 0
    for f in sess.glob("baro_data_*.csv"):
        with f.open(encoding="utf-8-sig") as fh:
            n += max(0, sum(1 for _ in fh) - 1)
    return n


def list_sessions(data_root: Path) -> list[Path]:
    out: list[Path] = []
    for p in sorted(data_root.iterdir()):
        if not p.is_dir() or not p.name.startswith("2026"):
            continue
        if any(p.glob("location_data_*.csv")) or (p / "sensor_events.csv").is_file():
            out.append(p)
    return out


def group_near_sessions(sessions: list[Path], max_gap_s: float = 60.0) -> list[list[Path]]:
    if not sessions:
        return []
    groups: list[list[Path]] = [[sessions[0]]]
    for s in sessions[1:]:
        prev = groups[-1][-1]
        gap = abs((session_dt(s.name) - session_dt(prev.name)).total_seconds())
        if gap <= max_gap_s:
            groups[-1].append(s)
        else:
            groups.append([s])
    return groups


def pick_baro_donor(group: list[Path]) -> Optional[Path]:
    ranked = sorted(((baro_row_count(p), p) for p in group), key=lambda x: -x[0])
    if not ranked or ranked[0][0] < BARO_MIN_ROWS:
        return None
    return ranked[0][1]


def write_theta(path: Path, theta: dict) -> None:
    body = {"coordinate_system": "WGS84", **{k: v for k, v in theta.items() if k != "coordinate_system"}}
    path.write_text(json.dumps(body, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def load_theta(path: Path) -> dict:
    obj = json.loads(path.read_text(encoding="utf-8"))
    return {k: v for k, v in obj.items() if k != "coordinate_system"}


def concat_jsonl(paths: list[Path]) -> str:
    lines: list[str] = []
    for path in paths:
        if not path.is_file():
            continue
        lines.extend(ln for ln in path.read_text(encoding="utf-8").splitlines() if ln.strip())
    return "\n".join(lines) + ("\n" if lines else "")


def find_company_pushes(session_out: Path) -> list[dict]:
    eps = session_out / "leave_episodes.jsonl"
    if not eps.is_file():
        return []
    pushes = []
    for ln in eps.read_text(encoding="utf-8").splitlines():
        if not ln.strip():
            continue
        obj = json.loads(ln)
        if obj.get("type") == "push" and obj.get("scene") == "LEAVING_COMPANY":
            pushes.append(obj)
    return pushes


def find_label_for_push(session_out: Path, t_push_ms: Any) -> Optional[str]:
    eps = session_out / "leave_episodes.jsonl"
    if not eps.is_file():
        return None
    for ln in eps.read_text(encoding="utf-8").splitlines():
        if not ln.strip():
            continue
        obj = json.loads(ln)
        if obj.get("type") == "label" and obj.get("t_push_ms") == t_push_ms:
            return obj.get("label")
    return None


def replay_one(
    raw: Path,
    out: Path,
    tick_s: float,
    baro_dir: Optional[Path],
    theta_path: Path,
) -> int:
    cmd = [
        sys.executable,
        str(ROOT / "examples" / "run_real_flow.py"),
        "--raw-dir",
        str(raw),
        "--sensor-dir",
        str(raw),
        "--out-dir",
        str(out),
        "--anchors",
        str(ROOT / "config" / "anchors.json"),
        "--company-radio-fingerprint",
        str(ROOT / "config" / "company_radio_fingerprint.json"),
        "--location-crs",
        "GCJ02",
        "--tick-s",
        str(tick_s),
        "--theta",
        str(theta_path),
    ]
    if baro_dir is not None and baro_dir.resolve() != raw.resolve():
        cmd.extend(["--baro-dir", str(baro_dir)])
    print("\n======== replay", raw.name, "========", flush=True)
    if baro_dir and baro_dir.resolve() != raw.resolve():
        print(f"baro_borrow: {baro_dir.name} -> {raw.name}", flush=True)
    print(" ".join(cmd), flush=True)
    return subprocess.call(cmd)


def stage_workspace(
    srcs: list[Path],
    out_root: Path,
    theta: dict,
    trigger_session: Optional[Path],
) -> dict:
    """Refresh cumulative product files; keep param_changes/audit append-only logs elsewhere."""
    out_root.mkdir(parents=True, exist_ok=True)

    (out_root / "leave_episodes.jsonl").write_text(
        concat_jsonl([p / "leave_episodes.jsonl" for p in srcs]), encoding="utf-8"
    )
    (out_root / "leave_window_samples.jsonl").write_text(
        concat_jsonl([p / "leave_window_samples.jsonl" for p in srcs]), encoding="utf-8"
    )
    (out_root / "policy_history.jsonl").write_text(
        concat_jsonl([p / "policy_history.jsonl" for p in srcs]), encoding="utf-8"
    )

    write_theta(out_root / "theta.json", theta)
    shutil.copy2(ROOT / "config" / "anchors.json", out_root / "anchors.json")
    fp = ROOT / "config" / "company_radio_fingerprint.json"
    if fp.is_file():
        shutil.copy2(fp, out_root / "company_radio_fingerprint.json")

    policy_src = None
    if trigger_session and (trigger_session / "policy.json").is_file():
        policy_src = trigger_session / "policy.json"
    else:
        policy_src = next((p / "policy.json" for p in reversed(srcs) if (p / "policy.json").is_file()), None)
    if policy_src:
        shutil.copy2(policy_src, out_root / "policy.json")

    # Fresh per-invoke trails (append copies go to seq logs after agent).
    (out_root / "param_changes.jsonl").write_text("", encoding="utf-8")
    (out_root / "audit.jsonl").write_text("", encoding="utf-8")

    jobs: list[dict] = []
    last_push = None
    if trigger_session is not None:
        pushes = find_company_pushes(trigger_session)
        if pushes:
            last_push = pushes[-1]
            jobs.append(
                {
                    "reason": "AFTER_PUSH",
                    "intent": last_push.get("intent"),
                    "scene": last_push.get("scene"),
                    "t_push_ms": last_push.get("t_push_ms"),
                    "focus_side": "company",
                    "label": find_label_for_push(trigger_session, last_push.get("t_push_ms")),
                    "trigger_session": trigger_session.name,
                }
            )
            sess = trigger_session / "session"
            dest = out_root / "session"
            if dest.exists():
                shutil.rmtree(dest)
            if sess.is_dir():
                shutil.copytree(sess, dest)

    (out_root / "personalize_jobs.jsonl").write_text(
        "".join(json.dumps(j, ensure_ascii=False) + "\n" for j in jobs), encoding="utf-8"
    )

    ep_lines = [
        ln for ln in (out_root / "leave_episodes.jsonl").read_text(encoding="utf-8").splitlines() if ln.strip()
    ]
    labels = [json.loads(ln) for ln in ep_lines if json.loads(ln).get("type") == "label"]
    pushes = [json.loads(ln) for ln in ep_lines if json.loads(ln).get("type") == "push"]
    hist_n = sum(
        1 for ln in (out_root / "policy_history.jsonl").read_text(encoding="utf-8").splitlines() if ln.strip()
    )
    obs_n = sum(
        1
        for ln in (out_root / "policy_history.jsonl").read_text(encoding="utf-8").splitlines()
        if "obs_pdr_outbound" in ln
    )
    summary = {
        "n_sessions": len(srcs),
        "n_pushes": len(pushes),
        "n_labels": len(labels),
        "label_counts": dict(Counter(l.get("label") for l in labels)),
        "policy_history_lines": hist_n,
        "policy_history_obs_lines": obs_n,
        "jobs": jobs,
        "trigger_session": None if trigger_session is None else trigger_session.name,
    }
    return summary


def append_text(path: Path, text: str, header: str = "") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as fh:
        if header:
            fh.write(header)
        if text and not text.endswith("\n"):
            text += "\n"
        fh.write(text)


def run_agent(out_root: Path, env_file: Path, max_turn: int) -> int:
    out_posix = to_posix(out_root)
    env_posix = to_posix(env_file)
    root_posix = to_posix(ROOT)
    bash_cmd = (
        f"cd {root_posix} && "
        f"JIUWEN_ROOT=/mnt/d/bbpjiuwen "
        f"PERSONALIZER_MAX_TURN={max_turn} "
        f"bash examples/personalizer_llm/run.sh {env_posix} {out_posix} "
        f"--no-fixture --debug --max-turn {max_turn}"
    )
    if Path("/mnt/d/commute_scene_baseline").is_dir() and os.name != "nt":
        print("running agent in-place:", bash_cmd, flush=True)
        return subprocess.call(["bash", "-lc", bash_cmd])
    print("running agent via wsl:", bash_cmd, flush=True)
    return subprocess.call(["wsl", "-e", "bash", "-lc", bash_cmd])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root-win", default=r"D:\0812")
    ap.add_argument("--data-root-wsl", default="/mnt/d/0812")
    ap.add_argument("--tick-s", type=float, default=5.0)
    ap.add_argument("--group-gap-s", type=float, default=60.0)
    ap.add_argument("--out-root", default=str(ROOT / "output" / "real_0812_seq_agent_flow"))
    ap.add_argument("--session-out-prefix", default="real_0812_seq_")
    ap.add_argument("--env-file", default=str(ROOT / "sa_service" / "etc" / "agent.env"))
    ap.add_argument("--max-turn", type=int, default=80, help="Jiuwen ReAct maxTurn (raise if exhausted)")
    ap.add_argument("--skip-agent", action="store_true")
    ap.add_argument(
        "--seed-theta",
        default="",
        help="optional theta.json to start from (default: DEFAULT_THETA)",
    )
    ap.add_argument(
        "--max-groups",
        type=int,
        default=0,
        help="if >0, only replay N near-session groups (after --skip-groups)",
    )
    ap.add_argument(
        "--skip-groups",
        type=int,
        default=0,
        help="skip the first N near-session groups (chrono order)",
    )
    ap.add_argument(
        "--resume-from",
        default="",
        help="skip sessions before this folder name (inclusive start), e.g. 20260812_152909",
    )
    ap.add_argument(
        "--sessions",
        default="",
        help="comma-separated session folder names or HHMM prefixes to include "
        "(e.g. 20260812_105415,20260812_110128 or 1054,1101)",
    )
    args = ap.parse_args()

    data_root = resolve_data_root(args.data_root_win, args.data_root_wsl)
    sessions = list_sessions(data_root)
    # Baro groups from the full day, then optionally filter which sessions to replay.
    groups_all = group_near_sessions(sessions, args.group_gap_s)
    skip_g = max(0, int(args.skip_groups or 0))
    max_g = int(args.max_groups or 0)
    if skip_g or max_g:
        selected_groups = groups_all[skip_g:]
        if max_g > 0:
            selected_groups = selected_groups[:max_g]
        keep = {p.name for g in selected_groups for p in g}
        sessions = [s for s in sessions if s.name in keep]
        print(
            f"group_slice skip={skip_g} max={max_g or 'all'} -> {len(selected_groups)} groups",
            flush=True,
        )
    if args.sessions.strip():
        tokens = [t.strip() for t in args.sessions.split(",") if t.strip()]

        def wanted(name: str) -> bool:
            return any(tok in name for tok in tokens)

        sessions = [s for s in sessions if wanted(s.name)]
        if not sessions:
            print(f"ERROR: --sessions matched nothing: {tokens}", file=sys.stderr)
            return 1
    groups = group_near_sessions(sessions, args.group_gap_s)
    print(
        f"data_root={data_root} sessions={len(sessions)} "
        f"[{', '.join(s.name for s in sessions)}] groups={len(groups)} max_turn={args.max_turn}",
        flush=True,
    )

    baro_for: dict[str, Optional[Path]] = {}
    borrow_map: dict[str, str] = {}
    selected = {s.name for s in sessions}
    name_to_path = {p.name: p for g in groups_all for p in g}
    for g in groups_all:
        donor = pick_baro_donor(g)
        members = ", ".join(f"{p.name}(baro={baro_row_count(p)})" for p in g if p.name in selected)
        if not members:
            continue
        print(f" group: {members}  donor={None if donor is None else donor.name}", flush=True)
        for p in g:
            if p.name not in selected:
                continue
            own = baro_row_count(p)
            if own >= BARO_MIN_ROWS:
                baro_for[p.name] = None
            elif donor is not None:
                baro_for[p.name] = donor
                borrow_map[p.name] = donor.name
            else:
                baro_for[p.name] = None
                print(f"  WARN: {p.name} has no baro and no donor", flush=True)

    out_root = Path(args.out_root)
    if out_root.exists() and not args.resume_from:
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True, exist_ok=True)
    (out_root / "BARO_BORROW.json").write_text(
        json.dumps(borrow_map, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    seq_log = out_root / "SEQ_LOG.jsonl"
    audit_all = out_root / "audit_all.jsonl"
    params_all = out_root / "param_changes_all.jsonl"
    if not args.resume_from:
        for p in (seq_log, audit_all, params_all):
            p.write_text("", encoding="utf-8")

    current_theta = dict(DEFAULT_THETA)
    if args.seed_theta.strip():
        seed_path = Path(args.seed_theta)
        if not seed_path.is_file():
            print(f"ERROR: --seed-theta not found: {seed_path}", file=sys.stderr)
            return 1
        current_theta = load_theta(seed_path)
        print(f"seeded theta from {seed_path}", flush=True)
    theta_seed = out_root / "theta_live.json"
    write_theta(theta_seed, current_theta)

    completed: list[Path] = []
    failed: list[str] = []
    agent_runs = 0
    resume = args.resume_from.strip()
    skipping = bool(resume)

    # On resume, reload completed outs + theta
    if resume:
        live = out_root / "theta.json"
        if live.is_file():
            current_theta = load_theta(live)
            write_theta(theta_seed, current_theta)
        for raw in sessions:
            out = ROOT / "output" / f"{args.session_out_prefix}{raw.name}"
            if raw.name == resume:
                skipping = False
            if skipping and out.is_dir():
                completed.append(out)

    env_file = Path(args.env_file)
    if not args.skip_agent and not env_file.is_file():
        print(f"ERROR: missing {env_file}", file=sys.stderr)
        return 1

    for raw in sessions:
        if resume and skipping:
            if raw.name == resume:
                skipping = False
            else:
                continue

        out = ROOT / "output" / f"{args.session_out_prefix}{raw.name}"
        write_theta(theta_seed, current_theta)
        rc = replay_one(raw, out, args.tick_s, baro_for.get(raw.name), theta_seed)
        if rc != 0:
            failed.append(raw.name)
            print("FAILED replay", raw.name, "rc", rc, flush=True)
            append_text(
                seq_log,
                json.dumps({"session": raw.name, "event": "replay_failed", "rc": rc}, ensure_ascii=False) + "\n",
            )
            continue

        completed.append(out)
        pushes = find_company_pushes(out)
        summary = stage_workspace(completed, out_root, current_theta, out if pushes else None)
        print(
            f"staged n={summary['n_sessions']} pushes_cum={summary['n_pushes']} "
            f"labels={summary['label_counts']} hist_obs={summary['policy_history_obs_lines']} "
            f"this_pushes={len(pushes)}",
            flush=True,
        )

        step = {
            "session": raw.name,
            "event": "replay_ok",
            "n_pushes_session": len(pushes),
            "theta_enter_leave": current_theta.get("enter_leave"),
            "theta_w_wifi": current_theta.get("w_wifi"),
            "cumulative": summary,
        }
        append_text(seq_log, json.dumps(step, ensure_ascii=False) + "\n")

        if not pushes or args.skip_agent:
            continue

        print(
            f"\n======== agent AFTER_PUSH on {raw.name} "
            f"(history sessions={len(completed)}) ========",
            flush=True,
        )
        agent_runs += 1
        arc = run_agent(out_root, env_file, args.max_turn)
        print("agent rc", arc, flush=True)

        # Persist trails
        audit = out_root / "audit.jsonl"
        params = out_root / "param_changes.jsonl"
        if audit.is_file() and audit.stat().st_size:
            append_text(audit_all, audit.read_text(encoding="utf-8"), header=f"# {raw.name}\n")
            print("----- audit -----", flush=True)
            print(audit.read_text(encoding="utf-8"), flush=True)
        if params.is_file() and params.stat().st_size:
            append_text(params_all, params.read_text(encoding="utf-8"), header=f"# {raw.name}\n")
            print("----- param_changes -----", flush=True)
            print(params.read_text(encoding="utf-8"), flush=True)

        # Carry theta forward
        th_path = out_root / "theta.json"
        if th_path.is_file():
            current_theta = load_theta(th_path)
            write_theta(theta_seed, current_theta)
        # Snapshot after this agent step
        snap = out_root / "snapshots"
        snap.mkdir(exist_ok=True)
        shutil.copy2(th_path, snap / f"theta_after_{raw.name}.json")
        if audit.is_file():
            shutil.copy2(audit, snap / f"audit_{raw.name}.jsonl")
        trace = out_root / "agent_trace.jsonl"
        if trace.is_file() and trace.stat().st_size:
            shutil.copy2(trace, snap / f"agent_trace_{raw.name}.jsonl")

        append_text(
            seq_log,
            json.dumps(
                {
                    "session": raw.name,
                    "event": "agent_done",
                    "rc": arc,
                    "enter_leave": current_theta.get("enter_leave"),
                    "w_wifi": current_theta.get("w_wifi"),
                    "arm_delay_s": current_theta.get("arm_delay_s"),
                    "w_baro": current_theta.get("w_baro"),
                },
                ensure_ascii=False,
            )
            + "\n",
        )

        if arc != 0:
            print("WARN: agent non-zero; continuing with current theta", flush=True)

    # Final cumulative stage without a new job
    if completed:
        stage_workspace(completed, out_root, current_theta, None)
        write_theta(out_root / "theta.json", current_theta)

    final = {
        "n_sessions_ok": len(completed),
        "failed": failed,
        "agent_runs": agent_runs,
        "borrow_map": borrow_map,
        "final_theta": {
            k: current_theta.get(k)
            for k in (
                "enter_leave",
                "arm_delay_s",
                "w_walk",
                "w_pdr",
                "w_geo",
                "w_wifi",
                "w_cell",
                "w_ble",
                "w_time",
                "w_baro",
            )
        },
    }
    (out_root / "SEQ_SUMMARY.json").write_text(
        json.dumps(final, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print("\n======== SEQ DONE ========", flush=True)
    print(json.dumps(final, ensure_ascii=False, indent=2), flush=True)
    return 0 if not failed else 1


if __name__ == "__main__":
    raise SystemExit(main())
