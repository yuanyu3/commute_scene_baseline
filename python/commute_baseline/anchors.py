"""HOME / COMPANY anchor inference (WGS84)."""

from __future__ import annotations

import json
import math
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from datetime import datetime
from typing import Iterable, List, Optional, Sequence, Tuple

from .geo import haversine_m


@dataclass
class GpsPoint:
    t: datetime
    lat: float  # WGS84
    lon: float
    acc: float
    source_type: int = 1


@dataclass
class Anchor:
    id: str
    lat: float
    lon: float
    r_in_m: float
    r_out_m: float
    method: str


@dataclass
class AnchorSet:
    coordinate_system: str
    home: Anchor
    company: Anchor
    updated_at: str
    notes: str = ""


def _median(xs: Sequence[float]) -> float:
    ys = sorted(xs)
    n = len(ys)
    if n == 0:
        return 0.0
    if n % 2:
        return ys[n // 2]
    return 0.5 * (ys[n // 2 - 1] + ys[n // 2])


def _percentile(xs: Sequence[float], p: float) -> float:
    ys = sorted(xs)
    if not ys:
        return 0.0
    k = min(len(ys) - 1, max(0, int(round((len(ys) - 1) * p))))
    return ys[k]


def _cluster_vote(
    points: Sequence[GpsPoint], grid_m: float = 40.0
) -> Tuple[float, float, int, List[GpsPoint]]:
    lat0 = _median([p.lat for p in points])
    lon0 = _median([p.lon for p in points])
    m_lat = 111320.0
    m_lon = 111320.0 * math.cos(math.radians(lat0))
    votes: Counter[Tuple[int, int]] = Counter()
    buckets: dict[Tuple[int, int], List[GpsPoint]] = defaultdict(list)
    for g in points:
        ix = int(round((g.lat - lat0) * m_lat / grid_m))
        iy = int(round((g.lon - lon0) * m_lon / grid_m))
        votes[(ix, iy)] += 1
        buckets[(ix, iy)].append(g)
    best, _ = votes.most_common(1)[0]
    pts = buckets[best]
    return _median([p.lat for p in pts]), _median([p.lon for p in pts]), len(pts), pts


def _radius_from_members(center_lat: float, center_lon: float, members: Sequence[GpsPoint]) -> Tuple[float, float]:
    dists = [haversine_m(p.lat, p.lon, center_lat, center_lon) for p in members]
    r_in = max(50.0, min(120.0, _percentile(dists, 0.9)))
    r_out = r_in + 40.0
    return r_in, r_out


def _still_segments(points: Sequence[GpsPoint], max_speed_mps: float = 1.5) -> List[GpsPoint]:
    """Keep points that are locally slow (incl. repeated network fixes)."""
    if not points:
        return []
    out = [points[0]]
    for i in range(1, len(points)):
        a, b = points[i - 1], points[i]
        dt = max(0.5, (b.t - a.t).total_seconds())
        speed = haversine_m(a.lat, a.lon, b.lat, b.lon) / dt
        if speed <= max_speed_mps:
            out.append(b)
    return out


def infer_company(points: Sequence[GpsPoint], work_hours: range = range(9, 18)) -> Anchor:
    day = [p for p in points if p.t.hour in work_hours]
    still = _still_segments(day)
    pool = still if len(still) >= 30 else day
    if len(pool) < 10:
        raise RuntimeError("not enough daytime GPS for company")
    lat, lon, n, members = _cluster_vote(pool, grid_m=35.0)
    r_in, r_out = _radius_from_members(lat, lon, members)
    return Anchor("company_001", lat, lon, r_in, r_out, f"daytime_still_cluster_n={n}")


def infer_home(
    points: Sequence[GpsPoint],
    company: Optional[Anchor] = None,
    min_sep_m: float = 2000.0,
) -> Anchor:
    """Prefer evening/night still clusters far from company; allow high-acc network dwell."""
    still = _still_segments(points, max_speed_mps=1.2)
    # Night 0-5 + late evening after 19:30
    nightish = [
        p
        for p in still
        if p.t.hour < 6 or p.t.hour >= 23 or (p.t.hour == 19 and p.t.minute >= 20) or p.t.hour >= 20
    ]
    # Also: long repeated identical network points (indoor dwell)
    rounded = Counter((round(p.lat, 5), round(p.lon, 5)) for p in still)
    sticky_keys = {k for k, c in rounded.items() if c >= 5}
    sticky = [p for p in still if (round(p.lat, 5), round(p.lon, 5)) in sticky_keys]

    pool = nightish + sticky
    if company is not None:
        pool = [p for p in pool if haversine_m(p.lat, p.lon, company.lat, company.lon) >= min_sep_m]
    if len(pool) < 8:
        # fallback: farthest still cluster from company
        if company is None or not still:
            raise RuntimeError("not enough GPS for home")
        far = sorted(still, key=lambda p: -haversine_m(p.lat, p.lon, company.lat, company.lon))
        pool = far[: max(20, len(far) // 4)]

    lat, lon, n, members = _cluster_vote(pool, grid_m=35.0)
    if company is not None:
        sep = haversine_m(lat, lon, company.lat, company.lon)
        if sep < min_sep_m:
            raise RuntimeError(f"home too close to company: {sep:.0f}m")

    r_in, r_out = _radius_from_members(lat, lon, members)
    return Anchor("home_001", lat, lon, r_in, r_out, f"night_still+sticky_n={n}")


def build_anchors(points_wgs84: Sequence[GpsPoint]) -> AnchorSet:
    company = infer_company(points_wgs84)
    home = infer_home(points_wgs84, company)
    return AnchorSet(
        coordinate_system="WGS84",
        home=home,
        company=company,
        updated_at=datetime.now().isoformat(timespec="seconds"),
        notes="home≠daytime peak; allow network dwell for indoor",
    )


def anchors_to_dict(a: AnchorSet) -> dict:
    return {
        "coordinate_system": a.coordinate_system,
        "home": asdict(a.home),
        "company": asdict(a.company),
        "updated_at": a.updated_at,
        "notes": a.notes,
    }


def save_anchors(path: str, a: AnchorSet) -> None:
    with open(path, "w", encoding="utf-8") as f:
        json.dump(anchors_to_dict(a), f, ensure_ascii=False, indent=2)


def load_anchors(path: str) -> AnchorSet:
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    return AnchorSet(
        coordinate_system=d["coordinate_system"],
        home=Anchor(**d["home"]),
        company=Anchor(**d["company"]),
        updated_at=d.get("updated_at", ""),
        notes=d.get("notes", ""),
    )
