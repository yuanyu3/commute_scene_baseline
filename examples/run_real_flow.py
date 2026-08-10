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
from commute_baseline.crs import gcj02_to_wgs84  # noqa: E402
from commute_baseline.engine import DEFAULT_THETA, SceneEngine, TickFeatures  # noqa: E402
from commute_baseline.geo import haversine_m  # noqa: E402
from commute_baseline.io_data import (  # noqa: E402
    load_location_csv_dir,
    load_sensor_gps,
    load_walking_events,
    merge_gps,
)

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
    ap.add_argument("--tick-min-s", type=float, default=8.0)
    ap.add_argument("--run-personalizer", action="store_true", help="call WSL personalizer_llm")
    args = ap.parse_args()

    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    session = out / "session"
    session.mkdir(exist_ok=True)

    raw = load_location_csv_dir(args.raw_dir, source_crs=args.location_crs)
    sensor = load_sensor_gps(args.sensor_dir)
    merged = merge_gps(raw, sensor)
    walks = load_walking_events(args.sensor_dir)
    print(f"GPS raw={len(raw)} sensor={len(sensor)} merged={len(merged)} walks={len(walks)}")
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
    write_theta(out / "theta.json", theta)

    engine = SceneEngine(anchors, theta)
    walk_on = False
    walk_started = None
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
            else:
                walk_on = False
            wi += 1
        if last_t and (p.t - last_t).total_seconds() < args.tick_min_s:
            continue
        last_t = p.t
        feat = TickFeatures(
            t=p.t,
            lat=p.lat,
            lon=p.lon,
            acc=p.acc,
            walking=walk_on,
            walk_started_at=walk_started if walk_on else None,
        )
        d = engine.step(feat)
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
            "lead_gate_ok": d.lead_gate_ok,
            "push_block_reason": d.push_block_reason,
        }
        decisions.append(row)
        if d.should_service:
            pushes.append(row)
            print(
                f"PUSH {d.service_intent} @ {p.t.isoformat()} scene={d.scene.value} "
                f"rel={d.home_relation.value}/{d.company_relation.value} "
                f"distH={d.dist_home_m} eta={d.eta_leave_s:.1f} "
                f"sh={d.score_home:.2f} sc={d.score_company:.2f}"
            )

    # Product leave_episodes + sparse samples + settle labels
    episodes_path = out / "leave_episodes.jsonl"
    samples_path = out / "leave_window_samples.jsonl"
    by_t = {r["t_ms"]: r for r in decisions}
    sorted_dec = sorted(decisions, key=lambda r: r["t_ms"])

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
            last_rel = push["home_relation"] if "HOME" in push["service_intent"] or push["scene"] == "LEAVING_HOME" else push["company_relation"]
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
                if t_star is None and rel_now == "OUTSIDE":
                    t_star = r["t_ms"]

            # Early confirm on first OUTSIDE; else FALSE_PUSH at settle timeout.
            settle_t = t_push + int(args.settle_s * 1000)
            if t_star is not None:
                label = "CONFIRMED_LEAVE"
                t_label = t_star
            elif last_rel == "INSIDE":
                label = "FALSE_PUSH"
                t_label = min(settle_t, sorted_dec[-1]["t_ms"] if sorted_dec else settle_t)
            else:
                label = "UNKNOWN"
                t_label = min(settle_t, sorted_dec[-1]["t_ms"] if sorted_dec else settle_t)

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
                        "home_relation": last_rel if use_home else push["home_relation"],
                        "anchor_relation": last_rel,
                        "dist_home_m": last_dist if use_home and last_dist is not None else -1,
                        "t_star_ms": t_star,
                        "lead_s": lead_s,
                    },
                    ensure_ascii=False,
                )
                + "\n"
            )
            print(
                f"LABEL {label} push@{push['t']} t_label={t_label} lead_s={lead_s} "
                f"anchor_rel={last_rel} dist={last_dist}"
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
        f"- out-dir: `{out}`",
        f"- GPS merged: {len(merged)}, ticks: {len(decisions)}, pushes: {len(pushes)}",
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
            f"bash examples/personalizer_llm/run.sh {env_file} {wsl_out} --debug",
        ]
        print("running:", " ".join(cmd))
        subprocess.run(cmd, check=False)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
