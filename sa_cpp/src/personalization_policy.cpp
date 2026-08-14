#include "commute_sa/personalization_policy.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace commute_sa {
namespace {

bool ExtractString(const std::string &json, const char *key, std::string *out)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return false;
    const size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return false;
    *out = json.substr(pos + 1, end - pos - 1);
    return true;
}

bool ExtractNumber(const std::string &json, const char *key, double *out)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    char *end = nullptr;
    const double value = std::strtod(json.c_str() + pos + 1, &end);
    if (end == json.c_str() + pos + 1) return false;
    *out = value;
    return true;
}

bool ExtractBool(const std::string &json, const char *key, bool *out)
{
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    const size_t value = json.find_first_not_of(" \t\r\n", pos + 1);
    if (value == std::string::npos) return false;
    if (json.compare(value, 4, "true") == 0) { *out = true; return true; }
    if (json.compare(value, 5, "false") == 0) { *out = false; return true; }
    return false;
}

}  // namespace

PersonalizationPolicy DefaultPersonalizationPolicy()
{
    PersonalizationPolicy policy;
    BuildPolicyTemplate("confirmed_leaving", &policy, nullptr);
    return policy;
}

bool BuildPolicyTemplate(const std::string &name, PersonalizationPolicy *out, std::string *err)
{
    if (out == nullptr) return false;
    PersonalizationPolicy p;
    p.template_name = name;
    if (name == "confirmed_leaving") {
        p.trigger_phase = "LEAVING";
        p.probability_threshold = 0.58;
        p.min_duration_s = 0.0;
        p.min_independent_evidence = 2;
    } else if (name == "wifi_first_preleave") {
        p.trigger_phase = "PRE_LEAVE";
        p.probability_threshold = 0.50;
        p.min_duration_s = 5.0;
        p.min_independent_evidence = 2;
        p.require_walking = true;
        p.require_wifi_detach = true;
        p.require_radio = true;
        p.gps_mode = "IGNORE";
    } else if (name == "radio_motion_preleave") {
        p.trigger_phase = "PRE_LEAVE";
        p.probability_threshold = 0.52;
        p.min_duration_s = 5.0;
        p.min_independent_evidence = 2;
        p.require_walking = true;
        p.require_radio = true;
        p.gps_mode = "IGNORE";
    } else if (name == "conservative_preleave") {
        p.trigger_phase = "PRE_LEAVE";
        p.probability_threshold = 0.60;
        p.min_duration_s = 10.0;
        p.min_independent_evidence = 3;
        p.require_walking = true;
        p.require_wifi_detach = true;
        p.require_radio = true;
        p.gps_mode = "OPTIONAL";
    } else {
        if (err) *err = "unknown policy template";
        return false;
    }
    *out = p;
    return true;
}

bool ValidatePersonalizationPolicy(const PersonalizationPolicy &p, std::string *err)
{
    auto fail = [&](const char *message) { if (err) *err = message; return false; };
    PersonalizationPolicy known;
    if (!BuildPolicyTemplate(p.template_name, &known, nullptr)) return fail("unknown template_name");
    if (p.schema_version != 1) return fail("unsupported schema_version");
    if (p.trigger_phase != "PRE_LEAVE" && p.trigger_phase != "LEAVING") return fail("invalid trigger_phase");
    if (p.probability_threshold < 0.35 || p.probability_threshold > 0.90) return fail("probability_threshold out of bounds");
    if (p.min_duration_s < 0.0 || p.min_duration_s > 60.0) return fail("min_duration_s out of bounds");
    if (p.min_independent_evidence < 1 || p.min_independent_evidence > 5) return fail("min_independent_evidence out of bounds");
    if (p.gps_mode != "IGNORE" && p.gps_mode != "OPTIONAL" && p.gps_mode != "REQUIRED") return fail("invalid gps_mode");
    if (p.baro_mode != "OFF" && p.baro_mode != "SOFT" && p.baro_mode != "GATE" && p.baro_mode != "CONFIRM") return fail("invalid baro_mode");
    return true;
}

PolicyMatch MatchPersonalizationPolicy(const PersonalizationPolicy &p, const PolicyEvidence &e)
{
    PolicyMatch result;
    if (!p.enabled) { result.reason = "POLICY_DISABLED"; return result; }
    const double probability = p.trigger_phase == "PRE_LEAVE" ? e.preleave_probability : e.leaving_probability;
    if (probability < p.probability_threshold) { result.reason = "PROBABILITY"; return result; }
    const bool motion = e.walking || e.pdr_net_out_m >= 2.0;
    const bool radio = e.wifi_detach || e.cell_leave || e.ble_detach;
    result.independent_evidence = static_cast<int>(motion) + static_cast<int>(e.wifi_detach) +
        static_cast<int>(e.cell_leave) + static_cast<int>(e.ble_detach) + static_cast<int>(e.geo_outbound);
    if (p.require_walking && !e.walking) { result.reason = "WALKING_REQUIRED"; return result; }
    if (p.require_wifi_detach && !e.wifi_detach) { result.reason = "WIFI_REQUIRED"; return result; }
    if (p.require_radio && !radio) { result.reason = "RADIO_REQUIRED"; return result; }
    if (p.gps_mode == "REQUIRED" && !e.has_usable_gps) { result.reason = "GPS_REQUIRED"; return result; }
    // Wi-Fi-first PRE_LEAVE must still contain a short outward PDR excursion;
    // Wi-Fi loss alone is too easy to produce inside a large site.
    if (p.require_wifi_detach && !e.geo_outbound && e.pdr_net_out_m < 4.0) {
        result.reason = "PDR_REQUIRED";
        return result;
    }
    if (!p.allow_cell_pdr_pair && e.cell_leave && motion && !e.wifi_detach && !e.ble_detach && !e.geo_outbound) {
        result.reason = "CELL_PDR_PAIR_DISABLED";
        return result;
    }
    if (result.independent_evidence < p.min_independent_evidence || e.baseline_hits < p.min_independent_evidence) {
        result.reason = "INSUFFICIENT_EVIDENCE";
        return result;
    }
    result.matched = true;
    result.reason = "MATCH";
    return result;
}

std::string PersonalizationPolicyToJson(const PersonalizationPolicy &p)
{
    std::ostringstream out;
    out << "{\"schema_version\":" << p.schema_version << ",\"revision\":" << p.revision
        << ",\"enabled\":" << (p.enabled ? "true" : "false") << ",\"template_name\":\"" << p.template_name
        << "\",\"trigger_phase\":\"" << p.trigger_phase << "\",\"probability_threshold\":" << p.probability_threshold
        << ",\"min_duration_s\":" << p.min_duration_s << ",\"min_independent_evidence\":" << p.min_independent_evidence
        << ",\"require_walking\":" << (p.require_walking ? "true" : "false")
        << ",\"require_wifi_detach\":" << (p.require_wifi_detach ? "true" : "false")
        << ",\"require_radio\":" << (p.require_radio ? "true" : "false")
        << ",\"allow_cell_pdr_pair\":" << (p.allow_cell_pdr_pair ? "true" : "false")
        << ",\"gps_mode\":\"" << p.gps_mode << "\",\"baro_mode\":\"" << p.baro_mode << "\"}";
    return out.str();
}

bool LoadPersonalizationPolicyFromFile(const std::string &path, PersonalizationPolicy *out, std::string *err)
{
    if (out == nullptr) return false;
    std::ifstream in(path);
    if (!in) { if (err) *err = "policy file missing"; return false; }
    std::ostringstream body; body << in.rdbuf();
    PersonalizationPolicy p = DefaultPersonalizationPolicy();
    double number = 0.0;
    ExtractString(body.str(), "template_name", &p.template_name);
    ExtractString(body.str(), "trigger_phase", &p.trigger_phase);
    ExtractString(body.str(), "gps_mode", &p.gps_mode);
    ExtractString(body.str(), "baro_mode", &p.baro_mode);
    ExtractBool(body.str(), "enabled", &p.enabled);
    ExtractBool(body.str(), "require_walking", &p.require_walking);
    ExtractBool(body.str(), "require_wifi_detach", &p.require_wifi_detach);
    ExtractBool(body.str(), "require_radio", &p.require_radio);
    ExtractBool(body.str(), "allow_cell_pdr_pair", &p.allow_cell_pdr_pair);
    if (ExtractNumber(body.str(), "schema_version", &number)) p.schema_version = static_cast<int>(number);
    if (ExtractNumber(body.str(), "revision", &number)) p.revision = static_cast<int>(number);
    if (ExtractNumber(body.str(), "probability_threshold", &number)) p.probability_threshold = number;
    if (ExtractNumber(body.str(), "min_duration_s", &number)) p.min_duration_s = number;
    if (ExtractNumber(body.str(), "min_independent_evidence", &number)) p.min_independent_evidence = static_cast<int>(number);
    if (!ValidatePersonalizationPolicy(p, err)) return false;
    *out = p;
    return true;
}

bool SavePersonalizationPolicyToFile(const std::string &path, const PersonalizationPolicy &p, std::string *err)
{
    if (!ValidatePersonalizationPolicy(p, err)) return false;
    std::ofstream out(path, std::ios::trunc);
    if (!out) { if (err) *err = "cannot open policy file"; return false; }
    out << PersonalizationPolicyToJson(p) << "\n";
    return out.good();
}

std::string PersonalizationPolicyCatalogJson()
{
    return R"({"schema_version":1,"templates":[{"name":"confirmed_leaving","purpose":"HSMM P(LEAVING) push; product bans stay in C++"}],"bounds":{"probability_threshold":[0.35,0.9]},"note":"Live engine ignores policy templates for push. Tune w_* and enter_leave; do not invent hard-gate catalogs."})";
}

}  // namespace commute_sa
