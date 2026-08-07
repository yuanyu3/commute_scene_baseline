#include "commute_sa/sensor_events_writer.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define COMMUTE_SA_MKDIR(path) _mkdir(path)
#else
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

std::string FormatIso8601(TimestampMs ms, bool withMillis)
{
    std::time_t sec = static_cast<std::time_t>(ms / 1000);
    const int millis = static_cast<int>(ms % 1000);
    std::tm tmValue {};
#if defined(_WIN32)
    gmtime_s(&tmValue, &sec);
#else
    gmtime_r(&sec, &tmValue);
#endif
    char buf[64] = {};
    if (withMillis) {
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tmValue.tm_year + 1900,
            tmValue.tm_mon + 1, tmValue.tm_mday, tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec, millis);
    } else {
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tmValue.tm_year + 1900, tmValue.tm_mon + 1,
            tmValue.tm_mday, tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);
    }
    return std::string(buf);
}

std::string EscapeCsv(const std::string &s)
{
    bool needQuotes = false;
    for (char c : s) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needQuotes = true;
            break;
        }
    }
    if (!needQuotes) {
        return s;
    }
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

std::string BuildGpsPayload(const RawGpsLocation &loc)
{
    std::ostringstream payload;
    payload << std::setprecision(15) << "{\"latitude\":" << loc.latitude << ",\"longitude\":" << loc.longitude
            << ",\"horizontal_accuracy_m\":";
    if (loc.has_horizontal_accuracy) {
        payload << loc.horizontal_accuracy_m;
    } else {
        payload << "null";
    }
    payload << ",\"valid\":" << (loc.valid ? "true" : "false") << ",\"coordinate_system\":\""
            << kCoordinateSystemWgs84 << "\",\"source_type\":" << loc.source_type << "}";
    return payload.str();
}

}  // namespace

SensorEventsWriter::SensorEventsWriter(std::string outputRoot) : outputRoot_(std::move(outputRoot)) {}

SensorEventsWriter::~SensorEventsWriter()
{
    Stop();
}

bool SensorEventsWriter::Start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return true;
    }
    if (!InitOutputsUnlocked()) {
        return false;
    }
    running_ = true;
    return true;
}

void SensorEventsWriter::Stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        return;
    }
    CloseOutputsUnlocked();
    running_ = false;
}

bool SensorEventsWriter::Running() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

const std::string &SensorEventsWriter::RunDir() const
{
    return runDir_;
}

void SensorEventsWriter::SetPowerMode(std::string mode)
{
    std::lock_guard<std::mutex> lock(mutex_);
    powerMode_ = std::move(mode);
}

bool SensorEventsWriter::InitOutputsUnlocked()
{
    CloseOutputsUnlocked();
    if (!EnsureDirectoryExistsRecursive(outputRoot_)) {
        return false;
    }
    const std::string baseName = FormatTimestampForPath(NowMs());
    runDir_ = outputRoot_ + "/" + baseName;
    if (!EnsureDirectoryExistsRecursive(runDir_)) {
        return false;
    }
    const std::string path = runDir_ + "/sensor_events.csv";
    sensorEventsFile_.open(path, std::ios::out | std::ios::trunc);
    if (!sensorEventsFile_.is_open()) {
        return false;
    }
    sensorEventsFile_
        << "sequence_id,received_at,source_observed_at,event_type,motion_state,episode_id,payload_json,power_mode\n";
    sensorEventsFile_.flush();
    sensorEventsEnabled_ = sensorEventsFile_.good();
    std::ofstream crs(runDir_ + "/CRS.txt", std::ios::out | std::ios::trunc);
    if (crs.is_open()) {
        crs << "sensor_events GPS CRS=" << kCoordinateSystemWgs84 << "\n";
    }
    return sensorEventsEnabled_;
}

void SensorEventsWriter::CloseOutputsUnlocked()
{
    if (sensorEventsFile_.is_open()) {
        sensorEventsFile_.flush();
        sensorEventsFile_.close();
    }
    sensorEventsEnabled_ = false;
}

void SensorEventsWriter::WriteEventsUnlocked(const std::vector<SensorDebugEvent> &events)
{
    if (!sensorEventsEnabled_ || !sensorEventsFile_.is_open()) {
        return;
    }
    for (const auto &e : events) {
        sensorEventsFile_ << e.sequence_id << "," << EscapeCsv(FormatIso8601(e.received_at, true)) << ","
                          << EscapeCsv(FormatIso8601(e.source_observed_at, true)) << ","
                          << EscapeCsv(SensorEventTypeToString(e.event_type)) << ","
                          << EscapeCsv(MotionStateToString(e.motion_state)) << "," << EscapeCsv(e.episode_id) << ","
                          << EscapeCsv(e.payload_json) << "," << EscapeCsv(e.power_mode) << "\n";
    }
    sensorEventsFile_.flush();
}

void SensorEventsWriter::AppendEvent(SensorDebugEvent event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        return;
    }
    event.sequence_id = ++sequence_;
    if (event.power_mode.empty()) {
        event.power_mode = powerMode_;
    }
    WriteEventsUnlocked({event});
}

void SensorEventsWriter::OnGpsLocation(const RawGpsLocation &location)
{
    RawGpsLocation loc = location;
    const TimestampMs receivedAt = NowMs();
    if (loc.received_at <= 0) {
        loc.received_at = receivedAt;
    }
    if (loc.observed_at <= 0) {
        loc.observed_at = receivedAt;
    }

    SensorDebugEvent dbg;
    dbg.received_at = loc.received_at;
    dbg.source_observed_at = loc.observed_at;
    dbg.event_type = SensorEventType::kGpsReport;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dbg.motion_state = motionState_;
        dbg.power_mode = powerMode_;
    }
    dbg.payload_json = BuildGpsPayload(loc);
    AppendEvent(std::move(dbg));
}

void SensorEventsWriter::OnWalkingStarted(TimestampMs timestampMs)
{
    const TimestampMs receivedAt = NowMs();
    const TimestampMs eventTs = (timestampMs > 0) ? timestampMs : receivedAt;
    SensorDebugEvent dbg;
    dbg.received_at = receivedAt;
    dbg.source_observed_at = eventTs;
    dbg.event_type = SensorEventType::kWalkingStarted;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        motionState_ = MotionState::kWalking;
        dbg.motion_state = motionState_;
        dbg.power_mode = powerMode_;
    }
    dbg.payload_json = "{\"timestamp_ms\":" + std::to_string(eventTs) + "}";
    AppendEvent(std::move(dbg));
}

void SensorEventsWriter::OnWalkingStopped(TimestampMs timestampMs)
{
    const TimestampMs receivedAt = NowMs();
    const TimestampMs eventTs = (timestampMs > 0) ? timestampMs : receivedAt;
    SensorDebugEvent dbg;
    dbg.received_at = receivedAt;
    dbg.source_observed_at = eventTs;
    dbg.event_type = SensorEventType::kWalkingStopped;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        motionState_ = MotionState::kNotWalking;
        dbg.motion_state = motionState_;
        dbg.power_mode = powerMode_;
    }
    dbg.payload_json = "{\"timestamp_ms\":" + std::to_string(eventTs) + "}";
    AppendEvent(std::move(dbg));
}

void SensorEventsWriter::Flush()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (sensorEventsFile_.is_open()) {
        sensorEventsFile_.flush();
    }
}

}  // namespace commute_sa
