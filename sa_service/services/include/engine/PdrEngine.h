/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
* Description: SA-side PDR engine bridge
*/

#ifndef PDR_ENGINE_H
#define PDR_ENGINE_H

#include <cstdint>
#include <string>

namespace OHOS::Multimedia::CameraAgentService::PdrEngine {

void PushAcc(int64_t timestampMs, double x, double y, double z);
void PushGyro(int64_t timestampMs, double x, double y, double z);
void PushMag(int64_t timestampMs, double x, double y, double z);
void PushRv(int64_t timestampMs, double x, double y, double z, double w);
/** Feed GNSS fix into Riemann PDR+GPS fusion (WGS84 lat/lon). */
void PushGps(int64_t timestampMs, double latDeg, double lonDeg, float accuracyM, float headingDeg, bool valid);

void SetActive(bool active);
bool IsActive();
void ClearResults();
/** Reset PDR walking session (trajectory, windows, GPS fusion); keeps provider instance. */
void ResetSession();
void Stop();

/** Same session folder as sensor CSV under SENSOR_DUMP_DIR (set when dump worker starts). */
void SetTrajectoryDumpDir(const std::string &dir);
void ClearTrajectoryDumpDir();
std::string GetTrajectoryDumpDir();

/** Snapshot current realtime PDR queue into SA-side ring buffer (call when a walking segment ends). */
void ArchiveCurrentQueueBeforeClear(int64_t segmentId);
void ClearSavedSegments();
size_t GetSavedSegmentCount();
std::string GetSavedSegmentsMetaJson();
/** Returns JSON array of points for segmentId and removes that saved segment; "[]" if not found. */
std::string DrainSavedSegmentJson(int64_t segmentId);

std::string DrainResultsJson();
std::string GetDiagJson();

} // namespace OHOS::Multimedia::CameraAgentService::PdrEngine

#endif // PDR_ENGINE_H
