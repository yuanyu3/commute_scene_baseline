#include "commute_sa/context_template.h"

#include "commute_sa/baseline_runtime.h"
#include "commute_sa/product_store.h"
#include "commute_sa/theta.h"
#include "commute_sa/theta_eval.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace commute_sa {
namespace {

struct Metrics {
    bool ok = false;
    double score = -1.0e100;
    double mean_lead_s = -1.0;
    double late_seconds = 0.0;
    int n_episodes = 0;
    int n_false_push = 0;
    int n_confirmed_leave = 0;
    int n_missed_leave_label = 0;
    int false_kept = 0;
    int false_avoided = 0;
    int soft_false_kept = 0;
    int confirmed_kept = 0;
    int missed_leave = 0;
    int recovered_miss = 0;
};

struct TemplateSpec {
    int ready_prefix_length = 0; // 0: legacy fusion; fitted only by bounded replay.
    std::string template_name;
    std::string side = "company";
    std::string anchor_id = "company_001";
    std::string applicability = "baro_ready";
    std::vector<std::string> positive_sequence;
    std::vector<std::string> negative_pattern;
    std::vector<std::string> parameter_families;
    std::vector<std::string> unavailable_parameter_families;
    bool personalized_time = false;
    double time_center_hour = 0.0;
    double time_window_min = 0.0;
    int time_sample_count = 0;
    bool personalized_vertical_threshold = false;
    double baro_min_descent_m = 0.0;
    int baro_sample_count = 0;
    std::string rationale;
};

struct Candidate {
    TemplateSpec spec;
    int id = 0;
    std::string strength_name;
    double strength = 0.0;
    Metrics metrics;
    bool eligible = false;
    std::string rejection;
};

struct Trial {
    bool active = false;
    TemplateSpec spec;
    Metrics baseline;
    Metrics incumbent;
    std::vector<Candidate> candidates;
    int best_index = -1;
};

std::mutex gTemplateMutex;
Trial gTrial;
bool gActiveLoaded = false;
bool gActivePresent = false;
TemplateSpec gActiveSpec;
double gActiveStrength = 0.0;
size_t gActiveSequenceIndex = 0;
int64_t gActiveLastMatchMs = 0;

std::string RootDir()
{
    const std::string root = ProductStore::GetInstance().RootDir();
    return root.empty() ? "/data/service/el1/public/commuteagentservice" : root;
}

Theta CurrentTheta()
{
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        return BaselineRuntime::GetInstance().Engine()->GetTheta();
    }
    Theta theta = DefaultTheta();
    LoadThetaFromFile(RootDir() + "/theta.json", &theta, nullptr);
    return theta;
}

int64_t NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

double Clip01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

std::string Esc(const std::string &value)
{
    std::string out;
    for (char c : value) {
        if (c == '\\' || c == '"') out.push_back('\\');
        if (c == '\n' || c == '\r') out.push_back(' '); else out.push_back(c);
    }
    return out;
}

bool ExtractString(const std::string &json, const char *key, std::string *out)
{
    if (out == nullptr || key == nullptr) return false;
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return false;
    ++pos;
    std::string value;
    bool escaped = false;
    for (; pos < json.size(); ++pos) {
        const char c = json[pos];
        if (escaped) {
            value.push_back(c == 'n' ? '\n' : c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            *out = value;
            return true;
        } else {
            value.push_back(c);
        }
    }
    return false;
}

bool ExtractNumber(const std::string &json, const char *key, double *out)
{
    if (out == nullptr || key == nullptr) return false;
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos) return false;
    char *end = nullptr;
    const double value = std::strtod(json.c_str() + pos, &end);
    if (end == json.c_str() + pos) return false;
    *out = value;
    return true;
}

bool ExtractBool(const std::string &json, const char *key, bool *out)
{
    if (out == nullptr || key == nullptr) return false;
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (json.compare(pos, 4, "true") == 0) { *out = true; return true; }
    if (json.compare(pos, 5, "false") == 0) { *out = false; return true; }
    return false;
}

std::vector<std::string> SplitCsv(const std::string &csv)
{
    std::vector<std::string> result;
    std::stringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token.erase(token.begin(), std::find_if(token.begin(), token.end(), [](unsigned char c) { return !std::isspace(c); }));
        token.erase(std::find_if(token.rbegin(), token.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), token.end());
        if (!token.empty()) result.push_back(token);
    }
    return result;
}

std::string JoinCsv(const std::vector<std::string> &values)
{
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ',';
        out << values[i];
    }
    return out.str();
}

const std::set<std::string> &SupportedEvents()
{
    static const std::set<std::string> events = {"walking", "pdr_outbound", "geo_outbound", "wifi_detach",
        "cell_detach", "ble_detach", "baro_descending", "lower_platform", "outside", "approaching",
        "attached", "no_baro_descent", "no_geo_outbound"};
    return events;
}

const std::set<std::string> &SupportedParameterFamilies()
{
    static const std::set<std::string> families = {"departure_time", "vertical_threshold"};
    return families;
}

const std::set<std::string> &EnabledParameterFamilies()
{
    // departure_time remains readable for schema compatibility, but is not
    // generatable while evaluation data are collected at artificial times.
    static const std::set<std::string> families = {"vertical_threshold"};
    return families;
}

double DecimalHourLocal(int64_t tMs)
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

double CircularHourDistance(double a, double b)
{
    const double d = std::fabs(a - b);
    return std::min(d, 24.0 - d);
}

double PersonalizedTimePrior(int64_t tMs, double centerHour, double windowMin)
{
    if (tMs <= 0 || windowMin <= 0.0) return 0.0;
    const double halfHours = windowMin / 60.0;
    const double distance = CircularHourDistance(DecimalHourLocal(tMs), centerHour);
    return distance <= halfHours ? Clip01(1.0 - distance / std::max(halfHours, 1.0e-3)) : 0.0;
}

double Quantile(std::vector<double> values, double q)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(std::round(
        Clip01(q) * static_cast<double>(values.size() - 1)));
    return values[index];
}

struct EpisodeParameterEvidence {
    std::string side;
    std::string label;
    int64_t outcome_ms = 0;
    double max_baro_descent_m = 0.0;
    bool lower_platform = false;
};

std::vector<EpisodeParameterEvidence> LoadParameterEvidence()
{
    std::ifstream in(RootDir() + "/policy_history.jsonl");
    std::map<std::string, EpisodeParameterEvidence> grouped;
    std::string line;
    while (std::getline(in, line)) {
        int64_t outcomeMs = 0;
        double outcomeValue = 0.0;
        std::string side;
        std::string label;
        if (!ExtractNumber(line, "outcome_t_ms", &outcomeValue) ||
            !ExtractString(line, "side", &side) || !ExtractString(line, "label", &label)) {
            continue;
        }
        outcomeMs = static_cast<int64_t>(outcomeValue);
        const std::string key = side + ":" + std::to_string(outcomeMs) + ":" + label;
        auto &episode = grouped[key];
        episode.side = side;
        episode.label = label;
        episode.outcome_ms = outcomeMs;
        double descent = 0.0;
        if (ExtractNumber(line, "baro_descent_m", &descent)) {
            episode.max_baro_descent_m = std::max(episode.max_baro_descent_m, descent);
        }
        double lower = 0.0;
        if (ExtractNumber(line, "obs_baro_lower_platform", &lower) && lower >= 0.5) {
            episode.lower_platform = true;
        }
    }
    std::vector<EpisodeParameterEvidence> result;
    for (auto &entry : grouped) result.push_back(std::move(entry.second));
    return result;
}

void EstimateRequestedParameters(const Theta &theta, TemplateSpec *spec)
{
    if (spec == nullptr || spec->parameter_families.empty()) return;
    const auto episodes = LoadParameterEvidence();
    for (const auto &family : spec->parameter_families) {
        if (family == "departure_time") {
            std::vector<double> hours;
            for (const auto &episode : episodes) {
                if (episode.side == spec->side && episode.label == "CONFIRMED_LEAVE" && episode.outcome_ms > 0) {
                    hours.push_back(DecimalHourLocal(episode.outcome_ms));
                }
            }
            spec->time_sample_count = static_cast<int>(hours.size());
            if (hours.size() < 3) {
                spec->unavailable_parameter_families.push_back("departure_time:need_3_confirmed_leave_episodes");
                continue;
            }
            double sinSum = 0.0;
            double cosSum = 0.0;
            constexpr double kPi = 3.14159265358979323846;
            for (double hour : hours) {
                const double angle = hour * 2.0 * kPi / 24.0;
                sinSum += std::sin(angle);
                cosSum += std::cos(angle);
            }
            double angle = std::atan2(sinSum, cosSum);
            if (angle < 0.0) angle += 2.0 * kPi;
            spec->time_center_hour = angle * 24.0 / (2.0 * kPi);
            std::vector<double> distancesMin;
            for (double hour : hours) {
                distancesMin.push_back(60.0 * CircularHourDistance(hour, spec->time_center_hour));
            }
            spec->time_window_min = std::max(15.0, std::min(120.0, Quantile(distancesMin, 0.8) + 10.0));
            spec->personalized_time = true;
        } else if (family == "vertical_threshold") {
            std::vector<double> positives;
            std::vector<double> hardFalse;
            for (const auto &episode : episodes) {
                if (episode.side != spec->side || episode.max_baro_descent_m <= 0.0) continue;
                // A labeled lobby/lower-platform intermediate is valid
                // vertical evidence even if the user has not crossed outside.
                if ((episode.label == "CONFIRMED_LEAVE" || episode.label == "FALSE_PUSH") &&
                    episode.lower_platform) {
                    positives.push_back(episode.max_baro_descent_m);
                } else if ((episode.label == "FALSE_PUSH" || episode.label == "TRUE_NEGATIVE") &&
                    !episode.lower_platform) {
                    hardFalse.push_back(episode.max_baro_descent_m);
                }
            }
            spec->baro_sample_count = static_cast<int>(positives.size());
            if (positives.size() < 3) {
                spec->unavailable_parameter_families.push_back(
                    "vertical_threshold:need_3_validated_lower_platform_episodes");
                continue;
            }
            const double positiveLow = Quantile(positives, 0.2);
            const double falseHigh = hardFalse.empty() ? 0.0 : Quantile(hardFalse, 0.9);
            double threshold = positiveLow * 0.75;
            if (!hardFalse.empty() && positiveLow > falseHigh + 2.0) {
                threshold = 0.5 * (positiveLow + falseHigh);
            }
            // Old history only proves stability when lower_platform was true,
            // so lowering the live threshold cannot be replayed faithfully.
            threshold = std::max(theta.baro_min_descent_m, threshold);
            threshold = std::max(4.0, std::min(30.0, threshold));
            spec->baro_min_descent_m = 2.0 * std::round(threshold / 2.0);
            spec->personalized_vertical_threshold = true;
        }
    }
}

bool EventActive(const std::string &event, const LeaveObservation &obs)
{
    if (event == "walking") return obs.walking >= 0.5;
    if (event == "pdr_outbound") return obs.pdr_outbound >= 0.5;
    if (event == "geo_outbound") return obs.geo_outbound >= 0.5;
    if (event == "wifi_detach") return obs.wifi_detach >= 0.5;
    if (event == "cell_detach") return obs.cell_detach >= 0.5;
    if (event == "ble_detach") return obs.ble_detach >= 0.5;
    if (event == "baro_descending") return obs.baro_descending >= 0.5;
    if (event == "lower_platform") return obs.baro_lower_platform >= 0.5;
    if (event == "outside") return obs.outside;
    if (event == "approaching") return obs.approaching;
    if (event == "attached") return obs.attached;
    if (event == "no_baro_descent") {
        return obs.baro_available && obs.baro_descending < 0.25 && obs.baro_lower_platform < 0.5;
    }
    if (event == "no_geo_outbound") return obs.geo_outbound < 0.25;
    return false;
}

bool ApplySpec(const TemplateSpec &spec, double strength, LeaveObservation *obs, size_t *sequenceIndex)
{
    if (obs == nullptr || sequenceIndex == nullptr) return false;
    // Replaying an observation that was previously produced under another
    // active template must not carry old template evidence into this trial.
    obs->sequence_available = true;
    obs->sequence_progress = 0.0;
    obs->sequence_complete = 0.0;
    obs->sequence_ready = spec.ready_prefix_length > 0 ? 0.0 : -1.0;
    obs->negative_pattern_match = 0.0;
    obs->sequence_reliability = Clip01(strength);
    if (spec.personalized_time && obs->t_ms > 0) {
        obs->time_prior = PersonalizedTimePrior(obs->t_ms, spec.time_center_hour, spec.time_window_min);
    }
    if (spec.personalized_vertical_threshold && obs->baro_available && obs->baro_stable_platform_known) {
        obs->baro_lower_platform = obs->baro_stable_platform &&
            obs->baro_descent_m >= spec.baro_min_descent_m ? 1.0 : 0.0;
    }
    const size_t previousIndex = *sequenceIndex;
    if (*sequenceIndex < spec.positive_sequence.size() &&
        EventActive(spec.positive_sequence[*sequenceIndex], *obs)) {
        ++(*sequenceIndex);
    }
    if (!spec.positive_sequence.empty()) {
        const double progress = static_cast<double>(*sequenceIndex) /
            static_cast<double>(spec.positive_sequence.size());
        obs->sequence_progress = Clip01(progress);
        if (spec.ready_prefix_length > 0) {
            obs->sequence_ready = *sequenceIndex >= static_cast<size_t>(spec.ready_prefix_length) ? 1.0 : 0.0;
        }
        if (*sequenceIndex >= spec.positive_sequence.size()) {
            obs->sequence_complete = 1.0;
        }
    }

    bool negative = !spec.negative_pattern.empty();
    for (const auto &event : spec.negative_pattern) negative = negative && EventActive(event, *obs);
    if (negative) {
        obs->negative_pattern_match = 1.0;
    }
    return *sequenceIndex > previousIndex;
}

void LoadActiveTemplateLocked()
{
    gActiveLoaded = true;
    gActivePresent = false;
    gActiveSequenceIndex = 0;
    gActiveLastMatchMs = 0;

    std::ifstream in(RootDir() + "/active_context_template.json");
    if (!in.is_open()) return;
    std::stringstream body;
    body << in.rdbuf();
    const std::string json = body.str();
    TemplateSpec spec;
    std::string positiveCsv;
    std::string negativeCsv;
    std::string parameterFamiliesCsv;
    double strength = 0.0;
    if (!ExtractString(json, "template_name", &spec.template_name) ||
        !ExtractString(json, "side", &spec.side) ||
        !ExtractString(json, "anchor_id", &spec.anchor_id) ||
        !ExtractString(json, "applicability", &spec.applicability) ||
        !ExtractString(json, "positive_sequence", &positiveCsv) ||
        !ExtractNumber(json, "strength", &strength)) {
        return;
    }
    ExtractString(json, "negative_pattern", &negativeCsv);
    ExtractString(json, "parameter_families", &parameterFamiliesCsv);
    ExtractString(json, "rationale", &spec.rationale);
    spec.positive_sequence = SplitCsv(positiveCsv);
    spec.negative_pattern = SplitCsv(negativeCsv);
    spec.parameter_families = SplitCsv(parameterFamiliesCsv);
    ExtractBool(json, "personalized_time", &spec.personalized_time);
    ExtractNumber(json, "time_center_hour", &spec.time_center_hour);
    ExtractNumber(json, "time_window_min", &spec.time_window_min);
    double count = 0.0;
    if (ExtractNumber(json, "ready_prefix_length", &count)) {
        if (!std::isfinite(count) || count < 0 || count > spec.positive_sequence.size() ||
            count != std::floor(count)) return;
        spec.ready_prefix_length = static_cast<int>(count);
    }
    if (ExtractNumber(json, "time_sample_count", &count)) spec.time_sample_count = static_cast<int>(count);
    ExtractBool(json, "personalized_vertical_threshold", &spec.personalized_vertical_threshold);
    ExtractNumber(json, "baro_min_descent_m", &spec.baro_min_descent_m);
    if (ExtractNumber(json, "baro_sample_count", &count)) spec.baro_sample_count = static_cast<int>(count);
    if (strength <= 0.0 || strength > 1.0) return;
    for (const auto &event : spec.positive_sequence) if (SupportedEvents().count(event) == 0) return;
    for (const auto &event : spec.negative_pattern) if (SupportedEvents().count(event) == 0) return;
    for (const auto &family : spec.parameter_families) {
        if (SupportedParameterFamilies().count(family) == 0) return;
    }
    if (spec.personalized_time &&
        (spec.time_center_hour < 0.0 || spec.time_center_hour >= 24.0 ||
         spec.time_window_min < 15.0 || spec.time_window_min > 120.0 || spec.time_sample_count < 3)) {
        return;
    }
    if (spec.personalized_vertical_threshold &&
        (spec.baro_min_descent_m < 4.0 || spec.baro_min_descent_m > 30.0 || spec.baro_sample_count < 3)) {
        return;
    }
    gActiveSpec = std::move(spec);
    gActiveStrength = strength;
    gActivePresent = true;
}

Metrics ParseMetrics(const std::string &json)
{
    Metrics metrics;
    ExtractBool(json, "ok", &metrics.ok);
    ExtractNumber(json, "mean_lead_s", &metrics.mean_lead_s);
    ExtractNumber(json, "late_seconds", &metrics.late_seconds);
    double value = 0.0;
    auto integer = [&](const char *key, int *target) {
        if (ExtractNumber(json, key, &value)) *target = static_cast<int>(value);
    };
    ExtractNumber(json, "score", &metrics.score);
    integer("n_episodes", &metrics.n_episodes);
    integer("n_false_push", &metrics.n_false_push);
    integer("n_confirmed_leave", &metrics.n_confirmed_leave);
    integer("n_missed_leave_label", &metrics.n_missed_leave_label);
    integer("false_kept", &metrics.false_kept);
    integer("false_avoided", &metrics.false_avoided);
    integer("soft_false_kept", &metrics.soft_false_kept);
    integer("confirmed_kept", &metrics.confirmed_kept);
    integer("missed_leave", &metrics.missed_leave);
    integer("recovered_miss", &metrics.recovered_miss);
    return metrics;
}

std::string MetricsJson(const Metrics &m)
{
    std::ostringstream out;
    out << "{\"score_version\":2,\"mean_lead_s\":" << m.mean_lead_s
        << ",\"late_seconds\":" << m.late_seconds
        << ",\"score\":" << m.score << ",\"n_episodes\":" << m.n_episodes
        << ",\"n_false_push\":" << m.n_false_push << ",\"n_confirmed_leave\":" << m.n_confirmed_leave
        << ",\"n_missed_leave_label\":" << m.n_missed_leave_label << ",\"false_kept\":" << m.false_kept
        << ",\"false_avoided\":" << m.false_avoided << ",\"soft_false_kept\":" << m.soft_false_kept
        << ",\"confirmed_kept\":" << m.confirmed_kept << ",\"missed_leave\":" << m.missed_leave
        << ",\"recovered_miss\":" << m.recovered_miss << '}';
    return out.str();
}

bool Eligible(const Metrics &base, const Metrics &candidate, std::string *why)
{
    if (!candidate.ok || candidate.n_episodes <= 0) { if (why) *why = "evaluation_failed"; return false; }
    if (base.n_confirmed_leave + base.n_missed_leave_label <= 0) {
        if (why) *why = "insufficient_positive_history";
        return false;
    }
    if (candidate.score < base.score + 0.25) { if (why) *why = "score_not_improved"; return false; }
    if (candidate.missed_leave > base.missed_leave) { if (why) *why = "missed_leave_increased"; return false; }
    if (candidate.confirmed_kept < base.confirmed_kept) { if (why) *why = "confirmed_recall_decreased"; return false; }
    if (candidate.soft_false_kept < base.soft_false_kept) { if (why) *why = "lower_platform_positive_decreased"; return false; }
    if (candidate.false_kept > base.false_kept) { if (why) *why = "hard_false_push_increased"; return false; }
    return true;
}

ObservationAdapter BuildAdapter(const TemplateSpec &spec, double strength)
{
    size_t sequenceIndex = 0;
    int64_t lastMatchMs = 0;
    return [spec, strength, sequenceIndex, lastMatchMs](LeaveObservation &obs, bool episodeStart) mutable {
        if (episodeStart) { sequenceIndex = 0; lastMatchMs = 0; }
        obs.sequence_available = false;
        obs.sequence_progress = 0.0;
        obs.sequence_complete = 0.0;
        obs.sequence_ready = -1.0;
        obs.negative_pattern_match = 0.0;
        obs.sequence_reliability = 0.0;
        if (!obs.context_side.empty() && obs.context_side != spec.side) return;
        if (spec.applicability == "baro_ready" && !obs.baro_available) return;
        if (lastMatchMs > 0 && (obs.t_ms < lastMatchMs || obs.t_ms - lastMatchMs > 600000)) {
            sequenceIndex = 0;
            lastMatchMs = 0;
        }
        if (ApplySpec(spec, strength, &obs, &sequenceIndex)) lastMatchMs = obs.t_ms;
        if (obs.outside || obs.approaching || obs.attached) { sequenceIndex = 0; lastMatchMs = 0; }
    };
}

std::string SpecJson(const TemplateSpec &spec, const std::string &strengthName = "", double strength = 0.0)
{
    std::ostringstream out;
    out << "{\"schema_version\":4,\"ready_prefix_length\":" << spec.ready_prefix_length
        << ",\"template_name\":\"" << Esc(spec.template_name)
        << "\",\"side\":\"" << Esc(spec.side) << "\",\"anchor_id\":\"" << Esc(spec.anchor_id)
        << "\",\"applicability\":\"" << Esc(spec.applicability)
        << "\",\"positive_sequence\":\"" << Esc(JoinCsv(spec.positive_sequence))
        << "\",\"negative_pattern\":\"" << Esc(JoinCsv(spec.negative_pattern))
        << "\",\"parameter_families\":\"" << Esc(JoinCsv(spec.parameter_families))
        << "\",\"positive_effect\":\"emit_progress_completion_and_optional_prefix_readiness\","
           "\"negative_effect\":\"emit_negative_pattern_match\"";
    out << ",\"personalized_time\":" << (spec.personalized_time ? "true" : "false")
        << ",\"time_center_hour\":" << spec.time_center_hour
        << ",\"time_window_min\":" << spec.time_window_min
        << ",\"time_sample_count\":" << spec.time_sample_count
        << ",\"personalized_vertical_threshold\":"
        << (spec.personalized_vertical_threshold ? "true" : "false")
        << ",\"baro_min_descent_m\":" << spec.baro_min_descent_m
        << ",\"baro_sample_count\":" << spec.baro_sample_count
        << ",\"unavailable_parameter_families\":\""
        << Esc(JoinCsv(spec.unavailable_parameter_families)) << "\"";
    if (!strengthName.empty()) out << ",\"strength_level\":\"" << strengthName << "\",\"strength\":" << strength;
    out << ",\"rationale\":\"" << Esc(spec.rationale) << "\"}";
    return out.str();
}

std::string TrialJson(const Trial &trial)
{
    std::ostringstream out;
    out << "{\"ok\":true,\"trial_active\":" << (trial.active ? "true" : "false")
        << ",\"generated_template\":" << SpecJson(trial.spec) << ",\"baseline\":" << MetricsJson(trial.baseline)
        << ",\"incumbent\":" << MetricsJson(trial.incumbent)
        << ",\"best_candidate_id\":";
    if (trial.best_index >= 0) out << trial.candidates[trial.best_index].id; else out << "null";
    out << ",\"candidates\":[";
    for (size_t i = 0; i < trial.candidates.size(); ++i) {
        if (i) out << ',';
        const auto &candidate = trial.candidates[i];
        out << "{\"id\":" << candidate.id << ",\"strength_level\":\"" << candidate.strength_name
            << "\",\"strength\":" << candidate.strength << ",\"eligible\":"
            << (candidate.eligible ? "true" : "false") << ",\"rejection\":\"" << Esc(candidate.rejection)
            << "\",\"template\":" << SpecJson(candidate.spec, candidate.strength_name, candidate.strength)
            << ",\"metrics\":" << MetricsJson(candidate.metrics) << '}';
    }
    out << "],\"commit_guard\":{\"score_must_improve\":true,\"hard_false_must_not_increase\":true,"
           "\"confirmed_and_lower_platform_positives_must_not_decrease\":true,\"missed_must_not_increase\":true,"
           "\"per_episode_no_new_false_or_lost_positive\":true,\"per_episode_positive_must_not_be_later\":true}}";
    return out.str();
}

}  // namespace

std::string GetContextTemplateCatalogAction(const std::string &)
{
    return "{\"ok\":true,\"schema_version\":4,\"design\":\"Agent composes supported primitives and requests parameter families; C++ estimates all numeric values\","
           "\"applicability\":[\"always\",\"baro_ready\"],"
           "\"events\":[\"walking\",\"pdr_outbound\",\"geo_outbound\",\"wifi_detach\",\"cell_detach\","
           "\"ble_detach\",\"baro_descending\",\"lower_platform\",\"outside\",\"approaching\","
           "\"attached\",\"no_baro_descent\",\"no_geo_outbound\"],"
           "\"effects\":{\"positive_sequence\":\"emit progress/completion and replay-calibrated prefix readiness; readiness replaces, never adds to, legacy positive evidence\","
           "\"negative_pattern\":\"emit an independent negative_pattern_match observation\","
           "\"strength\":\"emit sequence_reliability selected by deterministic replay\"},"
           "\"parameter_families\":{\"vertical_threshold\":\"anchor-scoped stable descent threshold; needs 3 validated lower-platform episodes\"},"
           "\"disabled_parameter_families\":{\"departure_time\":\"disabled while collection timestamps are not representative of normal behavior\"},"
           "\"ready_prefix_length\":\"C++ evaluates legacy mode and each causal prefix; agent supplies sequence only\","
           "\"strengths\":\"LOW|MEDIUM|HIGH selected by deterministic replay with continuous lead score and no false/missed regression\"}";
}

std::string GenerateContextTemplateAction(const std::string &paramsJson)
{
    TemplateSpec spec;
    std::string positiveCsv;
    std::string negativeCsv;
    std::string parameterFamiliesCsv;
    if (!ExtractString(paramsJson, "template_name", &spec.template_name) || spec.template_name.empty() ||
        !ExtractString(paramsJson, "positive_sequence", &positiveCsv) || positiveCsv.empty()) {
        return "{\"ok\":false,\"error\":\"template_name and comma-separated positive_sequence required\"}";
    }
    ExtractString(paramsJson, "negative_pattern", &negativeCsv);
    ExtractString(paramsJson, "parameter_families", &parameterFamiliesCsv);
    ExtractString(paramsJson, "side", &spec.side);
    ExtractString(paramsJson, "anchor_id", &spec.anchor_id);
    ExtractString(paramsJson, "applicability", &spec.applicability);
    ExtractString(paramsJson, "rationale", &spec.rationale);
    if (spec.side != "company" && spec.side != "home") {
        return "{\"ok\":false,\"error\":\"side must be company|home\"}";
    }
    if (spec.applicability != "always" && spec.applicability != "baro_ready") {
        return "{\"ok\":false,\"error\":\"applicability must be always|baro_ready\"}";
    }
    spec.positive_sequence = SplitCsv(positiveCsv);
    spec.negative_pattern = SplitCsv(negativeCsv);
    spec.parameter_families = SplitCsv(parameterFamiliesCsv);
    std::set<std::string> seenFamilies;
    for (const auto &family : spec.parameter_families) {
        if (EnabledParameterFamilies().count(family) == 0) {
            return "{\"ok\":false,\"error\":\"parameter family disabled or unsupported\",\"family\":\"" + Esc(family) + "\"}";
        }
        if (!seenFamilies.insert(family).second) {
            return "{\"ok\":false,\"error\":\"duplicate parameter family\",\"family\":\"" + Esc(family) + "\"}";
        }
    }
    for (const auto &event : spec.positive_sequence) {
        if (SupportedEvents().count(event) == 0 || event.rfind("no_", 0) == 0) {
            return "{\"ok\":false,\"error\":\"unsupported positive event\",\"event\":\"" + Esc(event) + "\"}";
        }
    }
    for (const auto &event : spec.negative_pattern) {
        if (SupportedEvents().count(event) == 0) {
            return "{\"ok\":false,\"error\":\"unsupported negative event\",\"event\":\"" + Esc(event) + "\"}";
        }
    }
    if (spec.positive_sequence.size() > 6 || spec.negative_pattern.size() > 6) {
        return "{\"ok\":false,\"error\":\"template primitive budget exceeded\",\"max_per_clause\":6}";
    }

    const Theta theta = CurrentTheta();
    EstimateRequestedParameters(theta, &spec);
    std::vector<ReplayEpisodeSummary> baselineEpisodes, incumbentEpisodes;
    const Metrics baseline = ParseMetrics(EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), theta, {}, 0, 100, false, 0, &baselineEpisodes));
    if (!baseline.ok) return "{\"ok\":false,\"error\":\"baseline history replay unavailable\"}";

    Trial trial;
    trial.active = true;
    trial.spec = spec;
    trial.baseline = baseline;
    // Replacing an existing template must not regress it merely because the
    // candidate beats an unpersonalized baseline.
    TemplateSpec incumbentSpec;
    double incumbentStrength = 0.0;
    {
        std::lock_guard<std::mutex> lock(gTemplateMutex);
        if (!gActiveLoaded) LoadActiveTemplateLocked();
        if (gActivePresent && gActiveSpec.side == spec.side && gActiveSpec.anchor_id == spec.anchor_id) {
            incumbentSpec = gActiveSpec;
            incumbentStrength = gActiveStrength;
        }
    }
    if (incumbentStrength > 0.0) {
        trial.incumbent = ParseMetrics(EvaluateThetaOnHistoryWithAdapterJson(
            RootDir(), theta, BuildAdapter(incumbentSpec, incumbentStrength), 0, 100, false, 0, &incumbentEpisodes));
    }
    const std::vector<std::pair<std::string, double>> levels = {{"LOW", 0.20}, {"MEDIUM", 0.40}, {"HIGH", 0.60}};
    double bestScore = -1.0e100;
    int nextId = 1;
    for (int prefix = 0; prefix <= static_cast<int>(spec.positive_sequence.size()); ++prefix) {
      for (const auto &level : levels) {
        Candidate candidate;
        candidate.spec = spec;
        candidate.spec.ready_prefix_length = prefix;
        candidate.id = nextId++;
        candidate.strength_name = level.first;
        candidate.strength = level.second;
        std::vector<ReplayEpisodeSummary> candidateEpisodes;
        candidate.metrics = ParseMetrics(EvaluateThetaOnHistoryWithAdapterJson(
            RootDir(), theta, BuildAdapter(candidate.spec, candidate.strength), 0, 100, false, 0, &candidateEpisodes));
        candidate.eligible = Eligible(baseline, candidate.metrics, &candidate.rejection);
        if (candidate.eligible && !CheckReplayEpisodeSafety(baselineEpisodes, candidateEpisodes, &candidate.rejection))
            candidate.eligible = false;
        if (candidate.eligible && prefix > 0 && baseline.false_avoided + baseline.false_kept == 0) {
            candidate.eligible = false;
            candidate.rejection = "prefix_requires_hard_negative_history";
        }
        if (candidate.eligible && trial.incumbent.ok &&
            !Eligible(trial.incumbent, candidate.metrics, &candidate.rejection)) {
            candidate.eligible = false;
            candidate.rejection = "incumbent_" + candidate.rejection;
        }
        if (candidate.eligible && trial.incumbent.ok &&
            !CheckReplayEpisodeSafety(incumbentEpisodes, candidateEpisodes, &candidate.rejection)) {
            candidate.eligible = false;
            candidate.rejection = "incumbent_" + candidate.rejection;
        }
        const double regularized = candidate.metrics.score - 0.1 * candidate.strength;
        if (candidate.eligible && regularized > bestScore) {
            bestScore = regularized;
            trial.best_index = static_cast<int>(trial.candidates.size());
        }
        trial.candidates.push_back(candidate);
      }
    }
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    gTrial = std::move(trial);
    return TrialJson(gTrial);
}

std::string GetContextTemplateTrialAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    if (!gTrial.active) return "{\"ok\":true,\"trial_active\":false}";
    return TrialJson(gTrial);
}

std::string CommitContextTemplateAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    if (!gTrial.active) return "{\"ok\":false,\"error\":\"no active context template trial\"}";
    if (gTrial.best_index < 0 || gTrial.best_index >= static_cast<int>(gTrial.candidates.size())) {
        return "{\"ok\":false,\"error\":\"no eligible context template candidate\"}";
    }
    const Candidate &candidate = gTrial.candidates[gTrial.best_index];
    const std::string profile = SpecJson(candidate.spec, candidate.strength_name, candidate.strength);
    std::ofstream active(RootDir() + "/active_context_template.json", std::ios::out | std::ios::trunc);
    std::ofstream history(RootDir() + "/context_templates.jsonl", std::ios::out | std::ios::app);
    if (!active.is_open() || !history.is_open()) {
        return "{\"ok\":false,\"error\":\"cannot persist context template\"}";
    }
    active << profile << '\n';
    history << "{\"committed_at_ms\":" << NowMs() << ",\"candidate_id\":" << candidate.id
        << ",\"template\":" << profile << ",\"baseline\":" << MetricsJson(gTrial.baseline)
        << ",\"candidate\":" << MetricsJson(candidate.metrics) << "}\n";
    const int committedId = candidate.id;
    const std::string name = gTrial.spec.template_name;
    gTrial = Trial {};
    gActiveLoaded = false;
    return "{\"ok\":true,\"committed_candidate_id\":" + std::to_string(committedId) +
        ",\"template_name\":\"" + Esc(name) + "\",\"path\":\"active_context_template.json\"}";
}

std::string DiscardContextTemplateAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    const bool active = gTrial.active;
    gTrial = Trial {};
    return std::string("{\"ok\":true,\"discarded\":") + (active ? "true" : "false") + '}';
}

std::string GetActiveContextTemplateAction(const std::string &)
{
    std::ifstream in(RootDir() + "/active_context_template.json");
    if (!in.is_open()) return "{\"ok\":true,\"active\":false}";
    std::stringstream body;
    body << in.rdbuf();
    std::string json = body.str();
    while (!json.empty() && std::isspace(static_cast<unsigned char>(json.back()))) json.pop_back();
    if (json.empty() || json.front() != '{') return "{\"ok\":false,\"error\":\"invalid active context template\"}";
    return "{\"ok\":true,\"active\":true,\"template\":" + json + '}';
}

std::string EvaluateActiveContextTemplateOnHistoryAction(const std::string &args)
{
    TemplateSpec spec;
    double strength = 0.0;
    {
        std::lock_guard<std::mutex> lock(gTemplateMutex);
        if (!gActiveLoaded) LoadActiveTemplateLocked();
        if (!gActivePresent) {
            return "{\"ok\":false,\"error\":\"no valid active context template\"}";
        }
        spec = gActiveSpec;
        strength = gActiveStrength;
    }
    const Theta theta = CurrentTheta();
    const std::string baseline = EvaluateThetaOnHistoryJson(RootDir(), theta, 0, 1000);
    bool trace = false;
    double cutoff = 0;
    ExtractBool(args, "include_prefix_trace", &trace);
    ExtractNumber(args, "cutoff_t_ms", &cutoff);
    if (!std::isfinite(cutoff) || cutoff < 0 || cutoff > 9007199254740991.0)
        return "{\"ok\":false,\"error\":\"invalid cutoff_t_ms\"}";
    const std::string frozen = EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), theta, BuildAdapter(spec, strength), 0, 1000, trace, static_cast<int64_t>(cutoff));
    return "{\"ok\":true,\"mode\":\"frozen_template_no_tuning\",\"template\":" +
        SpecJson(spec, "FROZEN", strength) + ",\"baseline\":" + baseline + ",\"frozen\":" + frozen + '}';
}

std::string DiagnoseContextTemplateOnHistoryAction(const std::string &args)
{
    std::string ablation;
    if (!ExtractString(args, "ablation", &ablation) ||
        (ablation != "positive" && ablation != "negative" && ablation != "both"))
        return "{\"ok\":false,\"error\":\"ablation must be positive|negative|both\"}";
    double limit = 20;
    ExtractNumber(args, "limit", &limit);
    if (!std::isfinite(limit) || limit < 1 || limit > 100 || limit != std::floor(limit))
        return "{\"ok\":false,\"error\":\"limit must be an integer in 1..100\"}";
    TemplateSpec spec;
    double strength = 0;
    {
        std::lock_guard<std::mutex> lock(gTemplateMutex);
        if (!gActiveLoaded) LoadActiveTemplateLocked();
        if (!gActivePresent) return "{\"ok\":false,\"error\":\"no active template; query raw episode evidence first\"}";
        spec = gActiveSpec;
        strength = gActiveStrength;
    }
    // Fresh adapter state for each arm. No profile/theta writes, no candidate fit.
    const Theta theta = CurrentTheta();
    auto disabled = [adapter = BuildAdapter(spec, strength), ablation](LeaveObservation &obs, bool start) mutable {
        adapter(obs, start);
        if (ablation == "positive" || ablation == "both") {
            obs.sequence_progress = 0;
            obs.sequence_complete = 0;
            obs.sequence_ready = -1;
        }
        if (ablation == "negative" || ablation == "both") obs.negative_pattern_match = 0;
    };
    const auto active = EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), theta, BuildAdapter(spec, strength), 0, static_cast<int>(limit));
    const auto counterfactual = EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), theta, disabled, 0, static_cast<int>(limit));
    if (!ParseMetrics(active).ok || !ParseMetrics(counterfactual).ok)
        return "{\"ok\":false,\"read_only\":true,\"error\":\"HSMM replay history unavailable\"}";
    return "{\"ok\":true,\"read_only\":true,\"ablation\":\"" + ablation +
        "\",\"interpretation\":\"Model intervention, not physical causality. Compare matching side/outcome/label rows. "
        "Zero timestamp means not observed. Soft platform labels are proxy labels. No fitting or commit performed.\","
        "\"template\":" + SpecJson(spec, "FROZEN", strength) +
        ",\"active\":" + active + ",\"ablated\":" + counterfactual + '}';
}

bool ApplyActiveContextTemplateObservation(const std::string &side, const std::string &anchorId,
    int64_t tMs, LeaveObservation *observation)
{
    if (observation == nullptr) return false;
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    if (!gActiveLoaded) LoadActiveTemplateLocked();
    if (!gActivePresent || gActiveSpec.side != side ||
        (!gActiveSpec.anchor_id.empty() && gActiveSpec.anchor_id != anchorId) ||
        (gActiveSpec.applicability == "baro_ready" && !observation->baro_available)) {
        return false;
    }
    // A stale partial sequence must not leak into a later departure episode.
    if (gActiveLastMatchMs > 0 && (tMs < gActiveLastMatchMs || tMs - gActiveLastMatchMs > 600000)) {
        gActiveSequenceIndex = 0;
        gActiveLastMatchMs = 0;
    }
    const bool advanced = ApplySpec(gActiveSpec, gActiveStrength, observation, &gActiveSequenceIndex);
    if (advanced) gActiveLastMatchMs = tMs;
    if (observation->outside || observation->approaching || observation->attached) {
        gActiveSequenceIndex = 0;
        gActiveLastMatchMs = 0;
    }
    return true;
}

void ReloadActiveContextTemplateRuntime()
{
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    gActiveLoaded = false;
    gActivePresent = false;
    gActiveSequenceIndex = 0;
    gActiveLastMatchMs = 0;
}

}  // namespace commute_sa
