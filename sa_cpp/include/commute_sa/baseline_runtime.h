#pragma once

#include "commute_sa/scene_engine.h"

#include <mutex>
#include <string>

namespace commute_sa {

class BaselineRuntime {
public:
    static BaselineRuntime &GetInstance();

    /** Call once from Initialize(). Paths may be empty → defaults. */
    void Init(const std::string &anchorsPath, const std::string &thetaPath);

    void OnWalkingStarted(int64_t tsMs);
    void OnWalkingStopped(int64_t tsMs);

    TickDecision OnTick(int64_t tMs, bool hasGps, double lat, double lon, double accM, bool gpsValid);

    SceneEngine *Engine();
    bool Enabled() const;
    bool Walking() const;

    /** Persist current θ after personalize apply. */
    bool PersistTheta() const;
    bool ApplyThetaDeltaAndPersist(const std::string &param, double delta, const std::string &reason);

private:
    BaselineRuntime() = default;

    mutable std::mutex mutex_;
    bool enabled_ = true;
    bool inited_ = false;
    bool walking_ = false;
    bool hasWalkStarted_ = false;
    int64_t walkStartedAtMs_ = 0;
    SceneEngine *engine_ = nullptr;
    std::string anchorsPath_;
    std::string thetaPath_;
};

}  // namespace commute_sa
