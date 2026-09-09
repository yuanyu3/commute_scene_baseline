#include "commute_sa/evidence_strength_profile.h"

#include "commute_sa/product_store.h"
#include "commute_sa/theta_eval.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

namespace commute_sa {
namespace {

constexpr double kFullSampleCount = 8.0;
constexpr double kMaxStrengthChange = 0.30;
constexpr int kMinDurationSamples = 3;
constexpr double kMaxDurationRelativeChange = 0.50;

struct ChannelStats {
    int pos_hit = 0;
    int pos_valid = 0;
    int neg_hit = 0;
    int neg_valid = 0;
};

struct EpisodeChannel {
    bool seen = false;
    bool valid = false;
    double peak = 0.0;
};

struct Episode {
    std::string label;
    std::map<std::string, EpisodeChannel> channels;
};

struct Trial {
    bool active = false;
    bool eligible = false;
    std::string side;
    std::string anchor_id;
    Theta baseline;
    Theta candidate;
    std::string baseline_json;
    std::string candidate_json;
    std::string profile_json;
    std::string rejection;
    std::set<std::string> override_channels;
    std::set<std::string> override_durations;
};

struct DurationTrial {
    bool active = false;
    bool eligible = false;
    std::string side;
    std::string anchor_id;
    std::string state;
    Theta baseline;
    Theta candidate;
    std::string baseline_json;
    std::string candidate_json;
    std::string profile_json;
    std::string rejection;
    std::set<std::string> override_channels;
    std::set<std::string> override_durations;
    int sample_count = 0;
    double median = 0.0;
    double trimmed_mean = 0.0;
    double mad = 0.0;
    double confidence = 0.0;
};

std::mutex gMu;
Trial gTrial;
DurationTrial gDurationTrial;
std::mutex gProfileMu;
struct CachedProfile {
    bool loaded = false;
    bool found = false;
    Theta theta;
    std::string raw;
};
std::map<std::string, CachedProfile> gProfiles;

std::string Esc(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '"') out.push_back('\\');
        if (c == '\n' || c == '\r') continue;
        out.push_back(c);
    }
    return out;
}

bool ExtractString(const std::string &json, const std::string &key, std::string *out)
{
    std::smatch m;
    const std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    if (!std::regex_search(json, m, re)) return false;
    *out = m[1].str();
    return true;
}

bool ExtractNumber(const std::string &json, const std::string &key, double *out)
{
    std::smatch m;
    const std::regex re("\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    if (!std::regex_search(json, m, re)) return false;
    *out = std::stod(m[1].str());
    return true;
}

bool ExtractBool(const std::string &json, const std::string &key, bool *out)
{
    std::smatch m;
    const std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    if (!std::regex_search(json, m, re)) return false;
    *out = m[1].str() == "true";
    return true;
}

std::string RootDir()
{
    const auto root = ProductStore::GetInstance().RootDir();
    return root.empty() ? std::string("/data/service/el1/public/commuteagentservice") : root;
}

std::string ProfilePath(const std::string &side)
{
    return RootDir() + "/user_anchor_profile_" + side + ".json";
}

double *Strength(Theta *theta, const std::string &name)
{
    if (name == "walking") return &theta->w_walk;
    if (name == "pdr") return &theta->w_pdr;
    if (name == "geo") return &theta->w_geo;
    if (name == "wifi") return &theta->w_wifi;
    if (name == "cell") return &theta->w_cell;
    if (name == "ble") return &theta->w_ble;
    if (name == "time") return &theta->w_time;
    if (name == "baro") return &theta->w_baro;
    return nullptr;
}

double StrengthValue(const Theta &theta, const std::string &name)
{
    Theta copy = theta;
    const double *value = Strength(&copy, name);
    return value == nullptr ? 0.0 : *value;
}

double *DurationMean(Theta *theta, const std::string &name)
{
    if (name == "PRE_LEAVE") return &theta->hsmm_preleave_mean_s;
    if (name == "LEAVING") return &theta->hsmm_leaving_mean_s;
    return nullptr;
}

std::string DurationKey(const std::string &state)
{
    return state == "PRE_LEAVE" ? "pre_leave_mean_s" : "leaving_mean_s";
}

const std::vector<std::string> &Channels()
{
    static const std::vector<std::string> names =
        {"walking", "pdr", "geo", "wifi", "cell", "ble", "time", "baro"};
    return names;
}

std::string ObsKey(const std::string &name)
{
    if (name == "walking") return "obs_walking";
    if (name == "pdr") return "obs_pdr_outbound";
    if (name == "geo") return "obs_geo_outbound";
    if (name == "wifi") return "obs_wifi_detach";
    if (name == "cell") return "obs_cell_detach";
    if (name == "ble") return "obs_ble_detach";
    if (name == "time") return "obs_time_prior";
    return "obs_baro_descending";
}

bool LoadProfile(const std::string &side, const std::string &anchorId, Theta *theta, std::string *raw)
{
    const std::string cacheKey = RootDir() + ":" + side + ":" + anchorId;
    std::lock_guard<std::mutex> cacheLock(gProfileMu);
    auto &cached = gProfiles[cacheKey];
    if (cached.loaded) {
        if (!cached.found) return false;
        for (const auto &name : Channels()) *Strength(theta, name) = *Strength(&cached.theta, name);
        if (raw) *raw = cached.raw;
        return true;
    }
    cached.loaded = true;
    std::ifstream in(ProfilePath(side), std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string json = ss.str();
    std::string storedSide;
    std::string storedAnchor;
    if (!ExtractString(json, "side", &storedSide) || !ExtractString(json, "anchor_id", &storedAnchor) ||
        storedSide != side || storedAnchor != anchorId) return false;
    for (const auto &name : Channels()) {
        double value = 0.0;
        if (ExtractNumber(json, name, &value)) {
            *Strength(theta, name) = std::clamp(value, 0.0, 1.0);
        }
    }
    double value = 0.0;
    if (ExtractNumber(json, "pre_leave_mean_s", &value)) {
        theta->hsmm_preleave_mean_s = std::clamp(value, 20.0, 240.0);
    }
    if (ExtractNumber(json, "leaving_mean_s", &value)) {
        theta->hsmm_leaving_mean_s = std::clamp(value, 20.0, 360.0);
    }
    cached.found = true;
    cached.theta = *theta;
    cached.raw = json;
    if (raw) *raw = json;
    return true;
}

std::set<std::string> RequestedChannels(const std::string &csv)
{
    std::set<std::string> result;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (std::find(Channels().begin(), Channels().end(), item) != Channels().end()) result.insert(item);
    }
    if (result.empty()) result.insert(Channels().begin(), Channels().end());
    return result;
}

void FindExistingOverrides(const std::string &raw, std::set<std::string> *channels,
    std::set<std::string> *durations)
{
    if (channels != nullptr) {
        for (const auto &name : Channels()) {
            double value = 0.0;
            if (ExtractNumber(raw, name, &value)) channels->insert(name);
        }
    }
    if (durations != nullptr) {
        double value = 0.0;
        if (ExtractNumber(raw, "pre_leave_mean_s", &value)) durations->insert("PRE_LEAVE");
        if (ExtractNumber(raw, "leaving_mean_s", &value)) durations->insert("LEAVING");
    }
}

bool IsPositive(const std::string &label)
{
    return label == "CONFIRMED_LEAVE" || label == "MISSED_LEAVE";
}

bool IsNegative(const std::string &label)
{
    return label == "FALSE_PUSH" || label == "TRUE_NEGATIVE";
}

std::map<std::string, Episode> LoadEpisodes(const std::string &side)
{
    std::map<std::string, Episode> episodes;
    std::set<std::string> aborted;
    {
        std::ifstream labels(RootDir() + "/episode_interpretations.jsonl");
        std::string row;
        while (std::getline(labels, row)) {
            std::string rowSide;
            std::string episodeId;
            std::string type;
            double outcome = 0.0;
            if (ExtractString(row, "side", &rowSide) && rowSide == side &&
                ExtractString(row, "episode_id", &episodeId) &&
                ExtractString(row, "episode_type", &type) && type == "ABORTED_LEAVE" &&
                ExtractNumber(row, "outcome_t_ms", &outcome)) {
                aborted.insert(episodeId + ":" + std::to_string(static_cast<int64_t>(outcome)));
            }
        }
    }
    std::ifstream in(RootDir() + "/policy_history.jsonl");
    std::string line;
    while (std::getline(in, line)) {
        std::string rowSide;
        std::string label;
        std::string episodeId;
        double outcome = 0.0;
        if (!ExtractString(line, "side", &rowSide) || rowSide != side ||
            !ExtractString(line, "label", &label) || (!IsPositive(label) && !IsNegative(label))) continue;
        ExtractString(line, "episode_id", &episodeId);
        if (!ExtractNumber(line, "outcome_t_ms", &outcome)) ExtractNumber(line, "t_ms", &outcome);
        const std::string key = episodeId + ":" + std::to_string(static_cast<int64_t>(outcome));
        if (aborted.count(key) != 0) continue;  // structural return, not a strength negative
        auto &ep = episodes[key];
        ep.label = label;
        for (const auto &name : Channels()) {
            double value = 0.0;
            const bool seen = ExtractNumber(line, ObsKey(name), &value);
            auto &ch = ep.channels[name];
            ch.seen = ch.seen || seen;
            if (!seen) continue;
            bool valid = true;
            if (name == "baro") {
                bool available = false;
                valid = ExtractBool(line, "obs_baro_available", &available) && available;
            } else if (name == "geo") {
                bool relationKnown = false;
                valid = ExtractBool(line, "obs_relation_known", &relationKnown) && relationKnown;
            }
            ch.valid = ch.valid || valid;
            if (valid) ch.peak = std::max(ch.peak, value);
        }
    }
    return episodes;
}

std::string BuildProfileJson(const std::string &side, const std::string &anchorId, const Theta &candidate,
    const std::set<std::string> &channels, const std::set<std::string> &durations,
    int posN, int negN, const std::string &estimator)
{
    std::ostringstream out;
    out << "{\"schema_version\":1,\"side\":\"" << Esc(side)
        << "\",\"anchor_id\":\"" << Esc(anchorId) << "\",\"evidence_strength\":{";
    bool first = true;
    for (const auto &name : Channels()) {
        if (channels.count(name) == 0) continue;
        if (!first) out << ',';
        first = false;
        out << "\"" << name << "\":" << StrengthValue(candidate, name);
    }
    out << "},\"duration_prior\":{";
    first = true;
    if (durations.count("PRE_LEAVE") != 0) {
        out << "\"pre_leave_mean_s\":" << candidate.hsmm_preleave_mean_s;
        first = false;
    }
    if (durations.count("LEAVING") != 0) {
        if (!first) out << ',';
        out << "\"leaving_mean_s\":" << candidate.hsmm_leaving_mean_s;
    }
    out << "},\"feature_thresholds\":{},\"metadata\":{"
        << "\"positive_episode_count\":" << posN << ",\"negative_episode_count\":" << negN
        << ",\"estimator\":\"" << Esc(estimator) << "\"}}";
    return out.str();
}

double Metric(const std::string &json, const std::string &name, double fallback)
{
    double value = fallback;
    ExtractNumber(json, name, &value);
    return value;
}

}  // namespace

double EstimateEvidenceStrengthValue(double globalStrength, int posHit, int posValid,
    int negHit, int negValid, int totalEpisodes)
{
    const int valid = posValid + negValid;
    const double pPos = (posHit + 1.0) / (posValid + 2.0);
    const double pNeg = (negHit + 1.0) / (negValid + 2.0);
    const double discrimination = std::max(0.0, pPos - pNeg);
    const double coverage = totalEpisodes <= 0 ? 0.0 : valid / static_cast<double>(totalEpisodes);
    const double confidence = std::min(1.0, valid / kFullSampleCount);
    const double raw = (1.0 - confidence) * globalStrength + confidence * coverage * discrimination;
    return std::clamp(raw, std::max(0.0, globalStrength - kMaxStrengthChange),
        std::min(1.0, globalStrength + kMaxStrengthChange));
}

double EstimateDurationMeanValue(double globalMean, const std::vector<double> &samples,
    double *medianOut, double *trimmedMeanOut, double *madOut, double *confidenceOut)
{
    std::vector<double> values;
    for (double value : samples) {
        if (std::isfinite(value) && value > 0.0 && value <= 3600.0) values.push_back(value);
    }
    std::sort(values.begin(), values.end());
    auto medianOf = [](const std::vector<double> &v) {
        if (v.empty()) return 0.0;
        const size_t mid = v.size() / 2;
        return v.size() % 2 == 0 ? 0.5 * (v[mid - 1] + v[mid]) : v[mid];
    };
    const double initialMedian = medianOf(values);
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (double value : values) deviations.push_back(std::fabs(value - initialMedian));
    std::sort(deviations.begin(), deviations.end());
    const double initialMad = medianOf(deviations);

    std::vector<double> robust = values;
    if (initialMad > 0.0) {
        const double limit = 3.0 * 1.4826 * initialMad;
        robust.clear();
        for (double value : values) {
            if (std::fabs(value - initialMedian) <= limit) robust.push_back(value);
        }
    }
    const double median = medianOf(robust);
    std::vector<double> robustDev;
    for (double value : robust) robustDev.push_back(std::fabs(value - median));
    std::sort(robustDev.begin(), robustDev.end());
    const double mad = medianOf(robustDev);
    size_t trim = robust.size() >= 5 ? robust.size() / 10 : 0;
    if (trim * 2 >= robust.size()) trim = 0;
    double sum = 0.0;
    for (size_t i = trim; i < robust.size() - trim; ++i) sum += robust[i];
    const size_t kept = robust.size() >= 2 * trim ? robust.size() - 2 * trim : 0;
    const double trimmedMean = kept == 0 ? 0.0 : sum / kept;
    double confidence = 0.0;
    if (robust.size() >= 8) confidence = std::min(1.0, 0.75 + 0.05 * (robust.size() - 8));
    else if (robust.size() >= 5) confidence = 0.55;
    else if (robust.size() >= kMinDurationSamples) confidence = 0.25;

    if (medianOut) *medianOut = median;
    if (trimmedMeanOut) *trimmedMeanOut = trimmedMean;
    if (madOut) *madOut = mad;
    if (confidenceOut) *confidenceOut = confidence;
    if (confidence == 0.0) return globalMean;
    const double fitted = 0.5 * (median + trimmedMean);
    const double shrunk = (1.0 - confidence) * globalMean + confidence * fitted;
    return std::clamp(shrunk, globalMean * (1.0 - kMaxDurationRelativeChange),
        globalMean * (1.0 + kMaxDurationRelativeChange));
}

Theta ApplyCommittedUserAnchorProfile(
    const Theta &global, const std::string &side, const std::string &anchorId)
{
    Theta effective = global;
    LoadProfile(side, anchorId, &effective, nullptr);
    return effective;
}

Theta ApplyCommittedEvidenceStrengthProfile(
    const Theta &global, const std::string &side, const std::string &anchorId)
{
    return ApplyCommittedUserAnchorProfile(global, side, anchorId);
}

std::string GetUserAnchorProfileAction(const std::string &paramsJson)
{
    std::string side;
    std::string anchor;
    if (!ExtractString(paramsJson, "side", &side) || !ExtractString(paramsJson, "anchor_id", &anchor) ||
        (side != "home" && side != "company")) {
        return "{\"ok\":false,\"error\":\"side and anchor_id required\"}";
    }
    Theta effective = DefaultTheta();
    std::string raw;
    if (!LoadProfile(side, anchor, &effective, &raw)) {
        return "{\"ok\":true,\"active\":false,\"fallback\":\"global evidence strength\"}";
    }
    return "{\"ok\":true,\"active\":true,\"profile\":" + raw + "}";
}

std::string EstimateEvidenceStrengthAction(const std::string &paramsJson)
{
    std::lock_guard<std::mutex> lock(gMu);
    if (gTrial.active || gDurationTrial.active) {
        return "{\"ok\":false,\"error\":\"another personalization trial is active\"}";
    }
    std::string side;
    std::string anchor;
    std::string families;
    if (!ExtractString(paramsJson, "side", &side) || !ExtractString(paramsJson, "anchor_id", &anchor) ||
        (side != "home" && side != "company")) {
        return "{\"ok\":false,\"error\":\"side and anchor_id required\"}";
    }
    ExtractString(paramsJson, "families", &families);
    const auto requested = RequestedChannels(families);
    auto episodes = LoadEpisodes(side);
    std::map<std::string, ChannelStats> stats;
    int posN = 0;
    int negN = 0;
    for (const auto &entry : episodes) {
        const bool positive = IsPositive(entry.second.label);
        if (positive) ++posN; else ++negN;
        for (const auto &name : requested) {
            const auto found = entry.second.channels.find(name);
            if (found == entry.second.channels.end() || !found->second.valid) continue;
            auto &s = stats[name];
            if (positive) {
                ++s.pos_valid;
                if (found->second.peak >= 0.5) ++s.pos_hit;
            } else {
                ++s.neg_valid;
                if (found->second.peak >= 0.5) ++s.neg_hit;
            }
        }
    }

    Theta global = DefaultTheta();
    LoadThetaFromFile(RootDir() + "/theta.json", &global, nullptr);
    gTrial = Trial {};
    gTrial.active = true;
    gTrial.side = side;
    gTrial.anchor_id = anchor;
    gTrial.baseline = ApplyCommittedUserAnchorProfile(global, side, anchor);
    gTrial.candidate = gTrial.baseline;
    std::string activeRaw;
    Theta ignored = global;
    if (LoadProfile(side, anchor, &ignored, &activeRaw)) {
        FindExistingOverrides(activeRaw, &gTrial.override_channels, &gTrial.override_durations);
    }
    gTrial.override_channels.insert(requested.begin(), requested.end());

    std::ostringstream details;
    details << '{';
    bool first = true;
    for (const auto &name : requested) {
        const auto s = stats[name];
        const int valid = s.pos_valid + s.neg_valid;
        const double pPos = (s.pos_hit + 1.0) / (s.pos_valid + 2.0);
        const double pNeg = (s.neg_hit + 1.0) / (s.neg_valid + 2.0);
        const double discrimination = std::max(0.0, pPos - pNeg);
        const double coverage = episodes.empty() ? 0.0 : valid / static_cast<double>(episodes.size());
        const double confidence = std::min(1.0, valid / kFullSampleCount);
        const double base = *Strength(&gTrial.baseline, name);
        const double fitted = EstimateEvidenceStrengthValue(
            base, s.pos_hit, s.pos_valid, s.neg_hit, s.neg_valid, static_cast<int>(episodes.size()));
        *Strength(&gTrial.candidate, name) = fitted;
        if (!first) details << ',';
        first = false;
        details << "\"" << name << "\":{\"pos_hit\":" << s.pos_hit << ",\"pos_valid\":" << s.pos_valid
            << ",\"neg_hit\":" << s.neg_hit << ",\"neg_valid\":" << s.neg_valid
            << ",\"coverage\":" << coverage << ",\"confidence\":" << confidence
            << ",\"global\":" << base << ",\"candidate\":" << fitted << '}';
    }
    details << '}';

    gTrial.baseline.focus_side = side;
    gTrial.candidate.focus_side = side;
    std::vector<ReplayEpisodeSummary> baselineEpisodes;
    std::vector<ReplayEpisodeSummary> candidateEpisodes;
    gTrial.baseline_json = EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), gTrial.baseline, {}, 0, 0, false, 0, &baselineEpisodes);
    gTrial.candidate_json = EvaluateThetaOnHistoryWithAdapterJson(
        RootDir(), gTrial.candidate, {}, 0, 0, false, 0, &candidateEpisodes);
    const bool replayOk = gTrial.baseline_json.find("\"ok\":true") != std::string::npos &&
        gTrial.candidate_json.find("\"ok\":true") != std::string::npos;
    std::string episodeSafetyReason;
    const bool episodeSafe = replayOk &&
        CheckReplayEpisodeSafety(baselineEpisodes, candidateEpisodes, &episodeSafetyReason);
    const bool enoughClasses = posN > 0 && negN > 0;
    const bool noMoreFalse = Metric(gTrial.candidate_json, "false_kept", 1e9) <=
        Metric(gTrial.baseline_json, "false_kept", -1);
    const bool noMoreMiss = Metric(gTrial.candidate_json, "missed_leave", 1e9) <=
        Metric(gTrial.baseline_json, "missed_leave", -1);
    const bool keepConfirmed = Metric(gTrial.candidate_json, "confirmed_kept", -1) >=
        Metric(gTrial.baseline_json, "confirmed_kept", 1e9);
    gTrial.eligible = replayOk && enoughClasses && episodeSafe && noMoreFalse && noMoreMiss && keepConfirmed;
    if (!enoughClasses) gTrial.rejection = "need_positive_and_negative_episodes";
    else if (!replayOk) gTrial.rejection = "full_hsmm_replay_unavailable";
    else if (!episodeSafe) gTrial.rejection = episodeSafetyReason;
    else if (!noMoreFalse) gTrial.rejection = "false_push_increased";
    else if (!noMoreMiss) gTrial.rejection = "missed_leave_increased";
    else if (!keepConfirmed) gTrial.rejection = "confirmed_leave_decreased";
    gTrial.profile_json = BuildProfileJson(gTrial.side, gTrial.anchor_id, gTrial.candidate,
        gTrial.override_channels, gTrial.override_durations, posN, negN,
        "laplace_discrimination_shrinkage_v1");

    return "{\"ok\":true,\"trial_active\":true,\"eligible\":" +
        std::string(gTrial.eligible ? "true" : "false") + ",\"rejection\":\"" + Esc(gTrial.rejection) +
        "\",\"stats\":" + details.str() + ",\"candidate_profile\":" + gTrial.profile_json +
        ",\"baseline_replay\":" + gTrial.baseline_json + ",\"candidate_replay\":" + gTrial.candidate_json + "}";
}

std::string GetEvidenceStrengthTrialAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gMu);
    if (!gTrial.active) return "{\"ok\":true,\"trial_active\":false}";
    return "{\"ok\":true,\"trial_active\":true,\"eligible\":" +
        std::string(gTrial.eligible ? "true" : "false") + ",\"rejection\":\"" +
        Esc(gTrial.rejection) + "\",\"candidate_profile\":" + gTrial.profile_json + "}";
}

std::string CommitEvidenceStrengthCandidateAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gMu);
    if (!gTrial.active || !gTrial.eligible) {
        return "{\"ok\":false,\"error\":\"no eligible evidence strength candidate\"}";
    }
    std::ofstream out(ProfilePath(gTrial.side), std::ios::trunc);
    if (!out) return "{\"ok\":false,\"error\":\"cannot persist user anchor profile\"}";
    out << gTrial.profile_json << '\n';
    {
        std::lock_guard<std::mutex> cacheLock(gProfileMu);
        gProfiles.erase(RootDir() + ":" + gTrial.side + ":" + gTrial.anchor_id);
    }
    const std::string profile = gTrial.profile_json;
    gTrial = Trial {};
    return "{\"ok\":true,\"committed\":true,\"profile\":" + profile + "}";
}

std::string DiscardEvidenceStrengthCandidateAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gMu);
    const bool active = gTrial.active;
    gTrial = Trial {};
    return "{\"ok\":true,\"discarded\":" + std::string(active ? "true" : "false") + "}";
}

std::string FitDurationPriorAction(const std::string &paramsJson)
{
    std::lock_guard<std::mutex> lock(gMu);
    if (gTrial.active || gDurationTrial.active) {
        return "{\"ok\":false,\"error\":\"another personalization trial is active\"}";
    }
    std::string side;
    std::string anchor;
    std::string state;
    if (!ExtractString(paramsJson, "side", &side) || !ExtractString(paramsJson, "anchor_id", &anchor) ||
        !ExtractString(paramsJson, "state", &state) || (side != "home" && side != "company") ||
        (state != "PRE_LEAVE" && state != "LEAVING")) {
        return "{\"ok\":false,\"error\":\"side, anchor_id and state PRE_LEAVE|LEAVING required\"}";
    }

    Theta global = DefaultTheta();
    LoadThetaFromFile(RootDir() + "/theta.json", &global, nullptr);
    gDurationTrial = DurationTrial {};
    gDurationTrial.active = true;
    gDurationTrial.side = side;
    gDurationTrial.anchor_id = anchor;
    gDurationTrial.state = state;
    gDurationTrial.baseline = ApplyCommittedUserAnchorProfile(global, side, anchor);
    gDurationTrial.candidate = gDurationTrial.baseline;

    std::string activeRaw;
    Theta ignored = global;
    if (LoadProfile(side, anchor, &ignored, &activeRaw)) {
        FindExistingOverrides(activeRaw, &gDurationTrial.override_channels, &gDurationTrial.override_durations);
    }
    gDurationTrial.override_durations.insert(state);

    const auto samples = InferHsmmDurationSamples(RootDir(), gDurationTrial.baseline, side, state, 100);
    gDurationTrial.sample_count = static_cast<int>(samples.size());
    double *candidateMean = DurationMean(&gDurationTrial.candidate, state);
    const double globalMean = *DurationMean(&gDurationTrial.baseline, state);
    *candidateMean = EstimateDurationMeanValue(globalMean, samples, &gDurationTrial.median,
        &gDurationTrial.trimmed_mean, &gDurationTrial.mad, &gDurationTrial.confidence);
    if (state == "PRE_LEAVE") {
        *candidateMean = std::clamp(*candidateMean, gDurationTrial.candidate.hsmm_preleave_min_s,
            gDurationTrial.candidate.hsmm_preleave_max_s);
    } else {
        *candidateMean = std::clamp(*candidateMean, gDurationTrial.candidate.hsmm_leaving_min_s,
            gDurationTrial.candidate.hsmm_leaving_max_s);
    }
    gDurationTrial.baseline.focus_side = side;
    gDurationTrial.candidate.focus_side = side;
    std::vector<ReplayEpisodeSummary> baselineEpisodes;
    std::vector<ReplayEpisodeSummary> candidateEpisodes;
    gDurationTrial.baseline_json = EvaluateThetaOnHistoryWithAdapterJson(RootDir(),
        gDurationTrial.baseline, {}, 0, 0, false, 0, &baselineEpisodes);
    gDurationTrial.candidate_json = EvaluateThetaOnHistoryWithAdapterJson(RootDir(),
        gDurationTrial.candidate, {}, 0, 0, false, 0, &candidateEpisodes);
    const bool replayOk = gDurationTrial.baseline_json.find("\"ok\":true") != std::string::npos &&
        gDurationTrial.candidate_json.find("\"ok\":true") != std::string::npos;
    std::string episodeSafetyReason;
    const bool episodeSafe = replayOk &&
        CheckReplayEpisodeSafety(baselineEpisodes, candidateEpisodes, &episodeSafetyReason);
    const bool enoughSamples = gDurationTrial.sample_count >= kMinDurationSamples;
    const bool noMoreFalse = Metric(gDurationTrial.candidate_json, "false_kept", 1e9) <=
        Metric(gDurationTrial.baseline_json, "false_kept", -1);
    const bool noMoreMiss = Metric(gDurationTrial.candidate_json, "missed_leave", 1e9) <=
        Metric(gDurationTrial.baseline_json, "missed_leave", -1);
    const bool keepConfirmed = Metric(gDurationTrial.candidate_json, "confirmed_kept", -1) >=
        Metric(gDurationTrial.baseline_json, "confirmed_kept", 1e9);
    gDurationTrial.eligible = enoughSamples && replayOk && episodeSafe && noMoreFalse && noMoreMiss && keepConfirmed;
    if (!enoughSamples) gDurationTrial.rejection = "need_at_least_3_uncensored_positive_samples";
    else if (!replayOk) gDurationTrial.rejection = "full_hsmm_replay_unavailable";
    else if (!episodeSafe) gDurationTrial.rejection = episodeSafetyReason;
    else if (!noMoreFalse) gDurationTrial.rejection = "false_push_increased";
    else if (!noMoreMiss) gDurationTrial.rejection = "missed_leave_increased";
    else if (!keepConfirmed) gDurationTrial.rejection = "confirmed_leave_decreased";

    auto episodes = LoadEpisodes(side);
    int posN = 0;
    int negN = 0;
    for (const auto &entry : episodes) {
        if (IsPositive(entry.second.label)) ++posN;
        else if (IsNegative(entry.second.label)) ++negN;
    }
    gDurationTrial.profile_json = BuildProfileJson(gDurationTrial.side, gDurationTrial.anchor_id,
        gDurationTrial.candidate, gDurationTrial.override_channels, gDurationTrial.override_durations,
        posN, negN, "robust_posterior_duration_shrinkage_v1");

    std::ostringstream out;
    out << "{\"ok\":true,\"trial_active\":true,\"eligible\":"
        << (gDurationTrial.eligible ? "true" : "false") << ",\"rejection\":\""
        << Esc(gDurationTrial.rejection) << "\",\"state\":\"" << state
        << "\",\"sample_count\":" << gDurationTrial.sample_count
        << ",\"median_s\":" << gDurationTrial.median
        << ",\"trimmed_mean_s\":" << gDurationTrial.trimmed_mean
        << ",\"mad_s\":" << gDurationTrial.mad
        << ",\"confidence\":" << gDurationTrial.confidence
        << ",\"global_mean_s\":" << globalMean << ",\"candidate_mean_s\":" << *candidateMean
        << ",\"candidate_profile\":" << gDurationTrial.profile_json
        << ",\"baseline_replay\":" << gDurationTrial.baseline_json
        << ",\"candidate_replay\":" << gDurationTrial.candidate_json << '}';
    return out.str();
}

std::string GetDurationPriorTrialAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gMu);
    if (!gDurationTrial.active) return "{\"ok\":true,\"trial_active\":false}";
    return "{\"ok\":true,\"trial_active\":true,\"eligible\":" +
        std::string(gDurationTrial.eligible ? "true" : "false") + ",\"rejection\":\"" +
        Esc(gDurationTrial.rejection) + "\",\"candidate_profile\":" + gDurationTrial.profile_json + "}";
}

std::string CommitDurationPriorCandidateAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gMu);
    if (!gDurationTrial.active || !gDurationTrial.eligible) {
        return "{\"ok\":false,\"error\":\"no eligible duration candidate\"}";
    }
    std::ofstream out(ProfilePath(gDurationTrial.side), std::ios::trunc);
    if (!out) return "{\"ok\":false,\"error\":\"cannot persist user anchor profile\"}";
    out << gDurationTrial.profile_json << '\n';
    {
        std::lock_guard<std::mutex> cacheLock(gProfileMu);
        gProfiles.erase(RootDir() + ":" + gDurationTrial.side + ":" + gDurationTrial.anchor_id);
    }
    const std::string profile = gDurationTrial.profile_json;
    gDurationTrial = DurationTrial {};
    return "{\"ok\":true,\"committed\":true,\"profile\":" + profile + "}";
}

std::string DiscardDurationPriorCandidateAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gMu);
    const bool active = gDurationTrial.active;
    gDurationTrial = DurationTrial {};
    return "{\"ok\":true,\"discarded\":" + std::string(active ? "true" : "false") + "}";
}

}  // namespace commute_sa
