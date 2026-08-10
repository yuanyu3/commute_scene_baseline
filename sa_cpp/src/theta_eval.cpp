#include "commute_sa/theta_eval.h"

#include "commute_sa/baseline_runtime.h"
#include "commute_sa/product_store.h"
#include "commute_sa/theta.h"

#include <algorithm>
#include <cmath>
#include <fstream>
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
        << ",\"notes\":\"Replay recorded push scores against candidate enter_leave; lead vs lead_min/max. "
           "Not full GPS SceneEngine replay. Episodes filtered by focus_side.\""
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
        << ",\"better_guidance\":\"Prefer higher score; if missed_leave rises, undo enter_leave increase; "
           "if false_kept high, raise enter_leave / min_evidence.\"}";
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
