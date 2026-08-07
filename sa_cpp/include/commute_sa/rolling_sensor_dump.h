#pragma once

#include "commute_sa/types.h"

#include <cstdint>
#include <string>

namespace commute_sa {

/**
 * Rolling CSV dumps for location + IMU + radio.
 *
 * Location rows are ALWAYS WGS84 and include coordinate_system column.
 * Format (compatible with python/commute_baseline/io_data.py):
 *   wallTsMs,latitude,longitude,accuracy,sourceType,power_mode,coordinate_system
 *
 * No dependency on helloworld_agent. Wire from your SA location callback:
 *   dump.EnqueueLocationWgs84(ts, lat, lon, accuracy, sourceType);
 */
class RollingSensorDump {
public:
    static constexpr int64_t kDefaultRollIntervalMs = 10 * 60 * 1000;

    explicit RollingSensorDump(std::string dumpRoot, int64_t rollIntervalMs = kDefaultRollIntervalMs);
    ~RollingSensorDump();

    RollingSensorDump(const RollingSensorDump &) = delete;
    RollingSensorDump &operator=(const RollingSensorDump &) = delete;

    /** Create session dir under dumpRoot and start writer thread. */
    bool Start();
    void Stop();

    bool Running() const;
    const std::string &SessionDir() const;

    void SetPowerMode(std::string mode);

    /** Enqueue WGS84 location (do NOT convert to GCJ). */
    void EnqueueLocationWgs84(TimestampMs wallTsMs, double latitude, double longitude, double accuracyM,
        int32_t sourceType);

    void EnqueueImu(const char *sensor /*acc|gyro|mag*/, TimestampMs wallTsMs, double x, double y, double z);
    void EnqueueRv(TimestampMs wallTsMs, double x, double y, double z, double w);
    void EnqueueBaro(TimestampMs wallTsMs, double pressure);
    void EnqueueWifiLine(TimestampMs wallTsMs, const std::string &csvLineWithoutPower);
    void EnqueueBleLine(TimestampMs wallTsMs, const std::string &csvLineWithoutPower);
    void EnqueueCellLine(TimestampMs wallTsMs, const std::string &csvLineWithoutPower);

private:
    struct Impl;
    Impl *impl_;
};

}  // namespace commute_sa
