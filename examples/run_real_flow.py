#!/usr/bin/env python3
"""Run one end-to-end flow on local helloworld dumps.

Steps:
  1) Load GPS (location CSV + sensor_events) + walking
  2) SceneEngine replay with predictive push (config anchors by default)
  3) Write product-min files (theta/anchors/leave_episodes/samples)
  4) Write SUMMARY.md; optionally invoke personalizer_llm (WSL)

Example:
  python examples/run_real_flow.py ^
    --raw-dir D:\\huawei\\data\\20260804_174813 ^
    --sensor-dir D:\\huawei\\data\\20260804_175841_sensor ^
    --home-gcj 40.011181,116.32677
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
from collections import Counter
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from commute_baseline.anchors import load_anchors, save_anchors  # noqa: E402
from commute_baseline.baro_evidence import BaroEvidence  # noqa: E402
from commute_baseline.crs import gcj02_to_wgs84  # noqa: E402
from commute_baseline.engine import DEFAULT_THETA, SceneEngine, TickFeatures  # noqa: E402
from commute_baseline.geo import haversine_m  # noqa: E402
from commute_baseline.io_data import (  # noqa: E402
    load_location_csv_dir,
    load_sensor_gps,
    load_walking_events,
    load_pdr_net_series,
    pdr_net_at,
    load_baro_series,
    merge_gps,
)
from commute_baseline.radio_evidence import (  # noqa: E402
    RadioEvidence,
    RadioFeed,
    load_ble_samples,
    load_cell_samples,
    load_wifi_scans,
)
from commute_baseline.geo import Relation  # noqa: E402

CST = timezone(timedelta(hours=8))


def ms(dt: datetime) -> int:
    return int(dt.timestamp() * 1000)


def write_theta(path: Path, theta: dict) -> None:
    body = {"coordinate_system": "WGS84", **theta}
    path.write_text(json.dumps(body, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw-dir", required=True, help="dir with location_data_*.csv")
    ap.add_argument("--sensor-dir", required=True, help="dir with sensor_events.csv")
    ap.add_argument(
        "--baro-dir",
        default="",
        help="optional dir with baro_data_*.csv (default: --raw-dir); use to borrow baro from a sibling dump",
    )
    ap.add_argument(
        "--out-dir",
        default=str(ROOT / "output" / "real_20260804_flow"),
        help="product-like output root",
    )
    ap.add_argument(
        "--anchors",
        default=str(ROOT / "config" / "anchors.json"),
        help="use project anchors (WGS84); empty → infer from GPS",
    )
    ap.add_argument("--location-crs", default="GCJ02", choices=["GCJ02", "WGS84"])
    ap.add_argument("--home-gcj", default="40.011181,116.32677")
    ap.add_argument("--settle-s", type=float, default=1200.0, help="AFTER_PUSH settle seconds")
    ap.add_argument(
        "--tick-s",
        type=float,
        default=5.0,
        help="time-axis SceneEngine step (seconds); GPS is held between fixes",
    )
    ap.add_argument(
        "--tick-min-s",
        type=float,
        default=None,
        help="deprecated alias for --tick-s (GPS-driven spacing removed)",
    )
    ap.add_argument(
        "--outdoor-source-types",
        default="1",
        help="comma-separated GPS source types treated as an outdoor confirmation",
    )
    ap.add_argument(
        "--episode-truth",
        default="auto",
        choices=["auto", "leave", "not_leave", "unknown"],
        help=(
            "closed-window truth. auto uses independent outdoor GPS; leave/not_leave are explicit dataset truth; "
            "unknown leaves a no-push/no-outdoor window unlabeled"
        ),
    )
    ap.add_argument(
        "--truth-event-ms",
        type=int,
        default=0,
        help="optional ground-truth leave time for --episode-truth leave (defaults to first outdoor fix or final tick)",
    )
    ap.add_argument(
        "--stop-at-motion",
        action="store_true",
        help="truncate replay at the last acc/gyro/mag/rv/baro sample",
    )
    ap.add_argument("--run-personalizer", action="store_true", help="call WSL personalizer_llm")
    ap.add_argument(
        "--company-radio-fingerprint",
        default=str(ROOT / "config" / "company_radio_fingerprint.json"),
        help="local cross-session company WiFi/Cell/BLE profile; ignored when absent",
    )
    ap.add_argument(
        "--baro-mode", choices=("OFF", "SOFT", "GATE", "CONFIRM"), default="OFF",
        help="bounded barometer strategy; OFF preserves the original replay path",
    )
    ap.add_argument(
        "--theta",
        default="",
        help="optional theta.json to seed SceneEngine (default: built-in DEFAULT_THETA)",
    )
    args = ap.parse_args()
    tick_s = float(args.tick_s if args.tick_min_s is None else args.tick_min_s)
    if tick_s <= 0:
        print("ERROR: --tick-s must be > 0", file=sys.stderr)
        return 1
    outdoor_source_types = {
        int(part.strip()) for part in args.outdoor_source_types.split(",") if part.strip()
    }

    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    session = out / "session"
    session.mkdir(exist_ok=True)
    (session / "metadata.json").write_text(
        json.dumps({"location_crs": args.location_crs}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    raw = load_location_csv_dir(args.raw_dir, source_crs=args.location_crs)
    sensor = load_sensor_gps(args.sensor_dir)
    merged = merge_gps(raw, sensor)
    walks = load_walking_events(args.sensor_dir)
    pdr_series = load_pdr_net_series(args.sensor_dir)
    baro_dir = args.baro_dir.strip() or args.raw_dir
    baro_series = load_baro_series(baro_dir)
    if Path(baro_dir).resolve() != Path(args.raw_dir).resolve():
        print(f"baro borrowed from {baro_dir} (n={len(baro_series)})")
    motion_stop = None
    if args.stop_at_motion:
        motion_prefixes = ("acc_data_", "gyro_data_", "mag_data_", "rv_data_", "baro_data_")
        last_wall_ms = None
        for path in sorted(Path(args.raw_dir).glob("*_data_*.csv")):
            if not path.name.startswith(motion_prefixes):
                continue
            with path.open(newline="", encoding="utf-8-sig") as f:
                for row in csv.reader(f):
                    if not row or row[0] == "wallTsMs":
                        continue
                    try:
                        wall_ms = int(float(row[0]))
                    except (ValueError, TypeError):
                        continue
                    last_wall_ms = wall_ms if last_wall_ms is None else max(last_wall_ms, wall_ms)
        if last_wall_ms is not None:
            motion_stop = datetime.fromtimestamp(last_wall_ms / 1000.0, tz=CST)
            merged = [p for p in merged if p.t <= motion_stop]
            walks = [w for w in walks if w[0] <= motion_stop]
            print(f"replay stop_at_motion={motion_stop.isoformat()}")
    print(f"GPS raw={len(raw)} sensor={len(sensor)} merged={len(merged)} walks={len(walks)} baro={len(baro_series)}")
    if not merged:
        print("ERROR: no GPS points", file=sys.stderr)
        return 1

    if args.anchors and Path(args.anchors).is_file():
        anchors = load_anchors(args.anchors)
        print(f"loaded anchors from {args.anchors}")
    else:
        from commute_baseline.anchors import build_anchors

        anchors = build_anchors(merged)
        print("inferred anchors from GPS")
    save_anchors(str(out / "anchors.json"), anchors)
    print("HOME", anchors.home)
    print("COMPANY", anchors.company)
    print(
        "home-company sep_m:",
        round(haversine_m(anchors.home.lat, anchors.home.lon, anchors.company.lat, anchors.company.lon), 1),
    )

    if args.home_gcj:
        lat_s, lon_s = args.home_gcj.split(",")
        tlat, tlon = gcj02_to_wgs84(float(lat_s), float(lon_s))
        d = haversine_m(anchors.home.lat, anchors.home.lon, tlat, tlon)
        print(f"HOME vs truth GCJ→WGS84: {d:.1f} m  truth=({tlat:.6f},{tlon:.6f})")

    theta = dict(DEFAULT_THETA)
    if args.theta.strip():
        th_path = Path(args.theta)
        if not th_path.is_file():
            print(f"ERROR: --theta not found: {th_path}", file=sys.stderr)
            return 1
        loaded = json.loads(th_path.read_text(encoding="utf-8"))
        if not isinstance(loaded, dict):
            print("ERROR: --theta must be a JSON object", file=sys.stderr)
            return 1
        for k, v in loaded.items():
            if k in ("coordinate_system",):
                continue
            theta[k] = v
        print(f"seeded theta from {th_path}")
    write_theta(out / "theta.json", theta)

    engine = SceneEngine(anchors, theta)
    walk_on = False
    walk_started = None
    wi = 0
    decisions = []
    pushes = []
    baro_i = 0
    gps_i = 0
    baro = BaroEvidence()
    radio_dirs = [args.raw_dir, args.sensor_dir]
    radio = RadioEvidence()
    fingerprint_path = Path(args.company_radio_fingerprint)
    if fingerprint_path.is_file():
        radio.import_company_site_fingerprint_file(str(fingerprint_path))
        shutil.copy2(fingerprint_path, out / "company_radio_fingerprint.json")
        print(f"loaded company radio fingerprint from {fingerprint_path}")
    else:
        print("company radio fingerprint not found; using session-only radio evidence")
    radio_feed = RadioFeed(
        load_wifi_scans(radio_dirs),
        load_cell_samples(radio_dirs),
        load_ble_samples(radio_dirs),
        radio=radio,
    )

    # Time-axis ticks (align with on-device fixed period). GPS is held between fixes;
    # baro / walking / radio advance every tick_s.
    t_start = merged[0].t
    t_end = merged[-1].t
    if baro_series:
        t_start = min(t_start, baro_series[0][0])
        t_end = max(t_end, baro_series[-1][0])
    if walks:
        t_start = min(t_start, walks[0][0])
        t_end = max(t_end, walks[-1][0])
    # Align to whole seconds so dumps are easy to read.
    tick_t = t_start.replace(microsecond=0)
    if tick_t < t_start:
        tick_t += timedelta(seconds=1)
    step = timedelta(seconds=tick_s)
    held_gps = None
    n_ticks = 0
    print(f"time-axis tick_s={tick_s} range={tick_t.isoformat()} .. {t_end.isoformat()}")

    while tick_t <= t_end:
        while wi < len(walks) and walks[wi][0] <= tick_t:
            et = walks[wi][1]
            if et == "WALKING_STARTED":
                walk_on = True
                walk_started = walks[wi][0]
            else:
                walk_on = False
            wi += 1
        gps_fresh = False
        while gps_i < len(merged) and merged[gps_i].t <= tick_t:
            held_gps = merged[gps_i]
            gps_fresh = True
            gps_i += 1
        if held_gps is None:
            tick_t += step
            continue

        t_ms = ms(tick_t)
        radio_snap = radio_feed.advance(t_ms)
        while baro_i < len(baro_series) and baro_series[baro_i][0] <= tick_t:
            bt, bp = baro_series[baro_i]
            baro.observe(bt, bp)
            baro_i += 1
        wifi_enabled = float(theta.get("w_wifi", 0.0)) > 0.0
        cell_enabled = float(theta.get("w_cell", 0.0)) > 0.0
        workplace_ready = (
            wifi_enabled
            and (radio_snap.company_dwell_ready or radio_snap.company_site_wifi_coverage >= 0.50)
        ) or (cell_enabled and radio_snap.company_site_cell_match)
        baro_snap = baro.evaluate(tick_t, workplace_ready, float(theta.get("baro_min_descent_m", 12.0)))
        pdr_net_out = pdr_net_at(pdr_series, tick_t)
        feat = TickFeatures(
            t=tick_t,
            lat=held_gps.lat,
            lon=held_gps.lon,
            acc=held_gps.acc,
            gps_source_type=held_gps.source_type,
            walking=walk_on,
            walk_started_at=walk_started if walk_on else None,
            pdr_net_out_home_m=pdr_net_out,
            pdr_net_out_company_m=pdr_net_out,
            wifi_home_detach=radio_snap.wifi_home_detach,
            wifi_company_detach=radio_snap.wifi_company_detach,
            wifi_home_attach=radio_snap.wifi_home_attach,
            wifi_company_attach=radio_snap.wifi_company_attach,
            cell_leave_home=radio_snap.cell_leave_home,
            cell_leave_company=radio_snap.cell_leave_company,
            ble_home_detach=radio_snap.ble_home_detach,
            ble_company_detach=radio_snap.ble_company_detach,
            wifi_jaccard_home=radio_snap.jaccard_home,
            wifi_jaccard_company=radio_snap.jaccard_company,
            baro_available=baro_snap.available,
            baro_descent_m=baro_snap.descent_m,
            baro_baseline_ready=baro_snap.baseline_ready,
            baro_stable_platform=baro_snap.stable_platform,
            baro_descending=baro_snap.descending,
            baro_lower_platform=baro_snap.lower_platform,
            baro_mode=args.baro_mode,
        )
        d = engine.step(feat)
        radio_feed.radio.observe_dwell(
            t_ms,
            d.home_relation == Relation.INSIDE,
            d.company_relation == Relation.INSIDE,
        )
        row = {
            "t": tick_t.isoformat(),
            "t_ms": t_ms,
            "lat": held_gps.lat,
            "lon": held_gps.lon,
            "gps_fresh": gps_fresh,
            "gps_held_from": held_gps.t.isoformat(),
            "baro_pressure_hpa": baro_snap.pressure_hpa,
            "baro_descent_m": round(baro_snap.descent_m, 2),
            "baro_baseline_ready": baro_snap.baseline_ready,
            "baro_stable_platform": baro_snap.stable_platform,
            "baro_descending": round(baro_snap.descending, 3),
            "baro_lower_platform": baro_snap.lower_platform,
            "acc": held_gps.acc,
            "gps_source_type": held_gps.source_type,
            "walking": walk_on,
            "scene": d.scene.value,
            "score_home": d.score_home,
            "score_company": d.score_company,
            "dist_home_m": d.dist_home_m,
            "dist_company_m": d.dist_company_m,
            "home_relation": d.home_relation.value,
            "company_relation": d.company_relation.value,
            "should_service": d.should_service,
            "service_intent": d.service_intent,
            "hsmm_preleave_home": d.hsmm_preleave_home,
            "hsmm_preleave_company": d.hsmm_preleave_company,
            "hsmm_outside_home": d.hsmm_outside_home,
            "hsmm_outside_company": d.hsmm_outside_company,
            "hits_home": d.evidence.get("hits_home", 0),
            "hits_company": d.evidence.get("hits_company", 0),
            "evidence": d.evidence,
            "uncertainty": d.uncertainty,
            "eta_leave_s": d.eta_leave_s,
            "lead_gate_ok": d.lead_gate_ok,
            "push_block_reason": d.push_block_reason,
        }
        decisions.append(row)
        n_ticks += 1
        if d.should_service:
            pushes.append(row)
            print(
                f"PUSH {d.service_intent} @ {tick_t.isoformat()} scene={d.scene.value} "
                f"rel={d.home_relation.value}/{d.company_relation.value} "
                f"distH={d.dist_home_m} eta={d.eta_leave_s:.1f} "
                f"sh={d.score_home:.2f} sc={d.score_company:.2f} "
                f"baro_d={baro_snap.descent_m:.1f}m desc={baro_snap.descending:.2f}"
            )
        tick_t += step

    print(f"time-axis ticks={n_ticks} gps_fixes={len(merged)}")

    # Product leave_episodes + sparse samples + settle labels
    episodes_path = out / "leave_episodes.jsonl"
    samples_path = out / "leave_window_samples.jsonl"
    policy_history_path = out / "policy_history.jsonl"
    by_t = {r["t_ms"]: r for r in decisions}
    sorted_dec = sorted(decisions, key=lambda r: r["t_ms"])

    policy_rows = []

    def append_policy_window(
        side: str,
        label: str,
        start_ms: int,
        outcome_ms: int,
        lead_s: float | None,
    ) -> None:
        """Store the immutable pre-update evidence window used by later counterfactual replay."""
        for r in sorted_dec:
            if r["t_ms"] < start_ms or r["t_ms"] > outcome_ms:
                continue
            ev = r.get("evidence", {}).get(side, {})
            obs = ev.get("obs", {})
            rel = r["home_relation"] if side == "home" else r["company_relation"]
            policy_rows.append(
                {
                    "t_ms": r["t_ms"],
                    "outcome_t_ms": outcome_ms,
                    "side": side,
                    "label": label,
                    "preleave_probability": r["hsmm_preleave_home"] if side == "home" else r["hsmm_preleave_company"],
                    "leaving_probability": r["score_home"] if side == "home" else r["score_company"],
                    "hits": ev.get("hits", r.get("hits_home", 0) if side == "home" else r.get("hits_company", 0)),
                    "walking": r["walking"],
                    "pdr_net_out_m": ev.get("pdr_net_out", 0.0),
                    "wifi_detach": ev.get("wifi_detach", False),
                    "cell_leave": ev.get("cell_leave", False),
                    "ble_detach": ev.get("ble_detach", False),
                    "geo_outbound": float(ev.get("s_geo", 0.0)) >= float(theta.get("thr_geo", 0.5)),
                    "has_usable_gps": rel != "UNKNOWN",
                    "baro_available": r.get("baro_pressure_hpa") is not None,
                    "baro_baseline_ready": r.get("baro_baseline_ready", False),
                    "baro_descent_m": r.get("baro_descent_m", 0.0),
                    "baro_lower_platform": r.get("baro_lower_platform", False),
                    "obs_walking": float(obs.get("walking", ev.get("s_walk", 0.0))),
                    "obs_pdr_outbound": float(obs.get("pdr_outbound", ev.get("s_pdr", 0.0))),
                    "obs_geo_outbound": float(obs.get("geo_outbound", ev.get("s_geo", 0.0))),
                    "obs_wifi_detach": float(obs.get("wifi_detach", ev.get("s_wifi", 0.0))),
                    "obs_cell_detach": float(obs.get("cell_detach", ev.get("s_cell", 0.0))),
                    "obs_ble_detach": float(obs.get("ble_detach", ev.get("s_ble", 0.0))),
                    "obs_time_prior": float(obs.get("time_prior", ev.get("s_time", 0.0))),
                    "obs_baro_descending": float(obs.get("baro_descending", r.get("baro_descending", 0.0))),
                    "obs_baro_lower_platform": float(obs.get("baro_lower_platform", 1.0 if r.get("baro_lower_platform") else 0.0)),
                    "obs_baro_available": bool(obs.get("baro_available", r.get("baro_baseline_ready", False))),
                    "obs_sequence_available": bool(obs.get("sequence_available", False)),
                    "obs_sequence_progress": float(obs.get("sequence_progress", 0.0)),
                    "obs_sequence_complete": float(obs.get("sequence_complete", 0.0)),
                    "obs_negative_pattern_match": float(obs.get("negative_pattern_match", 0.0)),
                    "obs_sequence_reliability": float(obs.get("sequence_reliability", 0.0)),
                    "obs_relation_known": bool(obs.get("relation_known", rel != "UNKNOWN")),
                    "obs_inside": bool(obs.get("inside", rel == "INSIDE")),
                    "obs_near": bool(obs.get("near", rel == "NEAR")),
                    "obs_outside": bool(obs.get("outside", rel == "OUTSIDE")),
                    "obs_approaching": bool(obs.get("approaching", ev.get("approaching", False))),
                    "obs_attached": bool(obs.get("attached", False)),
                    "lead_s": lead_s,
                }
            )

    # Ground truth is independent of the model push, but an outdoor GPS fix is
    # not itself a leave event: a session may begin outdoors while the user is
    # returning to the company.  Require an observed company origin followed
    # by an outward INSIDE/NEAR -> OUTSIDE transition.
    observed_company_gt: list[int] = []
    saw_company_origin = False
    leave_event_open = False
    previous_company_relation = "UNKNOWN"
    for r in sorted_dec:
        relation = r.get("company_relation", "UNKNOWN")
        if relation in ("INSIDE", "NEAR"):
            saw_company_origin = True
            leave_event_open = False
        outdoor = r.get("gps_source_type") in outdoor_source_types
        approaching = bool(r.get("evidence", {}).get("company", {}).get("approaching", False))
        crossed_outward = previous_company_relation in ("INSIDE", "NEAR") and relation == "OUTSIDE"
        if saw_company_origin and outdoor and crossed_outward and not approaching and not leave_event_open:
            observed_company_gt.append(r["t_ms"])
            leave_event_open = True
        if relation != "UNKNOWN":
            previous_company_relation = relation
    company_gt = list(observed_company_gt) if args.episode_truth in ("auto", "leave") else []
    if args.episode_truth == "leave" and not company_gt and sorted_dec:
        company_gt.append(args.truth_event_ms if args.truth_event_ms > 0 else sorted_dec[-1]["t_ms"])

    with episodes_path.open("w", encoding="utf-8") as ep, samples_path.open("w", encoding="utf-8") as sp:
        for push in pushes:
            t_push = push["t_ms"]
            ep.write(
                json.dumps(
                    {
                        "type": "push",
                        "t_push_ms": t_push,
                        "intent": push["service_intent"],
                        "scene": push["scene"],
                        "score_home": push["score_home"],
                        "score_company": push["score_company"],
                        "dist_home_m": push["dist_home_m"] if push["dist_home_m"] is not None else -1,
                        "walking": push["walking"],
                        "home_relation": push["home_relation"],
                        "eta_leave_s": push["eta_leave_s"],
                        "theta": {
                            "enter_leave": theta["enter_leave"],
                            "min_evidence": theta["min_evidence"],
                        },
                    },
                    ensure_ascii=False,
                )
                + "\n"
            )

            t_star = None
            truth_source = ""
            last_rel = (
                push["home_relation"]
                if "HOME" in push["service_intent"] or push["scene"] == "LEAVING_HOME"
                else push["company_relation"]
            )
            last_dist = push["dist_home_m"] if last_rel == push["home_relation"] else push["dist_company_m"]
            use_home = push["scene"] == "LEAVING_HOME" or push["service_intent"] == "DEPARTURE_NOTIFICATION"
            for r in sorted_dec:
                if r["t_ms"] < t_push:
                    continue
                if r["t_ms"] > t_push + int(args.settle_s * 1000):
                    break
                rel_now = r["home_relation"] if use_home else r["company_relation"]
                dist_now = r["dist_home_m"] if use_home else r["dist_company_m"]
                # sparse samples always vs home (product schema)
                if r["dist_home_m"] is not None:
                    sp.write(
                        json.dumps(
                            {
                                "t_ms": r["t_ms"],
                                "t_push_ms": t_push,
                                "lat": r["lat"],
                                "lon": r["lon"],
                                "acc": r["acc"],
                                "walking": r["walking"],
                                "home_relation": r["home_relation"],
                                "dist_home_m": r["dist_home_m"],
                            },
                            ensure_ascii=False,
                        )
                        + "\n"
                    )
                if rel_now != "UNKNOWN":
                    last_rel = rel_now
                    last_dist = dist_now
                outdoor_fix = (
                    args.episode_truth != "not_leave" and
                    r.get("gps_source_type") in outdoor_source_types
                )
                if use_home:
                    if t_star is None and rel_now == "OUTSIDE":
                        t_star = r["t_ms"]
                        truth_source = "ANCHOR_OUTSIDE"
                    elif t_star is None and outdoor_fix:
                        t_star = r["t_ms"]
                        truth_source = "OUTDOOR_GPS"
                elif t_star is None and outdoor_fix:
                    # Company leave truth is source_type outdoor (typically 1 = out the gate).
                    # Fence OUTSIDE alone is not enough; no type=1 in the dump ⇒ did not leave.
                    t_star = r["t_ms"]
                    truth_source = "OUTDOOR_GPS"

            settle_t = t_push + int(args.settle_s * 1000)
            if t_star is not None:
                label = "CONFIRMED_LEAVE"
                t_label = t_star
            else:
                # Entire replay settled without outdoor confirmation → not a real leave.
                # Prefer FALSE_PUSH over UNKNOWN so Agent can train on the miss.
                label = "FALSE_PUSH"
                t_label = min(settle_t, sorted_dec[-1]["t_ms"] if sorted_dec else settle_t)
                if not use_home and not truth_source:
                    truth_source = "NO_SOURCE_TYPE_OUTDOOR"

            lead_s = None
            if t_star is not None and t_star >= t_push:
                lead_s = (t_star - t_push) / 1000.0
            ep.write(
                json.dumps(
                    {
                        "type": "label",
                        "t_label_ms": t_label,
                        "t_push_ms": t_push,
                        "label": label,
                        "side": "home" if use_home else "company",
                        "home_relation": last_rel if use_home else push["home_relation"],
                        "anchor_relation": last_rel,
                        "dist_home_m": last_dist if use_home and last_dist is not None else -1,
                        "t_star_ms": t_star,
                        "truth_source": truth_source,
                        "lead_s": lead_s,
                    },
                    ensure_ascii=False,
                )
                + "\n"
            )
            side = "home" if use_home else "company"
            # A false-push episode remains open until its settle/label time.
            # Post-push vertical completion or return evidence is essential for
            # context-template learning and must not be truncated at t_push.
            outcome_ms = t_star if t_star is not None else t_label
            append_policy_window(side, label, t_push - 600000, outcome_ms, lead_s)
            print(
                f"LABEL {label} push@{push['t']} t_label={t_label} lead_s={lead_s} "
                f"anchor_rel={last_rel} dist={last_dist}"
            )

        company_pushes = [p for p in pushes if p["scene"] == "LEAVING_COMPANY"]
        for t_star in company_gt:
            matched = any(0 <= t_star - p["t_ms"] <= int(args.settle_s * 1000) for p in company_pushes)
            if matched:
                continue
            label_row = {
                "type": "label",
                "t_label_ms": t_star,
                "t_push_ms": 0,
                "label": "MISSED_LEAVE",
                "side": "company",
                "anchor_relation": "OUTSIDE",
                "t_star_ms": t_star,
                "truth_source": "EXPLICIT_LEAVE" if args.episode_truth == "leave" else "OUTDOOR_GPS",
                "lead_s": None,
            }
            ep.write(json.dumps(label_row, ensure_ascii=False) + "\n")
            append_policy_window("company", "MISSED_LEAVE", t_star - 600000, t_star, None)
            print(f"LABEL MISSED_LEAVE t_label={t_star} truth={label_row['truth_source']}")

        if args.episode_truth == "not_leave" and not company_pushes and sorted_dec:
            outcome_ms = sorted_dec[-1]["t_ms"]
            label_row = {
                "type": "label",
                "t_label_ms": outcome_ms,
                "t_push_ms": 0,
                "label": "TRUE_NEGATIVE",
                "side": "company",
                "anchor_relation": sorted_dec[-1]["company_relation"],
                "t_star_ms": None,
                "truth_source": "EXPLICIT_NOT_LEAVE",
                "lead_s": None,
            }
            ep.write(json.dumps(label_row, ensure_ascii=False) + "\n")
            append_policy_window("company", "TRUE_NEGATIVE", outcome_ms - 600000, outcome_ms, None)
            print(f"LABEL TRUE_NEGATIVE t_label={outcome_ms} truth=EXPLICIT_NOT_LEAVE")

    policy_history_path.write_text(
        "\n".join(json.dumps(row, ensure_ascii=False) for row in policy_rows) +
        ("\n" if policy_rows else ""), encoding="utf-8"
    )
    (out / "policy.json").write_text(
        json.dumps(
            {
                "schema_version": 1,
                "revision": 0,
                "enabled": True,
                "template_name": "confirmed_leaving",
                "trigger_phase": "LEAVING",
                "probability_threshold": 0.58,
                "min_duration_s": 0,
                "min_independent_evidence": 2,
                "require_walking": False,
                "require_wifi_detach": False,
                "require_radio": False,
                "allow_cell_pdr_pair": True,
                "gps_mode": "OPTIONAL",
                "baro_mode": "OFF",
            }, ensure_ascii=False, indent=2
        ) + "\n", encoding="utf-8"
    )

    # Link sensor session for evidence tools (wifi/gps windows)
    for name in ("wifi_data", "cell_data", "mag_data", "location_data"):
        # copy a few hours around first push if any, else first files
        src_glob = list(Path(args.raw_dir).glob(f"{name}_*.csv"))
        if not src_glob and name != "location_data":
            src_glob = list(Path(args.sensor_dir).glob(f"{name}_*.csv"))
        for f in sorted(src_glob)[:6]:
            dst = session / f.name
            if not dst.exists():
                try:
                    shutil.copy2(f, dst)
                except OSError:
                    pass
    # also sensor GPS as location_data_smoke for host tools that look for session csv
    sens = Path(args.sensor_dir) / "sensor_events.csv"
    if sens.exists():
        shutil.copy2(sens, session / "sensor_events.csv")

    (out / "param_changes.jsonl").write_text("", encoding="utf-8")
    (out / "audit.jsonl").write_text("", encoding="utf-8")
    (out / "personalize_jobs.jsonl").write_text("", encoding="utf-8")

    scene_counts = Counter(r["scene"] for r in decisions)
    decisions_path = out / "replay_decisions.json"
    decisions_path.write_text(
        json.dumps(
            {
                "n_decisions": len(decisions),
                "n_pushes": len(pushes),
                "scene_counts": dict(scene_counts),
                "pushes": pushes,
                "decisions": decisions,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )

    summary = [
        "# Real-data flow SUMMARY",
        "",
        f"- raw-dir: `{args.raw_dir}`",
        f"- sensor-dir: `{args.sensor_dir}`",
        f"- baro-dir: `{baro_dir}`",
        f"- out-dir: `{out}`",
        f"- GPS merged: {len(merged)}, ticks: {len(decisions)} (tick_s={tick_s}), pushes: {len(pushes)}",
        f"- baro samples: {len(baro_series)}",
        f"- company radio fingerprint: {'loaded' if fingerprint_path.is_file() else 'not loaded'}",
        f"- scenes: {dict(scene_counts)}",
        "",
        "## Pushes",
    ]
    if not pushes:
        summary.append("_（本次回放无推送）_")
    for p in pushes:
        summary.append(
            f"- `{p['t']}` **{p['service_intent']}** scene={p['scene']} "
            f"rel={p['home_relation']} distH={p['dist_home_m']} eta={p['eta_leave_s']}"
        )
    summary += [
        "",
        "## Product files",
        f"- `{episodes_path.name}`",
        f"- `{samples_path.name}`",
        f"- `theta.json` / `anchors.json`",
        f"- `replay_decisions.json`",
        f"- `policy.json` / `{policy_history_path.name}`",
        "",
    ]
    (out / "SUMMARY.md").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print(f"wrote {decisions_path} ticks={len(decisions)} pushes={len(pushes)}")
    print(f"summary → {out / 'SUMMARY.md'}")

    if args.run_personalizer:
        if not pushes:
            print("skip personalizer: no pushes")
            return 0
        # Prefer WSL path mapping
        wsl_out = "/mnt/d/huawei/commute_scene_baseline/" + str(out.relative_to(ROOT)).replace("\\", "/")
        env_file = "/mnt/d/huawei/commute_scene_baseline/sa_service/etc/agent.env"
        cmd = [
            "wsl",
            "-e",
            "bash",
            "-lc",
            f"cd /mnt/d/huawei/commute_scene_baseline && "
            f"bash examples/personalizer_llm/run.sh {env_file} {wsl_out} --no-fixture --debug",
        ]
        print("running:", " ".join(cmd))
        subprocess.run(cmd, check=False)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
