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
}

const AnchorSet &SceneEngine::GetAnchors() const
{
    return anchors_;
}

Scene SceneEngine::CurrentScene() const
{
    return scene_;
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
    double pdrNetOut, std::optional<double> prevDist, std::optional<TickTsMs> prevT, TickTsMs tMs) const
{
    if (!hasDist) {
        return std::nullopt;
    }
    if (distM >= rOut) {
        return 0.0;
    }
    const double remain = std::max(0.0, rOut - distM);

    double speed = 0.0;
    if (prevDist.has_value() && prevT.has_value() && tMs > *prevT) {
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

SceneEngine::ScoreResult SceneEngine::ScoreLeaving(const TickFeatures &feat, Relation rel, bool hasDist, double distM,
    double rIn, double rOut, double pdrNetOut, bool radioDetach, bool cellLeave, double centerHour,
    std::optional<double> prevDist) const
{
    ScoreResult out;
    int hits = 0;

    const double sWalk = feat.walking ? 1.0 : 0.0;
    if (feat.walking) {
        ++hits;
    }

    const double sPdr = Clip01(pdrNetOut / std::max(15.0, rIn * 0.3));
    if (sPdr >= 0.5) {
        ++hits;
    }

    double sGeo = 0.0;
    if (hasDist && (rel == Relation::kInside || rel == Relation::kNear)) {
        if (prevDist.has_value() && distM > *prevDist + 3.0) {
            sGeo = Clip01(distM / std::max(rOut, 1.0));
            ++hits;
        } else if (rel == Relation::kNear) {
            sGeo = 0.4;
        }
    } else if (rel == Relation::kOutside) {
        sGeo = 0.8;
    }

    const double sRadio = (radioDetach || cellLeave) ? 1.0 : 0.0;
    if (sRadio >= 0.5) {
        ++hits;
    }

    const double sTime = TimePrior(feat.t_ms, centerHour, theta_.leave_window_min);
    if (sTime >= 0.5) {
        ++hits;
    }

    if (prevDist.has_value() && hasDist && distM + 12.0 < *prevDist) {
        return {0.0, 0};
    }

    out.score = Clip01(theta_.w_walk * sWalk + theta_.w_pdr * sPdr + theta_.w_geo * sGeo + theta_.w_radio * sRadio +
        theta_.w_time * sTime);
    out.hits = hits;
    return out;
}

TickDecision SceneEngine::Step(const TickFeatures &feat)
{
    double dHome = 0.0;
    double dCo = 0.0;
    const Relation hRel = RelTo(feat, anchors_.home, &dHome);
    const Relation cRel = RelTo(feat, anchors_.company, &dCo);
    const bool hasHome = feat.has_gps && hRel != Relation::kUnknown;
    const bool hasCo = feat.has_gps && cRel != Relation::kUnknown;

    const auto sh = ScoreLeaving(feat, hRel, hasHome, dHome, anchors_.home.r_in_m, anchors_.home.r_out_m,
        feat.pdr_net_out_home_m, feat.wifi_home_detach, feat.cell_leave_home, theta_.weekday_leave_home_hour,
        prevDistHome_);
    const auto sc = ScoreLeaving(feat, cRel, hasCo, dCo, anchors_.company.r_in_m, anchors_.company.r_out_m,
        feat.pdr_net_out_company_m, feat.wifi_company_detach, feat.cell_leave_company,
        theta_.weekday_leave_company_hour, prevDistCompany_);

    const auto etaHome = EstimateEtaOutS(hasHome, dHome, anchors_.home.r_out_m, feat.walking, feat.pdr_net_out_home_m,
        prevDistHome_, prevTMs_, feat.t_ms);
    const auto etaCo = EstimateEtaOutS(hasCo, dCo, anchors_.company.r_out_m, feat.walking, feat.pdr_net_out_company_m,
        prevDistCompany_, prevTMs_, feat.t_ms);

    const double enter = theta_.enter_leave;
    const int need = theta_.min_evidence;
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

    if (hRel == Relation::kInside && sh.score < enter) {
        newScene = Scene::kAtHome;
        leaveHomeSince_.reset();
        leaveHomePushed_ = false;
    } else if (cRel == Relation::kInside && sc.score < enter) {
        newScene = Scene::kAtCompany;
        leaveCompanySince_.reset();
        leaveCompanyPushed_ = false;
    }

    const bool homeLeaveCand =
        (hRel == Relation::kInside || hRel == Relation::kNear) && sh.score >= enter && sh.hits >= need && ArmDelayOk(feat);
    const bool coLeaveCand =
        (cRel == Relation::kInside || cRel == Relation::kNear) && sc.score >= enter && sc.hits >= need && ArmDelayOk(feat);

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
    } else if (!leavingNow && persistOut(outsideHomeSince_) && cRel == Relation::kInside) {
        newScene = Scene::kAtCompany;
    } else if (!leavingNow && persistOut(outsideCompanySince_) && hRel == Relation::kInside) {
        newScene = Scene::kAtHome;
    } else if (persistOut(outsideHomeSince_) && newScene == Scene::kLeavingHome) {
        if ((feat.t_ms - leaveHomeSince_.value_or(feat.t_ms)) >=
            static_cast<TickTsMs>(theta_.away_confirm_s * 1000.0)) {
            newScene = (cRel != Relation::kInside) ? Scene::kCommute : Scene::kAtCompany;
        }
    } else if (persistOut(outsideCompanySince_) && newScene == Scene::kLeavingCompany) {
        if ((feat.t_ms - leaveCompanySince_.value_or(feat.t_ms)) >=
            static_cast<TickTsMs>(theta_.away_confirm_s * 1000.0)) {
            newScene = (hRel != Relation::kInside) ? Scene::kCommute : Scene::kAtHome;
        }
    }

    if (!leavingNow && hRel == Relation::kInside && sh.score < enter && persistOut(outsideCompanySince_)) {
        newScene = Scene::kAtHome;
        leaveHomePushed_ = false;
    }
    if (!leavingNow && cRel == Relation::kInside && sc.score < enter && persistOut(outsideHomeSince_)) {
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

    // Hard ban: never push departure when already outside the fence.
    if (shouldService && intent == "DEPARTURE_NOTIFICATION" && hRel == Relation::kOutside) {
        shouldService = false;
        intent = "NONE";
        pushBlock = "OUTSIDE";
        leaveHomePushed_ = false;
    }
    if (shouldService && intent == "LEAVE_COMPANY_NOTIFICATION" && cRel == Relation::kOutside) {
        shouldService = false;
        intent = "NONE";
        pushBlock = "OUTSIDE";
        leaveCompanyPushed_ = false;
    }

    scene_ = newScene;
    if (hasHome) {
        prevDistHome_ = dHome;
    }
    if (hasCo) {
        prevDistCompany_ = dCo;
    }
    prevTMs_ = feat.t_ms;

    TickDecision d;
    d.scene = newScene;
    d.score_home = sh.score;
    d.score_company = sc.score;
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
    return d;
}

}  // namespace commute_sa
