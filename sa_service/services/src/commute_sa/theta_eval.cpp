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
    std::string episode_id;
    std::string side;
    std::string label;
    int64_t outcome_ms = 0;
    int64_t abort_ms = 0;
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

std::string ScoreHsmmReplay(const std::vector<HsmmEpisode> &episodes, const Theta &theta,
    const ObservationAdapter &adapter = {}, bool includePrefixTrace = false, int64_t cutoffMs = 0,
    std::vector<ReplayEpisodeSummary> *summaries = nullptr)
{
    if (summaries) summaries->clear();
    int n = 0;
    int nFalse = 0;
    int nConfirmed = 0;
    int nMissedLabel = 0;
    int nTrueNegative = 0;
    int nAborted = 0;
    int nUnscored = 0;
    int falseAvoided = 0;
    int falseKept = 0;
    int softFalseAvoided = 0;
    int softFalseKept = 0;
    int confirmedKept = 0;
    int missed = 0;
    int recovered = 0;
    int abortedIntentRecognized = 0;
    int abortedCancelRecognized = 0;
    int abortedVisiblePush = 0;
    int leadOk = 0;
    int leadLate = 0;
    int leadEarly = 0;
    double leadAbsErrSum = 0.0;
    double leadUtility = 0.0;
    double lateSeconds = 0.0;
    double leadSum = 0.0;
    int leadErrN = 0;
    std::vector<std::string> episodeResults;

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
        int64_t pushAtMs = 0;
        double leadAtPush = -1.0;
        int64_t walkStartedMs = 0;
        bool episodeStart = true;
        std::ostringstream trace;
        bool firstTrace = true;
        int64_t firstReadyMs = 0, firstCompleteMs = 0, firstThresholdMs = 0, firstCancelMs = 0;
        int negativeTicks = 0, productBlockedTicks = 0, armBlockedTicks = 0;
        for (const auto &tick : ep.ticks) {
            if (cutoffMs > 0 && tick.t_ms > cutoffMs) break;
            LeaveObservation observation = tick.obs;
            // Always rebuild template evidence from atomic observations. A
            // historical row may have been recorded under another template.
            observation.sequence_available = false;
            observation.sequence_progress = 0.0;
            observation.sequence_complete = 0.0;
            observation.sequence_ready = -1.0;
            observation.negative_pattern_match = 0.0;
            observation.cancel_sequence_match = 0.0;
            observation.sequence_reliability = 0.0;
            if (adapter) {
                adapter(observation, episodeStart);
            }
            episodeStart = false;
            if (observation.walking >= 0.5) {
                if (walkStartedMs <= 0) {
                    walkStartedMs = tick.t_ms;
                }
            } else {
                walkStartedMs = 0;
            }
            const LeaveHsmmResult result = hsmm.Step(observation, tick.t_ms, cfg);
            if (!firstReadyMs && observation.sequence_ready >= 0.5) firstReadyMs = tick.t_ms;
            if (!firstCompleteMs && observation.sequence_complete >= 0.5) firstCompleteMs = tick.t_ms;
            if (observation.negative_pattern_match >= 0.5) ++negativeTicks;
            if (!firstCancelMs && observation.cancel_sequence_match >= 0.5) firstCancelMs = tick.t_ms;
            if (result.LeavingProbability() >= theta.enter_leave) {
                if (!firstThresholdMs) firstThresholdMs = tick.t_ms;
                if (observation.outside || observation.approaching || observation.attached ||
                    (!observation.inside && !observation.near)) ++productBlockedTicks;
                if (walkStartedMs > 0 && tick.t_ms - walkStartedMs < theta.arm_delay_s * 1000) ++armBlockedTicks;
            }
            if (includePrefixTrace) {
                if (!firstTrace) trace << ',';
                firstTrace = false;
                trace << "{\"t_ms\":" << tick.t_ms << ",\"probabilities\":[";
                for (size_t i = 0; i < result.probability.size(); ++i) {
                    if (i) trace << ',';
                    trace << result.probability[i];
                }
                trace << "],\"sequence_progress\":" << observation.sequence_progress
                    << ",\"sequence_complete\":" << observation.sequence_complete
                    << ",\"sequence_ready\":" << observation.sequence_ready
                    << ",\"negative_match\":" << observation.negative_pattern_match
                    << ",\"cancel_match\":" << observation.cancel_sequence_match
                    << ",\"threshold_pass\":" << (result.LeavingProbability() >= theta.enter_leave ? "true" : "false")
                    << ",\"product_gate_pass\":" << (!observation.outside && !observation.approaching &&
                        !observation.attached && (observation.inside || observation.near) ? "true" : "false")
                    << ",\"arm_gate_pass\":" << (walkStartedMs <= 0 ||
                        tick.t_ms - walkStartedMs >= theta.arm_delay_s * 1000 ? "true" : "false")
                    << ",\"eligible_push\":" << (WouldHsmmPush(result, observation, theta.enter_leave,
                        walkStartedMs, tick.t_ms, theta.arm_delay_s) ? "true" : "false") << '}';
            }
            if (!wouldPush &&
                WouldHsmmPush(result, observation, theta.enter_leave, walkStartedMs, tick.t_ms, theta.arm_delay_s)) {
                wouldPush = true;
                pushAtMs = tick.t_ms;
                leadAtPush = (ep.outcome_ms > tick.t_ms)
                    ? static_cast<double>(ep.outcome_ms - tick.t_ms) / 1000.0
                    : 0.0;
            }
        }
        const bool isAborted = ep.label == "ABORTED_LEAVE";
        const bool isSoft = ep.label == "FALSE_PUSH" && EpisodeHasBaroLowerPlatform(ep);
        if (summaries) {
            summaries->push_back({ep.side + ":" + std::to_string(ep.outcome_ms) + ":" + ep.label + ":" + ep.episode_id,
                isSoft || ep.label == "CONFIRMED_LEAVE" || ep.label == "MISSED_LEAVE",
                !isSoft && (ep.label == "FALSE_PUSH" || ep.label == "TRUE_NEGATIVE"),
                wouldPush, pushAtMs, leadAtPush});
        }
        // Outcome labels/time are used only for scoring, never by the prefix filter.
        if (wouldPush && (isSoft || ep.label == "CONFIRMED_LEAVE" || ep.label == "MISSED_LEAVE")) {
            const double late = std::max(0.0, theta.lead_min_s - leadAtPush);
            const double early = std::max(0.0, leadAtPush - theta.lead_max_s);
            leadUtility += 1.0 - (late + early) / 60.0;
            lateSeconds += late;
            leadSum += leadAtPush;
            leadAbsErrSum += std::fabs(leadAtPush - 0.5 * (theta.lead_min_s + theta.lead_max_s));
            ++leadErrN;
            if (late > 0) ++leadLate;
            else if (early > 0) ++leadEarly;
            else ++leadOk;
        }
        if (isAborted) {
            ++nAborted;
            const int64_t branchMs = ep.abort_ms > 0 ? ep.abort_ms : ep.outcome_ms;
            if (firstThresholdMs > 0 && (branchMs <= 0 || firstThresholdMs <= branchMs)) {
                ++abortedIntentRecognized;
            }
            if (firstCancelMs > 0 && (branchMs <= 0 || firstCancelMs >= branchMs)) {
                ++abortedCancelRecognized;
            }
            if (wouldPush) ++abortedVisiblePush;
        } else if (ep.label == "FALSE_PUSH" || ep.label == "TRUE_NEGATIVE") {
            if (ep.label == "TRUE_NEGATIVE") {
                ++nTrueNegative;
            }
            ++nFalse;
            if (isSoft) {
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
        } else {
            ++nUnscored;
        }
        std::ostringstream episodeRow;
        episodeRow << "{\"episode_id\":\"" << Esc(ep.episode_id)
            << "\",\"outcome_t_ms\":" << ep.outcome_ms << ",\"side\":\"" << Esc(ep.side)
            << "\",\"label\":\"" << Esc(ep.label) << "\",\"soft_lower_platform\":"
            << (isSoft ? "true" : "false") << ",\"would_push\":" << (wouldPush ? "true" : "false")
            << ",\"push_t_ms\":" << pushAtMs << ",\"lead_s\":" << leadAtPush
            << ",\"first_ready_t_ms\":" << firstReadyMs
            << ",\"first_complete_t_ms\":" << firstCompleteMs
            << ",\"first_threshold_t_ms\":" << firstThresholdMs
            << ",\"first_cancel_t_ms\":" << firstCancelMs
            << ",\"negative_match_ticks\":" << negativeTicks
            << ",\"threshold_product_blocked_ticks\":" << productBlockedTicks
            << ",\"threshold_arm_blocked_ticks\":" << armBlockedTicks;
        if (includePrefixTrace) episodeRow << ",\"prefix_trace\":[" << trace.str() << ']';
        episodeRow << '}';
        episodeResults.push_back(episodeRow.str());
    }

    // soft_false_* = FALSE_PUSH with baro_lower_platform: scored as positives (same as confirmed).
    // soft_false_avoided already folded into missed. Hard FALSE_PUSH only in false_*.
    const double scoreValue = 2.0 * static_cast<double>(falseAvoided) - 2.0 * static_cast<double>(falseKept) +
        1.5 * static_cast<double>(confirmedKept + softFalseKept) - 3.0 * static_cast<double>(missed) +
        1.5 * static_cast<double>(recovered) + 0.75 * static_cast<double>(abortedIntentRecognized) +
        1.25 * static_cast<double>(abortedCancelRecognized) + leadUtility;
    const double leadMae = (leadErrN > 0) ? (leadAbsErrSum / static_cast<double>(leadErrN)) : -1.0;

    std::ostringstream oss;
    oss << "{\"ok\":true,\"method\":\"hsmm_window_replay\""
        << ",\"score_version\":3,\"lead_utility\":" << leadUtility
        << ",\"late_seconds\":" << lateSeconds
        << ",\"mean_lead_s\":" << (leadErrN ? leadSum / leadErrN : -1.0)
        << ",\"prefix_cutoff_ms\":" << cutoffMs
        << ",\"partial_replay\":" << (cutoffMs > 0 ? "true" : "false")
        << ",\"focus_side\":\"" << Esc(theta.focus_side) << "\""
        << ",\"notes\":\"Replay LeaveHsmm on stored leave-window observations. Evaluates w_*, enter_leave, and "
           "arm_delay_s. Counterfactual lead_s = t_star - first eligible push tick. "
           "Product bans (OUTSIDE/approaching/attach) stay in C++. Filtered by focus_side. "
           "FALSE_PUSH with obs_baro_lower_platform counted as soft_false_* and scored as positives "
           "(kept=+1.5 like confirmed; avoided folds into missed_leave=-3). "
           "ABORTED_LEAVE rewards recognizing intent before abort_t_ms and matching a cancel sequence after it; "
           "it is not a hard negative. Hard FALSE_PUSH and explicit TRUE_NEGATIVE stay in false_kept/false_avoided.\""
        << ",\"n_episodes\":" << n << ",\"n_false_push\":" << nFalse << ",\"n_confirmed_leave\":" << nConfirmed
        << ",\"n_missed_leave_label\":" << nMissedLabel
        << ",\"n_true_negative\":" << nTrueNegative
        << ",\"n_aborted_leave\":" << nAborted
        << ",\"false_avoided\":" << falseAvoided << ",\"false_kept\":" << falseKept
        << ",\"soft_false_avoided\":" << softFalseAvoided << ",\"soft_false_kept\":" << softFalseKept
        << ",\"confirmed_kept\":" << confirmedKept << ",\"missed_leave\":" << missed
        << ",\"recovered_miss\":" << recovered << ",\"unscored\":" << nUnscored
        << ",\"aborted_intent_recognized\":" << abortedIntentRecognized
        << ",\"aborted_cancel_recognized\":" << abortedCancelRecognized
        << ",\"aborted_visible_push\":" << abortedVisiblePush
        << ",\"lead_ok\":" << leadOk << ",\"lead_late\":" << leadLate << ",\"lead_early\":" << leadEarly
        << ",\"lead_mae_to_mid_s\":" << leadMae << ",\"score\":" << scoreValue
        << ",\"theta\":{\"enter_leave\":" << theta.enter_leave << ",\"lead_min_s\":" << theta.lead_min_s
        << ",\"lead_max_s\":" << theta.lead_max_s << ",\"w_walk\":" << theta.w_walk << ",\"w_pdr\":" << theta.w_pdr
        << ",\"w_geo\":" << theta.w_geo << ",\"w_wifi\":" << theta.w_wifi << ",\"w_cell\":" << theta.w_cell
        << ",\"w_ble\":" << theta.w_ble << ",\"w_time\":" << theta.w_time << ",\"w_baro\":" << theta.w_baro
        << ",\"arm_delay_s\":" << theta.arm_delay_s << "}"
        << ",\"episode_results\":[";
    for (size_t i = 0; i < episodeResults.size(); ++i) {
        if (i) oss << ',';
        oss << episodeResults[i];
    }
    oss << ']'
        << ",\"better_guidance\":\"Prefer higher score. If missed_leave rises, revert the last change. "
           "Positives: CONFIRMED_LEAVE and soft_false (FALSE_PUSH with baro_lower_platform / lobby-1F). "
           "soft_false_kept is rewarded; soft_false_avoided counts as missed. "
           "Hard false_kept (no baro_lower_platform) is the suppress target. "
           "ABORTED_LEAVE is reported separately from false pushes. "
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
    struct Interpretation { std::string type; int64_t abort_ms = 0; };
    std::map<std::string, Interpretation> interpretations;
    {
        std::ifstream labels(rootDir + "/episode_interpretations.jsonl");
        std::string labelLine;
        while (std::getline(labels, labelLine)) {
            std::string side;
            std::string episodeId;
            std::string type;
            int64_t outcomeMs = 0;
            int64_t abortMs = 0;
            if (!ExtractString(labelLine, "side", &side) ||
                !ExtractString(labelLine, "episode_id", &episodeId) ||
                !ExtractString(labelLine, "episode_type", &type) || type != "ABORTED_LEAVE" ||
                !ExtractInt64(labelLine, "outcome_t_ms", &outcomeMs) ||
                !ExtractInt64(labelLine, "abort_t_ms", &abortMs)) continue;
            interpretations[side + ":" + std::to_string(outcomeMs) + ":" + episodeId] = {type, abortMs};
        }
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
        tick.obs.t_ms = tick.t_ms;
        ExtractNumber(line, "obs_walking", &tick.obs.walking);
        ExtractNumber(line, "obs_pdr_outbound", &tick.obs.pdr_outbound);
        ExtractNumber(line, "obs_geo_outbound", &tick.obs.geo_outbound);
        ExtractNumber(line, "obs_wifi_detach", &tick.obs.wifi_detach);
        ExtractNumber(line, "obs_cell_detach", &tick.obs.cell_detach);
        ExtractNumber(line, "obs_ble_detach", &tick.obs.ble_detach);
        ExtractNumber(line, "obs_time_prior", &tick.obs.time_prior);
        ExtractNumber(line, "obs_baro_descending", &tick.obs.baro_descending);
        ExtractNumber(line, "obs_baro_lower_platform", &tick.obs.baro_lower_platform);
        ExtractNumber(line, "obs_baro_ascending", &tick.obs.baro_ascending);
        ExtractNumber(line, "obs_vertical_closure", &tick.obs.vertical_closure);
        ExtractBool(line, "obs_sequence_available", &tick.obs.sequence_available);
        ExtractNumber(line, "obs_sequence_progress", &tick.obs.sequence_progress);
        ExtractNumber(line, "obs_sequence_complete", &tick.obs.sequence_complete);
        ExtractNumber(line, "obs_negative_pattern_match", &tick.obs.negative_pattern_match);
        ExtractNumber(line, "obs_cancel_sequence_match", &tick.obs.cancel_sequence_match);
        ExtractNumber(line, "obs_sequence_reliability", &tick.obs.sequence_reliability);
        ExtractNumber(line, "baro_descent_m", &tick.obs.baro_descent_m);
        tick.obs.baro_stable_platform_known = ExtractBool(
            line, "baro_stable_platform", &tick.obs.baro_stable_platform);
        // Old history did not persist stability separately. A recorded lower
        // platform is still sufficient proof that stability was true.
        if (!tick.obs.baro_stable_platform_known && tick.obs.baro_lower_platform >= 0.5) {
            tick.obs.baro_stable_platform = true;
            tick.obs.baro_stable_platform_known = true;
        }
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
        tick.obs.context_side = side;
        std::string label;
        ExtractString(line, "label", &label);
        int64_t outcomeMs = tick.t_ms;
        ExtractInt64(line, "outcome_t_ms", &outcomeMs);
        int64_t abortMs = 0;
        ExtractInt64(line, "abort_t_ms", &abortMs);
        if (sinceMs > 0 && outcomeMs > 0 && outcomeMs < sinceMs) {
            continue;
        }
        std::string episodeId;
        ExtractString(line, "episode_id", &episodeId);
        const auto interpretation = interpretations.find(
            side + ":" + std::to_string(outcomeMs) + ":" + episodeId);
        if (interpretation != interpretations.end() && label == "FALSE_PUSH") {
            label = interpretation->second.type;
            abortMs = interpretation->second.abort_ms;
        }
        const std::string key = side + ":" + std::to_string(outcomeMs) + ":" + label + ":" + episodeId;
        auto &ep = grouped[key];
        ep.episode_id = episodeId;
        ep.side = side;
        ep.label = label;
        ep.outcome_ms = outcomeMs;
        ep.abort_ms = abortMs;
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

struct TrialGuardMetrics {
    bool ok = false;
    double score = -1.0e100;
    int false_kept = 0;
    int soft_false_kept = 0;
    int confirmed_kept = 0;
    int missed_leave = 0;
};

TrialGuardMetrics gTrialBaselineMetrics {};

TrialGuardMetrics ParseTrialGuardMetrics(const std::string &json)
{
    TrialGuardMetrics m;
    bool ok = false;
    ExtractBool(json, "ok", &ok);
    m.ok = ok;
    double v = 0.0;
    if (ExtractNumber(json, "score", &v)) m.score = v;
    if (ExtractNumber(json, "false_kept", &v)) m.false_kept = static_cast<int>(v);
    if (ExtractNumber(json, "soft_false_kept", &v)) m.soft_false_kept = static_cast<int>(v);
    if (ExtractNumber(json, "confirmed_kept", &v)) m.confirmed_kept = static_cast<int>(v);
    if (ExtractNumber(json, "missed_leave", &v)) m.missed_leave = static_cast<int>(v);
    return m;
}

std::string EvalRootDir()
{
    return ProductStore::GetInstance().RootDir().empty()
        ? std::string("/data/service/el1/public/commuteagentservice")
        : ProductStore::GetInstance().RootDir();
}

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
    gTrialBaselineMetrics = ParseTrialGuardMetrics(EvaluateThetaOnHistoryJson(EvalRootDir(), gTrialSnapshot, 0, 30));
    if (!gTrialBaselineMetrics.ok) {
        if (err) {
            *err = "baseline history replay unavailable; refusing unsafe trial";
        }
        return false;
    }
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
    gTrialBaselineMetrics = TrialGuardMetrics {};
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
    const Theta candidateTheta = LoadLiveTheta();
    const TrialGuardMetrics candidate =
        ParseTrialGuardMetrics(EvaluateThetaOnHistoryJson(EvalRootDir(), candidateTheta, 0, 30));
    if (!candidate.ok) {
        if (err) {
            *err = "candidate replay unavailable; revert required";
        }
        return false;
    }
    if (candidate.score < gTrialBaselineMetrics.score + 0.25) {
        if (err) {
            *err = "commit guard: score improvement below 0.25";
        }
        return false;
    }
    if (candidate.missed_leave > gTrialBaselineMetrics.missed_leave) {
        if (err) {
            *err = "commit guard: missed_leave increased";
        }
        return false;
    }
    if (candidate.false_kept > gTrialBaselineMetrics.false_kept) {
        if (err) {
            *err = "commit guard: hard false push increased";
        }
        return false;
    }
    if (candidate.confirmed_kept < gTrialBaselineMetrics.confirmed_kept ||
        candidate.soft_false_kept < gTrialBaselineMetrics.soft_false_kept) {
        if (err) {
            *err = "commit guard: confirmed/lower-platform positives decreased";
        }
        return false;
    }
    gTrialActive = false;
    gTrialBaselineMetrics = TrialGuardMetrics {};
    return true;
}

std::string EvaluateThetaOnHistoryWithAdapterJson(const std::string &rootDir, const Theta &theta,
    const ObservationAdapter &adapter, int64_t sinceMs, int maxEpisodes, bool includePrefixTrace, int64_t cutoffMs,
    std::vector<ReplayEpisodeSummary> *summaries)
{
    std::vector<HsmmEpisode> hsmmEpisodes;
    if (!LoadHsmmEpisodes(rootDir, sinceMs, maxEpisodes, &hsmmEpisodes)) {
        return "{\"ok\":false,\"error\":\"policy_history.jsonl with HSMM observations required for context template replay\"}";
    }
    return ScoreHsmmReplay(hsmmEpisodes, theta, adapter, includePrefixTrace, cutoffMs, summaries);
}

bool CheckReplayEpisodeSafety(const std::vector<ReplayEpisodeSummary> &baseline,
    const std::vector<ReplayEpisodeSummary> &candidate, std::string *reason)
{
    auto reject = [&](const std::string &why) { if (reason) *reason = why; return false; };
    if (baseline.empty() || baseline.size() != candidate.size()) return reject("episode_set_mismatch");
    for (size_t i = 0; i < baseline.size(); ++i) {
        const auto &b = baseline[i];
        const auto &c = candidate[i];
        if (b.key != c.key || b.positive != c.positive || b.hard_negative != c.hard_negative)
            return reject("episode_set_mismatch");
        if (b.hard_negative && !b.pushed && c.pushed) return reject("new_false_push:" + b.key);
        if (b.positive && b.pushed && !c.pushed) return reject("lost_positive:" + b.key);
        if (b.positive && b.pushed && c.pushed && c.push_ms > b.push_ms)
            return reject("positive_push_delayed:" + b.key);
    }
    return true;
}

bool HasActiveThetaTrial()
{
    std::lock_guard<std::mutex> lock(gTrialMu);
    return gTrialActive;
}

}  // namespace commute_sa
