#pragma once

#include "commute_sa/scene_engine.h"
#include "commute_sa/theta.h"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

namespace commute_sa {

/**
 * Minimal on-device persistence for leave-home accuracy loop.
 * Not helloworld debug dumps — only what SceneEngine + θ personalize need.
 *
 * Layout under root (default /data/.../commuteagentservice):
 *   anchors.json, theta.json
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

    /** After settle window: CONFIRMED_LEAVE | FALSE_PUSH | UNKNOWN */
    void AppendLeaveLabel(int64_t tLabelMs, int64_t tPushMs, const std::string &label, const std::string &homeRelation,
        bool hasDistHome, double distHomeM);

    /**
     * While a leave episode is active, observe relation each tick.
     * Records first OUTSIDE as t* for lead_s = (t* - t_push).
     */
    void ObserveLeaveProgress(int64_t tMs, const std::string &homeRelation);

    void AppendSparseSample(int64_t tMs, int64_t tPushMs, double lat, double lon, double acc, bool walking,
        const std::string &homeRelation, double distHomeM);

    void AppendPersonalizeJob(int64_t createdAtMs, const std::string &reason, const std::string &lastIntent,
        const std::string &lastScene, int64_t lastPushAtMs, const Theta &theta);

    void AppendParamChange(int64_t tMs, const std::string &param, double oldValue, double newValue,
        const std::string &reason);

    /** Agent write_audit → audit.jsonl; returns audit_id or empty on failure. */
    std::string AppendAudit(int64_t tMs, const std::string &message, const std::string &changesJson);

    /** Queue offline anchor re-inference; returns job_id. */
    std::string AppendAnchorReestimateJob(int64_t tMs, const std::string &which);

    bool SaveTheta(const Theta &theta) const;
    bool SaveAnchors(const AnchorSet &anchors) const;

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
    int64_t activePushMs_ = 0;
    int64_t tStarOutsideMs_ = 0;  // first OUTSIDE after active push (t*)
    uint64_t auditSeq_ = 0;
    uint64_t anchorJobSeq_ = 0;
};

}  // namespace commute_sa
