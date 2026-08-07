/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SA-side detector for walking-to-stationary transition.
 */

#include "StopWalking.h"

#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>

namespace OHOS::Multimedia::CameraAgentService::StopWalking {
namespace {

struct AccMagnitudeSample {
    int64_t timestampMs = 0;
    double magnitude = 0.0;
};

constexpr int64_t STILL_WINDOW_MS = 2000;
constexpr int64_t STILL_HOLD_MS = 10000;
constexpr size_t MIN_STILL_SAMPLES = 80;
constexpr double STILL_STD_THRESHOLD = 0.16;

std::mutex gMutex;
std::deque<AccMagnitudeSample> gAccMagnitudes;
int64_t gStillCandidateStartMs = 0;
bool gStoppedWalking = false;

} // namespace

void PushAcc(int64_t timestampMs, double x, double y, double z)
{
    const double magnitude = std::sqrt(x * x + y * y + z * z);
    std::lock_guard<std::mutex> lock(gMutex);
    gAccMagnitudes.push_back({timestampMs, magnitude});
    while (!gAccMagnitudes.empty() && (timestampMs - gAccMagnitudes.front().timestampMs) > STILL_WINDOW_MS) {
        gAccMagnitudes.pop_front();
    }
    if (gAccMagnitudes.size() < MIN_STILL_SAMPLES) {
        gStillCandidateStartMs = 0;
        gStoppedWalking = false;
        return;
    }

    double sum = 0.0;
    for (const auto &sample : gAccMagnitudes) {
        sum += sample.magnitude;
    }
    const double mean = sum / static_cast<double>(gAccMagnitudes.size());
    double variance = 0.0;
    for (const auto &sample : gAccMagnitudes) {
        const double diff = sample.magnitude - mean;
        variance += diff * diff;
    }
    const double stddev = std::sqrt(variance / static_cast<double>(gAccMagnitudes.size()));
    if (stddev < STILL_STD_THRESHOLD) {
        if (gStillCandidateStartMs == 0) {
            gStillCandidateStartMs = timestampMs;
        }
        if ((timestampMs - gStillCandidateStartMs) >= STILL_HOLD_MS) {
            gStoppedWalking = true;
        }
    } else {
        gStillCandidateStartMs = 0;
        gStoppedWalking = false;
    }
}

bool HasStoppedWalking()
{
    std::lock_guard<std::mutex> lock(gMutex);
    return gStoppedWalking;
}

void Reset()
{
    std::lock_guard<std::mutex> lock(gMutex);
    gAccMagnitudes.clear();
    gStillCandidateStartMs = 0;
    gStoppedWalking = false;
}

} // namespace OHOS::Multimedia::CameraAgentService::StopWalking
