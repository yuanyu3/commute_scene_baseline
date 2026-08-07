#pragma once

/**
 * China GCJ-02 <-> WGS84 helpers.
 * Ported from helloworld GeoEngine (algorithm only); this project owns the copy.
 * All commute_scene_baseline inference / dumps use WGS84.
 */

namespace commute_sa {

struct LatLon {
    double latitude = 0.0;
    double longitude = 0.0;
};

bool IsInChina(double latitude, double longitude);

/** OHOS location APIs typically return WGS84; use only if you need map-display GCJ. */
LatLon Wgs84ToGcj02(double latitude, double longitude);

/** Convert map/GCJ anchors into WGS84 for geofence / dump. */
LatLon Gcj02ToWgs84(double latitude, double longitude);

}  // namespace commute_sa
