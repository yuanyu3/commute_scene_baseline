#!/usr/bin/env python3
"""Build one local company radio fingerprint from offline sessions.

Only radio samples temporally close to a GPS point inside the configured
company inner fence are used. Device identifiers must recur across sessions,
so transient phones and one-off scans do not become part of the profile.
"""

from __future__ import annotations

import argparse
import bisect
import json
import statistics
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from commute_baseline.anchors import load_anchors  # noqa: E402
from commute_baseline.geo import haversine_m  # noqa: E402
from commute_baseline.io_data import load_location_csv_dir, load_sensor_gps, merge_gps  # noqa: E402
from commute_baseline.radio_evidence import load_ble_samples, load_cell_samples, load_wifi_scans  # noqa: E402


def nearby_inside(t_ms: int, gps, gps_ms, anchor, max_gap_ms: int) -> bool:
    i = bisect.bisect_left(gps_ms, t_ms)
    for j in (i - 1, i):
        if 0 <= j < len(gps) and abs(gps_ms[j] - t_ms) <= max_gap_ms:
            p = gps[j]
            if haversine_m(p.lat, p.lon, anchor.lat, anchor.lon) <= anchor.r_in_m:
                return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root", action="append", help="repeat for each offline data root")
    ap.add_argument("--anchors", default=str(ROOT / "config" / "anchors.json"))
    ap.add_argument("--out", default=str(ROOT / "config" / "company_radio_fingerprint.json"))
    ap.add_argument("--nearby-gps-s", type=float, default=45.0)
    ap.add_argument("--min-session-ratio", type=float, default=0.15)
    ap.add_argument("--min-sessions", type=int, default=5)
    ap.add_argument(
        "--dwell-session",
        action="append",
        help="repeat for each known dwell session at the target workplace floor",
    )
    ap.add_argument(
        "--floor-label",
        help="optional semantic scope stored in the fingerprint, for example floor_5",
    )
    ap.add_argument("--top-k", type=int, default=12, help="number of WiFi APs in a dwell fingerprint")
    ap.add_argument("--cell-top-k", type=int, default=8, help="maximum recurring company cell IDs")
    ap.add_argument(
        "--preserve-cell",
        action="store_true",
        help="when rebuilding floor WiFi, preserve company.cell from the existing --out file",
    )
    ap.add_argument("--include-ble", action="store_true", help="include BLE; disabled by default for site fingerprints")
    args = ap.parse_args()
    if not args.data_root and not args.dwell_session:
        ap.error("provide --dwell-session or at least one --data-root")

    company = load_anchors(args.anchors).company
    wifi_sessions: Counter[str] = Counter()
    wifi_scans: Counter[str] = Counter()
    cell_sessions: Counter[int] = Counter()
    cell_samples: Counter[int] = Counter()
    ble_sessions: Counter[str] = Counter()
    rssi: dict[str, list[int]] = {}
    accepted = []
    gap_ms = int(args.nearby_gps_s * 1000)

    if args.dwell_session:
        sessions = [Path(x) for x in args.dwell_session]
        for session in sessions:
            if not session.is_dir():
                raise SystemExit(f"dwell session not found: {session}")

        # These sessions are explicitly labelled dwell data. GPS is still used
        # as a coarse company guard, but it must not expand the profile to other
        # floors or to elevator/entrance portions of a route.
        ap_seen = Counter()
        ap_sessions = Counter()
        ap_rssi: dict[str, list[int]] = {}
        cell_counts: Counter[int] = Counter()
        cell_session_counts: Counter[int] = Counter()
        accepted_dwell = []
        scan_count = 0
        for session in sessions:
            gps = merge_gps(
                load_location_csv_dir(str(session)),
                load_sensor_gps(str(session)) if (session / "sensor_events.csv").is_file() else [],
            )
            if not gps:
                raise SystemExit(f"dwell session has no GPS: {session}")
            gps_ms = [int(p.t.timestamp() * 1000) for p in gps]
            scans = [
                s for s in load_wifi_scans([str(session)])
                if nearby_inside(s.t_ms, gps, gps_ms, company, gap_ms)
            ]
            cells = [
                x for x in load_cell_samples([str(session)])
                if x.cell_id and nearby_inside(x.t_ms, gps, gps_ms, company, gap_ms)
            ]
            if not scans and not cells:
                raise SystemExit(f"dwell session has no company-nearby WiFi/Cell samples: {session}")
            accepted_dwell.append(session.name)
            scan_count += len(scans)
            session_aps = set()
            for scan in scans:
                per_scan = set()
                for apx in scan.aps:
                    if apx.bssid and apx.rssi >= -85:
                        b = apx.bssid.lower()
                        per_scan.add(b)
                        session_aps.add(b)
                        ap_rssi.setdefault(b, []).append(apx.rssi)
                ap_seen.update(per_scan)
            ap_sessions.update(session_aps)
            per_session_cells = {x.cell_id for x in cells}
            cell_session_counts.update(per_session_cells)
            cell_counts.update(x.cell_id for x in cells)

        # Rank by recurrence across the explicitly selected floor-dwell
        # sessions, then scan recurrence and median RSSI.
        ranked = sorted(
            ap_seen,
            key=lambda b: (
                ap_sessions[b],
                ap_seen[b],
                statistics.median(ap_rssi.get(b, [-127])),
                b,
            ),
            reverse=True,
        )[: max(1, args.top_k)]
        ranked_cells = sorted(
            cell_counts,
            key=lambda x: (cell_session_counts[x], cell_counts[x], x),
            reverse=True,
        )[: max(1, args.cell_top_k)]
        cell_body = {
            "cell_ids": ranked_cells,
            "session_counts": {str(x): cell_session_counts[x] for x in ranked_cells},
            "sample_counts": {str(x): cell_counts[x] for x in ranked_cells},
        }
        if args.preserve_cell:
            existing_out = Path(args.out)
            if not existing_out.is_file():
                raise SystemExit(f"cannot preserve cell; output fingerprint not found: {existing_out}")
            existing = json.loads(existing_out.read_text(encoding="utf-8"))
            cell_body = existing.get("company", {}).get("cell")
            if not isinstance(cell_body, dict):
                raise SystemExit(f"cannot preserve cell; company.cell missing in: {existing_out}")
        body = {
            "version": 2,
            "site": "company_001",
            "mode": "known_floor_dwell_topk" if args.floor_label else "known_company_dwell_topk",
            "built_at": datetime.now(timezone.utc).isoformat(),
            "source_sessions": accepted_dwell,
            "selection": {
                "floor_label": args.floor_label,
                "top_k": args.top_k,
                "cell_top_k": args.cell_top_k,
                "rssi_min": -85,
                "nearby_gps_s": args.nearby_gps_s,
                "ble_used": bool(args.include_ble),
            },
            "company": {
                "wifi": {
                    "bssids": ranked,
                    "session_counts": {b: ap_sessions[b] for b in ranked},
                    "scan_counts": {b: ap_seen[b] for b in ranked},
                    "rssi_med": {b: int(statistics.median(ap_rssi[b])) for b in ranked},
                },
                "cell": cell_body,
                "ble": {"macs": [], "session_counts": {}},
            },
        }
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(body, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(
            f"wrote {out}: dwell_sessions={len(accepted_dwell)}, scans={scan_count}, "
            f"wifi_topk={len(ranked)}, cell={len(cell_body.get('cell_ids', []))}, ble=0"
        )
        return 0

    for root_s in args.data_root:
        root = Path(root_s)
        for session in sorted(p for p in root.rglob("*") if p.is_dir() and list(p.glob("location_data_*.csv"))):
            sensor = load_sensor_gps(str(session)) if (session / "sensor_events.csv").is_file() else []
            gps = merge_gps(load_location_csv_dir(str(session)), sensor)
            if not gps:
                continue
            gps_ms = [int(p.t.timestamp() * 1000) for p in gps]
            wifi = load_wifi_scans([str(session)])
            cells = load_cell_samples([str(session)])
            ble = load_ble_samples([str(session)])
            ws = {
                apx.bssid.lower()
                for scan in wifi if nearby_inside(scan.t_ms, gps, gps_ms, company, gap_ms)
                for apx in scan.aps if apx.bssid and apx.rssi >= -85
            }
            cs = {x.cell_id for x in cells if x.cell_id and nearby_inside(x.t_ms, gps, gps_ms, company, gap_ms)}
            bs = {
                x.mac.lower() for x in ble
                if x.mac and x.rssi >= -85 and nearby_inside(x.t_ms, gps, gps_ms, company, gap_ms)
            } if args.include_ble else set()
            if not (ws or cs or bs):
                continue
            accepted.append(session.name)
            wifi_sessions.update(ws)
            cell_sessions.update(cs)
            ble_sessions.update(bs)
            for scan in wifi:
                if not nearby_inside(scan.t_ms, gps, gps_ms, company, gap_ms):
                    continue
                per_scan = set()
                for apx in scan.aps:
                    if apx.bssid and apx.rssi >= -85:
                        bssid = apx.bssid.lower()
                        per_scan.add(bssid)
                        rssi.setdefault(bssid, []).append(apx.rssi)
                wifi_scans.update(per_scan)
            cell_samples.update(
                x.cell_id for x in cells
                if x.cell_id and nearby_inside(x.t_ms, gps, gps_ms, company, gap_ms)
            )

    n = len(accepted)
    required = max(args.min_sessions, int(n * args.min_session_ratio + 0.999))
    wifi_candidates = [x for x, count in wifi_sessions.items() if count >= required]
    wifi = sorted(
        wifi_candidates,
        key=lambda x: (wifi_sessions[x], wifi_scans[x], statistics.median(rssi.get(x, [-127])), x),
        reverse=True,
    )[: max(1, args.top_k)]
    cell_candidates = [x for x, count in cell_sessions.items() if count >= required]
    cells = sorted(
        cell_candidates,
        key=lambda x: (cell_sessions[x], cell_samples[x], x),
        reverse=True,
    )[: max(1, args.cell_top_k)]
    ble = sorted(k for k, v in ble_sessions.items() if v >= required)
    med = {b: sorted(v)[len(v) // 2] for b, v in rssi.items() if b in set(wifi)}
    body = {
        "version": 2,
        "site": "company_001",
        "mode": "testset_recurring_topk",
        "built_at": datetime.now(timezone.utc).isoformat(),
        "source_sessions": accepted,
        "selection": {
            "min_sessions": required,
            "min_session_ratio": args.min_session_ratio,
            "top_k": args.top_k,
            "cell_top_k": args.cell_top_k,
            "rssi_min": -85,
            "nearby_gps_s": args.nearby_gps_s,
            "ble_used": bool(args.include_ble),
        },
        "company": {
            "wifi": {
                "bssids": wifi,
                "session_counts": {x: wifi_sessions[x] for x in wifi},
                "scan_counts": {x: wifi_scans[x] for x in wifi},
                "rssi_med": med,
            },
            "cell": {
                "cell_ids": cells,
                "session_counts": {str(x): cell_sessions[x] for x in cells},
                "sample_counts": {str(x): cell_samples[x] for x in cells},
            },
            "ble": {"macs": ble, "session_counts": {x: ble_sessions[x] for x in ble}},
        },
    }
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(body, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out}: sessions={n}, required={required}, wifi={len(wifi)}, cell={len(cells)}, ble={len(ble)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
