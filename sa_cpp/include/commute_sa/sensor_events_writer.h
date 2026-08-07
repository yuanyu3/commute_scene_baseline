#pragma once

#include "commute_sa/types.h"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace commute_sa {

/**
 * Writes sensor_events.csv (and optional sa_perception_ticks.csv) in the same
 * schema as helloworld ProactiveAgentBusinessModule — but owned by this project.
 * GPS payloads always label coordinate_system=WGS84.
 */
class SensorEventsWriter {
public:
    explicit SensorEventsWriter(std::string outputRoot);
    ~SensorEventsWriter();

    SensorEventsWriter(const SensorEventsWriter &) = delete;
    SensorEventsWriter &operator=(const SensorEventsWriter &) = delete;

    bool Start();
    void Stop();

    bool Running() const;
    const std::string &RunDir() const;

    void SetPowerMode(std::string mode);

    /** Append GPS_REPORT with WGS84 payload. */
    void OnGpsLocation(const RawGpsLocation &location);

    void OnWalkingStarted(TimestampMs timestampMs);
    void OnWalkingStopped(TimestampMs timestampMs);

    /** Flush buffered events to disk. */
    void Flush();

private:
    void AppendEvent(SensorDebugEvent event);
    void WriteEventsUnlocked(const std::vector<SensorDebugEvent> &events);
    bool InitOutputsUnlocked();
    void CloseOutputsUnlocked();

    std::string outputRoot_;
    std::string runDir_;
    std::string powerMode_ = "HIGH_STILL";
    mutable std::mutex mutex_;
    bool running_ = false;
    uint64_t sequence_ = 0;
    MotionState motionState_ = MotionState::kUnknown;
    std::ofstream sensorEventsFile_;
    bool sensorEventsEnabled_ = false;
};

}  // namespace commute_sa
