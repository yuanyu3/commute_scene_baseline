#pragma once

#include <cstdint>
#include <string>

namespace commute_sa {

/**
 * Persisted policy document. Live SceneEngine ignores templates for push
 * (HSMM P(LEAVING) + product bans). Catalog is confirmed_leaving only.
 */
struct PersonalizationPolicy {
    int schema_version = 1;
    int revision = 0;
    bool enabled = true;
    std::string template_name = "confirmed_leaving";
    std::string trigger_phase = "LEAVING";  // PRE_LEAVE | LEAVING
    double probability_threshold = 0.58;
    double min_duration_s = 0.0;
    int min_independent_evidence = 2;
    bool require_walking = false;
    bool require_wifi_detach = false;
    bool require_radio = false;
    bool allow_cell_pdr_pair = true;
    std::string gps_mode = "OPTIONAL";  // IGNORE | OPTIONAL | REQUIRED
    /** Load-compat only. Live engine ignores baro_mode; baro auto-feeds HSMM when available. */
    std::string baro_mode = "OFF";
};

struct PolicyEvidence {
    int64_t t_ms = 0;
    double preleave_probability = 0.0;
    double leaving_probability = 0.0;
    int baseline_hits = 0;
    bool walking = false;
    double pdr_net_out_m = 0.0;
    bool wifi_detach = false;
    bool cell_leave = false;
    bool ble_detach = false;
    bool geo_outbound = false;
    bool has_usable_gps = false;
    bool baro_available = false;
    bool baro_baseline_ready = false;
    double baro_descent_m = 0.0;
    bool baro_lower_platform = false;
};

struct PolicyMatch {
    bool matched = false;
    std::string reason = "NONE";
    int independent_evidence = 0;
};

PersonalizationPolicy DefaultPersonalizationPolicy();
bool BuildPolicyTemplate(const std::string &name, PersonalizationPolicy *out, std::string *err = nullptr);
bool ValidatePersonalizationPolicy(const PersonalizationPolicy &policy, std::string *err = nullptr);
PolicyMatch MatchPersonalizationPolicy(const PersonalizationPolicy &policy, const PolicyEvidence &evidence);

bool LoadPersonalizationPolicyFromFile(
    const std::string &path, PersonalizationPolicy *out, std::string *err = nullptr);
bool SavePersonalizationPolicyToFile(
    const std::string &path, const PersonalizationPolicy &policy, std::string *err = nullptr);
std::string PersonalizationPolicyToJson(const PersonalizationPolicy &policy);
std::string PersonalizationPolicyCatalogJson();

}  // namespace commute_sa
