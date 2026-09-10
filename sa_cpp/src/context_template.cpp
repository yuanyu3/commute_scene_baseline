#include "commute_sa/context_template.h"

#include "commute_sa/anchors.h"
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
    double lead_utility = 0.0;
    double mean_lead_s = -1.0;
    double late_seconds = 0.0;
    double early_seconds = 0.0;
    int n_episodes = 0;
    int n_false_push = 0;
    int n_confirmed_leave = 0;
    int n_missed_leave_label = 0;
    int false_kept = 0;
    int false_avoided = 0;
    int confirmed_kept = 0;
    int missed_leave = 0;
    int recovered_miss = 0;
    int n_aborted_leave = 0;
    int aborted_intent_recognized = 0;
    int aborted_cancel_recognized = 0;
    int aborted_visible_push = 0;
};

struct TemplateSpec {
    int ready_prefix_length = 0; // 0: legacy fusion; fitted only by bounded replay.
    std::string template_name;
    std::string side = "company";
    std::string anchor_id = "company_001";
    std::string applicability = "baro_ready";
    std::vector<std::string> positive_sequence;
    /** Ordered return sequence; active only after the positive prefix starts. */
    std::vector<std::string> cancel_sequence;
    // Alternatives, not a bag of independently summed sensor observations.
    std::vector<std::vector<std::string>> cancel_paths;
    bool cancel_paths_mode = false; // Also retained when ablating the final path.
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
struct TemplateRuntimeState {
    size_t positive_index = 0;
    size_t cancel_index = 0;
    int64_t last_match_ms = 0;
    int64_t cancel_until_ms = 0;
    std::vector<size_t> path_indices;
    std::vector<int64_t> path_started_ms;
    int64_t departure_started_ms = 0;
    int64_t restart_started_ms = 0;
};

TemplateRuntimeState gActiveState;

std::string RootDir()
{
    const std::string root = ProductStore::GetInstance().RootDir();
    return root.empty() ? "/data/service/el1/public/commuteagentservice" : root;
}

bool ResolveAnchorRole(const std::string &requested, std::string *anchorId, std::string *role)
{
    AnchorSet anchors = DefaultAnchors();
    LoadAnchorsFromFile(RootDir() + "/anchors.json", &anchors, nullptr);
    if (requested == anchors.home.id || requested == "home") {
        if (anchorId) *anchorId = anchors.home.id;
        if (role) *role = "home";
        return true;
    }
    if (requested == anchors.company.id || requested == "company") {
        if (anchorId) *anchorId = anchors.company.id;
        if (role) *role = "company";
        return true;
    }
    return false;
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
        "attached", "baro_ascending", "vertical_closure", "no_baro_descent", "no_geo_outbound"};
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
    std::string anchor_id;
    std::string label;
    int64_t outcome_ms = 0;
    double max_baro_descent_m = 0.0;
    bool baro_available = false;
};

std::vector<EpisodeParameterEvidence> LoadParameterEvidence()
{
    std::map<std::string, std::string> interpretations;
    {
        std::ifstream labels(RootDir() + "/episode_interpretations.jsonl");
        std::string labelLine;
        while (std::getline(labels, labelLine)) {
            std::string side;
            std::string episodeId;
            std::string type;
            double outcome = 0.0;
            if (ExtractString(labelLine, "side", &side) &&
                ExtractString(labelLine, "episode_id", &episodeId) &&
                ExtractString(labelLine, "episode_type", &type) && type == "ABORTED_LEAVE" &&
                ExtractNumber(labelLine, "outcome_t_ms", &outcome)) {
                interpretations[side + ":" + std::to_string(static_cast<int64_t>(outcome)) + ":" + episodeId] = type;
            }
        }
    }
    std::ifstream in(RootDir() + "/policy_history.jsonl");
    std::map<std::string, EpisodeParameterEvidence> grouped;
    std::string line;
    while (std::getline(in, line)) {
        int64_t outcomeMs = 0;
        double outcomeValue = 0.0;
        std::string side;
        std::string anchorId;
        std::string label;
        std::string episodeId;
        if (!ExtractNumber(line, "outcome_t_ms", &outcomeValue) ||
            !ExtractString(line, "side", &side) || !ExtractString(line, "label", &label)) {
            continue;
        }
        outcomeMs = static_cast<int64_t>(outcomeValue);
        ExtractString(line, "anchor_id", &anchorId);
        ExtractString(line, "episode_id", &episodeId);
        const auto interpretation = interpretations.find(
            side + ":" + std::to_string(outcomeMs) + ":" + episodeId);
        if (label == "FALSE_PUSH" && interpretation != interpretations.end()) label = interpretation->second;
        const std::string key = (anchorId.empty() ? side : anchorId) + ":" +
            std::to_string(outcomeMs) + ":" + label + ":" + episodeId;
        auto &episode = grouped[key];
        episode.side = side;
        episode.anchor_id = anchorId;
        episode.label = label;
        episode.outcome_ms = outcomeMs;
        double descent = 0.0;
        if (ExtractNumber(line, "baro_descent_m", &descent)) {
            episode.max_baro_descent_m = std::max(episode.max_baro_descent_m, descent);
        }
        bool baroAvailable = false;
        if (ExtractBool(line, "obs_baro_available", &baroAvailable) && baroAvailable) {
            episode.baro_available = true;
        }
    }
    std::vector<EpisodeParameterEvidence> result;
    for (auto &entry : grouped) result.push_back(std::move(entry.second));
    return result;
}

void EstimateRequestedParameters(TemplateSpec *spec)
{
    if (spec == nullptr || spec->parameter_families.empty()) return;
    const auto episodes = LoadParameterEvidence();
    for (const auto &family : spec->parameter_families) {
        if (family == "departure_time") {
            std::vector<double> hours;
            for (const auto &episode : episodes) {
                const bool anchorMatches = !episode.anchor_id.empty() ?
                    episode.anchor_id == spec->anchor_id : episode.side == spec->side;
                if (anchorMatches && episode.label == "CONFIRMED_LEAVE" && episode.outcome_ms > 0) {
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
                const bool anchorMatches = !episode.anchor_id.empty() ?
                    episode.anchor_id == spec->anchor_id : episode.side == spec->side;
                if (!anchorMatches || !episode.baro_available) continue;
                // Select samples from independent outcome labels and raw descent.
                // Never use lower_platform here: it was computed with the old
                // threshold and would make threshold fitting circular.
                if (episode.label == "CONFIRMED_LEAVE") {
                    positives.push_back(episode.max_baro_descent_m);
                } else if (episode.label == "FALSE_PUSH" || episode.label == "TRUE_NEGATIVE") {
                    hardFalse.push_back(episode.max_baro_descent_m);
                }
            }
            spec->baro_sample_count = static_cast<int>(positives.size());
            if (positives.size() < 3) {
                spec->unavailable_parameter_families.push_back(
                    "vertical_threshold:need_3_baro_valid_confirmed_leave_episodes");
                continue;
            }
            const double positiveLow = Quantile(positives, 0.2);
            const double falseHigh = hardFalse.empty() ? 0.0 : Quantile(hardFalse, 0.9);
            double threshold = positiveLow * 0.75;
            if (!hardFalse.empty() && positiveLow > falseHigh + 2.0) {
                threshold = 0.5 * (positiveLow + falseHigh);
            }
            threshold = std::max(4.0, std::min(30.0, threshold));
            spec->baro_min_descent_m = 2.0 * std::round(threshold / 2.0);
            spec->personalized_vertical_threshold = true;
        }
    }
}

// Small DSL: comma separates ordered events, pipe separates alternative paths.
// Each path must contain an observed departure followed by a physical return,
// or the vertical reversal/closure pair relative to the positive prefix.
bool ParseCancelPaths(const std::string &csv, std::vector<std::vector<std::string>> *paths)
{
    paths->clear();
    if (csv.empty()) return true;
    if (csv.back() == '|') return false;
    std::stringstream stream(csv);
    std::string clause;
    std::set<std::string> seen;
    while (std::getline(stream, clause, '|')) {
        const auto events = SplitCsv(clause);
        if (events.size() < 2 || events.size() > 6 || !seen.insert(JoinCsv(events)).second) return false;
        for (const auto &event : events) if (SupportedEvents().count(event) == 0) return false;
        const auto hasOrdered = [&](const std::string &a, const std::string &b) {
            auto first = std::find(events.begin(), events.end(), a);
            return first != events.end() && std::find(first + 1, events.end(), b) != events.end();
        };
        const bool vertical = hasOrdered("baro_ascending", "vertical_closure");
        const bool spatial = hasOrdered("geo_outbound", "approaching") &&
            events.back() == "attached";
        if (!vertical && !spatial) return false;
        paths->push_back(events);
    }
    if (paths->empty() || paths->size() > 3) return false;
    // With OR fusion a longer path prefixed by an accepted shorter path adds
    // no recognition ability. Canonicalize instead of rewarding verbosity.
    std::vector<std::vector<std::string>> minimal;
    for (size_t i = 0; i < paths->size(); ++i) {
        bool redundant = false;
        for (size_t j = 0; j < paths->size(); ++j) {
            if (i != j && (*paths)[j].size() < (*paths)[i].size() &&
                std::equal((*paths)[j].begin(), (*paths)[j].end(), (*paths)[i].begin())) redundant = true;
        }
        if (!redundant) minimal.push_back((*paths)[i]);
    }
    *paths = std::move(minimal);
    return true;
}

std::string JoinCancelPaths(const std::vector<std::vector<std::string>> &paths)
{
    std::string result;
    for (const auto &path : paths) {
        if (!result.empty()) result += '|';
        result += JoinCsv(path);
    }
    return result;
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
    if (event == "baro_ascending") return obs.baro_available && obs.baro_ascending >= 0.5;
    if (event == "vertical_closure") return obs.baro_available && obs.vertical_closure >= 0.5;
    if (event == "outside") return obs.outside;
    if (event == "approaching") return obs.approaching;
    if (event == "attached") return obs.attached;
    if (event == "no_baro_descent") {
        return obs.baro_available && obs.baro_descending < 0.25 && obs.baro_lower_platform < 0.5;
    }
    if (event == "no_geo_outbound") return obs.geo_outbound < 0.25;
    return false;
}

bool ApplySpec(const TemplateSpec &spec, double strength, LeaveObservation *obs, TemplateRuntimeState *state)
{
    if (obs == nullptr || state == nullptr) return false;
    // Replaying an observation that was previously produced under another
    // active template must not carry old template evidence into this trial.
    obs->sequence_available = true;
    obs->sequence_progress = 0.0;
    obs->sequence_complete = 0.0;
    obs->sequence_ready = spec.ready_prefix_length > 0 ? 0.0 : -1.0;
    obs->negative_pattern_match = 0.0;
    obs->cancel_sequence_match = 0.0;
    obs->sequence_reliability = Clip01(strength);
    if (spec.personalized_time && obs->t_ms > 0) {
        obs->time_prior = PersonalizedTimePrior(obs->t_ms, spec.time_center_hour, spec.time_window_min);
    }
    if (spec.personalized_vertical_threshold && obs->baro_available && obs->baro_stable_platform_known) {
        obs->baro_lower_platform = obs->baro_stable_platform &&
            obs->baro_descent_m >= spec.baro_min_descent_m ? 1.0 : 0.0;
    }
    if (state->cancel_until_ms > 0) {
        // New templates may release the hold after a fresh, sustained departure.
        // A single noisy tick must not release cancellation.
        if (spec.cancel_paths_mode) {
            const bool restarting = !obs->approaching && !obs->attached &&
                ((obs->baro_available && obs->baro_descending >= 0.5 && obs->vertical_closure < 0.5) ||
                 (obs->walking >= 0.5 && obs->geo_outbound >= 0.5));
            if (!restarting) state->restart_started_ms = 0;
            else if (!state->restart_started_ms) state->restart_started_ms = obs->t_ms;
            if (state->restart_started_ms && obs->t_ms - state->restart_started_ms >= 10000) {
                *state = TemplateRuntimeState {};
            }
        }
        if (state->cancel_until_ms > 0 && obs->t_ms <= state->cancel_until_ms) {
            obs->negative_pattern_match = 1.0;
            obs->cancel_sequence_match = 1.0;
            return false;
        }
        state->cancel_until_ms = 0;
    }
    const size_t previousPositive = state->positive_index;
    const size_t previousCancel = state->cancel_index;
    if (state->positive_index < spec.positive_sequence.size() &&
        EventActive(spec.positive_sequence[state->positive_index], *obs)) {
        if (!state->positive_index) state->departure_started_ms = obs->t_ms;
        ++state->positive_index;
    }
    if (!spec.positive_sequence.empty()) {
        const double progress = static_cast<double>(state->positive_index) /
            static_cast<double>(spec.positive_sequence.size());
        obs->sequence_progress = Clip01(progress);
        if (spec.ready_prefix_length > 0) {
            obs->sequence_ready = state->positive_index >= static_cast<size_t>(spec.ready_prefix_length) ? 1.0 : 0.0;
        }
        if (state->positive_index >= spec.positive_sequence.size()) {
            obs->sequence_complete = 1.0;
        }
    }

    // A cancellation can only explain a reversal after this episode has
    // entered the learned departure prefix. It is never a global negative.
    if (state->positive_index > 0 && state->cancel_index < spec.cancel_sequence.size() &&
        EventActive(spec.cancel_sequence[state->cancel_index], *obs)) {
        ++state->cancel_index;
    }
    bool pathComplete = false;
    if (spec.cancel_paths_mode) {
        if (state->path_indices.size() != spec.cancel_paths.size()) {
            state->path_indices.assign(spec.cancel_paths.size(), 0);
            state->path_started_ms.assign(spec.cancel_paths.size(), 0);
        }
        for (size_t i = 0; i < spec.cancel_paths.size(); ++i) {
            auto &index = state->path_indices[i];
            auto &started = state->path_started_ms[i];
            const auto &path = spec.cancel_paths[i];
            // Expire from the first event, not the latest match. Outside closes
            // this attempted departure; a later homecoming is not a cancellation.
            if (obs->outside || (started && obs->t_ms - started > 180000)) {
                index = 0;
                started = 0;
            }
            if (obs->outside || !state->positive_index ||
                obs->t_ms <= state->departure_started_ms) continue;
            if (index < path.size() && EventActive(path[index], *obs)) {
                if (!index) started = obs->t_ms;
                ++index; // At most one stage per tick, never unordered co-occurrence.
            }
            pathComplete = pathComplete || index == path.size();
        }
        if (obs->outside) state->positive_index = 0;
    }
    if (pathComplete || (!spec.cancel_sequence.empty() && state->cancel_index >= spec.cancel_sequence.size())) {
        constexpr int64_t kCancelHoldMs = 120000;
        state->cancel_until_ms = obs->t_ms + kCancelHoldMs;
        state->positive_index = 0;
        state->cancel_index = 0;
        state->path_indices.clear();
        state->path_started_ms.clear();
        obs->sequence_progress = 0.0;
        obs->sequence_complete = 0.0;
        obs->sequence_ready = spec.ready_prefix_length > 0 ? 0.0 : -1.0;
        obs->negative_pattern_match = 1.0;
        obs->cancel_sequence_match = 1.0;
    }

    bool negative = !spec.negative_pattern.empty();
    for (const auto &event : spec.negative_pattern) negative = negative && EventActive(event, *obs);
    if (negative) {
        obs->negative_pattern_match = 1.0;
    }
    if (spec.cancel_paths_mode && (obs->approaching || obs->attached || obs->outside)) {
        obs->sequence_progress = 0.0;
        obs->sequence_complete = 0.0;
        obs->sequence_ready = spec.ready_prefix_length > 0 ? 0.0 : -1.0;
    }
    return state->positive_index > previousPositive || state->cancel_index > previousCancel ||
        state->cancel_until_ms > 0;
}

void LoadActiveTemplateLocked()
{
    gActiveLoaded = true;
    gActivePresent = false;
    gActiveState = TemplateRuntimeState {};

    std::ifstream in(RootDir() + "/active_context_template.json");
    if (!in.is_open()) return;
    std::stringstream body;
    body << in.rdbuf();
    const std::string json = body.str();
    TemplateSpec spec;
    std::string positiveCsv;
    std::string cancelCsv;
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
    ExtractString(json, "cancel_sequence", &cancelCsv);
    std::string pathsCsv;
    ExtractString(json, "cancel_paths", &pathsCsv);
    if (!ParseCancelPaths(pathsCsv, &spec.cancel_paths) || (!pathsCsv.empty() && !cancelCsv.empty())) return;
    spec.cancel_paths_mode = !pathsCsv.empty();
    ExtractString(json, "parameter_families", &parameterFamiliesCsv);
    ExtractString(json, "rationale", &spec.rationale);
    spec.positive_sequence = SplitCsv(positiveCsv);
    spec.cancel_sequence = SplitCsv(cancelCsv);
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
    for (const auto &event : spec.cancel_sequence) if (SupportedEvents().count(event) == 0) return;
    for (const auto &event : spec.negative_pattern) if (SupportedEvents().count(event) == 0) return;
    if (spec.positive_sequence.size() > 6 || spec.cancel_sequence.size() > 6 ||
        spec.negative_pattern.size() > 6) return;
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
    ExtractNumber(json, "early_seconds", &metrics.early_seconds);
    double value = 0.0;
    auto integer = [&](const char *key, int *target) {
        if (ExtractNumber(json, key, &value)) *target = static_cast<int>(value);
    };
    ExtractNumber(json, "score", &metrics.score);
    ExtractNumber(json, "lead_utility", &metrics.lead_utility);
    integer("n_episodes", &metrics.n_episodes);
    integer("n_false_push", &metrics.n_false_push);
    integer("n_confirmed_leave", &metrics.n_confirmed_leave);
    integer("n_missed_leave_label", &metrics.n_missed_leave_label);
    integer("false_kept", &metrics.false_kept);
    integer("false_avoided", &metrics.false_avoided);
    integer("confirmed_kept", &metrics.confirmed_kept);
    integer("missed_leave", &metrics.missed_leave);
    integer("recovered_miss", &metrics.recovered_miss);
    integer("n_aborted_leave", &metrics.n_aborted_leave);
    integer("aborted_intent_recognized", &metrics.aborted_intent_recognized);
    integer("aborted_cancel_recognized", &metrics.aborted_cancel_recognized);
    integer("aborted_visible_push", &metrics.aborted_visible_push);
    return metrics;
}

std::string MetricsJson(const Metrics &m)
{
    std::ostringstream out;
    out << "{\"score_version\":6,\"mean_lead_s\":" << m.mean_lead_s
        << ",\"late_seconds\":" << m.late_seconds
        << ",\"early_seconds\":" << m.early_seconds << ",\"lead_utility\":" << m.lead_utility
        << ",\"score\":" << m.score << ",\"n_episodes\":" << m.n_episodes
        << ",\"n_false_push\":" << m.n_false_push << ",\"n_confirmed_leave\":" << m.n_confirmed_leave
        << ",\"n_missed_leave_label\":" << m.n_missed_leave_label << ",\"false_kept\":" << m.false_kept
        << ",\"false_avoided\":" << m.false_avoided
        << ",\"confirmed_kept\":" << m.confirmed_kept << ",\"missed_leave\":" << m.missed_leave
        << ",\"recovered_miss\":" << m.recovered_miss
        << ",\"n_aborted_leave\":" << m.n_aborted_leave
        << ",\"aborted_intent_recognized\":" << m.aborted_intent_recognized
        << ",\"aborted_cancel_recognized\":" << m.aborted_cancel_recognized
        << ",\"aborted_visible_push\":" << m.aborted_visible_push << '}';
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
    if (candidate.false_kept > base.false_kept) { if (why) *why = "hard_false_push_increased"; return false; }
    if (candidate.aborted_intent_recognized < base.aborted_intent_recognized) {
        if (why) *why = "aborted_intent_recognition_decreased"; return false;
    }
    if (candidate.aborted_visible_push > base.aborted_visible_push) {
        if (why) *why = "aborted_visible_push_increased"; return false;
    }
    if (candidate.aborted_cancel_recognized < base.aborted_cancel_recognized) {
        if (why) *why = "aborted_cancel_recognition_decreased"; return false;
    }
    return true;
}

ObservationAdapter BuildAdapter(const TemplateSpec &spec, double strength)
{
    TemplateRuntimeState state;
    return [spec, strength, state](LeaveObservation &obs, bool episodeStart) mutable {
        if (episodeStart) state = TemplateRuntimeState {};
        obs.sequence_available = false;
        obs.sequence_progress = 0.0;
        obs.sequence_complete = 0.0;
        obs.sequence_ready = -1.0;
        obs.negative_pattern_match = 0.0;
        obs.cancel_sequence_match = 0.0;
        obs.sequence_reliability = 0.0;
        if (!obs.context_side.empty() && obs.context_side != spec.side) return;
        if (spec.applicability == "baro_ready" && !obs.baro_available) return;
        if (state.last_match_ms > 0 &&
            (obs.t_ms < state.last_match_ms || obs.t_ms - state.last_match_ms > 600000)) {
            state = TemplateRuntimeState {};
        }
        if (ApplySpec(spec, strength, &obs, &state)) state.last_match_ms = obs.t_ms;
        if (obs.outside || (!spec.cancel_paths_mode &&
            (obs.approaching || (obs.attached && state.cancel_until_ms <= 0)))) {
            state = TemplateRuntimeState {};
        }
    };
}

std::string SpecJson(const TemplateSpec &spec, const std::string &strengthName = "", double strength = 0.0)
{
    std::ostringstream out;
    out << "{\"schema_version\":6,\"ready_prefix_length\":" << spec.ready_prefix_length
        << ",\"template_name\":\"" << Esc(spec.template_name)
        << "\",\"side\":\"" << Esc(spec.side) << "\",\"anchor_id\":\"" << Esc(spec.anchor_id)
        << "\",\"applicability\":\"" << Esc(spec.applicability)
        << "\",\"positive_sequence\":\"" << Esc(JoinCsv(spec.positive_sequence))
        << "\",\"cancel_sequence\":\"" << Esc(JoinCsv(spec.cancel_sequence))
        << "\",\"cancel_paths\":\"" << Esc(JoinCancelPaths(spec.cancel_paths))
        << "\",\"negative_pattern\":\"" << Esc(JoinCsv(spec.negative_pattern))
        << "\",\"parameter_families\":\"" << Esc(JoinCsv(spec.parameter_families))
        << "\",\"positive_effect\":\"emit_progress_completion_and_optional_prefix_readiness\","
           "\"negative_effect\":\"emit_negative_pattern_match\","
           "\"cancel_effect\":\"after a started positive prefix, ordered return events emit a bounded cancel observation\"";
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
    out << "],\"commit_guard\":{\"score_must_improve\":true,\"false_push_must_not_increase\":true,"
           "\"confirmed_recall_must_not_decrease\":true,\"missed_must_not_increase\":true,"
           "\"aborted_intent_must_not_decrease\":true,\"aborted_visible_push_must_not_increase\":true,"
           "\"per_episode_no_new_false_or_lost_positive\":true,\"per_episode_positive_must_not_be_later\":true}}";
    return out.str();
}

}  // namespace

std::string GetContextTemplateCatalogAction(const std::string &)
{
    return "{\"ok\":true,\"schema_version\":6,\"design\":\"Agent composes supported primitives and requests parameter families; C++ estimates all numeric values\","
           "\"cancel_paths\":{\"syntax\":\"ordered comma-separated events; pipe separates up to 3 alternative paths; mutually exclusive with cancel_sequence\","
           "\"validation\":\"each path needs baro_ascending before vertical_closure, or geo_outbound before approaching ending in attached; <=6 events per path\","
           "\"fusion\":\"any completed path; no additive sensor votes; redundant prefix extensions removed; 180s path expiry; fresh departure for 10s releases 120s hold\","
           "\"evidence_rule\":\"attached_observed is whole-episode presence, NOT reattachment; missing auxiliary evidence must not become a mandatory prerequisite\"},"
           "\"applicability\":[\"always\",\"baro_ready\"],"
           "\"events\":[\"walking\",\"pdr_outbound\",\"geo_outbound\",\"wifi_detach\",\"cell_detach\","
           "\"ble_detach\",\"baro_descending\",\"lower_platform\",\"outside\",\"approaching\","
           "\"attached\",\"baro_ascending\",\"vertical_closure\",\"no_baro_descent\",\"no_geo_outbound\"],"
           "\"effects\":{\"positive_sequence\":\"emit progress/completion and replay-calibrated prefix readiness; readiness replaces, never adds to, legacy positive evidence\","
           "\"negative_pattern\":\"emit an independent negative_pattern_match observation\","
           "\"cancel_sequence\":\"ordered reversal after a started positive prefix; emits a 120-second cancel observation\","
           "\"strength\":\"emit sequence_reliability selected by deterministic replay\"},"
           "\"parameter_families\":{\"vertical_threshold\":\"anchor-scoped stable descent threshold; needs 3 validated lower-platform episodes\"},"
           "\"disabled_parameter_families\":{\"departure_time\":\"disabled while collection timestamps are not representative of normal behavior\"},"
           "\"ready_prefix_length\":\"C++ evaluates legacy mode and each causal prefix; agent supplies sequence only\","
           "\"strengths\":\"LOW|MEDIUM|HIGH selected by deterministic replay with continuous lead score and no false/missed regression\"}";
}

std::string GetAbortedLeaveCandidatesAction(const std::string &paramsJson)
{
    std::string requestedAnchor;
    std::string wantedAnchor;
    std::string legacySide;
    if (!ExtractString(paramsJson, "anchor_id", &requestedAnchor) ||
        !ResolveAnchorRole(requestedAnchor, &wantedAnchor, &legacySide)) {
        return "{\"ok\":false,\"error\":\"known anchor_id required\"}";
    }
    double limitValue = 50;
    ExtractNumber(paramsJson, "limit", &limitValue);
    const size_t limit = static_cast<size_t>(std::max(1.0, std::min(100.0, limitValue)));
    struct Summary {
        std::string episode;
        std::string label;
        int64_t outcome = 0;
        int64_t firstDescent = 0;
        int64_t firstLower = 0;
        int64_t firstAscending = 0;
        int64_t firstClosure = 0;
        double maxDescent = 0;
        bool outside = false;
        bool walking = false;
        bool outbound = false;
        bool attached = false;
        size_t ticks = 0;
        size_t attachedKnown = 0;
        size_t attachedTrue = 0;
        size_t baroKnown = 0;
        size_t baroValid = 0;
        int64_t firstAttachedAfterClosure = 0;
        int64_t firstAttached = 0;
        int64_t lastMs = 0;
    };
    std::map<std::string, Summary> grouped;
    std::ifstream in(RootDir() + "/policy_history.jsonl");
    if (!in.is_open()) return "{\"ok\":false,\"error\":\"policy_history.jsonl missing\"}";
    const double material = std::max(4.0, CurrentTheta().baro_min_descent_m);
    std::string line;
    while (std::getline(in, line)) {
        std::string side;
        std::string rowAnchor;
        std::string episode;
        std::string label;
        double value = 0;
        ExtractString(line, "side", &side);
        ExtractString(line, "anchor_id", &rowAnchor);
        const bool anchorMatches = !rowAnchor.empty() ? rowAnchor == wantedAnchor : side == legacySide;
        if (!anchorMatches ||
            !ExtractString(line, "episode_id", &episode) || episode.empty() ||
            !ExtractString(line, "label", &label) || label != "FALSE_PUSH" ||
            !ExtractNumber(line, "outcome_t_ms", &value)) continue;
        const int64_t outcome = static_cast<int64_t>(value);
        const std::string key = episode + ":" + std::to_string(outcome);
        auto &s = grouped[key];
        s.episode = episode;
        s.label = label;
        s.outcome = outcome;
        int64_t tMs = 0;
        if (ExtractNumber(line, "t_ms", &value)) tMs = static_cast<int64_t>(value);
        ++s.ticks;
        s.lastMs = std::max(s.lastMs, tMs);
        if (ExtractNumber(line, "baro_descent_m", &value)) {
            s.maxDescent = std::max(s.maxDescent, value);
            if (!s.firstDescent && value >= material) s.firstDescent = tMs;
        }
        if (ExtractNumber(line, "obs_baro_lower_platform", &value) && value >= 0.5 && !s.firstLower)
            s.firstLower = tMs;
        if (s.firstDescent && tMs > s.firstDescent &&
            ExtractNumber(line, "obs_baro_ascending", &value) && value >= 0.5 && !s.firstAscending)
            s.firstAscending = tMs;
        if (s.firstAscending && tMs > s.firstAscending &&
            ExtractNumber(line, "obs_vertical_closure", &value) && value >= 0.5 && !s.firstClosure)
            s.firstClosure = tMs;
        bool flag = false;
        if (ExtractBool(line, "obs_outside", &flag) && flag) s.outside = true;
        if (ExtractNumber(line, "obs_walking", &value) && value >= 0.5) s.walking = true;
        const char *outboundFields[] = {"obs_pdr_outbound", "obs_geo_outbound", "obs_wifi_detach"};
        for (const char *field : outboundFields) {
            if (ExtractNumber(line, field, &value) && value >= 0.5) s.outbound = true;
        }
        if (ExtractBool(line, "obs_baro_available", &flag)) {
            ++s.baroKnown;
            if (flag) ++s.baroValid;
        }
        if (ExtractBool(line, "obs_attached", &flag)) {
            ++s.attachedKnown;
            if (flag) {
                ++s.attachedTrue;
                s.attached = true;
                if (!s.firstAttached) s.firstAttached = tMs;
                if (s.firstClosure && tMs >= s.firstClosure && !s.firstAttachedAfterClosure)
                    s.firstAttachedAfterClosure = tMs;
            }
        }
    }
    std::ostringstream out;
    out << "{\"ok\":true,\"anchor_id\":\"" << Esc(wantedAnchor)
        << "\",\"material_descent_m\":" << material << ",\"episodes\":[";
    size_t count = 0;
    for (const auto &entry : grouped) {
        if (count >= limit) break;
        const auto &s = entry.second;
        if (count++) out << ',';
        out << "{\"episode_id\":\"" << Esc(s.episode) << "\",\"outcome_t_ms\":" << s.outcome
            << ",\"label\":\"" << s.label << "\",\"max_baro_descent_m\":" << s.maxDescent
            << ",\"first_material_descent_t_ms\":" << s.firstDescent
            << ",\"first_lower_platform_t_ms\":" << s.firstLower
            << ",\"first_baro_ascending_t_ms\":" << s.firstAscending
            << ",\"first_vertical_closure_t_ms\":" << s.firstClosure
            << ",\"outside_observed\":" << (s.outside ? "true" : "false")
            << ",\"walking_observed\":" << (s.walking ? "true" : "false")
            << ",\"outbound_support_observed\":" << (s.outbound ? "true" : "false")
            << ",\"attached_observed\":" << (s.attached ? "true" : "false")
            << ",\"ticks\":" << s.ticks << ",\"attached_field_ticks\":" << s.attachedKnown
            << ",\"attached_true_ticks\":" << s.attachedTrue
            << ",\"baro_field_ticks\":" << s.baroKnown << ",\"baro_available_ticks\":" << s.baroValid
            << ",\"first_attached_t_ms\":" << s.firstAttached
            << ",\"first_attached_after_closure_t_ms\":" << s.firstAttachedAfterClosure
            << ",\"record_end_t_ms\":" << s.lastMs
            << ",\"ordered_vertical_return_candidate\":"
            << (s.firstDescent && s.firstAscending && s.firstClosure && !s.outside ? "true" : "false")
            << ",\"attached_sensor_quality\":\"unknown: historical attached is a fused boolean, not scan availability; first_attached_after_closure is presence, NOT a measured detach-to-reattach transition\"}";
    }
    out << "],\"total_candidates\":" << grouped.size() << ",\"returned_candidates\":" << count
        << ",\"truncated\":" << (grouped.size() > count ? "true" : "false")
        << ",\"interpretation\":\"Read-only evidence, not a label or proof of subjective intent. attached_observed=true means ever attached, NOT reattached; false means no observed attachment, NOT proof of no return. Missing auxiliary evidence is not counterevidence. Compare complete timelines; account for every candidate, including uncertain cases. Proposals remain C++ validated.\"}";
    return out.str();
}

std::string ProposeAbortedLeaveInterpretationAction(const std::string &paramsJson)
{
    std::string side;
    std::string requestedAnchor;
    std::string anchorId;
    std::string episodeId;
    std::string rationale;
    double outcomeValue = 0.0;
    double confidence = 0.0;
    if (!ExtractString(paramsJson, "anchor_id", &requestedAnchor) ||
        !ResolveAnchorRole(requestedAnchor, &anchorId, &side) ||
        !ExtractString(paramsJson, "episode_id", &episodeId) || episodeId.empty() ||
        !ExtractNumber(paramsJson, "confidence", &confidence) || confidence < 0.5 || confidence > 1.0 ||
        !ExtractString(paramsJson, "rationale", &rationale) || rationale.empty()) {
        return "{\"ok\":false,\"error\":\"anchor_id, episode_id, confidence>=0.5 and rationale required\"}";
    }
    ExtractNumber(paramsJson, "outcome_t_ms", &outcomeValue);
    const bool outcomeProvided = outcomeValue > 0;
    int64_t outcomeMs = outcomeValue > 0 ? static_cast<int64_t>(outcomeValue) : 0;
    bool ambiguousOutcome = false;
    const Theta theta = CurrentTheta();
    std::ifstream in(RootDir() + "/policy_history.jsonl");
    if (!in.is_open()) return "{\"ok\":false,\"error\":\"policy_history.jsonl missing\"}";

    bool found = false;
    bool originalFalse = false;
    bool outside = false;
    bool lowerPlatform = false;
    bool walking = false;
    bool outbound = false;
    bool sawAscendingAfterDescent = false;
    bool sawClosureAfterDescent = false;
    double maxDescent = 0.0;
    int64_t firstMaterialDescentMs = 0;
    int64_t abortMs = 0;
    std::string line;
    while (std::getline(in, line)) {
        std::string rowSide;
        std::string rowAnchor;
        std::string rowEpisode;
        std::string label;
        double rowOutcome = 0.0;
        ExtractString(line, "side", &rowSide);
        ExtractString(line, "anchor_id", &rowAnchor);
        const bool anchorMatches = !rowAnchor.empty() ? rowAnchor == anchorId : rowSide == side;
        if (!anchorMatches ||
            !ExtractString(line, "episode_id", &rowEpisode) || rowEpisode != episodeId ||
            !ExtractNumber(line, "outcome_t_ms", &rowOutcome)) continue;
        const int64_t rowOutcomeMs = static_cast<int64_t>(rowOutcome);
        if (outcomeProvided && rowOutcomeMs != outcomeMs) continue;
        if (outcomeMs == 0) outcomeMs = rowOutcomeMs;
        else if (rowOutcomeMs != outcomeMs) ambiguousOutcome = true;
        found = true;
        ExtractString(line, "label", &label);
        originalFalse = originalFalse || label == "FALSE_PUSH";
        double value = 0.0;
        int64_t tMs = 0;
        if (ExtractNumber(line, "t_ms", &value)) tMs = static_cast<int64_t>(value);
        if (ExtractNumber(line, "baro_descent_m", &value)) {
            maxDescent = std::max(maxDescent, value);
            if (!firstMaterialDescentMs && value >= std::max(4.0, theta.baro_min_descent_m)) {
                firstMaterialDescentMs = tMs;
            }
        }
        bool boolean = false;
        if (ExtractBool(line, "obs_outside", &boolean) && boolean) outside = true;
        if (ExtractNumber(line, "obs_baro_lower_platform", &value) && value >= 0.5) lowerPlatform = true;
        if (ExtractNumber(line, "obs_walking", &value) && value >= 0.5) walking = true;
        const char *outboundFields[] = {"obs_pdr_outbound", "obs_geo_outbound", "obs_wifi_detach"};
        for (const char *field : outboundFields) {
            if (ExtractNumber(line, field, &value) && value >= 0.5) outbound = true;
        }
        if (firstMaterialDescentMs > 0 && tMs > firstMaterialDescentMs &&
            ExtractNumber(line, "obs_baro_ascending", &value) && value >= 0.5) {
            sawAscendingAfterDescent = true;
            if (!abortMs) abortMs = tMs;
        }
        if (sawAscendingAfterDescent && abortMs > 0 && tMs > abortMs &&
            ExtractNumber(line, "obs_vertical_closure", &value) && value >= 0.5) {
            sawClosureAfterDescent = true;
        }
    }

    if (ambiguousOutcome) {
        return "{\"ok\":false,\"error\":\"episode_id is ambiguous; provide outcome_t_ms\"}";
    }
    if (!found || !originalFalse) {
        return "{\"ok\":false,\"error\":\"matching original FALSE_PUSH episode not found\"}";
    }
    const bool candidatePrefix = maxDescent >= std::max(4.0, theta.baro_min_descent_m) &&
        (lowerPlatform || walking || outbound);
    const int candidateSupport = (walking ? 1 : 0) | (outbound ? 2 : 0) | (lowerPlatform ? 4 : 0);
    bool confirmedSharedPrefix = false;
    std::ifstream confirmedIn(RootDir() + "/policy_history.jsonl");
    std::map<std::string, std::pair<bool, int>> confirmedEvidence;
    while (std::getline(confirmedIn, line)) {
        std::string rowSide;
        std::string rowAnchor;
        std::string label;
        std::string rowEpisode;
        double rowOutcome = 0.0;
        ExtractString(line, "side", &rowSide);
        ExtractString(line, "anchor_id", &rowAnchor);
        const bool anchorMatches = !rowAnchor.empty() ? rowAnchor == anchorId : rowSide == side;
        if (!anchorMatches ||
            !ExtractString(line, "label", &label) || label != "CONFIRMED_LEAVE" ||
            !ExtractNumber(line, "outcome_t_ms", &rowOutcome)) continue;
        ExtractString(line, "episode_id", &rowEpisode);
        const std::string key = std::to_string(static_cast<int64_t>(rowOutcome)) + ":" + rowEpisode;
        double value = 0.0;
        if (ExtractNumber(line, "baro_descent_m", &value) &&
            value >= std::max(4.0, theta.baro_min_descent_m)) confirmedEvidence[key].first = true;
        if (ExtractNumber(line, "obs_walking", &value) && value >= 0.5) confirmedEvidence[key].second |= 1;
        const char *outboundFields[] = {"obs_pdr_outbound", "obs_geo_outbound", "obs_wifi_detach"};
        for (const char *field : outboundFields) {
            if (ExtractNumber(line, field, &value) && value >= 0.5) confirmedEvidence[key].second |= 2;
        }
        if (ExtractNumber(line, "obs_baro_lower_platform", &value) && value >= 0.5)
            confirmedEvidence[key].second |= 4;
    }
    for (const auto &entry : confirmedEvidence) {
        if (entry.second.first && (entry.second.second & candidateSupport) != 0) {
            confirmedSharedPrefix = true;
            break;
        }
    }
    const bool sharedDeparturePrefix = candidatePrefix && confirmedSharedPrefix;
    if (!sharedDeparturePrefix || !sawAscendingAfterDescent || !sawClosureAfterDescent || outside) {
        std::ostringstream rejected;
        rejected << "{\"ok\":false,\"error\":\"physical reversal validation failed\",\"validation\":{"
                 << "\"shared_departure_prefix\":" << (sharedDeparturePrefix ? "true" : "false")
                 << ",\"confirmed_prefix_reference\":" << (confirmedSharedPrefix ? "true" : "false")
                 << ",\"ascending_after_descent\":" << (sawAscendingAfterDescent ? "true" : "false")
                 << ",\"vertical_closure_after_descent\":" << (sawClosureAfterDescent ? "true" : "false")
                 << ",\"outside_observed\":" << (outside ? "true" : "false") << "}}";
        return rejected.str();
    }

    std::ofstream out(RootDir() + "/episode_interpretations.jsonl", std::ios::out | std::ios::app);
    if (!out.is_open()) return "{\"ok\":false,\"error\":\"cannot persist episode interpretation\"}";
    out << "{\"created_at_ms\":" << NowMs() << ",\"anchor_id\":\"" << Esc(anchorId)
        << "\",\"side\":\"" << Esc(side) << "\",\"episode_id\":\"" << Esc(episodeId)
        << "\",\"outcome_t_ms\":" << outcomeMs
        << ",\"original_label\":\"FALSE_PUSH\",\"episode_type\":\"ABORTED_LEAVE\""
        << ",\"abort_t_ms\":" << abortMs << ",\"confidence\":" << confidence
        << ",\"max_baro_descent_m\":" << maxDescent
        << ",\"validation\":{\"shared_departure_prefix\":true,\"confirmed_prefix_reference\":true,"
           "\"ascending_after_descent\":true,"
           "\"vertical_closure_after_descent\":true,\"outside_observed\":false}"
        << ",\"rationale\":\"" << Esc(rationale) << "\"}\n";
    return "{\"ok\":true,\"episode_type\":\"ABORTED_LEAVE\",\"original_label\":\"FALSE_PUSH\","
           "\"episode_id\":\"" + Esc(episodeId) + "\",\"abort_t_ms\":" + std::to_string(abortMs) +
           ",\"note\":\"Validated physical reversal; subjective intent remains an inference\"}";
}

std::string GenerateContextTemplateAction(const std::string &paramsJson)
{
    TemplateSpec spec;
    std::string positiveCsv;
    std::string cancelCsv;
    std::string negativeCsv;
    std::string parameterFamiliesCsv;
    if (!ExtractString(paramsJson, "template_name", &spec.template_name) || spec.template_name.empty() ||
        !ExtractString(paramsJson, "positive_sequence", &positiveCsv) || positiveCsv.empty()) {
        return "{\"ok\":false,\"error\":\"template_name and comma-separated positive_sequence required\"}";
    }
    ExtractString(paramsJson, "negative_pattern", &negativeCsv);
    ExtractString(paramsJson, "cancel_sequence", &cancelCsv);
    std::string pathsCsv;
    ExtractString(paramsJson, "cancel_paths", &pathsCsv);
    if (!ParseCancelPaths(pathsCsv, &spec.cancel_paths) || (!pathsCsv.empty() && !cancelCsv.empty())) {
        return "{\"ok\":false,\"error\":\"invalid cancel_paths: need ordered physical reversal, <=3 paths, and no simultaneous cancel_sequence\"}";
    }
    spec.cancel_paths_mode = !pathsCsv.empty();
    ExtractString(paramsJson, "parameter_families", &parameterFamiliesCsv);
    std::string requestedAnchor;
    if (!ExtractString(paramsJson, "anchor_id", &requestedAnchor) ||
        !ResolveAnchorRole(requestedAnchor, &spec.anchor_id, &spec.side)) {
        return "{\"ok\":false,\"error\":\"known anchor_id required\"}";
    }
    ExtractString(paramsJson, "applicability", &spec.applicability);
    ExtractString(paramsJson, "rationale", &spec.rationale);
    if (spec.applicability != "always" && spec.applicability != "baro_ready") {
        return "{\"ok\":false,\"error\":\"applicability must be always|baro_ready\"}";
    }
    spec.positive_sequence = SplitCsv(positiveCsv);
    spec.cancel_sequence = SplitCsv(cancelCsv);
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
    for (const auto &event : spec.cancel_sequence) {
        if (SupportedEvents().count(event) == 0 || event.rfind("no_", 0) == 0) {
            return "{\"ok\":false,\"error\":\"unsupported cancel event\",\"event\":\"" + Esc(event) + "\"}";
        }
    }
    if (spec.positive_sequence.size() > 6 || spec.cancel_sequence.size() > 6 ||
        spec.negative_pattern.size() > 6) {
        return "{\"ok\":false,\"error\":\"template primitive budget exceeded\",\"max_per_clause\":6}";
    }

    const Theta theta = CurrentTheta();
    EstimateRequestedParameters(&spec);
    std::vector<ReplayEpisodeSummary> baselineEpisodes, incumbentEpisodes;
    const Metrics baseline = ParseMetrics(EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), theta, {}, 0, 100, false, 0, &baselineEpisodes, spec.anchor_id, spec.side));
    if (!baseline.ok) return "{\"ok\":false,\"error\":\"baseline history replay unavailable\"}";
    if ((!spec.cancel_sequence.empty() || !spec.cancel_paths.empty()) && baseline.n_aborted_leave < 2) {
        return "{\"ok\":false,\"error\":\"cancel_sequence requires at least 2 ABORTED_LEAVE episodes\","
               "\"n_aborted_leave\":" + std::to_string(baseline.n_aborted_leave) + "}";
    }
    // A supported vertical path cannot license an untested alternative.
    // Validate each path separately at a frozen diagnostic strength.
    for (size_t i = 0; i < spec.cancel_paths.size(); ++i) {
        TemplateSpec single = spec;
        single.cancel_paths = {spec.cancel_paths[i]};
        const Metrics support = ParseMetrics(EvaluateThetaOnHistoryWithAdapterJson(
            RootDir(), theta, BuildAdapter(single, 0.2), 0, 100, false, 0, nullptr,
            spec.anchor_id, spec.side));
        if (!support.ok || support.aborted_cancel_recognized < 2) {
            return "{\"ok\":false,\"error\":\"each cancel path requires 2 independently labeled ABORTED_LEAVE matches\","
                "\"path_index\":" + std::to_string(i) + ",\"matched_episodes\":" +
                std::to_string(support.aborted_cancel_recognized) + "}";
        }
    }

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
            RootDir(), theta, BuildAdapter(incumbentSpec, incumbentStrength), 0, 100, false, 0,
            &incumbentEpisodes, spec.anchor_id, spec.side));
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
            RootDir(), theta, BuildAdapter(candidate.spec, candidate.strength), 0, 100, false, 0,
            &candidateEpisodes, spec.anchor_id, spec.side));
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
    bool trace = false;
    double cutoff = 0;
    ExtractBool(args, "include_prefix_trace", &trace);
    ExtractNumber(args, "cutoff_t_ms", &cutoff);
    if (!std::isfinite(cutoff) || cutoff < 0 || cutoff > 9007199254740991.0)
        return "{\"ok\":false,\"error\":\"invalid cutoff_t_ms\"}";
    const std::string baseline = EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), theta, {}, 0, 1000, trace, static_cast<int64_t>(cutoff), nullptr,
        spec.anchor_id, spec.side);
    const std::string frozen = EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), theta, BuildAdapter(spec, strength), 0, 1000, trace, static_cast<int64_t>(cutoff), nullptr,
        spec.anchor_id, spec.side);
    if (!ParseMetrics(baseline).ok || !ParseMetrics(frozen).ok)
        return "{\"ok\":false,\"error\":\"valid unique-timestamp HSMM history required; no score-only fallback permitted\"}";
    return "{\"ok\":true,\"mode\":\"frozen_template_no_tuning\",\"template\":" +
        SpecJson(spec, "FROZEN", strength) + ",\"baseline\":" + baseline + ",\"frozen\":" + frozen + '}';
}

std::string DiagnoseContextTemplateOnHistoryAction(const std::string &args)
{
    std::string ablation;
    if (!ExtractString(args, "ablation", &ablation) ||
        (ablation != "positive" && ablation != "negative" && ablation != "both" && ablation != "cancel_path"))
        return "{\"ok\":false,\"error\":\"ablation must be positive|negative|both|cancel_path\"}";
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
    TemplateSpec ablatedSpec = spec;
    if (ablation == "cancel_path") {
        double index = -1;
        if (!ExtractNumber(args, "path_index", &index) || !std::isfinite(index) ||
            index < 0 || index != std::floor(index) || index >= spec.cancel_paths.size())
            return "{\"ok\":false,\"error\":\"valid zero-based path_index required\"}";
        ablatedSpec.cancel_paths.erase(ablatedSpec.cancel_paths.begin() + static_cast<size_t>(index));
    }
    // Fresh adapter state for each arm. No profile/theta writes, no candidate fit.
    const Theta theta = CurrentTheta();
    auto disabled = [adapter = BuildAdapter(ablatedSpec, strength), ablation](LeaveObservation &obs, bool start) mutable {
        adapter(obs, start);
        if (ablation == "positive" || ablation == "both") {
            obs.sequence_progress = 0;
            obs.sequence_complete = 0;
            obs.sequence_ready = -1;
        }
        if (ablation == "negative" || ablation == "both") {
            obs.negative_pattern_match = 0;
            obs.cancel_sequence_match = 0;
        }
    };
    const auto active = EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), theta, BuildAdapter(spec, strength), 0, static_cast<int>(limit), false, 0, nullptr,
        spec.anchor_id, spec.side);
    const auto counterfactual = EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), theta, disabled, 0, static_cast<int>(limit), false, 0, nullptr,
        spec.anchor_id, spec.side);
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
    if (gActiveState.last_match_ms > 0 &&
        (tMs < gActiveState.last_match_ms || tMs - gActiveState.last_match_ms > 600000)) {
        gActiveState = TemplateRuntimeState {};
    }
    observation->t_ms = tMs;
    const bool advanced = ApplySpec(gActiveSpec, gActiveStrength, observation, &gActiveState);
    if (advanced) gActiveState.last_match_ms = tMs;
    if (observation->outside || (!gActiveSpec.cancel_paths_mode &&
        (observation->approaching || (observation->attached && gActiveState.cancel_until_ms <= 0)))) {
        gActiveState = TemplateRuntimeState {};
    }
    return true;
}

void ReloadActiveContextTemplateRuntime()
{
    std::lock_guard<std::mutex> lock(gTemplateMutex);
    gActiveLoaded = false;
    gActivePresent = false;
    gActiveState = TemplateRuntimeState {};
}

}  // namespace commute_sa
