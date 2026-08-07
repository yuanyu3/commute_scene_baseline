/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Proactive Agent business module — perception tick, normalize, debug CSV.
 */

#ifndef PROACTIVE_AGENT_BUSINESS_MODULE_H
#define PROACTIVE_AGENT_BUSINESS_MODULE_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AgentRuntimeConfig.h"

namespace jiuwen {
class Agent;
} // namespace jiuwen

namespace OHOS::Multimedia::CameraAgentService {

/** Wall-clock timestamp in milliseconds. */
using Timestamp = int64_t;

// ---- Fixed MVP config (change tick interval here only; schema/algorithms stay) ----
constexpr int kDebugTickIntervalSeconds = 10;
#ifdef PROACTIVE_AGENT_HOST_TEST
constexpr const char *kProductRoot = "commute_product_host";
constexpr const char *kDebugOutputRoot = "sa_sensor_test_host";
#else
constexpr const char *kProductRoot = "/data/service/el1/public/commuteagentservice";
constexpr const char *kDebugOutputRoot =
    "/data/service/el1/public/commuteagentservice/sa_sensor_test";
#endif
// HOME anchor is WGS84, same CRS as OnLocationReport → RawGpsLocation.
constexpr const char *kHomeAnchorId = "home_001";
constexpr double kHomeLatitude = 40.0539261;
constexpr double kHomeLongitude = 116.1735015;
constexpr double kHomeInsideRadiusMeters = 50.0;
constexpr double kHomeNearRadiusMeters = 150.0;
constexpr double kMaxUsableGpsAccuracyMeters = 80.0;
constexpr int kGpsStaleAfterSeconds = 120;
constexpr size_t kDebugEventQueueCap = 2000; // ≥ ~10 Hz * 10 s + motion/GPS margin
constexpr const char *kTimezoneId = "Asia/Shanghai";
constexpr const char *kSchemaVersion = "1.0";
constexpr const char *kGpsCoordinateSystem = "WGS84";

enum class MotionState {
    kUnknown = 0,
    kWalking = 1,
    kNotWalking = 2,
};

enum class MotionEventType {
    kWalkingStarted = 0,
    kWalkingStopped = 1,
};

struct MotionEvent {
    MotionEventType type = MotionEventType::kWalkingStarted;
    Timestamp timestamp = 0;       // source / wall-clock ms used for windowing
    Timestamp received_at = 0;     // SA ingress wall-clock ms
};

/**
 * Minimal PDR fields mirrored from AIPDR::XDR::PdrResult / SA PdrPoint.
 *
 * Coordinate system / units are NOT labeled in the PDR library headers.
 * - x/y: corrected relative coordinates from GetCx()/GetCy().
 * - Treated as local planar meters (x_m/y_m) for path metrics; see TODO in .cpp.
 * - observed_at: library GetTimestamp() as long, stored as int64_t ms in SA.
 */
struct RawPdrPoint {
    Timestamp observed_at = 0;
    double x = 0.0;
    double y = 0.0;
    double px = 0.0;
    double py = 0.0;
    double pz = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double azimuth = 0.0;
    double accuracy = 0.0;
    int32_t errorCode = 0;
    int32_t motionStatus = 0;
    int32_t directionInitialized = 0;
};

enum class PdrEpisodeState {
    kActive = 0,
    kEnded = 1,
};

struct PdrEpisode {
    std::string episode_id;
    PdrEpisodeState state = PdrEpisodeState::kActive;
    Timestamp started_at = 0;
    Timestamp ended_at = 0;
    bool has_ended_at = false;
    std::vector<RawPdrPoint> points;
};

/**
 * Latest GPS fix from Location::Location (OnLocationReport).
 * - latitude/longitude: WGS84 from GetLatitude()/GetLongitude().
 * - observed_at: SA wall-clock ms (GetTimestampMs()), not GNSS time-of-fix.
 * - received_at: business-module ingress wall-clock ms.
 */
struct RawGpsLocation {
    Timestamp observed_at = 0;
    Timestamp received_at = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double horizontal_accuracy_m = 0.0;
    bool has_horizontal_accuracy = false;
    bool valid = false;
    int32_t source_type = 0;
};

struct PerceptionInputSnapshot {
    Timestamp captured_at = 0;
    MotionState current_motion_state = MotionState::kUnknown;
    std::vector<MotionEvent> motion_events;
    bool has_active_pdr_episode = false;
    PdrEpisode active_pdr_episode;
    std::vector<PdrEpisode> completed_pdr_episodes;
    bool has_latest_gps = false;
    RawGpsLocation latest_gps;
};

// ---- SaPerceptionTick (pre-normalize) ----

struct ObservationWindow {
    Timestamp started_at = 0;
    Timestamp ended_at = 0;
    double duration_s = 0.0;
};

struct MotionWindowInput {
    MotionState state_at_window_start = MotionState::kUnknown;
    MotionState state_at_window_end = MotionState::kUnknown;
    std::vector<MotionEvent> events;
};

struct TickPdrPoint {
    uint64_t sequence = 0;
    Timestamp observed_at = 0;
    double x_m = 0.0;
    double y_m = 0.0;
};

struct PdrEpisodeInput {
    std::string episode_id;
    PdrEpisodeState state_at_window_end = PdrEpisodeState::kActive;
    Timestamp started_at = 0;
    Timestamp ended_at = 0;
    bool has_ended_at = false;
    std::vector<TickPdrPoint> points;
};

struct CollectionQuality {
    uint64_t dropped_pdr_points = 0;
    uint64_t out_of_order_pdr_points = 0;
    uint64_t duplicate_motion_events = 0;
};

/** Perception-layer power state (three modes). */
struct PowerStateFact {
    std::string mode;  // HIGH_WALKING | HIGH_STILL | LOW_POWER
    bool agent_inference_enabled = true;
};

struct SaPerceptionTick {
    std::string schema_version;
    std::string tick_id;
    Timestamp observed_at = 0;
    std::string timezone;
    ObservationWindow observation_window;
    MotionWindowInput motion;
    bool has_gps = false;
    RawGpsLocation gps;
    std::vector<PdrEpisodeInput> pdr_episodes;
    CollectionQuality collection_quality;
    PowerStateFact power_state;
};

// ---- SemanticSnapshot (post-normalize; no time_context) ----

struct SemanticMotion {
    std::string fact_id;
    std::string state;       // WALKING | NOT_WALKING | UNKNOWN
    std::string transition;  // NONE | STARTED | CONTINUING | ENDED | STARTED_AND_ENDED
};

struct HomeRelation {
    std::string fact_id;
    std::string anchor_id;
    bool has_distance_m = false;
    double distance_m = 0.0;
    bool has_gps_accuracy_m = false;
    double gps_accuracy_m = 0.0;
    bool has_gps_observed_at = false;
    Timestamp gps_observed_at = 0;
    bool has_gps_age_s = false;
    double gps_age_s = 0.0;
    std::string relation;  // INSIDE | NEAR | OUTSIDE | UNKNOWN
    std::string quality;   // USABLE | STALE | POOR_ACCURACY | MISSING | INVALID
};

struct PdrWindowMetrics {
    double walking_duration_s = 0.0;
    double path_length_m = 0.0;
    int64_t point_count = 0;
};

struct PdrCumulativeMetrics {
    bool has_walking_duration_s = false;
    double walking_duration_s = 0.0;
    bool has_path_length_m = false;
    double path_length_m = 0.0;
    bool has_net_displacement_m = false;
    double net_displacement_m = 0.0;
    bool has_straightness_ratio = false;
    double straightness_ratio = 0.0;
    int64_t point_count = 0;
};

struct SemanticPdr {
    std::string fact_id;
    bool has_episode_id = false;
    std::string episode_id;
    std::string transition;
    bool active_at_window_start = false;
    bool active_at_window_end = false;
    bool has_started_at = false;
    Timestamp started_at = 0;
    bool has_ended_at = false;
    Timestamp ended_at = 0;
    PdrWindowMetrics window;
    PdrCumulativeMetrics cumulative;
    std::string quality;
};

struct DataQuality {
    std::string fact_id;
    std::string overall;  // GOOD | PARTIAL | INSUFFICIENT
    std::vector<std::string> missing_fields;
    std::vector<std::string> issues;
};

struct SemanticSnapshot {
    std::string schema_version;
    std::string snapshot_id;
    std::string tick_id;
    Timestamp observed_at = 0;
    std::string timezone;
    ObservationWindow observation_window;
    SemanticMotion motion;
    HomeRelation home_relation;
    SemanticPdr pdr;
    DataQuality data_quality;
    PowerStateFact power_state;
    // Intentionally no time_context.
};

struct AgentInvokeResult {
    bool implemented = false;
    std::string status = "NotImplemented";
    std::string tick_id;
    std::string request_id;
    std::string session_id;
    Timestamp invoked_at = 0;
    int error_code = 0;
    std::string response_message;
    std::string stream_payload;
};

struct ValidationResult {
    bool implemented = false;
    std::string status = "NotImplemented";
};

struct DebugDeliveryPayload {
    bool implemented = false;
    std::string tick_id;
    std::string scene;
    std::string service_intent;
    std::string message;
    Timestamp observed_at = 0;
    double score_home = 0.0;
    double score_company = 0.0;
};

enum class SensorDebugEventType {
    kWalkingStarted = 0,
    kWalkingStopped = 1,
    kPdrPoint = 2,
    kGpsReport = 3,
};

struct SensorDebugEvent {
    uint64_t sequence_id = 0;
    Timestamp received_at = 0;
    Timestamp source_observed_at = 0;
    SensorDebugEventType event_type = SensorDebugEventType::kPdrPoint;
    MotionState motion_state = MotionState::kUnknown;
    std::string episode_id;
    std::string payload_json;
    std::string power_mode;  // HIGH_WALKING | HIGH_STILL | LOW_POWER
};

class ProactiveAgentBusinessModule {
public:
    static ProactiveAgentBusinessModule &GetInstance();

    void Initialize();
    void Shutdown();
    /** Sleep power-save: stop tick/LLM feeding without tearing down agent. */
    void PausePerception();
    void ResumePerception();
    bool IsPerceptionPaused() const;
    /** Update tick cadence + whether InvokeAgent runs (disabled in LOW_POWER). */
    void ApplyPowerPolicy(int64_t tickIntervalMs, bool agentInferenceEnabled);

    void OnWalkingStarted(int64_t timestampMs);
    void OnWalkingStopped(int64_t timestampMs);
    void OnPdrPoint(const RawPdrPoint &point);
    void OnGpsLocation(const RawGpsLocation &location);

    /** Value-copy consistent snapshot; does not clear internal state. */
    PerceptionInputSnapshot CapturePerceptionInput() const;

    void StartPeriodicScheduler();
    void StopPeriodicScheduler();
    void ProcessTick();
    /** Core tick entry: freeze window ending at windowEndMs. Safe for unit tests. */
    void ProcessTickAt(Timestamp windowEndMs);

    SemanticSnapshot NormalizePerceptionInput(const SaPerceptionTick &input);
    /**
     * Invoke leaving-home baseline agent with one semantic snapshot JSON frame
     * (includes home_relation). Host builds without jiuwen return NotImplemented when "ready".
     */
    AgentInvokeResult InvokeAgent(const std::string &tickId, const std::string &semanticSnapshotJson);
    ValidationResult ValidateAgentResponse(const AgentInvokeResult &response);
    void PushDebugToHap(const DebugDeliveryPayload &payload);

    /** Last produced tick / snapshot (for tests / future Agent). */
    bool HasLastTick() const;
    SaPerceptionTick GetLastTick() const;
    bool HasLastSnapshot() const;
    SemanticSnapshot GetLastSnapshot() const;
    std::string GetDebugRunDirectory() const;

    /** Agent lifecycle status for tests / future invoke (no secrets). */
    bool IsAgentReady() const;
    std::string GetAgentInitStatus() const;

private:
    ProactiveAgentBusinessModule() = default;
    ~ProactiveAgentBusinessModule();
    ProactiveAgentBusinessModule(const ProactiveAgentBusinessModule &) = delete;
    ProactiveAgentBusinessModule &operator=(const ProactiveAgentBusinessModule &) = delete;

    bool AcceptingInputLocked() const;
    void ResetStateLocked();
    void ProcessTickAtInner(Timestamp windowEndMs);
    AgentInvokeResult InvokeAgentInner(const std::string &tickId, const std::string &semanticSnapshotJson);
    std::string MakeEpisodeIdLocked(Timestamp startedAt);
    void AppendDebugEventLocked(SensorDebugEvent event);
    bool InitDebugOutputsLocked();
    void CloseDebugOutputsLocked();
    void FlushDebugEventsUnlocked(std::vector<SensorDebugEvent> events);
    void WriteSensorEventsCsv(const std::vector<SensorDebugEvent> &events);
    void WriteTickCsv(const SaPerceptionTick &tick, const std::string &tickJson);
    void WriteSnapshotCsv(const SemanticSnapshot &snap, const std::string &snapJson);
    void WriteAgentResponseCsv(const AgentInvokeResult &result);
    void SchedulerLoop();
    std::string ResolveAgentEnvFilePath() const;
    void TryInitializeAgent();
    void ShutdownAgent();

    mutable std::mutex mutex_;
    bool initialized_ = false;
    bool acceptingInput_ = false;
    bool perceptionPaused_ = false;
    bool agentInferenceEnabled_ = true;
    std::atomic<int64_t> tickIntervalMs_ { static_cast<int64_t>(kDebugTickIntervalSeconds) * 1000 };

    MotionState currentMotionState_ = MotionState::kUnknown;
    MotionState motionStateAtWindowStart_ = MotionState::kUnknown;
    std::vector<MotionEvent> motionEvents_;

    bool hasActiveEpisode_ = false;
    PdrEpisode activeEpisode_;
    std::vector<PdrEpisode> completedEpisodes_;

    bool hasLatestGps_ = false;
    RawGpsLocation latestGps_;

    int64_t episodeSequence_ = 0;
    uint64_t tickSequence_ = 0;
    uint64_t debugEventSequence_ = 0;

    uint64_t windowDroppedPdr_ = 0;
    uint64_t windowOutOfOrderPdr_ = 0;
    uint64_t windowDuplicateMotion_ = 0;
    int64_t ignoredPdrWithoutEpisode_ = 0;
    int64_t discardedOutOfOrderPdr_ = 0;

    Timestamp windowStartMs_ = 0;
    std::vector<SensorDebugEvent> pendingDebugEvents_;

    std::string debugRunDir_;
    std::ofstream sensorEventsFile_;
    std::ofstream ticksFile_;
    std::ofstream snapshotsFile_;
    std::ofstream agentResponsesFile_;
    std::ofstream baselineDecisionsFile_;
    bool sensorEventsEnabled_ = false;
    bool ticksEnabled_ = false;
    bool snapshotsEnabled_ = false;
    bool agentResponsesEnabled_ = false;
    bool baselineDecisionsEnabled_ = false;
    bool headerSensorWritten_ = false;
    bool headerTicksWritten_ = false;
    bool headerSnapshotsWritten_ = false;
    bool headerAgentResponsesWritten_ = false;
    bool headerBaselineWritten_ = false;
    /** From agent.env; verbose dumps only when SA_AGENT_DEBUG_SINKS=1. */
    bool debugSinksConfig_ = false;
    std::string lastBaselineScene_;
    bool hasLastBaselineScene_ = false;

    bool hasLastTick_ = false;
    SaPerceptionTick lastTick_;
    bool hasLastSnapshot_ = false;
    SemanticSnapshot lastSnapshot_;

    std::mutex schedulerMutex_;
    std::condition_variable schedulerCv_;
    std::thread schedulerThread_;
    std::atomic<bool> schedulerStop_ {true};
    std::atomic<bool> schedulerRunning_ {false};
    std::mutex tickSerialMutex_;

    std::shared_ptr<jiuwen::Agent> agent_;
    std::string agentInitStatus_;
    AgentRuntimeEnvironment agentEnvironment_;
    std::string agentSessionId_;
    bool agentConfigured_ = false;
};

} // namespace OHOS::Multimedia::CameraAgentService

#endif // PROACTIVE_AGENT_BUSINESS_MODULE_H
