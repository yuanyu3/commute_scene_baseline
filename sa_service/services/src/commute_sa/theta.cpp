#include "commute_sa/theta.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

namespace commute_sa {
namespace {

bool ReadFile(const std::string &path, std::string *out)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

bool ExtractNumber(const std::string &json, const std::string &key, double *val)
{
    std::regex re("\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch m;
    if (!std::regex_search(json, m, re)) {
        return false;
    }
    *val = std::stod(m[1].str());
    return true;
}

bool ExtractString(const std::string &json, const std::string &key, std::string *val)
{
    // Avoid matching "type":"label" when looking for key "label" via requiring quote-colon after key.
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (!std::regex_search(json, m, re)) {
        return false;
    }
    *val = m[1].str();
    return true;
}

double LegacyWeightToEvidenceStrength(double weight)
{
    return std::clamp(weight <= 0.0 ? 0.0 : 0.25 + 3.0 * weight, 0.0, 1.0);
}

}  // namespace

bool LoadThetaFromFile(const std::string &path, Theta *out, std::string *err)
{
    if (out == nullptr) {
        if (err) {
            *err = "out is null";
        }
        return false;
    }
    std::string json;
    if (!ReadFile(path, &json)) {
        if (err) {
            *err = "cannot read " + path;
        }
        return false;
    }
    Theta t = DefaultTheta();
    double v = 0.0;
    if (ExtractNumber(json, "enter_leave", &v)) {
        t.enter_leave = v;
    }
    if (ExtractNumber(json, "exit_leave", &v)) {
        t.exit_leave = v;
    }
    if (ExtractNumber(json, "min_evidence", &v)) {
        t.min_evidence = static_cast<int>(v);
    }
    const bool hasEvidenceStrength = json.find("\"evidence_strength\"") != std::string::npos;
    if (hasEvidenceStrength) {
        if (ExtractNumber(json, "walking", &v)) t.w_walk = std::clamp(v, 0.0, 1.0);
        if (ExtractNumber(json, "pdr", &v)) t.w_pdr = std::clamp(v, 0.0, 1.0);
        if (ExtractNumber(json, "geo", &v)) t.w_geo = std::clamp(v, 0.0, 1.0);
        if (ExtractNumber(json, "wifi", &v)) t.w_wifi = std::clamp(v, 0.0, 1.0);
        if (ExtractNumber(json, "cell", &v)) t.w_cell = std::clamp(v, 0.0, 1.0);
        if (ExtractNumber(json, "ble", &v)) t.w_ble = std::clamp(v, 0.0, 1.0);
        if (ExtractNumber(json, "time", &v)) t.w_time = std::clamp(v, 0.0, 1.0);
        if (ExtractNumber(json, "baro", &v)) t.w_baro = std::clamp(v, 0.0, 1.0);
    } else {
        if (ExtractNumber(json, "w_walk", &v)) t.w_walk = LegacyWeightToEvidenceStrength(v);
        if (ExtractNumber(json, "w_pdr", &v)) t.w_pdr = LegacyWeightToEvidenceStrength(v);
        if (ExtractNumber(json, "w_geo", &v)) t.w_geo = LegacyWeightToEvidenceStrength(v);
    }
    bool hasSplitRadio = false;
    if (!hasEvidenceStrength && ExtractNumber(json, "w_wifi", &v)) {
        t.w_wifi = LegacyWeightToEvidenceStrength(v);
        hasSplitRadio = true;
    }
    if (!hasEvidenceStrength && ExtractNumber(json, "w_cell", &v)) {
        t.w_cell = LegacyWeightToEvidenceStrength(v);
        hasSplitRadio = true;
    }
    if (!hasEvidenceStrength && ExtractNumber(json, "w_ble", &v)) {
        t.w_ble = LegacyWeightToEvidenceStrength(v);
        hasSplitRadio = true;
    }
    if (!hasEvidenceStrength && ExtractNumber(json, "w_radio", &v)) {
        t.w_radio = v;
        if (!hasSplitRadio) {
            // Legacy single radio weight → split across modalities.
            t.w_wifi = LegacyWeightToEvidenceStrength(v * 0.55);
            t.w_cell = LegacyWeightToEvidenceStrength(v * 0.30);
            t.w_ble = LegacyWeightToEvidenceStrength(v * 0.15);
        }
    }
    if (ExtractNumber(json, "thr_walk", &v)) {
        t.thr_walk = v;
    }
    if (ExtractNumber(json, "thr_pdr", &v)) {
        t.thr_pdr = v;
    }
    if (ExtractNumber(json, "thr_geo", &v)) {
        t.thr_geo = v;
    }
    if (ExtractNumber(json, "thr_wifi", &v)) {
        t.thr_wifi = v;
    }
    if (ExtractNumber(json, "thr_cell", &v)) {
        t.thr_cell = v;
    }
    if (ExtractNumber(json, "thr_ble", &v)) {
        t.thr_ble = v;
    }
    if (ExtractNumber(json, "thr_time", &v)) {
        t.thr_time = v;
    }
    if (ExtractNumber(json, "thr_wifi_jaccard", &v)) {
        t.thr_wifi_jaccard = v;
    }
    if (ExtractNumber(json, "radio_suppress_after_approach_s", &v)) {
        t.radio_suppress_after_approach_s = v;
    }
    if (!hasEvidenceStrength && ExtractNumber(json, "w_time", &v)) {
        t.w_time = LegacyWeightToEvidenceStrength(v);
    }
    if (ExtractNumber(json, "weekday_leave_home_hour", &v)) {
        t.weekday_leave_home_hour = v;
    }
    if (ExtractNumber(json, "weekday_leave_company_hour", &v)) {
        t.weekday_leave_company_hour = v;
    }
    if (ExtractNumber(json, "leave_window_min", &v)) {
        t.leave_window_min = v;
    }
    if (ExtractNumber(json, "arm_delay_s", &v)) {
        t.arm_delay_s = v;
    }
    if (ExtractNumber(json, "hsmm_preleave_min_s", &v)) {
        t.hsmm_preleave_min_s = v;
    }
    if (ExtractNumber(json, "hsmm_preleave_mean_s", &v)) {
        t.hsmm_preleave_mean_s = v;
    }
    if (ExtractNumber(json, "hsmm_preleave_max_s", &v)) {
        t.hsmm_preleave_max_s = v;
    }
    if (ExtractNumber(json, "hsmm_leaving_min_s", &v)) {
        t.hsmm_leaving_min_s = v;
    }
    if (ExtractNumber(json, "hsmm_leaving_mean_s", &v)) {
        t.hsmm_leaving_mean_s = v;
    }
    if (ExtractNumber(json, "hsmm_leaving_max_s", &v)) {
        t.hsmm_leaving_max_s = v;
    }
    if (ExtractNumber(json, "hsmm_max_gap_s", &v)) {
        t.hsmm_max_gap_s = v;
    }
    if (ExtractNumber(json, "lead_min_s", &v)) {
        t.lead_min_s = v;
    }
    if (ExtractNumber(json, "lead_max_s", &v)) {
        t.lead_max_s = v;
    }
    if (ExtractNumber(json, "away_confirm_s", &v)) {
        t.away_confirm_s = v;
    }
    if (ExtractNumber(json, "min_away_s", &v)) {
        t.min_away_s = v;
    }
    if (ExtractNumber(json, "push_cooldown_s", &v)) {
        t.push_cooldown_s = v;
    }
    if (ExtractNumber(json, "max_gps_acc_m", &v)) {
        t.max_gps_acc_m = v;
    }
    if (ExtractNumber(json, "allow_network_dwell_acc_m", &v)) {
        t.allow_network_dwell_acc_m = v;
    }
    if (ExtractNumber(json, "company_source_vicinity_m", &v)) {
        t.company_source_vicinity_m = v;
    }
    if (!hasEvidenceStrength && ExtractNumber(json, "w_baro", &v)) {
        t.w_baro = LegacyWeightToEvidenceStrength(v);
    }
    if (ExtractNumber(json, "baro_min_descent_m", &v)) {
        t.baro_min_descent_m = v;
    }
    std::string focus;
    if (ExtractString(json, "focus_side", &focus)) {
        if (focus == "home" || focus == "company" || focus == "both") {
            t.focus_side = focus;
        }
    }
    *out = t;
    return true;
}

bool ApplyThetaDelta(Theta *theta, const std::string &param, double delta, std::string *err)
{
    if (theta == nullptr) {
        if (err) {
            *err = "theta is null";
        }
        return false;
    }
    if (param == "enter_leave") {
        theta->enter_leave = theta->enter_leave + delta;
    } else if (param == "exit_leave") {
        theta->exit_leave = theta->exit_leave + delta;
    } else if (param == "w_walk") {
        theta->w_walk = theta->w_walk + delta;
    } else if (param == "w_pdr") {
        theta->w_pdr = theta->w_pdr + delta;
    } else if (param == "w_geo") {
        theta->w_geo = theta->w_geo + delta;
    } else if (param == "w_time") {
        theta->w_time = theta->w_time + delta;
    } else if (param == "w_baro") {
        theta->w_baro = theta->w_baro + delta;
    } else if (param == "w_wifi") {
        theta->w_wifi = theta->w_wifi + delta;
        theta->w_radio = theta->w_wifi + theta->w_cell + theta->w_ble;
    } else if (param == "w_cell") {
        theta->w_cell = theta->w_cell + delta;
        theta->w_radio = theta->w_wifi + theta->w_cell + theta->w_ble;
    } else if (param == "w_ble") {
        theta->w_ble = theta->w_ble + delta;
        theta->w_radio = theta->w_wifi + theta->w_cell + theta->w_ble;
    } else if (param == "w_radio") {
        // Legacy: nudge wifi primarily, keep cell/ble ratio.
        theta->w_wifi = theta->w_wifi + delta;
        theta->w_radio = theta->w_wifi + theta->w_cell + theta->w_ble;
    } else if (param == "min_evidence") {
        const double v = static_cast<double>(theta->min_evidence) + delta;
        theta->min_evidence = static_cast<int>(std::lround(v));
    } else if (param == "weekday_leave_home_hour") {
        theta->weekday_leave_home_hour = theta->weekday_leave_home_hour + delta;
    } else if (param == "weekday_leave_company_hour") {
        theta->weekday_leave_company_hour = theta->weekday_leave_company_hour + delta;
    } else if (param == "arm_delay_s") {
        theta->arm_delay_s = theta->arm_delay_s + delta;
    } else if (param == "hsmm_preleave_min_s") {
        theta->hsmm_preleave_min_s = theta->hsmm_preleave_min_s + delta;
        theta->hsmm_preleave_mean_s = std::max(theta->hsmm_preleave_min_s, theta->hsmm_preleave_mean_s);
        theta->hsmm_preleave_max_s = std::max(theta->hsmm_preleave_mean_s, theta->hsmm_preleave_max_s);
    } else if (param == "hsmm_preleave_mean_s") {
        theta->hsmm_preleave_mean_s = theta->hsmm_preleave_mean_s + delta;
        theta->hsmm_preleave_mean_s = std::max(theta->hsmm_preleave_min_s, theta->hsmm_preleave_mean_s);
        theta->hsmm_preleave_max_s = std::max(theta->hsmm_preleave_mean_s, theta->hsmm_preleave_max_s);
    } else if (param == "hsmm_preleave_max_s") {
        theta->hsmm_preleave_max_s = theta->hsmm_preleave_max_s + delta;
        theta->hsmm_preleave_mean_s = std::min(theta->hsmm_preleave_mean_s, theta->hsmm_preleave_max_s);
        theta->hsmm_preleave_min_s = std::min(theta->hsmm_preleave_min_s, theta->hsmm_preleave_mean_s);
    } else if (param == "hsmm_leaving_min_s") {
        theta->hsmm_leaving_min_s = theta->hsmm_leaving_min_s + delta;
        theta->hsmm_leaving_mean_s = std::max(theta->hsmm_leaving_min_s, theta->hsmm_leaving_mean_s);
        theta->hsmm_leaving_max_s = std::max(theta->hsmm_leaving_mean_s, theta->hsmm_leaving_max_s);
    } else if (param == "hsmm_leaving_mean_s") {
        theta->hsmm_leaving_mean_s = theta->hsmm_leaving_mean_s + delta;
        theta->hsmm_leaving_mean_s = std::max(theta->hsmm_leaving_min_s, theta->hsmm_leaving_mean_s);
        theta->hsmm_leaving_max_s = std::max(theta->hsmm_leaving_mean_s, theta->hsmm_leaving_max_s);
    } else if (param == "hsmm_leaving_max_s") {
        theta->hsmm_leaving_max_s = theta->hsmm_leaving_max_s + delta;
        theta->hsmm_leaving_mean_s = std::min(theta->hsmm_leaving_mean_s, theta->hsmm_leaving_max_s);
        theta->hsmm_leaving_min_s = std::min(theta->hsmm_leaving_min_s, theta->hsmm_leaving_mean_s);
    } else if (param == "lead_min_s") {
        theta->lead_min_s = theta->lead_min_s + delta;
        if (theta->lead_min_s > theta->lead_max_s) {
            theta->lead_max_s = theta->lead_min_s;
        }
    } else if (param == "lead_max_s") {
        theta->lead_max_s = theta->lead_max_s + delta;
        if (theta->lead_max_s < theta->lead_min_s) {
            theta->lead_min_s = theta->lead_max_s;
        }
    } else if (param == "baro_min_descent_m") {
        theta->baro_min_descent_m = theta->baro_min_descent_m + delta;
    } else {
        if (err) {
            *err = "unknown param: " + param;
        }
        return false;
    }
    // Preserve cross-parameter invariants for every caller, including the live
    // BaselineRuntime path. Absolute min/max are enforced by the action/optimizer
    // catalogs; these relationships cannot be validated one field at a time.
    theta->exit_leave = std::min(theta->exit_leave, theta->enter_leave - 0.01);
    theta->hsmm_preleave_mean_s = std::max(theta->hsmm_preleave_min_s, theta->hsmm_preleave_mean_s);
    theta->hsmm_preleave_max_s = std::max(theta->hsmm_preleave_mean_s, theta->hsmm_preleave_max_s);
    theta->hsmm_leaving_mean_s = std::max(theta->hsmm_leaving_min_s, theta->hsmm_leaving_mean_s);
    theta->hsmm_leaving_max_s = std::max(theta->hsmm_leaving_mean_s, theta->hsmm_leaving_max_s);
    theta->lead_max_s = std::max(theta->lead_min_s, theta->lead_max_s);
    return true;
}

}  // namespace commute_sa
