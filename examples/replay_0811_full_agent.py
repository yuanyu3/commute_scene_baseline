#!/usr/bin/env python3
"""Full D:/0811 replay (5s time-axis) → merge historical set → personalizer_llm Agent.

Steps:
  1) Discover session dirs under --data-root
  2) run_real_flow.py each with --tick-s 5
  3) Merge leave_episodes / policy_history / samples into one product root
  4) Seed AFTER_PUSH job from last company push; reset theta to defaults
  5) Optionally invoke personalizer_llm (WSL) so constrained replay tools see the full set
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Optional

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from commute_baseline.engine import DEFAULT_THETA  # noqa: E402


def resolve_data_root(win: str, wsl: str) -> Path:
    wsl_p = Path(wsl)
    win_p = Path(win)
    if wsl_p.is_dir():
        return wsl_p
    if win_p.is_dir():
        return win_p
    raise FileNotFoundError(f"missing data root {wsl} / {win}")


def list_sessions(data_root: Path) -> list[Path]:
    out: list[Path] = []
    for p in sorted(data_root.iterdir()):
        if not p.is_dir():
            continue
        if not p.name.startswith("2026"):
            continue
        has_loc = any(p.glob("location_data_*.csv"))
        has_sensor = (p / "sensor_events.csv").is_file()
        if has_loc or has_sensor:
            out.append(p)
    return out


def write_theta(path: Path, theta: dict) -> None:
    body = {"coordinate_system": "WGS84", **theta}
    path.write_text(json.dumps(body, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def concat_jsonl(paths: list[Path]) -> str:
    lines: list[str] = []
    for path in paths:
        if not path.is_file():
            continue
        lines.extend(ln for ln in path.read_text(encoding="utf-8").splitlines() if ln.strip())
    return "\n".join(lines) + ("\n" if lines else "")


def replay_one(raw: Path, out: Path, tick_s: float) -> int:
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
    ]
    print("\n========", raw.name, "========", flush=True)
    print(" ".join(cmd), flush=True)
    return subprocess.call(cmd)


def merge(srcs: list[Path], out_root: Path) -> dict:
    if out_root.exists():
        shutil.rmtree(out_root)
    out_root.mkdir(parents=True)

    (out_root / "leave_episodes.jsonl").write_text(
        concat_jsonl([p / "leave_episodes.jsonl" for p in srcs]), encoding="utf-8"
    )
    (out_root / "leave_window_samples.jsonl").write_text(
        concat_jsonl([p / "leave_window_samples.jsonl" for p in srcs]), encoding="utf-8"
    )
    (out_root / "policy_history.jsonl").write_text(
        concat_jsonl([p / "policy_history.jsonl" for p in srcs]), encoding="utf-8"
    )

    # Fresh default θ so Agent starts from baseline, not a prior personalizer commit.
    write_theta(out_root / "theta.json", dict(DEFAULT_THETA))
    shutil.copy2(ROOT / "config" / "anchors.json", out_root / "anchors.json")
    fp = ROOT / "config" / "company_radio_fingerprint.json"
    if fp.is_file():
        shutil.copy2(fp, out_root / "company_radio_fingerprint.json")
    policy_src = next((p / "policy.json" for p in reversed(srcs) if (p / "policy.json").is_file()), None)
    if policy_src:
        shutil.copy2(policy_src, out_root / "policy.json")

    (out_root / "param_changes.jsonl").write_text("", encoding="utf-8")
    (out_root / "audit.jsonl").write_text("", encoding="utf-8")

    ep_lines = [
        ln for ln in (out_root / "leave_episodes.jsonl").read_text(encoding="utf-8").splitlines() if ln.strip()
    ]
    last_company_push = None
    last_push_src: Optional[Path] = None
    for src in srcs:
        eps = src / "leave_episodes.jsonl"
        if not eps.is_file():
            continue
        for ln in eps.read_text(encoding="utf-8").splitlines():
            if not ln.strip():
                continue
            obj = json.loads(ln)
            if obj.get("type") == "push" and obj.get("scene") == "LEAVING_COMPANY":
                last_company_push = obj
                last_push_src = src

    session_src = (last_push_src or srcs[-1]) / "session"
    if session_src.is_dir():
        shutil.copytree(session_src, out_root / "session")

    jobs = []
    if last_company_push:
        jobs.append(
            {
                "reason": "AFTER_PUSH",
                "intent": last_company_push.get("intent"),
                "scene": last_company_push.get("scene"),
                "t_push_ms": last_company_push.get("t_push_ms"),
                "focus_side": "company",
            }
        )
    (out_root / "personalize_jobs.jsonl").write_text(
        "".join(json.dumps(j, ensure_ascii=False) + "\n" for j in jobs), encoding="utf-8"
    )

    labels = [json.loads(ln) for ln in ep_lines if json.loads(ln).get("type") == "label"]
    pushes = [json.loads(ln) for ln in ep_lines if json.loads(ln).get("type") == "push"]
    hist_n = sum(
        1
        for ln in (out_root / "policy_history.jsonl").read_text(encoding="utf-8").splitlines()
        if ln.strip()
    )
    # Count HSMM-replayable episodes (obs_* rows grouped later by eval).
    obs_n = sum(
        1
        for ln in (out_root / "policy_history.jsonl").read_text(encoding="utf-8").splitlines()
        if "obs_pdr_outbound" in ln
    )
    summary = {
        "out": str(out_root),
        "n_sessions_merged": len(srcs),
        "n_episode_lines": len(ep_lines),
        "n_pushes": len(pushes),
        "n_labels": len(labels),
        "label_counts": dict(Counter(l.get("label") for l in labels)),
        "leads": [l.get("lead_s") for l in labels if l.get("label") == "CONFIRMED_LEAVE"],
        "policy_history_lines": hist_n,
        "policy_history_obs_lines": obs_n,
        "last_push_ms": None if last_company_push is None else last_company_push.get("t_push_ms"),
        "last_push_session": None if last_push_src is None else last_push_src.name,
        "jobs": jobs,
    }
    (out_root / "MERGE_SUMMARY.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print("\n======== merge ========", flush=True)
    for k, v in summary.items():
        print(f"{k}: {v}", flush=True)
    return summary


def run_agent(out_root: Path, env_file: Path) -> int:
    """Invoke WSL personalizer_llm against the merged product dir."""
    # Prefer running inside WSL when cwd is already Linux; else wrap with wsl.
    out_posix = str(out_root).replace("\\", "/")
    if out_posix.startswith("D:/") or out_posix.startswith("D:"):
        out_posix = "/mnt/d/" + out_posix[3:].lstrip("/\\").replace("\\", "/")
    elif len(out_posix) >= 2 and out_posix[1] == ":":
        drive = out_posix[0].lower()
        out_posix = f"/mnt/{drive}/" + out_posix[3:].lstrip("/\\").replace("\\", "/")

    env_posix = str(env_file).replace("\\", "/")
    if env_posix.startswith("D:/") or env_posix.startswith("D:"):
        env_posix = "/mnt/d/" + env_posix[3:].lstrip("/\\").replace("\\", "/")

    root_posix = str(ROOT).replace("\\", "/")
    if root_posix.startswith("D:/") or root_posix.startswith("D:"):
        root_posix = "/mnt/d/" + root_posix[3:].lstrip("/\\").replace("\\", "/")

    bash_cmd = (
        f"cd {root_posix} && "
        f"JIUWEN_ROOT=/mnt/d/bbpjiuwen "
        f"bash examples/personalizer_llm/run.sh {env_posix} {out_posix} --no-fixture --debug"
    )
    if Path("/mnt/d/commute_scene_baseline").is_dir() and os.name != "nt":
        print("running agent in-place:", bash_cmd, flush=True)
        return subprocess.call(["bash", "-lc", bash_cmd])

    print("running agent via wsl:", bash_cmd, flush=True)
    return subprocess.call(["wsl", "-e", "bash", "-lc", bash_cmd])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root-win", default=r"D:\0811")
    ap.add_argument("--data-root-wsl", default="/mnt/d/0811")
    ap.add_argument("--tick-s", type=float, default=5.0)
    ap.add_argument(
        "--out-root",
        default=str(ROOT / "output" / "real_0811_full_agent_flow"),
        help="merged product dir for Agent",
    )
    ap.add_argument(
        "--session-out-prefix",
        default="real_0811_",
        help="per-session output under output/{prefix}{sid}",
    )
    ap.add_argument("--skip-replay", action="store_true", help="only merge existing session outs")
    ap.add_argument("--skip-agent", action="store_true", help="replay+merge only")
    ap.add_argument(
        "--only-with-push",
        action="store_true",
        help="merge only sessions that produced a push (still replay all unless --skip-replay)",
    )
    ap.add_argument(
        "--env-file",
        default=str(ROOT / "sa_service" / "etc" / "agent.env"),
    )
    args = ap.parse_args()

    data_root = resolve_data_root(args.data_root_win, args.data_root_wsl)
    sessions = list_sessions(data_root)
    print(f"data_root={data_root} sessions={len(sessions)}", flush=True)
    for s in sessions:
        print(" ", s.name, flush=True)

    failed: list[str] = []
    outs: list[Path] = []
    for raw in sessions:
        out = ROOT / "output" / f"{args.session_out_prefix}{raw.name}"
        if not args.skip_replay:
            rc = replay_one(raw, out, args.tick_s)
            if rc != 0:
                failed.append(raw.name)
                print("FAILED", raw.name, "rc", rc, flush=True)
                continue
        if out.is_dir():
            outs.append(out)

    if failed:
        print("replay failed:", failed, flush=True)

    merge_srcs = outs
    if args.only_with_push:
        kept = []
        for p in outs:
            eps = p / "leave_episodes.jsonl"
            if not eps.is_file():
                continue
            if any(
                '"type": "push"' in ln or '"type":"push"' in ln
                for ln in eps.read_text(encoding="utf-8").splitlines()
            ):
                kept.append(p)
        merge_srcs = kept
        print(f"merge filter only_with_push: {len(merge_srcs)}/{len(outs)}", flush=True)

    if not merge_srcs:
        print("ERROR: nothing to merge", file=sys.stderr)
        return 1

    out_root = Path(args.out_root)
    summary = merge(merge_srcs, out_root)
    if summary["n_pushes"] == 0:
        print("WARN: no pushes in merged set; Agent AFTER_PUSH job empty", flush=True)

    if args.skip_agent:
        return 0 if not failed else 1

    env_file = Path(args.env_file)
    if not env_file.is_file():
        print(f"ERROR: missing {env_file}", file=sys.stderr)
        return 1
    rc = run_agent(out_root, env_file)
    print("agent rc", rc, flush=True)
    audit = out_root / "audit.jsonl"
    if audit.is_file():
        print("----- audit -----", flush=True)
        print(audit.read_text(encoding="utf-8"), flush=True)
    params = out_root / "param_changes.jsonl"
    if params.is_file() and params.stat().st_size:
        print("----- param_changes -----", flush=True)
        print(params.read_text(encoding="utf-8"), flush=True)
    return 0 if rc == 0 and not failed else 1


if __name__ == "__main__":
    raise SystemExit(main())
