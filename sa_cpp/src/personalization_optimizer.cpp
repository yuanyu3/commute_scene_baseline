#include "commute_sa/personalization_optimizer.h"

#include "commute_sa/baseline_runtime.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/product_store.h"
#include "commute_sa/theta.h"
#include "commute_sa/theta_eval.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace commute_sa {
namespace {

struct ParamSpec {
    const char *name;
    double min_value;
    double max_value;
    double step;
};

constexpr ParamSpec kParamSpecs[] = {
    {"enter_leave", 0.40, 0.85, 0.03},
    {"exit_leave", 0.20, 0.70, 0.03},
    {"w_walk", 0.00, 0.50, 0.05},
    {"w_pdr", 0.00, 0.40, 0.05},
    {"w_geo", 0.05, 0.40, 0.05},
    {"w_wifi", 0.00, 0.40, 0.02},
    {"w_cell", 0.00, 0.30, 0.02},
    {"w_ble", 0.00, 0.20, 0.02},
    {"w_time", 0.05, 0.35, 0.05},
    {"w_baro", 0.00, 0.40, 0.05},
    {"weekday_leave_home_hour", 0.0, 24.0, 0.10},
    {"weekday_leave_company_hour", 11.0, 21.0, 0.10},
    {"arm_delay_s", 0.0, 90.0, 5.0},
    {"hsmm_preleave_min_s", 0.0, 120.0, 5.0},
    {"hsmm_preleave_mean_s", 20.0, 240.0, 10.0},
    {"hsmm_preleave_max_s", 60.0, 900.0, 30.0},
    {"hsmm_leaving_min_s", 0.0, 120.0, 5.0},
    {"hsmm_leaving_mean_s", 20.0, 360.0, 10.0},
    {"hsmm_leaving_max_s", 60.0, 1200.0, 30.0},
    {"lead_min_s", 0.0, 600.0, 15.0},
    {"lead_max_s", 30.0, 900.0, 30.0},
    {"baro_min_descent_m", 2.0, 40.0, 2.0},
};

struct EvalMetrics {
    bool ok = false;
    double score = -1.0e100;
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
    int lead_ok = 0;
    int lead_late = 0;
    int lead_early = 0;
};

struct EpisodeSignature {
    std::string side = "company";
    std::string label;
    int64_t outcome_ms = 0;
    int64_t first_ms = 0;
    int64_t last_ms = 0;
    double max_walk = 0.0;
    double max_pdr = 0.0;
    double max_geo = 0.0;
    double max_wifi = 0.0;
    double max_cell = 0.0;
    double max_ble = 0.0;
    double max_time = 0.0;
    double max_baro_desc = 0.0;
    double max_leaving = 0.0;
    bool baro_available = false;
    bool lower_platform = false;
    bool attached = false;
    bool approaching = false;
    bool outside = false;
};

struct Cause {
    std::string name;
    double score = 0.0;
    std::string block;
    std::string direction;
    std::string evidence;
};

struct Change {
    std::string param;
    double delta = 0.0;
};

struct Candidate {
    int id = 0;
    Theta theta;
    std::vector<Change> changes;
    EvalMetrics metrics;
    std::string eval_json;
    double regularized_score = -1.0e100;
    bool eligible = false;
    std::string rejection;
};

struct OptimizationTrial {
    bool active = false;
    std::string source;
    std::string objective = "balanced";
    std::string plan_json;
    Theta baseline_theta;
    EvalMetrics baseline;
    std::string baseline_json;
    std::vector<Candidate> candidates;
    int best_index = -1;
    double min_improvement = 0.25;
};

std::mutex gOptimizerMutex;
OptimizationTrial gOptimizerTrial;
int gOptimizerRuns = 0;
int gCandidatesEvaluated = 0;

int64_t NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

double Clip01(double v)
{
    return std::max(0.0, std::min(1.0, v));
}

std::string Esc(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '"') out.push_back('\\');
        if (c == '\n' || c == '\r') {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool ExtractNumber(const std::string &json, const char *key, double *out)
{
    if (out == nullptr || key == nullptr) return false;
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) return false;
    i = json.find_first_not_of(" \t\r\n", i + 1);
    if (i == std::string::npos) return false;
    char *end = nullptr;
    const double v = std::strtod(json.c_str() + i, &end);
    if (end == json.c_str() + i) return false;
    *out = v;
    return true;
}

bool ExtractString(const std::string &json, const char *key, std::string *out)
{
    if (out == nullptr || key == nullptr) return false;
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) return false;
    i = json.find_first_not_of(" \t\r\n", i + 1);
    if (i == std::string::npos || json[i] != '"') return false;
    ++i;
    std::string value;
    while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\' && i + 1 < json.size()) {
            value.push_back(json[i + 1]);
            i += 2;
        } else {
            value.push_back(json[i++]);
        }
    }
    *out = value;
    return true;
}

bool ExtractBool(const std::string &json, const char *key, bool *out)
{
    if (out == nullptr || key == nullptr) return false;
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) return false;
    i = json.find_first_not_of(" \t\r\n", i + 1);
    if (i == std::string::npos) return false;
    if (json.compare(i, 4, "true") == 0) { *out = true; return true; }
    if (json.compare(i, 5, "false") == 0) { *out = false; return true; }
    return false;
}

std::string RootDir()
{
    const std::string &product = ProductStore::GetInstance().RootDir();
    if (!product.empty()) return product;
    const std::string &evidence = EvidenceQuery::GetInstance().RootDir();
    if (!evidence.empty()) return evidence;
    return "/data/service/el1/public/commuteagentservice";
}

Theta LoadLiveTheta()
{
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        return BaselineRuntime::GetInstance().Engine()->GetTheta();
    }
    Theta theta = DefaultTheta();
    LoadThetaFromFile(RootDir() + "/theta.json", &theta, nullptr);
    return theta;
}

bool PersistTheta(const Theta &theta, std::string *err)
{
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        BaselineRuntime::GetInstance().Engine()->SetTheta(theta);
    }
    if (!ProductStore::GetInstance().SaveTheta(theta)) {
        if (err) *err = "SaveTheta failed";
        return false;
    }
    return true;
}

const ParamSpec *FindParamSpec(const std::string &name)
{
    for (const auto &spec : kParamSpecs) {
        if (name == spec.name) return &spec;
    }
    return nullptr;
}

bool ReadParam(const Theta &t, const std::string &name, double *out)
{
    if (out == nullptr) return false;
#define READ_PARAM(field) if (name == #field) { *out = t.field; return true; }
    READ_PARAM(enter_leave)
    READ_PARAM(exit_leave)
    READ_PARAM(w_walk)
    READ_PARAM(w_pdr)
    READ_PARAM(w_geo)
    READ_PARAM(w_wifi)
    READ_PARAM(w_cell)
    READ_PARAM(w_ble)
    READ_PARAM(w_time)
    READ_PARAM(w_baro)
    READ_PARAM(weekday_leave_home_hour)
    READ_PARAM(weekday_leave_company_hour)
    READ_PARAM(arm_delay_s)
    READ_PARAM(hsmm_preleave_min_s)
    READ_PARAM(hsmm_preleave_mean_s)
    READ_PARAM(hsmm_preleave_max_s)
    READ_PARAM(hsmm_leaving_min_s)
    READ_PARAM(hsmm_leaving_mean_s)
    READ_PARAM(hsmm_leaving_max_s)
    READ_PARAM(lead_min_s)
    READ_PARAM(lead_max_s)
    READ_PARAM(baro_min_descent_m)
#undef READ_PARAM
    return false;
}

bool SameTunableTheta(const Theta &a, const Theta &b)
{
    for (const auto &spec : kParamSpecs) {
        double av = 0.0;
        double bv = 0.0;
        if (ReadParam(a, spec.name, &av) && ReadParam(b, spec.name, &bv) && std::fabs(av - bv) > 1e-9) {
            return false;
        }
    }
    return true;
}

void NormalizeTheta(Theta *t)
{
    t->exit_leave = std::min(t->exit_leave, t->enter_leave - 0.01);
    t->hsmm_preleave_mean_s = std::max(t->hsmm_preleave_min_s, t->hsmm_preleave_mean_s);
    t->hsmm_preleave_max_s = std::max(t->hsmm_preleave_mean_s, t->hsmm_preleave_max_s);
    t->hsmm_leaving_mean_s = std::max(t->hsmm_leaving_min_s, t->hsmm_leaving_mean_s);
    t->hsmm_leaving_max_s = std::max(t->hsmm_leaving_mean_s, t->hsmm_leaving_max_s);
    t->lead_max_s = std::max(t->lead_min_s, t->lead_max_s);
    t->w_radio = t->w_wifi + t->w_cell + t->w_ble;
}

bool ApplyBoundedChange(Theta *theta, const Change &change)
{
    const ParamSpec *spec = FindParamSpec(change.param);
    double old_value = 0.0;
    if (theta == nullptr || spec == nullptr || !ReadParam(*theta, change.param, &old_value)) return false;
    const double requested = old_value + change.delta;
    const double target = std::max(spec->min_value, std::min(spec->max_value, requested));
    std::string err;
    if (!ApplyThetaDelta(theta, change.param, target - old_value, &err)) return false;
    NormalizeTheta(theta);
    return true;
}

EvalMetrics ParseEval(const std::string &json)
{
    EvalMetrics m;
    bool ok = false;
    ExtractBool(json, "ok", &ok);
    m.ok = ok;
    double v = 0.0;
#define PARSE_INT(field) if (ExtractNumber(json, #field, &v)) m.field = static_cast<int>(v)
    if (ExtractNumber(json, "score", &v)) m.score = v;
    PARSE_INT(n_episodes);
    PARSE_INT(n_false_push);
    PARSE_INT(n_confirmed_leave);
    PARSE_INT(n_missed_leave_label);
    PARSE_INT(false_kept);
    PARSE_INT(false_avoided);
    PARSE_INT(soft_false_kept);
    PARSE_INT(confirmed_kept);
    PARSE_INT(missed_leave);
    PARSE_INT(recovered_miss);
    PARSE_INT(lead_ok);
    PARSE_INT(lead_late);
    PARSE_INT(lead_early);
#undef PARSE_INT
    return m;
}

std::string MetricsJson(const EvalMetrics &m)
{
    std::ostringstream out;
    out << "{\"score\":" << m.score << ",\"n_episodes\":" << m.n_episodes
        << ",\"n_false_push\":" << m.n_false_push << ",\"n_confirmed_leave\":" << m.n_confirmed_leave
        << ",\"n_missed_leave_label\":" << m.n_missed_leave_label
        << ",\"false_kept\":" << m.false_kept << ",\"false_avoided\":" << m.false_avoided
        << ",\"soft_false_kept\":" << m.soft_false_kept << ",\"confirmed_kept\":"
        << m.confirmed_kept << ",\"missed_leave\":" << m.missed_leave << ",\"recovered_miss\":"
        << m.recovered_miss << ",\"lead_ok\":" << m.lead_ok << ",\"lead_late\":" << m.lead_late
        << ",\"lead_early\":" << m.lead_early << "}";
    return out.str();
}

std::vector<EpisodeSignature> LoadSignatures(int maxEpisodes)
{
    std::ifstream in(RootDir() + "/policy_history.jsonl");
    std::map<std::string, EpisodeSignature> grouped;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("\"obs_pdr_outbound\"") == std::string::npos) continue;
        int64_t t_ms = 0;
        int64_t outcome_ms = 0;
        double n = 0.0;
        if (!ExtractNumber(line, "t_ms", &n)) continue;
        t_ms = static_cast<int64_t>(n);
        if (ExtractNumber(line, "outcome_t_ms", &n)) outcome_ms = static_cast<int64_t>(n);
        if (outcome_ms <= 0) outcome_ms = t_ms;
        std::string side = "company";
        std::string label;
        ExtractString(line, "side", &side);
        ExtractString(line, "label", &label);
        const std::string key = side + ":" + std::to_string(outcome_ms) + ":" + label;
        auto &ep = grouped[key];
        ep.side = side;
        ep.label = label;
        ep.outcome_ms = outcome_ms;
        ep.first_ms = ep.first_ms == 0 ? t_ms : std::min(ep.first_ms, t_ms);
        ep.last_ms = std::max(ep.last_ms, t_ms);
        auto maxField = [&](const char *name, double *target) {
            double value = 0.0;
            if (ExtractNumber(line, name, &value)) *target = std::max(*target, value);
        };
        maxField("obs_walking", &ep.max_walk);
        maxField("obs_pdr_outbound", &ep.max_pdr);
        maxField("obs_geo_outbound", &ep.max_geo);
        maxField("obs_wifi_detach", &ep.max_wifi);
        maxField("obs_cell_detach", &ep.max_cell);
        maxField("obs_ble_detach", &ep.max_ble);
        maxField("obs_time_prior", &ep.max_time);
        maxField("obs_baro_descending", &ep.max_baro_desc);
        maxField("leaving_probability", &ep.max_leaving);
        bool flag = false;
        if (ExtractBool(line, "obs_baro_available", &flag)) ep.baro_available = ep.baro_available || flag;
        if (ExtractBool(line, "obs_baro_lower_platform", &flag)) ep.lower_platform = ep.lower_platform || flag;
        if (ExtractBool(line, "obs_attached", &flag)) ep.attached = ep.attached || flag;
        if (ExtractBool(line, "obs_approaching", &flag)) ep.approaching = ep.approaching || flag;
        if (ExtractBool(line, "obs_outside", &flag)) ep.outside = ep.outside || flag;
    }
    std::vector<EpisodeSignature> result;
    for (auto &entry : grouped) result.push_back(std::move(entry.second));
    std::sort(result.begin(), result.end(), [](const EpisodeSignature &a, const EpisodeSignature &b) {
        return a.outcome_ms < b.outcome_ms;
    });
    if (maxEpisodes > 0 && static_cast<int>(result.size()) > maxEpisodes) {
        result.erase(result.begin(), result.end() - maxEpisodes);
    }
    return result;
}

std::vector<Cause> AnalyzeCauses(const std::vector<EpisodeSignature> &episodes, const Theta &theta)
{
    std::vector<Cause> causes;
    if (episodes.empty()) return causes;
    const EpisodeSignature &target = episodes.back();
    const double outbound = std::max(target.max_pdr, target.max_geo);
    const double radio = std::max(target.max_wifi, std::max(target.max_cell, target.max_ble));
    const double duration_s = std::max(0.0, static_cast<double>(target.last_ms - target.first_ms) / 1000.0);
    int confirmed = 0;
    int confirmed_baro = 0;
    int confirmed_lower = 0;
    for (const auto &ep : episodes) {
        if (ep.label == "CONFIRMED_LEAVE") {
            ++confirmed;
            if (ep.baro_available) ++confirmed_baro;
            if (ep.lower_platform) ++confirmed_lower;
        }
    }
    if (target.label == "FALSE_PUSH" && !target.lower_platform) {
        causes.push_back({"WIFI_TRANSIENT", Clip01(target.max_wifi * (target.attached ? 1.0 : 0.65) *
                (1.0 - outbound)), "radio_reliability", "decrease",
            "hard false push with radio detach, weak outbound, and possible reattach"});
        causes.push_back({"WALK_NON_SPECIFIC", Clip01(target.max_walk * (1.0 - outbound) *
                (1.0 - target.max_baro_desc)), "motion_reliability", "decrease",
            "walking was active without corroborating outbound or vertical evidence"});
        const int activeFamilies = static_cast<int>(outbound >= 0.5) + static_cast<int>(radio >= 0.5) +
            static_cast<int>(target.max_baro_desc >= 0.5);
        causes.push_back({"PRELEAVE_TOO_FAST", Clip01((duration_s < theta.hsmm_preleave_mean_s ? 0.65 : 0.25) +
                (activeFamilies <= 1 ? 0.25 : 0.0)), "preleave_duration", "increase",
            "false push developed quickly or from too few independent evidence families"});
        causes.push_back({"GLOBAL_THRESHOLD_TOO_LOW", 0.25, "trigger_threshold", "increase",
            "fallback only; prefer a channel-specific or duration intervention"});
    } else if (target.label == "MISSED_LEAVE" ||
        (target.label == "CONFIRMED_LEAVE" && target.max_leaving < theta.enter_leave)) {
        causes.push_back({"PDR_UNDERUSED", Clip01(target.max_pdr * (1.0 - target.max_leaving)),
            "motion_reliability", "increase", "sustained PDR outbound did not raise leaving probability enough"});
        causes.push_back({"BARO_UNDERUSED", Clip01(std::max(target.max_baro_desc,
                target.lower_platform ? 1.0 : 0.0) * (1.0 - target.max_leaving)),
            "baro_reliability", "increase", "vertical evidence preceded a missed or weak detection"});
        causes.push_back({"PRELEAVE_TOO_SLOW", Clip01((duration_s > theta.hsmm_preleave_mean_s ? 0.7 : 0.3) *
                std::max(outbound, radio)), "preleave_duration", "decrease",
            "corroborating evidence persisted but the process did not advance in time"});
        causes.push_back({"GLOBAL_THRESHOLD_TOO_HIGH", 0.25, "trigger_threshold", "decrease",
            "fallback only; use after evidence reliability and duration are tested"});
    } else if (target.label == "CONFIRMED_LEAVE") {
        causes.push_back({"CONFIRMED_STABLE", 0.70, "preleave_duration", "hold",
            "latest episode was confirmed; no error-specific intervention is justified"});
    }
    if (confirmed >= 3 && confirmed_baro >= 3 && confirmed_lower == 0) {
        causes.push_back({"BARO_NOT_APPLICABLE", Clip01(static_cast<double>(confirmed_baro) / confirmed),
            "baro_reliability", "decrease", "barometer is available but repeated confirmed leaves have no lower platform"});
    }
    std::sort(causes.begin(), causes.end(), [](const Cause &a, const Cause &b) { return a.score > b.score; });
    return causes;
}

std::string CausesJson(const std::vector<EpisodeSignature> &episodes, const std::vector<Cause> &causes)
{
    std::ostringstream out;
    out << "{\"ok\":true,\"algorithm\":\"ES-CRO-rule-diagnosis\",\"n_episodes\":" << episodes.size();
    if (!episodes.empty()) {
        const auto &t = episodes.back();
        out << ",\"target\":{\"side\":\"" << Esc(t.side) << "\",\"label\":\"" << Esc(t.label)
            << "\",\"outcome_ms\":" << t.outcome_ms << ",\"max_walk\":" << t.max_walk
            << ",\"max_pdr\":" << t.max_pdr << ",\"max_geo\":" << t.max_geo
            << ",\"max_wifi\":" << t.max_wifi << ",\"max_time\":" << t.max_time
            << ",\"max_baro_descending\":" << t.max_baro_desc << ",\"baro_available\":"
            << (t.baro_available ? "true" : "false") << ",\"lower_platform\":"
            << (t.lower_platform ? "true" : "false") << ",\"attached\":" << (t.attached ? "true" : "false")
            << ",\"approaching\":" << (t.approaching ? "true" : "false") << "}";
    }
    out << ",\"ranked_causes\":[";
    for (size_t i = 0; i < causes.size(); ++i) {
        if (i) out << ',';
        out << "{\"cause\":\"" << Esc(causes[i].name) << "\",\"score\":" << causes[i].score
            << ",\"block\":\"" << Esc(causes[i].block) << "\",\"direction\":\""
            << Esc(causes[i].direction) << "\",\"evidence\":\"" << Esc(causes[i].evidence) << "\"}";
    }
    out << ']';
    if (!causes.empty()) {
        out << ",\"rule_plan\":{\"primary_block\":\"" << Esc(causes[0].block)
            << "\",\"primary_direction\":\"" << Esc(causes[0].direction) << "\"";
        if (causes.size() > 1) {
            out << ",\"secondary_block\":\"" << Esc(causes[1].block)
                << "\",\"secondary_direction\":\"" << Esc(causes[1].direction) << "\"";
        }
        out << "}";
    }
    out << ",\"notes\":\"Rule scores are deterministic hypotheses, not labels. The shared replay optimizer decides numeric values.\"}";
    return out.str();
}

std::vector<std::pair<std::string, double>> ParamsForBlock(const std::string &block, const std::string &direction)
{
    const double sign = direction == "decrease" ? -1.0 : (direction == "increase" ? 1.0 : 0.0);
    if (sign == 0.0) return {};
    if (block == "radio_reliability") return {{"w_wifi", sign}, {"w_cell", sign}};
    if (block == "motion_reliability") return {{"w_walk", sign}, {"w_pdr", sign}};
    if (block == "geo_reliability") return {{"w_geo", sign}};
    if (block == "baro_reliability") return {{"w_baro", sign}};
    if (block == "time_prior") return {{"w_time", sign}};
    if (block == "preleave_duration") {
        return {{"hsmm_preleave_mean_s", sign}, {"hsmm_preleave_min_s", sign}};
    }
    if (block == "leaving_duration") return {{"hsmm_leaving_mean_s", sign}};
    if (block == "trigger_threshold") return {{"enter_leave", sign}};
    if (block == "arm_timing") return {{"arm_delay_s", sign}};
    return {};
}

std::vector<std::vector<Change>> GenerateChangeSets(const std::string &primaryBlock,
    const std::string &primaryDirection, const std::string &secondaryBlock, const std::string &secondaryDirection,
    int maxCandidates)
{
    std::vector<std::vector<Change>> sets;
    const auto primary = ParamsForBlock(primaryBlock, primaryDirection);
    const auto secondary = ParamsForBlock(secondaryBlock, secondaryDirection);
    auto appendSingles = [&](const auto &params) {
        for (const auto &entry : params) {
            const ParamSpec *spec = FindParamSpec(entry.first);
            if (spec != nullptr) sets.push_back({{entry.first, entry.second * spec->step}});
        }
    };
    appendSingles(primary);
    appendSingles(secondary);
    if (primary.size() >= 2) {
        const ParamSpec *a = FindParamSpec(primary[0].first);
        const ParamSpec *b = FindParamSpec(primary[1].first);
        if (a && b) sets.push_back({{primary[0].first, primary[0].second * a->step},
            {primary[1].first, primary[1].second * b->step}});
    }
    for (const auto &a : primary) {
        for (const auto &b : secondary) {
            const ParamSpec *sa = FindParamSpec(a.first);
            const ParamSpec *sb = FindParamSpec(b.first);
            if (sa && sb && a.first != b.first) {
                sets.push_back({{a.first, a.second * sa->step}, {b.first, b.second * sb->step}});
            }
        }
    }
    if (maxCandidates > 0 && static_cast<int>(sets.size()) > maxCandidates) sets.resize(maxCandidates);
    return sets;
}

bool CandidateEligible(const EvalMetrics &base, const EvalMetrics &candidate, double minImprovement,
    const std::string &objective, std::string *why)
{
    if (!candidate.ok || candidate.n_episodes <= 0) {
        if (why) *why = "evaluation_failed_or_empty";
        return false;
    }
    // One-sided history cannot establish that suppressing or sensitizing the
    // detector is safe. Keep this in code: an Agent may recognize overfitting
    // risk and still request a commit.
    const int positiveLabels = base.n_confirmed_leave + base.n_missed_leave_label;
    if ((objective == "false_push" || objective == "balanced") && positiveLabels <= 0) {
        if (why) *why = "insufficient_positive_history_for_suppression";
        return false;
    }
    if ((objective == "missed_leave" || objective == "balanced") && base.n_false_push <= 0) {
        if (why) *why = "insufficient_negative_history_for_sensitization";
        return false;
    }
    if (objective == "lead" && base.n_confirmed_leave <= 0) {
        if (why) *why = "insufficient_confirmed_history_for_lead_tuning";
        return false;
    }
    if (objective == "false_push" && candidate.false_kept >= base.false_kept) {
        if (why) *why = "false_push_objective_not_improved";
        return false;
    }
    if (objective == "missed_leave" && candidate.missed_leave >= base.missed_leave &&
        candidate.recovered_miss <= base.recovered_miss) {
        if (why) *why = "missed_leave_objective_not_improved";
        return false;
    }
    if (objective == "lead" && candidate.lead_ok <= base.lead_ok &&
        candidate.lead_late + candidate.lead_early >= base.lead_late + base.lead_early) {
        if (why) *why = "lead_objective_not_improved";
        return false;
    }
    if (candidate.score < base.score + minImprovement) {
        if (why) *why = "score_improvement_below_gate";
        return false;
    }
    if (candidate.missed_leave > base.missed_leave) {
        if (why) *why = "missed_leave_increased";
        return false;
    }
    if (candidate.false_kept > base.false_kept) {
        if (why) *why = "hard_false_push_increased";
        return false;
    }
    if (candidate.confirmed_kept < base.confirmed_kept) {
        if (why) *why = "confirmed_recall_decreased";
        return false;
    }
    if (candidate.soft_false_kept < base.soft_false_kept) {
        if (why) *why = "lower_platform_positive_decreased";
        return false;
    }
    return true;
}

std::string TrialJson(const OptimizationTrial &trial)
{
    std::ostringstream out;
    out << "{\"ok\":true,\"trial_active\":" << (trial.active ? "true" : "false")
        << ",\"source\":\"" << Esc(trial.source) << "\",\"objective\":\"" << Esc(trial.objective)
        << "\",\"min_improvement\":" << trial.min_improvement
        << ",\"baseline\":" << MetricsJson(trial.baseline) << ",\"best_candidate_id\":";
    if (trial.best_index >= 0) out << trial.candidates[trial.best_index].id; else out << "null";
    out << ",\"candidates\":[";
    for (size_t i = 0; i < trial.candidates.size(); ++i) {
        if (i) out << ',';
        const auto &c = trial.candidates[i];
        out << "{\"id\":" << c.id << ",\"eligible\":" << (c.eligible ? "true" : "false")
            << ",\"rejection\":\"" << Esc(c.rejection) << "\",\"regularized_score\":"
            << c.regularized_score << ",\"changes\":[";
        for (size_t j = 0; j < c.changes.size(); ++j) {
            if (j) out << ',';
            out << "{\"param\":\"" << Esc(c.changes[j].param) << "\",\"delta\":" << c.changes[j].delta << "}";
        }
        out << "],\"metrics\":" << MetricsJson(c.metrics) << "}";
    }
    out << "],\"commit_guard\":{\"requires_score_improvement\":true,\"missed_leave_must_not_increase\":true,"
           "\"hard_false_must_not_increase\":true,\"confirmed_and_lower_platform_positives_must_not_decrease\":true,"
           "\"suppression_requires_positive_history\":true,\"sensitization_requires_negative_history\":true}}";
    return out.str();
}

std::string RunOptimizer(const std::string &paramsJson, const std::string &source)
{
    std::string primaryBlock;
    std::string primaryDirection;
    std::string secondaryBlock;
    std::string secondaryDirection;
    std::string objective = "balanced";
    ExtractString(paramsJson, "primary_block", &primaryBlock);
    ExtractString(paramsJson, "primary_direction", &primaryDirection);
    ExtractString(paramsJson, "secondary_block", &secondaryBlock);
    ExtractString(paramsJson, "secondary_direction", &secondaryDirection);
    ExtractString(paramsJson, "objective", &objective);
    if (objective != "balanced" && objective != "false_push" && objective != "missed_leave" &&
        objective != "lead") {
        return "{\"ok\":false,\"error\":\"objective must be balanced|false_push|missed_leave|lead\"}";
    }
    if (primaryBlock.empty() || (primaryDirection != "increase" && primaryDirection != "decrease")) {
        return "{\"ok\":false,\"error\":\"primary_block and increase|decrease direction required\"}";
    }
    const auto targetEpisodes = LoadSignatures(1);
    if (!targetEpisodes.empty() && targetEpisodes.back().label == "MISSED_LEAVE" && objective != "missed_leave") {
        return "{\"ok\":false,\"error\":\"target_objective_mismatch\","
               "\"required_objective\":\"missed_leave\",\"target_label\":\"MISSED_LEAVE\"}";
    }
    if (gOptimizerRuns >= 2 || gCandidatesEvaluated >= 20) {
        return "{\"ok\":false,\"error\":\"episode_optimizer_budget_exhausted\","
               "\"max_runs\":2,\"max_candidates\":20}";
    }
    int maxCandidates = 12;
    double n = 0.0;
    if (ExtractNumber(paramsJson, "max_candidates", &n)) maxCandidates = std::max(1, std::min(20, static_cast<int>(n)));
    maxCandidates = std::min(maxCandidates, 20 - gCandidatesEvaluated);
    double minImprovement = 0.25;
    if (ExtractNumber(paramsJson, "min_improvement", &n)) minImprovement = std::max(0.25, n);

    const auto changeSets = GenerateChangeSets(primaryBlock, primaryDirection, secondaryBlock,
        secondaryDirection, maxCandidates);
    if (changeSets.empty()) {
        return "{\"ok\":false,\"error\":\"unsupported or empty semantic intervention block\"}";
    }
    ++gOptimizerRuns;
    gCandidatesEvaluated += static_cast<int>(changeSets.size());

    OptimizationTrial trial;
    trial.active = true;
    trial.source = source;
    trial.objective = objective;
    trial.plan_json = paramsJson;
    trial.min_improvement = minImprovement;
    trial.baseline_theta = LoadLiveTheta();
    trial.baseline_json = EvaluateThetaOnHistoryJson(RootDir(), trial.baseline_theta, 0, 30);
    trial.baseline = ParseEval(trial.baseline_json);
    if (!trial.baseline.ok || trial.baseline.n_episodes <= 0) {
        return "{\"ok\":false,\"error\":\"baseline replay unavailable\",\"baseline\":" +
            trial.baseline_json + "}";
    }

    int nextId = 1;
    for (const auto &changes : changeSets) {
        Candidate c;
        c.id = nextId++;
        c.theta = trial.baseline_theta;
        c.changes = changes;
        bool changed = true;
        double stepDistance = 0.0;
        for (const auto &change : changes) {
            double before = 0.0;
            ReadParam(c.theta, change.param, &before);
            if (!ApplyBoundedChange(&c.theta, change)) changed = false;
            double after = before;
            ReadParam(c.theta, change.param, &after);
            const ParamSpec *spec = FindParamSpec(change.param);
            if (std::fabs(after - before) < 1e-9) changed = false;
            if (spec != nullptr) stepDistance += std::fabs(after - before) / spec->step;
        }
        if (!changed) {
            c.rejection = "bounded_change_is_noop";
            trial.candidates.push_back(std::move(c));
            continue;
        }
        c.eval_json = EvaluateThetaOnHistoryJson(RootDir(), c.theta, 0, 30);
        c.metrics = ParseEval(c.eval_json);
        double objectiveBonus = 0.0;
        if (objective == "false_push") {
            objectiveBonus = 0.75 * static_cast<double>(trial.baseline.false_kept - c.metrics.false_kept);
        } else if (objective == "missed_leave") {
            objectiveBonus = 0.75 * static_cast<double>(trial.baseline.missed_leave - c.metrics.missed_leave) +
                0.50 * static_cast<double>(c.metrics.recovered_miss - trial.baseline.recovered_miss);
        } else if (objective == "lead") {
            objectiveBonus = 0.50 * static_cast<double>(c.metrics.lead_ok - trial.baseline.lead_ok) -
                0.25 * static_cast<double>((c.metrics.lead_late + c.metrics.lead_early) -
                    (trial.baseline.lead_late + trial.baseline.lead_early));
        }
        c.regularized_score = c.metrics.score + objectiveBonus - 0.15 * stepDistance -
            0.10 * std::max(0, static_cast<int>(changes.size()) - 1);
        c.eligible = CandidateEligible(trial.baseline, c.metrics, minImprovement, objective, &c.rejection);
        trial.candidates.push_back(std::move(c));
    }
    double best = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < trial.candidates.size(); ++i) {
        const auto &c = trial.candidates[i];
        if (c.eligible && c.regularized_score > best) {
            best = c.regularized_score;
            trial.best_index = static_cast<int>(i);
        }
    }
    std::lock_guard<std::mutex> lock(gOptimizerMutex);
    gOptimizerTrial = std::move(trial);
    return TrialJson(gOptimizerTrial);
}

}  // namespace

std::string AnalyzePersonalizationRulesAction(const std::string &paramsJson)
{
    int maxEpisodes = 30;
    double n = 0.0;
    if (ExtractNumber(paramsJson, "limit", &n)) maxEpisodes = std::max(1, std::min(200, static_cast<int>(n)));
    const auto episodes = LoadSignatures(maxEpisodes);
    if (episodes.empty()) {
        return "{\"ok\":false,\"error\":\"policy_history with obs_* is required\"}";
    }
    return CausesJson(episodes, AnalyzeCauses(episodes, LoadLiveTheta()));
}

std::string RunConstrainedThetaOptimizerAction(const std::string &paramsJson)
{
    return RunOptimizer(paramsJson, "agent_semantic_plan");
}

std::string RunRulePersonalizationAction(const std::string &paramsJson)
{
    int maxEpisodes = 30;
    double n = 0.0;
    if (ExtractNumber(paramsJson, "limit", &n)) maxEpisodes = std::max(1, std::min(200, static_cast<int>(n)));
    const auto episodes = LoadSignatures(maxEpisodes);
    const auto causes = AnalyzeCauses(episodes, LoadLiveTheta());
    if (causes.empty() || causes[0].direction == "hold" || causes[0].score < 0.35) {
        return "{\"ok\":true,\"no_op\":true,\"reason\":\"rule confidence insufficient or no error\",\"analysis\":" +
            CausesJson(episodes, causes) + "}";
    }
    std::ostringstream plan;
    plan << "{\"primary_block\":\"" << Esc(causes[0].block) << "\",\"primary_direction\":\""
         << Esc(causes[0].direction) << "\"";
    if (causes.size() > 1 && causes[1].direction != "hold" && causes[1].score >= 0.35) {
        plan << ",\"secondary_block\":\"" << Esc(causes[1].block) << "\",\"secondary_direction\":\""
             << Esc(causes[1].direction) << "\"";
    }
    plan << ",\"max_candidates\":12,\"min_improvement\":0.25}";
    return RunOptimizer(plan.str(), "rule_es_cro");
}

std::string GetOptimizationTrialAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gOptimizerMutex);
    if (!gOptimizerTrial.active) return "{\"ok\":true,\"trial_active\":false}";
    return TrialJson(gOptimizerTrial);
}

std::string CommitOptimizedThetaAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gOptimizerMutex);
    if (!gOptimizerTrial.active) return "{\"ok\":false,\"error\":\"no active optimizer trial\"}";
    if (gOptimizerTrial.best_index < 0) {
        return "{\"ok\":false,\"error\":\"no candidate passed commit guard\"}";
    }
    Candidate &best = gOptimizerTrial.candidates[gOptimizerTrial.best_index];
    if (!SameTunableTheta(LoadLiveTheta(), gOptimizerTrial.baseline_theta)) {
        return "{\"ok\":false,\"error\":\"live theta changed after trial; rerun optimizer\"}";
    }
    const EvalMetrics freshBaseline = ParseEval(
        EvaluateThetaOnHistoryJson(RootDir(), gOptimizerTrial.baseline_theta, 0, 30));
    const EvalMetrics freshCandidate = ParseEval(EvaluateThetaOnHistoryJson(RootDir(), best.theta, 0, 30));
    std::string rejection;
    if (!CandidateEligible(freshBaseline, freshCandidate, gOptimizerTrial.min_improvement,
            gOptimizerTrial.objective, &rejection)) {
        return "{\"ok\":false,\"error\":\"candidate no longer eligible\",\"reason\":\"" + Esc(rejection) + "\"}";
    }
    gOptimizerTrial.baseline = freshBaseline;
    best.metrics = freshCandidate;
    std::string err;
    if (!PersistTheta(best.theta, &err)) {
        return "{\"ok\":false,\"error\":\"" + Esc(err) + "\"}";
    }
    for (const auto &change : best.changes) {
        double oldValue = 0.0;
        double newValue = 0.0;
        ReadParam(gOptimizerTrial.baseline_theta, change.param, &oldValue);
        ReadParam(best.theta, change.param, &newValue);
        ProductStore::GetInstance().AppendParamChange(NowMs(), change.param, oldValue, newValue,
            "constrained_optimizer:" + gOptimizerTrial.source);
    }
    const std::string audit = ProductStore::GetInstance().AppendAudit(NowMs(),
        "constrained optimizer committed candidate " + std::to_string(best.id),
        "{\"source\":\"" + Esc(gOptimizerTrial.source) + "\",\"candidate_id\":" + std::to_string(best.id) +
            ",\"baseline\":" + MetricsJson(gOptimizerTrial.baseline) + ",\"candidate\":" + MetricsJson(best.metrics) + "}");
    const int committedId = best.id;
    const std::string source = gOptimizerTrial.source;
    gOptimizerTrial = OptimizationTrial {};
    return "{\"ok\":true,\"committed_candidate_id\":" + std::to_string(committedId) +
        ",\"source\":\"" + Esc(source) + "\",\"audit_id\":\"" + Esc(audit) + "\"}";
}

std::string DiscardOptimizationTrialAction(const std::string &)
{
    std::lock_guard<std::mutex> lock(gOptimizerMutex);
    const bool wasActive = gOptimizerTrial.active;
    gOptimizerTrial = OptimizationTrial {};
    return std::string("{\"ok\":true,\"discarded\":") + (wasActive ? "true" : "false") + "}";
}

std::string GetPersonalizationProfileAction(const std::string &paramsJson)
{
    int maxEpisodes = 100;
    double n = 0.0;
    if (ExtractNumber(paramsJson, "limit", &n)) maxEpisodes = std::max(1, std::min(500, static_cast<int>(n)));
    const auto episodes = LoadSignatures(maxEpisodes);
    int confirmed = 0;
    int falsePush = 0;
    int missed = 0;
    int baroAvailableConfirmed = 0;
    int lowerPlatformConfirmed = 0;
    int wifiActiveConfirmed = 0;
    int wifiActiveFalse = 0;
    for (const auto &ep : episodes) {
        if (ep.label == "CONFIRMED_LEAVE") {
            ++confirmed;
            if (ep.baro_available) ++baroAvailableConfirmed;
            if (ep.lower_platform) ++lowerPlatformConfirmed;
            if (ep.max_wifi >= 0.5) ++wifiActiveConfirmed;
        } else if (ep.label == "FALSE_PUSH") {
            ++falsePush;
            if (ep.max_wifi >= 0.5) ++wifiActiveFalse;
        } else if (ep.label == "MISSED_LEAVE") {
            ++missed;
        }
    }
    std::ifstream memoryIn(RootDir() + "/personalization_profile.jsonl");
    std::vector<std::string> memories;
    std::string line;
    while (std::getline(memoryIn, line)) {
        if (!line.empty() && line.front() == '{') memories.push_back(line);
    }
    if (memories.size() > 20) memories.erase(memories.begin(), memories.end() - 20);
    std::ostringstream out;
    out << "{\"ok\":true,\"derived_profile\":{\"n_episodes\":" << episodes.size()
        << ",\"confirmed\":" << confirmed << ",\"false_push\":" << falsePush << ",\"missed\":" << missed
        << ",\"baro_confirmed_coverage\":"
        << (confirmed > 0 ? static_cast<double>(baroAvailableConfirmed) / confirmed : 0.0)
        << ",\"lower_platform_confirmed_rate\":"
        << (confirmed > 0 ? static_cast<double>(lowerPlatformConfirmed) / confirmed : 0.0)
        << ",\"wifi_active_confirmed_rate\":"
        << (confirmed > 0 ? static_cast<double>(wifiActiveConfirmed) / confirmed : 0.0)
        << ",\"wifi_active_false_rate\":"
        << (falsePush > 0 ? static_cast<double>(wifiActiveFalse) / falsePush : 0.0)
        << "},\"validated_context_memory\":[";
    for (size_t i = 0; i < memories.size(); ++i) {
        if (i) out << ',';
        out << memories[i];
    }
    out << "],\"notes\":\"Derived fields are deterministic. Context memory is Agent-proposed but accepted only by support/conflict gates.\"}";
    return out.str();
}

std::string ProposeContextProfileUpdateAction(const std::string &paramsJson)
{
    std::string contextName;
    std::string signature;
    if (!ExtractString(paramsJson, "context_name", &contextName) || contextName.empty() ||
        !ExtractString(paramsJson, "context_signature", &signature) || signature.empty()) {
        return "{\"ok\":false,\"error\":\"context_name and deterministic context_signature required\"}";
    }
    double supportN = 0.0;
    double positiveN = 0.0;
    double contradictionN = 0.0;
    double confidence = 0.0;
    ExtractNumber(paramsJson, "support_count", &supportN);
    ExtractNumber(paramsJson, "positive_count", &positiveN);
    ExtractNumber(paramsJson, "contradiction_count", &contradictionN);
    ExtractNumber(paramsJson, "confidence", &confidence);
    if (supportN < 3.0 || positiveN < 1.0 || confidence < 0.65 || contradictionN > supportN * 0.5) {
        return "{\"ok\":true,\"accepted\":false,\"gate\":\"SHADOW_ONLY\","
               "\"reason\":\"requires support>=3, positive>=1, confidence>=0.65, contradictions<=50%\"}";
    }
    std::ofstream out(RootDir() + "/personalization_profile.jsonl", std::ios::out | std::ios::app);
    if (!out.is_open()) return "{\"ok\":false,\"error\":\"cannot open personalization_profile.jsonl\"}";
    out << "{\"updated_at_ms\":" << NowMs() << ",\"context_name\":\"" << Esc(contextName)
        << "\",\"context_signature\":\"" << Esc(signature) << "\",\"support_count\":"
        << static_cast<int>(supportN) << ",\"positive_count\":" << static_cast<int>(positiveN)
        << ",\"contradiction_count\":" << static_cast<int>(contradictionN) << ",\"confidence\":"
        << confidence << ",\"status\":\"VALIDATED_MEMORY\"}\n";
    return "{\"ok\":true,\"accepted\":true,\"gate\":\"VALIDATED_MEMORY\","
           "\"note\":\"profile memory does not bypass theta replay or realtime gates\"}";
}

std::string SubmitAgentAnalysisAction(const std::string &paramsJson)
{
    std::string context;
    std::string cause;
    std::string block;
    std::string direction;
    std::string supporting;
    std::string contradicting;
    if (!ExtractString(paramsJson, "context_name", &context) || context.empty() ||
        !ExtractString(paramsJson, "primary_cause", &cause) || cause.empty() ||
        !ExtractString(paramsJson, "supporting_evidence", &supporting) || supporting.empty() ||
        !ExtractString(paramsJson, "contradicting_evidence", &contradicting)) {
        return "{\"ok\":false,\"error\":\"context_name, primary_cause, supporting_evidence and contradicting_evidence required\"}";
    }
    ExtractString(paramsJson, "intervention_block", &block);
    ExtractString(paramsJson, "direction", &direction);
    bool abstain = false;
    ExtractBool(paramsJson, "abstain", &abstain);
    const bool templateComposition = block == "context_template" && direction == "compose";
    if (!abstain && !templateComposition && ParamsForBlock(block, direction).empty()) {
        return "{\"ok\":false,\"error\":\"non-abstain analysis requires context_template/compose or a supported legacy semantic block\"}";
    }
    double confidence = 0.0;
    ExtractNumber(paramsJson, "confidence", &confidence);
    if (confidence < 0.0 || confidence > 1.0) {
        return "{\"ok\":false,\"error\":\"confidence must be in [0,1]\"}";
    }
    const std::string auditId = ProductStore::GetInstance().AppendAudit(NowMs(),
        "structured_agent_analysis context=" + context + " cause=" + cause + (abstain ? " abstain" : ""),
        paramsJson);
    return "{\"ok\":true,\"validated\":true,\"audit_id\":\"" + Esc(auditId) + "\"}";
}

}  // namespace commute_sa
