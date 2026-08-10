#!/usr/bin/env python3
"""Merge real-sensor leave-company replays into a product dir for personalizer_llm."""

from __future__ import annotations

import json
import shutil
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "output" / "leave_company_real_sensor_best" / "iter0"
OUT = ROOT / "output" / "leave_company_agent_data"
SESSIONS = [
    "20260804_174813",
    "20260804_195251",
    "20260805_162326",
    "20260810_114452",
    "20260810_114956",
]


def main() -> int:
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    ep_lines: list[str] = []
    sp_lines: list[str] = []
    for name in SESSIONS:
        d = SRC / name
        ep = d / "leave_episodes.jsonl"
        sp = d / "leave_window_samples.jsonl"
        if ep.exists():
            ep_lines.extend(ln for ln in ep.read_text(encoding="utf-8").splitlines() if ln.strip())
        if sp.exists():
            sp_lines.extend(ln for ln in sp.read_text(encoding="utf-8").splitlines() if ln.strip())

        sess_src = d / "session"
        # Flat session dirs under product root (EvidenceQuery PickLatestSessionDir)
        sess_dst = OUT / f"{name}_session"
        if sess_src.exists():
            sess_dst.mkdir(parents=True, exist_ok=True)
            for f in sess_src.iterdir():
                if f.is_file():
                    shutil.copy2(f, sess_dst / f.name)

        # Also copy raw wifi/cell/mag/location from data if present
        raw = Path(r"D:\huawei\data") / name
        for prefix in ("wifi_data", "cell_data", "mag_data", "location_data"):
            for f in sorted(raw.glob(f"{prefix}_*.csv"))[:8]:
                dst = sess_dst / f.name
                if not dst.exists():
                    try:
                        shutil.copy2(f, dst)
                    except OSError:
                        pass

    (OUT / "leave_episodes.jsonl").write_text("\n".join(ep_lines) + "\n", encoding="utf-8")
    (OUT / "leave_window_samples.jsonl").write_text("\n".join(sp_lines) + "\n", encoding="utf-8")

    theta_path = ROOT / "output" / "leave_company_real_sensor" / "_seed_theta.json"
    theta = json.loads(theta_path.read_text(encoding="utf-8"))
    theta["focus_side"] = "company"
    (OUT / "theta.json").write_text(json.dumps(theta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    shutil.copy2(ROOT / "config" / "anchors.json", OUT / "anchors.json")
    (OUT / "param_changes.jsonl").write_text("", encoding="utf-8")
    (OUT / "audit.jsonl").write_text("", encoding="utf-8")

    last_company_push = None
    for ln in ep_lines:
        o = json.loads(ln)
        if o.get("type") == "push" and o.get("scene") == "LEAVING_COMPANY":
            last_company_push = o

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
        "".join(json.dumps(j, ensure_ascii=False) + "\n" for j in jobs),
        encoding="utf-8",
    )

    labels = [json.loads(ln) for ln in ep_lines if json.loads(ln).get("type") == "label"]
    print("OUT", OUT)
    print("episode_lines", len(ep_lines), "sample_lines", len(sp_lines))
    print("labels", dict(Counter(l.get("label") for l in labels)))
    print(
        "company_pushes",
        sum(1 for ln in ep_lines if '"type": "push"' in ln and "LEAVING_COMPANY" in ln),
    )
    print("enter_leave", theta.get("enter_leave"), "focus_side", theta.get("focus_side"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
