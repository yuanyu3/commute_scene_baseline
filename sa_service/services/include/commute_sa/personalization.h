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
     * Call from SA tick (cheap). Returns true if a personalize job was enqueued.
     * Conditions: enabled, cooldown elapsed, and (after-push settle OR day-boundary).
     */
    bool MaybeEnqueueJob(int64_t nowMs, const Theta &currentTheta);

    bool HasPendingJob() const;
    bool PopPendingJob(PersonalizeJob *out);

    /** Min seconds between personalize LLM runs. */
    void SetCooldownSec(double sec);
    /** Seconds after a push before we allow AFTER_PUSH personalize. */
    void SetAfterPushSettleSec(double sec);

private:
    PersonalizationController() = default;

    mutable std::mutex mutex_;
    bool enabled_ = true;
    double cooldownSec_ = 6 * 3600.0;      // 6h
    double afterPushSettleSec_ = 1200.0;   // 20 min
    int64_t lastJobAtMs_ = 0;
    int lastJobDayKey_ = -1;
    int64_t lastPushAtMs_ = 0;
    std::string lastIntent_;
    std::string lastScene_;
    bool pushPendingSettle_ = false;
    bool hasJob_ = false;
    PersonalizeJob pending_;
};

}  // namespace commute_sa
