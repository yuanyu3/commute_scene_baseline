#include "commute_sa/rolling_sensor_dump.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sys/stat.h>
#include <thread>

#if defined(_WIN32)
#include <direct.h>
#define COMMUTE_SA_MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define COMMUTE_SA_MKDIR(path) mkdir(path, 0755)
#endif

namespace commute_sa {
namespace {

TimestampMs NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool EnsureDirectoryExists(const char *path)
{
    if (path == nullptr) {
        return false;
    }
    struct stat st {};
    if (stat(path, &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }
    if (COMMUTE_SA_MKDIR(path) == 0) {
        return true;
    }
    if (stat(path, &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }
    return false;
}

bool EnsureDirectoryExistsRecursive(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        current.push_back(c);
        const bool isSep = (c == '/' || c == '\\');
        if (isSep && current.size() > 1) {
            // Skip drive root like "D:\"
            if (current.size() == 3 && current[1] == ':') {
                continue;
            }
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

}  // namespace

struct RollingSensorDump::Impl {
    std::string dumpRoot;
    int64_t rollIntervalMs = kDefaultRollIntervalMs;

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<SensorDumpRecord> queue;
    std::deque<TextDumpRecord> textQueue;
    std::thread worker;
    bool running = false;

    std::ofstream acc;
    std::ofstream gyro;
    std::ofstream mag;
    std::ofstream rv;
    std::ofstream baro;
    std::ofstream wifi;
    std::ofstream ble;
    std::ofstream cell;
    std::ofstream location;

    std::string sessionDir;
    int64_t currentBucketStartMs = -1;
    std::string powerMode = "HIGH_STILL";

    void CloseFilesNoLock()
    {
        auto closeOne = [](std::ofstream &f) {
            if (f.is_open()) {
                f.close();
            }
        };
        closeOne(acc);
        closeOne(gyro);
        closeOne(mag);
        closeOne(rv);
        closeOne(baro);
        closeOne(wifi);
        closeOne(ble);
        closeOne(cell);
        closeOne(location);
    }

    bool OpenBucketNoLock(int64_t bucketStartMs)
    {
        if (sessionDir.empty()) {
            return false;
        }
        CloseFilesNoLock();
        const std::string suffix = FormatTimestampForPath(bucketStartMs);
        auto openOne = [&](std::ofstream &f, const std::string &name, const char *header) -> bool {
            f.open(sessionDir + "/" + name + "_" + suffix + ".csv", std::ios::out | std::ios::trunc);
            if (!f.is_open()) {
                return false;
            }
            f << header << "\n";
            return true;
        };
        if (!openOne(acc, "acc_data", "wallTsMs,x,y,z,power_mode") ||
            !openOne(gyro, "gyro_data", "wallTsMs,x,y,z,power_mode") ||
            !openOne(mag, "mag_data", "wallTsMs,x,y,z,power_mode") ||
            !openOne(rv, "rv_data", "wallTsMs,x,y,z,w,power_mode") ||
            !openOne(baro, "baro_data", "wallTsMs,pressure,power_mode") ||
            !openOne(wifi, "wifi_data", "wallTsMs,bssid,ssid,rssi,freq,power_mode") ||
            !openOne(ble, "ble_data", "wallTsMs,mac,name,rssi,power_mode") ||
            !openOne(cell, "cell_data", "wallTsMs,type,cellId,signalIntensity,mcc,mnc,pci,tac,earfcn,power_mode") ||
            !openOne(location, "location_data",
                "wallTsMs,latitude,longitude,accuracy,sourceType,power_mode,coordinate_system")) {
            CloseFilesNoLock();
            return false;
        }
        // Marker so consumers know dump CRS without reading every row.
        std::ofstream crs(sessionDir + "/CRS.txt", std::ios::out | std::ios::trunc);
        if (crs.is_open()) {
            crs << "location_data CRS=" << kCoordinateSystemWgs84 << "\n";
            crs.close();
        }
        currentBucketStartMs = bucketStartMs;
        return true;
    }

    bool RotateIfNeededNoLock(int64_t wallTsMs)
    {
        const int64_t bucket = (wallTsMs / rollIntervalMs) * rollIntervalMs;
        if (currentBucketStartMs == bucket && location.is_open()) {
            return true;
        }
        return OpenBucketNoLock(bucket);
    }

    void FlushAll()
    {
        auto flushOne = [](std::ofstream &f) {
            if (f.is_open()) {
                f.flush();
            }
        };
        flushOne(acc);
        flushOne(gyro);
        flushOne(mag);
        flushOne(rv);
        flushOne(baro);
        flushOne(wifi);
        flushOne(ble);
        flushOne(cell);
        flushOne(location);
    }

    void WorkerLoop()
    {
        for (;;) {
            std::deque<SensorDumpRecord> pending;
            std::deque<TextDumpRecord> pendingText;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this] { return !running || !queue.empty() || !textQueue.empty(); });
                if (!running && queue.empty() && textQueue.empty()) {
                    break;
                }
                pending.swap(queue);
                pendingText.swap(textQueue);
            }
            for (const auto &item : pending) {
                if (!RotateIfNeededNoLock(item.wallTsMs)) {
                    continue;
                }
                if (std::strcmp(item.sensor, "acc") == 0 && acc.is_open()) {
                    acc << item.wallTsMs << "," << item.x << "," << item.y << "," << item.z << "," << item.powerMode
                        << "\n";
                } else if (std::strcmp(item.sensor, "gyro") == 0 && gyro.is_open()) {
                    gyro << item.wallTsMs << "," << item.x << "," << item.y << "," << item.z << "," << item.powerMode
                         << "\n";
                } else if (std::strcmp(item.sensor, "mag") == 0 && mag.is_open()) {
                    mag << item.wallTsMs << "," << item.x << "," << item.y << "," << item.z << "," << item.powerMode
                        << "\n";
                } else if (std::strcmp(item.sensor, "rv") == 0 && rv.is_open()) {
                    rv << item.wallTsMs << "," << item.x << "," << item.y << "," << item.z << "," << item.w << ","
                       << item.powerMode << "\n";
                } else if (std::strcmp(item.sensor, "baro") == 0 && baro.is_open()) {
                    baro << item.wallTsMs << "," << item.x << "," << item.powerMode << "\n";
                } else if (std::strcmp(item.sensor, "loc") == 0 && location.is_open()) {
                    location << item.wallTsMs << "," << std::fixed << std::setprecision(8) << item.x << "," << item.y
                             << "," << std::setprecision(2) << item.z << "," << static_cast<int32_t>(item.w)
                             << std::defaultfloat << "," << item.powerMode << "," << kCoordinateSystemWgs84 << "\n";
                }
            }
            for (const auto &item : pendingText) {
                if (!RotateIfNeededNoLock(item.wallTsMs)) {
                    continue;
                }
                if (std::strcmp(item.sensor, "wifi") == 0 && wifi.is_open()) {
                    wifi << item.line << "," << item.powerMode << "\n";
                } else if (std::strcmp(item.sensor, "ble") == 0 && ble.is_open()) {
                    ble << item.line << "," << item.powerMode << "\n";
                } else if (std::strcmp(item.sensor, "cell") == 0 && cell.is_open()) {
                    cell << item.line << "," << item.powerMode << "\n";
                }
            }
            FlushAll();
        }
    }

    void EnqueueNumeric(const char *sensor, int64_t wallTsMs, double x, double y, double z, double w)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!running) {
            return;
        }
        SensorDumpRecord record;
        (void)std::snprintf(record.sensor, sizeof(record.sensor), "%s", sensor == nullptr ? "" : sensor);
        record.wallTsMs = wallTsMs;
        record.x = x;
        record.y = y;
        record.z = z;
        record.w = w;
        (void)std::snprintf(record.powerMode, sizeof(record.powerMode), "%s", powerMode.c_str());
        queue.push_back(record);
        if (queue.size() > 20000) {
            queue.pop_front();
        }
        cv.notify_one();
    }

    void EnqueueText(const char *sensor, int64_t wallTsMs, const std::string &line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!running) {
            return;
        }
        TextDumpRecord record;
        (void)std::snprintf(record.sensor, sizeof(record.sensor), "%s", sensor == nullptr ? "" : sensor);
        record.wallTsMs = wallTsMs;
        record.line = line;
        (void)std::snprintf(record.powerMode, sizeof(record.powerMode), "%s", powerMode.c_str());
        textQueue.push_back(std::move(record));
        if (textQueue.size() > 20000) {
            textQueue.pop_front();
        }
        cv.notify_one();
    }
};

RollingSensorDump::RollingSensorDump(std::string dumpRoot, int64_t rollIntervalMs) : impl_(new Impl)
{
    impl_->dumpRoot = std::move(dumpRoot);
    impl_->rollIntervalMs = rollIntervalMs > 0 ? rollIntervalMs : kDefaultRollIntervalMs;
}

RollingSensorDump::~RollingSensorDump()
{
    Stop();
    delete impl_;
    impl_ = nullptr;
}

bool RollingSensorDump::Start()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->running) {
        return true;
    }
    if (!EnsureDirectoryExistsRecursive(impl_->dumpRoot)) {
        return false;
    }
    const int64_t nowMs = NowMs();
    const std::string sessionDir = impl_->dumpRoot + "/" + FormatTimestampForPath(nowMs);
    if (!EnsureDirectoryExistsRecursive(sessionDir)) {
        return false;
    }
    impl_->sessionDir = sessionDir;
    impl_->currentBucketStartMs = -1;
    impl_->queue.clear();
    impl_->textQueue.clear();
    impl_->running = true;
    impl_->worker = std::thread([this] { impl_->WorkerLoop(); });
    return true;
}

void RollingSensorDump::Stop()
{
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->running) {
            return;
        }
        impl_->running = false;
    }
    impl_->cv.notify_all();
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->CloseFilesNoLock();
    impl_->queue.clear();
    impl_->textQueue.clear();
    impl_->sessionDir.clear();
    impl_->currentBucketStartMs = -1;
}

bool RollingSensorDump::Running() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->running;
}

const std::string &RollingSensorDump::SessionDir() const
{
    return impl_->sessionDir;
}

void RollingSensorDump::SetPowerMode(std::string mode)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->powerMode = std::move(mode);
}

void RollingSensorDump::EnqueueLocationWgs84(
    TimestampMs wallTsMs, double latitude, double longitude, double accuracyM, int32_t sourceType)
{
    // Intentionally no GCJ conversion — dump stays WGS84.
    impl_->EnqueueNumeric("loc", wallTsMs, latitude, longitude, accuracyM, static_cast<double>(sourceType));
}

void RollingSensorDump::EnqueueImu(const char *sensor, TimestampMs wallTsMs, double x, double y, double z)
{
    impl_->EnqueueNumeric(sensor, wallTsMs, x, y, z, 0.0);
}

void RollingSensorDump::EnqueueRv(TimestampMs wallTsMs, double x, double y, double z, double w)
{
    impl_->EnqueueNumeric("rv", wallTsMs, x, y, z, w);
}

void RollingSensorDump::EnqueueBaro(TimestampMs wallTsMs, double pressure)
{
    impl_->EnqueueNumeric("baro", wallTsMs, pressure, 0.0, 0.0, 0.0);
}

void RollingSensorDump::EnqueueWifiLine(TimestampMs wallTsMs, const std::string &csvLineWithoutPower)
{
    impl_->EnqueueText("wifi", wallTsMs, csvLineWithoutPower);
}

void RollingSensorDump::EnqueueBleLine(TimestampMs wallTsMs, const std::string &csvLineWithoutPower)
{
    impl_->EnqueueText("ble", wallTsMs, csvLineWithoutPower);
}

void RollingSensorDump::EnqueueCellLine(TimestampMs wallTsMs, const std::string &csvLineWithoutPower)
{
    impl_->EnqueueText("cell", wallTsMs, csvLineWithoutPower);
}

}  // namespace commute_sa
