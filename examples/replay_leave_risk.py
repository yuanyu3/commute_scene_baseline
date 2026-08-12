#!/usr/bin/env python3
"""Replay a trained risk model through the HSMM and emit per-tick timelines."""

from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from commute_baseline.hsmm import LeaveHsmm, LeaveObservation, LeavePhase  # noqa: E402
from commute_baseline.leave_risk import PortableRiskModel, load_event_rows  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root", default=r"D:\huawei\data\0811")
    ap.add_argument("--groups", default=str(ROOT / "config" / "0811_event_groups.json"))
    ap.add_argument("--fingerprint", default=str(ROOT / "config" / "company_radio_fingerprint.json"))
    ap.add_argument("--model", default=str(ROOT / "output" / "leave_risk_v2" / "leave_risk_model.json"))
    ap.add_argument("--out", default=str(ROOT / "output" / "leave_risk_v2" / "replay"))
    ap.add_argument("--risk-threshold", type=float)
    ap.add_argument("--consecutive", type=int)
    args = ap.parse_args()
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    model = PortableRiskModel.load(args.model)
    model_body = json.loads(Path(args.model).read_text(encoding="utf-8"))
    risk_threshold = args.risk_threshold or float(model_body.get("report", {}).get("risk_threshold", 0.60))
    consecutive = args.consecutive or int(model_body.get("report", {}).get("consecutive_ticks", 3))
    rows = load_event_rows(Path(args.data_root), Path(args.groups), Path(args.fingerprint))
    sessions = defaultdict(list)
    for row in rows:
        sessions[row.session].append(row)
    summary = []
    for session, samples in sorted(sessions.items()):
        samples.sort(key=lambda r: r.t_ms)
        hsmm = LeaveHsmm(); streak = 0; push = None; timeline = []
        for row in samples:
            risk = model.predict(row.features)
            result = hsmm.step(LeaveObservation(
                walking=row.features.get("walking", 0.0), risk_available=True,
                risk_30s=risk[30], risk_60s=risk[60], risk_120s=risk[120],
                relation_known=True, inside=True,
            ), datetime.fromtimestamp(row.t_ms / 1000, tz=timezone.utc), {"w_risk": 1.2})
            smooth = result.probability[LeavePhase.PRE_LEAVE] + result.probability[LeavePhase.LEAVING]
            eligible = risk[120] >= risk_threshold and smooth >= 0.50 and row.features.get("walking", 0) > 0
            streak = streak + 1 if eligible else 0
            should_push = push is None and streak >= consecutive
            if should_push:
                push = row.t_ms
            timeline.append({
                "t_ms": row.t_ms,
                "risk_30s": round(risk[30], 6), "risk_60s": round(risk[60], 6),
                "risk_120s": round(risk[120], 6), "hsmm_preleave": round(result.probability[LeavePhase.PRE_LEAVE], 6),
                "hsmm_leaving": round(result.probability[LeavePhase.LEAVING], 6),
                "eligible": eligible, "push": should_push,
            })
        truth = samples[0].truth_t_ms
        item = {"session": session, "group_id": samples[0].group_id, "label": samples[0].label,
                "route": samples[0].route, "push_t_ms": push, "truth_t_ms": truth,
                "lead_s": (truth - push) / 1000 if truth and push else None}
        summary.append(item)
        (out / f"{session}.json").write_text(json.dumps({"summary": item, "timeline": timeline}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (out / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    with (out / "summary.csv").open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=list(summary[0]))
        writer.writeheader(); writer.writerows(summary)
    for item in summary:
        print(item)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
