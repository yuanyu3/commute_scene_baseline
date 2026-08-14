#!/usr/bin/env python3
"""Merge 0811 company-leave replays (with baro) into one product dir for personalizer_llm."""
from __future__ import annotations

import json
import shutil
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output" / "real_0811_agent_flow"
# True leaves that have pressure samples. 163706/165659 are the same events
# without baro and are left out so the agent is not trained on missing-sensor noise.
SESSIONS = [
    "20260811_105613",  # morning elevator, CONFIRMED_LEAVE, lead ~76s
    "20260811_151051",  # afternoon elevator, CONFIRMED_LEAVE, lead ~160s
    "20260811_165701",  # walk downstairs, CONFIRMED_LEAVE, lead ~121s
]


def concat_jsonl(paths: list[Path]) -> str:
    lines: list[str] = []
    for path in paths:
        if not path.is_file():
            continue
        lines.extend(ln for ln in path.read_text(encoding="utf-8").splitlines() if ln.strip())
    return "\n".join(lines) + ("\n" if lines else "")


def main() -> int:
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    srcs = [ROOT / "output" / f"real_0811_{sid}" for sid in SESSIONS]
    missing = [str(p) for p in srcs if not p.is_dir()]
    if missing:
        print("missing replay dirs", missing)
        return 1

    (OUT / "leave_episodes.jsonl").write_text(
        concat_jsonl([p / "leave_episodes.jsonl" for p in srcs]), encoding="utf-8"
    )
    (OUT / "leave_window_samples.jsonl").write_text(
        concat_jsonl([p / "leave_window_samples.jsonl" for p in srcs]), encoding="utf-8"
    )
    (OUT / "policy_history.jsonl").write_text(
        concat_jsonl([p / "policy_history.jsonl" for p in srcs]), encoding="utf-8"
    )

    shutil.copy2(srcs[-1] / "theta.json", OUT / "theta.json")
    shutil.copy2(ROOT / "config" / "anchors.json", OUT / "anchors.json")
    if (srcs[-1] / "policy.json").is_file():
        shutil.copy2(srcs[-1] / "policy.json", OUT / "policy.json")
    fp = ROOT / "config" / "company_radio_fingerprint.json"
    if fp.is_file():
        shutil.copy2(fp, OUT / "company_radio_fingerprint.json")

    (OUT / "param_changes.jsonl").write_text("", encoding="utf-8")
    (OUT / "audit.jsonl").write_text("", encoding="utf-8")

    last_session = srcs[-1] / "session"
    if last_session.is_dir():
        shutil.copytree(last_session, OUT / "session")

    ep_lines = (OUT / "leave_episodes.jsonl").read_text(encoding="utf-8").splitlines()
    last_company_push = None
    for ln in ep_lines:
        obj = json.loads(ln)
        if obj.get("type") == "push" and obj.get("scene") == "LEAVING_COMPANY":
            last_company_push = obj
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
    (OUT / "personalize_jobs.jsonl").write_text(
        "".join(json.dumps(j, ensure_ascii=False) + "\n" for j in jobs), encoding="utf-8"
    )

    labels = [json.loads(ln) for ln in ep_lines if json.loads(ln).get("type") == "label"]
    hist_n = sum(
        1 for ln in (OUT / "policy_history.jsonl").read_text(encoding="utf-8").splitlines() if ln.strip()
    )
    print("OUT", OUT)
    print("sessions", SESSIONS)
    print("episode_lines", len(ep_lines), "policy_history", hist_n)
    print("labels", dict(Counter(l.get("label") for l in labels)))
    print("leads", [l.get("lead_s") for l in labels])
    print("last_push", None if last_company_push is None else last_company_push.get("t_push_ms"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
