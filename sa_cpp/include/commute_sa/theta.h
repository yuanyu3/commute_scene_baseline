#pragma once

#include "commute_sa/leave_hsmm.h"

#include <algorithm>
#include <string>

namespace commute_sa {

/** Realtime HSMM and service-gate parameters (mirrors config/theta_default.json). */
struct Theta {
    /** Posterior P(LEAVING) enter/exit thresholds. */
    double enter_leave = 0.58;
    double exit_leave = 0.45;
    int min_evidence = 2;
    /**
     * Direct HSMM evidence strengths in [0,1]. The w_* member names remain
     * temporarily for source compatibility; they no longer use the historic
     * 0.25 + 3*w mapping. New JSON is persisted under evidence_strength.
     */
    double w_walk = 1.00;
    double w_pdr = 0.85;
    double w_geo = 0.85;
    double w_wifi = 0.61;
    double w_cell = 0.49;
    double w_ble = 0.0;
    /**
     * Legacy combined radio weight. Load-only fallback when w_wifi/w_cell/w_ble absent:
     * split ≈ 0.55/0.30/0.15 into wifi/cell/ble.
     */
    double w_radio = 0.15;
    double w_time = 0.85;
    /** Per-channel hit thresholds (s_i counts toward min_evidence if s_i >= thr_i). */
    double thr_walk = 0.5;
    double thr_pdr = 0.5;
    double thr_geo = 0.5;
    double thr_wifi = 0.5;
    double thr_cell = 0.5;
    double thr_ble = 0.5;
    double thr_time = 0.5;
    /** Jaccard at/below this → s_wifi = 1. */
    double thr_wifi_jaccard = 0.30;
    /** After approach ends, zero radio leave scores for this long (seconds). */
    double radio_suppress_after_approach_s = 120.0;
    double weekday_leave_home_hour = 8.25;
    double weekday_leave_company_hour = 18.2;
    double leave_window_min = 25.0;
    // Legacy config compatibility only; no longer gates realtime or replay.
    double arm_delay_s = 25.0;
    /** Explicit state-duration priors for the online HSMM. */
    double hsmm_preleave_min_s = 10.0;
    double hsmm_preleave_mean_s = 90.0;
    double hsmm_preleave_max_s = 300.0;
    double hsmm_leaving_min_s = 10.0;
    double hsmm_leaving_mean_s = 120.0;
    double hsmm_leaving_max_s = 600.0;
    double hsmm_max_gap_s = 300.0;
    double lead_min_s = 90.0;
    // Scoring target only; ETA has no upper realtime push gate.
    double lead_max_s = 240.0;
    double away_confirm_s = 180.0;
    double min_away_s = 1200.0;
    double push_cooldown_s = 1800.0;
    double max_gps_acc_m = 80.0;
    /** Legacy load compatibility; no source-specific dwell override is applied. */
    double allow_network_dwell_acc_m = 120.0;
    /** GPS direction reliability falls linearly from 1 to 0 across this range. */
    double gps_low_quality_start_m = 20.0;
    double gps_low_quality_zero_m = 120.0;
    /** Implausible radial speed is downweighted from this value and rejected at the maximum. */
    double gps_jump_speed_start_mps = 3.0;
    double gps_jump_speed_zero_mps = 15.0;
    double gps_approach_min_reliability = 0.50;
    double w_baro = 0.85;
    double baro_min_descent_m = 12.0;
    /**
     * Which leave flow is active: "company" | "home" | "both".
     * Training near office → "company" (disables DEPARTURE_NOTIFICATION / LEAVING_HOME).
     */
    std::string focus_side = "company";
};

inline bool FocusAllowsHome(const std::string &focus)
{
    return focus.empty() || focus == "both" || focus == "home";
}

inline bool FocusAllowsCompany(const std::string &focus)
{
    return focus.empty() || focus == "both" || focus == "company";
}

inline Theta DefaultTheta()
{
    return Theta {};
}

/** Build HSMM emission reliability from direct [0,1] evidence strengths. */
inline LeaveHsmmConfig HsmmConfigFromTheta(const Theta &theta)
{
    LeaveHsmmConfig config;
    config.preleave_min_s = std::max(0.0, theta.hsmm_preleave_min_s);
    config.preleave_mean_s = std::max(config.preleave_min_s, theta.hsmm_preleave_mean_s);
    config.preleave_max_s = std::max(config.preleave_mean_s, theta.hsmm_preleave_max_s);
    config.leaving_min_s = std::max(0.0, theta.hsmm_leaving_min_s);
    config.leaving_mean_s = std::max(config.leaving_min_s, theta.hsmm_leaving_mean_s);
    config.leaving_max_s = std::max(config.leaving_mean_s, theta.hsmm_leaving_max_s);
    config.max_gap_s = std::max(1.0, theta.hsmm_max_gap_s);
    const auto reliability = [](double strength) { return std::clamp(strength, 0.0, 1.0); };
    config.reliability = {{reliability(theta.w_walk), reliability(theta.w_pdr), reliability(theta.w_geo),
        reliability(theta.w_wifi), reliability(theta.w_cell), reliability(theta.w_ble), reliability(theta.w_time),
        reliability(theta.w_baro), reliability(theta.w_baro)}};
    return config;
}

bool LoadThetaFromFile(const std::string &path, Theta *out, std::string *err = nullptr);
bool ApplyThetaDelta(Theta *theta, const std::string &param, double delta, std::string *err = nullptr);

}  // namespace commute_sa
