#pragma once

#include <string>

namespace commute_sa {

/** Realtime scorer parameters (mirrors config/theta_default.json). */
struct Theta {
    double enter_leave = 0.58;
    double exit_leave = 0.45;
    int min_evidence = 2;
    double w_walk = 0.25;
    double w_pdr = 0.20;
    double w_geo = 0.20;
    /** Split radio modalities (prefer these over legacy w_radio). */
    double w_wifi = 0.08;
    double w_cell = 0.05;
    double w_ble = 0.02;
    /**
     * Legacy combined radio weight. Load-only fallback when w_wifi/w_cell/w_ble absent:
     * split ≈ 0.55/0.30/0.15 into wifi/cell/ble.
     */
    double w_radio = 0.15;
    double w_time = 0.20;
    double weekday_leave_home_hour = 8.25;
    double weekday_leave_company_hour = 18.2;
    double leave_window_min = 25.0;
    double arm_delay_s = 25.0;
    double lead_min_s = 90.0;
    double lead_max_s = 240.0;
    double away_confirm_s = 180.0;
    double min_away_s = 1200.0;
    double push_cooldown_s = 1800.0;
    double max_gps_acc_m = 80.0;
    double allow_network_dwell_acc_m = 120.0;
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

bool LoadThetaFromFile(const std::string &path, Theta *out, std::string *err = nullptr);
bool ApplyThetaDelta(Theta *theta, const std::string &param, double delta, std::string *err = nullptr);

}  // namespace commute_sa
