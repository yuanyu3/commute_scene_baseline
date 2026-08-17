#include "commute_sa/theta_eval.h"

#include "commute_sa/baseline_runtime.h"
#include "commute_sa/leave_hsmm.h"
#include "commute_sa/product_store.h"
#include "commute_sa/theta.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace commute_sa {
namespace {

std::string Esc(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        if (c == '\n' || c == '\r') {
            out.push_back(' ');
            continue;
        }
        out.push_back(c);
    }
    return out;
}

bool ExtractNumber(const std::string &json, const char *key, double *out)
{
    if (out == nullptr || key == nullptr) {
        return false;
    }
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) {
        return false;
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
        ++i;
    }
    char *end = nullptr;
    const double v = std::strtod(json.c_str() + i, &end);
    if (end == json.c_str() + i) {
        return false;
    }
    *out = v;
    return true;
}

bool ExtractInt64(const std::string &json, const char *key, int64_t *out)
{
    double v = 0.0;
    if (!ExtractNumber(json, key, &v)) {
        return false;
    }
    *out = static_cast<int64_t>(v);
    return true;
}

bool ExtractString(const std::string &json, const char *key, std::string *out)
{
    if (out == nullptr || key == nullptr) {
        return false;
    }
    // Prefer "key": form so values like "type":"label" do not steal key "label".
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = 0;
    while (true) {
        pos = json.find(needle, pos);
        if (pos == std::string::npos) {
            return false;
        }
        size_t i = pos + needle.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
            ++i;
        }
        if (i < json.size() && json[i] == ':') {
            ++i;
            while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
                ++i;
            }
            if (i >= json.size() || json[i] != '"') {
                return false;
            }
            ++i;
            std::string val;
            while (i < json.size() && json[i] != '"') {
                if (json[i] == '\\' && i + 1 < json.size()) {
                    val.push_back(json[i + 1]);
                    i += 2;
                    continue;
                }
                val.push_back(json[i++]);
            }
            *out = val;
            return true;
        }
        pos += needle.size();
    }
}

bool ExtractBool(const std::string &json, const char *key, bool *out)
{
    if (out == nullptr || key == nullptr) {
        return false;
    }
    const std::string needle = std::string("\"") + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    size_t i = json.find(':', pos + needle.size());
    if (i == std::string::npos) {
        return false;
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) {
        ++i;
    }
    if (json.compare(i, 4, "true") == 0) {
        *out = true;
        return true;
    }
    if (json.compare(i, 5, "false") == 0) {
        *out = false;
        return true;
    }
    return false;
}

struct HsmmTick {
    int64_t t_ms = 0;
    LeaveObservation obs;
    double lead_s = -1.0;
};

struct HsmmEpisode {
    std::string side;
    std::string label;
    int64_t outcome_ms = 0;
    std::vector<HsmmTick> ticks;
};

bool ArmDelayOk(int64_t walkStartedMs, int64_t tMs, double armDelayS)
{
    if (walkStartedMs <= 0) {
        return true;
    }
    return (tMs - walkStartedMs) >= static_cast<int64_t>(armDelayS * 1000.0);
}

bool WouldHsmmPush(const LeaveHsmmResult &result, const LeaveObservation &obs, double enterLeave, int64_t walkStartedMs,
    int64_t tMs, double armDelayS)
{
    return result.LeavingProbability() >= enterLeave && !obs.outside && !obs.approaching && !obs.attached &&
        (obs.inside || obs.near) && ArmDelayOk(walkStartedMs, tMs, armDelayS);
}

bool EpisodeHasBaroLowerPlatform(const HsmmEpisode &ep)
{
    for (const auto &tick : ep.ticks) {
        if (tick.obs.baro_lower_platform >= 0.5) {
            return true;
        }
    }
    return false;
}

std::string ScoreHsmmReplay(const std::vector<HsmmEpisode> &episodes, const Theta &theta)
{
    int n = 0;
    int nFalse = 0;
    int nConfirmed = 0;
    int nMissedLabel = 0;
    int falseAvoided = 0;
    int falseKept = 0;
    int softFalseAvoided = 0;
    int softFalseKept = 0;
    int confirmedKept = 0;
    int missed = 0;
    int recovered = 0;
    int leadOk = 0;
    int leadLate = 0;
    int leadEarly = 0;
    double leadAbsErrSum = 0.0;
    int leadErrN = 0;

    const LeaveHsmmConfig cfg = HsmmConfigFromTheta(theta);
    for (const auto &ep : episodes) {
        if (!FocusAllowsHome(theta.focus_side) && ep.side == "home") {
            continue;
        }
        if (!FocusAllowsCompany(theta.focus_side) && ep.side == "company") {
            continue;
        }
        ++n;
        LeaveHsmm hsmm;
        bool wouldPush = false;
        double leadAtPush = -1.0;
        int64_t walkStartedMs = 0;
        for (const auto &tick : ep.ticks) {
            if (tick.obs.walking >= 0.5) {
                if (walkStartedMs <= 0) {
                    walkStartedMs = tick.t_ms;
                }
            } else {
                walkStartedMs = 0;
            }
            const LeaveHsmmResult result = hsmm.Step(tick.obs, tick.t_ms, cfg);
            if (!wouldPush &&
                WouldHsmmPush(result, tick.obs, theta.enter_leave, walkStartedMs, tick.t_ms, theta.arm_delay_s)) {
                wouldPush = true;
                leadAtPush = (ep.outcome_ms > tick.t_ms)
                    ? static_cast<double>(ep.outcome_ms - tick.t_ms) / 1000.0
                    : 0.0;
            }
        }
        if (ep.label == "FALSE_PUSH") {
            ++nFalse;
            const bool softOk = EpisodeHasBaroLowerPlatform(ep);
            if (softOk) {
                // Lobby/1F (baro_lower_platform): same positive target as outdoor confirm —
                // must keep push; failing to push counts as missed_leave.
                if (wouldPush) {
                    ++softFalseKept;
                } else {
                    ++softFalseAvoided;
                    ++missed;
                }
            } else if (wouldPush) {
                ++falseKept;
            } else {
                ++falseAvoided;
            }
        } else if (ep.label == "CONFIRMED_LEAVE") {
            ++nConfirmed;
            if (wouldPush) {
                ++confirmedKept;
                if (leadAtPush >= 0.0) {
                    const double mid = 0.5 * (theta.lead_min_s + theta.lead_max_s);
                    leadAbsErrSum += std::fabs(leadAtPush - mid);
                    ++leadErrN;
                    if (leadAtPush < theta.lead_min_s) {
                        ++leadLate;
                    } else if (leadAtPush > theta.lead_max_s) {
                        ++leadEarly;
                    } else {
                        ++leadOk;
                    }
                }
            } else {
                ++missed;
            }
        } else if (ep.label == "MISSED_LEAVE") {
            ++nMissedLabel;
            if (wouldPush) {
                ++recovered;
            } else {
                ++missed;
            }
        }
    }

    // soft_false_* = FALSE_PUSH with baro_lower_platform: scored as positives (same as confirmed).
    // soft_false_avoided already folded into missed. Hard FALSE_PUSH only in false_*.
    const double scoreValue = 2.0 * static_cast<double>(falseAvoided) - 2.0 * static_cast<double>(falseKept) +
        1.5 * static_cast<double>(confirmedKept + softFalseKept) - 3.0 * static_cast<double>(missed) +
        1.5 * static_cast<double>(recovered) + 1.0 * static_cast<double>(leadOk) -
        0.5 * static_cast<double>(leadLate) - 0.5 * static_cast<double>(leadEarly);
    const double leadMae = (leadErrN > 0) ? (leadAbsErrSum / static_cast<double>(leadErrN)) : -1.0;

    std::ostringstream oss;
    oss << "{\"ok\":true,\"method\":\"hsmm_window_replay\""
        << ",\"focus_side\":\"" << Esc(theta.focus_side) << "\""
        << ",\"notes\":\"Replay LeaveHsmm on stored leave-window observations. Evaluates w_*, enter_leave, and "
           "arm_delay_s. Counterfactual lead_s = t_star - first eligible push tick. "
           "Product bans (OUTSIDE/approaching/attach) stay in C++. Filtered by focus_side. "
           "FALSE_PUSH with obs_baro_lower_platform counted as soft_false_* and scored as positives "
           "(kept=+1.5 like confirmed; avoided folds into missed_leave=-3). "
           "Hard FALSE_PUSH (no baro_lower_platform) stays in false_kept/false_avoided.\""
        << ",\"n_episodes\":" << n << ",\"n_false_push\":" << nFalse << ",\"n_confirmed_leave\":" << nConfirmed
        << ",\"n_missed_leave_label\":" << nMissedLabel
        << ",\"false_avoided\":" << falseAvoided << ",\"false_kept\":" << falseKept
        << ",\"soft_false_avoided\":" << softFalseAvoided << ",\"soft_false_kept\":" << softFalseKept
        << ",\"confirmed_kept\":" << confirmedKept << ",\"missed_leave\":" << missed
        << ",\"recovered_miss\":" << recovered << ",\"unscored\":0"
        << ",\"lead_ok\":" << leadOk << ",\"lead_late\":" << leadLate << ",\"lead_early\":" << leadEarly
        << ",\"lead_mae_to_mid_s\":" << leadMae << ",\"score\":" << scoreValue
        << ",\"theta\":{\"enter_leave\":" << theta.enter_leave << ",\"lead_min_s\":" << theta.lead_min_s
        << ",\"lead_max_s\":" << theta.lead_max_s << ",\"w_walk\":" << theta.w_walk << ",\"w_pdr\":" << theta.w_pdr
        << ",\"w_geo\":" << theta.w_geo << ",\"w_wifi\":" << theta.w_wifi << ",\"w_cell\":" << theta.w_cell
        << ",\"w_ble\":" << theta.w_ble << ",\"w_time\":" << theta.w_time << ",\"w_baro\":" << theta.w_baro
        << ",\"arm_delay_s\":" << theta.arm_delay_s << "}"
        << ",\"better_guidance\":\"Prefer higher score. If missed_leave rises, revert the last change. "
           "Positives: CONFIRMED_LEAVE and soft_false (FALSE_PUSH with baro_lower_platform / lobby-1F). "
           "soft_false_kept is rewarded; soft_false_avoided counts as missed. "
           "Hard false_kept (no baro_lower_platform) is the suppress target. "
           "Diagnose obs_* before choosing enter_leave vs a channel w_*.\"}";
    return oss.str();
}

bool LoadHsmmEpisodes(const std::string &rootDir, int64_t sinceMs, int maxEpisodes, std::vector<HsmmEpisode> *out)
{
    const std::string path = rootDir + "/policy_history.jsonl";
    std::ifstream in(path);
    if (!in.is_open() || out == nullptr) {
        return false;
    }
    std::map<std::string, HsmmEpisode> grouped;
    std::string line;
    int nObs = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find("\"obs_pdr_outbound\"") == std::string::npos) {
            continue;
        }
        HsmmTick tick;
        if (!ExtractInt64(line, "t_ms", &tick.t_ms)) {
            continue;
        }
        ExtractNumber(line, "obs_walking", &tick.obs.walking);
        ExtractNumber(line, "obs_pdr_outbound", &tick.obs.pdr_outbound);
        ExtractNumber(line, "obs_geo_outbound", &tick.obs.geo_outbound);
        ExtractNumber(line, "obs_wifi_detach", &tick.obs.wifi_detach);
        ExtractNumber(line, "obs_cell_detach", &tick.obs.cell_detach);
        ExtractNumber(line, "obs_ble_detach", &tick.obs.ble_detach);
        ExtractNumber(line, "obs_time_prior", &tick.obs.time_prior);
        ExtractNumber(line, "obs_baro_descending", &tick.obs.baro_descending);
        ExtractNumber(line, "obs_baro_lower_platform", &tick.obs.baro_lower_platform);
        ExtractBool(line, "obs_baro_available", &tick.obs.baro_available);
        ExtractBool(line, "obs_relation_known", &tick.obs.relation_known);
        ExtractBool(line, "obs_inside", &tick.obs.inside);
        ExtractBool(line, "obs_near", &tick.obs.near);
        ExtractBool(line, "obs_outside", &tick.obs.outside);
        ExtractBool(line, "obs_approaching", &tick.obs.approaching);
        ExtractBool(line, "obs_attached", &tick.obs.attached);
        ExtractNumber(line, "lead_s", &tick.lead_s);

        std::string side = "company";
        ExtractString(line, "side", &side);
        std::string label;
        ExtractString(line, "label", &label);
        int64_t outcomeMs = tick.t_ms;
        ExtractInt64(line, "outcome_t_ms", &outcomeMs);
        if (sinceMs > 0 && outcomeMs > 0 && outcomeMs < sinceMs) {
            continue;
        }
        const std::string key = side + ":" + std::to_string(outcomeMs) + ":" + label;
        auto &ep = grouped[key];
        ep.side = side;
        ep.label = label;
        ep.outcome_ms = outcomeMs;
        ep.ticks.push_back(tick);
        ++nObs;
    }
    if (nObs == 0) {
        return false;
    }
    for (auto &entry : grouped) {
        std::sort(entry.second.ticks.begin(), entry.second.ticks.end(),
            [](const HsmmTick &a, const HsmmTick &b) { return a.t_ms < b.t_ms; });
        out->push_back(std::move(entry.second));
    }
    std::sort(out->begin(), out->end(),
        [](const HsmmEpisode &a, const HsmmEpisode &b) { return a.outcome_ms < b.outcome_ms; });
    if (maxEpisodes > 0 && static_cast<int>(out->size()) > maxEpisodes) {
        out->erase(out->begin(), out->begin() + static_cast<std::ptrdiff_t>(out->size() - maxEpisodes));
    }
    return !out->empty();
}

struct Episode {
    int64_t t_push_ms = 0;
    std::string intent;
    std::string label;
    double score_home = 0.0;
    double score_company = 0.0;
    bool has_score = false;
    double lead_s = -1.0;
    bool has_lead = false;
};

std::mutex gTrialMu;
bool gTrialActive = false;
Theta gTrialSnapshot {};

Theta LoadLiveTheta()
{
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        return BaselineRuntime::GetInstance().Engine()->GetTheta();
    }
    Theta t = DefaultTheta();
    const std::string root = ProductStore::GetInstance().RootDir().empty()
        ? std::string("/data/service/el1/public/commuteagentservice")
        : ProductStore::GetInstance().RootDir();
    LoadThetaFromFile(root + "/theta.json", &t, nullptr);
    return t;
}

bool PersistTheta(const Theta &t, std::string *err)
{
    if (BaselineRuntime::GetInstance().Enabled() && BaselineRuntime::GetInstance().Engine() != nullptr) {
        BaselineRuntime::GetInstance().Engine()->SetTheta(t);
    }
    if (!ProductStore::GetInstance().SaveTheta(t)) {
        if (err) {
            *err = "SaveTheta failed";
        }
        return false;
    }
    return true;
}

}  // namespace

std::string EvaluateThetaOnHistoryJson(const std::string &rootDir, const Theta &theta, int64_t sinceMs,
    int maxEpisodes)
{
    std::vector<HsmmEpisode> hsmmEpisodes;
    if (LoadHsmmEpisodes(rootDir, sinceMs, maxEpisodes, &hsmmEpisodes)) {
        return ScoreHsmmReplay(hsmmEpisodes, theta);
    }

    const std::string path = rootDir + "/leave_episodes.jsonl";
    std::ifstream in(path);
    if (!in.is_open()) {
        return "{\"ok\":false,\"error\":\"leave_episodes.jsonl missing\",\"path\":\"" + Esc(path) + "\"}";
    }

    std::vector<Episode> episodes;
    Episode cur;
    bool havePush = false;
    int nLines = 0;
    int nPushSeen = 0;
    int nLabelSeen = 0;
    std::string line;
    while (std::getline(in, line)) {
        ++nLines;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find("\"type\":\"push\"") != std::string::npos ||
            line.find("\"type\": \"push\"") != std::string::npos) {
            ++nPushSeen;
            cur = Episode {};
            ExtractInt64(line, "t_push_ms", &cur.t_push_ms);
            ExtractString(line, "intent", &cur.intent);
            if (ExtractNumber(line, "score_home", &cur.score_home)) {
                cur.has_score = true;
            }
            double sc = 0.0;
            if (ExtractNumber(line, "score_company", &sc)) {
                cur.score_company = sc;
                cur.has_score = true;
            }
            havePush = cur.t_push_ms > 0;
            continue;
        }
        if (!havePush) {
            continue;
        }
        if (line.find("\"type\":\"label\"") == std::string::npos &&
            line.find("\"type\": \"label\"") == std::string::npos) {
            continue;
        }
        ++nLabelSeen;
        int64_t tPush = 0;
        ExtractInt64(line, "t_push_ms", &tPush);
        if (tPush > 0 && tPush != cur.t_push_ms) {
            cur.t_push_ms = tPush;
        }
        if (sinceMs > 0 && cur.t_push_ms > 0 && cur.t_push_ms < sinceMs) {
            havePush = false;
            continue;
        }
        ExtractString(line, "label", &cur.label);
        double lead = 0.0;
        if (ExtractNumber(line, "lead_s", &lead)) {
            cur.lead_s = lead;
            cur.has_lead = true;
        }
        if (!cur.label.empty()) {
            episodes.push_back(cur);
        }
        havePush = false;
    }

    if (maxEpisodes > 0 && static_cast<int>(episodes.size()) > maxEpisodes) {
        episodes.erase(episodes.begin(),
            episodes.begin() + static_cast<std::ptrdiff_t>(episodes.size() - maxEpisodes));
    }

    int n = 0;
    int nFalse = 0;
    int nConfirmed = 0;
    int falseAvoided = 0;
    int falseKept = 0;
    int confirmedKept = 0;
    int missed = 0;
    int unscored = 0;
    int leadOk = 0;
    int leadLate = 0;
    int leadEarly = 0;
    double leadAbsErrSum = 0.0;
    int leadErrN = 0;

    for (const auto &ep : episodes) {
        // Respect focus_side: company-only training ignores home departure episodes.
        if (!FocusAllowsHome(theta.focus_side) && ep.intent == "DEPARTURE_NOTIFICATION") {
            continue;
        }
        if (!FocusAllowsCompany(theta.focus_side) && ep.intent == "LEAVE_COMPANY_NOTIFICATION") {
            continue;
        }
        ++n;
        const bool leaveCompany = (ep.intent == "LEAVE_COMPANY_NOTIFICATION");
        const double score = leaveCompany ? ep.score_company : ep.score_home;
        const bool wouldPush = ep.has_score && (score >= theta.enter_leave);

        if (ep.label == "FALSE_PUSH") {
            ++nFalse;
            if (!ep.has_score) {
                ++unscored;
            } else if (wouldPush) {
                ++falseKept;
            } else {
                ++falseAvoided;
            }
        } else if (ep.label == "CONFIRMED_LEAVE") {
            ++nConfirmed;
            if (!ep.has_score) {
                ++unscored;
            } else if (wouldPush) {
                ++confirmedKept;
            } else {
                ++missed;
            }
            if (ep.has_lead) {
                const double mid = 0.5 * (theta.lead_min_s + theta.lead_max_s);
                leadAbsErrSum += std::fabs(ep.lead_s - mid);
                ++leadErrN;
                if (ep.lead_s < theta.lead_min_s) {
                    ++leadLate;
                } else if (ep.lead_s > theta.lead_max_s) {
                    ++leadEarly;
                } else {
                    ++leadOk;
                }
            }
        }
    }

    // Higher is better. Missed leave hurts most; keeping false pushes also hurts.
    const double scoreValue = 2.0 * static_cast<double>(falseAvoided) - 2.0 * static_cast<double>(falseKept) +
        1.5 * static_cast<double>(confirmedKept) - 3.0 * static_cast<double>(missed) +
        1.0 * static_cast<double>(leadOk) - 0.5 * static_cast<double>(leadLate) -
        0.5 * static_cast<double>(leadEarly);

    const double leadMae = (leadErrN > 0) ? (leadAbsErrSum / static_cast<double>(leadErrN)) : -1.0;

    std::ostringstream oss;
    oss << "{\"ok\":true,\"method\":\"counterfactual_push_score\""
        << ",\"focus_side\":\"" << Esc(theta.focus_side) << "\""
        << ",\"notes\":\"Replay recorded push P(LEAVING) against candidate enter_leave; lead vs lead_min/max. "
           "Not full sensor-sequence HSMM replay; cannot evaluate w_* or hsmm duration changes. Episodes filtered by focus_side.\""
        << ",\"path\":\"" << Esc(path) << "\",\"n_lines\":" << nLines << ",\"n_push_seen\":" << nPushSeen
        << ",\"n_label_seen\":" << nLabelSeen
        << ",\"n_episodes\":" << n << ",\"n_false_push\":" << nFalse << ",\"n_confirmed_leave\":" << nConfirmed
        << ",\"false_avoided\":" << falseAvoided << ",\"false_kept\":" << falseKept
        << ",\"confirmed_kept\":" << confirmedKept << ",\"missed_leave\":" << missed
        << ",\"unscored\":" << unscored << ",\"lead_ok\":" << leadOk << ",\"lead_late\":" << leadLate
        << ",\"lead_early\":" << leadEarly << ",\"lead_mae_to_mid_s\":" << leadMae << ",\"score\":" << scoreValue
        << ",\"theta\":{\"enter_leave\":" << theta.enter_leave << ",\"lead_min_s\":" << theta.lead_min_s
        << ",\"lead_max_s\":" << theta.lead_max_s << ",\"min_evidence\":" << theta.min_evidence
        << ",\"arm_delay_s\":" << theta.arm_delay_s << "}"
        << ",\"better_guidance\":\"Prefer higher score. If missed_leave rises, revert. "
           "This fallback cannot evaluate w_*; store obs_* in policy_history for full HSMM replay. "
           "No fixed recipe for false_kept—use evidence then trial+eval.\"}";
    return oss.str();
}

bool BeginThetaTrial(std::string *err)
{
    std::lock_guard<std::mutex> lock(gTrialMu);
    if (gTrialActive) {
        if (err) {
            *err = "trial already active; commit or revert first";
        }
        return false;
    }
    gTrialSnapshot = LoadLiveTheta();
    gTrialActive = true;
    return true;
}

bool RevertThetaTrial(std::string *err)
{
    std::lock_guard<std::mutex> lock(gTrialMu);
    if (!gTrialActive) {
        if (err) {
            *err = "no active trial";
        }
        return false;
    }
    if (!PersistTheta(gTrialSnapshot, err)) {
        return false;
    }
    gTrialActive = false;
    return true;
}

bool CommitThetaTrial(std::string *err)
{
    std::lock_guard<std::mutex> lock(gTrialMu);
    if (!gTrialActive) {
        if (err) {
            *err = "no active trial";
        }
        return false;
    }
    gTrialActive = false;
    return true;
}

bool HasActiveThetaTrial()
{
    std::lock_guard<std::mutex> lock(gTrialMu);
    return gTrialActive;
}

}  // namespace commute_sa
