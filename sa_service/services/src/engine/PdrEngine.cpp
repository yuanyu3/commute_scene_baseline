/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
* Description: SA-side PDR engine bridge
*/

#include "PdrEngine.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <inttypes.h>
#include <mutex>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

#include "camera_agent_log.h"
#include "PowerModeController.h"
#include "ProactiveAgentBusinessModule.h"
#include "pdr/aipdr/gnss_log.h"
#include "pdr/other/higeo_interface_fused_manager.h"
#include "pdr/other/m_riemammPdr_interface.h"
#include "pdr/riemannPdr/riemann_pdr_pdr_result.h"
#include "pdr/riemannPdr/riemann_pdr_pdr_step_rotation_provider_c_api.h"

__attribute__((visibility("default"))) RiemannPdrHandle *g_pdrProvider = nullptr;

namespace OHOS::Multimedia::CameraAgentService::PdrEngine {
namespace {

enum class SensorType : uint8_t {
    ACC = 0,
    MAG = 1,
    GYRO = 2,
    RV = 3,
};

struct SensorInputPoint {
    SensorType type = SensorType::ACC;
    int64_t timestampMs = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
};

struct SensorDiagPoint {
    std::atomic<int64_t> received {0};
    std::atomic<int64_t> processed {0};
    std::atomic<int64_t> dropped {0};
    std::atomic<int64_t> lastTimestampMs {0};
};

struct PdrGpsPoint {
    double eastM = 0.0;
    double northM = 0.0;
    double latDeg = 0.0;
    double lonDeg = 0.0;
    int64_t timestampMs = 0;
    uint32_t status = 0;
};

struct PdrPoint {
    double x = 0.0;
    double y = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    int64_t timestamp = 0;
    int32_t errorCode = 0;
    double px = 0.0;
    double py = 0.0;
    double pz = 0.0;
    double azimuth = 0.0;
    double accuracy = 0.0;
    double deltaT = 0.0;
    double rotationTheta = 0.0;
    int32_t motionStatus = 0;
    int32_t directionInitialized = 0;
};

constexpr size_t MAX_SENSOR_QUEUE_SIZE = 65536;
constexpr size_t MAX_PDR_QUEUE_SIZE = 16384;
constexpr int64_t MAX_PDR_KEEP_MS = 10 * 60 * 1000;
constexpr uint32_t MAX_PDR_DRAIN_BATCH = 2048;
constexpr size_t MAX_SAVED_PDR_SEGMENTS = 16;
constexpr size_t MAX_SAVED_POINTS_PER_SEGMENT = 8000;
/** Full segment copy for SA save; not drained by HAP (gPdrQueue is). */
constexpr size_t MAX_WALKING_SEGMENT_MIRROR = 32000;

struct SavedPdrSegment {
    int64_t segmentId = 0;
    int64_t savedAtMs = 0;
    std::vector<PdrPoint> points;
    std::string csvFileName;
    std::string gpsCsvFileName;
};

std::mutex gSavedSegmentsMutex;
std::deque<SavedPdrSegment> gSavedSegments;

std::mutex gTrajectoryDumpDirMutex;
std::string gPdrTrajectoryDumpDir;

std::mutex gInitMutex;
std::mutex gPdrLibMutex;
std::atomic<bool> gPdrReady {false};
std::atomic<bool> gPdrActive {false};
std::atomic<int64_t> gPdrCallbackCount {0};

std::mutex gSensorQueueMutex;
std::condition_variable gSensorQueueCv;
std::deque<SensorInputPoint> gSensorQueue;
std::thread gSensorWorker;
std::atomic<bool> gSensorWorkerRunning {false};
std::atomic<int64_t> gSensorQueuePeak {0};
std::atomic<int64_t> gSensorDropTotal {0};
std::array<SensorDiagPoint, 4> gSensorDiag;

std::atomic<int64_t> gGpsReceived {0};
std::atomic<int64_t> gGpsProcessed {0};
std::atomic<int64_t> gAbsPositionCallbackCount {0};
std::mutex gAbsPositionMutex;
RiemannAbsPosition gLatestAbsPosition {};
bool gHaveAbsPosition = false;

std::mutex gPdrQueueMutex;
std::deque<PdrPoint> gPdrQueue;
/** All PDR points while PDR is active; survives DrainPdrResults popping gPdrQueue. */
std::deque<PdrPoint> gWalkingSegmentMirror;
/** PDR+GPS fused absolute positions while PDR is active. */
std::deque<PdrGpsPoint> gWalkingGpsSegmentMirror;
std::atomic<int64_t> gPdrQueuePeak {0};

void SilentHigeoLogCallback(
    uint8 /*ucType*/, const int8* /*tag*/, const int8* /*level*/,
    const int8* /*fileNum*/, int32 /*lineNum*/, const int8* /*logInfo*/, ...)
{}

size_t SensorIndex(SensorType type)
{
    return static_cast<size_t>(type);
}

void UpdateAtomicMax(std::atomic<int64_t> &target, int64_t candidate)
{
    int64_t current = target.load();
    while (candidate > current) {
        if (target.compare_exchange_weak(current, candidate)) {
            break;
        }
    }
}

void OnPdrResultRealtime(const void *info)
{
    if (info == nullptr) {
        return;
    }
    const auto *res = static_cast<const AIPDR::XDR::PdrResult *>(info);
    PdrPoint point;
    point.x = res->GetCx();
    point.y = res->GetCy();
    point.vx = res->GetVx();
    point.vy = res->GetVy();
    point.timestamp = static_cast<int64_t>(res->GetTimestamp());
    point.errorCode = res->GetErrorCode();
    point.px = res->GetPX();
    point.py = res->GetPY();
    point.pz = res->GetPZ();
    point.azimuth = res->GetAzimuth();
    point.accuracy = res->GetAccuracy();
    point.deltaT = res->GetDeltaT();
    point.rotationTheta = res->GetRotationTheta();
    point.motionStatus = static_cast<int32_t>(res->GetMotionStatus());
    point.directionInitialized = res->IsDirectionInitialized() ? 1 : 0;

    {
        RawPdrPoint raw;
        raw.observed_at = point.timestamp;
        raw.x = point.x;
        raw.y = point.y;
        raw.px = point.px;
        raw.py = point.py;
        raw.pz = point.pz;
        raw.vx = point.vx;
        raw.vy = point.vy;
        raw.azimuth = point.azimuth;
        raw.accuracy = point.accuracy;
        raw.errorCode = point.errorCode;
        raw.motionStatus = point.motionStatus;
        raw.directionInitialized = point.directionInitialized;
        ProactiveAgentBusinessModule::GetInstance().OnPdrPoint(raw);
    }

    const int64_t callbackCount = gPdrCallbackCount.fetch_add(1) + 1;
    if (callbackCount <= 10 || (callbackCount % 100) == 0) {
        CAMERA_AGENT_LOG_INFO(
            "pdr result n=%{public}" PRId64 " ts=%{public}" PRId64 " x=%{public}.4f y=%{public}.4f err=%{public}d",
            callbackCount, point.timestamp, point.x, point.y, point.errorCode);
    }

    std::lock_guard<std::mutex> lock(gPdrQueueMutex);
    gPdrQueue.push_back(point);
    if (gPdrActive.load()) {
        gWalkingSegmentMirror.push_back(point);
        while (gWalkingSegmentMirror.size() > MAX_WALKING_SEGMENT_MIRROR) {
            gWalkingSegmentMirror.pop_front();
        }
    }
    if (point.timestamp > 0) {
        const int64_t keepAfterTs = point.timestamp - MAX_PDR_KEEP_MS;
        while (!gPdrQueue.empty() && gPdrQueue.front().timestamp > 0 &&
            gPdrQueue.front().timestamp < keepAfterTs) {
            gPdrQueue.pop_front();
        }
    }
    while (gPdrQueue.size() > MAX_PDR_QUEUE_SIZE) {
        gPdrQueue.pop_front();
    }
    UpdateAtomicMax(gPdrQueuePeak, static_cast<int64_t>(gPdrQueue.size()));
}

PdrGpsPoint ToPdrGpsPoint(const RiemannAbsPosition &pos)
{
    PdrGpsPoint point;
    point.eastM = pos.east_m;
    point.northM = pos.north_m;
    point.latDeg = pos.lat_deg;
    point.lonDeg = pos.lon_deg;
    point.timestampMs = pos.timestamp_ms;
    point.status = pos.status;
    return point;
}

void OnAbsPositionRealtime(const void *info)
{
    if (info == nullptr) {
        return;
    }
    const auto *pos = static_cast<const RiemannAbsPosition *>(info);
    const PdrGpsPoint gpsPoint = ToPdrGpsPoint(*pos);
    {
        std::lock_guard<std::mutex> lock(gAbsPositionMutex);
        gLatestAbsPosition = *pos;
        gHaveAbsPosition = true;
    }
    if (gPdrActive.load()) {
        std::lock_guard<std::mutex> lock(gPdrQueueMutex);
        gWalkingGpsSegmentMirror.push_back(gpsPoint);
        while (gWalkingGpsSegmentMirror.size() > MAX_WALKING_SEGMENT_MIRROR) {
            gWalkingGpsSegmentMirror.pop_front();
        }
    }
    const int64_t n = gAbsPositionCallbackCount.fetch_add(1) + 1;
    if (n <= 5 || (n % 50) == 0) {
        CAMERA_AGENT_LOG_INFO(
            "pdr abs pos n=%{public}" PRId64 " ts=%{public}ld lat=%{public}.8f lon=%{public}.8f status=%{public}u",
            n, pos->timestamp_ms, pos->lat_deg, pos->lon_deg, pos->status);
    }
}

bool EnsurePdrReady()
{
    if (gPdrReady.load()) {
        return true;
    }
    std::lock_guard<std::mutex> lock(gInitMutex);
    if (gPdrReady.load()) {
        return true;
    }

    g_p_log_cb = SilentHigeoLogCallback;
    gnss_set_log_path("");
    AIPDR_InitLoadRiemann(true);
    g_pdrProvider = RiemannPdrGetProvider();
    if (g_pdrProvider == nullptr) {
        CAMERA_AGENT_LOG_ERROR("RiemannPdrGetProvider returned null");
        return false;
    }
    RiemannPdrSetOnPdrResultCallback(g_pdrProvider, OnPdrResultRealtime);
    RiemannPdrSetOnAbsPositionCallback(g_pdrProvider, OnAbsPositionRealtime);
    gPdrReady.store(true);
    CAMERA_AGENT_LOG_INFO("PDR engine initialized");
    return true;
}

void SensorWorkerLoop()
{
    CAMERA_AGENT_LOG_INFO("PDR sensor worker started");
    for (;;) {
        SensorInputPoint point;
        {
            std::unique_lock<std::mutex> lock(gSensorQueueMutex);
            gSensorQueueCv.wait(lock, [] {
                return !gSensorWorkerRunning.load() || !gSensorQueue.empty();
            });
            if (!gSensorWorkerRunning.load() && gSensorQueue.empty()) {
                break;
            }
            point = gSensorQueue.front();
            gSensorQueue.pop_front();
        }

        if (!EnsurePdrReady()) {
            continue;
        }
        const size_t diagIndex = SensorIndex(point.type);
        {
            std::lock_guard<std::mutex> libLock(gPdrLibMutex);
            switch (point.type) {
                case SensorType::ACC:
                    RiemannPdrUpdateAcc(g_pdrProvider, point.x, point.y, point.z, point.timestampMs);
                    break;
                case SensorType::MAG:
                    RiemannPdrUpdateMag(g_pdrProvider, point.x, point.y, point.z, point.timestampMs);
                    break;
                case SensorType::GYRO:
                    RiemannPdrUpdateGyro(g_pdrProvider, point.x, point.y, point.z, point.timestampMs);
                    break;
                case SensorType::RV: {
                    double rv[IDX_FOUR] = {point.w, point.x, point.y, point.z};
                    RiemannPdrUpdateRv(g_pdrProvider, rv, point.timestampMs);
                    break;
                }
                default:
                    break;
            }
        }
        gSensorDiag[diagIndex].processed.fetch_add(1);
    }
    CAMERA_AGENT_LOG_INFO("PDR sensor worker stopped");
}

void StartWorkerIfNeeded()
{
    bool expected = false;
    if (!gSensorWorkerRunning.compare_exchange_strong(expected, true)) {
        return;
    }
    gSensorWorker = std::thread(SensorWorkerLoop);
}

void EnqueueSensorInput(const SensorInputPoint &point)
{
    if (!gPdrActive.load()) {
        return;
    }
    StartWorkerIfNeeded();
    const size_t diagIndex = SensorIndex(point.type);
    gSensorDiag[diagIndex].received.fetch_add(1);
    gSensorDiag[diagIndex].lastTimestampMs.store(point.timestampMs);
    {
        std::lock_guard<std::mutex> lock(gSensorQueueMutex);
        if (gSensorQueue.size() >= MAX_SENSOR_QUEUE_SIZE) {
            const SensorInputPoint dropped = gSensorQueue.front();
            gSensorQueue.pop_front();
            gSensorDiag[SensorIndex(dropped.type)].dropped.fetch_add(1);
            gSensorDropTotal.fetch_add(1);
        }
        gSensorQueue.push_back(point);
        UpdateAtomicMax(gSensorQueuePeak, static_cast<int64_t>(gSensorQueue.size()));
    }
    gSensorQueueCv.notify_one();
}

void AppendPdrPointJson(std::ostringstream &oss, const PdrPoint &point)
{
    oss << "{\"x\":" << point.x << ",\"y\":" << point.y << ",\"vx\":" << point.vx << ",\"vy\":" <<
        point.vy << ",\"timestamp\":" << point.timestamp << ",\"errorCode\":" << point.errorCode <<
        ",\"px\":" << point.px << ",\"py\":" << point.py << ",\"pz\":" << point.pz <<
        ",\"azimuth\":" << point.azimuth << ",\"accuracy\":" << point.accuracy << ",\"deltaT\":" <<
        point.deltaT << ",\"rotationTheta\":" << point.rotationTheta << ",\"motionStatus\":" <<
        point.motionStatus << ",\"directionInitialized\":" << point.directionInitialized << "}";
}

void AppendSensorDiagJson(std::ostringstream &oss, const SensorDiagPoint &diag)
{
    oss << "{\"received\":" << diag.received.load() << ",\"processed\":" << diag.processed.load() <<
        ",\"dropped\":" << diag.dropped.load() << ",\"lastTimestamp\":" << diag.lastTimestampMs.load() << "}";
}

int64_t WallClockMsNow()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void TryWritePdrTrajectoryCsv(SavedPdrSegment &seg)
{
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(gTrajectoryDumpDirMutex);
        dir = gPdrTrajectoryDumpDir;
    }
    if (seg.points.empty()) {
        return;
    }
    if (dir.empty()) {
        CAMERA_AGENT_LOG_WARN(
            "PDR trajectory CSV skipped (dump dir empty) segment=%{public}" PRId64 " points=%{public}zu",
            seg.segmentId, seg.points.size());
        return;
    }
    seg.csvFileName = "pdr_segment_" + std::to_string(seg.segmentId) + "_" + std::to_string(seg.savedAtMs) + ".csv";
    const std::string fullPath = dir + "/" + seg.csvFileName;
    std::ofstream out(fullPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        CAMERA_AGENT_LOG_ERROR("PDR trajectory CSV open failed %{public}s", fullPath.c_str());
        seg.csvFileName.clear();
        return;
    }
    out << "timestamp,x,y,vx,vy,errorCode,px,py,pz,azimuth,accuracy,deltaT,rotationTheta,motionStatus,"
           "directionInitialized,power_mode\n";
    out << std::fixed << std::setprecision(9);
    const char *powerMode = PowerModeController::ModeToString(
        PowerModeController::GetInstance().GetSnapshot().mode);
    for (const PdrPoint &p : seg.points) {
        out << p.timestamp << "," << p.x << "," << p.y << "," << p.vx << "," << p.vy << "," << p.errorCode << "," <<
            p.px << "," << p.py << "," << p.pz << "," << p.azimuth << "," << p.accuracy << "," << p.deltaT << "," <<
            p.rotationTheta << "," << p.motionStatus << "," << p.directionInitialized << "," << powerMode << "\n";
    }
    out.flush();
    CAMERA_AGENT_LOG_INFO("PDR trajectory CSV saved %{public}s points=%{public}zu", fullPath.c_str(),
        seg.points.size());
}

std::string TryWritePdrGpsTrajectoryCsv(int64_t segmentId, int64_t savedAtMs, const std::vector<PdrGpsPoint> &points)
{
    if (points.empty()) {
        return {};
    }
    std::string dir;
    {
        std::lock_guard<std::mutex> lock(gTrajectoryDumpDirMutex);
        dir = gPdrTrajectoryDumpDir;
    }
    if (dir.empty()) {
        CAMERA_AGENT_LOG_WARN(
            "PDR+GPS trajectory CSV skipped (dump dir empty) segment=%{public}" PRId64 " points=%{public}zu",
            segmentId, points.size());
        return {};
    }
    const std::string fileName =
        "pdr_gps_segment_" + std::to_string(segmentId) + "_" + std::to_string(savedAtMs) + ".csv";
    const std::string fullPath = dir + "/" + fileName;
    std::ofstream out(fullPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        CAMERA_AGENT_LOG_ERROR("PDR+GPS trajectory CSV open failed %{public}s", fullPath.c_str());
        return {};
    }
    out << "timestamp,east,north,lat,lon,status,power_mode\n";
    out << std::fixed << std::setprecision(9);
    const char *powerMode = PowerModeController::ModeToString(
        PowerModeController::GetInstance().GetSnapshot().mode);
    for (const PdrGpsPoint &p : points) {
        out << p.timestampMs << "," << p.eastM << "," << p.northM << "," << p.latDeg << "," << p.lonDeg << "," <<
            p.status << "," << powerMode << "\n";
    }
    out.flush();
    CAMERA_AGENT_LOG_INFO("PDR+GPS trajectory CSV saved %{public}s points=%{public}zu", fullPath.c_str(),
        points.size());
    return fileName;
}

void ArchiveQueueToSavedRing(int64_t segmentId)
{
    if (segmentId <= 0) {
        return;
    }
    std::vector<PdrPoint> copy;
    std::vector<PdrGpsPoint> gpsCopy;
    {
        std::lock_guard<std::mutex> lock(gPdrQueueMutex);
        if (!gWalkingGpsSegmentMirror.empty()) {
            gpsCopy.assign(gWalkingGpsSegmentMirror.begin(), gWalkingGpsSegmentMirror.end());
            gWalkingGpsSegmentMirror.clear();
            if (gpsCopy.size() > MAX_SAVED_POINTS_PER_SEGMENT) {
                const size_t drop = gpsCopy.size() - MAX_SAVED_POINTS_PER_SEGMENT;
                gpsCopy.erase(gpsCopy.begin(), gpsCopy.begin() + static_cast<std::ptrdiff_t>(drop));
            }
        }
        if (!gWalkingSegmentMirror.empty()) {
            copy.assign(gWalkingSegmentMirror.begin(), gWalkingSegmentMirror.end());
            gWalkingSegmentMirror.clear();
            if (copy.size() > MAX_SAVED_POINTS_PER_SEGMENT) {
                const size_t drop = copy.size() - MAX_SAVED_POINTS_PER_SEGMENT;
                copy.erase(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(drop));
            }
        } else if (!gPdrQueue.empty()) {
            const size_t n = gPdrQueue.size();
            const size_t cap = MAX_SAVED_POINTS_PER_SEGMENT;
            const size_t skip = (n > cap) ? (n - cap) : 0;
            auto it = gPdrQueue.begin();
            std::advance(it, static_cast<typename std::deque<PdrPoint>::difference_type>(skip));
            for (; it != gPdrQueue.end(); ++it) {
                copy.push_back(*it);
            }
        }
        if (copy.empty() && gpsCopy.empty()) {
            return;
        }
    }
    SavedPdrSegment seg;
    seg.segmentId = segmentId;
    seg.savedAtMs = WallClockMsNow();
    seg.points = std::move(copy);
    const size_t pointCount = seg.points.size();
    const size_t gpsPointCount = gpsCopy.size();
    if (!gpsCopy.empty()) {
        seg.gpsCsvFileName = TryWritePdrGpsTrajectoryCsv(segmentId, seg.savedAtMs, gpsCopy);
    }
    if (!seg.points.empty()) {
        TryWritePdrTrajectoryCsv(seg);
    }
    if (!seg.points.empty() || !seg.gpsCsvFileName.empty()) {
        std::lock_guard<std::mutex> lock(gSavedSegmentsMutex);
        gSavedSegments.push_back(std::move(seg));
        while (gSavedSegments.size() > MAX_SAVED_PDR_SEGMENTS) {
            gSavedSegments.pop_front();
        }
    }
    CAMERA_AGENT_LOG_INFO(
        "PDR archived ended segment %{public}" PRId64 " pdrPoints=%{public}zu gpsPoints=%{public}zu",
        segmentId, pointCount, gpsPointCount);
}

void ClearSavedSegmentsImpl()
{
    std::lock_guard<std::mutex> lock(gSavedSegmentsMutex);
    gSavedSegments.clear();
}

size_t GetSavedSegmentCountImpl()
{
    std::lock_guard<std::mutex> lock(gSavedSegmentsMutex);
    return gSavedSegments.size();
}

std::string GetSavedSegmentsMetaJsonImpl()
{
    std::lock_guard<std::mutex> lock(gSavedSegmentsMutex);
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < gSavedSegments.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        const SavedPdrSegment &s = gSavedSegments[i];
        oss << "{\"segmentId\":" << s.segmentId << ",\"pointCount\":" << s.points.size() <<
            ",\"savedAtMs\":" << s.savedAtMs;
        if (!s.csvFileName.empty()) {
            oss << ",\"csvFile\":\"" << s.csvFileName << "\"";
        }
        if (!s.gpsCsvFileName.empty()) {
            oss << ",\"gpsCsvFile\":\"" << s.gpsCsvFileName << "\"";
        }
        oss << "}";
    }
    oss << "]";
    return oss.str();
}

std::string DrainSavedSegmentJsonImpl(int64_t segmentId)
{
    std::vector<PdrPoint> picked;
    {
        std::lock_guard<std::mutex> lock(gSavedSegmentsMutex);
        for (auto it = gSavedSegments.begin(); it != gSavedSegments.end(); ++it) {
            if (it->segmentId == segmentId) {
                picked = std::move(it->points);
                gSavedSegments.erase(it);
                break;
            }
        }
    }
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < picked.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        AppendPdrPointJson(oss, picked[i]);
    }
    oss << "]";
    return oss.str();
}

} // namespace

void ArchiveCurrentQueueBeforeClear(int64_t segmentId)
{
    ArchiveQueueToSavedRing(segmentId);
}

void ClearSavedSegments()
{
    ClearSavedSegmentsImpl();
}

size_t GetSavedSegmentCount()
{
    return GetSavedSegmentCountImpl();
}

std::string GetSavedSegmentsMetaJson()
{
    return GetSavedSegmentsMetaJsonImpl();
}

std::string DrainSavedSegmentJson(int64_t segmentId)
{
    return DrainSavedSegmentJsonImpl(segmentId);
}

void SetTrajectoryDumpDir(const std::string &dir)
{
    std::lock_guard<std::mutex> lock(gTrajectoryDumpDirMutex);
    gPdrTrajectoryDumpDir = dir;
}

void ClearTrajectoryDumpDir()
{
    std::lock_guard<std::mutex> lock(gTrajectoryDumpDirMutex);
    gPdrTrajectoryDumpDir.clear();
}

std::string GetTrajectoryDumpDir()
{
    std::lock_guard<std::mutex> lock(gTrajectoryDumpDirMutex);
    return gPdrTrajectoryDumpDir;
}

void SetActive(bool active)
{
    const bool wasActive = gPdrActive.exchange(active);
    if (wasActive == active) {
        return;
    }
    if (!active) {
        std::lock_guard<std::mutex> lock(gSensorQueueMutex);
        gSensorQueue.clear();
    }
    CAMERA_AGENT_LOG_INFO("PDR active changed to %{public}d", active ? 1 : 0);
}

bool IsActive()
{
    return gPdrActive.load();
}

void PushAcc(int64_t timestampMs, double x, double y, double z)
{
    EnqueueSensorInput({SensorType::ACC, timestampMs, x, y, z, 0.0});
}

void PushGyro(int64_t timestampMs, double x, double y, double z)
{
    EnqueueSensorInput({SensorType::GYRO, timestampMs, x, y, z, 0.0});
}

void PushMag(int64_t timestampMs, double x, double y, double z)
{
    EnqueueSensorInput({SensorType::MAG, timestampMs, x, y, z, 0.0});
}

void PushRv(int64_t timestampMs, double x, double y, double z, double w)
{
    EnqueueSensorInput({SensorType::RV, timestampMs, x, y, z, w});
}

void PushGps(int64_t timestampMs, double latDeg, double lonDeg, float accuracyM, float headingDeg, bool valid)
{
    if (!gPdrActive.load()) {
        return;
    }
    gGpsReceived.fetch_add(1);
    if (!EnsurePdrReady()) {
        return;
    }
    RiemannGpsSample sample {};
    sample.timestamp_ms = timestampMs;
    sample.lat_deg = latDeg;
    sample.lon_deg = lonDeg;
    sample.accuracy_m = accuracyM;
    sample.heading_deg = headingDeg;
    sample.valid = valid ? 1 : 0;
    {
        std::lock_guard<std::mutex> libLock(gPdrLibMutex);
        RiemannPdrUpdateGps(g_pdrProvider, &sample);
    }
    gGpsProcessed.fetch_add(1);
}

void ClearResults()
{
    {
        std::lock_guard<std::mutex> lock(gPdrQueueMutex);
        gPdrQueue.clear();
        gWalkingSegmentMirror.clear();
        gWalkingGpsSegmentMirror.clear();
    }
    gPdrCallbackCount.store(0);
}

void ResetSession()
{
    {
        std::lock_guard<std::mutex> lock(gAbsPositionMutex);
        gHaveAbsPosition = false;
        gLatestAbsPosition = {};
    }
    gAbsPositionCallbackCount.store(0);
    if (!EnsurePdrReady() || g_pdrProvider == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> libLock(gPdrLibMutex);
        RiemannPdrResetSession(g_pdrProvider);
    }
    CAMERA_AGENT_LOG_INFO("PDR session reset (trajectory + GPS fusion)");
}

void Stop()
{
    gPdrActive.store(false);
    gSensorWorkerRunning.store(false);
    gSensorQueueCv.notify_all();
    if (gSensorWorker.joinable()) {
        gSensorWorker.join();
    }
    {
        std::lock_guard<std::mutex> lock(gSensorQueueMutex);
        gSensorQueue.clear();
    }
}

std::string DrainResultsJson()
{
    std::deque<PdrPoint> local;
    {
        std::lock_guard<std::mutex> lock(gPdrQueueMutex);
        uint32_t count = static_cast<uint32_t>(gPdrQueue.size());
        if (count > MAX_PDR_DRAIN_BATCH) {
            count = MAX_PDR_DRAIN_BATCH;
        }
        for (uint32_t i = 0; i < count; ++i) {
            local.push_back(gPdrQueue.front());
            gPdrQueue.pop_front();
        }
    }

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < local.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        AppendPdrPointJson(oss, local[i]);
    }
    oss << "]";
    return oss.str();
}

std::string GetDiagJson()
{
    int64_t sensorQueueSize = 0;
    int64_t pdrQueueSize = 0;
    int64_t mirrorSize = 0;
    int64_t gpsMirrorSize = 0;
    {
        std::lock_guard<std::mutex> lock(gSensorQueueMutex);
        sensorQueueSize = static_cast<int64_t>(gSensorQueue.size());
    }
    {
        std::lock_guard<std::mutex> lock(gPdrQueueMutex);
        pdrQueueSize = static_cast<int64_t>(gPdrQueue.size());
        mirrorSize = static_cast<int64_t>(gWalkingSegmentMirror.size());
        gpsMirrorSize = static_cast<int64_t>(gWalkingGpsSegmentMirror.size());
    }

    std::ostringstream oss;
    oss << "{\"ready\":" << (gPdrReady.load() ? "true" : "false") << ",\"sensorQueueSize\":" <<
        sensorQueueSize << ",\"active\":" << (gPdrActive.load() ? "true" : "false") <<
        ",\"sensorQueuePeak\":" << gSensorQueuePeak.load() << ",\"sensorDropTotal\":" <<
        gSensorDropTotal.load() << ",\"pdrQueueSize\":" << pdrQueueSize << ",\"pdrWalkingMirrorSize\":" <<
        mirrorSize << ",\"pdrGpsWalkingMirrorSize\":" << gpsMirrorSize << ",\"pdrQueuePeak\":" <<
        gPdrQueuePeak.load() << ",\"pdrCallbackCount\":" << gPdrCallbackCount.load() <<
        ",\"drainBatchLimit\":" << MAX_PDR_DRAIN_BATCH << ",\"savedPdrSegmentCount\":" <<
        GetSavedSegmentCountImpl() << ",\"sensors\":{\"acc\":";
    AppendSensorDiagJson(oss, gSensorDiag[SensorIndex(SensorType::ACC)]);
    oss << ",\"mag\":";
    AppendSensorDiagJson(oss, gSensorDiag[SensorIndex(SensorType::MAG)]);
    oss << ",\"gyro\":";
    AppendSensorDiagJson(oss, gSensorDiag[SensorIndex(SensorType::GYRO)]);
    oss << ",\"rv\":";
    AppendSensorDiagJson(oss, gSensorDiag[SensorIndex(SensorType::RV)]);
    oss << "},\"gps\":{\"received\":" << gGpsReceived.load() << ",\"processed\":" << gGpsProcessed.load() <<
        ",\"absPositionCallbacks\":" << gAbsPositionCallbackCount.load();
    bool haveAbs = false;
    RiemannAbsPosition absCopy {};
    {
        std::lock_guard<std::mutex> lock(gAbsPositionMutex);
        haveAbs = gHaveAbsPosition;
        if (haveAbs) {
            absCopy = gLatestAbsPosition;
        }
    }
    if (haveAbs) {
        oss << ",\"latestAbs\":{\"east\":" << absCopy.east_m << ",\"north\":" << absCopy.north_m <<
            ",\"lat\":" << absCopy.lat_deg << ",\"lon\":" << absCopy.lon_deg << ",\"timestamp\":" <<
            absCopy.timestamp_ms << ",\"status\":" << absCopy.status << "}";
    }
    oss << "}}";
    return oss.str();
}

} // namespace OHOS::Multimedia::CameraAgentService::PdrEngine
