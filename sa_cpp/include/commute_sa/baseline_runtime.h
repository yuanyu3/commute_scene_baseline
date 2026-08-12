#pragma once

#include "commute_sa/pdr_evidence.h"
#include "commute_sa/radio_evidence.h"
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

    /** Feed WiFi/CELL/BLE into RadioEvidence (thread-safe vs OnTick). */
    void OnWifiScan(const WifiScanSample &scan);
    void OnCellSample(const CellSample &cell);
    void OnBleSample(const BleSample &ble);

    /** Feed PDR local planar meters (thread-safe vs OnTick). */
    void OnPdrPoint(int64_t tMs, double xM, double yM);

    TickDecision OnTick(int64_t tMs, bool hasGps, double lat, double lon, double accM, bool gpsValid);

    SceneEngine *Engine();
    RadioEvidence *Radio();
    PdrEvidence *Pdr();
    bool Enabled() const;
    bool Walking() const;

    /** Persist current θ after personalize apply. */
    bool PersistTheta() const;
    bool ApplyThetaDeltaAndPersist(const std::string &param, double delta, const std::string &reason);
    bool ApplyPersonalizationPolicyAndPersist(const PersonalizationPolicy &policy);

    std::string RadioDebugJson(int64_t tMs) const;
    std::string PdrDebugJson() const;

private:
    BaselineRuntime() = default;

    mutable std::mutex mutex_;
    bool enabled_ = true;
    bool inited_ = false;
    bool walking_ = false;
    bool hasWalkStarted_ = false;
    int64_t walkStartedAtMs_ = 0;
    SceneEngine *engine_ = nullptr;
    RadioEvidence radio_;
    PdrEvidence pdr_;
    std::string anchorsPath_;
    std::string thetaPath_;
};

}  // namespace commute_sa
