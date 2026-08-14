#!/usr/bin/env python3
"""Batch leave-company replay on D:\\huawei\\data sessions + theta iteration.

Existing flow (examples/run_real_flow.py):
  raw location_data_*.csv + sa_sensor_test/*/sensor_events.csv
  → SceneEngine → leave_episodes / lead → iterate θ.

Sensor map (raw folder → sa_sensor_test folder):
  20260804_174813 → 20260804_175841
  others: same folder name under sa_sensor_test/
Fallback: --allow-synth synthesizes walking from location power_mode.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import shutil
import sys
from collections import Counter, defaultdict
from copy import deepcopy
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from commute_baseline.anchors import load_anchors, save_anchors  # noqa: E402
from commute_baseline.engine import DEFAULT_THETA, SceneEngine, TickFeatures  # noqa: E402
from commute_baseline.geo import haversine_m  # noqa: E402
from commute_baseline.io_data import (  # noqa: E402
    load_location_csv_dir,
    load_pdr_net_series,
    load_sensor_gps,
    load_walking_events,
    merge_gps,
    pdr_net_at,
)
from commute_baseline.radio_evidence import (  # noqa: E402
    RadioEvidence,
    RadioFeed,
    load_ble_samples,
    load_cell_samples,
    load_wifi_scans,
)
from commute_baseline.crs import gcj02_to_wgs84  # noqa: E402
from commute_baseline.geo import Relation  # noqa: E402

CST = timezone(timedelta(hours=8))

# raw_dir name → sa_sensor_test subdir name
SESSION_SENSOR_MAP = {
    "20260804_174813": "20260804_175841_sensor",
    "20260804_195251": "20260804_195251_sensor",
    "20260805_162326": "20260805_162326_sensor",
    "20260809_230509": "20260809_230510_sensor",
    "20260810_114452": "20260810_114452_sensor",
    "20260810_114956": "20260810_114956_sensor",
}
SESSIONS = list(SESSION_SENSOR_MAP.keys())


def ms(dt: datetime) -> int:
    return int(dt.timestamp() * 1000)


def iso_ms(t_ms: int) -> str:
    return datetime.fromtimestamp(t_ms / 1000.0, CST).isoformat(timespec="milliseconds")


def write_json(path: Path, obj: Any) -> None:
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def synthesize_sensor_events(raw_dir: Path, out_path: Path) -> Dict[str, int]:
    """Build sensor_events.csv from location power_mode + WGS84 GPS."""
    rows_out: List[dict] = []
    seq = 0
    prev_walk: Optional[bool] = None
    n_gps = 0
    n_walk = 0

    for path in sorted(raw_dir.glob("location_data_*.csv")):
        if path.stat().st_size < 40:
            continue
        with path.open("r", encoding="utf-8", newline="") as f:
            for row in csv.DictReader(f):
                try:
                    t_ms = int(float(row["wallTsMs"]))
                    lat_gcj = float(row["latitude"])
                    lon_gcj = float(row["longitude"])
                    acc = float(row.get("accuracy") or 999)
                except (KeyError, ValueError):
                    continue
                lat, lon = gcj02_to_wgs84(lat_gcj, lon_gcj)
                t = datetime.fromtimestamp(t_ms / 1000.0, CST)
                t_iso = t.isoformat(timespec="milliseconds")
                pm = (row.get("power_mode") or "HIGH_STILL").strip()
                walking = "WALK" in pm.upper()

                if prev_walk is None:
                    prev_walk = walking
                elif walking and not prev_walk:
                    seq += 1
                    n_walk += 1
                    rows_out.append(
                        {
                            "sequence_id": seq,
                            "received_at": t_iso,
                            "source_observed_at": t_iso,
                            "event_type": "WALKING_STARTED",
                            "motion_state": "WALKING",
                            "episode_id": f"walk-synth-{t_ms}",
                            "payload_json": json.dumps({"timestamp_ms": t_ms}),
                            "power_mode": pm,
                        }
                    )
                    prev_walk = True
                elif (not walking) and prev_walk:
                    seq += 1
                    rows_out.append(
                        {
                            "sequence_id": seq,
                            "received_at": t_iso,
                            "source_observed_at": t_iso,
                            "event_type": "WALKING_STOPPED",
                            "motion_state": "NOT_WALKING",
                            "episode_id": "",
                            "payload_json": "{}",
                            "power_mode": pm,
                        }
                    )
                    prev_walk = False

                seq += 1
                n_gps += 1
                rows_out.append(
                    {
                        "sequence_id": seq,
                        "received_at": t_iso,
                        "source_observed_at": t_iso,
                        "event_type": "GPS_REPORT",
                        "motion_state": "WALKING" if walking else "NOT_WALKING",
                        "episode_id": "",
                        "payload_json": json.dumps(
                            {
                                "latitude": lat,
                                "longitude": lon,
                                "horizontal_accuracy_m": acc,
                                "valid": True,
                                "coordinate_system": "WGS84",
                                "source_type": int(float(row.get("sourceType") or 2)),
                            }
                        ),
                        "power_mode": pm,
                    }
                )

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(
            f,
            fieldnames=[
                "sequence_id",
                "received_at",
                "source_observed_at",
                "event_type",
                "motion_state",
                "episode_id",
                "payload_json",
                "power_mode",
            ],
        )
        w.writeheader()
        w.writerows(rows_out)
    return {"n_events": len(rows_out), "n_gps": n_gps, "n_walk_starts": n_walk}


def detect_leave_company_times(
    points: List[Any],
    company_lat: float,
    company_lon: float,
    r_in: float,
    r_out: float,
    max_acc: float = 80.0,
    away_confirm_s: float = 120.0,
) -> List[Dict[str, Any]]:
    """Heuristic t*: after INSIDE/NEAR dwell, first sustained OUTSIDE."""
    leaves: List[Dict[str, Any]] = []
    dwell_inside = False
    outside_since: Optional[datetime] = None
    last_inside_t: Optional[datetime] = None
    last_near_t: Optional[datetime] = None

    for p in points:
        if p.acc is not None and p.acc > max_acc:
            continue
        dist = haversine_m(p.lat, p.lon, company_lat, company_lon)
        if dist <= r_in:
            dwell_inside = True
            last_inside_t = p.t
            outside_since = None
        elif dist <= r_out:
            if dwell_inside:
                last_near_t = p.t
            outside_since = None
        else:
            if not dwell_inside:
                continue
            if outside_since is None:
                outside_since = p.t
            elif (p.t - outside_since).total_seconds() >= away_confirm_s:
                leaves.append(
                    {
                        "t_star_ms": ms(outside_since),
                        "t_star_iso": outside_since.isoformat(timespec="milliseconds"),
                        "confirm_ms": ms(p.t),
                        "dist_m": round(dist, 1),
                        "last_inside_iso": None
                        if last_inside_t is None
                        else last_inside_t.isoformat(timespec="milliseconds"),
                        "last_near_iso": None
                        if last_near_t is None
                        else last_near_t.isoformat(timespec="milliseconds"),
                    }
                )
                dwell_inside = False
                outside_since = None
    return leaves


def replay_session(
    raw_dir: Path,
    sensor_dir: Path,
    out_dir: Path,
    anchors_path: Path,
    theta: Dict[str, Any],
    company_radio_fingerprint: Optional[Path] = None,
    tick_min_s: float = 8.0,
    settle_s: float = 1200.0,
    outdoor_source_types: Optional[set] = None,
) -> Dict[str, Any]:
    out_dir.mkdir(parents=True, exist_ok=True)
    session = out_dir / "session"
    session.mkdir(exist_ok=True)
    if outdoor_source_types is None:
        outdoor_source_types = {1}

    raw = load_location_csv_dir(str(raw_dir), source_crs="GCJ02")
    has_sensor = (sensor_dir / "sensor_events.csv").exists()
    sensor = load_sensor_gps(str(sensor_dir)) if has_sensor else []
    merged = merge_gps(raw, sensor)
    walks = load_walking_events(str(sensor_dir)) if has_sensor else []
    pdr_series = load_pdr_net_series(str(sensor_dir)) if has_sensor else []
    if not merged:
        return {"ok": False, "error": "no_gps", "session": raw_dir.name}

    # Radio CSV usually lives under raw dump; also accept sensor_dir copies.
    radio_dirs = [str(raw_dir)]
    if sensor_dir.resolve() != raw_dir.resolve():
        radio_dirs.append(str(sensor_dir))
    wifi_scans = load_wifi_scans(radio_dirs)
    cell_samples = load_cell_samples(radio_dirs)
    ble_samples = load_ble_samples(radio_dirs)
    radio = RadioEvidence()
    if company_radio_fingerprint is not None and company_radio_fingerprint.is_file():
        radio.import_company_site_fingerprint_file(str(company_radio_fingerprint))
    radio_feed = RadioFeed(wifi_scans, cell_samples, ble_samples, radio=radio)

    anchors = load_anchors(str(anchors_path))
    save_anchors(str(out_dir / "anchors.json"), anchors)
    write_json(out_dir / "theta.json", {"coordinate_system": "WGS84", **theta})

    gt_leaves = detect_leave_company_times(
        merged,
        anchors.company.lat,
        anchors.company.lon,
        anchors.company.r_in_m,
        anchors.company.r_out_m,
        max_acc=float(theta.get("max_gps_acc_m", 80)),
        away_confirm_s=90.0,
    )

    engine = SceneEngine(anchors, theta)
    walk_on = False
    walk_started = None
    walk_stopped_at = None
    walk_hold_s = float(theta.get("walk_hold_s", 120.0))
    wi = 0
    decisions = []
    pushes = []
    last_t = None

    for p in merged:
        while wi < len(walks) and walks[wi][0] <= p.t:
            et = walks[wi][1]
            if et == "WALKING_STARTED":
                walk_on = True
                walk_started = walks[wi][0]
                walk_stopped_at = None
            else:
                walk_on = False
                walk_stopped_at = walks[wi][0]
            wi += 1
        # Hold walking evidence briefly after STOPPED (elevator / poor GPS gaps).
        walking_eff = walk_on
        if (not walking_eff) and walk_stopped_at is not None:
            if (p.t - walk_stopped_at).total_seconds() <= walk_hold_s:
                walking_eff = True
        if last_t and (p.t - last_t).total_seconds() < tick_min_s:
            continue
        last_t = p.t
        t_ms = ms(p.t)
        radio_snap = radio_feed.advance(t_ms)
        pdr_net = pdr_net_at(pdr_series, p.t) if walking_eff and pdr_series else 0.0
        # Directional PDR: only credit outbound when distance to that anchor is rising.
        pdr_home = pdr_net
        pdr_co = pdr_net
        if engine.prev_dist_home is not None:
            # filled after first tick; refine after step using post-hoc would be late —
            # zeroing when radio attach is safer here.
            pass
        if radio_snap.wifi_home_attach:
            pdr_home = 0.0
        if radio_snap.wifi_company_attach:
            pdr_co = 0.0
        feat = TickFeatures(
            t=p.t,
            lat=p.lat,
            lon=p.lon,
            acc=p.acc,
            gps_source_type=p.source_type,
            walking=walking_eff,
            walk_started_at=walk_started if walking_eff else None,
            pdr_net_out_home_m=pdr_home,
            pdr_net_out_company_m=pdr_co,
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
        )
        d = engine.step(feat)
        # Soft dwell warmup while INSIDE (semi-persistent fingerprint for detach).
        radio_feed.radio.observe_dwell(
            t_ms,
            d.home_relation == Relation.INSIDE,
            d.company_relation == Relation.INSIDE,
        )
        evidence = dict(d.evidence or {})
        evidence["radio"] = {
            "wifi_company_detach": radio_snap.wifi_company_detach,
            "wifi_company_attach": radio_snap.wifi_company_attach,
            "cell_leave_company": radio_snap.cell_leave_company,
            "jaccard_company": round(radio_snap.jaccard_company, 3),
            "jaccard_churn": round(radio_snap.jaccard_churn, 3),
            "company_dwell_ready": radio_snap.company_dwell_ready,
            "n_strong": radio_snap.n_strong,
            "reason": radio_snap.reason,
        }
        row = {
            "t": p.t.isoformat(),
            "t_ms": ms(p.t),
            "lat": p.lat,
            "lon": p.lon,
            "acc": p.acc,
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
            "uncertainty": d.uncertainty,
            "eta_leave_s": d.eta_leave_s,
            "push_block_reason": d.push_block_reason,
            "evidence": evidence,
        }
        decisions.append(row)
        if d.should_service:
            pushes.append(row)

    soft_path = out_dir / "radio_soft.json"
    soft_path.write_text(radio_feed.radio.export_soft_json() + "\n", encoding="utf-8")
    write_json(
        out_dir / "radio_feed_stats.json",
        {
            "wifi_scans": len(wifi_scans),
            "cell_samples": len(cell_samples),
            "ble_samples": len(ble_samples),
            "soft": json.loads(radio_feed.radio.export_soft_json()),
        },
    )

    company_pushes = [p for p in pushes if p["scene"] == "LEAVING_COMPANY" or p["service_intent"] == "LEAVE_COMPANY_NOTIFICATION"]

    episodes_path = out_dir / "leave_episodes.jsonl"
    samples_path = out_dir / "leave_window_samples.jsonl"
    sorted_dec = sorted(decisions, key=lambda r: r["t_ms"])
    labels = []

    with episodes_path.open("w", encoding="utf-8") as ep, samples_path.open("w", encoding="utf-8") as sp:
        for push in pushes:
            t_push = push["t_ms"]
            use_home = push["scene"] == "LEAVING_HOME" or push["service_intent"] == "DEPARTURE_NOTIFICATION"
            ep.write(
                json.dumps(
                    {
                        "type": "push",
                        "t_push_ms": t_push,
                        "intent": push["service_intent"],
                        "scene": push["scene"],
                        "score_home": push["score_home"],
                        "score_company": push["score_company"],
                        "dist_company_m": push["dist_company_m"],
                        "walking": push["walking"],
                        "company_relation": push["company_relation"],
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
            last_rel = push["company_relation"] if not use_home else push["home_relation"]
            last_dist = push["dist_company_m"] if not use_home else push["dist_home_m"]
            for r in sorted_dec:
                if r["t_ms"] < t_push:
                    continue
                if r["t_ms"] > t_push + int(settle_s * 1000):
                    break
                rel_now = r["home_relation"] if use_home else r["company_relation"]
                dist_now = r["dist_home_m"] if use_home else r["dist_company_m"]
                if (r["dist_company_m"] if not use_home else r["dist_home_m"]) is not None:
                    sp.write(
                        json.dumps(
                            {
                                "t_ms": r["t_ms"],
                                "t_push_ms": t_push,
                                "lat": r["lat"],
                                "lon": r["lon"],
                                "acc": r["acc"],
                                "walking": r["walking"],
                                "company_relation": r["company_relation"],
                                "dist_company_m": r["dist_company_m"],
                            },
                            ensure_ascii=False,
                        )
                        + "\n"
                    )
                if rel_now != "UNKNOWN":
                    last_rel = rel_now
                    last_dist = dist_now
                outdoor_fix = r.get("gps_source_type") in outdoor_source_types
                if use_home:
                    if t_star is None and rel_now == "OUTSIDE":
                        t_star = r["t_ms"]
                        truth_source = "ANCHOR_OUTSIDE"
                elif t_star is None and outdoor_fix:
                    t_star = r["t_ms"]
                    truth_source = "OUTDOOR_GPS"
            settle_t = t_push + int(settle_s * 1000)
            if t_star is not None:
                label = "CONFIRMED_LEAVE"
                t_label = t_star
            else:
                # No outdoor source_type=1 (company) / no fence OUTSIDE (home) → not a leave.
                label = "FALSE_PUSH"
                t_label = min(settle_t, sorted_dec[-1]["t_ms"])
                if not use_home:
                    truth_source = "NO_SOURCE_TYPE_OUTDOOR"
            lead_s = (t_star - t_push) / 1000.0 if t_star is not None and t_star >= t_push else None
            lab = {
                "type": "label",
                "t_label_ms": t_label,
                "t_push_ms": t_push,
                "label": label,
                "anchor_relation": last_rel,
                "dist_m": last_dist if last_dist is not None else -1,
                "t_star_ms": t_star,
                "truth_source": truth_source,
                "lead_s": lead_s,
                "side": "home" if use_home else "company",
            }            labels.append(lab)
            ep.write(json.dumps(lab, ensure_ascii=False) + "\n")

        # Missed leave: GT leave with no company push in lookback
        for gt in gt_leaves:
            t_star = gt["t_star_ms"]
            matched = False
            for p in company_pushes:
                if 0 <= t_star - p["t_ms"] <= int(settle_s * 1000):
                    matched = True
                    break
            if matched:
                continue
            lab = {
                "type": "label",
                "t_label_ms": t_star,
                "t_push_ms": None,
                "label": "MISSED_LEAVE",
                "anchor_relation": "OUTSIDE",
                "t_star_ms": t_star,
                "lead_s": None,
                "side": "company",
                "gt": gt,
            }
            labels.append(lab)
            ep.write(json.dumps(lab, ensure_ascii=False) + "\n")

    scene_counts = Counter(r["scene"] for r in decisions)
    write_json(
        out_dir / "replay_decisions.json",
        {
            "n_decisions": len(decisions),
            "n_pushes": len(pushes),
            "n_company_pushes": len(company_pushes),
            "scene_counts": dict(scene_counts),
            "gt_leave_company": gt_leaves,
            "pushes": pushes,
            "decisions": decisions,
        },
    )

    # Match each GT leave to nearest prior company push for lead
    matched_leads = []
    for gt in gt_leaves:
        best = None
        for p in company_pushes:
            lead = (gt["t_star_ms"] - p["t_ms"]) / 1000.0
            if lead < 0 or lead > settle_s:
                continue
            if best is None or lead < best["lead_s"]:
                best = {
                    "t_push_iso": p["t"],
                    "t_star_iso": gt["t_star_iso"],
                    "lead_s": lead,
                    "score_company": p["score_company"],
                    "company_relation_at_push": p["company_relation"],
                }
        matched_leads.append({"gt": gt, "match": best})

    summary = {
        "ok": True,
        "session": raw_dir.name,
        "raw_dir": str(raw_dir),
        "gps_n": len(merged),
        "walk_events": len(walks),
        "ticks": len(decisions),
        "scene_counts": dict(scene_counts),
        "gt_leave_company": gt_leaves,
        "company_pushes": [
            {
                "t": p["t"],
                "t_ms": p["t_ms"],
                "score_company": p["score_company"],
                "company_relation": p["company_relation"],
                "dist_company_m": p["dist_company_m"],
                "eta_leave_s": p["eta_leave_s"],
                "walking": p["walking"],
            }
            for p in company_pushes
        ],
        "labels": labels,
        "matched_leads": matched_leads,
    }
    write_json(out_dir / "leave_company_summary.json", summary)

    lines = [
        f"# Leave-company replay: {raw_dir.name}",
        "",
        f"- GPS={len(merged)} ticks={len(decisions)} walks={len(walks)}",
        f"- scenes: {dict(scene_counts)}",
        f"- GT leave_company times: {len(gt_leaves)}",
    ]
    for gt in gt_leaves:
        lines.append(f"  - t* `{gt['t_star_iso']}` dist={gt['dist_m']}m")
    lines.append(f"- LEAVE_COMPANY pushes: {len(company_pushes)}")
    for p in company_pushes:
        lines.append(
            f"  - `{p['t']}` sc={p['score_company']:.2f} rel={p['company_relation']} "
            f"dist={p['dist_company_m']} eta={p['eta_leave_s']}"
        )
    lines.append("- lead match:")
    for m in matched_leads:
        if m["match"]:
            lines.append(
                f"  - push `{m['match']['t_push_iso']}` → t* `{m['match']['t_star_iso']}` "
                f"lead={m['match']['lead_s']:.1f}s"
            )
        else:
            lines.append(f"  - MISSED t* `{m['gt']['t_star_iso']}`")
    (out_dir / "SUMMARY.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # copy a few wifi for evidence
    for f in sorted(raw_dir.glob("wifi_data_*.csv"))[:4]:
        dst = session / f.name
        if not dst.exists():
            try:
                shutil.copy2(f, dst)
            except OSError:
                pass
    sens = sensor_dir / "sensor_events.csv"
    if sens.exists():
        shutil.copy2(sens, session / "sensor_events.csv")

    return summary


def evaluate_batch(summaries: List[Dict[str, Any]], theta: Dict[str, Any]) -> Dict[str, Any]:
    n_gt = 0
    n_push = 0
    n_confirmed = 0
    n_false = 0
    n_missed = 0
    leads: List[float] = []
    for s in summaries:
        if not s.get("ok"):
            continue
        n_gt += len(s.get("gt_leave_company") or [])
        n_push += len(s.get("company_pushes") or [])
        for lab in s.get("labels") or []:
            side = lab.get("side")
            label = lab.get("label")
            if label == "MISSED_LEAVE":
                n_missed += 1
                continue
            if side != "company":
                continue
            if label == "CONFIRMED_LEAVE":
                n_confirmed += 1
            elif label == "FALSE_PUSH":
                n_false += 1
        for m in s.get("matched_leads") or []:
            if m.get("match") and m["match"].get("lead_s") is not None:
                leads.append(float(m["match"]["lead_s"]))

    leads = sorted(round(x, 1) for x in leads)
    lead_min = float(theta["lead_min_s"])
    lead_max = float(theta["lead_max_s"])
    too_late = sum(1 for x in leads if x < lead_min)
    too_early = sum(1 for x in leads if x > lead_max)
    in_win = sum(1 for x in leads if lead_min <= x <= lead_max)
    return {
        "n_sessions_ok": sum(1 for s in summaries if s.get("ok")),
        "n_gt_leave_company": n_gt,
        "n_company_push": n_push,
        "n_confirmed": n_confirmed,
        "n_false_push": n_false,
        "n_missed": n_missed,
        "leads_s": leads,
        "lead_too_late": too_late,
        "lead_too_early": too_early,
        "lead_in_window": in_win,
        "mean_lead_s": round(sum(leads) / len(leads), 1) if leads else None,
        "theta": {k: theta[k] for k in ("enter_leave", "min_evidence", "arm_delay_s", "weekday_leave_company_hour", "leave_window_min", "w_time", "w_walk", "w_geo", "w_wifi", "w_cell", "w_baro")},
    }


def iterate_theta(theta: Dict[str, Any], metrics: Dict[str, Any], limits: Dict[str, Any]) -> Tuple[Dict[str, Any], List[dict]]:
    """Rule-based θ update (same spirit as personalizer: earlier if late/missed)."""
    new_t = deepcopy(theta)
    changes: List[dict] = []

    def apply(param: str, new_val: float, reason: str) -> None:
        lim = limits.get(param, {})
        step = float(lim.get("step", 0.01))
        vmin = float(lim.get("min", new_val))
        vmax = float(lim.get("max", new_val))
        # quantize to step
        q = round(new_val / step) * step
        q = max(vmin, min(vmax, q))
        # keep int for min_evidence
        if param == "min_evidence":
            q = int(round(q))
        else:
            q = float(round(q, 6))
        old = new_t[param]
        if old == q:
            return
        new_t[param] = q
        changes.append({"param": param, "old": old, "new": q, "reason": reason})

    missed = int(metrics.get("n_missed") or 0)
    false_p = int(metrics.get("n_false_push") or 0)
    too_late = int(metrics.get("lead_too_late") or 0)
    too_early = int(metrics.get("lead_too_early") or 0)
    mean_lead = metrics.get("mean_lead_s")

    if missed > 0 or too_late > 0:
        apply(
            "enter_leave",
            float(new_t["enter_leave"]) - 0.03,
            f"missed={missed} too_late={too_late} mean_lead={mean_lead}: lower threshold to push earlier",
        )
        if int(new_t["min_evidence"]) > 1:
            apply(
                "min_evidence",
                int(new_t["min_evidence"]) - 1,
                "reduce evidence count to fire earlier on geo+walk",
            )
        apply(
            "arm_delay_s",
            max(0, float(new_t["arm_delay_s"]) - 10),
            "shorter arm_delay so leave-company can push sooner after walk starts",
        )
        # widen company leave window / shift hour toward observed
        apply(
            "leave_window_min",
            min(180, float(new_t["leave_window_min"]) + 30),
            "widen leave_window to cover late evening / lunch leave-company",
        )
        if "walk_hold_s" in new_t or True:
            new_t.setdefault("walk_hold_s", 120.0)
            limits.setdefault("walk_hold_s", {"min": 0, "max": 300, "step": 30})
            apply(
                "walk_hold_s",
                min(300, float(new_t.get("walk_hold_s", 120)) + 60),
                "extend walk hold across indoor GPS gaps / elevator / building exit",
            )

    if false_p > 0 and missed == 0:
        apply(
            "enter_leave",
            float(new_t["enter_leave"]) + 0.03,
            f"false_push={false_p}: raise enter_leave",
        )

    if too_early > 0 and too_late == 0 and missed == 0:
        apply(
            "enter_leave",
            float(new_t["enter_leave"]) + 0.03,
            f"lead_too_early={too_early}: raise threshold",
        )

    # observed company leaves ~11:57 and ~18:11/~20:28 → soften single-hour prior weight slightly if many misses
    if missed >= 2:
        apply(
            "w_time",
            max(0.05, float(new_t["w_time"]) - 0.05),
            "multiple missed leave-company outside single hour prior; down-weight time",
        )
        apply(
            "w_geo",
            min(0.35, float(new_t["w_geo"]) + 0.05),
            "boost geo weight for company distance rise",
        )
        apply(
            "weekday_leave_company_hour",
            18.2,
            "keep evening center; window widened instead of shifting to lunch",
        )

    return new_t, changes


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root", default=r"D:\huawei\data")
    ap.add_argument(
        "--sensor-root",
        default=r"D:\huawei\data\sa_sensor_test",
        help="dir containing per-session sensor_events.csv folders",
    )
    ap.add_argument("--out-root", default=str(ROOT / "output" / "leave_company_5sessions"))
    ap.add_argument("--anchors", default=str(ROOT / "config" / "anchors.json"))
    ap.add_argument("--theta", default=str(ROOT / "config" / "theta_default.json"))
    ap.add_argument(
        "--company-radio-fingerprint",
        default=str(ROOT / "config" / "company_radio_fingerprint.json"),
        help="local cross-session company WiFi/Cell/BLE profile; ignored when absent",
    )
    ap.add_argument("--sessions", nargs="*", default=SESSIONS)
    ap.add_argument("--iters", type=int, default=2, help="replay → update θ → replay cycles")
    ap.add_argument(
        "--allow-synth",
        action="store_true",
        help="if sensor_events missing, synthesize from location power_mode",
    )
    args = ap.parse_args()

    data_root = Path(args.data_root)
    sensor_root = Path(args.sensor_root)
    out_root = Path(args.out_root)
    out_root.mkdir(parents=True, exist_ok=True)

    theta_path = Path(args.theta)
    fingerprint_path = Path(args.company_radio_fingerprint)
    print(
        "company radio fingerprint: "
        + (str(fingerprint_path) if fingerprint_path.is_file() else "not loaded")
    )
    theta_body = json.loads(theta_path.read_text(encoding="utf-8"))
    limits = theta_body.pop("param_limits", {})
    # ensure limits for iteration params
    limits.setdefault("enter_leave", {"min": 0.4, "max": 0.85, "step": 0.03})
    limits.setdefault("min_evidence", {"min": 1, "max": 4, "step": 1})
    limits.setdefault("arm_delay_s", {"min": 0, "max": 90, "step": 5})
    limits.setdefault("leave_window_min", {"min": 15, "max": 180, "step": 15})
    limits.setdefault("w_time", {"min": 0.05, "max": 0.35, "step": 0.05})
    limits.setdefault("w_geo", {"min": 0.05, "max": 0.4, "step": 0.05})
    limits.setdefault("weekday_leave_company_hour", {"min": 11.0, "max": 21.0, "step": 0.1})

    theta = {**DEFAULT_THETA, **{k: v for k, v in theta_body.items() if k != "coordinate_system"}}
    print(
        f"loaded θ from {theta_path}: enter_leave={theta['enter_leave']} "
        f"min_evidence={theta['min_evidence']} walk_hold_s={theta.get('walk_hold_s')}"
    )

    all_param_changes: List[dict] = []
    iter_reports = []

    for it in range(args.iters):
        print(f"\n========== ITER {it} θ={ {k:theta[k] for k in ('enter_leave','min_evidence','arm_delay_s','leave_window_min','w_time','w_geo')} } ==========")
        iter_dir = out_root / f"iter{it}"
        iter_dir.mkdir(parents=True, exist_ok=True)
        write_json(iter_dir / "theta.json", {"coordinate_system": "WGS84", **theta})

        summaries = []
        for name in args.sessions:
            raw_dir = data_root / name
            if not raw_dir.is_dir():
                print(f"SKIP missing {raw_dir}")
                summaries.append({"ok": False, "session": name, "error": "missing"})
                continue
            sens_name = SESSION_SENSOR_MAP.get(name, name)
            real_sensor_dir = sensor_root / sens_name
            if (real_sensor_dir / "sensor_events.csv").exists():
                sensor_dir = real_sensor_dir
                print(f"\n--- {name} sensor={real_sensor_dir} ---")
            elif args.allow_synth:
                sensor_dir = iter_dir / name / "_synth_sensor"
                sensor_dir.mkdir(parents=True, exist_ok=True)
                stats = synthesize_sensor_events(raw_dir, sensor_dir / "sensor_events.csv")
                print(f"\n--- {name} SYNTH sensor={stats} ---")
            else:
                print(f"SKIP no sensor_events for {name} (looked in {real_sensor_dir})")
                summaries.append({"ok": False, "session": name, "error": "no_sensor"})
                continue
            summary = replay_session(
                raw_dir=raw_dir,
                sensor_dir=sensor_dir,
                out_dir=iter_dir / name,
                anchors_path=Path(args.anchors),
                theta=theta,
                company_radio_fingerprint=fingerprint_path,
            )
            summary["sensor_dir"] = str(sensor_dir)
            summaries.append(summary)
            if summary.get("ok"):
                print(
                    f"  GT leaves={len(summary['gt_leave_company'])} "
                    f"company_pushes={len(summary['company_pushes'])} "
                    f"walks={summary.get('walk_events')} "
                    f"scenes={summary['scene_counts']}"
                )
                for gt in summary["gt_leave_company"]:
                    print(f"  t* leave_company {gt['t_star_iso']}")
                for p in summary["company_pushes"]:
                    print(f"  PUSH {p['t']} sc={p['score_company']:.2f} rel={p['company_relation']}")

        metrics = evaluate_batch(summaries, theta)
        write_json(iter_dir / "metrics.json", metrics)
        print("METRICS", json.dumps(metrics, ensure_ascii=False, indent=2))
        iter_reports.append({"iter": it, "metrics": metrics, "theta": deepcopy(theta)})

        if it + 1 >= args.iters:
            break
        new_theta, changes = iterate_theta(theta, metrics, limits)
        if not changes:
            print("no θ changes; stop early")
            break
        for c in changes:
            c["iter"] = it
            c["t_ms"] = ms(datetime.now(CST))
            all_param_changes.append(c)
            print(f"  Δ {c['param']}: {c['old']} → {c['new']} | {c['reason']}")
        theta = new_theta

    # final artifacts at out_root
    write_json(out_root / "theta.json", {"coordinate_system": "WGS84", **theta})
    with (out_root / "param_changes.jsonl").open("w", encoding="utf-8") as f:
        for c in all_param_changes:
            f.write(json.dumps(c, ensure_ascii=False) + "\n")
    write_json(out_root / "iteration_report.json", {"iters": iter_reports, "final_theta": theta})

    # human summary
    lines = [
        "# Leave-company 5-session batch SUMMARY",
        "",
        f"- data_root: `{data_root}`",
        f"- sessions: {args.sessions}",
        f"- iters: {len(iter_reports)}",
        "",
        "## Final θ",
        "```json",
        json.dumps({k: theta[k] for k in sorted(theta.keys()) if k != "param_limits"}, ensure_ascii=False, indent=2),
        "```",
        "",
        "## Per-iter metrics",
    ]
    for rep in iter_reports:
        m = rep["metrics"]
        lines.append(
            f"- iter{rep['iter']}: GT={m['n_gt_leave_company']} push={m['n_company_push']} "
            f"confirmed={m['n_confirmed']} false={m['n_false_push']} missed={m['n_missed']} "
            f"leads={m['leads_s']} mean_lead={m['mean_lead_s']}"
        )
    lines.append("")
    lines.append("## Param changes")
    if not all_param_changes:
        lines.append("_none_")
    for c in all_param_changes:
        lines.append(f"- iter{c['iter']}: `{c['param']}` {c['old']}→{c['new']} — {c['reason']}")
    lines.append("")
    lines.append("## Leave-company times (last iter GT)")
    last = iter_reports[-1]["iter"] if iter_reports else 0
    last_dir = out_root / f"iter{last}"
    for name in args.sessions:
        p = last_dir / name / "leave_company_summary.json"
        if not p.exists():
            continue
        s = json.loads(p.read_text(encoding="utf-8"))
        lines.append(f"### {name}")
        for gt in s.get("gt_leave_company") or []:
            lines.append(f"- t* `{gt['t_star_iso']}` dist={gt['dist_m']}m")
        for m in s.get("matched_leads") or []:
            if m.get("match"):
                lines.append(
                    f"  - matched push `{m['match']['t_push_iso']}` lead={m['match']['lead_s']:.1f}s"
                )
            else:
                lines.append("  - **MISSED** (no LEAVING_COMPANY push before t*)")
    (out_root / "SUMMARY.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"\nWrote {out_root / 'SUMMARY.md'}")
    print(f"Final theta → {out_root / 'theta.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
