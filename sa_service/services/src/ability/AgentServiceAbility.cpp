/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
* Description: to be complete
* Author: to be complete
* Create: 2024/06/05
*/


#include "AgentServiceAbility.h"
#include "BleProvider.h"
#include "CellProvider.h"
#include "SignalTypes.h"
#include "WifiProvider.h"
#include "GeoEngine.h"
#include "LeaveCarEngine.h"
#include "PdrEngine.h"
#include "ProactiveAgentBusinessModule.h"
#include "PowerModeController.h"
#include "commute_sa/baseline_runtime.h"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <inttypes.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <ctime>
#include <unistd.h>
#include <sys/stat.h>
#include "iremote_broker.h"
#include "system_ability.h"
#include <if_system_ability_manager.h>
#include "iservice_registry.h"
#include "ipc_skeleton.h"
#include "iremote_stub.h"
#include "sensor_agent.h"
#include "camera_agent_log.h"
#if __has_include("accesstoken_kit.h") && __has_include("tokenid_kit.h")
#include "accesstoken_kit.h"
#include "tokenid_kit.h"
#define CAMERA_AGENT_HAS_ACCESS_TOKEN_API 1
#else
#define CAMERA_AGENT_HAS_ACCESS_TOKEN_API 0
#endif
#if __has_include("locator.h") && __has_include("i_locator_callback.h") && __has_include("location.h") && \
    __has_include("request_config.h") && __has_include("constant_definition.h")
#include "constant_definition.h"
#include "i_locator_callback.h"
#include "location.h"
#include "locator.h"
#include "request_config.h"
#define CAMERA_AGENT_HAS_LOCATION_API 1
#else
#define CAMERA_AGENT_HAS_LOCATION_API 0
#endif

using namespace OHOS::HiviewDFX;

namespace OHOS::Multimedia::CameraAgentService {

REGISTER_SYSTEM_ABILITY_BY_ID(AgentServiceAbility, COMMUTE_AGENT_SERVICE_ID, true);

namespace {
struct Vector3Frame {
    int64_t ts = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct RvFrame {
    int64_t ts = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct LocationFrame {
    int64_t ts = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double accuracy = 0.0;
    int32_t sourceType = 0;
};

struct GeoCoordinate {
    double latitude = 0.0;
    double longitude = 0.0;
};

constexpr size_t SENSOR_CACHE_SIZE = 100;
constexpr int64_t MIN_SAMPLE_INTERVAL_NS = 1'000'000;
constexpr int64_t DEFAULT_SAMPLE_INTERVAL_NS = PowerModeController::kHighSensorIntervalNs;
//步行测试5 ， 长距离测试40
constexpr int32_t LOCATION_REPORT_INTERVAL_SEC = PowerModeController::kHighLocationIntervalSec;
// Configure sensor HiLog frequency here (milliseconds). Set <= 0 to disable throttling.
constexpr int64_t SENSOR_LOG_INTERVAL_MS = 1000;
constexpr const char *ACC_PERMISSION = "ohos.permission.ACCELEROMETER";
constexpr const char *APPROX_LOCATION_PERMISSION = "ohos.permission.APPROXIMATELY_LOCATION";
constexpr const char *LOCATION_PERMISSION = "ohos.permission.LOCATION";
// commute_scene_baseline SA dump root (WGS84 location CSV).
constexpr const char *SENSOR_DUMP_DIR = "/data/service/el1/public/commuteagentservice";
constexpr int64_t DUMP_ROLL_INTERVAL_MS = 10 * 60 * 1000;
constexpr const char *kLocationDumpCrs = "WGS84";

struct SensorDumpRecord {
    char sensor[8] = {};
    int64_t wallTsMs = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
    char powerMode[24] = {};
};

struct TextDumpRecord {
    char sensor[8] = {};
    int64_t wallTsMs = 0;
    std::string line;
    char powerMode[24] = {};
};

std::shared_ptr<jiuwen::Agent> jiuwenAgent;

std::mutex gSensorMutex;
std::deque<Vector3Frame> gAccFrames;
std::deque<Vector3Frame> gGyroFrames;
std::deque<Vector3Frame> gMagFrames;
std::deque<RvFrame> gRvFrames;
std::deque<BaroFrame> gBaroFrames;
WifiFp gLatestWifi;
bool gHasWifi = false;
BleData gLatestBle;
bool gHasBle = false;
std::vector<std::shared_ptr<CellInfo>> gLatestCells;
bool gHasCell = false;
LocationFrame gLocationFrame;
bool gHasLocationFrame = false;
SensorUser gAccUser {};
SensorUser gGyroUser {};
SensorUser gMagUser {};
SensorUser gRvUser {};
SensorUser gBaroUser {};
bool gAccStarted = false;
bool gGyroStarted = false;
bool gMagStarted = false;
bool gRvStarted = false;
bool gBaroStarted = false;
bool gWifiStarted = false;
bool gBleStarted = false;
bool gCellStarted = false;
bool gLocationStarted = false;
bool gWifiDesired = false;
bool gBleDesired = false;
bool gCellDesired = false;
bool gLocationDesired = false;
PowerMode gAppliedPowerMode = PowerMode::kHighStill;
bool gBleSuspendedForPower = false;
int32_t gLocationIntervalSec = LOCATION_REPORT_INTERVAL_SEC;
int64_t gSensorSampleIntervalNs = DEFAULT_SAMPLE_INTERVAL_NS;
std::atomic<int> gCachedPowerModeInt{static_cast<int>(PowerMode::kHighStill)};

void MaybeHandlePowerDecision(const PowerModeDecision &decision);
void ApplyPowerProfile(const PowerModeSnapshot &snapshot);
void CachePowerMode(PowerMode mode);
const char *CachedPowerModeString();
void ScheduleApplyPowerProfile(const PowerModeSnapshot &snapshot);
int64_t gLastAccLogMs = 0;
int64_t gLastGyroLogMs = 0;
int64_t gLastMagLogMs = 0;
int64_t gLastRvLogMs = 0;
int64_t gLastBaroLogMs = 0;
std::mutex gDumpMutex;
std::condition_variable gDumpCv;
std::deque<SensorDumpRecord> gDumpQueue;
std::deque<TextDumpRecord> gTextDumpQueue;
std::thread gDumpThread;
bool gDumpRunning = false;
std::ofstream gAccDumpFile;
std::ofstream gGyroDumpFile;
std::ofstream gMagDumpFile;
std::ofstream gRvDumpFile;
std::ofstream gBaroDumpFile;
std::ofstream gWifiDumpFile;
std::ofstream gBleDumpFile;
std::ofstream gCellDumpFile;
std::ofstream gLocationDumpFile;
std::string gDumpSessionDir;
int64_t gCurrentDumpBucketStartMs = -1;
#if CAMERA_AGENT_HAS_LOCATION_API
std::shared_ptr<Location::LocatorImpl> gLocator;
sptr<Location::ILocatorCallback> gLocatorCallback;
#endif

int64_t GetTimestampMs();

bool IsInChina(const double latitude, const double longitude)
{
    return longitude >= 72.004 && longitude <= 137.8347 && latitude >= 0.8293 && latitude <= 55.8271;
}

double TransformLatitude(const double x, const double y)
{
    constexpr double pi = 3.14159265358979323846;
    return -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * std::sqrt(std::abs(x)) +
        (20.0 * std::sin(6.0 * x * pi) + 20.0 * std::sin(2.0 * x * pi)) * 2.0 / 3.0 +
        (20.0 * std::sin(y * pi) + 40.0 * std::sin(y / 3.0 * pi)) * 2.0 / 3.0 +
        (160.0 * std::sin(y / 12.0 * pi) + 320.0 * std::sin(y * pi / 30.0)) * 2.0 / 3.0;
}

double TransformLongitude(const double x, const double y)
{
    constexpr double pi = 3.14159265358979323846;
    return 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * std::sqrt(std::abs(x)) +
        (20.0 * std::sin(6.0 * x * pi) + 20.0 * std::sin(2.0 * x * pi)) * 2.0 / 3.0 +
        (20.0 * std::sin(x * pi) + 40.0 * std::sin(x / 3.0 * pi)) * 2.0 / 3.0 +
        (150.0 * std::sin(x / 12.0 * pi) + 300.0 * std::sin(x / 30.0 * pi)) * 2.0 / 3.0;
}

GeoCoordinate Wgs84ToGcj02(const double latitude, const double longitude)
{
    if (!IsInChina(latitude, longitude)) {
        return {latitude, longitude};
    }

    constexpr double pi = 3.14159265358979323846;
    constexpr double earthAxis = 6378245.0;
    constexpr double eccentricity = 0.00669342162296594323;
    double dLat = TransformLatitude(longitude - 105.0, latitude - 35.0);
    double dLon = TransformLongitude(longitude - 105.0, latitude - 35.0);
    const double radLat = latitude / 180.0 * pi;
    double magic = std::sin(radLat);
    magic = 1.0 - eccentricity * magic * magic;
    const double sqrtMagic = std::sqrt(magic);
    dLat = (dLat * 180.0) / ((earthAxis * (1.0 - eccentricity)) / (magic * sqrtMagic) * pi);
    dLon = (dLon * 180.0) / (earthAxis / sqrtMagic * std::cos(radLat) * pi);
    return {latitude + dLat, longitude + dLon};
}

bool EnsureDirectoryExists(const char *path)
{
    if (path == nullptr) {
        return false;
    }
    struct stat st {};
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) != 0;
    }
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) != 0;
    }
    return false;
}

bool EnsureDirectoryExistsRecursive(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    if (path[0] != '/') {
        return EnsureDirectoryExists(path.c_str());
    }
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] == '/' && current.size() > 1) {
            if (!EnsureDirectoryExists(current.c_str())) {
                return false;
            }
        }
    }
    return EnsureDirectoryExists(path.c_str());
}

std::string FormatTimestampForPath(int64_t tsMs)
{
    std::time_t sec = static_cast<std::time_t>(tsMs / 1000);
    std::tm tmValue {};
#if defined(_WIN32)
    localtime_s(&tmValue, &sec);
#else
    localtime_r(&sec, &tmValue);
#endif
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tmValue);
    return std::string(buf);
}

void CloseDumpFilesNoLock()
{
    if (gAccDumpFile.is_open()) {
        gAccDumpFile.close();
    }
    if (gGyroDumpFile.is_open()) {
        gGyroDumpFile.close();
    }
    if (gMagDumpFile.is_open()) {
        gMagDumpFile.close();
    }
    if (gRvDumpFile.is_open()) {
        gRvDumpFile.close();
    }
    if (gBaroDumpFile.is_open()) {
        gBaroDumpFile.close();
    }
    if (gWifiDumpFile.is_open()) {
        gWifiDumpFile.close();
    }
    if (gBleDumpFile.is_open()) {
        gBleDumpFile.close();
    }
    if (gCellDumpFile.is_open()) {
        gCellDumpFile.close();
    }
    if (gLocationDumpFile.is_open()) {
        gLocationDumpFile.close();
    }
}

bool OpenDumpFilesForBucketNoLock(int64_t bucketStartMs)
{
    if (gDumpSessionDir.empty()) {
        CAMERA_AGENT_LOG_ERROR("Dump session directory is empty");
        return false;
    }
    CloseDumpFilesNoLock();
    const std::string suffix = FormatTimestampForPath(bucketStartMs);
    gAccDumpFile.open(gDumpSessionDir + "/acc_data_" + suffix + ".csv", std::ios::out | std::ios::trunc);
    gGyroDumpFile.open(gDumpSessionDir + "/gyro_data_" + suffix + ".csv", std::ios::out | std::ios::trunc);
    gMagDumpFile.open(gDumpSessionDir + "/mag_data_" + suffix + ".csv", std::ios::out | std::ios::trunc);
    gRvDumpFile.open(gDumpSessionDir + "/rv_data_" + suffix + ".csv", std::ios::out | std::ios::trunc);
    gBaroDumpFile.open(gDumpSessionDir + "/baro_data_" + suffix + ".csv", std::ios::out | std::ios::trunc);
    gWifiDumpFile.open(gDumpSessionDir + "/wifi_data_" + suffix + ".csv", std::ios::out | std::ios::trunc);
    gBleDumpFile.open(gDumpSessionDir + "/ble_data_" + suffix + ".csv", std::ios::out | std::ios::trunc);
    gCellDumpFile.open(gDumpSessionDir + "/cell_data_" + suffix + ".csv", std::ios::out | std::ios::trunc);
    gLocationDumpFile.open(gDumpSessionDir + "/location_data_" + suffix + ".csv", std::ios::out | std::ios::trunc);
    if (!gAccDumpFile.is_open() || !gGyroDumpFile.is_open() || !gMagDumpFile.is_open() || !gRvDumpFile.is_open() ||
        !gBaroDumpFile.is_open() || !gWifiDumpFile.is_open() || !gBleDumpFile.is_open() || !gCellDumpFile.is_open() ||
        !gLocationDumpFile.is_open()) {
        CAMERA_AGENT_LOG_ERROR("Open rolling dump files failed under %{public}s", gDumpSessionDir.c_str());
        CloseDumpFilesNoLock();
        return false;
    }
    gAccDumpFile << "wallTsMs,x,y,z,power_mode\n";
    gGyroDumpFile << "wallTsMs,x,y,z,power_mode\n";
    gMagDumpFile << "wallTsMs,x,y,z,power_mode\n";
    gRvDumpFile << "wallTsMs,x,y,z,w,power_mode\n";
    gBaroDumpFile << "wallTsMs,pressure,power_mode\n";
    gWifiDumpFile << "wallTsMs,bssid,ssid,rssi,freq,power_mode\n";
    gBleDumpFile << "wallTsMs,mac,name,rssi,power_mode\n";
    gCellDumpFile << "wallTsMs,type,cellId,signalIntensity,mcc,mnc,pci,tac,earfcn,power_mode\n";
    gLocationDumpFile << "wallTsMs,latitude,longitude,accuracy,sourceType,power_mode,coordinate_system\n";
    gCurrentDumpBucketStartMs = bucketStartMs;
    CAMERA_AGENT_LOG_INFO("Opened rolling dump bucket: %{public}s", suffix.c_str());
    return true;
}

bool RotateDumpFilesIfNeededNoLock(int64_t wallTsMs)
{
    const int64_t bucketStartMs = (wallTsMs / DUMP_ROLL_INTERVAL_MS) * DUMP_ROLL_INTERVAL_MS;
    if (gCurrentDumpBucketStartMs == bucketStartMs && gAccDumpFile.is_open() && gGyroDumpFile.is_open() &&
        gMagDumpFile.is_open() && gRvDumpFile.is_open() && gBaroDumpFile.is_open() && gWifiDumpFile.is_open() &&
        gBleDumpFile.is_open() && gCellDumpFile.is_open() && gLocationDumpFile.is_open()) {
        return true;
    }
    return OpenDumpFilesForBucketNoLock(bucketStartMs);
}

void DumpWorkerLoop()
{
    for (;;) {
        std::deque<SensorDumpRecord> pending;
        std::deque<TextDumpRecord> pendingText;
        {
            std::unique_lock<std::mutex> lock(gDumpMutex);
            gDumpCv.wait(lock, [] {
                return !gDumpRunning || !gDumpQueue.empty() || !gTextDumpQueue.empty();
            });
            if (!gDumpRunning && gDumpQueue.empty() && gTextDumpQueue.empty()) {
                break;
            }
            pending.swap(gDumpQueue);
            pendingText.swap(gTextDumpQueue);
        }
        for (const auto &item : pending) {
            if (!RotateDumpFilesIfNeededNoLock(item.wallTsMs)) {
                continue;
            }
            if (std::strcmp(item.sensor, "acc") == 0 && gAccDumpFile.is_open()) {
                gAccDumpFile << item.wallTsMs << "," << item.x << "," << item.y << "," << item.z << ","
                    << item.powerMode << "\n";
            } else if (std::strcmp(item.sensor, "gyro") == 0 && gGyroDumpFile.is_open()) {
                gGyroDumpFile << item.wallTsMs << "," << item.x << "," << item.y << "," << item.z << ","
                    << item.powerMode << "\n";
            } else if (std::strcmp(item.sensor, "mag") == 0 && gMagDumpFile.is_open()) {
                gMagDumpFile << item.wallTsMs << "," << item.x << "," << item.y << "," << item.z << ","
                    << item.powerMode << "\n";
            } else if (std::strcmp(item.sensor, "rv") == 0 && gRvDumpFile.is_open()) {
                gRvDumpFile << item.wallTsMs << "," << item.x << "," << item.y << "," << item.z << "," << item.w
                    << "," << item.powerMode << "\n";
            } else if (std::strcmp(item.sensor, "baro") == 0 && gBaroDumpFile.is_open()) {
                gBaroDumpFile << item.wallTsMs << "," << item.x << "," << item.powerMode << "\n";
            } else if (std::strcmp(item.sensor, "loc") == 0 && gLocationDumpFile.is_open()) {
                // Always dump WGS84 (same CRS as OnGpsLocation / PDR); do not convert to GCJ-02.
                gLocationDumpFile << item.wallTsMs << "," << std::fixed << std::setprecision(8) <<
                    item.x << "," << item.y << "," << std::setprecision(2) << item.z << "," <<
                    static_cast<int32_t>(item.w) << std::defaultfloat << "," << item.powerMode << "," <<
                    kLocationDumpCrs << "\n";
            }
        }
        for (const auto &item : pendingText) {
            if (!RotateDumpFilesIfNeededNoLock(item.wallTsMs)) {
                continue;
            }
            if (std::strcmp(item.sensor, "wifi") == 0 && gWifiDumpFile.is_open()) {
                gWifiDumpFile << item.line << "," << item.powerMode << "\n";
            } else if (std::strcmp(item.sensor, "ble") == 0 && gBleDumpFile.is_open()) {
                gBleDumpFile << item.line << "," << item.powerMode << "\n";
            } else if (std::strcmp(item.sensor, "cell") == 0 && gCellDumpFile.is_open()) {
                gCellDumpFile << item.line << "," << item.powerMode << "\n";
            }
        }
        if (gAccDumpFile.is_open()) {
            gAccDumpFile.flush();
        }
        if (gGyroDumpFile.is_open()) {
            gGyroDumpFile.flush();
        }
        if (gMagDumpFile.is_open()) {
            gMagDumpFile.flush();
        }
        if (gRvDumpFile.is_open()) {
            gRvDumpFile.flush();
        }
        if (gBaroDumpFile.is_open()) {
            gBaroDumpFile.flush();
        }
        if (gWifiDumpFile.is_open()) {
            gWifiDumpFile.flush();
        }
        if (gBleDumpFile.is_open()) {
            gBleDumpFile.flush();
        }
        if (gCellDumpFile.is_open()) {
            gCellDumpFile.flush();
        }
        if (gLocationDumpFile.is_open()) {
            gLocationDumpFile.flush();
        }
    }
}

void StartDumpWorker()
{
    std::lock_guard<std::mutex> lock(gDumpMutex);
    if (gDumpRunning) {
        return;
    }
    const int64_t nowMs = GetTimestampMs();
    const std::string sessionDir = std::string(SENSOR_DUMP_DIR) + "/" + FormatTimestampForPath(nowMs);
    if (!EnsureDirectoryExistsRecursive(sessionDir)) {
        CAMERA_AGENT_LOG_ERROR("Failed to create dump session directory: %{public}s", sessionDir.c_str());
        return;
    }
    gDumpSessionDir = sessionDir;
    PdrEngine::SetTrajectoryDumpDir(sessionDir);
    {
        std::ofstream crsMarker(sessionDir + "/CRS.txt", std::ios::out | std::ios::trunc);
        if (crsMarker.is_open()) {
            crsMarker << "location_data CRS=" << kLocationDumpCrs << "\n";
        }
    }
    gCurrentDumpBucketStartMs = -1;
    gDumpQueue.clear();
    gTextDumpQueue.clear();
    gDumpRunning = true;
    gDumpThread = std::thread(DumpWorkerLoop);
    CAMERA_AGENT_LOG_INFO("Sensor dump started, session directory: %{public}s", gDumpSessionDir.c_str());
}

void StopDumpWorker()
{
    {
        std::lock_guard<std::mutex> lock(gDumpMutex);
        if (!gDumpRunning) {
            return;
        }
        gDumpRunning = false;
    }
    gDumpCv.notify_all();
    if (gDumpThread.joinable()) {
        gDumpThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(gDumpMutex);
        CloseDumpFilesNoLock();
        gDumpQueue.clear();
        gTextDumpQueue.clear();
        gDumpSessionDir.clear();
        PdrEngine::ClearTrajectoryDumpDir();
        gCurrentDumpBucketStartMs = -1;
    }
    CAMERA_AGENT_LOG_INFO("Sensor dump stopped");
}

void EnqueueDumpRecord(const char *sensor, int64_t wallTsMs, double x, double y, double z, double w)
{
    std::lock_guard<std::mutex> lock(gDumpMutex);
    if (!gDumpRunning) {
        return;
    }
    SensorDumpRecord record;
    (void)snprintf(record.sensor, sizeof(record.sensor), "%s", sensor == nullptr ? "" : sensor);
    record.wallTsMs = wallTsMs;
    record.x = x;
    record.y = y;
    record.z = z;
    record.w = w;
    (void)snprintf(record.powerMode, sizeof(record.powerMode), "%s", CachedPowerModeString());
    gDumpQueue.push_back(record);
    if (gDumpQueue.size() > 20000) {
        gDumpQueue.pop_front();
    }
    gDumpCv.notify_one();
}

void EnqueueTextDumpRecord(const char *sensor, int64_t wallTsMs, const std::string &line)
{
    std::lock_guard<std::mutex> lock(gDumpMutex);
    if (!gDumpRunning) {
        return;
    }
    TextDumpRecord record;
    (void)snprintf(record.sensor, sizeof(record.sensor), "%s", sensor == nullptr ? "" : sensor);
    record.wallTsMs = wallTsMs;
    record.line = line;
    (void)snprintf(record.powerMode, sizeof(record.powerMode), "%s", CachedPowerModeString());
    gTextDumpQueue.push_back(record);
    if (gTextDumpQueue.size() > 20000) {
        gTextDumpQueue.pop_front();
    }
    gDumpCv.notify_one();
}

bool ShouldLogSensor(int64_t tsMs, int64_t &lastLogMs)
{
    if (SENSOR_LOG_INTERVAL_MS <= 0) {
        return true;
    }
    if (lastLogMs == 0 || tsMs - lastLogMs >= SENSOR_LOG_INTERVAL_MS) {
        lastLogMs = tsMs;
        return true;
    }
    return false;
}

int64_t GetTimestampMs()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void CachePowerMode(PowerMode mode)
{
    gCachedPowerModeInt.store(static_cast<int>(mode), std::memory_order_relaxed);
}

const char *CachedPowerModeString()
{
    return PowerModeController::ModeToString(
        static_cast<PowerMode>(gCachedPowerModeInt.load(std::memory_order_relaxed)));
}

void ScheduleApplyPowerProfile(const PowerModeSnapshot &snapshot)
{
    // Never join agent scheduler / retune sensors on the sensor callback thread.
    std::thread([snapshot]() { ApplyPowerProfile(snapshot); }).detach();
}

void LogSelfSensorPermissionState()
{
#if CAMERA_AGENT_HAS_ACCESS_TOKEN_API
    using namespace OHOS::Security::AccessToken;
    AccessTokenID selfToken = IPCSkeleton::GetSelfTokenID();
    int32_t verifyRet = AccessTokenKit::VerifyAccessToken(selfToken, ACC_PERMISSION);
    CAMERA_AGENT_LOG_INFO("StartSensorCollection self check: tokenId=%{public}u, verify(%{public}s)=%{public}d, "
        "pid=%{public}d, uid=%{public}d", selfToken, ACC_PERMISSION, verifyRet, getpid(), getuid());
#else
    CAMERA_AGENT_LOG_WARN("AccessToken headers unavailable, skip VerifyAccessToken self-check");
#endif
}

void LogSelfLocationPermissionState()
{
#if CAMERA_AGENT_HAS_ACCESS_TOKEN_API
    using namespace OHOS::Security::AccessToken;
    AccessTokenID selfToken = IPCSkeleton::GetSelfTokenID();
    AccessTokenID callingToken = IPCSkeleton::GetCallingTokenID();
    AccessTokenID firstToken = IPCSkeleton::GetFirstTokenID();
    int32_t selfApproxRet = AccessTokenKit::VerifyAccessToken(selfToken, APPROX_LOCATION_PERMISSION);
    int32_t selfLocationRet = AccessTokenKit::VerifyAccessToken(selfToken, LOCATION_PERMISSION);
    int32_t callingApproxRet = AccessTokenKit::VerifyAccessToken(callingToken, APPROX_LOCATION_PERMISSION);
    int32_t callingLocationRet = AccessTokenKit::VerifyAccessToken(callingToken, LOCATION_PERMISSION);
    int32_t firstApproxRet = AccessTokenKit::VerifyAccessToken(firstToken, APPROX_LOCATION_PERMISSION);
    int32_t firstLocationRet = AccessTokenKit::VerifyAccessToken(firstToken, LOCATION_PERMISSION);
    CAMERA_AGENT_LOG_INFO("StartLocationCollection permission check: selfToken=%{public}u approx=%{public}d "
        "location=%{public}d, callingToken=%{public}u approx=%{public}d location=%{public}d, "
        "firstToken=%{public}u approx=%{public}d location=%{public}d, pid=%{public}d uid=%{public}d",
        selfToken, selfApproxRet, selfLocationRet, callingToken, callingApproxRet, callingLocationRet, firstToken,
        firstApproxRet, firstLocationRet, getpid(), getuid());
#else
    CAMERA_AGENT_LOG_WARN("AccessToken headers unavailable, skip location VerifyAccessToken self-check");
#endif
}

bool HasActiveSensorNoLock()
{
    return gAccStarted || gGyroStarted || gMagStarted || gRvStarted || gBaroStarted;
}

bool HasActiveCollectionNoLock()
{
    return HasActiveSensorNoLock() || gLocationStarted || gWifiStarted || gBleStarted || gCellStarted;
}

void StopDumpWorkerIfIdleNoLock()
{
    if (!HasActiveCollectionNoLock()) {
        StopDumpWorker();
    }
}

#if CAMERA_AGENT_HAS_LOCATION_API
class LocationCallback final : public IRemoteStub<Location::ILocatorCallback> {
public:
    int32_t OnRemoteRequest(uint32_t code, MessageParcel &data, MessageParcel &reply,
        MessageOption &option) override
    {
        if (data.ReadInterfaceToken() != GetDescriptor()) {
            CAMERA_AGENT_LOG_ERROR("Location callback invalid interface token");
            return OHOS::ERR_INVALID_STATE;
        }
        switch (code) {
            case Location::ILocatorCallback::RECEIVE_LOCATION_INFO_EVENT:
            case Location::ILocatorCallback::RECEIVE_LOCATION_INFO_EVENT_V9: {
                std::unique_ptr<Location::Location> location = Location::Location::UnmarshallingMakeUnique(data);
                OnLocationReport(location);
                return OHOS::NO_ERROR;
            }
            case Location::ILocatorCallback::RECEIVE_LOCATION_STATUS_EVENT: {
                OnLocatingStatusChange(data.ReadInt32());
                return OHOS::NO_ERROR;
            }
            case Location::ILocatorCallback::RECEIVE_ERROR_INFO_EVENT: {
                OnErrorReport(data.ReadInt32());
                return OHOS::NO_ERROR;
            }
            default:
                return IRemoteStub<Location::ILocatorCallback>::OnRemoteRequest(code, data, reply, option);
        }
    }

    void OnLocationReport(const std::unique_ptr<Location::Location> &location) override
    {
        if (location == nullptr) {
            CAMERA_AGENT_LOG_ERROR("Location callback received nullptr location");
            return;
        }
        const int64_t wallTsMs = GetTimestampMs();
        const double wgs84Latitude = location->GetLatitude();
        const double wgs84Longitude = location->GetLongitude();
        // GCJ only for GeoEngine map display / reverse-geocode; dumps & scene logic stay WGS84.
        const GeoCoordinate gcj02 = Wgs84ToGcj02(wgs84Latitude, wgs84Longitude);
        const double accuracy = location->GetAccuracy();
        const int32_t sourceType = location->GetLocationSourceType();
        {
            std::lock_guard<std::mutex> lock(gSensorMutex);
            if (!gLocationStarted) {
                return;
            }
            gLocationFrame = {wallTsMs, wgs84Latitude, wgs84Longitude, accuracy, sourceType};
            gHasLocationFrame = true;
        }
        EnqueueDumpRecord("loc", wallTsMs, wgs84Latitude, wgs84Longitude, accuracy, static_cast<double>(sourceType));
        GeoEngine::NotifyLocationSample(wgs84Latitude, wgs84Longitude, gcj02.latitude, gcj02.longitude, accuracy);
        const float headingDeg = static_cast<float>(location->GetDirection());
        const bool gpsValid = (accuracy > 0.0);
        PdrEngine::PushGps(wallTsMs, wgs84Latitude, wgs84Longitude, static_cast<float>(accuracy), headingDeg, gpsValid);
        {
            RawGpsLocation gps;
            gps.observed_at = wallTsMs;
            gps.latitude = wgs84Latitude;
            gps.longitude = wgs84Longitude;
            gps.horizontal_accuracy_m = accuracy;
            gps.has_horizontal_accuracy = true;
            gps.valid = gpsValid;
            gps.source_type = sourceType;
            ProactiveAgentBusinessModule::GetInstance().OnGpsLocation(gps);
        }
        CAMERA_AGENT_LOG_INFO(
            "location tsMs=%{public}" PRId64 " wgs84Lat=%{public}.8f wgs84Lon=%{public}.8f acc=%{public}.2f "
            "source=%{public}d",
            wallTsMs, wgs84Latitude, wgs84Longitude, accuracy, sourceType);
    }

    void OnLocatingStatusChange(const int status) override
    {
        CAMERA_AGENT_LOG_INFO("location status changed: %{public}d", status);
    }

    void OnErrorReport(const int errorCode) override
    {
        CAMERA_AGENT_LOG_ERROR("location error reported: %{public}d", errorCode);
    }
};
#endif

void CleanupSensorSubscriptionsNoLock()
{
    auto stopOne = [](int32_t sensorType, SensorUser &user, bool &started, const char *name) {
        if (!started) {
            return;
        }
        int32_t deactivateRet = DeactivateSensor(sensorType, &user);
        if (deactivateRet != 0) {
            CAMERA_AGENT_LOG_WARN("DeactivateSensor(%{public}s) failed, ret:%{public}d", name, deactivateRet);
        }
        int32_t unsubscribeRet = UnsubscribeSensor(sensorType, &user);
        if (unsubscribeRet != 0) {
            CAMERA_AGENT_LOG_WARN("UnsubscribeSensor(%{public}s) failed, ret:%{public}d", name, unsubscribeRet);
        }
        started = false;
    };
    stopOne(SENSOR_TYPE_ID_ROTATION_VECTOR, gRvUser, gRvStarted, "rv");
    stopOne(SENSOR_TYPE_ID_BAROMETER, gBaroUser, gBaroStarted, "baro");
    stopOne(SENSOR_TYPE_ID_MAGNETIC_FIELD, gMagUser, gMagStarted, "mag");
    stopOne(SENSOR_TYPE_ID_GYROSCOPE, gGyroUser, gGyroStarted, "gyro");
    stopOne(SENSOR_TYPE_ID_ACCELEROMETER, gAccUser, gAccStarted, "acc");
}

void AccSensorCallback(SensorEvent *event)
{
    if (event == nullptr) {
        return;
    }
    float *data = reinterpret_cast<float *>(event->data);
    if (data == nullptr) {
        return;
    }
    if (event->dataLen < static_cast<int32_t>(sizeof(float) * 3)) {
        return;
    }
    const int64_t unixTsMs = GetTimestampMs();
    bool doLog = false;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gAccFrames.push_back({unixTsMs, data[0], data[1], data[2]});
        while (gAccFrames.size() > SENSOR_CACHE_SIZE) {
            gAccFrames.pop_front();
        }
        doLog = ShouldLogSensor(unixTsMs, gLastAccLogMs);
    }
    // Dump/GetSnapshot must not run under gSensorMutex — HAP GetRecentAccFrames also needs that lock.
    EnqueueDumpRecord("acc", unixTsMs, data[0], data[1], data[2], 0.0f);
    if (doLog) {
        CAMERA_AGENT_LOG_INFO("sensor acc tsMs=%{public}" PRId64 " x=%{public}.6f y=%{public}.6f z=%{public}.6f",
            unixTsMs, data[0], data[1], data[2]);
    }
    PowerModeController::GetInstance().PushAcc(unixTsMs, data[0], data[1], data[2]);
    (void)PowerModeController::GetInstance().Evaluate(unixTsMs);
    PdrEngine::PushAcc(unixTsMs, data[0], data[1], data[2]);
    LeaveCarEngine::PushAcc(unixTsMs, data[0], data[1], data[2]);
}

void GyroSensorCallback(SensorEvent *event)
{
    if (event == nullptr) {
        return;
    }
    float *data = reinterpret_cast<float *>(event->data);
    if (data == nullptr || event->dataLen < static_cast<int32_t>(sizeof(float) * 3)) {
        return;
    }
    const int64_t unixTsMs = GetTimestampMs();
    bool doLog = false;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gGyroFrames.push_back({unixTsMs, data[0], data[1], data[2]});
        while (gGyroFrames.size() > SENSOR_CACHE_SIZE) {
            gGyroFrames.pop_front();
        }
        doLog = ShouldLogSensor(unixTsMs, gLastGyroLogMs);
    }
    EnqueueDumpRecord("gyro", unixTsMs, data[0], data[1], data[2], 0.0f);
    if (doLog) {
        CAMERA_AGENT_LOG_INFO("sensor gyro tsMs=%{public}" PRId64 " x=%{public}.6f y=%{public}.6f z=%{public}.6f",
            unixTsMs, data[0], data[1], data[2]);
    }
    PdrEngine::PushGyro(unixTsMs, data[0], data[1], data[2]);
    LeaveCarEngine::PushGyro(unixTsMs, data[0], data[1], data[2]);
}

void MagSensorCallback(SensorEvent *event)
{
    if (event == nullptr) {
        return;
    }
    float *data = reinterpret_cast<float *>(event->data);
    if (data == nullptr || event->dataLen < static_cast<int32_t>(sizeof(float) * 3)) {
        return;
    }
    const int64_t unixTsMs = GetTimestampMs();
    bool doLog = false;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gMagFrames.push_back({unixTsMs, data[0], data[1], data[2]});
        while (gMagFrames.size() > SENSOR_CACHE_SIZE) {
            gMagFrames.pop_front();
        }
        doLog = ShouldLogSensor(unixTsMs, gLastMagLogMs);
    }
    EnqueueDumpRecord("mag", unixTsMs, data[0], data[1], data[2], 0.0f);
    if (doLog) {
        CAMERA_AGENT_LOG_INFO("sensor mag tsMs=%{public}" PRId64 " x=%{public}.6f y=%{public}.6f z=%{public}.6f",
            unixTsMs, data[0], data[1], data[2]);
    }
    PdrEngine::PushMag(unixTsMs, data[0], data[1], data[2]);
    LeaveCarEngine::PushMag(unixTsMs, data[0], data[1], data[2]);
}

void RvSensorCallback(SensorEvent *event)
{
    if (event == nullptr) {
        return;
    }
    float *data = reinterpret_cast<float *>(event->data);
    if (data == nullptr || event->dataLen < static_cast<int32_t>(sizeof(float) * 4)) {
        return;
    }
    const int64_t unixTsMs = GetTimestampMs();
    bool doLog = false;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gRvFrames.push_back({unixTsMs, data[0], data[1], data[2], data[3]});
        while (gRvFrames.size() > SENSOR_CACHE_SIZE) {
            gRvFrames.pop_front();
        }
        doLog = ShouldLogSensor(unixTsMs, gLastRvLogMs);
    }
    EnqueueDumpRecord("rv", unixTsMs, data[0], data[1], data[2], data[3]);
    if (doLog) {
        CAMERA_AGENT_LOG_INFO(
            "sensor rv tsMs=%{public}" PRId64 " x=%{public}.6f y=%{public}.6f z=%{public}.6f w=%{public}.6f",
            unixTsMs, data[0], data[1], data[2], data[3]);
    }
    PdrEngine::PushRv(unixTsMs, data[0], data[1], data[2], data[3]);
}

void BaroSensorCallback(SensorEvent *event)
{
    if (event == nullptr) {
        return;
    }
    float *data = reinterpret_cast<float *>(event->data);
    if (data == nullptr || event->dataLen < static_cast<int32_t>(sizeof(float))) {
        return;
    }
    const int64_t unixTsMs = GetTimestampMs();
    const float pressure = data[0];
    bool doLog = false;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gBaroFrames.push_back({unixTsMs, pressure});
        while (gBaroFrames.size() > SENSOR_CACHE_SIZE) {
            gBaroFrames.pop_front();
        }
        doLog = ShouldLogSensor(unixTsMs, gLastBaroLogMs);
    }
    EnqueueDumpRecord("baro", unixTsMs, pressure, 0.0, 0.0, 0.0);
    if (doLog) {
        CAMERA_AGENT_LOG_INFO("sensor baro tsMs=%{public}" PRId64 " pressure=%{public}.3f", unixTsMs, pressure);
    }
}

std::string JsonEscape(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '"') {
            escaped += "\\\"";
        } else if (ch == '\\') {
            escaped += "\\\\";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

void OnWifiFrame(WifiErrorCode code, WifiFp &wifiFp)
{
    if (code != WIFI_SUCCESS) {
        return;
    }
    std::ostringstream dumpPrefix;
    dumpPrefix << wifiFp.timestamp;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gLatestWifi = wifiFp;
        gHasWifi = true;
    }
    {
        commute_sa::WifiScanSample scan;
        scan.t_ms = wifiFp.timestamp;
        scan.aps.reserve(wifiFp.aps.size());
        for (const auto &ap : wifiFp.aps) {
            if (ap.bssid.empty()) {
                continue;
            }
            commute_sa::WifiApSample s;
            s.bssid = ap.bssid;
            s.rssi = ap.rssi;
            scan.aps.push_back(std::move(s));
        }
        if (!scan.aps.empty()) {
            commute_sa::BaselineRuntime::GetInstance().OnWifiScan(scan);
        }
    }
    for (const auto &ap : wifiFp.aps) {
        std::ostringstream line;
        line << dumpPrefix.str() << "," << ap.bssid << "," << ap.ssid << "," << ap.rssi << "," << ap.freq;
        EnqueueTextDumpRecord("wifi", wifiFp.timestamp, line.str());
    }
}

void OnBleFrame(const BleData &bleData)
{
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gLatestBle = bleData;
        gHasBle = true;
    }
    if (!bleData.mac.empty() && bleData.timestamp > 0) {
        commute_sa::BleSample sample;
        sample.t_ms = bleData.timestamp;
        sample.mac = bleData.mac;
        sample.rssi = bleData.rssi;
        commute_sa::BaselineRuntime::GetInstance().OnBleSample(sample);
    }
    std::ostringstream line;
    line << bleData.timestamp << "," << bleData.mac << "," << bleData.name << "," << bleData.rssi;
    EnqueueTextDumpRecord("ble", bleData.timestamp, line.str());
}

void GetCellRadioExtras(const CellInfo &cell, int32_t &pci, int32_t &tac, int32_t &earfcn)
{
    pci = 0;
    tac = 0;
    earfcn = 0;
    // OHOS builds without RTTI — switch on celltype, then static_cast (set in Parse*).
    switch (cell.celltype) {
        case LTE: {
            const auto &lte = static_cast<const CellLte &>(cell);
            pci = lte.pci;
            tac = lte.tac;
            earfcn = lte.earfcn;
            break;
        }
        case NR: {
            const auto &nr = static_cast<const CellNr &>(cell);
            pci = nr.pci;
            tac = nr.tac;
            earfcn = nr.nrArfcn;
            break;
        }
        case W_CDMA: {
            const auto &wcdma = static_cast<const CellWcdma &>(cell);
            earfcn = wcdma.uarfcn;
            break;
        }
        case GSM: {
            const auto &gsm = static_cast<const CellGsm &>(cell);
            earfcn = gsm.arfcn;
            break;
        }
        case TDS_CDMA: {
            const auto &tds = static_cast<const CellTdsCdma &>(cell);
            earfcn = tds.uarfcn;
            break;
        }
        default:
            break;
    }
}

void OnCellFrame(const std::vector<std::shared_ptr<CellInfo>> &cellVec)
{
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gLatestCells = cellVec;
        gHasCell = !cellVec.empty();
    }
    // Prefer strongest serving-like sample for leave evidence.
    const CellInfo *best = nullptr;
    for (const auto &cell : cellVec) {
        if (cell == nullptr || cell->cellId == 0) {
            continue;
        }
        if (best == nullptr || cell->signalIntensity > best->signalIntensity) {
            best = cell.get();
        }
    }
    if (best != nullptr) {
        commute_sa::CellSample sample;
        sample.t_ms = best->timestamp;
        sample.cell_id = best->cellId;
        sample.rssi = best->signalIntensity;
        commute_sa::BaselineRuntime::GetInstance().OnCellSample(sample);
    }
    for (const auto &cell : cellVec) {
        if (cell == nullptr) {
            continue;
        }
        int32_t pci = 0;
        int32_t tac = 0;
        int32_t earfcn = 0;
        GetCellRadioExtras(*cell, pci, tac, earfcn);
        std::ostringstream line;
        line << cell->timestamp << "," << cell->TypeToString() << "," << cell->cellId << ","
             << cell->signalIntensity << "," << cell->mcc << "," << cell->mnc << ","
             << pci << "," << tac << "," << earfcn;
        EnqueueTextDumpRecord("cell", cell->timestamp, line.str());
    }
}

int32_t StartOneSensorNoLock(int32_t sensorType, SensorUser &user, bool &started, void (*callback)(SensorEvent *),
    int64_t sampleIntervalNs, const char *name)
{
    user.callback = callback;
    int32_t ret = SubscribeSensor(sensorType, &user);
    if (ret != 0) {
        CAMERA_AGENT_LOG_ERROR("SubscribeSensor(%{public}s) failed, ret:%{public}d", name, ret);
        return ret;
    }
    ret = SetBatch(sensorType, &user, sampleIntervalNs, sampleIntervalNs);
    if (ret != 0) {
        CAMERA_AGENT_LOG_ERROR("SetBatch(%{public}s) failed, ret:%{public}d", name, ret);
        (void)UnsubscribeSensor(sensorType, &user);
        return ret;
    }
    ret = ActivateSensor(sensorType, &user);
    if (ret != 0) {
        CAMERA_AGENT_LOG_ERROR("ActivateSensor(%{public}s) failed, ret:%{public}d", name, ret);
        (void)DeactivateSensor(sensorType, &user);
        (void)UnsubscribeSensor(sensorType, &user);
        return ret;
    }
    started = true;
    CAMERA_AGENT_LOG_INFO("Start sensor success: %{public}s", name);
    return 0;
}

void UpdateOneSensorBatchNoLock(int32_t sensorType, SensorUser &user, bool started, int64_t sampleIntervalNs,
    const char *name)
{
    if (!started) {
        return;
    }
    int32_t ret = DeactivateSensor(sensorType, &user);
    if (ret != 0) {
        CAMERA_AGENT_LOG_WARN("DeactivateSensor(%{public}s) for batch update failed, ret:%{public}d", name, ret);
    }
    ret = SetBatch(sensorType, &user, sampleIntervalNs, sampleIntervalNs);
    if (ret != 0) {
        CAMERA_AGENT_LOG_ERROR("SetBatch(%{public}s) rate update failed, ret:%{public}d", name, ret);
    }
    ret = ActivateSensor(sensorType, &user);
    if (ret != 0) {
        CAMERA_AGENT_LOG_ERROR("ActivateSensor(%{public}s) after batch update failed, ret:%{public}d", name, ret);
        return;
    }
    CAMERA_AGENT_LOG_INFO("Updated sensor batch %{public}s intervalNs=%{public}" PRId64, name, sampleIntervalNs);
}

void UpdateAllSensorBatchesNoLock(int64_t sampleIntervalNs)
{
    gSensorSampleIntervalNs = sampleIntervalNs;
    UpdateOneSensorBatchNoLock(SENSOR_TYPE_ID_ACCELEROMETER, gAccUser, gAccStarted, sampleIntervalNs, "acc");
    UpdateOneSensorBatchNoLock(SENSOR_TYPE_ID_GYROSCOPE, gGyroUser, gGyroStarted, sampleIntervalNs, "gyro");
    UpdateOneSensorBatchNoLock(SENSOR_TYPE_ID_MAGNETIC_FIELD, gMagUser, gMagStarted, sampleIntervalNs, "mag");
    UpdateOneSensorBatchNoLock(SENSOR_TYPE_ID_ROTATION_VECTOR, gRvUser, gRvStarted, sampleIntervalNs, "rv");
    UpdateOneSensorBatchNoLock(SENSOR_TYPE_ID_BAROMETER, gBaroUser, gBaroStarted, sampleIntervalNs, "baro");
}

#if CAMERA_AGENT_HAS_LOCATION_API
int32_t RestartLocationCollectionWithInterval(int32_t intervalSec)
{
    std::shared_ptr<Location::LocatorImpl> locator;
    sptr<Location::ILocatorCallback> oldCallback;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        if (!gLocationStarted) {
            gLocationIntervalSec = intervalSec;
            return 0;
        }
        locator = gLocator;
        oldCallback = gLocatorCallback;
    }
    if (locator != nullptr && oldCallback != nullptr) {
        (void)locator->StopLocatingV9(oldCallback);
    }
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gLocator = nullptr;
        gLocatorCallback = nullptr;
        gLocationStarted = false;
        gLocationIntervalSec = intervalSec;
    }

    auto newLocator = Location::Locator::GetInstance();
    if (newLocator == nullptr) {
        CAMERA_AGENT_LOG_ERROR("RestartLocation: Locator::GetInstance failed");
        return -10;
    }
    sptr<Location::ILocatorCallback> callback = new (std::nothrow) LocationCallback();
    if (callback == nullptr) {
        CAMERA_AGENT_LOG_ERROR("RestartLocation: create callback failed");
        return -11;
    }
    auto requestConfig = std::make_unique<Location::RequestConfig>();
    if (requestConfig == nullptr) {
        CAMERA_AGENT_LOG_ERROR("RestartLocation: create request config failed");
        return -12;
    }
    requestConfig->SetScenario(Location::SCENE_NAVIGATION);
    requestConfig->SetTimeInterval(intervalSec);
    Location::LocationErrCode ret = newLocator->StartLocatingV9(requestConfig, callback);
    if (ret != Location::ERRCODE_SUCCESS) {
        CAMERA_AGENT_LOG_ERROR("RestartLocation StartLocatingV9 failed, ret=%{public}d", ret);
        return static_cast<int32_t>(ret);
    }
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gLocator = newLocator;
        gLocatorCallback = callback;
        gLocationStarted = true;
    }
    CAMERA_AGENT_LOG_INFO("RestartLocation success intervalSec=%{public}d", intervalSec);
    return 0;
}
#endif

void ApplyPowerProfile(const PowerModeSnapshot &snapshot)
{
    PowerMode previous = PowerMode::kHighStill;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        previous = gAppliedPowerMode;
        if (previous == snapshot.mode) {
            return;
        }
        gAppliedPowerMode = snapshot.mode;
    }
    CachePowerMode(snapshot.mode);

    const bool lowPower = (snapshot.mode == PowerMode::kLowPower);
    const bool previousLow = (previous == PowerMode::kLowPower);
    const int64_t tickMs = lowPower ? PowerModeController::kLowTickIntervalMs
                                    : PowerModeController::kHighTickIntervalMs;

    CAMERA_AGENT_LOG_INFO("Applying power profile %{public}s -> %{public}s agent=%{public}d",
        PowerModeController::ModeToString(previous),
        PowerModeController::ModeToString(snapshot.mode),
        snapshot.agentInferenceEnabled ? 1 : 0);

    // HIGH_WALKING <-> HIGH_STILL: same sample rates & agent policy; mode is read from controller on tick.
    if (previousLow == lowPower) {
        return;
    }

    const int64_t sensorNs = lowPower ? PowerModeController::kLowSensorIntervalNs
                                      : PowerModeController::kHighSensorIntervalNs;
    const int32_t locSec = lowPower ? PowerModeController::kLowLocationIntervalSec
                                    : PowerModeController::kHighLocationIntervalSec;
    const int64_t scanMs = lowPower ? PowerModeController::kLowScanIntervalMs
                                    : PowerModeController::kHighScanIntervalMs;

    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        // Skip Deactivate/SetBatch when rate unchanged — retune can drop sensor subscriptions.
        if (gSensorSampleIntervalNs != sensorNs) {
            gSensorSampleIntervalNs = sensorNs;
            if (HasActiveSensorNoLock()) {
                UpdateAllSensorBatchesNoLock(sensorNs);
            }
        }
    }

    if (gWifiDesired) {
        WifiProvider::GetInstance()->SetScanIntervalMs(scanMs);
    }
    if (gCellDesired) {
        CellProvider::GetInstance()->SetScanIntervalMs(scanMs);
    }
    if (gBleDesired) {
        if (lowPower) {
            BleProvider::GetInstance()->Disable();
            std::lock_guard<std::mutex> lock(gSensorMutex);
            gBleStarted = false;
            gBleSuspendedForPower = true;
        } else if (gBleSuspendedForPower || !gBleStarted) {
            BleProvider::GetInstance()->Enable();
            std::lock_guard<std::mutex> lock(gSensorMutex);
            gBleStarted = true;
            gBleSuspendedForPower = false;
        }
    }

#if CAMERA_AGENT_HAS_LOCATION_API
    if (gLocationDesired) {
        bool locStarted = false;
        {
            std::lock_guard<std::mutex> lock(gSensorMutex);
            locStarted = gLocationStarted;
            gLocationIntervalSec = locSec;
        }
        if (locStarted) {
            (void)RestartLocationCollectionWithInterval(locSec);
        }
    }
#endif

    ProactiveAgentBusinessModule::GetInstance().ApplyPowerPolicy(tickMs, snapshot.agentInferenceEnabled);
}

void MaybeHandlePowerDecision(const PowerModeDecision &decision)
{
    if (!decision.changed) {
        return;
    }
    ScheduleApplyPowerProfile(decision.snapshot);
}
} // namespace

AgentServiceAbility::AgentServiceAbility(int32_t saId, bool runOnCreate)
    : SystemAbility(saId, runOnCreate)
{
    CAMERA_AGENT_LOG_INFO("AgentServiceAbility: saId:%d, runOnCreate:%d", saId, runOnCreate);
}

AgentServiceAbility::~AgentServiceAbility()
{
    StopWifiCollection();
    StopBleCollection();
    StopCellCollection();
    StopLocationCollection();
    StopSensorCollection();
    LeaveCarEngine::Stop();
    PdrEngine::Stop();
    ProactiveAgentBusinessModule::GetInstance().Shutdown();
    CAMERA_AGENT_LOG_INFO("~AgentServiceAbility");
}

std::string AgentServiceAbility::HelloWorld()
{
    CAMERA_AGENT_LOG_INFO("HelloWorld called");
    return "commute_scene";
}

void AgentServiceAbility::OnStart()
{
    CAMERA_AGENT_LOG_INFO("AgentServiceAbility OnStart called");
    bool res = Publish(this);
    if (!res) {
        CAMERA_AGENT_LOG_ERROR("Publish failed");
    } else {
		CAMERA_AGENT_LOG_INFO("Publish success!");
	}
}
void AgentServiceAbility::OnStop()
{
    CAMERA_AGENT_LOG_WARN("AgentServiceAbility OnStop — SA process stopping, collection ends");
    StopWifiCollection();
    StopBleCollection();
    StopCellCollection();
    StopLocationCollection();
    StopSensorCollection();
    LeaveCarEngine::Stop();
    PdrEngine::Stop();
    ProactiveAgentBusinessModule::GetInstance().Shutdown();
    CAMERA_AGENT_LOG_INFO("ability stop called");
}

void AgentServiceAbility::UnLoadService()
{
    CAMERA_AGENT_LOG_WARN("AgentServiceAbility UnLoadService requested — will unload SA 9903");
    CAMERA_AGENT_LOG_INFO("AgentServiceAbility, come in UnLoadService");
    sptr<ISystemAbilityManager> sam = SystemAbilityManagerClient::GetInstance().GetSystemAbilityManager();
    if (sam == nullptr) {
        CAMERA_AGENT_LOG_ERROR("AgentServiceAbility, GetSystemAbilityManager failed");
        return;
    }
    int32_t unLoadRes = sam->UnloadSystemAbility(COMMUTE_AGENT_SERVICE_ID);
    if (unLoadRes != 0) {
        CAMERA_AGENT_LOG_INFO("called, unload SystemAbility failed, error is: %{public}d.", unLoadRes);
    } else{
        CAMERA_AGENT_LOG_INFO("AgentServiceAbility, UnLoadService successful");
    }
}

int32_t AgentServiceAbility::StartSensorCollection()
{
    std::lock_guard<std::mutex> lock(gSensorMutex);
    LogSelfSensorPermissionState();
    if (gAccStarted && gGyroStarted && gMagStarted && gRvStarted && gBaroStarted) {
        return 0;
    }
    StartDumpWorker();
    PdrEngine::ClearResults();
    GeoEngine::Init();
    GeoEngine::ArmStartPlaceLatch();
    gAppliedPowerMode = PowerMode::kHighStill;
    gBleSuspendedForPower = false;
    gSensorSampleIntervalNs = DEFAULT_SAMPLE_INTERVAL_NS;
    CachePowerMode(PowerMode::kHighStill);
    int64_t sampleIntervalNs = gSensorSampleIntervalNs;
    if (sampleIntervalNs < MIN_SAMPLE_INTERVAL_NS) {
        sampleIntervalNs = MIN_SAMPLE_INTERVAL_NS;
    }
    if (StartOneSensorNoLock(SENSOR_TYPE_ID_ACCELEROMETER, gAccUser, gAccStarted, AccSensorCallback, sampleIntervalNs,
        "acc") != 0) {
        CleanupSensorSubscriptionsNoLock();
        StopDumpWorkerIfIdleNoLock();
        return -2;
    }
    if (StartOneSensorNoLock(SENSOR_TYPE_ID_GYROSCOPE, gGyroUser, gGyroStarted, GyroSensorCallback, sampleIntervalNs,
        "gyro") != 0) {
        CleanupSensorSubscriptionsNoLock();
        StopDumpWorkerIfIdleNoLock();
        return -3;
    }
    if (StartOneSensorNoLock(SENSOR_TYPE_ID_MAGNETIC_FIELD, gMagUser, gMagStarted, MagSensorCallback, sampleIntervalNs,
        "mag") != 0) {
        CleanupSensorSubscriptionsNoLock();
        StopDumpWorkerIfIdleNoLock();
        return -4;
    }
    if (StartOneSensorNoLock(SENSOR_TYPE_ID_ROTATION_VECTOR, gRvUser, gRvStarted, RvSensorCallback, sampleIntervalNs,
        "rv") != 0) {
        CleanupSensorSubscriptionsNoLock();
        StopDumpWorkerIfIdleNoLock();
        return -5;
    }
    if (StartOneSensorNoLock(SENSOR_TYPE_ID_BAROMETER, gBaroUser, gBaroStarted, BaroSensorCallback, sampleIntervalNs,
        "baro") != 0) {
        CAMERA_AGENT_LOG_ERROR("baro sensor unavailable on this device, continue without it");
        // CleanupSensorSubscriptionsNoLock();
        // StopDumpWorkerIfIdleNoLock();
        // return -6;
    }
    LeaveCarEngine::Start();
    ProactiveAgentBusinessModule::GetInstance().Initialize();
    PowerModeController::GetInstance().Reset();
    PowerModeController::GetInstance().SetOnChanged([](const PowerModeDecision &d) {
        MaybeHandlePowerDecision(d);
    });
    PowerModeController::GetInstance().SetEnabled(true);
    gAccFrames.clear();
    gGyroFrames.clear();
    gMagFrames.clear();
    gRvFrames.clear();
    gBaroFrames.clear();
    gLastAccLogMs = 0;
    gLastGyroLogMs = 0;
    gLastMagLogMs = 0;
    gLastRvLogMs = 0;
    gLastBaroLogMs = 0;
    return 0;
}

int32_t AgentServiceAbility::StopSensorCollection()
{
    PowerModeController::GetInstance().SetEnabled(false);
    PowerModeController::GetInstance().SetOnChanged(nullptr);
    PowerModeController::GetInstance().Reset();
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gAppliedPowerMode = PowerMode::kHighStill;
        gBleSuspendedForPower = false;
        CleanupSensorSubscriptionsNoLock();
        StopDumpWorkerIfIdleNoLock();
    }
    CachePowerMode(PowerMode::kHighStill);
    LeaveCarEngine::Stop();
    PdrEngine::Stop();
    ProactiveAgentBusinessModule::GetInstance().Shutdown();
    return 0;
}

int32_t AgentServiceAbility::StartLocationCollection()
{
#if CAMERA_AGENT_HAS_LOCATION_API
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        if (gLocationStarted) {
            return 0;
        }
    }
    LogSelfLocationPermissionState();
    StartDumpWorker();
    auto locator = Location::Locator::GetInstance();
    if (locator == nullptr) {
        CAMERA_AGENT_LOG_ERROR("Location Locator::GetInstance failed");
        std::lock_guard<std::mutex> lock(gSensorMutex);
        StopDumpWorkerIfIdleNoLock();
        return -10;
    }
    sptr<Location::ILocatorCallback> callback = new (std::nothrow) LocationCallback();
    if (callback == nullptr) {
        CAMERA_AGENT_LOG_ERROR("Create location callback failed");
        std::lock_guard<std::mutex> lock(gSensorMutex);
        StopDumpWorkerIfIdleNoLock();
        return -11;
    }
    auto requestConfig = std::make_unique<Location::RequestConfig>();
    if (requestConfig == nullptr) {
        CAMERA_AGENT_LOG_ERROR("Create location request config failed");
        std::lock_guard<std::mutex> lock(gSensorMutex);
        StopDumpWorkerIfIdleNoLock();
        return -12;
    }
    requestConfig->SetScenario(Location::SCENE_NAVIGATION);
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gLocationIntervalSec = (gAppliedPowerMode == PowerMode::kLowPower)
            ? PowerModeController::kLowLocationIntervalSec
            : PowerModeController::kHighLocationIntervalSec;
        requestConfig->SetTimeInterval(gLocationIntervalSec);
    }
    Location::LocationErrCode ret = locator->StartLocatingV9(requestConfig, callback);
    if (ret != Location::ERRCODE_SUCCESS) {
        CAMERA_AGENT_LOG_ERROR("StartLocatingV9 failed, ret=%{public}d", ret);
        std::lock_guard<std::mutex> lock(gSensorMutex);
        StopDumpWorkerIfIdleNoLock();
        return static_cast<int32_t>(ret);
    }
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gLocator = locator;
        gLocatorCallback = callback;
        gLocationStarted = true;
        gLocationDesired = true;
        gHasLocationFrame = false;
    }
    CAMERA_AGENT_LOG_INFO("StartLocationCollection success");
    return 0;
#else
    CAMERA_AGENT_LOG_ERROR("Location inner API headers unavailable, StartLocationCollection disabled");
    return -10;
#endif
}

int32_t AgentServiceAbility::StopLocationCollection()
{
#if CAMERA_AGENT_HAS_LOCATION_API
    std::shared_ptr<Location::LocatorImpl> locator;
    sptr<Location::ILocatorCallback> callback;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        if (!gLocationStarted) {
            return 0;
        }
        locator = gLocator;
        callback = gLocatorCallback;
    }
    int32_t ret = 0;
    if (locator != nullptr && callback != nullptr) {
        ret = static_cast<int32_t>(locator->StopLocatingV9(callback));
        if (ret != Location::ERRCODE_SUCCESS) {
            CAMERA_AGENT_LOG_WARN("StopLocatingV9 failed, ret=%{public}d", ret);
        }
    }
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        gLocator = nullptr;
        gLocatorCallback = nullptr;
        gLocationStarted = false;
        gLocationDesired = false;
        StopDumpWorkerIfIdleNoLock();
    }
    CAMERA_AGENT_LOG_INFO("StopLocationCollection ret=%{public}d", ret);
    return ret;
#else
    return 0;
#endif
}

std::string AgentServiceAbility::GetRecentAccFrames()
{
    // Copy latest snapshot under a short lock, then serialize outside — HAP polls this heavily.
    Vector3Frame acc {};
    Vector3Frame gyro {};
    Vector3Frame mag {};
    RvFrame rv {};
    BaroFrame baro {};
    LocationFrame loc {};
    WifiFp wifi {};
    BleData ble {};
    std::vector<std::shared_ptr<CellInfo>> cells;
    bool hasAcc = false;
    bool hasGyro = false;
    bool hasMag = false;
    bool hasRv = false;
    bool hasBaro = false;
    bool hasLoc = false;
    bool hasWifi = false;
    bool hasBle = false;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        if (!gAccFrames.empty()) {
            acc = gAccFrames.back();
            hasAcc = true;
        }
        if (!gGyroFrames.empty()) {
            gyro = gGyroFrames.back();
            hasGyro = true;
        }
        if (!gMagFrames.empty()) {
            mag = gMagFrames.back();
            hasMag = true;
        }
        if (!gRvFrames.empty()) {
            rv = gRvFrames.back();
            hasRv = true;
        }
        if (!gBaroFrames.empty()) {
            baro = gBaroFrames.back();
            hasBaro = true;
        }
        if (gHasLocationFrame) {
            loc = gLocationFrame;
            hasLoc = true;
        }
        if (gHasWifi) {
            wifi = gLatestWifi;
            hasWifi = true;
        }
        if (gHasBle) {
            ble = gLatestBle;
            hasBle = true;
        }
        cells = gLatestCells;
    }

    std::ostringstream oss;
    oss << "{";
    oss << "\"acc\":";
    if (hasAcc) {
        oss << "{\"ts\":" << acc.ts << ",\"x\":" << acc.x << ",\"y\":" << acc.y << ",\"z\":" << acc.z << "}";
    } else {
        oss << "null";
    }
    oss << ",\"gyro\":";
    if (hasGyro) {
        oss << "{\"ts\":" << gyro.ts << ",\"x\":" << gyro.x << ",\"y\":" << gyro.y << ",\"z\":" << gyro.z << "}";
    } else {
        oss << "null";
    }
    oss << ",\"mag\":";
    if (hasMag) {
        oss << "{\"ts\":" << mag.ts << ",\"x\":" << mag.x << ",\"y\":" << mag.y << ",\"z\":" << mag.z << "}";
    } else {
        oss << "null";
    }
    oss << ",\"rv\":";
    if (hasRv) {
        oss << "{\"ts\":" << rv.ts << ",\"x\":" << rv.x << ",\"y\":" << rv.y << ",\"z\":" << rv.z << ",\"w\":" << rv.w
            << "}";
    } else {
        oss << "null";
    }
    oss << ",\"location\":";
    if (hasLoc) {
        oss << "{\"ts\":" << loc.ts << ",\"latitude\":" << loc.latitude << ",\"longitude\":" << loc.longitude
            << ",\"accuracy\":" << loc.accuracy << ",\"sourceType\":" << loc.sourceType << "}";
    } else {
        oss << "null";
    }
    oss << ",\"baro\":";
    if (hasBaro) {
        oss << "{\"ts\":" << baro.ts << ",\"pressure\":" << baro.pressure << "}";
    } else {
        oss << "null";
    }
    oss << ",\"wifi\":";
    if (hasWifi) {
        oss << "{\"ts\":" << wifi.timestamp << ",\"apSize\":" << wifi.aps.size() << ",\"aps\":[";
        for (size_t i = 0; i < wifi.aps.size(); ++i) {
            if (i > 0) {
                oss << ",";
            }
            const auto &ap = wifi.aps[i];
            oss << "{\"ts\":" << ap.timestamp << ",\"bssid\":\"" << JsonEscape(ap.bssid) << "\",\"ssid\":\""
                << JsonEscape(ap.ssid) << "\",\"rssi\":" << ap.rssi << ",\"freq\":" << ap.freq << "}";
        }
        oss << "]}";
    } else {
        oss << "null";
    }
    oss << ",\"ble\":";
    if (hasBle) {
        oss << "{\"ts\":" << ble.timestamp << ",\"mac\":\"" << JsonEscape(ble.mac) << "\",\"name\":\""
            << JsonEscape(ble.name) << "\",\"rssi\":" << ble.rssi << "}";
    } else {
        oss << "null";
    }
    oss << ",\"cells\":[";
    for (size_t i = 0; i < cells.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        const auto &cell = cells[i];
        if (cell != nullptr) {
            int32_t pci = 0;
            int32_t tac = 0;
            int32_t earfcn = 0;
            GetCellRadioExtras(*cell, pci, tac, earfcn);
            oss << "{\"ts\":" << cell->timestamp << ",\"type\":\"" << cell->TypeToString() << "\",\"cellId\":"
                << cell->cellId << ",\"signalIntensity\":" << cell->signalIntensity << ",\"mcc\":\""
                << JsonEscape(cell->mcc) << "\",\"mnc\":\"" << JsonEscape(cell->mnc)
                << "\",\"pci\":" << pci << ",\"tac\":" << tac << ",\"earfcn\":" << earfcn << "}";
        }
    }
    oss << "]}";
    return oss.str();
}

std::string AgentServiceAbility::DrainPdrResults()
{
    return PdrEngine::DrainResultsJson();
}

int32_t AgentServiceAbility::ClearPdrResults()
{
    PdrEngine::ClearResults();
    PdrEngine::ClearSavedSegments();
    return 0;
}

std::string AgentServiceAbility::GetPdrDiag()
{
    return PdrEngine::GetDiagJson();
}

std::string AgentServiceAbility::GetLeaveCarState()
{
    return LeaveCarEngine::GetStateJson();
}

std::string AgentServiceAbility::GetStartPlaceInfo()
{
    return GeoEngine::GetStartPlaceJson();
}

int32_t AgentServiceAbility::StartWifiCollection()
{
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        if (gWifiStarted) {
            return 0;
        }
    }
    StartDumpWorker();
    if (!WifiProvider::GetInstance()->RegisterListener(OnWifiFrame)) {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        StopDumpWorkerIfIdleNoLock();
        return -1;
    }
    WifiProvider::GetInstance()->Enable();
    std::lock_guard<std::mutex> lock(gSensorMutex);
    gWifiStarted = true;
    gWifiDesired = true;
    gHasWifi = false;
    const int64_t scanMs = (gAppliedPowerMode == PowerMode::kLowPower)
        ? PowerModeController::kLowScanIntervalMs
        : PowerModeController::kHighScanIntervalMs;
    WifiProvider::GetInstance()->SetScanIntervalMs(scanMs);
    return 0;
}

int32_t AgentServiceAbility::StopWifiCollection()
{
    bool wasStarted = false;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        wasStarted = gWifiStarted;
        gWifiStarted = false;
        gWifiDesired = false;
    }
    if (!wasStarted) {
        return 0;
    }
    WifiProvider::GetInstance()->Disable();
    WifiProvider::GetInstance()->UnRegisterListener();
    std::lock_guard<std::mutex> lock(gSensorMutex);
    StopDumpWorkerIfIdleNoLock();
    return 0;
}

int32_t AgentServiceAbility::StartBleCollection()
{
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        if (gBleStarted) {
            return 0;
        }
    }
    StartDumpWorker();
    if (!BleProvider::GetInstance()->RegisterListener(OnBleFrame)) {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        StopDumpWorkerIfIdleNoLock();
        return -1;
    }
    BleProvider::GetInstance()->Enable();
    std::lock_guard<std::mutex> lock(gSensorMutex);
    gBleDesired = true;
    gBleStarted = true;
    gHasBle = false;
    if (gAppliedPowerMode == PowerMode::kLowPower) {
        gBleSuspendedForPower = true;
        BleProvider::GetInstance()->Disable();
        gBleStarted = false;
    } else {
        gBleSuspendedForPower = false;
    }
    return 0;
}

int32_t AgentServiceAbility::StopBleCollection()
{
    bool wasStarted = false;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        wasStarted = gBleStarted;
        gBleStarted = false;
        gBleDesired = false;
        gBleSuspendedForPower = false;
    }
    if (!wasStarted) {
        return 0;
    }
    BleProvider::GetInstance()->Disable();
    BleProvider::GetInstance()->UnRegisterListener();
    std::lock_guard<std::mutex> lock(gSensorMutex);
    StopDumpWorkerIfIdleNoLock();
    return 0;
}

int32_t AgentServiceAbility::StartCellCollection()
{
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        if (gCellStarted) {
            return 0;
        }
    }
    StartDumpWorker();
    if (!CellProvider::GetInstance()->RegisterListener(OnCellFrame)) {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        StopDumpWorkerIfIdleNoLock();
        return -1;
    }
    CellProvider::GetInstance()->Enable();
    std::lock_guard<std::mutex> lock(gSensorMutex);
    gCellStarted = true;
    gCellDesired = true;
    gHasCell = false;
    gLatestCells.clear();
    const int64_t scanMs = (gAppliedPowerMode == PowerMode::kLowPower)
        ? PowerModeController::kLowScanIntervalMs
        : PowerModeController::kHighScanIntervalMs;
    CellProvider::GetInstance()->SetScanIntervalMs(scanMs);
    return 0;
}

int32_t AgentServiceAbility::StopCellCollection()
{
    bool wasStarted = false;
    {
        std::lock_guard<std::mutex> lock(gSensorMutex);
        wasStarted = gCellStarted;
        gCellStarted = false;
        gCellDesired = false;
    }
    if (!wasStarted) {
        return 0;
    }
    CellProvider::GetInstance()->Disable();
    CellProvider::GetInstance()->UnRegisterListener();
    std::lock_guard<std::mutex> lock(gSensorMutex);
    StopDumpWorkerIfIdleNoLock();
    return 0;
}

std::string AgentServiceAbility::GetProductDebugTimeline()
{
    return ProactiveAgentBusinessModule::GetInstance().GetProductDebugTimelineJson();
}

int32_t AgentServiceAbility::ClearProductDebugTimeline()
{
    ProactiveAgentBusinessModule::GetInstance().ClearProductDebugTimeline();
    return 0;
}

}