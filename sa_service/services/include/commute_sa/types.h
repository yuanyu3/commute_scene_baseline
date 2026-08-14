#pragma once

#include <cstdint>
#include <string>

namespace commute_sa {

using TimestampMs = int64_t;

/** Canonical CRS for all dumps and scene logic in this project. */
constexpr const char *kCoordinateSystemWgs84 = "WGS84";

/**
 * Latest GPS fix from the platform location API.
 * latitude/longitude MUST be WGS84 (OHOS GetLatitude/GetLongitude typically are).
 */
struct RawGpsLocation {
    TimestampMs observed_at = 0;
    TimestampMs received_at = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double horizontal_accuracy_m = 0.0;
    bool has_horizontal_accuracy = false;
    bool valid = false;
    int32_t source_type = 0;
};

/**
 * Location::GetLocationSourceType() near the company campus:
 * GNSS (1) means already outside the gate; network/indoor (2) means still inside.
 * GPS r_in/r_out is only a vicinity hint for this gate, not the leave decision.
 */
constexpr int32_t kLocationSourceOutdoorGnss = 1;
constexpr int32_t kLocationSourceIndoorNetwork = 2;

enum class MotionState {
    kUnknown = 0,
    kWalking = 1,
    kNotWalking = 2,
};

enum class SensorEventType {
    kWalkingStarted = 0,
    kWalkingStopped = 1,
    kPdrPoint = 2,
    kGpsReport = 3,
};

inline const char *MotionStateToString(MotionState s)
{
    switch (s) {
        case MotionState::kWalking:
            return "WALKING";
        case MotionState::kNotWalking:
            return "NOT_WALKING";
        default:
            return "UNKNOWN";
    }
}

inline const char *SensorEventTypeToString(SensorEventType t)
{
    switch (t) {
        case SensorEventType::kWalkingStarted:
            return "WALKING_STARTED";
        case SensorEventType::kWalkingStopped:
            return "WALKING_STOPPED";
        case SensorEventType::kPdrPoint:
            return "PDR_POINT";
        case SensorEventType::kGpsReport:
            return "GPS_REPORT";
        default:
            return "UNKNOWN";
    }
}

struct SensorDebugEvent {
    uint64_t sequence_id = 0;
    TimestampMs received_at = 0;
    TimestampMs source_observed_at = 0;
    SensorEventType event_type = SensorEventType::kGpsReport;
    MotionState motion_state = MotionState::kUnknown;
    std::string episode_id;
    std::string payload_json;
    std::string power_mode = "HIGH_STILL";
};

}  // namespace commute_sa
