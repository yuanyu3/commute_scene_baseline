#include "commute_sa/scene_engine.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace commute_sa {
namespace {

double Clip01(double x)
{
    if (x < 0.0) {
        return 0.0;
    }
    if (x > 1.0) {
        return 1.0;
    }
    return x;
}

double DecimalHourLocal(TickTsMs tMs)
{
    std::time_t sec = static_cast<std::time_t>(tMs / 1000);
    std::tm tmValue {};
#if defined(_WIN32)
    localtime_s(&tmValue, &sec);
#else
    localtime_r(&sec, &tmValue);
#endif
    return tmValue.tm_hour + tmValue.tm_min / 60.0 + tmValue.tm_sec / 3600.0;
}

double TimePrior(TickTsMs tMs, double centerHour, double windowMin)
{
    const double h = DecimalHourLocal(tMs);
    const double half = windowMin / 60.0;
    double d = std::abs(h - centerHour);
    d = std::min(d, 24.0 - d);
    if (d <= half) {
        return Clip01(1.0 - d / std::max(half, 1e-3));
    }
    return 0.0;
}

constexpr double kDefaultWalkOutMps = 1.2;

}  // namespace

SceneEngine::SceneEngine(AnchorSet anchors, Theta theta) : anchors_(std::move(anchors)), theta_(theta) {}

void SceneEngine::SetTheta(const Theta &theta)
{
    theta_ = theta;
}

const Theta &SceneEngine::GetTheta() const
{
    return theta_;
}

void SceneEngine::SetAnchors(const AnchorSet &anchors)
{
    anchors_ = anchors;
    home_hsmm_.Reset();
    company_hsmm_.Reset();
}

const AnchorSet &SceneEngine::GetAnchors() const
{
    return anchors_;
}

Scene SceneEngine::CurrentScene() const
{
    return scene_;
}

void SceneEngine::SetPersonalizationPolicy(const PersonalizationPolicy &policy)
{
    if (ValidatePersonalizationPolicy(policy, nullptr)) {
        policy_ = policy;
        policyHomeMatchSince_.reset();
        policyCompanyMatchSince_.reset();
    }
}

const PersonalizationPolicy &SceneEngine::GetPersonalizationPolicy() const
{
    return policy_;
}

Relation SceneEngine::RelTo(const TickFeatures &feat, const Anchor &anchor, double *distOut) const
{
    if (!feat.has_gps) {
        return Relation::kUnknown;
    }
    if (feat.acc > theta_.max_gps_acc_m && feat.acc > theta_.allow_network_dwell_acc_m) {
        return Relation::kUnknown;
    }
    return RelationToAnchor(feat.lat, feat.lon, anchor.lat, anchor.lon, anchor.r_in_m, anchor.r_out_m, distOut);
}

bool SceneEngine::CooldownOk(TickTsMs tMs) const
{
    if (!lastPushAt_.has_value()) {
        return true;
    }
    return (tMs - *lastPushAt_) >= static_cast<TickTsMs>(theta_.push_cooldown_s * 1000.0);
}

bool SceneEngine::ArmDelayOk(const TickFeatures &feat) const
{
    if (!feat.has_walk_started) {
        return true;
    }
    return (feat.t_ms - feat.walk_started_at_ms) >= static_cast<TickTsMs>(theta_.arm_delay_s * 1000.0);
}

std::optional<double> SceneEngine::EstimateEtaOutS(bool hasDist, double distM, double rOut, bool walking,
    double pdrNetOut, std::optional<double> prevDist, std::optional<TickTsMs> prevT, TickTsMs tMs, bool gpsReliable) const
{
    if (!hasDist) {
        return std::nullopt;
    }
    if (distM >= rOut) {
        return 0.0;
    }
    const double remain = std::max(0.0, rOut - distM);

    double speed = 0.0;
    if (gpsReliable && prevDist.has_value() && prevT.has_value() && tMs > *prevT) {
        const double dt = static_cast<double>(tMs - *prevT) / 1000.0;
        if (dt >= 0.4) {
            const double v = (distM - *prevDist) / dt;
            if (v > 0.05) {
                speed = v;
            }
        }
    }
    if (speed < 0.05 && pdrNetOut >= 8.0 && walking) {
        // Weak PDR outbound hint → assume walk speed.
        speed = kDefaultWalkOutMps;
    }
    if (speed < 0.05 && walking && remain > 0.0) {
        speed = kDefaultWalkOutMps;
    }
    if (speed < 0.05) {
        return std::nullopt;
    }
    return remain / speed;
}

bool SceneEngine::LeadWindowOk(const std::optional<double> &etaS, std::string *blockReason) const
{
    if (!etaS.has_value()) {
        // No ETA: allow if other gates pass (still predictive via INSIDE/NEAR + score).
        return true;
    }
    // Only block "too early". ETA < lead_min means urgent (almost out) — still push while
    // INSIDE/NEAR so "带钥匙" happens before fully leaving; agent uses post-hoc lead_s.
    if (*etaS > theta_.lead_max_s) {
        if (blockReason) {
            *blockReason = "LEAD_EARLY";
        }
        return false;
    }
    return true;
}

LeaveHsmmConfig SceneEngine::HsmmConfig() const
{
    LeaveHsmmConfig config;
    config.preleave_min_s = std::max(0.0, theta_.hsmm_preleave_min_s);
    config.preleave_mean_s = std::max(config.preleave_min_s, theta_.hsmm_preleave_mean_s);
    config.preleave_max_s = std::max(config.preleave_mean_s, theta_.hsmm_preleave_max_s);
    config.leaving_min_s = std::max(0.0, theta_.hsmm_leaving_min_s);
    config.leaving_mean_s = std::max(config.leaving_min_s, theta_.hsmm_leaving_mean_s);
    config.leaving_max_s = std::max(config.leaving_mean_s, theta_.hsmm_leaving_max_s);
    config.max_gap_s = std::max(1.0, theta_.hsmm_max_gap_s);
    config.reliability = {{0.25 + 3.0 * theta_.w_walk, 0.25 + 3.0 * theta_.w_pdr,
        0.25 + 3.0 * theta_.w_geo, 0.25 + 3.0 * theta_.w_wifi, 0.25 + 3.0 * theta_.w_cell,
        0.25 + 3.0 * theta_.w_ble, 0.25 + 3.0 * theta_.w_time}};
    return config;
}

SceneEngine::ObservationResult SceneEngine::BuildLeaveObservation(const TickFeatures &feat, Relation rel, bool hasDist, double distM,
    double rIn, double rOut, double pdrNetOut, bool wifiDetach, bool cellLeave, bool bleDetach, double wifiJaccard,
    bool wifiAttach, double centerHour, std::optional<double> prevDist, bool approaching,
    bool radioSuppressed) const
{
    ObservationResult out;
    int hits = 0;

    const double sWalk = feat.walking ? 1.0 : 0.0;
    if (sWalk >= theta_.thr_walk) {
        ++hits;
    }

    const double pdrEff = approaching ? 0.0 : pdrNetOut;
    const double sPdr = Clip01(pdrEff / std::max(15.0, rIn * 0.3));
    if (sPdr >= theta_.thr_pdr) {
        ++hits;
    }

    double sGeo = 0.0;
    if (hasDist && (rel == Relation::kInside || rel == Relation::kNear)) {
        if (prevDist.has_value() && distM > *prevDist + 3.0) {
            sGeo = Clip01(distM / std::max(rOut, 1.0));
            if (rel == Relation::kNear && distM >= rIn * 0.9) {
                sGeo = std::max(sGeo, 0.85);
            }
        }
    } else if (rel == Relation::kOutside && !approaching) {
        sGeo = 0.8;
    }
    if (sGeo >= theta_.thr_geo) {
        ++hits;
    }

    // Continuous WiFi leave score from Jaccard; attach / suppress → 0.
    double sWifi = 0.0;
    if (!(wifiAttach || approaching || radioSuppressed)) {
        const double thrJ = std::max(1e-3, std::min(0.99, theta_.thr_wifi_jaccard));
        if (wifiJaccard <= thrJ) {
            sWifi = 1.0;
        } else {
            sWifi = Clip01((1.0 - wifiJaccard) / (1.0 - thrJ));
        }
        if (sWifi < 1e-6 && wifiDetach) {
            sWifi = 1.0;
        }
    }
    if (sWifi >= theta_.thr_wifi) {
        ++hits;
    }

    const double sCell =
        (radioSuppressed || approaching || wifiAttach) ? 0.0 : (cellLeave ? 1.0 : 0.0);
    if (sCell >= theta_.thr_cell) {
        ++hits;
    }
    const double sBle = (radioSuppressed || approaching) ? 0.0 : (bleDetach ? 1.0 : 0.0);
    if (sBle >= theta_.thr_ble) {
        ++hits;
    }

    const double sTime = TimePrior(feat.t_ms, centerHour, theta_.leave_window_min);
    if (sTime >= theta_.thr_time) {
        ++hits;
    }

    if (approaching || (prevDist.has_value() && hasDist && distM + 8.0 < *prevDist)) {
        sGeo = 0.0;
        sWifi = 0.0;
    }

    out.observation.walking = sWalk;
    out.observation.pdr_outbound = sPdr;
    out.observation.geo_outbound = sGeo;
    out.observation.wifi_detach = sWifi;
    out.observation.cell_detach = sCell;
    out.observation.ble_detach = sBle;
    out.observation.time_prior = sTime;
    out.observation.relation_known = rel != Relation::kUnknown;
    out.observation.inside = rel == Relation::kInside;
    out.observation.near = rel == Relation::kNear;
    out.observation.outside = rel == Relation::kOutside;
    out.observation.approaching = approaching;
    out.observation.attached = wifiAttach;
    out.hits = hits;
    return out;
}

TickDecision SceneEngine::Step(const TickFeatures &feat)
{
    if (wasWalking_ && !feat.walking) {
        lastWalkStopMs_ = feat.t_ms;
    }
    wasWalking_ = feat.walking;
    double dHome = 0.0;
    double dCo = 0.0;
    Relation hRel = RelTo(feat, anchors_.home, &dHome);
    Relation cRel = RelTo(feat, anchors_.company, &dCo);
    // source_type is the platform's company-gate semantic. It takes priority
    // over a noisy coordinate fence: 1=already outside, 2=inside company.
    if (feat.gps_source_type == 1) {
        cRel = Relation::kOutside;
    } else if (feat.gps_source_type == 2) {
        cRel = Relation::kInside;
    }
    const bool hasHome = feat.has_gps && hRel != Relation::kUnknown;
    const bool hasCo = feat.has_gps && cRel != Relation::kUnknown;

    auto updateApproach = [&](Relation rel, bool hasDist, double distM, Relation prevRel,
                               const std::optional<double> &prevDist, int *streak, bool wifiAttach,
                               bool *returnFromOutside) -> bool {
        if (hasDist && prevDist.has_value() && distM + 3.0 < *prevDist) {
            ++(*streak);
        } else {
            *streak = 0;
        }
        bool approaching = false;
        if (prevRel == Relation::kOutside && (rel == Relation::kNear || rel == Relation::kInside)) {
            approaching = true;
            *returnFromOutside = true;
        }
        if (*streak >= 1 &&
            (rel == Relation::kNear || rel == Relation::kInside || rel == Relation::kOutside)) {
            approaching = true;
        }
        if (prevDist.has_value() && hasDist && distM + 8.0 < *prevDist) {
            approaching = true;
        }
        if (wifiAttach) {
            approaching = true;
        }
        return approaching;
    };
    const bool approachHome = updateApproach(hRel, hasHome, dHome, prevRelHome_, prevDistHome_, &approachHomeStreak_,
        feat.wifi_home_attach, &returnFromOutsideHome_);
    const bool approachCo = updateApproach(cRel, hasCo, dCo, prevRelCompany_, prevDistCompany_, &approachCompanyStreak_,
        feat.wifi_company_attach, &returnFromOutsideCompany_);

    auto noteApproachEdge = [&](bool approaching, bool *wasApproach, bool *returnFromOutside,
                                std::optional<TickTsMs> *until) -> bool {
        if (*wasApproach && !approaching && *returnFromOutside) {
            *until = feat.t_ms + static_cast<TickTsMs>(theta_.radio_suppress_after_approach_s * 1000.0);
            *returnFromOutside = false;
        }
        *wasApproach = approaching;
        return until->has_value() && feat.t_ms < **until;
    };
    const bool radioSupHome =
        noteApproachEdge(approachHome, &wasApproachHome_, &returnFromOutsideHome_, &radioSuppressHomeUntil_);
    const bool radioSupCo =
        noteApproachEdge(approachCo, &wasApproachCompany_, &returnFromOutsideCompany_, &radioSuppressCompanyUntil_);

    const auto sh = BuildLeaveObservation(feat, hRel, hasHome, dHome, anchors_.home.r_in_m, anchors_.home.r_out_m,
        feat.pdr_net_out_home_m, feat.wifi_home_detach, feat.cell_leave_home, feat.ble_home_detach,
        feat.wifi_jaccard_home, feat.wifi_home_attach, theta_.weekday_leave_home_hour, prevDistHome_, approachHome,
        radioSupHome);
    const auto sc = BuildLeaveObservation(feat, cRel, hasCo, dCo, anchors_.company.r_in_m, anchors_.company.r_out_m,
        feat.pdr_net_out_company_m, feat.wifi_company_detach, feat.cell_leave_company, feat.ble_company_detach,
        feat.wifi_jaccard_company, feat.wifi_company_attach, theta_.weekday_leave_company_hour, prevDistCompany_,
        approachCo, radioSupCo);
    const LeaveHsmmConfig hsmmConfig = HsmmConfig();
    const auto hsmmHome = home_hsmm_.Step(sh.observation, feat.t_ms, hsmmConfig);
    const auto hsmmCompany = company_hsmm_.Step(sc.observation, feat.t_ms, hsmmConfig);

    const bool gpsReliable = feat.acc <= 50.0;
    const auto etaHome = EstimateEtaOutS(hasHome, dHome, anchors_.home.r_out_m, feat.walking, feat.pdr_net_out_home_m,
        prevDistHome_, prevTMs_, feat.t_ms, gpsReliable);
    const auto etaCo = EstimateEtaOutS(hasCo, dCo, anchors_.company.r_out_m, feat.walking, feat.pdr_net_out_company_m,
        prevDistCompany_, prevTMs_, feat.t_ms, gpsReliable);

    const double enter = theta_.enter_leave;
    const int need = theta_.min_evidence;
    const bool allowHome = FocusAllowsHome(theta_.focus_side);
    const bool allowCompany = FocusAllowsCompany(theta_.focus_side);
    bool shouldService = false;
    std::string intent = "NONE";
    std::string pushBlock = "NONE";
    Scene newScene = scene_;

    if (hRel == Relation::kOutside) {
        outsideHomeSince_ = outsideHomeSince_.value_or(feat.t_ms);
    } else {
        outsideHomeSince_.reset();
    }
    if (cRel == Relation::kOutside) {
        outsideCompanySince_ = outsideCompanySince_.value_or(feat.t_ms);
    } else {
        outsideCompanySince_.reset();
    }

    auto persistOut = [&](const std::optional<TickTsMs> &since) -> bool {
        if (!since.has_value()) {
            return false;
        }
        return (feat.t_ms - *since) >= static_cast<TickTsMs>(theta_.away_confirm_s * 1000.0);
    };

    if (allowHome && hRel == Relation::kInside && hsmmHome.LeavingProbability() <= theta_.exit_leave) {
        newScene = Scene::kAtHome;
        leaveHomeSince_.reset();
        leaveHomePushed_ = false;
    } else if (allowCompany && cRel == Relation::kInside && hsmmCompany.LeavingProbability() <= theta_.exit_leave) {
        newScene = Scene::kAtCompany;
        leaveCompanySince_.reset();
        leaveCompanyPushed_ = false;
    }

    const bool legacyHomeLeaveCand = allowHome && (hRel == Relation::kInside || hRel == Relation::kNear) &&
        !approachHome && prevRelHome_ != Relation::kOutside && hsmmHome.LeavingProbability() >= enter && sh.hits >= need &&
        ArmDelayOk(feat);
    const bool legacyCoLeaveCand = allowCompany && (cRel == Relation::kInside || cRel == Relation::kNear) &&
        !approachCo && prevRelCompany_ != Relation::kOutside && hsmmCompany.LeavingProbability() >= enter && sc.hits >= need &&
        ArmDelayOk(feat);

    std::string policyHomeReason = "LEGACY_MATCH";
    std::string policyCompanyReason = "LEGACY_MATCH";
    auto policyCandidate = [&](bool allowed, bool anchoredByScene, Relation rel, Relation prevRel, bool approaching,
                               const LeaveHsmmResult &hsmm, const ObservationResult &obs, double pdr,
                               bool wifiDetach, bool cellLeave, bool bleDetach, bool hasDist, double dist,
                               std::optional<double> prevDist, std::optional<TickTsMs> *matchSince,
                               std::string *reason) -> bool {
        if (policy_.template_name == "confirmed_leaving") {
            return false;  // caller uses the backward-compatible gate
        }
        const bool anchorGate = (rel == Relation::kInside || rel == Relation::kNear) ||
            (policy_.gps_mode == "IGNORE" && rel == Relation::kUnknown && anchoredByScene);
        const bool radio = wifiDetach || cellLeave || bleDetach;
        const bool elevatorResume = lastWalkStopMs_.has_value() && feat.walking && feat.has_walk_started &&
            (feat.walk_started_at_ms - *lastWalkStopMs_) >= 20000 &&
            (feat.walk_started_at_ms - *lastWalkStopMs_) <= 120000 &&
            (feat.t_ms - feat.walk_started_at_ms) >= 5000 && radio && pdr >= 4.0;
        if (!allowed || !anchorGate || approaching ||
            prevRel == Relation::kOutside || (!ArmDelayOk(feat) && !elevatorResume)) {
            matchSince->reset();
            *reason = "BASE_SAFETY_GATE";
            return false;
        }
        PolicyEvidence evidence;
        evidence.t_ms = feat.t_ms;
        evidence.preleave_probability = hsmm.PreLeaveProbability();
        evidence.leaving_probability = hsmm.LeavingProbability();
        evidence.baseline_hits = obs.hits;
        evidence.walking = feat.walking;
        evidence.pdr_net_out_m = pdr;
        evidence.wifi_detach = wifiDetach;
        evidence.cell_leave = cellLeave;
        evidence.ble_detach = bleDetach;
        evidence.geo_outbound = hasDist && prevDist.has_value() && dist > *prevDist + 3.0;
        evidence.has_usable_gps = hasDist;
        const PolicyMatch match = MatchPersonalizationPolicy(policy_, evidence);
        *reason = match.reason;
        if (!match.matched) {
            matchSince->reset();
            return false;
        }
        *matchSince = matchSince->value_or(feat.t_ms);
        if ((feat.t_ms - **matchSince) < static_cast<TickTsMs>(policy_.min_duration_s * 1000.0)) {
            *reason = "MIN_DURATION";
            return false;
        }
        *reason = "MATCH";
        return true;
    };
    const bool policyHomeCand = policyCandidate(allowHome,
        scene_ == Scene::kAtHome || scene_ == Scene::kLeavingHome, hRel, prevRelHome_, approachHome, hsmmHome, sh,
        feat.pdr_net_out_home_m, feat.wifi_home_detach, feat.cell_leave_home, feat.ble_home_detach,
        hasHome, dHome, prevDistHome_, &policyHomeMatchSince_, &policyHomeReason);
    const bool policyCompanyCand = policyCandidate(allowCompany,
        scene_ == Scene::kAtCompany || scene_ == Scene::kLeavingCompany, cRel, prevRelCompany_, approachCo, hsmmCompany, sc,
        feat.pdr_net_out_company_m, feat.wifi_company_detach, feat.cell_leave_company, feat.ble_company_detach,
        hasCo, dCo, prevDistCompany_, &policyCompanyMatchSince_, &policyCompanyReason);
    const bool useLegacyPolicy = !policy_.enabled || policy_.template_name == "confirmed_leaving";
    const bool homeLeaveCand = useLegacyPolicy ? legacyHomeLeaveCand : policyHomeCand;
    const bool coLeaveCand = useLegacyPolicy ? legacyCoLeaveCand : policyCompanyCand;

    std::optional<double> activeEta;
    bool leadOk = false;

    if (homeLeaveCand) {
        newScene = Scene::kLeavingHome;
        leaveHomeSince_ = leaveHomeSince_.value_or(feat.t_ms);
        activeEta = etaHome;
        std::string leadBlock;
        leadOk = LeadWindowOk(etaHome, &leadBlock);

        if (hRel == Relation::kOutside) {
            pushBlock = "OUTSIDE";
        } else if (approachHome || feat.wifi_home_attach) {
            pushBlock = "APPROACHING";
        } else if (leaveHomePushed_) {
            pushBlock = "ALREADY_PUSHED";
        } else if (!CooldownOk(feat.t_ms)) {
            pushBlock = "COOLDOWN";
        } else if (!leadOk) {
            pushBlock = leadBlock.empty() ? "LEAD_EARLY" : leadBlock;
        } else {
            shouldService = true;
            intent = "DEPARTURE_NOTIFICATION";
            leaveHomePushed_ = true;
            lastPushAt_ = feat.t_ms;
        }
    } else if (coLeaveCand) {
        newScene = Scene::kLeavingCompany;
        leaveCompanySince_ = leaveCompanySince_.value_or(feat.t_ms);
        activeEta = etaCo;
        std::string leadBlock;
        leadOk = LeadWindowOk(etaCo, &leadBlock);

        if (cRel == Relation::kOutside) {
            pushBlock = "OUTSIDE";
        } else if (approachCo || feat.wifi_company_attach) {
            pushBlock = "APPROACHING";
        } else if (leaveCompanyPushed_) {
            pushBlock = "ALREADY_PUSHED";
        } else if (!CooldownOk(feat.t_ms)) {
            pushBlock = "COOLDOWN";
        } else if (!leadOk) {
            pushBlock = leadBlock.empty() ? "LEAD_EARLY" : leadBlock;
        } else {
            shouldService = true;
            intent = "LEAVE_COMPANY_NOTIFICATION";
            leaveCompanyPushed_ = true;
            lastPushAt_ = feat.t_ms;
        }
    }

    const bool leavingNow = (newScene == Scene::kLeavingHome || newScene == Scene::kLeavingCompany);
    if (!leavingNow && persistOut(outsideHomeSince_) && persistOut(outsideCompanySince_)) {
        newScene = Scene::kCommute;
    } else if (!leavingNow && allowCompany && persistOut(outsideHomeSince_) && cRel == Relation::kInside) {
        newScene = Scene::kAtCompany;
    } else if (!leavingNow && allowHome && persistOut(outsideCompanySince_) && hRel == Relation::kInside) {
        newScene = Scene::kAtHome;
    } else if (persistOut(outsideHomeSince_) && newScene == Scene::kLeavingHome) {
        if ((feat.t_ms - leaveHomeSince_.value_or(feat.t_ms)) >=
            static_cast<TickTsMs>(theta_.away_confirm_s * 1000.0)) {
            newScene = (allowCompany && cRel == Relation::kInside) ? Scene::kAtCompany : Scene::kCommute;
        }
    } else if (persistOut(outsideCompanySince_) && newScene == Scene::kLeavingCompany) {
        if ((feat.t_ms - leaveCompanySince_.value_or(feat.t_ms)) >=
            static_cast<TickTsMs>(theta_.away_confirm_s * 1000.0)) {
            newScene = (allowHome && hRel == Relation::kInside) ? Scene::kAtHome : Scene::kCommute;
        }
    }

    if (allowHome && !leavingNow && hRel == Relation::kInside && hsmmHome.LeavingProbability() <= theta_.exit_leave &&
        persistOut(outsideCompanySince_)) {
        newScene = Scene::kAtHome;
        leaveHomePushed_ = false;
    }
    if (allowCompany && !leavingNow && cRel == Relation::kInside && hsmmCompany.LeavingProbability() <= theta_.exit_leave &&
        persistOut(outsideHomeSince_)) {
        newScene = Scene::kAtCompany;
        leaveCompanyPushed_ = false;
    }

    if (hRel == Relation::kUnknown && cRel == Relation::kUnknown && !feat.walking) {
        if (newScene != Scene::kAtHome && newScene != Scene::kAtCompany && newScene != Scene::kLeavingHome &&
            newScene != Scene::kLeavingCompany) {
            newScene = Scene::kUnknown;
        }
    }

    std::string uncertainty = "MEDIUM";
    const int maxHits = std::max(sh.hits, sc.hits);
    if (maxHits >= 3) {
        uncertainty = "LOW";
    } else if (maxHits < need) {
        uncertainty = "HIGH";
    }
    if (shouldService && uncertainty == "HIGH") {
        shouldService = false;
        intent = "NONE";
        pushBlock = "UNCERTAIN";
        // Allow retry on later ticks of same episode.
        if (newScene == Scene::kLeavingHome) {
            leaveHomePushed_ = false;
        }
        if (newScene == Scene::kLeavingCompany) {
            leaveCompanyPushed_ = false;
        }
    }

    // Hard ban: never push departure when already outside the fence or approaching.
    if (shouldService && intent == "DEPARTURE_NOTIFICATION" &&
        (hRel == Relation::kOutside || approachHome || feat.wifi_home_attach)) {
        shouldService = false;
        intent = "NONE";
        pushBlock = (approachHome || feat.wifi_home_attach) ? "APPROACHING" : "OUTSIDE";
        leaveHomePushed_ = false;
    }
    if (shouldService && intent == "LEAVE_COMPANY_NOTIFICATION" &&
        (cRel == Relation::kOutside || approachCo || feat.wifi_company_attach)) {
        shouldService = false;
        intent = "NONE";
        pushBlock = (approachCo || feat.wifi_company_attach) ? "APPROACHING" : "OUTSIDE";
        leaveCompanyPushed_ = false;
    }

    scene_ = newScene;
    prevRelHome_ = hRel;
    prevRelCompany_ = cRel;
    if (hasHome) {
        prevDistHome_ = dHome;
    }
    if (hasCo) {
        prevDistCompany_ = dCo;
    }
    prevTMs_ = feat.t_ms;

    TickDecision d;
    d.scene = newScene;
    d.score_home = hsmmHome.LeavingProbability();
    d.score_company = hsmmCompany.LeavingProbability();
    d.hsmm_phase_home = LeavePhaseToString(hsmmHome.phase);
    d.hsmm_phase_company = LeavePhaseToString(hsmmCompany.phase);
    d.hsmm_preleave_home = hsmmHome.PreLeaveProbability();
    d.hsmm_preleave_company = hsmmCompany.PreLeaveProbability();
    d.hsmm_outside_home = hsmmHome.OutsideProbability();
    d.hsmm_outside_company = hsmmCompany.OutsideProbability();
    d.home_relation = hRel;
    d.company_relation = cRel;
    d.has_dist_home = hasHome;
    d.dist_home_m = dHome;
    d.has_dist_company = hasCo;
    d.dist_company_m = dCo;
    d.should_service = shouldService;
    d.service_intent = intent;
    d.hits_home = sh.hits;
    d.hits_company = sc.hits;
    d.uncertainty = uncertainty;
    d.eta_leave_s = -1.0;
    if (activeEta.has_value()) {
        d.eta_leave_s = *activeEta;
    } else if ((hRel == Relation::kInside || hRel == Relation::kNear) && etaHome.has_value()) {
        d.eta_leave_s = *etaHome;
    } else if ((cRel == Relation::kInside || cRel == Relation::kNear) && etaCo.has_value()) {
        d.eta_leave_s = *etaCo;
    }
    if (d.eta_leave_s < 0.0) {
        d.lead_gate_ok = true;
    } else {
        d.lead_gate_ok = LeadWindowOk(std::optional<double>(d.eta_leave_s), nullptr);
    }
    d.push_block_reason = shouldService ? "NONE" : pushBlock;
    d.policy_template = policy_.template_name;
    d.policy_match_reason = homeLeaveCand ? policyHomeReason : policyCompanyReason;
    return d;
}

}  // namespace commute_sa
