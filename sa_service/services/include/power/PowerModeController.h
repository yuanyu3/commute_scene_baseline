/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Three power states from sensor data only — high walking / high still / low power.
 */

#ifndef POWER_MODE_CONTROLLER_H
#define POWER_MODE_CONTROLLER_H

#include <cstdint>
#include <functional>
#include <mutex>

namespace OHOS::Multimedia::CameraAgentService {

/**
 * Perception power states (no time/location gates):
 * - HIGH_WALKING: LeaveCar walking, high sample rate
 * - HIGH_STILL: not walking, short stillness, high sample rate
 * - LOW_POWER: prolonged stillness from IMU, reduced sample rate
 *
 * Walking speed sub-states (GPS) intentionally not implemented yet.
 */
enum class PowerMode {
    kHighWalking = 0,
    kHighStill = 1,
    kLowPower = 2,
};

struct PowerModeSnapshot {
    PowerMode mode = PowerMode::kHighStill;
    bool agentInferenceEnabled = true;
};

struct PowerModeDecision {
    bool changed = false;
    PowerModeSnapshot snapshot;
};

class PowerModeController {
public:
    static PowerModeController &GetInstance();

    void Reset();
    void SetEnabled(bool enabled);
    void SetOnChanged(std::function<void(const PowerModeDecision &)> handler);

    void PushAcc(int64_t timestampMs, double x, double y, double z);
    void NotifyWalking(bool walking, int64_t timestampMs);

    PowerModeDecision Evaluate(int64_t nowMs);
    PowerModeSnapshot GetSnapshot() const;
    bool IsLowPower() const;

    static const char *ModeToString(PowerMode mode);

    /**
     * Enter / exit hysteresis (ACC-only; walking is a hard exit):
     * - Enter LOW_POWER: still for 30 min (or 5 min during post-exit grace).
     * - Exit via ACC: need sustained activity ~2 min (关闹钟等短扰动不退出).
     * - Exit via walking: immediate (LeaveCar).
     * - After exit: 15 min grace — if still again for 5 min, re-enter LOW_POWER
     *   (起来关闹钟又睡下 → 不必再等满 30 分钟).
     */
    static constexpr int64_t kStillEnterLowPowerMs = 30LL * 60LL * 1000LL;   // 30 min
    static constexpr int64_t kStillReenterLowPowerMs = 5LL * 60LL * 1000LL;  // 5 min in grace
    static constexpr int64_t kLowPowerExitGraceMs = 15LL * 60LL * 1000LL;    // after leaving LP
    static constexpr int64_t kActiveExitLowPowerMs = 2LL * 60LL * 1000LL;    // 2 min sustained
    static constexpr double kStillStddevThreshold = 0.12;
    static constexpr double kActiveMagDeltaThreshold = 0.45;
    static constexpr size_t kStillWindowSamples = 200;

    // Collection cadences. IMU stays 100 Hz in LOW_POWER (LeaveCar/PDR need it);
    // GPS / scan / agent tick still throttle in low power.
    static constexpr int64_t kHighSensorIntervalNs = 10'000'000;        // 100 Hz
    static constexpr int64_t kLowSensorIntervalNs = 10'000'000;         // 100 Hz (same as high)
    static constexpr int32_t kHighLocationIntervalSec = 5;
    static constexpr int32_t kLowLocationIntervalSec = 300;             // 5 min
    static constexpr int64_t kHighScanIntervalMs = 1500;
    static constexpr int64_t kLowScanIntervalMs = 300'000;              // 5 min
    static constexpr int64_t kHighTickIntervalMs = 10'000;
    static constexpr int64_t kLowTickIntervalMs = 300'000;              // 5 min

private:
    PowerModeController() = default;

    void UpdateStillnessLocked(int64_t timestampMs, double mag);
    PowerModeSnapshot BuildSnapshotLocked() const;

    mutable std::mutex mutex_;
    bool enabled_ = false;
    PowerMode mode_ = PowerMode::kHighStill;
    bool walking_ = false;
    std::function<void(const PowerModeDecision &)> onChanged_;

    double magBuf_[kStillWindowSamples] {};
    size_t magCount_ = 0;
    size_t magIndex_ = 0;
    double magSum_ = 0.0;
    double magSumSq_ = 0.0;
    int64_t stillSinceMs_ = 0;
    int64_t activeSinceMs_ = 0;
    int64_t leftLowPowerAtMs_ = 0; // 0 = not in post-exit grace
};

} // namespace OHOS::Multimedia::CameraAgentService

#endif // POWER_MODE_CONTROLLER_H
