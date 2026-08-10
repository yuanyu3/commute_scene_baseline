#include "commute_sa/pdr_evidence.h"

#include <cmath>
#include <sstream>

namespace commute_sa {
namespace {

bool UsableSide(Relation r)
{
    return r == Relation::kInside || r == Relation::kNear;
}

double Hypot2(double dx, double dy)
{
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

void PdrEvidence::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    walking_ = false;
    has_origin_ = false;
    walk_started_ms_ = 0;
    origin_x_ = 0.0;
    origin_y_ = 0.0;
    last_x_ = 0.0;
    last_y_ = 0.0;
    path_length_m_ = 0.0;
    point_count_ = 0;
    tagged_home_ = false;
    tagged_company_ = false;
}

void PdrEvidence::OnWalkingStarted(int64_t tMs)
{
    std::lock_guard<std::mutex> lock(mutex_);
    walking_ = true;
    has_origin_ = false;
    walk_started_ms_ = tMs;
    origin_x_ = 0.0;
    origin_y_ = 0.0;
    last_x_ = 0.0;
    last_y_ = 0.0;
    path_length_m_ = 0.0;
    point_count_ = 0;
    tagged_home_ = false;
    tagged_company_ = false;
}

void PdrEvidence::OnWalkingStopped(int64_t /*tMs*/)
{
    std::lock_guard<std::mutex> lock(mutex_);
    walking_ = false;
    // Keep last metrics until next start so the final tick of a walk can still score;
    // next OnWalkingStarted clears.
}

void PdrEvidence::OnPdrPoint(int64_t /*tMs*/, double xM, double yM)
{
    if (!std::isfinite(xM) || !std::isfinite(yM)) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!walking_) {
        return;
    }
    if (!has_origin_) {
        origin_x_ = xM;
        origin_y_ = yM;
        last_x_ = xM;
        last_y_ = yM;
        has_origin_ = true;
        point_count_ = 1;
        path_length_m_ = 0.0;
        return;
    }
    const double step = Hypot2(xM - last_x_, yM - last_y_);
    if (std::isfinite(step) && step >= 0.0) {
        path_length_m_ += step;
    }
    last_x_ = xM;
    last_y_ = yM;
    ++point_count_;
}

void PdrEvidence::NoteWalkContext(Relation homeRel, Relation companyRel)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!walking_) {
        return;
    }
    if (UsableSide(homeRel)) {
        tagged_home_ = true;
    }
    if (UsableSide(companyRel)) {
        tagged_company_ = true;
    }
}

PdrLeaveSnapshot PdrEvidence::Evaluate() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    PdrLeaveSnapshot out;
    out.walking = walking_;
    out.has_origin = has_origin_;
    out.point_count = point_count_;
    out.tagged_home = tagged_home_;
    out.tagged_company = tagged_company_;
    out.path_length_m = path_length_m_;

    if (!walking_) {
        out.reason = "NOT_WALKING";
        return out;
    }
    if (!has_origin_ || point_count_ < 2) {
        out.reason = has_origin_ ? "NEED_MORE_POINTS" : "NO_ORIGIN";
        return out;
    }

    out.net_displacement_m = Hypot2(last_x_ - origin_x_, last_y_ - origin_y_);
    if (out.path_length_m > 1e-6) {
        out.straightness = out.net_displacement_m / out.path_length_m;
        if (out.straightness > 1.0) {
            out.straightness = 1.0;
        }
    }

    // Indoor wandering: long path but tiny net → do not credit outbound leave.
    // Matches agent rule: "路径增长但净位移很小" is not LEAVING.
    double credit = out.net_displacement_m;
    if (out.path_length_m >= 20.0 && out.straightness < 0.25) {
        credit *= out.straightness / 0.25;
        out.reason = "LOW_STRAIGHTNESS";
    } else {
        out.reason = "OK";
    }

    // Attribute to sides tagged at walk start / first INSIDE|NEAR tick.
    // If still untagged (no GPS yet), expose net on both so weak-GPS indoor leave
    // can still use PDR; focus_side + ScoreLeaving gates still apply per side.
    if (tagged_home_ || tagged_company_) {
        if (tagged_home_) {
            out.pdr_net_out_home_m = credit;
        }
        if (tagged_company_) {
            out.pdr_net_out_company_m = credit;
        }
    } else {
        out.pdr_net_out_home_m = credit;
        out.pdr_net_out_company_m = credit;
        out.reason = "UNTAGGED_BOTH";
    }
    return out;
}

std::string PdrEvidence::DebugJson() const
{
    const PdrLeaveSnapshot s = Evaluate();
    std::ostringstream oss;
    oss << std::fixed;
    oss << "{\"walking\":" << (s.walking ? "true" : "false") << ",\"has_origin\":" << (s.has_origin ? "true" : "false")
        << ",\"points\":" << s.point_count << ",\"net_m\":" << s.net_displacement_m << ",\"path_m\":" << s.path_length_m
        << ",\"straightness\":" << s.straightness << ",\"net_home\":" << s.pdr_net_out_home_m
        << ",\"net_company\":" << s.pdr_net_out_company_m << ",\"tag_home\":" << (s.tagged_home ? "true" : "false")
        << ",\"tag_company\":" << (s.tagged_company ? "true" : "false") << ",\"reason\":\"" << s.reason << "\"}";
    return oss.str();
}

}  // namespace commute_sa
