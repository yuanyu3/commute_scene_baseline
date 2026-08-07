/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SA bridge to leave-car detector; only leave_car_detector_c_api.h is used.
 */

#include "LeaveCarEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <inttypes.h>
#include <mutex>
#include <sstream>
#include <thread>

#include "camera_agent_log.h"
#include "leave_car_detector_c_api.h"
#include "PdrEngine.h"
#include "PowerModeController.h"
#include "ProactiveAgentBusinessModule.h"
#include "StopWalking.h"

namespace OHOS::Multimedia::CameraAgentService::LeaveCarEngine {
namespace {

enum class SampleKind : uint8_t { ACC = 0, GYRO = 1, MAG = 2 };

struct SamplePoint {
    SampleKind kind = SampleKind::ACC;
    int64_t timestampMs = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

constexpr size_t MAX_QUEUE = 65536;
constexpr int64_t FEED_PERIOD_MS = 10;

struct LatestSample {
    bool valid = false;
    int64_t timestampMs = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

std::mutex gInitMutex;
std::mutex gHandleMutex;
std::atomic<bool> gEngineRunning {false};

LeaveCarHandle *gHandle = nullptr;

std::mutex gQueueMutex;
std::condition_variable gQueueCv;
std::deque<SamplePoint> gQueue;
std::thread gWorker;
std::atomic<bool> gWorkerRunning {false};
LatestSample gLatestAcc;
LatestSample gLatestGyro;
LatestSample gLatestMag;
int64_t gLastFeedTimestampMs = 0;
int64_t gBaseTimestampMs = 0;

std::mutex gResultMutex;
LeaveCarDetectorResult gLatest {};
std::atomic<bool> gHaveResult {false};
std::atomic<int64_t> gCallbackCount {0};
std::atomic<int64_t> gFeedBatchCount {0};
std::atomic<bool> gOutputWalking {false};
std::atomic<bool> gPendingDetectorReset {false};
std::atomic<int64_t> gPdrSegmentId {0};

void OnLeaveCarResult(const void *payload)
{
    if (payload == nullptr) {
        return;
    }
    const auto *r = static_cast<const LeaveCarDetectorResult *>(payload);
    {
        std::lock_guard<std::mutex> lock(gResultMutex);
        gLatest = *r;
    }
    gHaveResult.store(true);
    if (r->is_walking != 0 && !gOutputWalking.exchange(true)) {
        const int64_t newSegmentId = gPdrSegmentId.fetch_add(1) + 1;
        PdrEngine::SetActive(false);
        PdrEngine::ClearResults();
        PdrEngine::SetActive(true);
        StopWalking::Reset();
        gPendingDetectorReset.store(true);
        CAMERA_AGENT_LOG_INFO("LeaveCarEngine output state changed to walking, pdrSegmentId=%{public}" PRId64
            ", request detector reset", newSegmentId);
        // Use wall-clock ms so WalkingStarted aligns with Stop/PDR/GPS / tick windows.
        // LeaveCar start_walk_timestamp / current_timestamp are session-relative and must not
        // drive observation windows or ISO-8601 facts.
        const int64_t walkTs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        CAMERA_AGENT_LOG_DEBUG(
            "OnWalkingStarted wallTs=%{public}" PRId64 " leaveCarRelStart=%{public}ld leaveCarRelNow=%{public}ld",
            walkTs, static_cast<long>(r->start_walk_timestamp), static_cast<long>(r->current_timestamp));
        ProactiveAgentBusinessModule::GetInstance().OnWalkingStarted(walkTs);
        PowerModeController::GetInstance().NotifyWalking(true, walkTs);
    }
    const int64_t n = gCallbackCount.fetch_add(1) + 1;
    if (n <= 5 || (n % 200) == 0) {
        CAMERA_AGENT_LOG_INFO(
            "leave_car cb n=%{public}" PRId64 " state=%{public}d walk=%{public}d leave=%{public}d ts=%{public}ld",
            n, r->state, r->is_walking, r->is_leave_car, r->current_timestamp);
    }
}

bool EnsureHandle()
{
    std::lock_guard<std::mutex> lock(gHandleMutex);
    if (gHandle != nullptr) {
        return true;
    }
    gHandle = LeaveCarDetector_Create(OnLeaveCarResult);
    if (gHandle == nullptr) {
        CAMERA_AGENT_LOG_ERROR("LeaveCarDetector_Create returned null");
        return false;
    }
    CAMERA_AGENT_LOG_INFO("LeaveCarDetector created");
    return true;
}

void WorkerLoop()
{
    CAMERA_AGENT_LOG_INFO("LeaveCarEngine worker started");
    for (;;) {
        SamplePoint p;
        {
            std::unique_lock<std::mutex> lock(gQueueMutex);
            gQueueCv.wait(lock, [] {
                return !gWorkerRunning.load() || !gQueue.empty();
            });
            if (!gWorkerRunning.load() && gQueue.empty()) {
                break;
            }
            p = gQueue.front();
            gQueue.pop_front();
        }
        if (!EnsureHandle()) {
            continue;
        }
        if (gPendingDetectorReset.exchange(false) && gHandle != nullptr) {
            LeaveCarDetector_Reset(gHandle);
            CAMERA_AGENT_LOG_INFO("LeaveCarDetector reset after walking transition");
        }
        const long ts = static_cast<long>(p.timestampMs);
        switch (p.kind) {
            case SampleKind::ACC:
                LeaveCarDetector_UpdateAcc(gHandle, p.x, p.y, p.z, ts);
                break;
            case SampleKind::GYRO:
                LeaveCarDetector_UpdateGyro(gHandle, p.x, p.y, p.z, ts);
                break;
            case SampleKind::MAG:
                LeaveCarDetector_UpdateMag(gHandle, p.x, p.y, p.z, ts);
                break;
            default:
                break;
        }
    }
    CAMERA_AGENT_LOG_INFO("LeaveCarEngine worker stopped");
}

void StartWorkerIfNeeded()
{
    bool expected = false;
    if (!gWorkerRunning.compare_exchange_strong(expected, true)) {
        return;
    }
    gWorker = std::thread(WorkerLoop);
}

void PushQueueNoLock(const SamplePoint &p)
{
    if (gQueue.size() >= MAX_QUEUE) {
        gQueue.pop_front();
    }
    gQueue.push_back(p);
}

void UpdateLatestNoLock(const SamplePoint &p)
{
    LatestSample *target = nullptr;
    switch (p.kind) {
        case SampleKind::ACC:
            target = &gLatestAcc;
            break;
        case SampleKind::GYRO:
            target = &gLatestGyro;
            break;
        case SampleKind::MAG:
            target = &gLatestMag;
            break;
        default:
            break;
    }
    if (target == nullptr) {
        return;
    }
    target->valid = true;
    target->timestampMs = p.timestampMs;
    target->x = p.x;
    target->y = p.y;
    target->z = p.z;
}

void EnqueueIfReadyNoLock()
{
    if (!gLatestAcc.valid || !gLatestGyro.valid || !gLatestMag.valid) {
        return;
    }
    const int64_t newestTimestamp = std::max(gLatestAcc.timestampMs,
        std::max(gLatestGyro.timestampMs, gLatestMag.timestampMs));
    if (gLastFeedTimestampMs != 0 && (newestTimestamp - gLastFeedTimestampMs) < FEED_PERIOD_MS) {
        return;
    }
    const int64_t feedTimestamp = (gLastFeedTimestampMs == 0) ? newestTimestamp :
        std::max(newestTimestamp, gLastFeedTimestampMs + FEED_PERIOD_MS);
    gLastFeedTimestampMs = feedTimestamp;
    if (gBaseTimestampMs == 0) {
        gBaseTimestampMs = feedTimestamp;
    }
    const int64_t relativeTimestamp = feedTimestamp - gBaseTimestampMs;
    PushQueueNoLock({SampleKind::GYRO, relativeTimestamp, gLatestGyro.x, gLatestGyro.y, gLatestGyro.z});
    PushQueueNoLock({SampleKind::MAG, relativeTimestamp, gLatestMag.x, gLatestMag.y, gLatestMag.z});
    PushQueueNoLock({SampleKind::ACC, relativeTimestamp, gLatestAcc.x, gLatestAcc.y, gLatestAcc.z});
    const int64_t count = gFeedBatchCount.fetch_add(1) + 1;
    if (count <= 5 || (count % 200) == 0) {
        CAMERA_AGENT_LOG_INFO("LeaveCarEngine feed batch n=%{public}" PRId64
            " rawTs=%{public}" PRId64 " relTs=%{public}" PRId64, count, feedTimestamp, relativeTimestamp);
    }
}

void Enqueue(const SamplePoint &p)
{
    if (!gEngineRunning.load()) {
        return;
    }
    if (p.kind == SampleKind::ACC) {
        StopWalking::PushAcc(p.timestampMs, p.x, p.y, p.z);
        if (gOutputWalking.load() && StopWalking::HasStoppedWalking()) {
            const int64_t segId = gPdrSegmentId.load();
            if (segId > 0) {
                PdrEngine::ArchiveCurrentQueueBeforeClear(segId);
            }
            gOutputWalking.store(false);
            PdrEngine::SetActive(false);
            PdrEngine::ResetSession();
            CAMERA_AGENT_LOG_INFO("LeaveCarEngine output state changed to non_walking by StopWalking");
            // ACC wall-clock ms; edge is gated by gOutputWalking (gStoppedWalking is sustained bool).
            ProactiveAgentBusinessModule::GetInstance().OnWalkingStopped(p.timestampMs);
            PowerModeController::GetInstance().NotifyWalking(false, p.timestampMs);
        }
    }
    StartWorkerIfNeeded();
    {
        std::lock_guard<std::mutex> lock(gQueueMutex);
        UpdateLatestNoLock(p);
        EnqueueIfReadyNoLock();
    }
    gQueueCv.notify_one();
}

} // namespace

void PushAcc(int64_t timestampMs, double x, double y, double z)
{
    Enqueue({SampleKind::ACC, timestampMs, x, y, z});
}

void PushGyro(int64_t timestampMs, double x, double y, double z)
{
    Enqueue({SampleKind::GYRO, timestampMs, x, y, z});
}

void PushMag(int64_t timestampMs, double x, double y, double z)
{
    Enqueue({SampleKind::MAG, timestampMs, x, y, z});
}

void Start()
{
    std::lock_guard<std::mutex> lock(gInitMutex);
    gEngineRunning.store(true);
    StartWorkerIfNeeded();
}

void Stop()
{
    gEngineRunning.store(false);
    gWorkerRunning.store(false);
    gQueueCv.notify_all();
    if (gWorker.joinable()) {
        gWorker.join();
    }
    {
        std::lock_guard<std::mutex> lock(gQueueMutex);
        gQueue.clear();
        gLatestAcc = {};
        gLatestGyro = {};
        gLatestMag = {};
        gLastFeedTimestampMs = 0;
        gBaseTimestampMs = 0;
    }
    std::lock_guard<std::mutex> hlock(gHandleMutex);
    if (gHandle != nullptr) {
        LeaveCarDetector_Destroy(gHandle);
        gHandle = nullptr;
        CAMERA_AGENT_LOG_INFO("LeaveCarDetector destroyed");
    }
    gHaveResult.store(false);
    gFeedBatchCount.store(0);
    gPendingDetectorReset.store(false);
    const int64_t segId = gPdrSegmentId.load();
    if (segId > 0) {
        PdrEngine::ArchiveCurrentQueueBeforeClear(segId);
    }
    gOutputWalking.store(false);
    PdrEngine::SetActive(false);
    PdrEngine::ResetSession();
    StopWalking::Reset();
}

static std::string EscapeJsonStrForState(const std::string &s)

{
    std::string o;
    for (const char c : s) {
        if (c == '\\' || c == '"') {
            o += '\\';
        }
        o += c;
    }
    return o;
}

std::string GetStateJson()
{
    LeaveCarDetectorResult copy {};
    bool haveLeaveResult = false;
    {
        std::lock_guard<std::mutex> lock(gResultMutex);
        if (gHaveResult.load()) {
            copy = gLatest;
            haveLeaveResult = true;
        }
    }
    const bool walkingReady = haveLeaveResult;
    const bool walkingNow = gOutputWalking.load();
    std::ostringstream oss;
    oss << "{\"ready\":" << (walkingReady ? "true" : "false");
    const char *mode = walkingNow ? "walking" : "non_walking";
    oss << ",\"userMode\":\"" << mode << "\"";
    oss << ",\"state\":" << (walkingNow ? LEAVE_CAR_STATE_WALKING : LEAVE_CAR_STATE_UNKNOWN);
    oss << ",\"isLeaveCar\":" << copy.is_leave_car;
    oss << ",\"isWalking\":" << (walkingNow ? 1 : 0);
    oss << ",\"leaveCarReady\":" << (haveLeaveResult ? "true" : "false");
    oss << ",\"leaveCarState\":" << copy.state;
    oss << ",\"leaveCarIsWalking\":" << copy.is_walking;
    oss << ",\"lastInferResult\":" << copy.last_infer_result;
    oss << ",\"currentTimestamp\":" << copy.current_timestamp;
    oss << ",\"leaveCarTimestamp\":" << copy.leave_car_timestamp;
    oss << ",\"startWalkTimestamp\":" << copy.start_walk_timestamp;
    oss << ",\"feedBatchCount\":" << gFeedBatchCount.load();
    oss << ",\"pdrActive\":" << (PdrEngine::IsActive() ? "true" : "false");
    oss << ",\"pdrSegmentId\":" << gPdrSegmentId.load();
    oss << ",\"pdrSavedSegmentCount\":" << PdrEngine::GetSavedSegmentCount();
    oss << ",\"pdrSavedSegments\":" << PdrEngine::GetSavedSegmentsMetaJson();
    oss << ",\"pdrTrajectoryDumpDir\":\"" << EscapeJsonStrForState(PdrEngine::GetTrajectoryDumpDir()) << "\"";
    oss << ",\"walkingTransitionCount\":" << gPdrSegmentId.load();
    oss << "}";
    return oss.str();
}

} // namespace OHOS::Multimedia::CameraAgentService::LeaveCarEngine
