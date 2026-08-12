#pragma once

#include <cstdint>
#include <string>

namespace commute_sa {

/**
 * Bounded policy interpreted by the realtime engine. The agent may select a
 * template and tune the exposed fields, but cannot add executable code.
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
