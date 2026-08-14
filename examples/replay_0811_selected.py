#!/usr/bin/env python3
"""Replay a few representative 0811 sessions through SceneEngine."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SESSIONS = [
    # morning elevator leave: longest, GNSS + baro
    "20260811_105613",
    # afternoon elevator leave with baro
    "20260811_151051",
    # walk downstairs leave
    "20260811_165659",
    # indoor fifth-floor walk (negative)
    "20260811_163706",
]

def main() -> int:
    py = sys.executable
    script = ROOT / "examples" / "run_real_flow.py"
    failed = []
    for sid in SESSIONS:
        raw = Path("/mnt/d/0811") / sid
        if not raw.is_dir():
            raw = Path(r"D:\0811") / sid
        out = ROOT / "output" / f"real_0811_{sid}"
        cmd = [
            py,
            str(script),
            "--raw-dir", str(raw),
            "--sensor-dir", str(raw),
            "--out-dir", str(out),
            "--anchors", str(ROOT / "config" / "anchors.json"),
            "--company-radio-fingerprint", str(ROOT / "config" / "company_radio_fingerprint.json"),
            "--location-crs", "GCJ02",
        ]
        print("\n========", sid, "========", flush=True)
        print(" ".join(cmd), flush=True)
        rc = subprocess.call(cmd)
        if rc != 0:
            failed.append(sid)
            print("FAILED", sid, "rc", rc, flush=True)
    print("\n======== done ========", flush=True)
    print("ok", [s for s in SESSIONS if s not in failed])
    print("failed", failed)
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
