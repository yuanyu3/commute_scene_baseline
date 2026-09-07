#include "commute_sa/evidence_query.h"
#include "commute_sa/personalization_optimizer.h"
#include "commute_sa/product_store.h"
#include "commute_sa/theta_eval.h"

#include <fstream>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

namespace {

void WriteTick(std::ofstream &out, int64_t tMs, int64_t outcomeMs, const char *label, double walk,
    double pdr, double wifi, double baro, bool lower, bool attached)
{
    out << "{\"t_ms\":" << tMs << ",\"outcome_t_ms\":" << outcomeMs
        << ",\"side\":\"company\",\"label\":\"" << label << "\""
        << ",\"leaving_probability\":0.62"
        << ",\"obs_walking\":" << walk << ",\"obs_pdr_outbound\":" << pdr
        << ",\"obs_geo_outbound\":0,\"obs_wifi_detach\":" << wifi
        << ",\"obs_cell_detach\":0,\"obs_ble_detach\":0,\"obs_time_prior\":0.2"
        << ",\"obs_baro_descending\":" << baro << ",\"obs_baro_lower_platform\":"
        << (lower ? 1 : 0) << ",\"obs_baro_available\":" << (baro > 0 || lower ? "true" : "false")
        << ",\"obs_relation_known\":true,\"obs_inside\":true,\"obs_near\":false"
        << ",\"obs_outside\":false,\"obs_approaching\":false,\"obs_attached\":"
        << (attached ? "true" : "false") << "}\n";
}

void WriteFixture(const std::string &root)
{
    std::ofstream theta(root + "/theta.json", std::ios::trunc);
    theta << "{\"focus_side\":\"company\",\"enter_leave\":0.55,\"exit_leave\":0.45,"
             "\"w_walk\":0.25,\"w_pdr\":0.20,\"w_geo\":0.20,\"w_wifi\":0.12,"
             "\"w_cell\":0.08,\"w_ble\":0.02,\"w_time\":0.20,\"w_baro\":0.20,"
             "\"arm_delay_s\":0,\"hsmm_preleave_min_s\":0,\"hsmm_preleave_mean_s\":20,"
             "\"hsmm_preleave_max_s\":120,\"hsmm_leaving_min_s\":0,"
             "\"hsmm_leaving_mean_s\":30,\"hsmm_leaving_max_s\":180,"
             "\"lead_min_s\":30,\"lead_max_s\":180}\n";
    std::ofstream history(root + "/policy_history.jsonl", std::ios::trunc);
    for (int i = 0; i < 40; ++i) {
        WriteTick(history, 100000 + i * 1000, 140000, "CONFIRMED_LEAVE", 1.0, 0.9, 0.8,
            i > 10 ? 0.8 : 0.0, i > 20, false);
    }
    for (int i = 0; i < 30; ++i) {
        WriteTick(history, 200000 + i * 1000, 230000, "FALSE_PUSH", 1.0, 0.05, 0.95, 0.0,
            false, i > 24);
    }
}

}  // namespace

int main()
{
    const std::string root = "personalization_optimizer_smoke_tmp";
    MKDIR(root.c_str());
    WriteFixture(root);
    commute_sa::ProductStore::GetInstance().Init(root);
    commute_sa::EvidenceQuery::GetInstance().SetRootDir(root);

    const std::string analysis = commute_sa::AnalyzePersonalizationRulesAction("{\"limit\":30}");
    std::cout << "analysis=" << analysis << "\n";
    if (analysis.find("\"ok\":true") == std::string::npos ||
        analysis.find("WIFI_TRANSIENT") == std::string::npos ||
        analysis.find("\"rule_plan\"") == std::string::npos) {
        std::cerr << "FAIL rule analysis\n";
        return 1;
    }

    const std::string optimized = commute_sa::RunConstrainedThetaOptimizerAction(
        "{\"primary_block\":\"radio_reliability\",\"primary_direction\":\"decrease\","
        "\"secondary_block\":\"preleave_duration\",\"secondary_direction\":\"increase\","
        "\"max_candidates\":8}");
    std::cout << "optimized=" << optimized << "\n";
    if (optimized.find("\"ok\":true") == std::string::npos ||
        optimized.find("\"candidates\":[") == std::string::npos ||
        optimized.find("\"commit_guard\"") == std::string::npos) {
        std::cerr << "FAIL constrained optimizer\n";
        return 1;
    }

    const std::string trial = commute_sa::GetOptimizationTrialAction("{}");
    if (trial.find("\"trial_active\":true") == std::string::npos) {
        std::cerr << "FAIL staged trial\n";
        return 1;
    }
    const std::string discard = commute_sa::DiscardOptimizationTrialAction("{}");
    if (discard.find("\"discarded\":true") == std::string::npos) {
        std::cerr << "FAIL discard\n";
        return 1;
    }
    {
        std::ofstream oneSided(root + "/policy_history.jsonl", std::ios::trunc);
        for (int i = 0; i < 30; ++i) {
            WriteTick(oneSided, 300000 + i * 1000, 330000, "FALSE_PUSH", 1.0, 0.05, 0.95, 0.0,
                false, i > 24);
        }
    }
    const std::string oneSided = commute_sa::RunConstrainedThetaOptimizerAction(
        "{\"primary_block\":\"preleave_duration\",\"primary_direction\":\"increase\","
        "\"objective\":\"false_push\"}");
    if (oneSided.find("\"best_candidate_id\":null") == std::string::npos ||
        oneSided.find("insufficient_positive_history_for_suppression") == std::string::npos) {
        std::cerr << "FAIL one-sided history coverage gate\n";
        return 1;
    }
    commute_sa::DiscardOptimizationTrialAction("{}");
    const std::string profile = commute_sa::GetPersonalizationProfileAction("{}");
    if (profile.find("\"derived_profile\"") == std::string::npos) {
        std::cerr << "FAIL derived profile\n";
        return 1;
    }
    const std::string shadow = commute_sa::ProposeContextProfileUpdateAction(
        "{\"context_name\":\"vertical_exit\",\"context_signature\":\"baro_available && lower_platform\","
        "\"support_count\":1,\"positive_count\":1,\"contradiction_count\":0,\"confidence\":0.9}");
    if (shadow.find("SHADOW_ONLY") == std::string::npos) {
        std::cerr << "FAIL context support gate\n";
        return 1;
    }
    const std::string accepted = commute_sa::ProposeContextProfileUpdateAction(
        "{\"context_name\":\"vertical_exit\",\"context_signature\":\"baro_available && lower_platform\","
        "\"support_count\":3,\"positive_count\":2,\"contradiction_count\":0,\"confidence\":0.8}");
    if (accepted.find("\"accepted\":true") == std::string::npos) {
        std::cerr << "FAIL context memory accept\n";
        return 1;
    }
    const std::string submitted = commute_sa::SubmitAgentAnalysisAction(
        "{\"context_name\":\"vertical_exit\",\"primary_cause\":\"WIFI_TRANSIENT\","
        "\"supporting_evidence\":\"wifi reattached; outbound weak\",\"contradicting_evidence\":\"walking sustained\","
        "\"intervention_block\":\"radio_reliability\",\"direction\":\"decrease\","
        "\"confidence\":0.78,\"abstain\":false}");
    if (submitted.find("\"validated\":true") == std::string::npos) {
        std::cerr << "FAIL structured analysis\n";
        return 1;
    }
    using commute_sa::ReplayEpisodeSummary;
    std::vector<ReplayEpisodeSummary> reference = {
        {"positive1", true, false, true, 1000, 90},
        {"positive2", true, false, true, 2000, 90},
        {"negative1", false, true, false, 0, -1},
        {"negative2", false, true, true, 3000, -1}};
    auto changed = reference;
    changed[0].push_ms -= 500;
    changed[1].push_ms += 100;
    std::string reason;
    if (commute_sa::CheckReplayEpisodeSafety(reference, changed, &reason)) {
        std::cerr << "FAIL average gain concealed delayed positive\n"; return 1;
    }
    changed = reference;
    changed[2].pushed = true;
    changed[3].pushed = false;
    if (commute_sa::CheckReplayEpisodeSafety(reference, changed, &reason)) {
        std::cerr << "FAIL unchanged false count concealed false-push swap\n"; return 1;
    }
    changed = reference;
    changed[0].push_ms -= 500;
    if (!commute_sa::CheckReplayEpisodeSafety(reference, changed, &reason)) {
        std::cerr << "FAIL safe earlier candidate rejected\n"; return 1;
    }
    std::cout << "ok\n";
    return 0;
}
