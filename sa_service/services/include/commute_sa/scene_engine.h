#pragma once

#include "commute_sa/anchors.h"
#include "commute_sa/geo.h"
#include "commute_sa/theta.h"

#include <cstdint>
#include <optional>
#include <string>

namespace commute_sa {

enum class Scene {
    kAtHome = 0,
    kLeavingHome,
    kAtCompany,
    kLeavingCompany,
    kCommute,
    kAway,
    kUnknown,
};

inline const char *SceneToString(Scene s)
{
    switch (s) {
        case Scene::kAtHome:
            return "AT_HOME";
        case Scene::kLeavingHome:
            return "LEAVING_HOME";
        case Scene::kAtCompany:
            return "AT_COMPANY";
        case Scene::kLeavingCompany:
            return "LEAVING_COMPANY";
        case Scene::kCommute:
            return "COMMUTE";
        case Scene::kAway:
            return "AWAY";
        default:
            return "UNKNOWN";
    }
}

/** Wall-clock ms since epoch (same as SA Timestamp). */
using TickTsMs = int64_t;

struct TickFeatures {
    TickTsMs t_ms = 0;
    bool has_gps = false;
    double lat = 0.0;
    double lon = 0.0;
    double acc = 0.0;
    bool walking = false;
    bool has_walk_started = false;
    TickTsMs walk_started_at_ms = 0;
    double pdr_net_out_home_m = 0.0;
    double pdr_net_out_company_m = 0.0;
    bool wifi_home_detach = false;
    bool wifi_company_detach = false;
    bool cell_leave_home = false;
    bool cell_leave_company = false;
    bool ble_home_detach = false;
    bool ble_company_detach = false;
};

struct TickDecision {
    Scene scene = Scene::kUnknown;
    double score_home = 0.0;
    double score_company = 0.0;
    Relation home_relation = Relation::kUnknown;
    Relation company_relation = Relation::kUnknown;
    bool has_dist_home = false;
    double dist_home_m = 0.0;
    bool has_dist_company = false;
    double dist_company_m = 0.0;
    bool should_service = false;
    std::string service_intent = "NONE";
    int hits_home = 0;
    int hits_company = 0;
    std::string uncertainty = "MEDIUM";
    /** Estimated seconds until crossing r_out (predictive leave). <0 if unknown. */
    double eta_leave_s = -1.0;
    /** True when ETA ≤ lead_max_s (or ETA unknown). lead_min is post-hoc / agent only. */
    bool lead_gate_ok = false;
    /** Why push was blocked: NONE | COOLDOWN | UNCERTAIN | OUTSIDE | LEAD_EARLY | ALREADY_PUSHED */
    std::string push_block_reason = "NONE";
};

/**
 * Realtime leave-home / leave-company scorer + FSM.
 * Push is predictive: only while still INSIDE/NEAR and ETA in lead window.
 */
class SceneEngine {
public:
    SceneEngine(AnchorSet anchors, Theta theta = DefaultTheta());

    void SetTheta(const Theta &theta);
    const Theta &GetTheta() const;
    void SetAnchors(const AnchorSet &anchors);
    const AnchorSet &GetAnchors() const;
    Scene CurrentScene() const;

    TickDecision Step(const TickFeatures &feat);

private:
    struct ScoreResult {
        double score = 0.0;
        int hits = 0;
    };

    ScoreResult ScoreLeaving(const TickFeatures &feat, Relation rel, bool hasDist, double distM, double rIn,
        double rOut, double pdrNetOut, bool wifiDetach, bool cellLeave, bool bleDetach, double centerHour,
        std::optional<double> prevDist) const;

    /** Seconds until dist reaches rOut; nullopt if not outbound / unknown. */
    std::optional<double> EstimateEtaOutS(bool hasDist, double distM, double rOut, bool walking, double pdrNetOut,
        std::optional<double> prevDist, std::optional<TickTsMs> prevT, TickTsMs tMs) const;

    bool LeadWindowOk(const std::optional<double> &etaS, std::string *blockReason) const;

    Relation RelTo(const TickFeatures &feat, const Anchor &anchor, double *distOut) const;
    bool CooldownOk(TickTsMs tMs) const;
    bool ArmDelayOk(const TickFeatures &feat) const;

    AnchorSet anchors_;
    Theta theta_;
    Scene scene_ = Scene::kUnknown;
    std::optional<double> prevDistHome_;
    std::optional<double> prevDistCompany_;
    std::optional<TickTsMs> prevTMs_;
    std::optional<TickTsMs> leaveHomeSince_;
    std::optional<TickTsMs> leaveCompanySince_;
    std::optional<TickTsMs> lastPushAt_;
    std::optional<TickTsMs> outsideHomeSince_;
    std::optional<TickTsMs> outsideCompanySince_;
    /** One push per leave episode; reset when back to AT_* */
    bool leaveHomePushed_ = false;
    bool leaveCompanyPushed_ = false;
};

}  // namespace commute_sa
