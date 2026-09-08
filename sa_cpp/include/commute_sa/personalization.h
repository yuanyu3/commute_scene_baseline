#pragma once

#include "commute_sa/theta.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace commute_sa {

/**
 * Online LLM is ONLY for θ personalization (rare), never for per-tick scene labels.
 * Gate + job queue; full Jiuwen Tool agent can consume pending jobs.
 */
struct PersonalizeJob {
    int64_t created_at_ms = 0;
    std::string reason;  // AFTER_PUSH | DAY_END | MANUAL
    std::string last_intent;
    std::string last_scene;
    int64_t last_push_at_ms = 0;
    /** CONFIRMED_LEAVE | ABORTED_LEAVE | FALSE_PUSH | MISSED_LEAVE | empty (DAY_END / MANUAL) */
    std::string settle_label;
    Theta theta_snapshot;
};

class PersonalizationController {
public:
    static PersonalizationController &GetInstance();

    void SetEnabled(bool enabled);
    bool Enabled() const;

    /** Record a SceneEngine push for later self-label / personalize trigger. */
    void OnBaselinePush(int64_t tMs, const std::string &intent, const std::string &scene);

    /**
     * First OUTSIDE after a pending push → allow immediate AFTER_PUSH personalize
     * (CONFIRMED_LEAVE) without waiting for the false-push settle timeout.
     */
    void OnOutsideObserved(int64_t tMs);

    /** Sustained OUTSIDE with no prior push → enqueue MISSED_LEAVE personalize. */
    void OnMissedLeave(int64_t tMs, const std::string &side);

    /**
     * Call from SA tick (cheap). Returns true if a personalize job was enqueued.
     * AFTER_PUSH fires on: first OUTSIDE (confirmed), or settle timeout (false push).
     * Also DAY_END (≥22:00, once per local day).
     */
    bool MaybeEnqueueJob(int64_t nowMs, const Theta &currentTheta);

    bool HasPendingJob() const;
    bool PopPendingJob(PersonalizeJob *out);

    /** Min seconds between personalize LLM runs. */
    void SetCooldownSec(double sec);
    /** Seconds after a push before FALSE_PUSH settle if never OUTSIDE. */
    void SetAfterPushSettleSec(double sec);

private:
    PersonalizationController() = default;

    mutable std::mutex mutex_;
    bool enabled_ = true;
    double cooldownSec_ = 6 * 3600.0;      // 6h
    double afterPushSettleSec_ = 1200.0;   // 20 min false-push timeout
    int64_t lastJobAtMs_ = 0;
    int lastJobDayKey_ = -1;
    int64_t lastPushAtMs_ = 0;
    std::string lastIntent_;
    std::string lastScene_;
    bool pushPendingSettle_ = false;
    bool outsideConfirmed_ = false;
    bool missedLeavePending_ = false;
    std::string missedLeaveSide_;
    int64_t missedLeaveAtMs_ = 0;
    bool hasJob_ = false;
    PersonalizeJob pending_;
};

}  // namespace commute_sa
