"""Multi-scale, scene-neutral leave risk features and lightweight models."""

from __future__ import annotations

import csv
import json
import math
from bisect import bisect_right
from collections import deque
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterable, List, Sequence

import numpy as np

from .radio_evidence import load_cell_samples, load_wifi_scans


WINDOWS_S = (15, 30, 60)
HORIZONS_S = (30, 60, 120)


@dataclass
class RiskRow:
    group_id: str
    session: str
    label: str
    route: str
    t_ms: int
    truth_t_ms: int | None
    gps_source_type: int
    returning_to_company: bool
    features: Dict[str, float]


def _load_xyz(session: Path, prefix: str) -> List[tuple[int, float]]:
    out = []
    for path in sorted(session.glob(f"{prefix}_data_*.csv")):
        with path.open(newline="", encoding="utf-8-sig", errors="replace") as f:
            for row in csv.DictReader(f):
                try:
                    t = int(float(row["wallTsMs"]))
                    value = math.sqrt(sum(float(row[k]) ** 2 for k in ("x", "y", "z")))
                except (KeyError, ValueError, TypeError):
                    continue
                # Device-local normalization handles the few corrupted magnetic dumps.
                if math.isfinite(value):
                    out.append((t, value))
    return sorted(out)


def _load_sensor_events(session: Path):
    pdr = []
    walking_events = []
    gps_types = []
    path = session / "sensor_events.csv"
    if not path.is_file():
        return pdr, walking_events, gps_types
    origin = None
    cumulative = 0.0
    previous = None
    with path.open(newline="", encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            try:
                t_ms = int(datetime.fromisoformat(row["source_observed_at"]).timestamp() * 1000)
            except (KeyError, ValueError):
                continue
            event = row.get("event_type")
            if event == "WALKING_STARTED":
                walking_events.append((t_ms, 1))
                origin = None
                previous = None
                cumulative = 0.0
            elif event in ("WALKING_STOPPED", "WALKING_ENDED"):
                walking_events.append((t_ms, 0))
            elif event == "PDR_POINT":
                try:
                    body = json.loads(row["payload_json"])
                    x, y = float(body["x"]), float(body["y"])
                except (KeyError, ValueError, TypeError, json.JSONDecodeError):
                    continue
                if origin is None:
                    origin = (x, y)
                if previous is not None:
                    cumulative += math.hypot(x - previous[0], y - previous[1])
                previous = (x, y)
                pdr.append((t_ms, x, y, cumulative, math.hypot(x - origin[0], y - origin[1])))
            elif event == "GPS_REPORT":
                try:
                    body = json.loads(row["payload_json"])
                    source_type = int(body.get("source_type") or 0)
                    if source_type in (1, 2):
                        gps_types.append((t_ms, source_type))
                except (ValueError, TypeError, json.JSONDecodeError):
                    pass
    return pdr, sorted(walking_events), sorted(gps_types)


def _latest(series: Sequence, times: Sequence[int], t_ms: int):
    i = bisect_right(times, t_ms) - 1
    return series[i] if i >= 0 else None


def _window_values(series: Sequence[tuple[int, float]], t_ms: int, window_s: int) -> List[float]:
    lo = t_ms - window_s * 1000
    return [v for t, v in series if lo <= t <= t_ms]


def _stats(values: Sequence[float]) -> tuple[float, float]:
    if not values:
        return 0.0, 0.0
    mean = sum(values) / len(values)
    var = sum((x - mean) ** 2 for x in values) / len(values)
    return mean, math.sqrt(var)


def extract_session_rows(
    session: Path,
    group_id: str,
    label: str,
    route: str,
    wifi_core: set[str],
    company_cells: set[int],
    tick_s: int = 5,
    truth_mode: str = "gps_type1",
) -> List[RiskRow]:
    pdr, walking_events, gps_types = _load_sensor_events(session)
    acc = _load_xyz(session, "acc")
    gyro = _load_xyz(session, "gyro")
    mag = _load_xyz(session, "mag")
    wifi = load_wifi_scans([str(session)])
    cells = load_cell_samples([str(session)])
    all_times = [x[0] for x in pdr] + [x[0] for x in walking_events] + [x[0] for x in acc]
    if not all_times:
        return []
    start, end = min(all_times), max(all_times)
    starts_outside = bool(gps_types and gps_types[0][1] == 1)
    # The only departure truth is the first 2 -> 1 transition. A session that
    # starts at type=1 is an outside/returning trace, never a leave episode.
    truth = None
    previous_type = None
    for t_ms, source_type in gps_types:
        if previous_type == 2 and source_type == 1:
            truth = t_ms
            break
        previous_type = source_type
    if label != "LEAVE" or starts_outside:
        truth = None
    if truth is not None:
        end = min(end, truth)

    pdr_t = [x[0] for x in pdr]
    walk_t = [x[0] for x in walking_events]
    cell_t = [x.t_ms for x in cells]
    wifi_t = [x.t_ms for x in wifi]
    gps_t = [x[0] for x in gps_types]
    mag_values = [v for _, v in mag]
    mag_center = float(np.median(mag_values)) if mag_values else 0.0
    mag_scale = float(np.median(np.abs(np.asarray(mag_values) - mag_center))) if mag_values else 1.0
    mag_scale = max(1.0, mag_scale)
    mag_norm = [(t, (v - mag_center) / mag_scale) for t, v in mag]
    rows = []
    for t_ms in range(start, end + 1, tick_s * 1000):
        current_gps = _latest(gps_types, gps_t, t_ms)
        gps_source_type = current_gps[1] if current_gps else 0
        current_walk = _latest(walking_events, walk_t, t_ms)
        walking = float(bool(current_walk and current_walk[1]))
        walk_started = 0
        for et, state in reversed(walking_events):
            if et <= t_ms and state == 1:
                walk_started = et
                break
        current_pdr = _latest(pdr, pdr_t, t_ms)
        cur_path = current_pdr[3] if current_pdr else 0.0
        cur_net = current_pdr[4] if current_pdr else 0.0
        features: Dict[str, float] = {
            "walking": walking,
            "walking_duration_s": max(0.0, (t_ms - walk_started) / 1000.0) if walking and walk_started else 0.0,
            "pdr_net_m": cur_net,
            "wifi_missing": float(not wifi),
            "cell_missing": float(not cells),
            "mag_missing": float(not mag),
        }
        for window in WINDOWS_S:
            old = _latest(pdr, pdr_t, t_ms - window * 1000)
            old_path = old[3] if old else 0.0
            old_net = old[4] if old else 0.0
            path_delta = max(0.0, cur_path - old_path)
            net_delta = abs(cur_net - old_net)
            features[f"pdr_path_{window}s"] = path_delta
            features[f"pdr_net_delta_{window}s"] = net_delta
            features[f"pdr_efficiency_{window}s"] = net_delta / max(1.0, path_delta)
            for name, series in (("acc", acc), ("gyro", gyro), ("mag", mag_norm)):
                values = _window_values(series, t_ms, window)
                mean, std = _stats(values)
                features[f"{name}_mean_{window}s"] = mean
                features[f"{name}_std_{window}s"] = std
        scan = _latest(wifi, wifi_t, t_ms)
        if scan is not None:
            current_bssid = {a.bssid.lower() for a in scan.aps if a.bssid and a.rssi >= -85}
            features["wifi_core_matches"] = float(len(current_bssid & wifi_core))
            features["wifi_core_coverage"] = len(current_bssid & wifi_core) / max(1, len(wifi_core))
        else:
            features["wifi_core_matches"] = 0.0
            features["wifi_core_coverage"] = 0.0
        for window in (30, 60):
            old_scan = _latest(wifi, wifi_t, t_ms - window * 1000)
            if old_scan is None:
                old_cov = features["wifi_core_coverage"]
            else:
                old_set = {a.bssid.lower() for a in old_scan.aps if a.bssid and a.rssi >= -85}
                old_cov = len(old_set & wifi_core) / max(1, len(wifi_core))
            features[f"wifi_coverage_delta_{window}s"] = features["wifi_core_coverage"] - old_cov
        cell = _latest(cells, cell_t, t_ms)
        features["cell_company_match"] = float(bool(cell and cell.cell_id in company_cells))
        for window in (30, 60):
            lo = t_ms - window * 1000
            ids = [x.cell_id for x in cells if lo <= x.t_ms <= t_ms and x.cell_id]
            features[f"cell_unique_{window}s"] = float(len(set(ids)))
            features[f"cell_changes_{window}s"] = float(sum(a != b for a, b in zip(ids, ids[1:])))
        rows.append(RiskRow(
            group_id, session.name, label, route, t_ms, truth,
            gps_source_type, starts_outside, features,
        ))
    return rows


def load_event_rows(data_root: Path, groups_path: Path, fingerprint_path: Path) -> List[RiskRow]:
    groups = json.loads(groups_path.read_text(encoding="utf-8"))["groups"]
    fp = json.loads(fingerprint_path.read_text(encoding="utf-8"))["company"]
    wifi_core = {x.lower() for x in fp.get("wifi", {}).get("bssids", [])}
    company_cells = {int(x) for x in fp.get("cell", {}).get("cell_ids", [])}
    out = []
    for group in groups:
        if group["label"] == "UNKNOWN":
            continue
        for name in group["sessions"]:
            session = data_root / name
            if session.is_dir():
                out.extend(extract_session_rows(
                session, group["id"], group["label"], group.get("route", "unknown"),
                    wifi_core, company_cells, truth_mode=group.get("truth_mode", "gps_type1"),
                ))
    return out


def feature_names(rows: Iterable[RiskRow]) -> List[str]:
    names = set()
    for row in rows:
        names.update(row.features)
    return sorted(names)


def label_for(row: RiskRow, horizon_s: int) -> int:
    if row.label != "LEAVE" or row.truth_t_ms is None:
        return 0
    delta = row.truth_t_ms - row.t_ms
    return int(0 < delta <= horizon_s * 1000)


class PortableRiskModel:
    """Dependency-free inference for the JSON exported by train_leave_risk."""

    def __init__(self, body: dict):
        self.features = list(body["features"])
        self.heads = body["heads"]

    @classmethod
    def load(cls, path: Path | str) -> "PortableRiskModel":
        return cls(json.loads(Path(path).read_text(encoding="utf-8")))

    def predict(self, features: Dict[str, float]) -> Dict[int, float]:
        x = [float(features.get(name, 0.0)) for name in self.features]
        out = {}
        for horizon, head in self.heads.items():
            z = float(head["intercept"])
            for value, mean, scale, weight in zip(x, head["mean"], head["scale"], head["coef"]):
                normalized = (value - mean) / max(1e-9, scale)
                z += normalized * weight
            z = max(-40.0, min(40.0, z))
            out[int(horizon)] = 1.0 / (1.0 + math.exp(-z))
        return out
