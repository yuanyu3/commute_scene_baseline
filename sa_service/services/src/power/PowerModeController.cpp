/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include "PowerModeController.h"

#include <algorithm>
#include <cmath>
#include <inttypes.h>

#include "camera_agent_log.h"

namespace OHOS::Multimedia::CameraAgentService {
namespace {
constexpr double kGravity = 9.80665;
} // namespace

PowerModeController &PowerModeController::GetInstance()
{
    static PowerModeController instance;
    return instance;
}

const char *PowerModeController::ModeToString(PowerMode mode)
{
    switch (mode) {
        case PowerMode::kHighWalking:
            return "HIGH_WALKING";
        case PowerMode::kHighStill:
            return "HIGH_STILL";
        case PowerMode::kLowPower:
            return "LOW_POWER";
        default:
            return "HIGH_STILL";
    }
}

void PowerModeController::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = false;
    mode_ = PowerMode::kHighStill;
    walking_ = false;
    magCount_ = 0;
    magIndex_ = 0;
    magSum_ = 0.0;
    magSumSq_ = 0.0;
    stillSinceMs_ = 0;
    activeSinceMs_ = 0;
    leftLowPowerAtMs_ = 0;
}

void PowerModeController::SetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = enabled;
    if (!enabled) {
        mode_ = PowerMode::kHighStill;
        stillSinceMs_ = 0;
        activeSinceMs_ = 0;
        leftLowPowerAtMs_ = 0;
    }
}

void PowerModeController::SetOnChanged(std::function<void(const PowerModeDecision &)> handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    onChanged_ = std::move(handler);
}

void PowerModeController::PushAcc(int64_t timestampMs, double x, double y, double z)
{
    const double mag = std::sqrt(x * x + y * y + z * z);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_) {
        return;
    }
    UpdateStillnessLocked(timestampMs, mag);
}

void PowerModeController::NotifyWalking(bool walking, int64_t timestampMs)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_) {
            return;
        }
        walking_ = walking;
        if (walking) {
            stillSinceMs_ = 0;
        }
    }
    const int64_t nowMs = (timestampMs > 0) ? timestampMs : 0;
    if (nowMs > 0) {
        (void)Evaluate(nowMs);
    }
}

PowerModeSnapshot PowerModeController::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return BuildSnapshotLocked();
}

bool PowerModeController::IsLowPower() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return mode_ == PowerMode::kLowPower;
}

void PowerModeController::UpdateStillnessLocked(int64_t timestampMs, double mag)
{
    if (magCount_ == kStillWindowSamples) {
        const double old = magBuf_[magIndex_];
        magSum_ -= old;
        magSumSq_ -= old * old;
    } else {
        ++magCount_;
    }
    magBuf_[magIndex_] = mag;
    magSum_ += mag;
    magSumSq_ += mag * mag;
    magIndex_ = (magIndex_ + 1) % kStillWindowSamples;

    const double mean = magSum_ / static_cast<double>(magCount_);
    const double var = std::max(0.0, magSumSq_ / static_cast<double>(magCount_) - mean * mean);
    const double stddev = std::sqrt(var);
    const double magDelta = std::fabs(mag - kGravity);
    const bool sampleStill = (magCount_ >= 10) && (stddev < kStillStddevThreshold) &&
        (magDelta < kActiveMagDeltaThreshold);

    if (sampleStill) {
        if (stillSinceMs_ == 0) {
            stillSinceMs_ = timestampMs;
        }
        activeSinceMs_ = 0;
    } else {
        stillSinceMs_ = 0;
        if (activeSinceMs_ == 0) {
            activeSinceMs_ = timestampMs;
        }
    }
}

PowerModeSnapshot PowerModeController::BuildSnapshotLocked() const
{
    PowerModeSnapshot out;
    out.mode = mode_;
    // Agent runs in high-power states only; low power skips LLM.
    out.agentInferenceEnabled = (mode_ != PowerMode::kLowPower);
    return out;
}

PowerModeDecision PowerModeController::Evaluate(int64_t nowMs)
{
    PowerModeDecision out;
    std::function<void(const PowerModeDecision &)> handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out.snapshot = BuildSnapshotLocked();
        if (!enabled_) {
            return out;
        }

        const bool inGrace = (leftLowPowerAtMs_ > 0) &&
            (nowMs - leftLowPowerAtMs_ < kLowPowerExitGraceMs);
        const int64_t stillNeedMs = inGrace ? kStillReenterLowPowerMs : kStillEnterLowPowerMs;
        const bool stillLong = (stillSinceMs_ > 0) && (nowMs - stillSinceMs_ >= stillNeedMs);
        const bool activeLong = (activeSinceMs_ > 0) && (nowMs - activeSinceMs_ >= kActiveExitLowPowerMs);

        PowerMode next = mode_;
        if (walking_) {
            next = PowerMode::kHighWalking;
        } else if (mode_ == PowerMode::kLowPower) {
            // Sustained ACC activity (~2 min) exits; brief motion (关闹钟) stays in LOW_POWER.
            next = activeLong ? PowerMode::kHighStill : PowerMode::kLowPower;
        } else if (stillLong) {
            next = PowerMode::kLowPower;
        } else {
            next = PowerMode::kHighStill;
        }

        if (next != mode_) {
            CAMERA_AGENT_LOG_INFO(
                "PowerMode %{public}s -> %{public}s (walk=%{public}d stillLong=%{public}d "
                "activeLong=%{public}d grace=%{public}d stillNeedMin=%{public}" PRId64 ")",
                ModeToString(mode_), ModeToString(next),
                walking_ ? 1 : 0, stillLong ? 1 : 0, activeLong ? 1 : 0, inGrace ? 1 : 0,
                stillNeedMs / 60000);
            if (mode_ == PowerMode::kLowPower && next != PowerMode::kLowPower) {
                leftLowPowerAtMs_ = nowMs;
            } else if (next == PowerMode::kLowPower) {
                leftLowPowerAtMs_ = 0;
            }
            mode_ = next;
            stillSinceMs_ = 0;
            activeSinceMs_ = 0;
            out.changed = true;
        } else if (leftLowPowerAtMs_ > 0 && (nowMs - leftLowPowerAtMs_ >= kLowPowerExitGraceMs)) {
            leftLowPowerAtMs_ = 0;
        }
        out.snapshot = BuildSnapshotLocked();
        handler = onChanged_;
    }
    if (out.changed && handler) {
        handler(out);
    }
    return out;
}

} // namespace OHOS::Multimedia::CameraAgentService
