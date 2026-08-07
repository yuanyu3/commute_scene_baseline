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
    if (ExtractNumber(json, "w_walk", &v)) {
        t.w_walk = v;
    }
    if (ExtractNumber(json, "w_pdr", &v)) {
        t.w_pdr = v;
    }
    if (ExtractNumber(json, "w_geo", &v)) {
        t.w_geo = v;
    }
    if (ExtractNumber(json, "w_radio", &v)) {
        t.w_radio = v;
    }
    if (ExtractNumber(json, "w_time", &v)) {
        t.w_time = v;
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
    auto clip = [](double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); };
    if (param == "enter_leave") {
        theta->enter_leave = clip(theta->enter_leave + delta, 0.4, 0.85);
    } else if (param == "exit_leave") {
        theta->exit_leave = clip(theta->exit_leave + delta, 0.2, 0.7);
    } else if (param == "w_walk") {
        theta->w_walk = clip(theta->w_walk + delta, 0.05, 0.4);
    } else if (param == "w_radio") {
        theta->w_radio = clip(theta->w_radio + delta, 0.05, 0.4);
    } else if (param == "min_evidence") {
        const double v = clip(static_cast<double>(theta->min_evidence) + delta, 1.0, 5.0);
        theta->min_evidence = static_cast<int>(std::lround(v));
    } else if (param == "weekday_leave_home_hour") {
        theta->weekday_leave_home_hour = clip(theta->weekday_leave_home_hour + delta, 5.0, 11.0);
    } else if (param == "weekday_leave_company_hour") {
        theta->weekday_leave_company_hour = clip(theta->weekday_leave_company_hour + delta, 16.0, 21.0);
    } else if (param == "arm_delay_s") {
        theta->arm_delay_s = clip(theta->arm_delay_s + delta, 0.0, 90.0);
    } else if (param == "lead_min_s") {
        theta->lead_min_s = clip(theta->lead_min_s + delta, 30.0, 180.0);
        if (theta->lead_min_s > theta->lead_max_s) {
            theta->lead_max_s = theta->lead_min_s;
        }
    } else if (param == "lead_max_s") {
        theta->lead_max_s = clip(theta->lead_max_s + delta, 60.0, 600.0);
        if (theta->lead_max_s < theta->lead_min_s) {
            theta->lead_min_s = theta->lead_max_s;
        }
    } else {
        if (err) {
            *err = "unknown param: " + param;
        }
        return false;
    }
    return true;
}

}  // namespace commute_sa
