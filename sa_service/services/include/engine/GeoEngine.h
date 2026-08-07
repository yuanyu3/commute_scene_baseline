/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SA-side offline reverse geocoding (OSM SQLite).
 */

#ifndef GEO_ENGINE_H
#define GEO_ENGINE_H

#include <cstdint>
#include <string>

namespace OHOS::Multimedia::CameraAgentService::GeoEngine {

/** Open readonly DB if present. Safe to call multiple times. */
bool Init();

/** WGS-84 lat/lon (match beijing_geocode.db). Returns JSON place object. */
std::string ReverseGeocode(double latWgs84, double lonWgs84);

/** Arm latch: next valid GPS fixes start point (clears previous start place). */
void ArmStartPlaceLatch();

void ClearStartPlace();

/** JSON: ready, lat/lon (GCJ-02 for display), wgs84, road, poi, community, summary, ... */
std::string GetStartPlaceJson();

/** Called from location callback; latches start place when armed (uses WGS-84 for DB). */
void NotifyLocationSample(double latWgs84, double lonWgs84, double latGcj02, double lonGcj02, double accuracy);

} // namespace OHOS::Multimedia::CameraAgentService::GeoEngine

#endif // GEO_ENGINE_H
