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
    double w_walk = 0.25;
    double w_pdr = 0.20;
    double w_geo = 0.20;
    /** Split radio modalities (prefer these over legacy w_radio). */
    double w_wifi = 0.12;
    double w_cell = 0.08;
    double w_ble = 0.02;
    /**
     * Legacy combined radio weight. Load-only fallback when w_wifi/w_cell/w_ble absent:
     * split ≈ 0.55/0.30/0.15 into wifi/cell/ble.
     */
    double w_radio = 0.15;
    double w_time = 0.20;
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
    double lead_max_s = 240.0;
    double away_confirm_s = 180.0;
    double min_away_s = 1200.0;
    double push_cooldown_s = 1800.0;
    double max_gps_acc_m = 80.0;
    double allow_network_dwell_acc_m = 120.0;
    /**
     * Company vicinity (metres) in which source_type 2/1 overrides the GPS fence.
     * r_in/r_out remain auxiliary; indoor network fixes are often hundreds of metres off.
     */
    double company_source_vicinity_m = 400.0;
    double w_baro = 0.20;
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

/** Map θ observation weights onto HSMM emission reliability. */
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
    config.reliability = {{0.25 + 3.0 * theta.w_walk, 0.25 + 3.0 * theta.w_pdr, 0.25 + 3.0 * theta.w_geo,
        0.25 + 3.0 * theta.w_wifi, 0.25 + 3.0 * theta.w_cell, 0.25 + 3.0 * theta.w_ble, 0.25 + 3.0 * theta.w_time,
        0.25 + 3.0 * theta.w_baro, 0.25 + 3.0 * theta.w_baro}};
    return config;
}

bool LoadThetaFromFile(const std::string &path, Theta *out, std::string *err = nullptr);
bool ApplyThetaDelta(Theta *theta, const std::string &param, double delta, std::string *err = nullptr);

}  // namespace commute_sa
