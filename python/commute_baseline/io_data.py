"""Load helloworld dumps into WGS84 GpsPoint streams."""

from __future__ import annotations

import csv
import glob
import json
import os
from datetime import datetime, timedelta, timezone
from typing import List, Optional, Tuple

from .anchors import GpsPoint
from .crs import ensure_wgs84

CST = timezone(timedelta(hours=8))


def _ms_to_dt(ms: int) -> datetime:
    return datetime.fromtimestamp(ms / 1000.0, tz=CST)


def load_location_csv_dir(raw_dir: str, source_crs: str = "GCJ02") -> List[GpsPoint]:
    """Load location_data_*.csv into WGS84 GpsPoints.

    - helloworld dumps: typically GCJ-02 → pass source_crs=\"GCJ02\" (default).
    - project-converted dumps (convert_location_to_wgs84.py): already WGS84;
      file may contain coordinate_system=WGS84 column (auto-detected per row).
    helloworld_agent itself is never modified.
    """
    out: List[GpsPoint] = []
    for path in sorted(glob.glob(os.path.join(raw_dir, "location_data_*.csv"))):
        with open(path, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                try:
                    lat = float(row["latitude"])
                    lon = float(row["longitude"])
                    row_crs = (row.get("coordinate_system") or source_crs or "GCJ02").upper()
                    if row_crs in ("WGS84", "WGS_84"):
                        row_crs = "WGS84"
                    elif row_crs in ("GCJ02", "GCJ-02", "GCJ_02"):
                        row_crs = "GCJ02"
                    else:
                        row_crs = source_crs
                    lat, lon = ensure_wgs84(lat, lon, row_crs)  # type: ignore[arg-type]
                    out.append(
                        GpsPoint(
                            t=_ms_to_dt(int(float(row["wallTsMs"]))),
                            lat=lat,
                            lon=lon,
                            acc=float(row.get("accuracy") or 999),
                            source_type=int(float(row.get("sourceType") or 0)),
                        )
                    )
                except (KeyError, ValueError):
                    continue
    out.sort(key=lambda p: p.t)
    return out


def load_sensor_gps(sensor_dir: str) -> List[GpsPoint]:
    """sensor_events GPS_REPORT is WGS84."""
    path = os.path.join(sensor_dir, "sensor_events.csv")
    out: List[GpsPoint] = []
    if not os.path.isfile(path):
        return out
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row.get("event_type") != "GPS_REPORT":
                continue
            try:
                p = json.loads(row["payload_json"])
                if not p.get("valid", True):
                    continue
                out.append(
                    GpsPoint(
                        t=datetime.fromisoformat(row["source_observed_at"]),
                        lat=float(p["latitude"]),
                        lon=float(p["longitude"]),
                        acc=float(p.get("horizontal_accuracy_m") or 999),
                        source_type=int(p.get("source_type") or 0),
                    )
                )
            except (KeyError, ValueError, json.JSONDecodeError):
                continue
    out.sort(key=lambda p: p.t)
    return out


def load_walking_events(sensor_dir: str) -> List[Tuple[datetime, str]]:
    path = os.path.join(sensor_dir, "sensor_events.csv")
    out = []
    if not os.path.isfile(path):
        return out
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            et = row.get("event_type")
            if et in ("WALKING_STARTED", "WALKING_STOPPED", "WALKING_ENDED"):
                # Normalize ENDED → STOPPED for callers that branch on stop.
                norm = "WALKING_STOPPED" if et == "WALKING_ENDED" else et
                out.append((datetime.fromisoformat(row["source_observed_at"]), norm))
    return out


def load_pdr_net_series(sensor_dir: str) -> List[Tuple[datetime, float]]:
    """Per-PDR-point cumulative net displacement (m) within each walk episode.

    Same definition as C++ PdrEvidence / SA cumulative.net_displacement_m:
    planar distance from episode origin (x,y) to current point.
    """
    path = os.path.join(sensor_dir, "sensor_events.csv")
    if not os.path.isfile(path):
        return []
    out: List[Tuple[datetime, float]] = []
    origin: Optional[Tuple[float, float]] = None
    episode = ""
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            et = row.get("event_type")
            if et == "WALKING_STARTED":
                origin = None
                episode = row.get("episode_id") or ""
                continue
            if et == "WALKING_STOPPED":
                origin = None
                episode = ""
                continue
            if et != "PDR_POINT":
                continue
            try:
                t = datetime.fromisoformat(row["source_observed_at"])
                p = json.loads(row["payload_json"])
                x = float(p["x"])
                y = float(p["y"])
            except (KeyError, ValueError, json.JSONDecodeError, TypeError):
                continue
            eid = row.get("episode_id") or episode
            if eid != episode:
                origin = None
                episode = eid
            if origin is None:
                origin = (x, y)
                out.append((t, 0.0))
                continue
            dx = x - origin[0]
            dy = y - origin[1]
            out.append((t, (dx * dx + dy * dy) ** 0.5))
    return out


def load_baro_series(raw_dir: str) -> List[Tuple[datetime, float]]:
    """Load valid pressure samples (hPa) from baro_data_*.csv."""
    out: List[Tuple[datetime, float]] = []
    for path in sorted(glob.glob(os.path.join(raw_dir, "baro_data_*.csv"))):
        with open(path, newline="", encoding="utf-8-sig") as f:
            for row in csv.DictReader(f):
                try:
                    pressure = float(row["pressure"])
                    if 850.0 <= pressure <= 1100.0:
                        out.append((_ms_to_dt(int(float(row["wallTsMs"]))), pressure))
                except (KeyError, ValueError, TypeError):
                    continue
    out.sort(key=lambda x: x[0])
    return out


def pdr_net_at(series: List[Tuple[datetime, float]], t: datetime) -> float:
    """Latest net displacement at or before t (0 if none / gap > 3 min)."""
    if not series:
        return 0.0
    lo, hi = 0, len(series) - 1
    best = -1
    while lo <= hi:
        mid = (lo + hi) // 2
        if series[mid][0] <= t:
            best = mid
            lo = mid + 1
        else:
            hi = mid - 1
    if best < 0:
        return 0.0
    dt = (t - series[best][0]).total_seconds()
    if dt > 180:
        return 0.0
    return float(series[best][1])


def merge_gps(raw_wgs: List[GpsPoint], sensor_wgs: List[GpsPoint]) -> List[GpsPoint]:
    """Prefer denser coverage; de-dupe by second."""
    best = {}
    for p in raw_wgs + sensor_wgs:
        key = p.t.replace(microsecond=0)
        if key not in best or p.acc < best[key].acc:
            best[key] = p
    return [best[k] for k in sorted(best.keys())]
