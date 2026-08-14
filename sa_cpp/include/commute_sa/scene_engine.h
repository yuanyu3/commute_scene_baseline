#pragma once

#include "commute_sa/anchors.h"
#include "commute_sa/geo.h"
#include "commute_sa/leave_hsmm.h"
#include "commute_sa/personalization_policy.h"
#include "commute_sa/theta.h"
#include "commute_sa/types.h"

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
    /** Platform semantic location: 2=inside company, 1=outside company gate. */
    int32_t gps_source_type = 0;
    double lat = 0.0;
    double lon = 0.0;
    double acc = 0.0;
    /** Location source: 1=GNSS/outdoor, 2=network/indoor. 0=unknown. */
    int32_t gps_source_type = 0;
    bool walking = false;
    bool has_walk_started = false;
    TickTsMs walk_started_at_ms = 0;
    double pdr_net_out_home_m = 0.0;
    double pdr_net_out_company_m = 0.0;
    bool wifi_home_detach = false;
    bool wifi_company_detach = false;
    bool wifi_home_attach = false;
    bool wifi_company_attach = false;
    bool cell_leave_home = false;
    bool cell_leave_company = false;
    bool ble_home_detach = false;
    bool ble_company_detach = false;
    /** Continuous WiFi overlap vs soft/baseline (1=same, 0=fully different). */
    double wifi_jaccard_home = 1.0;
    double wifi_jaccard_company = 1.0;
    bool baro_available = false;
    bool baro_baseline_ready = false;
    double baro_descent_m = 0.0;
    double baro_descending = 0.0;
    bool baro_stable_platform = false;
    bool baro_lower_platform = false;
};

struct TickDecision {
    Scene scene = Scene::kUnknown;
    double score_home = 0.0;
    double score_company = 0.0;
    /** Compatibility scores now carry posterior P(LEAVING), not a weighted evidence sum. */
    std::string hsmm_phase_home = "AT_ANCHOR";
    std::string hsmm_phase_company = "AT_ANCHOR";
    double hsmm_preleave_home = 0.0;
    double hsmm_preleave_company = 0.0;
    double hsmm_outside_home = 0.0;
    double hsmm_outside_company = 0.0;
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
    /** Why push was blocked: NONE | COOLDOWN | UNCERTAIN | OUTSIDE | LEAD_EARLY | ALREADY_PUSHED | APPROACHING */
    std::string push_block_reason = "NONE";
    /** Active bounded policy and its last gate result, for audit/debug APIs. */
    std::string policy_template = "confirmed_leaving";
    std::string policy_match_reason = "NONE";
    LeaveObservation hsmm_obs_home;
    LeaveObservation hsmm_obs_company;
};

/**
 * Realtime leave-home / leave-company HSMM + product FSM.
 * Push is predictive: only while still INSIDE/NEAR and ETA in lead window.
 */
class SceneEngine {
public:
    SceneEngine(AnchorSet anchors, Theta theta = DefaultTheta());

    void SetTheta(const Theta &theta);
    const Theta &GetTheta() const;
    void SetAnchors(const AnchorSet &anchors);
    const AnchorSet &GetAnchors() const;
    void SetPersonalizationPolicy(const PersonalizationPolicy &policy);
    const PersonalizationPolicy &GetPersonalizationPolicy() const;
    Scene CurrentScene() const;

    TickDecision Step(const TickFeatures &feat);

private:
    struct ObservationResult {
        LeaveObservation observation;
        int hits = 0;
    };

    ObservationResult BuildLeaveObservation(const TickFeatures &feat, Relation rel, bool hasDist, double distM, double rIn,
        double rOut, double pdrNetOut, bool wifiDetach, bool cellLeave, bool bleDetach, double wifiJaccard,
        bool wifiAttach, double centerHour, std::optional<double> prevDist, bool approaching,
        bool radioSuppressed, bool gpsDistUnreliable = false) const;
    LeaveHsmmConfig HsmmConfig() const { return HsmmConfigFromTheta(theta_); }

    /** Seconds until dist reaches rOut; nullopt if not outbound / unknown. */
    std::optional<double> EstimateEtaOutS(bool hasDist, double distM, double rOut, bool walking, double pdrNetOut,
        std::optional<double> prevDist, std::optional<TickTsMs> prevT, TickTsMs tMs, bool gpsReliable) const;

    bool LeadWindowOk(const std::optional<double> &etaS, std::string *blockReason) const;

    Relation RelTo(const TickFeatures &feat, const Anchor &anchor, double *distOut) const;
    /** Home: GPS fence. Company: source_type 2/1 when in vicinity; fence is auxiliary. */
    Relation CompanyRelTo(const TickFeatures &feat, double *distOut, bool *nearCompany) const;
    bool GpsFixUsable(const TickFeatures &feat) const;
    bool CooldownOk(TickTsMs tMs) const;
    bool ArmDelayOk(const TickFeatures &feat) const;

    AnchorSet anchors_;
    Theta theta_;
    PersonalizationPolicy policy_;
    LeaveHsmm home_hsmm_;
    LeaveHsmm company_hsmm_;
    Scene scene_ = Scene::kUnknown;
    std::optional<double> prevDistHome_;
    std::optional<double> prevDistCompany_;
    Relation prevRelHome_ = Relation::kUnknown;
    Relation prevRelCompany_ = Relation::kUnknown;
    int32_t prevGpsSourceType_ = 0;
    std::optional<TickTsMs> prevTMs_;
    int approachHomeStreak_ = 0;
    int approachCompanyStreak_ = 0;
    bool wasApproachHome_ = false;
    bool wasApproachCompany_ = false;
    bool returnFromOutsideHome_ = false;
    bool returnFromOutsideCompany_ = false;
    std::optional<TickTsMs> radioSuppressHomeUntil_;
    std::optional<TickTsMs> radioSuppressCompanyUntil_;
    std::optional<TickTsMs> leaveHomeSince_;
    std::optional<TickTsMs> leaveCompanySince_;
    std::optional<TickTsMs> lastPushAt_;
    std::optional<TickTsMs> outsideHomeSince_;
    std::optional<TickTsMs> outsideCompanySince_;
    /** One push per leave episode; reset when back to AT_* */
    bool leaveHomePushed_ = false;
    bool leaveCompanyPushed_ = false;
    std::optional<TickTsMs> lastWalkStopMs_;
    bool wasWalking_ = false;
};

}  // namespace commute_sa
