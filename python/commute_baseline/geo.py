"""Geo helpers (WGS84)."""

from __future__ import annotations

import math
from enum import Enum
from typing import Tuple


def haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    r = 6371000.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


class Relation(str, Enum):
    INSIDE = "INSIDE"
    NEAR = "NEAR"
    OUTSIDE = "OUTSIDE"
    UNKNOWN = "UNKNOWN"


def relation_to_anchor(
    lat: float,
    lon: float,
    anchor_lat: float,
    anchor_lon: float,
    r_in_m: float,
    r_out_m: float,
    acc_m: float | None = None,
    max_acc_m: float = 80.0,
) -> Tuple[Relation, float]:
    if acc_m is not None and acc_m > max_acc_m:
        # Network fixes with huge reported acc may still be usable if we
        # explicitly allow them for dwell; caller can pass max_acc_m=1e9.
        pass
    d = haversine_m(lat, lon, anchor_lat, anchor_lon)
    if d <= r_in_m:
        return Relation.INSIDE, d
    if d < r_out_m:
        return Relation.NEAR, d
    return Relation.OUTSIDE, d
