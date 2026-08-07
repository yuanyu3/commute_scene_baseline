#include "commute_sa/baseline_runtime.h"

#include "commute_sa/product_store.h"

#include <chrono>

namespace commute_sa {
namespace {

int64_t NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

BaselineRuntime &BaselineRuntime::GetInstance()
{
    static BaselineRuntime inst;
    return inst;
}

void BaselineRuntime::Init(const std::string &anchorsPath, const std::string &thetaPath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    anchorsPath_ = anchorsPath;
    thetaPath_ = thetaPath;
    AnchorSet anchors = DefaultAnchors();
    Theta theta = DefaultTheta();
    if (!anchorsPath.empty()) {
        LoadAnchorsFromFile(anchorsPath, &anchors, nullptr);
    }
    if (!thetaPath.empty()) {
        LoadThetaFromFile(thetaPath, &theta, nullptr);
    }
    delete engine_;
    engine_ = new SceneEngine(anchors, theta);
    inited_ = true;
}

bool BaselineRuntime::Enabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_ && inited_ && engine_ != nullptr;
}

bool BaselineRuntime::Walking() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return walking_;
}

SceneEngine *BaselineRuntime::Engine()
{
    return engine_;
}

void BaselineRuntime::OnWalkingStarted(int64_t tsMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    walking_ = true;
    hasWalkStarted_ = true;
    walkStartedAtMs_ = tsMs;
}

void BaselineRuntime::OnWalkingStopped(int64_t /*tsMs*/)
{
    std::lock_guard<std::mutex> lock(mutex_);
    walking_ = false;
    hasWalkStarted_ = false;
}

TickDecision BaselineRuntime::OnTick(
    int64_t tMs, bool hasGps, double lat, double lon, double accM, bool gpsValid)
{
    std::lock_guard<std::mutex> lock(mutex_);
    TickDecision empty;
    if (!inited_ || engine_ == nullptr || !enabled_) {
        return empty;
    }
    TickFeatures feat;
    feat.t_ms = tMs;
    feat.has_gps = hasGps && gpsValid;
    feat.lat = lat;
    feat.lon = lon;
    feat.acc = accM;
    feat.walking = walking_;
    feat.has_walk_started = hasWalkStarted_;
    feat.walk_started_at_ms = walkStartedAtMs_;
    return engine_->Step(feat);
}

bool BaselineRuntime::PersistTheta() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (engine_ == nullptr) {
        return false;
    }
    return ProductStore::GetInstance().SaveTheta(engine_->GetTheta());
}

bool BaselineRuntime::ApplyThetaDeltaAndPersist(const std::string &param, double delta, const std::string &reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (engine_ == nullptr) {
        return false;
    }
    Theta t = engine_->GetTheta();
    auto readV = [&](const Theta &th, double *out) -> bool {
        if (param == "enter_leave") {
            *out = th.enter_leave;
            return true;
        }
        if (param == "exit_leave") {
            *out = th.exit_leave;
            return true;
        }
        if (param == "w_walk") {
            *out = th.w_walk;
            return true;
        }
        if (param == "w_radio") {
            *out = th.w_radio;
            return true;
        }
        if (param == "min_evidence") {
            *out = static_cast<double>(th.min_evidence);
            return true;
        }
        if (param == "weekday_leave_home_hour") {
            *out = th.weekday_leave_home_hour;
            return true;
        }
        if (param == "weekday_leave_company_hour") {
            *out = th.weekday_leave_company_hour;
            return true;
        }
        if (param == "arm_delay_s") {
            *out = th.arm_delay_s;
            return true;
        }
        return false;
    };
    double oldV = 0.0;
    if (!readV(t, &oldV)) {
        return false;
    }
    if (!ApplyThetaDelta(&t, param, delta, nullptr)) {
        return false;
    }
    double newV = oldV;
    readV(t, &newV);
    engine_->SetTheta(t);
    ProductStore::GetInstance().AppendParamChange(NowMs(), param, oldV, newV, reason);
    return ProductStore::GetInstance().SaveTheta(t);
}

}  // namespace commute_sa
