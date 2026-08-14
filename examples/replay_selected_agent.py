#!/usr/bin/env python3
"""Replay the user-selected dumps, then merge a product dir for personalizer_llm."""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SESSIONS = [
    (Path("/mnt/d/0812/20260812_110128"), Path(r"D:\0812\20260812_110128")),
    (Path("/mnt/d/0811/20260811_164938"), Path(r"D:\0811\20260811_164938")),
    (Path("/mnt/d/0812/20260812_153840"), Path(r"D:\0812\20260812_153840")),
]
OUT_ROOT = ROOT / "output" / "real_selected_agent_flow"


def resolve_raw(wsl: Path, win: Path) -> Path:
    if wsl.is_dir():
        return wsl
    if win.is_dir():
        return win
    raise FileNotFoundError(f"missing dump {wsl} / {win}")


def replay() -> list[Path]:
    py = sys.executable
    script = ROOT / "examples" / "run_real_flow.py"
    outs: list[Path] = []
    failed = []
    for wsl, win in SESSIONS:
        raw = resolve_raw(wsl, win)
        sid = raw.name
        out = ROOT / "output" / f"real_{sid}"
        cmd = [
            py,
            str(script),
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
        ]
        print("\n========", sid, "========", flush=True)
        print(" ".join(cmd), flush=True)
        rc = subprocess.call(cmd)
        if rc != 0:
            failed.append(sid)
            print("FAILED", sid, "rc", rc, flush=True)
        else:
            outs.append(out)
    if failed:
        raise SystemExit(f"replay failed: {failed}")
    return outs


def concat_jsonl(paths: list[Path]) -> str:
    lines: list[str] = []
    for path in paths:
        if not path.is_file():
            continue
        lines.extend(ln for ln in path.read_text(encoding="utf-8").splitlines() if ln.strip())
    return "\n".join(lines) + ("\n" if lines else "")


def merge(srcs: list[Path]) -> None:
    if OUT_ROOT.exists():
        shutil.rmtree(OUT_ROOT)
    OUT_ROOT.mkdir(parents=True)
    (OUT_ROOT / "leave_episodes.jsonl").write_text(
        concat_jsonl([p / "leave_episodes.jsonl" for p in srcs]), encoding="utf-8"
    )
    (OUT_ROOT / "leave_window_samples.jsonl").write_text(
        concat_jsonl([p / "leave_window_samples.jsonl" for p in srcs]), encoding="utf-8"
    )
    (OUT_ROOT / "policy_history.jsonl").write_text(
        concat_jsonl([p / "policy_history.jsonl" for p in srcs]), encoding="utf-8"
    )
    shutil.copy2(srcs[-1] / "theta.json", OUT_ROOT / "theta.json")
    shutil.copy2(ROOT / "config" / "anchors.json", OUT_ROOT / "anchors.json")
    if (srcs[-1] / "policy.json").is_file():
        shutil.copy2(srcs[-1] / "policy.json", OUT_ROOT / "policy.json")
    fp = ROOT / "config" / "company_radio_fingerprint.json"
    if fp.is_file():
        shutil.copy2(fp, OUT_ROOT / "company_radio_fingerprint.json")
    (OUT_ROOT / "param_changes.jsonl").write_text("", encoding="utf-8")
    (OUT_ROOT / "audit.jsonl").write_text("", encoding="utf-8")

    ep_lines = (OUT_ROOT / "leave_episodes.jsonl").read_text(encoding="utf-8").splitlines()
    last_company_push = None
    for ln in ep_lines:
        obj = json.loads(ln)
        if obj.get("type") == "push" and obj.get("scene") == "LEAVING_COMPANY":
            last_company_push = obj
    session_src = srcs[-1] / "session"
    if last_company_push:
        for src in reversed(srcs):
            eps = src / "leave_episodes.jsonl"
            if not eps.is_file():
                continue
            if any(
                json.loads(ln).get("t_push_ms") == last_company_push.get("t_push_ms")
                for ln in eps.read_text(encoding="utf-8").splitlines()
                if ln.strip()
            ):
                session_src = src / "session"
                break
    if session_src.is_dir():
        shutil.copytree(session_src, OUT_ROOT / "session")

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
    (OUT_ROOT / "personalize_jobs.jsonl").write_text(
        "".join(json.dumps(j, ensure_ascii=False) + "\n" for j in jobs), encoding="utf-8"
    )
    labels = [json.loads(ln) for ln in ep_lines if json.loads(ln).get("type") == "label"]
    hist_n = sum(
        1
        for ln in (OUT_ROOT / "policy_history.jsonl").read_text(encoding="utf-8").splitlines()
        if ln.strip()
    )
    print("\n======== merge ========", flush=True)
    print("OUT", OUT_ROOT)
    print("episode_lines", len(ep_lines), "policy_history", hist_n)
    print("labels", dict(Counter(l.get("label") for l in labels)))
    print("leads", [l.get("lead_s") for l in labels])
    print("last_push", None if last_company_push is None else last_company_push.get("t_push_ms"))


def main() -> int:
    srcs = replay()
    merge(srcs)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
