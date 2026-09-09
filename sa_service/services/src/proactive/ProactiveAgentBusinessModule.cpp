/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Proactive Agent business module — perception tick, normalize, debug CSV.
 */

#include "ProactiveAgentBusinessModule.h"
#include "PowerModeController.h"

#ifndef PROACTIVE_AGENT_HOST_TEST
#include "Agent.h"
#include "ResourceManager.h"
#include "sa_agent/LeavingHomeBaselineAgentConfig.h"
#include "sa_agent/LeavingHomeContextEngine.h"
#include "sa_agent/EvidenceTools.h"
#include "sa_agent/ActionTools.h"
#include "commute_sa/baseline_runtime.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/anchor_reestimate.h"
#include "commute_sa/personalization.h"
#include "commute_sa/product_store.h"
#endif

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#ifdef PROACTIVE_AGENT_HOST_TEST
#include "host_log_stub.h"
#else
#include "camera_agent_log.h"
#endif

namespace OHOS::Multimedia::CameraAgentService {
namespace {

constexpr size_t kPdrPointsReserveHint = 600;
constexpr int64_t kTickIntervalMs = static_cast<int64_t>(kDebugTickIntervalSeconds) * 1000;

int64_t NowWallClockMs()
{
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

bool EnsureDirectoryExists(const char *path)
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    struct stat st {};
    if (stat(path, &st) == 0) {
#ifdef _WIN32
        return (st.st_mode & _S_IFDIR) != 0;
#else
        return S_ISDIR(st.st_mode) != 0;
#endif
    }
#ifdef _WIN32
    if (_mkdir(path) == 0) {
        return true;
    }
#else
    if (mkdir(path, 0755) == 0) {
        return true;
    }
#endif
    if (stat(path, &st) == 0) {
#ifdef _WIN32
        return (st.st_mode & _S_IFDIR) != 0;
#else
        return S_ISDIR(st.st_mode) != 0;
#endif
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
        char c = path[i];
        current.push_back(c);
        const bool sep = (c == '/' || c == '\\');
        if (sep && current.size() > 1) {
            // Skip drive prefix like "C:\"
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

std::string FormatIso8601(Timestamp ms, bool withMillis)
{
    if (ms <= 0) {
        return "";
    }
    const std::time_t sec = static_cast<std::time_t>(ms / 1000);
    const int millis = static_cast<int>(ms % 1000);
    std::tm tmValue {};
#if defined(_WIN32)
    localtime_s(&tmValue, &sec);
#else
    localtime_r(&sec, &tmValue);
#endif
    char buf[64] = {};
    // Fixed +08:00 for Asia/Shanghai MVP (device TZ may differ; documented).
    if (withMillis) {
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d+08:00",
            tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday,
            tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec, millis);
    } else {
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d+08:00",
            tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday,
            tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);
    }
    return std::string(buf);
}

std::string FormatCompactLocal(Timestamp ms)
{
    if (ms <= 0) {
        return "00000000-000000";
    }
    const std::time_t sec = static_cast<std::time_t>(ms / 1000);
    std::tm tmValue {};
#if defined(_WIN32)
    localtime_s(&tmValue, &sec);
#else
    localtime_r(&sec, &tmValue);
#endif
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
        tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday,
        tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);
    return std::string(buf);
}

std::string EscapeJson(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string EscapeCsv(const std::string &s)
{
    bool needQuote = false;
    for (char c : s) {
        if (c == '"' || c == ',' || c == '\n' || c == '\r') {
            needQuote = true;
            break;
        }
    }
    if (!needQuote) {
        return s;
    }
    std::string out;
    out.push_back('"');
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

std::string MotionStateToString(MotionState s)
{
    switch (s) {
        case MotionState::kWalking: return "WALKING";
        case MotionState::kNotWalking: return "NOT_WALKING";
        case MotionState::kUnknown:
        default: return "NOT_WALKING";
    }
}

std::string DebugEventTypeToString(SensorDebugEventType t)
{
    switch (t) {
        case SensorDebugEventType::kWalkingStarted: return "WALKING_STARTED";
        case SensorDebugEventType::kWalkingStopped: return "WALKING_STOPPED";
        case SensorDebugEventType::kPdrPoint: return "PDR_POINT";
        case SensorDebugEventType::kGpsReport: return "GPS_REPORT";
        default: return "UNKNOWN";
    }
}

std::string EpisodeStateToString(PdrEpisodeState s)
{
    return s == PdrEpisodeState::kEnded ? "ENDED" : "ACTIVE";
}

double HaversineM(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double r = 6371000.0;
    constexpr double kPi = 3.14159265358979323846;
    const double p1 = lat1 * kPi / 180.0;
    const double p2 = lat2 * kPi / 180.0;
    const double dlat = (lat2 - lat1) * kPi / 180.0;
    const double dlon = (lon2 - lon1) * kPi / 180.0;
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
        std::cos(p1) * std::cos(p2) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2 * r * std::asin(std::sqrt(a));
}

double PointDistM(double x0, double y0, double x1, double y1)
{
    return std::hypot(x1 - x0, y1 - y0);
}

bool InOpenClosedWindow(Timestamp t, Timestamp start, Timestamp end)
{
    return t > start && t <= end;
}

bool EpisodeIntersectsWindow(const PdrEpisode &ep, Timestamp start, Timestamp end)
{
    if (ep.started_at > end) {
        return false;
    }
    if (ep.has_ended_at && ep.ended_at <= start) {
        return false;
    }
    return true;
}

PdrEpisodeInput ToEpisodeInput(const PdrEpisode &ep)
{
    PdrEpisodeInput in;
    in.episode_id = ep.episode_id;
    in.state_at_window_end = ep.state;
    in.started_at = ep.started_at;
    in.has_ended_at = ep.has_ended_at;
    in.ended_at = ep.ended_at;
    in.points.reserve(ep.points.size());
    for (size_t i = 0; i < ep.points.size(); ++i) {
        TickPdrPoint tp;
        tp.sequence = static_cast<uint64_t>(i + 1);
        tp.observed_at = ep.points[i].observed_at;
        // TODO: PDR x/y units not confirmed in library headers; treat as local meters.
        tp.x_m = ep.points[i].x;
        tp.y_m = ep.points[i].y;
        in.points.push_back(tp);
    }
    return in;
}

std::string JsonNullOrNumber(bool has, double v)
{
    if (!has || !std::isfinite(v)) {
        return "null";
    }
    std::ostringstream oss;
    oss << std::setprecision(15) << v;
    return oss.str();
}

std::string JsonNullOrString(bool has, const std::string &s)
{
    if (!has) {
        return "null";
    }
    return "\"" + EscapeJson(s) + "\"";
}

std::string JsonNullOrIso(bool has, Timestamp ms, bool withMillis)
{
    if (!has || ms <= 0) {
        return "null";
    }
    return "\"" + EscapeJson(FormatIso8601(ms, withMillis)) + "\"";
}

std::string BuildTickJson(const SaPerceptionTick &t)
{
    std::ostringstream oss;
    oss << std::setprecision(15);
    oss << "{"
        << "\"schema_version\":\"" << EscapeJson(t.schema_version) << "\","
        << "\"tick_id\":\"" << EscapeJson(t.tick_id) << "\","
        << "\"observed_at\":\"" << EscapeJson(FormatIso8601(t.observed_at, false)) << "\","
        << "\"timezone\":\"" << EscapeJson(t.timezone) << "\","
        << "\"observation_window\":{"
        << "\"started_at\":\"" << EscapeJson(FormatIso8601(t.observation_window.started_at, false)) << "\","
        << "\"ended_at\":\"" << EscapeJson(FormatIso8601(t.observation_window.ended_at, false)) << "\","
        << "\"duration_s\":" << t.observation_window.duration_s
        << "},"
        << "\"motion\":{"
        << "\"state_at_window_start\":\"" << EscapeJson(MotionStateToString(t.motion.state_at_window_start)) << "\","
        << "\"state_at_window_end\":\"" << EscapeJson(MotionStateToString(t.motion.state_at_window_end)) << "\","
        << "\"events\":[";
    for (size_t i = 0; i < t.motion.events.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        const auto &e = t.motion.events[i];
        const char *etype = (e.type == MotionEventType::kWalkingStarted) ? "WALKING_STARTED" : "WALKING_STOPPED";
        oss << "{\"type\":\"" << etype << "\",\"observed_at\":\""
            << EscapeJson(FormatIso8601(e.timestamp, true)) << "\"}";
    }
    oss << "]},";
    if (!t.has_gps) {
        oss << "\"gps\":null,";
    } else {
        oss << "\"gps\":{"
            << "\"observed_at\":\"" << EscapeJson(FormatIso8601(t.gps.observed_at, true)) << "\","
            << "\"received_at\":\"" << EscapeJson(FormatIso8601(t.gps.received_at, true)) << "\","
            << "\"latitude\":" << t.gps.latitude << ","
            << "\"longitude\":" << t.gps.longitude << ",";
        if (t.gps.has_horizontal_accuracy) {
            oss << "\"horizontal_accuracy_m\":" << t.gps.horizontal_accuracy_m << ",";
        } else {
            oss << "\"horizontal_accuracy_m\":null,";
        }
        oss << "\"valid\":" << (t.gps.valid ? "true" : "false") << ","
            << "\"coordinate_system\":\"" << EscapeJson(kGpsCoordinateSystem) << "\"},";
    }
    oss << "\"pdr_episodes\":[";
    for (size_t ei = 0; ei < t.pdr_episodes.size(); ++ei) {
        if (ei > 0) {
            oss << ",";
        }
        const auto &ep = t.pdr_episodes[ei];
        oss << "{"
            << "\"episode_id\":\"" << EscapeJson(ep.episode_id) << "\","
            << "\"state_at_window_end\":\"" << EscapeJson(EpisodeStateToString(ep.state_at_window_end)) << "\","
            << "\"started_at\":\"" << EscapeJson(FormatIso8601(ep.started_at, true)) << "\","
            << "\"ended_at\":" << JsonNullOrIso(ep.has_ended_at, ep.ended_at, true) << ","
            << "\"points\":[";
        for (size_t pi = 0; pi < ep.points.size(); ++pi) {
            if (pi > 0) {
                oss << ",";
            }
            const auto &p = ep.points[pi];
            oss << "{\"sequence\":" << p.sequence
                << ",\"observed_at\":\"" << EscapeJson(FormatIso8601(p.observed_at, true)) << "\","
                << "\"x_m\":" << p.x_m << ",\"y_m\":" << p.y_m << "}";
        }
        oss << "]}";
    }
    oss << "],"
        << "\"collection_quality\":{"
        << "\"dropped_pdr_points\":" << t.collection_quality.dropped_pdr_points << ","
        << "\"out_of_order_pdr_points\":" << t.collection_quality.out_of_order_pdr_points << ","
        << "\"duplicate_motion_events\":" << t.collection_quality.duplicate_motion_events
        << "},"
        << "\"power_state\":{"
        << "\"mode\":\"" << EscapeJson(t.power_state.mode) << "\","
        << "\"agent_inference_enabled\":" << (t.power_state.agent_inference_enabled ? "true" : "false")
        << "}}";
    return oss.str();
}

std::string BuildSnapshotJson(const SemanticSnapshot &s)
{
    std::ostringstream oss;
    oss << std::setprecision(15);
    oss << "{"
        << "\"schema_version\":\"" << EscapeJson(s.schema_version) << "\","
        << "\"snapshot_id\":\"" << EscapeJson(s.snapshot_id) << "\","
        << "\"tick_id\":\"" << EscapeJson(s.tick_id) << "\","
        << "\"observed_at\":\"" << EscapeJson(FormatIso8601(s.observed_at, false)) << "\","
        << "\"timezone\":\"" << EscapeJson(s.timezone) << "\","
        << "\"observation_window\":{"
        << "\"started_at\":\"" << EscapeJson(FormatIso8601(s.observation_window.started_at, false)) << "\","
        << "\"ended_at\":\"" << EscapeJson(FormatIso8601(s.observation_window.ended_at, false)) << "\","
        << "\"duration_s\":" << s.observation_window.duration_s
        << "},"
        << "\"motion\":{"
        << "\"fact_id\":\"" << EscapeJson(s.motion.fact_id) << "\","
        << "\"state\":\"" << EscapeJson(s.motion.state) << "\","
        << "\"transition\":\"" << EscapeJson(s.motion.transition) << "\"},"
        << "\"home_relation\":{"
        << "\"fact_id\":\"" << EscapeJson(s.home_relation.fact_id) << "\","
        << "\"anchor_id\":\"" << EscapeJson(s.home_relation.anchor_id) << "\","
        << "\"distance_m\":" << JsonNullOrNumber(s.home_relation.has_distance_m, s.home_relation.distance_m) << ","
        << "\"gps_accuracy_m\":"
        << JsonNullOrNumber(s.home_relation.has_gps_accuracy_m, s.home_relation.gps_accuracy_m) << ","
        << "\"gps_observed_at\":"
        << JsonNullOrIso(s.home_relation.has_gps_observed_at, s.home_relation.gps_observed_at, true) << ","
        << "\"gps_age_s\":" << JsonNullOrNumber(s.home_relation.has_gps_age_s, s.home_relation.gps_age_s) << ","
        << "\"relation\":\"" << EscapeJson(s.home_relation.relation) << "\","
        << "\"quality\":\"" << EscapeJson(s.home_relation.quality) << "\"},"
        << "\"pdr\":{"
        << "\"fact_id\":\"" << EscapeJson(s.pdr.fact_id) << "\","
        << "\"episode_id\":" << JsonNullOrString(s.pdr.has_episode_id, s.pdr.episode_id) << ","
        << "\"transition\":\"" << EscapeJson(s.pdr.transition) << "\","
        << "\"active_at_window_start\":" << (s.pdr.active_at_window_start ? "true" : "false") << ","
        << "\"active_at_window_end\":" << (s.pdr.active_at_window_end ? "true" : "false") << ","
        << "\"started_at\":" << JsonNullOrIso(s.pdr.has_started_at, s.pdr.started_at, true) << ","
        << "\"ended_at\":" << JsonNullOrIso(s.pdr.has_ended_at, s.pdr.ended_at, true) << ","
        << "\"window\":{"
        << "\"walking_duration_s\":" << s.pdr.window.walking_duration_s << ","
        << "\"path_length_m\":" << s.pdr.window.path_length_m << ","
        << "\"point_count\":" << s.pdr.window.point_count << "},"
        << "\"cumulative\":{"
        << "\"walking_duration_s\":"
        << JsonNullOrNumber(s.pdr.cumulative.has_walking_duration_s, s.pdr.cumulative.walking_duration_s) << ","
        << "\"path_length_m\":"
        << JsonNullOrNumber(s.pdr.cumulative.has_path_length_m, s.pdr.cumulative.path_length_m) << ","
        << "\"net_displacement_m\":"
        << JsonNullOrNumber(s.pdr.cumulative.has_net_displacement_m, s.pdr.cumulative.net_displacement_m) << ","
        << "\"straightness_ratio\":"
        << JsonNullOrNumber(s.pdr.cumulative.has_straightness_ratio, s.pdr.cumulative.straightness_ratio) << ","
        << "\"point_count\":" << s.pdr.cumulative.point_count << "},"
        << "\"quality\":\"" << EscapeJson(s.pdr.quality) << "\"},"
        << "\"data_quality\":{"
        << "\"fact_id\":\"" << EscapeJson(s.data_quality.fact_id) << "\","
        << "\"overall\":\"" << EscapeJson(s.data_quality.overall) << "\","
        << "\"missing_fields\":[";
    for (size_t i = 0; i < s.data_quality.missing_fields.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << "\"" << EscapeJson(s.data_quality.missing_fields[i]) << "\"";
    }
    oss << "],\"issues\":[";
    for (size_t i = 0; i < s.data_quality.issues.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << "\"" << EscapeJson(s.data_quality.issues[i]) << "\"";
    }
    oss << "]},"
        << "\"power_state\":{"
        << "\"mode\":\"" << EscapeJson(s.power_state.mode) << "\","
        << "\"agent_inference_enabled\":" << (s.power_state.agent_inference_enabled ? "true" : "false")
        << "}}";
    return oss.str();
}

struct FrozenTickData {
    Timestamp windowStart = 0;
    Timestamp windowEnd = 0;
    MotionState stateAtStart = MotionState::kUnknown;
    MotionState stateAtEnd = MotionState::kUnknown;
    std::vector<MotionEvent> motionEvents;
    std::vector<PdrEpisode> episodes;
    bool hasGps = false;
    RawGpsLocation gps;
    CollectionQuality quality;
    std::vector<SensorDebugEvent> debugEvents;
    uint64_t tickSeq = 0;
};

void ComputePathMetrics(const std::vector<TickPdrPoint> &points, Timestamp rangeStart, Timestamp rangeEnd,
    bool useOpenStart, double *pathLength, double *netDisplacement, int64_t *pointCount, bool *finiteOk)
{
    *pathLength = 0.0;
    *netDisplacement = 0.0;
    *pointCount = 0;
    *finiteOk = true;

    std::vector<const TickPdrPoint *> selected;
    selected.reserve(points.size());
    for (const auto &p : points) {
        if (!std::isfinite(p.x_m) || !std::isfinite(p.y_m)) {
            *finiteOk = false;
            continue;
        }
        bool inRange = false;
        if (useOpenStart) {
            inRange = (p.observed_at > rangeStart && p.observed_at <= rangeEnd);
        } else {
            inRange = (p.observed_at >= rangeStart && p.observed_at <= rangeEnd);
        }
        if (inRange) {
            selected.push_back(&p);
            ++(*pointCount);
        }
    }

    // Window path: include last point at or before rangeStart as anchor.
    std::vector<const TickPdrPoint *> pathPts;
    if (useOpenStart) {
        const TickPdrPoint *anchor = nullptr;
        for (const auto &p : points) {
            if (!std::isfinite(p.x_m) || !std::isfinite(p.y_m)) {
                continue;
            }
            if (p.observed_at <= rangeStart) {
                anchor = &p;
            }
        }
        if (anchor != nullptr) {
            pathPts.push_back(anchor);
        }
        for (const auto *p : selected) {
            pathPts.push_back(p);
        }
    } else {
        pathPts = selected;
    }

    for (size_t i = 1; i < pathPts.size(); ++i) {
        const double d = PointDistM(pathPts[i - 1]->x_m, pathPts[i - 1]->y_m, pathPts[i]->x_m, pathPts[i]->y_m);
        if (!std::isfinite(d) || d < 0.0) {
            *finiteOk = false;
            continue;
        }
        *pathLength += d;
    }

    if (selected.size() >= 2) {
        *netDisplacement = PointDistM(selected.front()->x_m, selected.front()->y_m,
            selected.back()->x_m, selected.back()->y_m);
        if (!std::isfinite(*netDisplacement)) {
            *finiteOk = false;
            *netDisplacement = 0.0;
        }
    }
}

} // namespace

ProactiveAgentBusinessModule &ProactiveAgentBusinessModule::GetInstance()
{
    static ProactiveAgentBusinessModule instance;
    return instance;
}

ProactiveAgentBusinessModule::~ProactiveAgentBusinessModule()
{
    Shutdown();
}

void ProactiveAgentBusinessModule::ResetStateLocked()
{
    currentMotionState_ = MotionState::kUnknown;
    motionStateAtWindowStart_ = MotionState::kUnknown;
    motionEvents_.clear();
    hasActiveEpisode_ = false;
    activeEpisode_ = PdrEpisode {};
    completedEpisodes_.clear();
    hasLatestGps_ = false;
    latestGps_ = RawGpsLocation {};
    windowDroppedPdr_ = 0;
    windowOutOfOrderPdr_ = 0;
    windowDuplicateMotion_ = 0;
    ignoredPdrWithoutEpisode_ = 0;
    discardedOutOfOrderPdr_ = 0;
    pendingDebugEvents_.clear();
    windowStartMs_ = 0;
    hasLastTick_ = false;
    hasLastSnapshot_ = false;
    lastTick_ = SaPerceptionTick {};
    lastSnapshot_ = SemanticSnapshot {};
}

bool ProactiveAgentBusinessModule::AcceptingInputLocked() const
{
    return initialized_ && acceptingInput_;
}

std::string ProactiveAgentBusinessModule::MakeEpisodeIdLocked(Timestamp startedAt)
{
    ++episodeSequence_;
    std::ostringstream oss;
    oss << "walk-" << FormatCompactLocal(startedAt) << '-' << std::setw(3) << std::setfill('0') << episodeSequence_;
    return oss.str();
}

void ProactiveAgentBusinessModule::AppendDebugEventLocked(SensorDebugEvent event)
{
    if (event.power_mode.empty()) {
        event.power_mode = PowerModeController::ModeToString(
            PowerModeController::GetInstance().GetSnapshot().mode);
    }
    if (pendingDebugEvents_.size() >= kDebugEventQueueCap) {
        // Prefer dropping PDR-like overflow without blocking callbacks.
        ++windowDroppedPdr_;
        return;
    }
    pendingDebugEvents_.push_back(std::move(event));
}

bool ProactiveAgentBusinessModule::InitDebugOutputsLocked()
{
    CloseDebugOutputsLocked();
    debugSinksConfig_ = false;
    sensorEventsEnabled_ = false;
    ticksEnabled_ = false;
    snapshotsEnabled_ = false;
    agentResponsesEnabled_ = false;
    baselineDecisionsEnabled_ = false;

#ifndef PROACTIVE_AGENT_HOST_TEST
    if (!commute_sa::ProductStore::GetInstance().Init(kProductRoot)) {
        CAMERA_AGENT_LOG_ERROR("ProductStore init failed under %{public}s", kProductRoot);
    }
    commute_sa::EvidenceQuery::GetInstance().SetRootDir(kProductRoot);
#endif
    debugRunDir_ = kProductRoot;
    EnsureDirectoryExistsRecursive(kProductRoot);

    const AgentRuntimeEnvironment sinkCfg = LoadAgentDebugSinkDefaultsFromFile(ResolveAgentEnvFilePath());
    debugSinksConfig_ = sinkCfg.debugSinks;
    if (!debugSinksConfig_) {
        CAMERA_AGENT_LOG_INFO(
            "ProactiveAgent minimal sinks under %{public}s "
            "(anchors/theta/leave_episodes/leave_window_samples/param_changes/personalize_jobs)",
            debugRunDir_.c_str());
        return true;
    }

    // Optional verbose helloworld-style dumps under sa_sensor_test/<run>/.
    const int64_t nowMs = NowWallClockMs();
    const std::string baseName = FormatTimestampForPath(nowMs);
    if (!EnsureDirectoryExistsRecursive(kDebugOutputRoot)) {
        CAMERA_AGENT_LOG_ERROR("ProactiveAgent debug root create failed: %{public}s", kDebugOutputRoot);
        return true;
    }
    auto pathExists = [](const std::string &p) -> bool {
        struct stat st {};
        return stat(p.c_str(), &st) == 0;
    };
    std::string runDir = std::string(kDebugOutputRoot) + "/" + baseName;
    if (pathExists(runDir)) {
        for (int i = 1; i < 1000; ++i) {
            char suffix[16];
            std::snprintf(suffix, sizeof(suffix), "_%03d", i);
            const std::string candidate = std::string(kDebugOutputRoot) + "/" + baseName + suffix;
            if (!pathExists(candidate)) {
                runDir = candidate;
                break;
            }
        }
    }
    if (!EnsureDirectoryExistsRecursive(runDir)) {
        CAMERA_AGENT_LOG_ERROR("ProactiveAgent debug run dir create failed: %{public}s", runDir.c_str());
        return true;
    }
    debugRunDir_ = runDir;

    auto openCsv = [&](std::ofstream &f, const char *name, bool *enabled, bool *headerWritten,
                        const char *header) {
        const std::string path = runDir + "/" + name;
        f.open(path, std::ios::out | std::ios::trunc);
        if (!f.is_open()) {
            CAMERA_AGENT_LOG_ERROR("ProactiveAgent open CSV failed: %{public}s", path.c_str());
            *enabled = false;
            return;
        }
        f << header << "\n";
        f.flush();
        if (!f.good()) {
            CAMERA_AGENT_LOG_ERROR("ProactiveAgent write CSV header failed: %{public}s", path.c_str());
            f.close();
            *enabled = false;
            return;
        }
        *enabled = true;
        *headerWritten = true;
    };

    openCsv(sensorEventsFile_, "sensor_events.csv", &sensorEventsEnabled_, &headerSensorWritten_,
        "sequence_id,received_at,source_observed_at,event_type,motion_state,episode_id,payload_json,power_mode");
    openCsv(ticksFile_, "sa_perception_ticks.csv", &ticksEnabled_, &headerTicksWritten_,
        "tick_id,window_started_at,window_ended_at,motion_state_at_start,motion_state_at_end,"
        "motion_event_count,pdr_episode_count,gps_observed_at,power_mode,sa_input_json");
    openCsv(snapshotsFile_, "semantic_snapshots.csv", &snapshotsEnabled_, &headerSnapshotsWritten_,
        "snapshot_id,tick_id,observed_at,motion_state,motion_transition,home_distance_m,home_relation,"
        "home_quality,pdr_episode_id,pdr_transition,pdr_window_walking_duration_s,pdr_window_path_length_m,"
        "pdr_cumulative_walking_duration_s,pdr_cumulative_path_length_m,pdr_net_displacement_m,"
        "pdr_straightness_ratio,pdr_quality,overall_quality,power_mode,semantic_snapshot_json");
    openCsv(agentResponsesFile_, "agent_responses.csv", &agentResponsesEnabled_, &headerAgentResponsesWritten_,
        "tick_id,request_id,session_id,invoked_at,status,error_code,response_message,stream_payload,power_mode");
    openCsv(baselineDecisionsFile_, "baseline_decisions.csv", &baselineDecisionsEnabled_, &headerBaselineWritten_,
        "tick_id,observed_at,scene,p_leaving_home,p_leaving_company,hsmm_phase_home,hsmm_phase_company,"
        "p_preleave_home,p_preleave_company,p_outside_home,p_outside_company,home_relation,company_relation,"
        "dist_home_m,dist_company_m,should_service,service_intent,uncertainty,hits_home,hits_company");

    CAMERA_AGENT_LOG_INFO("ProactiveAgent DEBUG_SINKS=1 under %{public}s", debugRunDir_.c_str());
    return true;
}

void ProactiveAgentBusinessModule::CloseDebugOutputsLocked()
{
    if (sensorEventsFile_.is_open()) {
        sensorEventsFile_.flush();
        sensorEventsFile_.close();
    }
    if (ticksFile_.is_open()) {
        ticksFile_.flush();
        ticksFile_.close();
    }
    if (snapshotsFile_.is_open()) {
        snapshotsFile_.flush();
        snapshotsFile_.close();
    }
    if (agentResponsesFile_.is_open()) {
        agentResponsesFile_.flush();
        agentResponsesFile_.close();
    }
    if (baselineDecisionsFile_.is_open()) {
        baselineDecisionsFile_.flush();
        baselineDecisionsFile_.close();
    }
    sensorEventsEnabled_ = false;
    ticksEnabled_ = false;
    snapshotsEnabled_ = false;
    agentResponsesEnabled_ = false;
    baselineDecisionsEnabled_ = false;
    headerSensorWritten_ = false;
    headerTicksWritten_ = false;
    headerSnapshotsWritten_ = false;
    headerAgentResponsesWritten_ = false;
    headerBaselineWritten_ = false;
}

void ProactiveAgentBusinessModule::Initialize()
{
    StopPeriodicScheduler();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ResetStateLocked();
        tickSequence_ = 0;
        debugEventSequence_ = 0;
        windowStartMs_ = NowWallClockMs();
        motionStateAtWindowStart_ = MotionState::kUnknown;
        initialized_ = true;
        acceptingInput_ = true;
        perceptionPaused_ = false;
        agentInferenceEnabled_ = true;
        tickIntervalMs_.store(kTickIntervalMs);
        InitDebugOutputsLocked();
        CAMERA_AGENT_LOG_INFO("ProactiveAgentBusinessModule initialized");
    }
#ifndef PROACTIVE_AGENT_HOST_TEST
    // C++ SceneEngine (no Python). Device files optional → built-in defaults.
    const std::string anchorsPath = std::string(kProductRoot) + "/anchors.json";
    const std::string thetaPath = std::string(kProductRoot) + "/theta.json";
    commute_sa::BaselineRuntime::GetInstance().Init(anchorsPath, thetaPath);
    commute_sa::PersonalizationController::GetInstance().SetEnabled(true);
    if (commute_sa::BaselineRuntime::GetInstance().Engine() != nullptr) {
        auto *eng = commute_sa::BaselineRuntime::GetInstance().Engine();
        // Ensure product files exist for next boot / θ personalize.
        commute_sa::ProductStore::GetInstance().SaveAnchors(eng->GetAnchors());
        commute_sa::ProductStore::GetInstance().SaveTheta(eng->GetTheta());
    }
#endif
    TryInitializeAgent();
#ifndef PROACTIVE_AGENT_HOST_TEST
    StartPeriodicScheduler();
#endif
}

void ProactiveAgentBusinessModule::Shutdown()
{
    StopPeriodicScheduler();
    std::vector<SensorDebugEvent> leftover;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        acceptingInput_ = false;
        perceptionPaused_ = false;
        leftover.swap(pendingDebugEvents_);
        FlushDebugEventsUnlocked(leftover);
        CloseDebugOutputsLocked();
        ResetStateLocked();
        initialized_ = false;
        CAMERA_AGENT_LOG_INFO("ProactiveAgentBusinessModule shutdown");
    }
    ShutdownAgent();
}

void ProactiveAgentBusinessModule::PausePerception()
{
    StopPeriodicScheduler();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }
    acceptingInput_ = false;
    perceptionPaused_ = true;
    CAMERA_AGENT_LOG_INFO("ProactiveAgentBusinessModule perception paused (sleep)");
}

void ProactiveAgentBusinessModule::ResumePerception()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return;
        }
        acceptingInput_ = true;
        perceptionPaused_ = false;
        windowStartMs_ = NowWallClockMs();
        motionStateAtWindowStart_ = currentMotionState_;
        CAMERA_AGENT_LOG_INFO("ProactiveAgentBusinessModule perception resumed (awake)");
    }
#ifndef PROACTIVE_AGENT_HOST_TEST
    StartPeriodicScheduler();
#endif
}

bool ProactiveAgentBusinessModule::IsPerceptionPaused() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return perceptionPaused_;
}

void ProactiveAgentBusinessModule::ApplyPowerPolicy(int64_t tickIntervalMs, bool agentInferenceEnabled)
{
    if (tickIntervalMs < 1000) {
        tickIntervalMs = 1000;
    }
    tickIntervalMs_.store(tickIntervalMs);
    bool shouldRestartScheduler = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        agentInferenceEnabled_ = agentInferenceEnabled;
        if (!initialized_) {
            return;
        }
        acceptingInput_ = true;
        perceptionPaused_ = false;
        shouldRestartScheduler = true;
    }
    CAMERA_AGENT_LOG_INFO("ProactiveAgent ApplyPowerPolicy tickMs=%{public}" PRId64 " agent=%{public}d",
        tickIntervalMs, agentInferenceEnabled ? 1 : 0);
#ifndef PROACTIVE_AGENT_HOST_TEST
    if (shouldRestartScheduler) {
        StopPeriodicScheduler();
        StartPeriodicScheduler();
    }
#endif
}

void ProactiveAgentBusinessModule::FlushDebugEventsUnlocked(std::vector<SensorDebugEvent> events)
{
    // Caller holds mutex_ OR has exclusive ownership after swap; write without nested lock.
    WriteSensorEventsCsv(events);
}

void ProactiveAgentBusinessModule::WriteSensorEventsCsv(const std::vector<SensorDebugEvent> &events)
{
    if (!sensorEventsEnabled_ || !sensorEventsFile_.is_open() || events.empty()) {
        return;
    }
    for (const auto &e : events) {
        sensorEventsFile_ << e.sequence_id << ","
            << EscapeCsv(FormatIso8601(e.received_at, true)) << ","
            << EscapeCsv(FormatIso8601(e.source_observed_at, true)) << ","
            << EscapeCsv(DebugEventTypeToString(e.event_type)) << ","
            << EscapeCsv(MotionStateToString(e.motion_state)) << ","
            << EscapeCsv(e.episode_id) << ","
            << EscapeCsv(e.payload_json) << ","
            << EscapeCsv(e.power_mode) << "\n";
    }
    sensorEventsFile_.flush();
    if (!sensorEventsFile_.good()) {
        CAMERA_AGENT_LOG_ERROR("ProactiveAgent sensor_events.csv write failed; disabling sink");
        sensorEventsEnabled_ = false;
        sensorEventsFile_.close();
    }
}

void ProactiveAgentBusinessModule::WriteTickCsv(const SaPerceptionTick &tick, const std::string &tickJson)
{
    if (!ticksEnabled_ || !ticksFile_.is_open()) {
        return;
    }
    std::string gpsObs;
    if (tick.has_gps) {
        gpsObs = FormatIso8601(tick.gps.observed_at, true);
    }
    std::string jsonForCsv = tickJson;
    constexpr size_t kMaxCsvJson = 8 * 1024;
    if (jsonForCsv.size() > kMaxCsvJson) {
        jsonForCsv.resize(kMaxCsvJson);
        jsonForCsv.append("...<truncated>");
    }
    ticksFile_ << EscapeCsv(tick.tick_id) << ","
        << EscapeCsv(FormatIso8601(tick.observation_window.started_at, false)) << ","
        << EscapeCsv(FormatIso8601(tick.observation_window.ended_at, false)) << ","
        << EscapeCsv(MotionStateToString(tick.motion.state_at_window_start)) << ","
        << EscapeCsv(MotionStateToString(tick.motion.state_at_window_end)) << ","
        << tick.motion.events.size() << ","
        << tick.pdr_episodes.size() << ","
        << EscapeCsv(gpsObs) << ","
        << EscapeCsv(tick.power_state.mode) << ","
        << EscapeCsv(jsonForCsv) << "\n";
    ticksFile_.flush();
    if (!ticksFile_.good()) {
        CAMERA_AGENT_LOG_ERROR("ProactiveAgent sa_perception_ticks.csv write failed; disabling sink");
        ticksEnabled_ = false;
        ticksFile_.close();
    }
}

void ProactiveAgentBusinessModule::WriteSnapshotCsv(const SemanticSnapshot &snap, const std::string &snapJson)
{
    if (!snapshotsEnabled_ || !snapshotsFile_.is_open()) {
        return;
    }
    auto optNum = [](bool has, double v) -> std::string {
        if (!has) {
            return "";
        }
        std::ostringstream oss;
        oss << std::setprecision(15) << v;
        return oss.str();
    };
    std::string jsonForCsv = snapJson;
    constexpr size_t kMaxCsvJson = 8 * 1024;
    if (jsonForCsv.size() > kMaxCsvJson) {
        jsonForCsv.resize(kMaxCsvJson);
        jsonForCsv.append("...<truncated>");
    }
    snapshotsFile_ << EscapeCsv(snap.snapshot_id) << ","
        << EscapeCsv(snap.tick_id) << ","
        << EscapeCsv(FormatIso8601(snap.observed_at, false)) << ","
        << EscapeCsv(snap.motion.state) << ","
        << EscapeCsv(snap.motion.transition) << ","
        << EscapeCsv(optNum(snap.home_relation.has_distance_m, snap.home_relation.distance_m)) << ","
        << EscapeCsv(snap.home_relation.relation) << ","
        << EscapeCsv(snap.home_relation.quality) << ","
        << EscapeCsv(snap.pdr.has_episode_id ? snap.pdr.episode_id : "") << ","
        << EscapeCsv(snap.pdr.transition) << ","
        << EscapeCsv(optNum(true, snap.pdr.window.walking_duration_s)) << ","
        << EscapeCsv(optNum(true, snap.pdr.window.path_length_m)) << ","
        << EscapeCsv(optNum(snap.pdr.cumulative.has_walking_duration_s, snap.pdr.cumulative.walking_duration_s)) << ","
        << EscapeCsv(optNum(snap.pdr.cumulative.has_path_length_m, snap.pdr.cumulative.path_length_m)) << ","
        << EscapeCsv(optNum(snap.pdr.cumulative.has_net_displacement_m, snap.pdr.cumulative.net_displacement_m)) << ","
        << EscapeCsv(optNum(snap.pdr.cumulative.has_straightness_ratio, snap.pdr.cumulative.straightness_ratio)) << ","
        << EscapeCsv(snap.pdr.quality) << ","
        << EscapeCsv(snap.data_quality.overall) << ","
        << EscapeCsv(snap.power_state.mode) << ","
        << EscapeCsv(jsonForCsv) << "\n";
    snapshotsFile_.flush();
    if (!snapshotsFile_.good()) {
        CAMERA_AGENT_LOG_ERROR("ProactiveAgent semantic_snapshots.csv write failed; disabling sink");
        snapshotsEnabled_ = false;
        snapshotsFile_.close();
    }
}

void ProactiveAgentBusinessModule::WriteAgentResponseCsv(const AgentInvokeResult &result)
{
    if (!agentResponsesEnabled_ || !agentResponsesFile_.is_open()) {
        return;
    }
    // Flatten newlines so each invoke stays one CSV row in plain editors.
    auto flatten = [](std::string s) {
        for (char &c : s) {
            if (c == '\n' || c == '\r') {
                c = ' ';
            }
        }
        return s;
    };
    agentResponsesFile_ << EscapeCsv(result.tick_id) << ","
        << EscapeCsv(result.request_id) << ","
        << EscapeCsv(result.session_id) << ","
        << EscapeCsv(FormatIso8601(result.invoked_at, true)) << ","
        << EscapeCsv(result.status) << ","
        << result.error_code << ","
        << EscapeCsv(flatten(result.response_message)) << ","
        << EscapeCsv(flatten(result.stream_payload)) << ","
        << EscapeCsv(PowerModeController::ModeToString(
            PowerModeController::GetInstance().GetSnapshot().mode)) << "\n";
    agentResponsesFile_.flush();
    if (!agentResponsesFile_.good()) {
        CAMERA_AGENT_LOG_ERROR("ProactiveAgent agent_responses.csv write failed; disabling sink");
        agentResponsesEnabled_ = false;
        agentResponsesFile_.close();
    }
}

void ProactiveAgentBusinessModule::OnWalkingStarted(int64_t timestampMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!AcceptingInputLocked()) {
        return;
    }
    const Timestamp receivedAt = NowWallClockMs();
    const Timestamp eventTs = (timestampMs > 0) ? timestampMs : receivedAt;

    if (currentMotionState_ == MotionState::kWalking) {
        ++windowDuplicateMotion_;
    }

    currentMotionState_ = MotionState::kWalking;
    motionEvents_.push_back({MotionEventType::kWalkingStarted, eventTs, receivedAt});
#ifndef PROACTIVE_AGENT_HOST_TEST
    commute_sa::BaselineRuntime::GetInstance().OnWalkingStarted(eventTs);
#endif

    SensorDebugEvent dbg;
    dbg.sequence_id = ++debugEventSequence_;
    dbg.received_at = receivedAt;
    dbg.source_observed_at = eventTs;
    dbg.event_type = SensorDebugEventType::kWalkingStarted;
    dbg.motion_state = currentMotionState_;
    {
        std::ostringstream payload;
        payload << "{\"timestamp_ms\":" << eventTs << "}";
        dbg.payload_json = payload.str();
    }

    if (hasActiveEpisode_) {
        dbg.episode_id = activeEpisode_.episode_id;
        AppendDebugEventLocked(std::move(dbg));
        CAMERA_AGENT_LOG_DEBUG(
            "OnWalkingStarted idempotent ts=%{public}" PRId64 " activeEpisode=%{public}s points=%{public}zu",
            eventTs, activeEpisode_.episode_id.c_str(), activeEpisode_.points.size());
        return;
    }

    PdrEpisode episode;
    episode.episode_id = MakeEpisodeIdLocked(eventTs);
    episode.state = PdrEpisodeState::kActive;
    episode.started_at = eventTs;
    episode.has_ended_at = false;
    episode.ended_at = 0;
    episode.points.clear();
    episode.points.reserve(kPdrPointsReserveHint);

    activeEpisode_ = std::move(episode);
    hasActiveEpisode_ = true;
    dbg.episode_id = activeEpisode_.episode_id;
    AppendDebugEventLocked(std::move(dbg));
    CAMERA_AGENT_LOG_DEBUG("OnWalkingStarted new episode=%{public}s ts=%{public}" PRId64,
        activeEpisode_.episode_id.c_str(), eventTs);
}

void ProactiveAgentBusinessModule::OnWalkingStopped(int64_t timestampMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!AcceptingInputLocked()) {
        return;
    }
    const Timestamp receivedAt = NowWallClockMs();
    const Timestamp eventTs = (timestampMs > 0) ? timestampMs : receivedAt;

    if (currentMotionState_ == MotionState::kNotWalking) {
        ++windowDuplicateMotion_;
    }

    currentMotionState_ = MotionState::kNotWalking;
    motionEvents_.push_back({MotionEventType::kWalkingStopped, eventTs, receivedAt});
#ifndef PROACTIVE_AGENT_HOST_TEST
    commute_sa::BaselineRuntime::GetInstance().OnWalkingStopped(eventTs);
#endif

    SensorDebugEvent dbg;
    dbg.sequence_id = ++debugEventSequence_;
    dbg.received_at = receivedAt;
    dbg.source_observed_at = eventTs;
    dbg.event_type = SensorDebugEventType::kWalkingStopped;
    dbg.motion_state = currentMotionState_;
    {
        std::ostringstream payload;
        payload << "{\"timestamp_ms\":" << eventTs << "}";
        dbg.payload_json = payload.str();
    }

    if (!hasActiveEpisode_) {
        AppendDebugEventLocked(std::move(dbg));
        CAMERA_AGENT_LOG_DEBUG("OnWalkingStopped idempotent ts=%{public}" PRId64 " no active episode", eventTs);
        return;
    }

    const std::string endedId = activeEpisode_.episode_id;
    const size_t pointCount = activeEpisode_.points.size();
    const int64_t durationMs = eventTs - activeEpisode_.started_at;

    activeEpisode_.state = PdrEpisodeState::kEnded;
    activeEpisode_.ended_at = eventTs;
    activeEpisode_.has_ended_at = true;
    completedEpisodes_.push_back(std::move(activeEpisode_));
    activeEpisode_ = PdrEpisode {};
    hasActiveEpisode_ = false;

    dbg.episode_id = endedId;
    AppendDebugEventLocked(std::move(dbg));
    CAMERA_AGENT_LOG_DEBUG(
        "OnWalkingStopped episode=%{public}s ts=%{public}" PRId64 " points=%{public}zu durationMs=%{public}" PRId64
        " completedTotal=%{public}zu",
        endedId.c_str(), eventTs, pointCount, durationMs, completedEpisodes_.size());
}

void ProactiveAgentBusinessModule::OnPdrPoint(const RawPdrPoint &point)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!AcceptingInputLocked()) {
        return;
    }
    const Timestamp receivedAt = NowWallClockMs();

    if (!hasActiveEpisode_) {
        ++ignoredPdrWithoutEpisode_;
        ++windowDroppedPdr_;
        if (ignoredPdrWithoutEpisode_ == 1 || (ignoredPdrWithoutEpisode_ % 100) == 0) {
            CAMERA_AGENT_LOG_DEBUG(
                "OnPdrPoint ignored (no active episode) count=%{public}" PRId64 " ts=%{public}" PRId64,
                ignoredPdrWithoutEpisode_, point.observed_at);
        }
        return;
    }

    if (point.observed_at > 0 && !activeEpisode_.points.empty()) {
        const Timestamp lastTs = activeEpisode_.points.back().observed_at;
        if (lastTs > 0 && point.observed_at < lastTs) {
            ++discardedOutOfOrderPdr_;
            ++windowOutOfOrderPdr_;
            CAMERA_AGENT_LOG_DEBUG(
                "OnPdrPoint discarded out-of-order ts=%{public}" PRId64 " lastTs=%{public}" PRId64
                " episode=%{public}s totalDiscarded=%{public}" PRId64,
                point.observed_at, lastTs, activeEpisode_.episode_id.c_str(), discardedOutOfOrderPdr_);
            return;
        }
    }

    activeEpisode_.points.push_back(point);
    const size_t n = activeEpisode_.points.size();

    commute_sa::BaselineRuntime::GetInstance().OnPdrPoint(point.observed_at, point.x, point.y);

    SensorDebugEvent dbg;
    dbg.sequence_id = ++debugEventSequence_;
    dbg.received_at = receivedAt;
    dbg.source_observed_at = point.observed_at;
    dbg.event_type = SensorDebugEventType::kPdrPoint;
    dbg.motion_state = currentMotionState_;
    dbg.episode_id = activeEpisode_.episode_id;
    {
        std::ostringstream payload;
        payload << std::setprecision(15)
            << "{\"x\":" << point.x << ",\"y\":" << point.y
            << ",\"accuracy\":" << point.accuracy
            << ",\"errorCode\":" << point.errorCode
            << ",\"motionStatus\":" << point.motionStatus << "}";
        dbg.payload_json = payload.str();
    }
    AppendDebugEventLocked(std::move(dbg));

    if (n == 1 || (n % 100) == 0) {
        CAMERA_AGENT_LOG_DEBUG(
            "OnPdrPoint episode=%{public}s n=%{public}zu ts=%{public}" PRId64 " x=%{public}.3f y=%{public}.3f",
            activeEpisode_.episode_id.c_str(), n, point.observed_at, point.x, point.y);
    }
}

void ProactiveAgentBusinessModule::OnGpsLocation(const RawGpsLocation &location)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!AcceptingInputLocked()) {
        return;
    }
    const Timestamp receivedAt = NowWallClockMs();
    RawGpsLocation loc = location;
    if (loc.received_at <= 0) {
        loc.received_at = receivedAt;
    }
    if (loc.observed_at <= 0) {
        loc.observed_at = receivedAt;
    }

    if (hasLatestGps_ && loc.observed_at > 0 && latestGps_.observed_at > 0) {
        if (loc.observed_at < latestGps_.observed_at) {
            CAMERA_AGENT_LOG_DEBUG(
                "OnGpsLocation skipped stale ts=%{public}" PRId64 " latestTs=%{public}" PRId64,
                loc.observed_at, latestGps_.observed_at);
            return;
        }
    }

    latestGps_ = loc;
    hasLatestGps_ = true;

    SensorDebugEvent dbg;
    dbg.sequence_id = ++debugEventSequence_;
    dbg.received_at = loc.received_at;
    dbg.source_observed_at = loc.observed_at;
    dbg.event_type = SensorDebugEventType::kGpsReport;
    dbg.motion_state = currentMotionState_;
    {
        std::ostringstream payload;
        payload << std::setprecision(15)
            << "{\"latitude\":" << loc.latitude << ",\"longitude\":" << loc.longitude
            << ",\"horizontal_accuracy_m\":";
        if (loc.has_horizontal_accuracy) {
            payload << loc.horizontal_accuracy_m;
        } else {
            payload << "null";
        }
        payload << ",\"valid\":" << (loc.valid ? "true" : "false")
            << ",\"coordinate_system\":\"" << kGpsCoordinateSystem << "\""
            << ",\"source_type\":" << loc.source_type << "}";
        dbg.payload_json = payload.str();
    }
    AppendDebugEventLocked(std::move(dbg));

    CAMERA_AGENT_LOG_DEBUG(
        "OnGpsLocation ts=%{public}" PRId64 " acc=%{public}.1f valid=%{public}d source=%{public}d",
        loc.observed_at, loc.horizontal_accuracy_m, loc.valid ? 1 : 0, loc.source_type);
}

PerceptionInputSnapshot ProactiveAgentBusinessModule::CapturePerceptionInput() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    PerceptionInputSnapshot snap;
    snap.captured_at = NowWallClockMs();
    snap.current_motion_state = currentMotionState_;
    snap.motion_events = motionEvents_;
    snap.has_active_pdr_episode = hasActiveEpisode_;
    if (hasActiveEpisode_) {
        snap.active_pdr_episode = activeEpisode_;
    }
    snap.completed_pdr_episodes = completedEpisodes_;
    snap.has_latest_gps = hasLatestGps_;
    if (hasLatestGps_) {
        snap.latest_gps = latestGps_;
    }
    return snap;
}

void ProactiveAgentBusinessModule::StartPeriodicScheduler()
{
    std::lock_guard<std::mutex> lock(schedulerMutex_);
    if (schedulerRunning_.load()) {
        return;
    }
    schedulerStop_.store(false);
    schedulerRunning_.store(true);
    schedulerThread_ = std::thread([this]() { SchedulerLoop(); });
}

void ProactiveAgentBusinessModule::StopPeriodicScheduler()
{
    {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        if (!schedulerRunning_.load()) {
            return;
        }
        schedulerStop_.store(true);
    }
    schedulerCv_.notify_all();
    if (schedulerThread_.joinable()) {
        schedulerThread_.join();
    }
    schedulerRunning_.store(false);
}

void ProactiveAgentBusinessModule::SchedulerLoop()
{
    using clock = std::chrono::steady_clock;
    auto next = clock::now() + std::chrono::milliseconds(tickIntervalMs_.load());
    while (!schedulerStop_.load()) {
        std::unique_lock<std::mutex> lock(schedulerMutex_);
        schedulerCv_.wait_until(lock, next, [this]() { return schedulerStop_.load(); });
        if (schedulerStop_.load()) {
            break;
        }
        lock.unlock();
        ProcessTick();
        const int64_t intervalMs = tickIntervalMs_.load();
        next += std::chrono::milliseconds(intervalMs);
        const auto now = clock::now();
        if (next < now) {
            // Avoid busy loop after long stalls / clock jumps on steady_clock (should not happen).
            next = now + std::chrono::milliseconds(intervalMs);
        }
    }
}

void ProactiveAgentBusinessModule::ProcessTick()
{
    ProcessTickAt(NowWallClockMs());
}

void ProactiveAgentBusinessModule::ProcessTickAt(Timestamp windowEndMs)
{
    ProcessTickAtInner(windowEndMs);
}

void ProactiveAgentBusinessModule::ProcessTickAtInner(Timestamp windowEndMs)
{
    std::lock_guard<std::mutex> serial(tickSerialMutex_);

    FrozenTickData frozen;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return;
        }
        Timestamp windowStart = windowStartMs_;
        if (windowEndMs <= windowStart) {
            windowEndMs = windowStart + tickIntervalMs_.load();
        }

        frozen.windowStart = windowStart;
        frozen.windowEnd = windowEndMs;
        frozen.stateAtStart = motionStateAtWindowStart_;
        frozen.stateAtEnd = currentMotionState_;
        frozen.quality.dropped_pdr_points = windowDroppedPdr_;
        frozen.quality.out_of_order_pdr_points = windowOutOfOrderPdr_;
        frozen.quality.duplicate_motion_events = windowDuplicateMotion_;

        std::vector<MotionEvent> remainingEvents;
        for (const auto &e : motionEvents_) {
            if (InOpenClosedWindow(e.timestamp, windowStart, windowEndMs)) {
                frozen.motionEvents.push_back(e);
            } else if (e.timestamp > windowEndMs) {
                remainingEvents.push_back(e);
            }
            // Events at or before windowStart already belong to prior windows — drop.
        }
        motionEvents_.swap(remainingEvents);

        for (auto &ep : completedEpisodes_) {
            if (EpisodeIntersectsWindow(ep, windowStart, windowEndMs)) {
                frozen.episodes.push_back(ep);
            }
        }
        completedEpisodes_.clear();

        if (hasActiveEpisode_ && EpisodeIntersectsWindow(activeEpisode_, windowStart, windowEndMs)) {
            frozen.episodes.push_back(activeEpisode_);
        }

        frozen.hasGps = hasLatestGps_;
        if (hasLatestGps_) {
            frozen.gps = latestGps_;
        }

        frozen.debugEvents.swap(pendingDebugEvents_);
        windowDroppedPdr_ = 0;
        windowOutOfOrderPdr_ = 0;
        windowDuplicateMotion_ = 0;

        ++tickSequence_;
        frozen.tickSeq = tickSequence_;
        windowStartMs_ = windowEndMs;
        motionStateAtWindowStart_ = currentMotionState_;
    }

    // Build tick outside lock.
    SaPerceptionTick tick;
    tick.schema_version = kSchemaVersion;
    tick.tick_id = "sa-tick-" + FormatCompactLocal(frozen.windowEnd) + "-" +
        [&]() {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04llu", static_cast<unsigned long long>(frozen.tickSeq));
            return std::string(buf);
        }();
    tick.observed_at = frozen.windowEnd;
    tick.timezone = kTimezoneId;
    tick.observation_window.started_at = frozen.windowStart;
    tick.observation_window.ended_at = frozen.windowEnd;
    tick.observation_window.duration_s =
        static_cast<double>(frozen.windowEnd - frozen.windowStart) / 1000.0;
    tick.motion.state_at_window_start = frozen.stateAtStart;
    tick.motion.state_at_window_end = frozen.stateAtEnd;
    tick.motion.events = std::move(frozen.motionEvents);
    tick.has_gps = frozen.hasGps;
    if (frozen.hasGps) {
        tick.gps = frozen.gps;
    }
    tick.collection_quality = frozen.quality;
    tick.pdr_episodes.reserve(frozen.episodes.size());
    for (const auto &ep : frozen.episodes) {
        tick.pdr_episodes.push_back(ToEpisodeInput(ep));
    }
    {
        const auto power = PowerModeController::GetInstance().GetSnapshot();
        tick.power_state.mode = PowerModeController::ModeToString(power.mode);
        tick.power_state.agent_inference_enabled = power.agentInferenceEnabled;
    }

    const std::string tickJson = BuildTickJson(tick);
    SemanticSnapshot snapshot = NormalizePerceptionInput(tick);
    const std::string snapJson = BuildSnapshotJson(snapshot);

#ifndef PROACTIVE_AGENT_HOST_TEST
    // === On-device primary path: C++ SceneEngine on every SA tick ===
    commute_sa::TickDecision baselineDec;
    if (commute_sa::BaselineRuntime::GetInstance().Enabled()) {
        const bool hasGps = tick.has_gps && tick.gps.valid;
        baselineDec = commute_sa::BaselineRuntime::GetInstance().OnTick(tick.observed_at, hasGps, tick.gps.latitude,
            tick.gps.longitude, tick.gps.horizontal_accuracy_m, tick.gps.valid, tick.gps.source_type);
        CAMERA_AGENT_LOG_INFO(
            "SceneEngine tick=%{public}s scene=%{public}s pLeaveH=%{public}.2f pLeaveC=%{public}.2f "
            "push=%{public}d intent=%{public}s eta=%{public}.1f block=%{public}s pdr=%{public}s",
            tick.tick_id.c_str(), commute_sa::SceneToString(baselineDec.scene), baselineDec.score_home,
            baselineDec.score_company, baselineDec.should_service ? 1 : 0, baselineDec.service_intent.c_str(),
            baselineDec.eta_leave_s, baselineDec.push_block_reason.c_str(),
            commute_sa::BaselineRuntime::GetInstance().PdrDebugJson().c_str());
        {
            const std::string sceneStr = commute_sa::SceneToString(baselineDec.scene);
            const std::string homeRel = commute_sa::RelationToString(baselineDec.home_relation);
            const std::string companyRel = commute_sa::RelationToString(baselineDec.company_relation);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lastDebugScene_ = sceneStr;
                lastDebugHomeRel_ = homeRel;
                lastDebugCompanyRel_ = companyRel;
                lastDebugScoreHome_ = baselineDec.score_home;
                lastDebugScoreCompany_ = baselineDec.score_company;
                lastDebugEtaLeaveS_ = baselineDec.eta_leave_s;
                lastDebugDistHomeM_ = baselineDec.has_dist_home ? baselineDec.dist_home_m : -1.0;
                lastDebugShouldService_ = baselineDec.should_service;
                lastDebugHasGps_ = hasGps;
                if (!baselineDec.service_intent.empty()) {
                    lastDebugIntent_ = baselineDec.service_intent;
                }
            }
            if (sceneStr != lastBaselineScene_ || !hasLastBaselineScene_) {
                std::ostringstream detail;
                detail << std::fixed << std::setprecision(2) << "home=" << homeRel << " company=" << companyRel
                       << " hsmmH=" << baselineDec.hsmm_phase_home << " hsmmC=" << baselineDec.hsmm_phase_company
                       << " pLeaveH=" << baselineDec.score_home << " pLeaveC=" << baselineDec.score_company
                       << " eta=" << baselineDec.eta_leave_s;
                if (!baselineDec.push_block_reason.empty()) {
                    detail << " block=" << baselineDec.push_block_reason;
                }
                PublishProductDebugEvent("SCENE", tick.observed_at, sceneStr, detail.str());
            }
        }
        const bool firstOutside = commute_sa::ProductStore::GetInstance().ObserveLeaveProgress(
            tick.observed_at, commute_sa::RelationToString(baselineDec.home_relation),
            commute_sa::RelationToString(baselineDec.company_relation));
        if (firstOutside) {
            commute_sa::PersonalizationController::GetInstance().OnOutsideObserved(tick.observed_at);
            PublishProductDebugEvent("OUTSIDE", tick.observed_at, "first OUTSIDE after push",
                "t* recorded; CONFIRMED_LEAVE personalize pending");
        }
        {
            const auto &th = commute_sa::BaselineRuntime::GetInstance().Engine() != nullptr
                ? commute_sa::BaselineRuntime::GetInstance().Engine()->GetTheta()
                : commute_sa::DefaultTheta();
            const double lookbackS = std::max(th.lead_max_s + 600.0, 1200.0);
            std::string missedSide;
            if (commute_sa::ProductStore::GetInstance().ObserveMissedLeave(tick.observed_at,
                    commute_sa::RelationToString(baselineDec.home_relation),
                    commute_sa::RelationToString(baselineDec.company_relation), th.away_confirm_s, lookbackS,
                    th.focus_side, &missedSide)) {
                commute_sa::PersonalizationController::GetInstance().OnMissedLeave(tick.observed_at, missedSide);
                PublishProductDebugEvent("LABEL", tick.observed_at, "MISSED_LEAVE",
                    "side=" + missedSide + " sustained OUTSIDE without prior push");
            }
        }
    }
#endif

    {
        std::lock_guard<std::mutex> lock(mutex_);
        WriteSensorEventsCsv(frozen.debugEvents);
        WriteTickCsv(tick, tickJson);
        WriteSnapshotCsv(snapshot, snapJson);
#ifndef PROACTIVE_AGENT_HOST_TEST
        if (baselineDecisionsEnabled_ && baselineDecisionsFile_.is_open()) {
            baselineDecisionsFile_ << EscapeCsv(tick.tick_id) << ","
                << EscapeCsv(FormatIso8601(tick.observed_at, true)) << ","
                << EscapeCsv(commute_sa::SceneToString(baselineDec.scene)) << ","
                << baselineDec.score_home << "," << baselineDec.score_company << ","
                << EscapeCsv(baselineDec.hsmm_phase_home) << "," << EscapeCsv(baselineDec.hsmm_phase_company) << ","
                << baselineDec.hsmm_preleave_home << "," << baselineDec.hsmm_preleave_company << ","
                << baselineDec.hsmm_outside_home << "," << baselineDec.hsmm_outside_company << ","
                << EscapeCsv(commute_sa::RelationToString(baselineDec.home_relation)) << ","
                << EscapeCsv(commute_sa::RelationToString(baselineDec.company_relation)) << ","
                << (baselineDec.has_dist_home ? std::to_string(baselineDec.dist_home_m) : "") << ","
                << (baselineDec.has_dist_company ? std::to_string(baselineDec.dist_company_m) : "") << ","
                << (baselineDec.should_service ? 1 : 0) << ","
                << EscapeCsv(baselineDec.service_intent) << ","
                << EscapeCsv(baselineDec.uncertainty) << ","
                << baselineDec.hits_home << "," << baselineDec.hits_company << "\n";
            baselineDecisionsFile_.flush();
        }
        // Sparse GPS/walk samples only inside an active leave window (after push).
        if (tick.has_gps && tick.gps.valid) {
            commute_sa::ProductStore::GetInstance().AppendSparseSample(tick.observed_at, 0, tick.gps.latitude,
                tick.gps.longitude, tick.gps.horizontal_accuracy_m,
                commute_sa::BaselineRuntime::GetInstance().Walking(),
                commute_sa::RelationToString(baselineDec.home_relation),
                baselineDec.has_dist_home ? baselineDec.dist_home_m : -1.0);
        }
        lastBaselineScene_ = commute_sa::SceneToString(baselineDec.scene);
        hasLastBaselineScene_ = true;
#endif
        lastTick_ = tick;
        hasLastTick_ = true;
        lastSnapshot_ = snapshot;
        hasLastSnapshot_ = true;
    }

    // Product flow: SceneEngine for scene/push (no LLM).
    // LLM only when PersonalizationController says it is time to update θ.
    AgentInvokeResult invokeResult;
    invokeResult.implemented = true;
    invokeResult.tick_id = tick.tick_id;
    invokeResult.invoked_at = NowWallClockMs();
    invokeResult.status = "SceneEngineOnly";
    invokeResult.response_message = "no LLM on tick; SceneEngine owns scene";

#ifndef PROACTIVE_AGENT_HOST_TEST
    if (baselineDec.should_service) {
        invokeResult.status = "BaselinePush";
        invokeResult.response_message = baselineDec.service_intent + " scene=" +
            commute_sa::SceneToString(baselineDec.scene);

        DebugDeliveryPayload push;
        push.implemented = true;
        push.tick_id = tick.tick_id;
        push.scene = commute_sa::SceneToString(baselineDec.scene);
        push.service_intent = baselineDec.service_intent;
        push.observed_at = tick.observed_at;
        push.score_home = baselineDec.score_home;
        push.score_company = baselineDec.score_company;
        if (baselineDec.service_intent == "DEPARTURE_NOTIFICATION") {
            push.message = "记得带钥匙";
        } else if (baselineDec.service_intent == "LEAVE_COMPANY_NOTIFICATION") {
            push.message = "下班提醒";
        } else {
            push.message = baselineDec.service_intent;
        }
        PushDebugToHap(push);
        commute_sa::PersonalizationController::GetInstance().OnBaselinePush(
            tick.observed_at, baselineDec.service_intent, push.scene);

        double enterLeave = 0.0;
        int minEvidence = 0;
        if (commute_sa::BaselineRuntime::GetInstance().Engine() != nullptr) {
            const auto &th = commute_sa::BaselineRuntime::GetInstance().Engine()->GetTheta();
            enterLeave = th.enter_leave;
            minEvidence = th.min_evidence;
        }
        commute_sa::ProductStore::GetInstance().AppendLeavePush(tick.observed_at, baselineDec.service_intent,
            push.scene, baselineDec.score_home, baselineDec.score_company, baselineDec.has_dist_home,
            baselineDec.dist_home_m, commute_sa::BaselineRuntime::GetInstance().Walking(), enterLeave, minEvidence,
            commute_sa::RelationToString(baselineDec.home_relation), baselineDec.eta_leave_s);

        CAMERA_AGENT_LOG_WARN(
            "SceneEngine PUSH intent=%{public}s scene=%{public}s msg=%{public}s tick=%{public}s",
            push.service_intent.c_str(), push.scene.c_str(), push.message.c_str(), push.tick_id.c_str());
    }

    // Update-θ: first OUTSIDE after push (CONFIRMED_LEAVE) or 20min timeout (FALSE_PUSH) / DAY_END.
    if (commute_sa::BaselineRuntime::GetInstance().Enabled() &&
        commute_sa::BaselineRuntime::GetInstance().Engine() != nullptr) {
        const auto &theta = commute_sa::BaselineRuntime::GetInstance().Engine()->GetTheta();
        if (commute_sa::PersonalizationController::GetInstance().MaybeEnqueueJob(tick.observed_at, theta)) {
            commute_sa::PersonalizeJob job;
            if (commute_sa::PersonalizationController::GetInstance().PopPendingJob(&job)) {
                commute_sa::ProductStore::GetInstance().AppendPersonalizeJob(job.created_at_ms, job.reason,
                    job.last_intent, job.last_scene, job.last_push_at_ms, job.theta_snapshot);

                if ((job.reason == "AFTER_PUSH" || job.reason == "MISSED_LEAVE") && !job.settle_label.empty()) {
                    if (job.reason == "AFTER_PUSH" && job.last_push_at_ms > 0) {
                        const bool leaveCompany = (job.last_intent == "LEAVE_COMPANY_NOTIFICATION");
                        const std::string rel = leaveCompany
                            ? commute_sa::RelationToString(baselineDec.company_relation)
                            : commute_sa::RelationToString(baselineDec.home_relation);
                        const bool hasDist = leaveCompany ? baselineDec.has_dist_company : baselineDec.has_dist_home;
                        const double distM = leaveCompany ? baselineDec.dist_company_m : baselineDec.dist_home_m;
                        commute_sa::ProductStore::GetInstance().AppendLeaveLabel(tick.observed_at, job.last_push_at_ms,
                            job.settle_label, rel, hasDist, distM);
                        PublishProductDebugEvent("LABEL", tick.observed_at, job.settle_label,
                            "reason=" + job.reason + " rel=" + rel + " intent=" + job.last_intent);
                    }
                    // MISSED_LEAVE already written by ObserveMissedLeave.
                }

                if (job.reason == "DAY_END") {
                    const std::string anchorExec =
                        commute_sa::ProcessQueuedAnchorReestimateJobs(commute_sa::ProductStore::GetInstance().RootDir(), 3);
                    CAMERA_AGENT_LOG_INFO("DAY_END anchor reestimate drain: %{public}s", anchorExec.c_str());
                    PublishProductDebugEvent("ANCHOR", tick.observed_at, "DAY_END_DRAIN", anchorExec.substr(0, 200));
                }

                CAMERA_AGENT_LOG_INFO(
                    "PersonalizeJob reason=%{public}s label=%{public}s → invoke θ LLM (not scene)",
                    job.reason.c_str(), job.settle_label.c_str());
                PublishProductDebugEvent("LLM_START", tick.observed_at, job.reason,
                    "settle_label=" + job.settle_label + " invoke θ personalizer");
                std::ostringstream query;
                query << "{\"task\":\"personalize_leave_strategy\""
                      << ",\"reason\":\"" << job.reason << "\""
                      << ",\"settle_label\":\"" << job.settle_label << "\""
                      << ",\"last_intent\":\"" << job.last_intent << "\""
                      << ",\"last_scene\":\"" << job.last_scene << "\""
                      << ",\"last_push_at_ms\":" << job.last_push_at_ms
                      << ",\"theta\":{"
                      << "\"enter_leave\":" << job.theta_snapshot.enter_leave
                      << ",\"w_walk\":" << job.theta_snapshot.w_walk
                      << ",\"w_pdr\":" << job.theta_snapshot.w_pdr
                       << ",\"w_geo\":" << job.theta_snapshot.w_geo
                       << ",\"w_wifi\":" << job.theta_snapshot.w_wifi
                       << ",\"w_cell\":" << job.theta_snapshot.w_cell
                       << ",\"w_ble\":" << job.theta_snapshot.w_ble
                       << ",\"w_time\":" << job.theta_snapshot.w_time
                       << ",\"w_baro\":" << job.theta_snapshot.w_baro
                      << ",\"weekday_leave_home_hour\":" << job.theta_snapshot.weekday_leave_home_hour
                      << ",\"weekday_leave_company_hour\":" << job.theta_snapshot.weekday_leave_company_hour
                      << ",\"arm_delay_s\":" << job.theta_snapshot.arm_delay_s
                      << ",\"min_evidence\":" << job.theta_snapshot.min_evidence
                       << "},\"instruction\":\"Actively inspect evidence and compare the deterministic rule diagnosis. "
                          "Form multiple falsifiable hypotheses and a context-aware semantic intervention plan. "
                          "Use only the constrained optimizer for numeric candidates; commit only its guarded best candidate, "
                          "otherwise discard and audit no_op. Do not classify scenes.\"}";
                const int64_t invokeSinceMs = NowWallClockMs();
                invokeResult = InvokeAgent(tick.tick_id + "-personalize", query.str());
                if (invokeResult.status == "NotInitialized" || invokeResult.status == "NotImplemented") {
                    invokeResult.status = "PersonalizeQueuedNoAgent";
                    invokeResult.response_message =
                        "job written to personalize_jobs.jsonl; agent credentials missing or not ready";
                } else if (invokeResult.status == "Ok" || invokeResult.implemented) {
                    invokeResult.status = "PersonalizeInvoke";
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    lastLlmStatus_ = invokeResult.status;
                    lastLlmAtMs_ = tick.observed_at;
                }
                std::string summary = invokeResult.response_message;
                if (summary.size() > 1200) {
                    summary.resize(1200);
                    summary.append("...");
                }
                const std::string auditsJson =
                    commute_sa::ProductStore::GetInstance().GetRecentAuditsJson(invokeSinceMs - 2000, 8);
                std::ostringstream resultOss;
                resultOss << "{\"status\":\"" << EscapeJson(invokeResult.status) << "\""
                          << ",\"reason\":\"" << EscapeJson(job.reason) << "\""
                          << ",\"settle_label\":\"" << EscapeJson(job.settle_label) << "\""
                          << ",\"response_summary\":\"" << EscapeJson(summary) << "\""
                          << ",\"audits\":" << auditsJson << "}";
                std::ostringstream detailOss;
                detailOss << "tap for personalization decision; status=" << invokeResult.status;
                if (auditsJson.find("\"intervention_type\":\"NO_OP\"") != std::string::npos) {
                    detailOss << " (NO_OP)";
                } else if (auditsJson != "[]") {
                    detailOss << " (typed audit available)";
                } else {
                    detailOss << " (no typed audit recorded)";
                }
                PublishProductDebugEvent("LLM_DONE", tick.observed_at, invokeResult.status, detailOss.str(),
                    resultOss.str());
            }
        }
    }
#endif
    {
        std::lock_guard<std::mutex> lock(mutex_);
        WriteAgentResponseCsv(invokeResult);
    }
}

SemanticSnapshot ProactiveAgentBusinessModule::NormalizePerceptionInput(const SaPerceptionTick &input)
{
    SemanticSnapshot out;
    out.schema_version = kSchemaVersion;
    out.tick_id = input.tick_id;
    {
        const std::string prefix = "sa-tick-";
        if (input.tick_id.compare(0, prefix.size(), prefix) == 0) {
            out.snapshot_id = "snapshot-" + input.tick_id.substr(prefix.size());
        } else {
            out.snapshot_id = "snapshot-" + FormatCompactLocal(input.observed_at) + "-0000";
        }
    }
    out.observed_at = input.observed_at;
    out.timezone = input.timezone.empty() ? kTimezoneId : input.timezone;
    out.observation_window = input.observation_window;
    out.power_state = input.power_state;

    DataQuality dq;
    dq.fact_id = input.tick_id + ":quality";

    // ---- Motion ----
    out.motion.fact_id = input.tick_id + ":motion";
    out.motion.state = MotionStateToString(input.motion.state_at_window_end);

    bool hasStarted = false;
    bool hasStopped = false;
    for (const auto &e : input.motion.events) {
        if (e.type == MotionEventType::kWalkingStarted) {
            hasStarted = true;
        } else if (e.type == MotionEventType::kWalkingStopped) {
            hasStopped = true;
        }
    }
    const MotionState s0 = input.motion.state_at_window_start;
    const MotionState s1 = input.motion.state_at_window_end;

    if (s0 == MotionState::kNotWalking && s1 == MotionState::kNotWalking && !hasStarted && !hasStopped) {
        out.motion.transition = "NONE";
    } else if (s0 == MotionState::kNotWalking && s1 == MotionState::kWalking) {
        out.motion.transition = "STARTED";
        if (!hasStarted) {
            dq.issues.push_back("MOTION_EVENT_MISSING");
        }
    } else if (s0 == MotionState::kWalking && s1 == MotionState::kWalking && !hasStopped) {
        out.motion.transition = "CONTINUING";
    } else if (s0 == MotionState::kWalking && s1 == MotionState::kNotWalking) {
        out.motion.transition = "ENDED";
        if (!hasStopped) {
            dq.issues.push_back("MOTION_EVENT_MISSING");
        }
    } else if (s0 == MotionState::kNotWalking && s1 == MotionState::kNotWalking && hasStarted && hasStopped) {
        out.motion.transition = "STARTED_AND_ENDED";
    } else if (s0 == MotionState::kUnknown && s1 == MotionState::kUnknown && !hasStarted && !hasStopped) {
        out.motion.transition = "NONE";
    } else if (s0 == MotionState::kUnknown && s1 == MotionState::kWalking) {
        out.motion.transition = "STARTED";
        if (!hasStarted) {
            dq.issues.push_back("MOTION_EVENT_MISSING");
        }
    } else if (s0 == MotionState::kUnknown && s1 == MotionState::kNotWalking) {
        if (hasStarted && hasStopped) {
            out.motion.transition = "STARTED_AND_ENDED";
        } else if (hasStopped) {
            out.motion.transition = "ENDED";
        } else {
            out.motion.transition = "NONE";
        }
    } else {
        // Conflict / unexpected combo — derive from states.
        if (s0 != MotionState::kWalking && s1 == MotionState::kWalking) {
            out.motion.transition = "STARTED";
        } else if (s0 == MotionState::kWalking && s1 != MotionState::kWalking) {
            out.motion.transition = "ENDED";
        } else if (s0 == MotionState::kWalking && s1 == MotionState::kWalking) {
            out.motion.transition = "CONTINUING";
        } else if (hasStarted && hasStopped) {
            out.motion.transition = "STARTED_AND_ENDED";
        } else {
            out.motion.transition = "NONE";
        }
        dq.issues.push_back("MOTION_EVENT_CONFLICT");
    }

    if ((out.motion.transition == "STARTED" || out.motion.transition == "CONTINUING") && hasStopped &&
        s1 == MotionState::kWalking) {
        dq.issues.push_back("MOTION_EVENT_CONFLICT");
    }

    // ---- HOME (WGS84, same CRS as OnLocationReport / RawGpsLocation) ----
    out.home_relation.fact_id = input.tick_id + ":home";
    out.home_relation.anchor_id = kHomeAnchorId;
    out.home_relation.relation = "UNKNOWN";
    out.home_relation.quality = "MISSING";

    if (!input.has_gps) {
        out.home_relation.quality = "MISSING";
        out.home_relation.relation = "UNKNOWN";
        dq.missing_fields.push_back("gps");
    } else if (!input.gps.valid) {
        out.home_relation.quality = "INVALID";
        out.home_relation.relation = "UNKNOWN";
        if (input.gps.has_horizontal_accuracy) {
            out.home_relation.has_gps_accuracy_m = true;
            out.home_relation.gps_accuracy_m = input.gps.horizontal_accuracy_m;
        }
        if (input.gps.observed_at > 0) {
            out.home_relation.has_gps_observed_at = true;
            out.home_relation.gps_observed_at = input.gps.observed_at;
        }
        dq.issues.push_back("GPS_INVALID");
    } else {
        if (input.gps.observed_at > 0) {
            out.home_relation.has_gps_observed_at = true;
            out.home_relation.gps_observed_at = input.gps.observed_at;
            const double ageS = static_cast<double>(input.observed_at - input.gps.observed_at) / 1000.0;
            out.home_relation.has_gps_age_s = true;
            out.home_relation.gps_age_s = ageS;
            if (ageS < 0.0) {
                out.home_relation.quality = "INVALID";
                out.home_relation.relation = "UNKNOWN";
                dq.issues.push_back("GPS_INVALID");
            } else if (ageS > static_cast<double>(kGpsStaleAfterSeconds)) {
                out.home_relation.quality = "STALE";
                out.home_relation.relation = "UNKNOWN";
                dq.issues.push_back("GPS_STALE");
            }
        } else {
            out.home_relation.quality = "INVALID";
            out.home_relation.relation = "UNKNOWN";
            dq.issues.push_back("GPS_INVALID");
        }

        if (out.home_relation.quality != "INVALID" && out.home_relation.quality != "STALE") {
            if (!input.gps.has_horizontal_accuracy) {
                out.home_relation.quality = "MISSING";
                out.home_relation.relation = "UNKNOWN";
                dq.missing_fields.push_back("gps.horizontal_accuracy_m");
            } else {
                out.home_relation.has_gps_accuracy_m = true;
                out.home_relation.gps_accuracy_m = input.gps.horizontal_accuracy_m;
                if (input.gps.horizontal_accuracy_m > kMaxUsableGpsAccuracyMeters) {
                    out.home_relation.quality = "POOR_ACCURACY";
                    out.home_relation.relation = "UNKNOWN";
                    dq.issues.push_back("GPS_POOR_ACCURACY");
                } else {
                    const double dist = HaversineM(input.gps.latitude, input.gps.longitude,
                        kHomeLatitude, kHomeLongitude);
                    if (!std::isfinite(dist)) {
                        out.home_relation.quality = "INVALID";
                        out.home_relation.relation = "UNKNOWN";
                        dq.issues.push_back("GPS_INVALID");
                    } else {
                        out.home_relation.quality = "USABLE";
                        out.home_relation.has_distance_m = true;
                        out.home_relation.distance_m = dist;
                        if (dist <= kHomeInsideRadiusMeters) {
                            out.home_relation.relation = "INSIDE";
                        } else if (dist <= kHomeNearRadiusMeters) {
                            out.home_relation.relation = "NEAR";
                        } else {
                            out.home_relation.relation = "OUTSIDE";
                        }
                    }
                }
            }
        } else if (input.gps.has_horizontal_accuracy) {
            out.home_relation.has_gps_accuracy_m = true;
            out.home_relation.gps_accuracy_m = input.gps.horizontal_accuracy_m;
        }
    }

    // ---- PDR ----
    out.pdr.fact_id = input.tick_id + ":pdr";
    const Timestamp wStart = input.observation_window.started_at;
    const Timestamp wEnd = input.observation_window.ended_at;

    if (input.collection_quality.out_of_order_pdr_points > 0) {
        dq.issues.push_back("PDR_OUT_OF_ORDER_POINTS");
    }
    if (input.collection_quality.dropped_pdr_points > 0) {
        dq.issues.push_back("PDR_DROPPED_POINTS");
    }

    const PdrEpisodeInput *selected = nullptr;
    if (input.pdr_episodes.size() > 1) {
        dq.issues.push_back("MULTIPLE_PDR_EPISODES_IN_WINDOW");
    }

    std::vector<const PdrEpisodeInput *> activeAtEnd;
    for (const auto &ep : input.pdr_episodes) {
        const bool activeEnd = (ep.started_at <= wEnd) &&
            (!ep.has_ended_at || ep.ended_at > wEnd);
        if (activeEnd) {
            activeAtEnd.push_back(&ep);
        }
    }
    if (activeAtEnd.size() > 1) {
        dq.issues.push_back("MULTIPLE_ACTIVE_PDR_EPISODES");
        selected = activeAtEnd[0];
        for (const auto *ep : activeAtEnd) {
            if (ep->started_at > selected->started_at) {
                selected = ep;
            }
        }
    } else if (activeAtEnd.size() == 1) {
        selected = activeAtEnd[0];
    } else if (!input.pdr_episodes.empty()) {
        selected = &input.pdr_episodes[0];
        for (const auto &ep : input.pdr_episodes) {
            if (!ep.has_ended_at) {
                continue;
            }
            if (!selected->has_ended_at || ep.ended_at > selected->ended_at) {
                selected = &ep;
            }
        }
    }

    if (selected == nullptr) {
        out.pdr.has_episode_id = false;
        out.pdr.transition = "NONE";
        out.pdr.active_at_window_start = false;
        out.pdr.active_at_window_end = false;
        out.pdr.has_started_at = false;
        out.pdr.has_ended_at = false;
        out.pdr.window = {};
        out.pdr.cumulative.has_walking_duration_s = false;
        out.pdr.cumulative.has_path_length_m = false;
        out.pdr.cumulative.has_net_displacement_m = false;
        out.pdr.cumulative.has_straightness_ratio = false;
        out.pdr.cumulative.point_count = 0;
        if (input.motion.state_at_window_end == MotionState::kWalking) {
            out.pdr.quality = "MISSING";
            dq.missing_fields.push_back("pdr.points");
        } else {
            out.pdr.quality = "NOT_APPLICABLE";
        }
    } else {
        out.pdr.has_episode_id = true;
        out.pdr.episode_id = selected->episode_id;
        out.pdr.has_started_at = true;
        out.pdr.started_at = selected->started_at;
        out.pdr.has_ended_at = selected->has_ended_at;
        out.pdr.ended_at = selected->ended_at;

        const bool startedInWindow = InOpenClosedWindow(selected->started_at, wStart, wEnd);
        const bool endedInWindow =
            selected->has_ended_at && InOpenClosedWindow(selected->ended_at, wStart, wEnd);
        out.pdr.active_at_window_start = (selected->started_at <= wStart) &&
            (!selected->has_ended_at || selected->ended_at > wStart);
        out.pdr.active_at_window_end = (selected->started_at <= wEnd) &&
            (!selected->has_ended_at || selected->ended_at > wEnd);

        if (startedInWindow && endedInWindow) {
            out.pdr.transition = "STARTED_AND_ENDED";
        } else if (startedInWindow && out.pdr.active_at_window_end) {
            out.pdr.transition = "STARTED";
        } else if (out.pdr.active_at_window_start && endedInWindow) {
            out.pdr.transition = "ENDED";
        } else if (out.pdr.active_at_window_start && out.pdr.active_at_window_end) {
            out.pdr.transition = "CONTINUING";
        } else {
            out.pdr.transition = "NONE";
            out.pdr.quality = "INVALID";
            dq.issues.push_back("PDR_EPISODE_TIME_CONFLICT");
        }

        const Timestamp epEnd = selected->has_ended_at ? selected->ended_at : wEnd;
        const Timestamp overlapStart = std::max(wStart, selected->started_at);
        const Timestamp overlapEnd = std::min(wEnd, epEnd);
        out.pdr.window.walking_duration_s =
            (overlapEnd > overlapStart) ? static_cast<double>(overlapEnd - overlapStart) / 1000.0 : 0.0;

        double winPath = 0.0;
        double winNet = 0.0;
        int64_t winCount = 0;
        bool winFinite = true;
        ComputePathMetrics(selected->points, wStart, wEnd, true, &winPath, &winNet, &winCount, &winFinite);
        out.pdr.window.path_length_m = winPath;
        out.pdr.window.point_count = winCount;

        const Timestamp cumEnd = std::min(wEnd, epEnd);
        double cumPath = 0.0;
        double cumNet = 0.0;
        int64_t cumCount = 0;
        bool cumFinite = true;
        ComputePathMetrics(selected->points, selected->started_at, cumEnd, false, &cumPath, &cumNet, &cumCount,
            &cumFinite);

        out.pdr.cumulative.has_walking_duration_s = true;
        out.pdr.cumulative.walking_duration_s =
            (cumEnd > selected->started_at)
                ? static_cast<double>(cumEnd - selected->started_at) / 1000.0
                : 0.0;
        out.pdr.cumulative.has_path_length_m = true;
        out.pdr.cumulative.path_length_m = cumPath;
        out.pdr.cumulative.has_net_displacement_m = true;
        out.pdr.cumulative.net_displacement_m = cumNet;
        out.pdr.cumulative.point_count = cumCount;

        if (cumPath > 0.0) {
            double ratio = cumNet / cumPath;
            if (ratio > 1.0 && ratio <= 1.000001) {
                ratio = 1.0;
            }
            if (ratio > 1.0 + 1e-3) {
                out.pdr.quality = "INVALID";
                dq.issues.push_back("PDR_EPISODE_TIME_CONFLICT");
            } else {
                out.pdr.cumulative.has_straightness_ratio = true;
                out.pdr.cumulative.straightness_ratio = std::max(0.0, std::min(1.0, ratio));
            }
        } else {
            out.pdr.cumulative.has_straightness_ratio = false;
        }

        if (out.pdr.quality != "INVALID") {
            if (!winFinite || !cumFinite) {
                out.pdr.quality = "INVALID";
            } else if (cumCount < 2) {
                out.pdr.quality = "INSUFFICIENT_POINTS";
            } else {
                out.pdr.quality = "USABLE";
            }
        }
    }

    // ---- overall ----
    const bool homeUsable = (out.home_relation.quality == "USABLE");
    const bool pdrUsable = (out.pdr.quality == "USABLE");
    const bool pdrNotApplicable = (out.pdr.quality == "NOT_APPLICABLE");
    const bool pdrBad = (out.pdr.quality == "INSUFFICIENT_POINTS" || out.pdr.quality == "MISSING" ||
        out.pdr.quality == "INVALID");

    if (!homeUsable && !pdrUsable && !pdrNotApplicable) {
        dq.overall = "INSUFFICIENT";
    } else if (!dq.missing_fields.empty() || !dq.issues.empty() || !homeUsable || pdrBad) {
        dq.overall = "PARTIAL";
    } else {
        dq.overall = "GOOD";
    }
    // Deduplicate issues while preserving order.
    {
        std::vector<std::string> uniq;
        for (const auto &iss : dq.issues) {
            if (std::find(uniq.begin(), uniq.end(), iss) == uniq.end()) {
                uniq.push_back(iss);
            }
        }
        dq.issues.swap(uniq);
    }
    out.data_quality = std::move(dq);
    return out;
}

std::string ProactiveAgentBusinessModule::ResolveAgentEnvFilePath() const
{
    return kAgentEnvFilePath;
}

#ifndef PROACTIVE_AGENT_HOST_TEST
namespace {
constexpr const char *kSaContextEngineBundleName = "commuteagentservice";
constexpr const char *kSaContextEngineModuleName = "commuteagentservice";
constexpr const char *kSaContextEngineDatabaseDir = "/data/service/el2/9903/database";

/** Low-frequency personalization researcher; never used for per-tick scene labels. */
constexpr const char *kThetaPersonalizerSystemPrompt = R"delimiter(
 你是离家检测的低频个性化研究 Agent。实时场景由端侧 HSMM + SceneEngine 确定；你不参与逐 tick 推断，也不直接写数值参数。

 先查看总体错误、当前 theta、真实 anchor、该anchor_id的历史摘要、目标完整 episode 和跨 episode 画像，再按疑点查询传感器摘要。个性化统计只以anchor_id为主键，
 company/home仅作为实时产品角色，不作为Agent工具参数。提出最多三个可证伪假设，
 每个假设记录支持证据、反证、缺失证据和置信度。区分传感器不可用、可用但无变化、已形成离开前缀后行为反转三种情况。

 若跨 episode 差异主要来自原子证据的长期区分能力，Agent只能选择需要重估的通道并调用
 estimate_evidence_strength；不得给目标数值。C++执行Laplace平滑、coverage、样本收缩、完整HSMM回放与安全门，
 eligible后才可commit_evidence_strength_candidate。顺序或返回关系问题仍使用context template，不得用强度拟合替代。

 若证据和顺序稳定、但PRE_LEAVE或LEAVING后验占用时长跨正例持续偏离全局先验，只能调用fit_duration_prior并选择一个状态，
 不得给目标均值或方向。C++从未截断正例提取时长，执行稳健统计、样本收缩、±50%限制、全历史回放与逐episode安全门；
 eligible后才可commit_duration_prior_candidate。一次trial只允许一个主要干预族，样本不足或回放退化时no_op。

 对原始 FALSE_PUSH，只有完整历史显示它与确认离开共享前缀、随后气压回升并回到起始高度、且未 outside 时，才可提议
 propose_aborted_leave_interpretation；这仍是行为推断，不是主观意图事实。至少两个 ABORTED_LEAVE 支持时，才可把有序返回过程写入
 cancel_sequence。调用 get_context_template_catalog 后，只能组合白名单 positive_sequence、cancel_sequence 和 negative_pattern；不得提供
 数值强度、阈值或参数变化。C++ 负责参数估计、前缀/强度枚举和全历史回放。

 每次任务最多生成一次模板。只有 best_candidate_id 非空且所有硬门通过时才提交，否则丢弃或 no_op。禁止把单个传感器当作充分条件，
 禁止编造证据。所有工具结束后调用一次submit_agent_analysis：intervention_type只能是STRUCTURE、EVIDENCE_STRENGTH、DURATION、NO_OP，
 decision只能是COMMITTED、REJECTED、DISCARDED、NO_OP。前三类记录匹配的目标结构/通道/状态、最终工具、工具结果和回放结果；
 NO_OP必须同时作为type和decision。所有类型记录真实anchor_id、支持、反证、缺失证据、置信度和决策理由。
 submit_agent_analysis是唯一最终审计入口；调用后停止，不再调用其他审计或直接数值修改工具。
)delimiter";
} // namespace
#endif

void ProactiveAgentBusinessModule::TryInitializeAgent()
{
    ShutdownAgent();

    const std::string envPath = ResolveAgentEnvFilePath();
    const AgentEnvLoadResult loadResult = LoadAgentEnvironmentFromFile(envPath);
    if (!loadResult.ok) {
        agentInitStatus_ = loadResult.error.empty() ? "Agent initialization failed" : loadResult.error;
        CAMERA_AGENT_LOG_INFO("Agent initialization failed: %{public}s", agentInitStatus_.c_str());
        return;
    }

    agentEnvironment_ = loadResult.environment;
    debugSinksConfig_ = agentEnvironment_.debugSinks;
#ifndef PROACTIVE_AGENT_HOST_TEST
    // Personalization always follows flow gates (AFTER_PUSH / DAY_END), not env toggles.
    commute_sa::PersonalizationController::GetInstance().SetEnabled(true);
    commute_sa::EvidenceQuery::GetInstance().SetRootDir(kProductRoot);
#endif
    if (!debugSinksConfig_) {
        sensorEventsEnabled_ = false;
        ticksEnabled_ = false;
        snapshotsEnabled_ = false;
        agentResponsesEnabled_ = false;
        baselineDecisionsEnabled_ = false;
    }
    CAMERA_AGENT_LOG_INFO("Agent environment file loaded (credentials for θ personalize LLM)");
    CAMERA_AGENT_LOG_INFO("SA_AGENT_BASE_URL configured");
    CAMERA_AGENT_LOG_INFO("SA_AGENT_API_KEY configured: yes");
    CAMERA_AGENT_LOG_INFO("SA_AGENT_DEBUG_SINKS=%{public}d", debugSinksConfig_ ? 1 : 0);

#ifdef PROACTIVE_AGENT_HOST_TEST
    agentConfigured_ = true;
    agentInitStatus_ = "Ready";
    return;
#else
    sa_agent::LeavingHomeContextEngineOptions ctxOptions;
    ctxOptions.bundleName = kSaContextEngineBundleName;
    ctxOptions.moduleName = kSaContextEngineModuleName;
    ctxOptions.databaseDir = kSaContextEngineDatabaseDir;

    auto agentConfig = sa_agent::BuildLeavingHomeBaselineAgentConfig(
        agentEnvironment_.baseUrl, agentEnvironment_.apiKey, ctxOptions);
    if (agentConfig == nullptr) {
        agentInitStatus_ = "Agent config build failed";
        agentEnvironment_.apiKey.clear();
        CAMERA_AGENT_LOG_ERROR("Agent initialization failed: %{public}s", agentInitStatus_.c_str());
        CAMERA_AGENT_LOG_INFO("Agent initialization failed: %{public}s", agentInitStatus_.c_str());
        return;
    }

    agentConfig->mode = jiuwen::AgentType::REACT;
    agentConfig->maxTurn = 32;
    agentConfig->promptTemplates["system"] = kThetaPersonalizerSystemPrompt;

    (void)jiuwen::ResourceManager::GetInstance();
    const std::vector<std::string> evidenceTools = sa_agent::RegisterEvidenceTools();
    const std::vector<std::string> actionTools = sa_agent::RegisterActionTools();
    std::vector<std::string> allTools = evidenceTools;
    allTools.insert(allTools.end(), actionTools.begin(), actionTools.end());

    CAMERA_AGENT_LOG_INFO(
        "Theta personalizer mode=%{public}d maxTurn=%{public}u evidence=%{public}zu action=%{public}zu",
        static_cast<int>(agentConfig->mode), agentConfig->maxTurn, evidenceTools.size(), actionTools.size());

    agent_ = std::make_shared<jiuwen::Agent>(agentConfig);
    const auto addRc = agent_->AddTools(allTools);
    if (addRc != jiuwen::ErrorCode::SUCCESS) {
        CAMERA_AGENT_LOG_ERROR("AddTools failed code=%{public}d", static_cast<int>(addRc));
    } else {
        CAMERA_AGENT_LOG_INFO("Evidence+action tools registered and added to agent n=%{public}zu",
            allTools.size());
    }
    agentSessionId_ = std::string("theta-personalize-") + FormatCompactLocal(NowWallClockMs());
    agentConfigured_ = true;
    agentInitStatus_ = "Ready";
    CAMERA_AGENT_LOG_INFO("Theta personalizer agent initialized session=%{public}s",
        agentSessionId_.c_str());
#endif
}

void ProactiveAgentBusinessModule::ShutdownAgent()
{
    agent_.reset();
    agentEnvironment_.baseUrl.clear();
    agentEnvironment_.apiKey.clear();
    agentSessionId_.clear();
    agentConfigured_ = false;
    agentInitStatus_.clear();
}

bool ProactiveAgentBusinessModule::IsAgentReady() const
{
#ifdef PROACTIVE_AGENT_HOST_TEST
    return agentConfigured_;
#else
    return agent_ != nullptr;
#endif
}

std::string ProactiveAgentBusinessModule::GetAgentInitStatus() const
{
    return agentInitStatus_;
}

AgentInvokeResult ProactiveAgentBusinessModule::InvokeAgent(const std::string &tickId,
    const std::string &semanticSnapshotJson)
{
    return InvokeAgentInner(tickId, semanticSnapshotJson);
}

AgentInvokeResult ProactiveAgentBusinessModule::InvokeAgentInner(const std::string &tickId,
    const std::string &semanticSnapshotJson)
{
    AgentInvokeResult r;
    r.tick_id = tickId;
    r.invoked_at = NowWallClockMs();
    r.implemented = false;

    if (!IsAgentReady()) {
        r.status = agentInitStatus_.empty() ? "NotInitialized" : agentInitStatus_;
        return r;
    }

#ifdef PROACTIVE_AGENT_HOST_TEST
    (void)semanticSnapshotJson;
    r.status = "NotImplemented";
    return r;
#else
    std::shared_ptr<jiuwen::Agent> agent;
    std::string sessionId;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        agent = agent_;
        sessionId = agentSessionId_;
        if (sessionId.empty() && agent != nullptr) {
            agentSessionId_ = std::string("leaving-home-") + FormatCompactLocal(NowWallClockMs());
            sessionId = agentSessionId_;
        }
    }
    if (agent == nullptr) {
        r.status = "NotInitialized";
        return r;
    }

    r.session_id = sessionId;
    r.request_id = tickId.empty() ? (std::string("req-") + FormatCompactLocal(r.invoked_at)) : tickId;

    auto req = std::make_shared<jiuwen::Request>();
    req->sessionId = r.session_id;
    req->requestId = r.request_id;
    // Cap query size — oversized snapshot JSON has been seen adjacent to heap metadata in cppcrash.
    constexpr size_t kMaxQueryBytes = 48 * 1024;
    if (semanticSnapshotJson.size() > kMaxQueryBytes) {
        req->query = semanticSnapshotJson.substr(0, kMaxQueryBytes);
        req->query.append("...<truncated>");
        CAMERA_AGENT_LOG_WARN("InvokeAgent truncating snapshot query from %{public}zu to %{public}zu",
            semanticSnapshotJson.size(), req->query.size());
    } else {
        req->query = semanticSnapshotJson;
    }

    jiuwen::StreamFilter filter;
    filter.mode = jiuwen::StreamMode::OUTPUT;
    filter.filterMap["type"] = {"assistant", "agent"};

    // Stream callbacks can outlive Invoke on jiuwen worker threads — must be thread-safe and closable.
    struct StreamSink {
        std::mutex mutex;
        std::string data;
        bool closed = false;
    };
    auto sink = std::make_shared<StreamSink>();
    auto callback = [sink](const std::shared_ptr<jiuwen::StreamData> &streamData) {
        if (sink == nullptr || streamData == nullptr || streamData->payload.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(sink->mutex);
        if (sink->closed) {
            return;
        }
        constexpr size_t kMaxStreamBytes = 64 * 1024;
        if (sink->data.size() >= kMaxStreamBytes) {
            return;
        }
        if (!sink->data.empty()) {
            sink->data.push_back('\n');
        }
        const size_t room = kMaxStreamBytes - sink->data.size();
        sink->data.append(streamData->payload, 0, std::min(room, streamData->payload.size()));
    };

    const auto resp = agent->Invoke(req, filter, callback);
    {
        std::lock_guard<std::mutex> lock(sink->mutex);
        sink->closed = true;
        r.stream_payload = sink->data;
    }
    r.implemented = true;
    r.error_code = static_cast<int>(resp.errorCode);
    r.response_message = resp.message;
    constexpr size_t kMaxLoggedPayload = 16 * 1024;
    if (r.response_message.size() > kMaxLoggedPayload) {
        r.response_message.resize(kMaxLoggedPayload);
        r.response_message.append("...<truncated>");
    }
    if (r.stream_payload.size() > kMaxLoggedPayload) {
        r.stream_payload.resize(kMaxLoggedPayload);
        r.stream_payload.append("...<truncated>");
    }
    r.status = (resp.errorCode == jiuwen::ErrorCode::SUCCESS) ? "Ok" : "InvokeFailed";

    CAMERA_AGENT_LOG_INFO(
        "Agent Invoke tick=%{public}s status=%{public}s err=%{public}d msg_len=%{public}zu stream_len=%{public}zu",
        tickId.c_str(), r.status.c_str(), r.error_code, r.response_message.size(), r.stream_payload.size());
    return r;
#endif
}

ValidationResult ProactiveAgentBusinessModule::ValidateAgentResponse(const AgentInvokeResult &response)
{
    (void)response;
    ValidationResult r;
    r.implemented = false;
    r.status = "NotImplemented";
    return r;
}

void ProactiveAgentBusinessModule::PushDebugToHap(const DebugDeliveryPayload &payload)
{
    // Feed debug HAP timeline; WantAgent/notification can be added later.
    if (!payload.implemented) {
        return;
    }
    CAMERA_AGENT_LOG_WARN(
        "PushDebugToHap intent=%{public}s scene=%{public}s msg=%{public}s tick=%{public}s "
        "scoreH=%{public}.2f scoreC=%{public}.2f",
        payload.service_intent.c_str(), payload.scene.c_str(), payload.message.c_str(), payload.tick_id.c_str(),
        payload.score_home, payload.score_company);
    std::ostringstream detail;
    detail << std::fixed << std::setprecision(2) << "intent=" << payload.service_intent << " scene=" << payload.scene
           << " scoreH=" << payload.score_home << " scoreC=" << payload.score_company << " tick=" << payload.tick_id;
    PublishProductDebugEvent("PUSH", payload.observed_at,
        payload.message.empty() ? payload.service_intent : payload.message, detail.str());
}

void ProactiveAgentBusinessModule::PublishProductDebugEvent(const std::string &type, int64_t tMs,
    const std::string &title, const std::string &detail, const std::string &resultJson)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ProductDebugEvent ev;
    ev.seq = ++productDebugSeq_;
    ev.t_ms = tMs;
    ev.type = type;
    ev.title = title;
    ev.detail = detail;
    ev.result_json = resultJson;
    productDebugEvents_.push_back(ev);
    while (productDebugEvents_.size() > kProductDebugEventCap) {
        productDebugEvents_.pop_front();
    }
}

void ProactiveAgentBusinessModule::ClearProductDebugTimeline()
{
    std::lock_guard<std::mutex> lock(mutex_);
    productDebugEvents_.clear();
    productDebugSeq_ = 0;
}

std::string ProactiveAgentBusinessModule::GetProductDebugTimelineJson() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{\"ok\":true,\"now_ms\":" << NowWallClockMs()
        << ",\"status\":{"
        << "\"scene\":\"" << EscapeJson(lastDebugScene_) << "\""
        << ",\"home_relation\":\"" << EscapeJson(lastDebugHomeRel_) << "\""
        << ",\"company_relation\":\"" << EscapeJson(lastDebugCompanyRel_) << "\""
        << ",\"score_home\":" << lastDebugScoreHome_
        << ",\"score_company\":" << lastDebugScoreCompany_
        << ",\"eta_leave_s\":" << lastDebugEtaLeaveS_
        << ",\"dist_home_m\":" << lastDebugDistHomeM_
        << ",\"should_service\":" << (lastDebugShouldService_ ? "true" : "false")
        << ",\"has_gps\":" << (lastDebugHasGps_ ? "true" : "false")
        << ",\"last_intent\":\"" << EscapeJson(lastDebugIntent_) << "\""
        << ",\"last_llm_status\":\"" << EscapeJson(lastLlmStatus_) << "\""
        << ",\"last_llm_at_ms\":" << lastLlmAtMs_
        << "},\"events\":[";
    bool first = true;
    for (const auto &ev : productDebugEvents_) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << "{\"seq\":" << ev.seq << ",\"t_ms\":" << ev.t_ms << ",\"type\":\"" << EscapeJson(ev.type)
            << "\",\"title\":\"" << EscapeJson(ev.title) << "\",\"detail\":\"" << EscapeJson(ev.detail) << "\"";
        if (!ev.result_json.empty()) {
            oss << ",\"result\":" << ev.result_json;
        }
        oss << "}";
    }
    oss << "],\"event_count\":" << productDebugEvents_.size() << ",\"next_seq\":" << (productDebugSeq_ + 1) << "}";
    return oss.str();
}

bool ProactiveAgentBusinessModule::HasLastTick() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return hasLastTick_;
}

SaPerceptionTick ProactiveAgentBusinessModule::GetLastTick() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastTick_;
}

bool ProactiveAgentBusinessModule::HasLastSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return hasLastSnapshot_;
}

SemanticSnapshot ProactiveAgentBusinessModule::GetLastSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastSnapshot_;
}

std::string ProactiveAgentBusinessModule::GetDebugRunDirectory() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return debugRunDir_;
}

} // namespace OHOS::Multimedia::CameraAgentService
