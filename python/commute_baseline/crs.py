"""CRS helpers: unify on WGS84 for all baseline logic.

China GCJ-02 <-> WGS84 conversion (approx). Used when replaying
helloworld location_data dumps that were written in GCJ-02.
"""

from __future__ import annotations

import math
from typing import Literal, Tuple

SourceCrs = Literal["WGS84", "GCJ02"]

_A = 6378245.0
_EE = 0.00669342162296594323


def _out_of_china(lat: float, lon: float) -> bool:
    return not (0.8293 <= lat <= 55.8271 and 72.004 <= lon <= 137.8347)


def _transform_lat(x: float, y: float) -> float:
    ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * math.sqrt(abs(x))
    ret += (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
    ret += (20.0 * math.sin(y * math.pi) + 40.0 * math.sin(y / 3.0 * math.pi)) * 2.0 / 3.0
    ret += (160.0 * math.sin(y / 12.0 * math.pi) + 320.0 * math.sin(y * math.pi / 30.0)) * 2.0 / 3.0
    return ret


def _transform_lon(x: float, y: float) -> float:
    ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * math.sqrt(abs(x))
    ret += (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
    ret += (20.0 * math.sin(x * math.pi) + 40.0 * math.sin(x / 3.0 * math.pi)) * 2.0 / 3.0
    ret += (150.0 * math.sin(x / 12.0 * math.pi) + 300.0 * math.sin(x / 30.0 * math.pi)) * 2.0 / 3.0
    return ret


def wgs84_to_gcj02(lat: float, lon: float) -> Tuple[float, float]:
    if _out_of_china(lat, lon):
        return lat, lon
    dlat = _transform_lat(lon - 105.0, lat - 35.0)
    dlon = _transform_lon(lon - 105.0, lat - 35.0)
    rad_lat = lat / 180.0 * math.pi
    magic = 1 - _EE * math.sin(rad_lat) ** 2
    sqrt_magic = math.sqrt(magic)
    dlat = (dlat * 180.0) / ((_A * (1 - _EE)) / (magic * sqrt_magic) * math.pi)
    dlon = (dlon * 180.0) / (_A / sqrt_magic * math.cos(rad_lat) * math.pi)
    return lat + dlat, lon + dlon


def gcj02_to_wgs84(lat: float, lon: float) -> Tuple[float, float]:
    """Approximate inverse via iteration."""
    if _out_of_china(lat, lon):
        return lat, lon
    wgs_lat, wgs_lon = lat, lon
    for _ in range(8):
        glat, glon = wgs84_to_gcj02(wgs_lat, wgs_lon)
        wgs_lat -= glat - lat
        wgs_lon -= glon - lon
    return wgs_lat, wgs_lon


def ensure_wgs84(lat: float, lon: float, source_crs: SourceCrs) -> Tuple[float, float]:
    if source_crs == "WGS84":
        return lat, lon
    if source_crs == "GCJ02":
        return gcj02_to_wgs84(lat, lon)
    raise ValueError(f"unknown crs: {source_crs}")
