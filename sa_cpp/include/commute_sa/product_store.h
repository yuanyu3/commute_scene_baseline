#pragma once

#include "commute_sa/scene_engine.h"
#include "commute_sa/theta.h"

#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace commute_sa {

/**
 * Minimal on-device persistence for leave-home accuracy loop.
 * Not helloworld debug dumps — only what SceneEngine + θ personalize need.
 *
 * Layout under root (default /data/.../commuteagentservice):
 *   anchors.json, theta.json
 *   radio_soft.json          // semi-persistent WiFi soft dwell sets
 *   leave_episodes.jsonl
 *   param_changes.jsonl
 *   personalize_jobs.jsonl
 *   audit.jsonl
 *   anchor_reestimate_jobs.jsonl
 */
class ProductStore {
public:
    static ProductStore &GetInstance();

    /** Creates root if needed; opens append jsonl files. */
    bool Init(const std::string &rootDir);

    const std::string &RootDir() const;

    void AppendLeavePush(int64_t tPushMs, const std::string &intent, const std::string &scene, double scoreHome,
        double scoreCompany, bool hasDistHome, double distHomeM, bool walking, double enterLeave, int minEvidence,
        const std::string &homeRelation = "", double etaLeaveS = -1.0);

    /** CONFIRMED_LEAVE | FALSE_PUSH | MISSED_LEAVE | UNKNOWN */
    void AppendLeaveLabel(int64_t tLabelMs, int64_t tPushMs, const std::string &label, const std::string &homeRelation,
        bool hasDistHome, double distHomeM);

    /**
     * While a leave episode is active, observe relations each tick.
     * Records first OUTSIDE on the push anchor as t* for lead_s = (t* - t_push).
     * @return true iff this call newly recorded t* (first OUTSIDE).
     */
    bool ObserveLeaveProgress(int64_t tMs, const std::string &homeRelation, const std::string &companyRelation = "");

    /**
     * Self-label missed leave: sustained OUTSIDE without a prior push in lookback.
     * Writes MISSED_LEAVE to leave_episodes.jsonl at most once per away bout.
     * @return true when a new MISSED_LEAVE label was written.
     */
    bool ObserveMissedLeave(int64_t tMs, const std::string &homeRelation, const std::string &companyRelation,
        double awayConfirmS, double lookbackS, const std::string &focusSide, std::string *sideOut = nullptr);

    void AppendSparseSample(int64_t tMs, int64_t tPushMs, double lat, double lon, double acc, bool walking,
        const std::string &homeRelation, double distHomeM);

    /** Buffer semantic candidate ticks; they are persisted with a label when an episode settles. */
    void ObservePolicyFeatures(const TickFeatures &features, const TickDecision &decision);

    void AppendPersonalizeJob(int64_t createdAtMs, const std::string &reason, const std::string &lastIntent,
        const std::string &lastScene, int64_t lastPushAtMs, const Theta &theta);

    void AppendParamChange(int64_t tMs, const std::string &param, double oldValue, double newValue,
        const std::string &reason);

    /** Agent write_audit → audit.jsonl; returns audit_id or empty on failure. */
    std::string AppendAudit(int64_t tMs, const std::string &message, const std::string &changesJson);

    /**
     * In-memory recent θ deltas since sinceMs (inclusive), for debug HAP after personalize.
     * Returns a JSON array string.
     */
    std::string GetRecentParamChangesJson(int64_t sinceMs, int limit = 20) const;

    /** In-memory recent audits since sinceMs. Returns a JSON array string. */
    std::string GetRecentAuditsJson(int64_t sinceMs, int limit = 10) const;

    /** Queue anchor re-inference; returns job_id. Executor: ProcessQueuedAnchorReestimateJobs. */
    std::string AppendAnchorReestimateJob(int64_t tMs, const std::string &which);

    /** Pop next queued (in-memory) anchor job. */
    bool PopQueuedAnchorJob(std::string *jobId, std::string *which, int64_t *tMs);

    /** Append status line for a job (done|failed|...). */
    void AppendAnchorJobStatus(const std::string &jobId, const std::string &status, const std::string &detail);

    bool SaveTheta(const Theta &theta) const;
    bool SaveAnchors(const AnchorSet &anchors) const;

    /** Semi-persistent WiFi soft dwell sets (radio_soft.json). */
    bool SaveRadioSoftJson(const std::string &json) const;
    bool LoadRadioSoftJson(std::string *out) const;

private:
    ProductStore() = default;
    void AppendLine(std::ofstream &f, const std::string &line);

    mutable std::mutex mutex_;
    std::string root_;
    bool inited_ = false;
    std::ofstream leaveEpisodes_;
    std::ofstream paramChanges_;
    std::ofstream personalizeJobs_;
    std::ofstream sparseSamples_;  // leave window GPS/walk only
    std::ofstream auditLog_;
    std::ofstream anchorJobs_;
    std::ofstream policyHistory_;
    int64_t activePushMs_ = 0;
    std::string activeIntent_;
    int64_t tStarOutsideMs_ = 0;  // first OUTSIDE after active push (t*)
    int64_t lastHomePushMs_ = 0;
    int64_t lastCompanyPushMs_ = 0;
    int64_t outsideHomeSinceMs_ = 0;
    int64_t outsideCompanySinceMs_ = 0;
    bool seenInsideHome_ = false;
    bool seenInsideCompany_ = false;
    bool missedHomeEmitted_ = false;
    bool missedCompanyEmitted_ = false;
    uint64_t auditSeq_ = 0;
    uint64_t anchorJobSeq_ = 0;

    struct PolicyHistoryRow {
        int64_t t_ms = 0;
        std::string side;
        double preleave_probability = 0.0;
        double leaving_probability = 0.0;
        int hits = 0;
        bool walking = false;
        double pdr_net_out_m = 0.0;
        bool wifi_detach = false;
        bool cell_leave = false;
        bool ble_detach = false;
        bool has_usable_gps = false;
        bool baro_available = false;
        bool baro_baseline_ready = false;
        double baro_descent_m = 0.0;
        bool baro_stable_platform = false;
        bool baro_lower_platform = false;
        LeaveObservation hsmm_obs;
    };
    std::deque<PolicyHistoryRow> policyBuffer_;
    int64_t lastPolicySampleMs_ = 0;
    void FlushPolicyHistoryLocked(const std::string &side, const std::string &label, int64_t outcomeMs);

    struct PendingAnchorJob {
        std::string job_id;
        std::string which;
        int64_t t_ms = 0;
    };
    std::deque<PendingAnchorJob> pendingAnchorJobs_;

    struct RecentParamChange {
        int64_t t_ms = 0;
        std::string param;
        double old_value = 0.0;
        double new_value = 0.0;
        std::string reason;
    };
    struct RecentAudit {
        int64_t t_ms = 0;
        std::string audit_id;
        std::string message;
        std::string changes_json;
    };
    static constexpr size_t kRecentChangeCap = 64;
    static constexpr size_t kRecentAuditCap = 32;
    std::deque<RecentParamChange> recentParamChanges_;
    std::deque<RecentAudit> recentAudits_;
};

}  // namespace commute_sa
