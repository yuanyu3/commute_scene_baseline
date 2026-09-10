#include "commute_sa/scene_engine.h"

#include "commute_sa/context_template.h"
#include "commute_sa/evidence_strength_profile.h"

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
    prevGpsSourceType_ = 0;
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
    }
}

const PersonalizationPolicy &SceneEngine::GetPersonalizationPolicy() const
{
    return policy_;
}

Relation SceneEngine::RelTo(const TickFeatures &feat, const Anchor &anchor, double *distOut) const
{
    if (!GpsFixUsable(feat)) {
        return Relation::kUnknown;
    }
    return RelationToAnchor(feat.lat, feat.lon, anchor.lat, anchor.lon, anchor.r_in_m, anchor.r_out_m, distOut);
}

bool SceneEngine::GpsFixUsable(const TickFeatures &feat) const
{
    if (!feat.has_gps) {
        return false;
    }
    if (feat.acc > theta_.max_gps_acc_m && feat.acc > theta_.allow_network_dwell_acc_m) {
        return false;
    }
    return true;
}

Relation SceneEngine::CompanyRelTo(const TickFeatures &feat, double *distOut, bool *nearCompany) const
{
    if (nearCompany != nullptr) {
        *nearCompany = false;
    }
    if (!feat.has_gps) {
        return Relation::kUnknown;
    }
    const Relation geoRel = RelationToAnchor(feat.lat, feat.lon, anchors_.company.lat, anchors_.company.lon,
        anchors_.company.r_in_m, anchors_.company.r_out_m, distOut);
    const double dist = distOut != nullptr ? *distOut : 0.0;
    const double vicinity = std::max(theta_.company_source_vicinity_m, anchors_.company.r_out_m);
    const bool sticky = scene_ == Scene::kAtCompany || scene_ == Scene::kLeavingCompany;
    const bool near = sticky || (theta_.w_wifi > 0.0 && feat.wifi_company_attach) || dist <= vicinity || geoRel == Relation::kInside ||
        geoRel == Relation::kNear;
    if (nearCompany != nullptr) {
        *nearCompany = near;
    }
    if (near) {
        if (feat.gps_source_type == kLocationSourceIndoorNetwork) {
            return Relation::kInside;
        }
        if (feat.gps_source_type == kLocationSourceOutdoorGnss) {
            return Relation::kOutside;
        }
    }
    if (!GpsFixUsable(feat)) {
        return Relation::kUnknown;
    }
    return geoRel;
}

bool SceneEngine::CooldownOk(TickTsMs tMs) const
{
    if (!lastPushAt_.has_value()) {
        return true;
    }
    return (tMs - *lastPushAt_) >= static_cast<TickTsMs>(theta_.push_cooldown_s * 1000.0);
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

SceneEngine::ObservationResult SceneEngine::BuildLeaveObservation(const Theta &effectiveTheta, const TickFeatures &feat, Relation rel, bool hasDist, double distM,
    double rIn, double rOut, double pdrNetOut, bool wifiDetach, bool cellLeave, bool bleDetach, double wifiJaccard,
    bool wifiAttach, double centerHour, std::optional<double> prevDist, bool approaching,
    bool radioSuppressed, bool gpsDistUnreliable) const
{
    ObservationResult out;
    int hits = 0;

    const bool useWalk = effectiveTheta.w_walk > 0.0;
    const bool usePdr = effectiveTheta.w_pdr > 0.0;
    const bool useGeo = effectiveTheta.w_geo > 0.0;
    const bool useWifi = effectiveTheta.w_wifi > 0.0;
    const bool useCell = effectiveTheta.w_cell > 0.0;
    const bool useBle = effectiveTheta.w_ble > 0.0;
    const bool useTime = effectiveTheta.w_time > 0.0;
    const bool useBaro = effectiveTheta.w_baro > 0.0;

    const double sWalk = useWalk && feat.walking ? 1.0 : 0.0;
    if (useWalk && sWalk >= effectiveTheta.thr_walk) {
        ++hits;
    }

    const double pdrEff = approaching ? 0.0 : pdrNetOut;
    const double sPdr = usePdr ? Clip01(pdrEff / std::max(15.0, rIn * 0.3)) : 0.0;
    if (usePdr && sPdr >= effectiveTheta.thr_pdr) {
        ++hits;
    }

    double sGeo = 0.0;
    if (useGeo && !gpsDistUnreliable) {
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
    } else if (useGeo && rel == Relation::kOutside && !approaching) {
        // source_type 2→1 (or GNSS while near company) is the precise gate-leave.
        sGeo = 1.0;
    }
    if (useGeo && sGeo >= effectiveTheta.thr_geo) {
        ++hits;
    }

    // Continuous WiFi leave score with a neutral band. Similarity at/below
    // thrJ is full leave evidence; similarity at/above the attach side of the
    // hysteresis band is zero evidence.
    double sWifi = 0.0;
    if (useWifi && !(wifiAttach || approaching || radioSuppressed)) {
        const double thrJ = std::max(1e-3, std::min(0.99, effectiveTheta.thr_wifi_jaccard));
        const double attachJ = std::min(1.0, thrJ + 0.25);
        if (wifiJaccard <= thrJ) {
            sWifi = 1.0;
        } else {
            sWifi = Clip01((attachJ - wifiJaccard) / std::max(1e-3, attachJ - thrJ));
        }
        if (sWifi < 1e-6 && wifiDetach) {
            sWifi = 1.0;
        }
    }
    if (useWifi && sWifi >= effectiveTheta.thr_wifi) {
        ++hits;
    }

    const double sCell = !useCell || radioSuppressed || approaching || wifiAttach ? 0.0 : (cellLeave ? 1.0 : 0.0);
    if (useCell && sCell >= effectiveTheta.thr_cell) {
        ++hits;
    }
    const double sBle = !useBle || radioSuppressed || approaching ? 0.0 : (bleDetach ? 1.0 : 0.0);
    if (useBle && sBle >= effectiveTheta.thr_ble) {
        ++hits;
    }

    const double sTime = useTime ? TimePrior(feat.t_ms, centerHour, effectiveTheta.leave_window_min) : 0.0;
    if (useTime && sTime >= effectiveTheta.thr_time) {
        ++hits;
    }

    if (approaching || (!gpsDistUnreliable && prevDist.has_value() && hasDist && distM + 8.0 < *prevDist)) {
        sGeo = 0.0;
        sWifi = 0.0;
    }

    out.observation.walking = sWalk;
    out.observation.t_ms = feat.t_ms;
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
    const bool baroReady = useBaro && feat.baro_available && feat.baro_baseline_ready;
    out.observation.baro_descent_m = feat.baro_descent_m;
    out.observation.baro_stable_platform = feat.baro_stable_platform;
    out.observation.baro_stable_platform_known = baroReady;
    out.observation.baro_descending = baroReady ? feat.baro_descending : 0.0;
    out.observation.baro_lower_platform = baroReady && feat.baro_lower_platform ? 1.0 : 0.0;
    out.observation.baro_available = baroReady;
    out.observation.baro_ascending = baroReady ? feat.baro_ascending : 0.0;
    out.observation.vertical_closure = baroReady && feat.vertical_closure ? 1.0 : 0.0;
    out.hits = hits;
    return out;
}

TickDecision SceneEngine::Step(const TickFeatures &feat)
{
    if (wasWalking_ && !feat.walking) {
        lastWalkStopMs_ = feat.t_ms;
    }
    wasWalking_ = feat.walking;
    const Theta homeTheta = ApplyCommittedUserAnchorProfile(theta_, anchors_.home.id);
    const Theta companyTheta = ApplyCommittedUserAnchorProfile(theta_, anchors_.company.id);
    const bool wifiHomeAttach = homeTheta.w_wifi > 0.0 && feat.wifi_home_attach;
    const bool wifiCompanyAttach = companyTheta.w_wifi > 0.0 && feat.wifi_company_attach;
    double dHome = 0.0;
    double dCo = 0.0;
    bool nearCompany = false;
    const Relation hRel = RelTo(feat, anchors_.home, &dHome);
    const Relation cRel = CompanyRelTo(feat, &dCo, &nearCompany);
    const bool hasHome = feat.has_gps && hRel != Relation::kUnknown;
    const bool hasCo = feat.has_gps && cRel != Relation::kUnknown;
    const bool companySourceIndoor = nearCompany && feat.gps_source_type == kLocationSourceIndoorNetwork;
    const bool companySourceOutdoor = nearCompany && feat.gps_source_type == kLocationSourceOutdoorGnss;
    const bool companySourceGate = companySourceIndoor || companySourceOutdoor;
    const bool companyGateLeave = companySourceOutdoor && prevGpsSourceType_ == kLocationSourceIndoorNetwork;

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
        wifiHomeAttach, &returnFromOutsideHome_);
    bool approachCo = false;
    if (companySourceGate) {
        approachCompanyStreak_ = 0;
        if (prevGpsSourceType_ == kLocationSourceOutdoorGnss && companySourceIndoor) {
            approachCo = true;
            returnFromOutsideCompany_ = true;
        }
        if (wifiCompanyAttach) {
            approachCo = true;
        }
        if (companyGateLeave) {
            approachCo = false;
        }
    } else {
        approachCo = updateApproach(cRel, hasCo, dCo, prevRelCompany_, prevDistCompany_, &approachCompanyStreak_,
            wifiCompanyAttach, &returnFromOutsideCompany_);
    }

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

    auto sh = BuildLeaveObservation(homeTheta, feat, hRel, hasHome, dHome, anchors_.home.r_in_m, anchors_.home.r_out_m,
        feat.pdr_net_out_home_m, feat.wifi_home_detach, feat.cell_leave_home, feat.ble_home_detach,
        feat.wifi_jaccard_home, wifiHomeAttach, theta_.weekday_leave_home_hour, prevDistHome_, approachHome,
        radioSupHome);
    auto sc = BuildLeaveObservation(companyTheta, feat, cRel, hasCo, dCo, anchors_.company.r_in_m, anchors_.company.r_out_m,
        feat.pdr_net_out_company_m, feat.wifi_company_detach, feat.cell_leave_company, feat.ble_company_detach,
        feat.wifi_jaccard_company, wifiCompanyAttach, theta_.weekday_leave_company_hour, prevDistCompany_,
        approachCo, radioSupCo, companySourceGate);
    ApplyActiveContextTemplateObservation("home", anchors_.home.id, feat.t_ms, &sh.observation);
    ApplyActiveContextTemplateObservation("company", anchors_.company.id, feat.t_ms, &sc.observation);
    // A zero weight means the channel is unavailable, including values added
    // by a context template. Structural relation/inside/outside facts remain.
    auto applyChannelMask = [&](const Theta &effectiveTheta, LeaveObservation *obs) {
        if (effectiveTheta.w_walk <= 0.0) obs->walking = 0.0;
        if (effectiveTheta.w_pdr <= 0.0) obs->pdr_outbound = 0.0;
        if (effectiveTheta.w_geo <= 0.0) obs->geo_outbound = 0.0;
        if (effectiveTheta.w_wifi <= 0.0) {
            obs->wifi_detach = 0.0;
            obs->attached = false;
        }
        if (effectiveTheta.w_cell <= 0.0) obs->cell_detach = 0.0;
        if (effectiveTheta.w_ble <= 0.0) obs->ble_detach = 0.0;
        if (effectiveTheta.w_time <= 0.0) obs->time_prior = 0.0;
        if (effectiveTheta.w_baro <= 0.0) {
            obs->baro_descending = 0.0;
            obs->baro_lower_platform = 0.0;
            obs->baro_ascending = 0.0;
            obs->vertical_closure = 0.0;
            obs->baro_available = false;
        }
    };
    applyChannelMask(homeTheta, &sh.observation);
    applyChannelMask(companyTheta, &sc.observation);
    const auto hsmmHome = home_hsmm_.Step(sh.observation, feat.t_ms, HsmmConfigFromTheta(homeTheta));
    const auto hsmmCompany = company_hsmm_.Step(sc.observation, feat.t_ms, HsmmConfigFromTheta(companyTheta));

    const bool gpsReliable = feat.acc <= 50.0;
    const auto etaHome = EstimateEtaOutS(hasHome, dHome, anchors_.home.r_out_m, feat.walking, feat.pdr_net_out_home_m,
        prevDistHome_, prevTMs_, feat.t_ms, gpsReliable);
    std::optional<double> etaCo;
    if (companySourceOutdoor) {
        etaCo = 0.0;
    } else if (!companySourceIndoor) {
        etaCo = EstimateEtaOutS(hasCo, dCo, anchors_.company.r_out_m, feat.walking, feat.pdr_net_out_company_m,
            prevDistCompany_, prevTMs_, feat.t_ms, gpsReliable);
    }

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
        !approachHome && prevRelHome_ != Relation::kOutside && hsmmHome.LeavingProbability() >= enter;
    const bool legacyCoLeaveCand = allowCompany && (cRel == Relation::kInside || cRel == Relation::kNear) &&
        !approachCo && prevRelCompany_ != Relation::kOutside && hsmmCompany.LeavingProbability() >= enter;

    const bool homeLeaveCand = legacyHomeLeaveCand;
    const bool coLeaveCand = legacyCoLeaveCand;
    const std::string policyHomeReason = "HSMM";
    const std::string policyCompanyReason = "HSMM";

    std::optional<double> activeEta;
    // ETA remains diagnostic, but no longer blocks an otherwise valid HSMM
    // departure candidate. This avoids suppressing early predictive evidence.
    bool leadOk = true;

    if (homeLeaveCand) {
        newScene = Scene::kLeavingHome;
        leaveHomeSince_ = leaveHomeSince_.value_or(feat.t_ms);
        activeEta = etaHome;
        if (hRel == Relation::kOutside) {
            pushBlock = "OUTSIDE";
        } else if (approachHome || wifiHomeAttach) {
            pushBlock = "APPROACHING";
        } else if (leaveHomePushed_) {
            pushBlock = "ALREADY_PUSHED";
        } else if (!CooldownOk(feat.t_ms)) {
            pushBlock = "COOLDOWN";
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
        if (cRel == Relation::kOutside) {
            pushBlock = "OUTSIDE";
        } else if (approachCo || wifiCompanyAttach) {
            pushBlock = "APPROACHING";
        } else if (leaveCompanyPushed_) {
            pushBlock = "ALREADY_PUSHED";
        } else if (!CooldownOk(feat.t_ms)) {
            pushBlock = "COOLDOWN";
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

    // Hard ban: never push when already outside the gate or approaching.
    if (shouldService && intent == "DEPARTURE_NOTIFICATION" &&
        (hRel == Relation::kOutside || approachHome || wifiHomeAttach)) {
        shouldService = false;
        intent = "NONE";
        pushBlock = (approachHome || wifiHomeAttach) ? "APPROACHING" : "OUTSIDE";
        leaveHomePushed_ = false;
    }
    if (shouldService && intent == "LEAVE_COMPANY_NOTIFICATION" &&
        (cRel == Relation::kOutside || approachCo || wifiCompanyAttach)) {
        shouldService = false;
        intent = "NONE";
        pushBlock = (approachCo || wifiCompanyAttach) ? "APPROACHING" : "OUTSIDE";
        leaveCompanyPushed_ = false;
    }

    scene_ = newScene;
    prevRelHome_ = hRel;
    prevRelCompany_ = cRel;
    prevGpsSourceType_ = feat.gps_source_type;
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
    d.lead_gate_ok = true;
    d.push_block_reason = shouldService ? "NONE" : pushBlock;
    d.policy_template = "confirmed_leaving";
    d.policy_match_reason = homeLeaveCand ? policyHomeReason : policyCompanyReason;
    d.hsmm_obs_home = sh.observation;
    d.hsmm_obs_company = sc.observation;
    return d;
}

}  // namespace commute_sa
