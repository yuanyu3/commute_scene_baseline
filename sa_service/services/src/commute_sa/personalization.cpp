#include "commute_sa/personalization.h"

#include <ctime>

namespace commute_sa {
namespace {

void FillLocalTm(int64_t tMs, std::tm *tmValue)
{
    std::time_t sec = static_cast<std::time_t>(tMs / 1000);
#if defined(_WIN32)
    localtime_s(tmValue, &sec);
#else
    localtime_r(&sec, tmValue);
#endif
}

int LocalDayKey(int64_t tMs)
{
    std::tm tmValue {};
    FillLocalTm(tMs, &tmValue);
    return tmValue.tm_yday + tmValue.tm_year * 400;
}

int LocalHour(int64_t tMs)
{
    std::tm tmValue {};
    FillLocalTm(tMs, &tmValue);
    return tmValue.tm_hour;
}

}  // namespace

PersonalizationController &PersonalizationController::GetInstance()
{
    static PersonalizationController inst;
    return inst;
}

void PersonalizationController::SetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = enabled;
}

bool PersonalizationController::Enabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

void PersonalizationController::SetCooldownSec(double sec)
{
    std::lock_guard<std::mutex> lock(mutex_);
    cooldownSec_ = sec;
}

void PersonalizationController::SetAfterPushSettleSec(double sec)
{
    std::lock_guard<std::mutex> lock(mutex_);
    afterPushSettleSec_ = sec;
}

void PersonalizationController::OnBaselinePush(int64_t tMs, const std::string &intent, const std::string &scene)
{
    std::lock_guard<std::mutex> lock(mutex_);
    lastPushAtMs_ = tMs;
    lastIntent_ = intent;
    lastScene_ = scene;
    pushPendingSettle_ = true;
}

bool PersonalizationController::MaybeEnqueueJob(int64_t nowMs, const Theta &currentTheta)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_ || hasJob_) {
        return false;
    }
    if (lastJobAtMs_ > 0 &&
        (nowMs - lastJobAtMs_) < static_cast<int64_t>(cooldownSec_ * 1000.0)) {
        return false;
    }

    std::string reason;
    if (pushPendingSettle_ && lastPushAtMs_ > 0 &&
        (nowMs - lastPushAtMs_) >= static_cast<int64_t>(afterPushSettleSec_ * 1000.0)) {
        reason = "AFTER_PUSH";
        pushPendingSettle_ = false;
    } else if (LocalHour(nowMs) >= 22 && LocalDayKey(nowMs) != lastJobDayKey_) {
        // Evening batch: at most once per local calendar day.
        reason = "DAY_END";
    } else {
        return false;
    }

    pending_ = PersonalizeJob {};
    pending_.created_at_ms = nowMs;
    pending_.reason = reason;
    pending_.last_intent = lastIntent_;
    pending_.last_scene = lastScene_;
    pending_.last_push_at_ms = lastPushAtMs_;
    pending_.theta_snapshot = currentTheta;
    hasJob_ = true;
    lastJobAtMs_ = nowMs;
    lastJobDayKey_ = LocalDayKey(nowMs);
    return true;
}

bool PersonalizationController::HasPendingJob() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return hasJob_;
}

bool PersonalizationController::PopPendingJob(PersonalizeJob *out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasJob_ || out == nullptr) {
        return false;
    }
    *out = pending_;
    hasJob_ = false;
    return true;
}

}  // namespace commute_sa
