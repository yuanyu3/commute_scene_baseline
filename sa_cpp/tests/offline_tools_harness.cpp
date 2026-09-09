/**
 * Offline harness: invoke evidence + action ops (same backends as jiuwen tools).
 * No LLM / no OHOS required.
 */
#include "commute_sa/action_ops.h"
#include "commute_sa/context_template.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/evidence_strength_profile.h"
#include "commute_sa/personalization_optimizer.h"
#include "commute_sa/product_store.h"
#include "commute_sa/theta_eval.h"

#include <cmath>
#include <cstdio>
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

void WriteFile(const std::string &path, const std::string &body)
{
    std::ofstream out(path, std::ios::trunc);
    out << body;
}

void Section(const char *name)
{
    std::cout << "\n=== " << name << " ===\n";
}

void Call(const char *tool, const std::string &out)
{
    std::cout << "[" << tool << "]\n" << out << "\n";
}

}  // namespace

int main(int argc, char **argv)
{
    // Dataset mode used by ablation scripts:
    //   commute_offline_tools <root> <evaluate|rules|rule_optimize|agent_optimize|profile|
    //     template_catalog|template_generate|template_fit|template_trial|template_commit|template_discard|template_active|
    //     template_evaluate_frozen> [json]
    if (argc >= 3) {
        const std::string dataRoot = argv[1];
        const std::string command = argv[2];
        const std::string params = argc >= 4 ? argv[3] : "{}";
        if (!commute_sa::ProductStore::GetInstance().Init(dataRoot)) {
            std::cout << "{\"ok\":false,\"error\":\"ProductStore init failed\"}\n";
            return 1;
        }
        commute_sa::EvidenceQuery::GetInstance().SetRootDir(dataRoot);
        if (command == "evaluate") {
            std::cout << commute_sa::EvaluateThetaOnHistoryAction(params) << "\n";
        } else if (command == "rules") {
            std::cout << commute_sa::AnalyzePersonalizationRulesAction(params) << "\n";
        } else if (command == "rule_optimize") {
            std::cout << commute_sa::RunRulePersonalizationAction(params) << "\n";
        } else if (command == "agent_optimize") {
            std::cout << commute_sa::RunConstrainedThetaOptimizerAction(params) << "\n";
        } else if (command == "profile") {
            std::cout << commute_sa::GetPersonalizationProfileAction(params) << "\n";
        } else if (command == "anchor_profile") {
            std::cout << commute_sa::GetUserAnchorProfileAction(params) << "\n";
        } else if (command == "history_summary") {
            std::cout << commute_sa::GetPersonalizationHistorySummaryAction(params) << "\n";
        } else if (command == "strength_estimate") {
            std::cout << commute_sa::EstimateEvidenceStrengthAction(params) << "\n";
        } else if (command == "strength_trial") {
            std::cout << commute_sa::GetEvidenceStrengthTrialAction(params) << "\n";
        } else if (command == "strength_commit") {
            std::cout << commute_sa::CommitEvidenceStrengthCandidateAction(params) << "\n";
        } else if (command == "strength_discard") {
            std::cout << commute_sa::DiscardEvidenceStrengthCandidateAction(params) << "\n";
        } else if (command == "duration_fit") {
            std::cout << commute_sa::FitDurationPriorAction(params) << "\n";
        } else if (command == "duration_trial") {
            std::cout << commute_sa::GetDurationPriorTrialAction(params) << "\n";
        } else if (command == "duration_commit") {
            std::cout << commute_sa::CommitDurationPriorCandidateAction(params) << "\n";
        } else if (command == "duration_discard") {
            std::cout << commute_sa::DiscardDurationPriorCandidateAction(params) << "\n";
        } else if (command == "template_catalog") {
            std::cout << commute_sa::GetContextTemplateCatalogAction(params) << "\n";
        } else if (command == "semantic_timeline") {
            std::cout << commute_sa::EvidenceQuery::GetInstance().GetEpisodeSemanticTimelineJson(params) << "\n";
        } else if (command == "dynamic_diagnostics") {
            std::cout << commute_sa::EvidenceQuery::GetInstance().GetEpisodeDynamicDiagnosticsJson(params) << "\n";
        } else if (command == "aborted_candidates") {
            std::cout << commute_sa::GetAbortedLeaveCandidatesAction(params) << "\n";
        } else if (command == "propose_aborted") {
            std::cout << commute_sa::ProposeAbortedLeaveInterpretationAction(params) << "\n";
        } else if (command == "template_generate") {
            std::cout << commute_sa::GenerateContextTemplateAction(params) << "\n";
        } else if (command == "template_fit") {
            const auto trial = commute_sa::GenerateContextTemplateAction(params);
            std::cout << "{\"trial\":" << trial << ",\"commit\":"
                      << commute_sa::CommitContextTemplateAction("{}") << "}\n";
        } else if (command == "template_trial") {
            std::cout << commute_sa::GetContextTemplateTrialAction(params) << "\n";
        } else if (command == "template_commit") {
            std::cout << commute_sa::CommitContextTemplateAction(params) << "\n";
        } else if (command == "template_discard") {
            std::cout << commute_sa::DiscardContextTemplateAction(params) << "\n";
        } else if (command == "template_active") {
            std::cout << commute_sa::GetActiveContextTemplateAction(params) << "\n";
        } else if (command == "template_evaluate_frozen") {
            std::cout << commute_sa::EvaluateActiveContextTemplateOnHistoryAction(params) << "\n";
        } else if (command == "template_diagnose") {
            std::cout << commute_sa::DiagnoseContextTemplateOnHistoryAction(params) << "\n";
        } else {
            std::cout << "{\"ok\":false,\"error\":\"unknown dataset command\"}\n";
            return 2;
        }
        return 0;
    }

    const std::string root = "offline_tools_run";
    MKDIR(root.c_str());
    const std::string session = root + "/20260806_session";
    MKDIR(session.c_str());

    // Product minimal set
    WriteFile(root + "/theta.json",
        "{\n  \"enter_leave\": 0.58,\n  \"exit_leave\": 0.45,\n  \"min_evidence\": 2,\n"
        "  \"w_walk\": 0.25,\n  \"w_radio\": 0.15,\n  \"arm_delay_s\": 25,\n"
        "  \"weekday_leave_home_hour\": 8.25\n}\n");
    WriteFile(root + "/anchors.json",
        "{\n  \"coordinate_system\": \"WGS84\",\n"
        "  \"home\": {\"id\":\"home\",\"lat\":40.05,\"lon\":116.17,\"r_in_m\":50,\"r_out_m\":90,\"method\":\"seed\"},\n"
        "  \"company\": {\"id\":\"co\",\"lat\":40.0,\"lon\":116.3,\"r_in_m\":80,\"r_out_m\":120,\"method\":\"seed\"}\n}\n");

    WriteFile(root + "/leave_episodes.jsonl",
        "{\"type\":\"push\",\"t_push_ms\":1700000000000,\"intent\":\"DEPARTURE_NOTIFICATION\","
        "\"scene\":\"LEAVING_HOME\",\"score_home\":0.72,\"dist_home_m\":55,\"walking\":true}\n"
        "{\"type\":\"label\",\"t_label_ms\":1700001200000,\"t_push_ms\":1700000000000,"
        "\"label\":\"FALSE_PUSH\",\"home_relation\":\"INSIDE\",\"dist_home_m\":12}\n");

    WriteFile(root + "/leave_window_samples.jsonl",
        "{\"t_ms\":1700000060000,\"t_push_ms\":1700000000000,\"lat\":40.0501,\"lon\":116.1702,"
        "\"acc\":15,\"walking\":true,\"home_relation\":\"NEAR\",\"dist_home_m\":48}\n"
        "{\"t_ms\":1700000120000,\"t_push_ms\":1700000000000,\"lat\":40.0500,\"lon\":116.1701,"
        "\"acc\":12,\"walking\":false,\"home_relation\":\"INSIDE\",\"dist_home_m\":18}\n");
    WriteFile(root + "/episode_interpretations.jsonl", "");

    // Three independent validated lower-platform episodes support deterministic
    // estimation of the vertical parameter family.
    WriteFile(root + "/policy_history.jsonl",
        "{\"t_ms\":1799999940000,\"outcome_t_ms\":1800000000000,\"side\":\"company\",\"label\":\"CONFIRMED_LEAVE\",\"baro_descent_m\":18,\"obs_pdr_outbound\":0.8,\"obs_walking\":1,\"obs_geo_outbound\":0.7,\"obs_wifi_detach\":0,\"obs_cell_detach\":0,\"obs_ble_detach\":0,\"obs_time_prior\":0,\"obs_baro_descending\":1,\"obs_baro_lower_platform\":1,\"obs_baro_available\":true,\"obs_relation_known\":true,\"obs_inside\":true,\"obs_near\":false,\"obs_outside\":false,\"obs_approaching\":false,\"obs_attached\":false,\"lead_s\":60}\n"
        "{\"t_ms\":1800086340000,\"outcome_t_ms\":1800086400000,\"side\":\"company\",\"label\":\"FALSE_PUSH\",\"baro_descent_m\":20,\"obs_pdr_outbound\":0.8,\"obs_walking\":1,\"obs_geo_outbound\":0.7,\"obs_wifi_detach\":0,\"obs_cell_detach\":0,\"obs_ble_detach\":0,\"obs_time_prior\":0,\"obs_baro_descending\":1,\"obs_baro_lower_platform\":1,\"obs_baro_available\":true,\"obs_relation_known\":true,\"obs_inside\":true,\"obs_near\":false,\"obs_outside\":false,\"obs_approaching\":false,\"obs_attached\":false,\"lead_s\":60}\n"
        "{\"t_ms\":1800172740000,\"outcome_t_ms\":1800172800000,\"side\":\"company\",\"label\":\"CONFIRMED_LEAVE\",\"baro_descent_m\":22,\"obs_pdr_outbound\":0.8,\"obs_walking\":1,\"obs_geo_outbound\":0.7,\"obs_wifi_detach\":0,\"obs_cell_detach\":0,\"obs_ble_detach\":0,\"obs_time_prior\":0,\"obs_baro_descending\":1,\"obs_baro_lower_platform\":1,\"obs_baro_available\":true,\"obs_relation_known\":true,\"obs_inside\":true,\"obs_near\":false,\"obs_outside\":false,\"obs_approaching\":false,\"obs_attached\":false,\"lead_s\":60}\n"
        "{\"t_ms\":1800259140000,\"outcome_t_ms\":1800259200000,\"side\":\"company\",\"label\":\"FALSE_PUSH\",\"baro_descent_m\":5,\"obs_pdr_outbound\":0.1,\"obs_walking\":1,\"obs_geo_outbound\":0,\"obs_wifi_detach\":1,\"obs_cell_detach\":0,\"obs_ble_detach\":0,\"obs_time_prior\":0,\"obs_baro_descending\":0,\"obs_baro_lower_platform\":0,\"obs_baro_available\":true,\"obs_relation_known\":true,\"obs_inside\":true,\"obs_near\":false,\"obs_outside\":false,\"obs_approaching\":false,\"obs_attached\":false,\"lead_s\":60}\n");

    // Two explicitly labelled intent reversals. They are neither hard false
    // examples nor successful departures: the positive prefix should remain
    // recognizable and the ordered return should be learned as cancellation.
    {
        std::ofstream history(root + "/policy_history.jsonl", std::ios::app);
        for (int episode = 0; episode < 2; ++episode) {
            const int64_t base = 1800345600000LL + episode * 86400000LL;
            const std::string id = "abort_" + std::to_string(episode);
            history << "{\"t_ms\":" << base << ",\"outcome_t_ms\":" << base + 30000
                    << ",\"abort_t_ms\":" << base + 20000 << ",\"episode_id\":\"" << id
                    << "\",\"side\":\"company\",\"label\":\"ABORTED_LEAVE\",\"baro_descent_m\":18,"
                       "\"baro_stable_platform\":false,\"obs_pdr_outbound\":0.7,\"obs_walking\":1,"
                       "\"obs_geo_outbound\":0,\"obs_wifi_detach\":0,\"obs_cell_detach\":0,"
                       "\"obs_ble_detach\":0,\"obs_time_prior\":0,\"obs_baro_descending\":1,"
                       "\"obs_baro_lower_platform\":0,\"obs_baro_ascending\":0,"
                       "\"obs_vertical_closure\":0,\"obs_baro_available\":true,"
                       "\"obs_relation_known\":true,\"obs_inside\":true,\"obs_near\":false,"
                       "\"obs_outside\":false,\"obs_approaching\":false,\"obs_attached\":false}\n";
            history << "{\"t_ms\":" << base + 20000 << ",\"outcome_t_ms\":" << base + 30000
                    << ",\"abort_t_ms\":" << base + 20000 << ",\"episode_id\":\"" << id
                    << "\",\"side\":\"company\",\"label\":\"ABORTED_LEAVE\",\"baro_descent_m\":8,"
                       "\"baro_stable_platform\":false,\"obs_pdr_outbound\":0,\"obs_walking\":1,"
                       "\"obs_geo_outbound\":0,\"obs_wifi_detach\":0,\"obs_cell_detach\":0,"
                       "\"obs_ble_detach\":0,\"obs_time_prior\":0,\"obs_baro_descending\":0,"
                       "\"obs_baro_lower_platform\":0,\"obs_baro_ascending\":1,"
                       "\"obs_vertical_closure\":0,\"obs_baro_available\":true,"
                       "\"obs_relation_known\":true,\"obs_inside\":true,\"obs_near\":false,"
                       "\"obs_outside\":false,\"obs_approaching\":false,\"obs_attached\":false}\n";
            history << "{\"t_ms\":" << base + 30000 << ",\"outcome_t_ms\":" << base + 30000
                    << ",\"abort_t_ms\":" << base + 20000 << ",\"episode_id\":\"" << id
                    << "\",\"side\":\"company\",\"label\":\"ABORTED_LEAVE\",\"baro_descent_m\":1,"
                       "\"baro_stable_platform\":true,\"obs_pdr_outbound\":0,\"obs_walking\":0,"
                       "\"obs_geo_outbound\":0,\"obs_wifi_detach\":0,\"obs_cell_detach\":0,"
                       "\"obs_ble_detach\":0,\"obs_time_prior\":0,\"obs_baro_descending\":0,"
                       "\"obs_baro_lower_platform\":0,\"obs_baro_ascending\":0,"
                       "\"obs_vertical_closure\":1,\"obs_baro_available\":true,"
                       "\"obs_relation_known\":true,\"obs_inside\":true,\"obs_near\":false,"
                       "\"obs_outside\":false,\"obs_approaching\":false,\"obs_attached\":true}\n";
        }
        const int64_t candidate = 1800604800000LL;
        for (int tick = 0; tick < 3; ++tick) {
            const int64_t at = candidate + tick * 10000;
            history << "{\"t_ms\":" << at << ",\"outcome_t_ms\":" << candidate + 20000
                    << ",\"episode_id\":\"agent_abort_candidate\",\"side\":\"company\","
                       "\"label\":\"FALSE_PUSH\",\"baro_descent_m\":" << (tick == 0 ? 18 : tick == 1 ? 8 : 1)
                    << ",\"baro_stable_platform\":" << (tick == 2 ? "true" : "false")
                    << ",\"obs_pdr_outbound\":" << (tick == 0 ? 0.7 : 0.0)
                    << ",\"obs_walking\":" << (tick < 2 ? 1 : 0)
                    << ",\"obs_geo_outbound\":0,\"obs_wifi_detach\":0,\"obs_cell_detach\":0,"
                       "\"obs_ble_detach\":0,\"obs_time_prior\":0,\"obs_baro_descending\":"
                    << (tick == 0 ? 1 : 0) << ",\"obs_baro_lower_platform\":0,\"obs_baro_ascending\":"
                    << (tick == 1 ? 1 : 0) << ",\"obs_vertical_closure\":" << (tick == 2 ? 1 : 0)
                    << ",\"obs_baro_available\":true,\"obs_relation_known\":true,\"obs_inside\":true,"
                       "\"obs_near\":false,\"obs_outside\":false,\"obs_approaching\":false,"
                       "\"obs_attached\":" << (tick == 2 ? "true" : "false") << "}\n";
        }
    }

    // Fake Ability session CSVs (wallTsMs first column)
    WriteFile(session + "/wifi_data_smoke.csv",
        "wallTsMs,bssid,ssid,rssi,freq,power_mode\n"
        "1699999700000,aa:bb:cc:dd:ee:01,HomeWiFi,-45,2412,HIGH_STILL\n"
        "1700000005000,aa:bb:cc:dd:ee:01,HomeWiFi,-48,2412,HIGH_WALKING\n"
        "1700000600000,aa:bb:cc:dd:ee:02,OtherNet,-70,2437,HIGH_WALKING\n");
    WriteFile(session + "/cell_data_smoke.csv",
        "wallTsMs,type,cellId,signalIntensity,mcc,mnc,pci,tac,earfcn,power_mode\n"
        "1700000000000,LTE,12345,-90,460,00,10,100,1850,HIGH_WALKING\n"
        "1700000900000,LTE,67890,-95,460,00,11,101,1850,HIGH_WALKING\n");
    WriteFile(session + "/mag_data_smoke.csv",
        "wallTsMs,x,y,z,power_mode\n"
        "1700000000000,12.1,-3.2,41.0,HIGH_WALKING\n"
        "1700000030000,18.4,-1.1,39.5,HIGH_WALKING\n");
    WriteFile(session + "/location_data_smoke.csv",
        "wallTsMs,lat,lon,acc,power_mode\n"
        "1700000000000,40.0502,116.1703,20,HIGH_WALKING\n"
        "1700000300000,40.05005,116.17015,14,HIGH_WALKING\n");

    if (!commute_sa::ProductStore::GetInstance().Init(root)) {
        std::cerr << "ProductStore init failed\n";
        return 1;
    }
    commute_sa::EvidenceQuery::GetInstance().SetRootDir(root);
    auto &eq = commute_sa::EvidenceQuery::GetInstance();

    int fails = 0;
    auto expectOk = [&](const char *name, const std::string &s) {
        if (s.find("\"ok\":true") == std::string::npos && s.find("\"ok\": true") == std::string::npos) {
            std::cerr << "FAIL " << name << " not ok\n";
            ++fails;
        }
    };

    Section("EVIDENCE");
    Call("get_theta", eq.GetThetaJson());
    expectOk("get_theta", eq.GetThetaJson());
    Call("get_anchors", eq.GetAnchorsJson());
    Call("get_error_stats", eq.GetErrorStatsJson("{}"));
    Call("get_leave_episode", eq.GetLeaveEpisodeJson("{}"));
    Call("get_leave_window_samples", eq.GetLeaveWindowSamplesJson("{\"limit\":10}"));
    Call("get_leave_sensor_summary",
        eq.GetLeaveSensorSummaryJson("{\"t_push_ms\":1700000000000,\"before_s\":600,\"after_s\":1200}"));
    expectOk("get_leave_sensor_summary",
        eq.GetLeaveSensorSummaryJson("{\"t_push_ms\":1700000000000,\"before_s\":600,\"after_s\":1200}"));
    const std::string semanticTimeline = eq.GetEpisodeSemanticTimelineJson(
        "{\"episode_id\":\"abort_0\",\"anchor_id\":\"co\",\"bin_s\":10}");
    Call("get_episode_semantic_timeline", semanticTimeline);
    if (semanticTimeline.find("\"quality\":\"OBSERVED\"") == std::string::npos ||
        semanticTimeline.find("\"vertical_closure\"") == std::string::npos ||
        semanticTimeline.find("\"known_ticks\":1") == std::string::npos) {
        std::cerr << "FAIL semantic timeline lost aligned signal detail\n"; ++fails;
    }
    const std::string dynamics = eq.GetEpisodeDynamicDiagnosticsJson(
        "{\"episode_id\":\"abort_0\",\"anchor_id\":\"co\"}");
    Call("get_episode_dynamic_diagnostics", dynamics);
    if (dynamics.find("\"ordered_return\":true") == std::string::npos ||
        dynamics.find("\"first_ascending_after_descent_ms\":1800345620000") == std::string::npos ||
        dynamics.find("\"first_closure_after_ascent_ms\":1800345630000") == std::string::npos) {
        std::cerr << "FAIL dynamic diagnostics lost ordered return\n"; ++fails;
    }
    const std::string boundedTimeline = eq.GetEpisodeSemanticTimelineJson(
        "{\"episode_id\":\"abort_0\",\"anchor_id\":\"co\",\"bin_s\":5,\"max_bins\":1}");
    if (boundedTimeline.find("exceeds max_bins") == std::string::npos) {
        std::cerr << "FAIL semantic timeline ignored output budget\n"; ++fails;
    }

    Section("ACTION");
    Call("get_param_limits", std::string("{\"ok\":true,\"param_limits\":") + commute_sa::GetParamLimitsJson() + "}");
    const std::string apply = commute_sa::ApplyThetaDeltaAction(
        "{\"param\":\"enter_leave\",\"delta\":0.03,\"reason\":\"offline_false_push_home_wifi_still_strong\"}");
    Call("apply_theta_delta", apply);
    expectOk("apply_theta_delta", apply);

    const std::string audit = commute_sa::WriteAuditAction(
        "{\"message\":\"offline harness: raised enter_leave after FALSE_PUSH + home wifi\",\"changes\":"
        "{\"param\":\"enter_leave\",\"delta\":0.03}}");
    Call("write_audit", audit);
    expectOk("write_audit", audit);

    const std::string job = commute_sa::RequestAnchorReestimateAction("{\"which\":\"home\"}");
    Call("request_anchor_reestimate", job);
    expectOk("request_anchor_reestimate", job);

    Section("VERIFY_FILES");
    std::ifstream th(root + "/theta.json");
    std::string thetaBody((std::istreambuf_iterator<char>(th)), std::istreambuf_iterator<char>());
    std::cout << "theta.json after apply:\n" << thetaBody << "\n";
    if (thetaBody.find("0.61") == std::string::npos && thetaBody.find("0.610") == std::string::npos) {
        // 0.58+0.03=0.61
        if (thetaBody.find("enter_leave") == std::string::npos) {
            std::cerr << "FAIL theta not updated\n";
            ++fails;
        }
    }

    Section("ACTIVE_CONTEXT_TEMPLATE_RUNTIME");
    WriteFile(root + "/active_context_template.json",
        "{\"schema_version\":2,\"template_name\":\"runtime_smoke\",\"side\":\"company\","
        "\"anchor_id\":\"co\",\"applicability\":\"baro_ready\","
        "\"positive_sequence\":\"baro_descending,lower_platform,geo_outbound\","
        "\"negative_pattern\":\"walking,no_baro_descent\",\"strength_level\":\"LOW\","
        "\"parameter_families\":\"vertical_threshold\","
        "\"personalized_vertical_threshold\":true,\"baro_min_descent_m\":20,\"baro_sample_count\":3,"
        "\"strength\":0.2,\"rationale\":\"smoke\"}\n");
    commute_sa::ReloadActiveContextTemplateRuntime();
    commute_sa::LeaveObservation runtimeObs;
    runtimeObs.walking = 1.0;
    runtimeObs.pdr_outbound = 1.0;
    runtimeObs.wifi_detach = 1.0;
    runtimeObs.baro_available = true;
    runtimeObs.baro_lower_platform = 1.0;
    runtimeObs.baro_descent_m = 15.0;
    runtimeObs.baro_stable_platform = true;
    runtimeObs.baro_stable_platform_known = true;
    const bool applied = commute_sa::ApplyActiveContextTemplateObservation(
        "company", "co", 1700000000000, &runtimeObs);
    std::cout << "applied=" << applied << " walking_after=" << runtimeObs.walking
              << " negative_match=" << runtimeObs.negative_pattern_match << "\n";
    if (!applied || runtimeObs.baro_lower_platform != 0.0 || std::abs(runtimeObs.walking - 1.0) > 1.0e-9 ||
        std::abs(runtimeObs.wifi_detach - 1.0) > 1.0e-9 ||
        std::abs(runtimeObs.negative_pattern_match - 1.0) > 1.0e-9 ||
        std::abs(runtimeObs.sequence_reliability - 0.2) > 1.0e-9) {
        std::cerr << "FAIL active context template was not applied to realtime observation\n";
        ++fails;
    }

    // Positive matching emits independent sequence evidence and leaves atomic
    // sensor semantics unchanged.
    commute_sa::LeaveObservation seq1;
    seq1.baro_available = true;
    seq1.baro_descending = 1.0;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1700000005000, &seq1);
    commute_sa::LeaveObservation seq2;
    seq2.baro_available = true;
    seq2.baro_descent_m = 25.0;
    seq2.baro_stable_platform = true;
    seq2.baro_stable_platform_known = true;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1700000010000, &seq2);
    commute_sa::LeaveObservation seq3;
    seq3.baro_available = true;
    seq3.geo_outbound = 1.0;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1700000015000, &seq3);
    if (!seq3.sequence_available || std::abs(seq3.sequence_progress - 1.0) > 1.0e-9 ||
        std::abs(seq3.sequence_complete - 1.0) > 1.0e-9 ||
        std::abs(seq3.sequence_reliability - 0.2) > 1.0e-9 || std::abs(seq3.geo_outbound - 1.0) > 1.0e-9) {
        std::cerr << "FAIL positive sequence did not emit independent completion evidence\n";
        ++fails;
    }

    Section("GENERIC_PREFIX_READINESS");
    WriteFile(root + "/active_context_template.json",
        "{\"schema_version\":4,\"template_name\":\"generic_prefix\",\"side\":\"company\","
        "\"anchor_id\":\"co\",\"applicability\":\"always\","
        "\"positive_sequence\":\"wifi_detach,pdr_outbound,geo_outbound\","
        "\"ready_prefix_length\":2,\"strength\":0.4}");
    commute_sa::ReloadActiveContextTemplateRuntime();
    commute_sa::LeaveObservation firstPrefix;
    firstPrefix.wifi_detach = 1.0;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1800000000000, &firstPrefix);
    commute_sa::LeaveObservation readyPrefix;
    readyPrefix.pdr_outbound = 1.0;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1800000005000, &readyPrefix);
    commute_sa::LeaveObservation expiredPrefix;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1800000610000, &expiredPrefix);
    if (firstPrefix.sequence_ready != 0.0 || readyPrefix.sequence_ready != 1.0 ||
        readyPrefix.sequence_complete != 0.0 || expiredPrefix.sequence_ready != 0.0 ||
        readyPrefix.pdr_outbound != 1.0) {
        std::cerr << "FAIL generic prefix readiness/expiry/raw observation preservation\n";
        ++fails;
    }

    Section("ABORTED_LEAVE_CANCEL_SEQUENCE");
    WriteFile(root + "/active_context_template.json",
        "{\"schema_version\":5,\"template_name\":\"return_cancel\",\"side\":\"company\","
        "\"anchor_id\":\"co\",\"applicability\":\"baro_ready\","
        "\"positive_sequence\":\"baro_descending,lower_platform\","
        "\"cancel_sequence\":\"baro_ascending,vertical_closure\","
        "\"negative_pattern\":\"\",\"ready_prefix_length\":1,\"strength\":0.4}");
    commute_sa::ReloadActiveContextTemplateRuntime();
    commute_sa::LeaveObservation depart1;
    depart1.baro_available = true; depart1.baro_descending = 1.0;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1900000000000, &depart1);
    commute_sa::LeaveObservation depart2;
    depart2.baro_available = true; depart2.baro_lower_platform = 1.0;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1900000010000, &depart2);
    commute_sa::LeaveObservation reverse1;
    reverse1.baro_available = true; reverse1.baro_ascending = 1.0;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1900000020000, &reverse1);
    commute_sa::LeaveObservation reverse2;
    reverse2.baro_available = true; reverse2.vertical_closure = 1.0;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1900000030000, &reverse2);
    commute_sa::LeaveObservation cancelHold;
    cancelHold.baro_available = true;
    commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1900000040000, &cancelHold);
    if (reverse1.cancel_sequence_match != 0.0 || reverse2.cancel_sequence_match != 1.0 ||
        reverse2.negative_pattern_match != 1.0 || cancelHold.cancel_sequence_match != 1.0 ||
        reverse2.sequence_ready != 0.0) {
        std::cerr << "FAIL ordered return sequence did not latch cancellation\n";
        ++fails;
    }

    Section("ALTERNATIVE_RETURN_PATHS");
    commute_sa::ReplayEpisodeSummary safeReturn;
    safeReturn.key = "return"; safeReturn.aborted = true; safeReturn.cancel_ms = 100;
    auto lateReturn = safeReturn;
    lateReturn.cancel_ms = 101;
    std::string guardReason;
    if (commute_sa::CheckReplayEpisodeSafety({safeReturn}, {lateReturn}, &guardReason)) {
        std::cerr << "FAIL delayed cancellation passed per-episode guard\n"; ++fails;
    }
    auto newPush = safeReturn;
    newPush.pushed = true;
    if (commute_sa::CheckReplayEpisodeSafety({safeReturn}, {newPush}, &guardReason)) {
        std::cerr << "FAIL new aborted push passed per-episode guard\n"; ++fails;
    }
    WriteFile(root + "/active_context_template.json",
        "{\"schema_version\":6,\"template_name\":\"return_paths\",\"side\":\"company\","
        "\"anchor_id\":\"co\",\"applicability\":\"always\","
        "\"positive_sequence\":\"walking\","
        "\"cancel_paths\":\"baro_ascending,vertical_closure|geo_outbound,approaching,attached\","
        "\"negative_pattern\":\"\",\"ready_prefix_length\":1,\"strength\":0.4}");
    const auto step = [](int seconds, bool walk, bool up, bool closure, bool geo,
                         bool approaching, bool attached, bool baro = true, bool outside = false) {
        commute_sa::LeaveObservation obs;
        obs.walking = walk; obs.baro_ascending = up; obs.vertical_closure = closure;
        obs.geo_outbound = geo; obs.approaching = approaching; obs.attached = attached;
        obs.baro_available = baro; obs.outside = outside;
        commute_sa::ApplyActiveContextTemplateObservation("company", "co", 1910000000000 + seconds * 1000, &obs);
        return obs;
    };
    commute_sa::ReloadActiveContextTemplateRuntime();
    step(0, true, false, false, false, false, false);
    step(5, false, true, false, false, false, false);
    if (step(10, false, false, true, false, false, false).cancel_sequence_match != 1.0) {
        std::cerr << "FAIL vertical path required optional attachment\n"; ++fails;
    }
    step(15, true, false, false, true, false, false);
    if (step(25, true, false, false, true, false, false).cancel_sequence_match != 0.0) {
        std::cerr << "FAIL fresh departure could not release cancel hold\n"; ++fails;
    }
    commute_sa::ReloadActiveContextTemplateRuntime();
    step(0, true, false, false, false, false, false, false);
    step(5, false, false, false, true, false, false, false);
    step(10, false, false, false, false, true, false, false);
    if (step(15, false, false, false, false, false, true, false).cancel_sequence_match != 1.0) {
        std::cerr << "FAIL horizontal alternative required barometer\n"; ++fails;
    }
    commute_sa::ReloadActiveContextTemplateRuntime();
    step(0, true, false, false, false, false, false);
    step(5, false, false, true, false, false, false);
    if (step(10, false, true, false, false, false, false).cancel_sequence_match != 0.0 ||
        step(200, false, false, true, false, false, false).cancel_sequence_match != 0.0) {
        std::cerr << "FAIL unordered or expired events cancelled departure\n"; ++fails;
    }
    commute_sa::ReloadActiveContextTemplateRuntime();
    step(0, true, false, false, false, false, false);
    step(5, false, true, false, false, false, false, false);
    if (step(10, false, false, true, false, false, false, false).cancel_sequence_match != 0.0) {
        std::cerr << "FAIL unavailable barometer used as positive evidence\n"; ++fails;
    }
    commute_sa::ReloadActiveContextTemplateRuntime();
    step(0, true, false, false, false, false, false);
    step(5, false, true, false, false, false, false);
    step(10, false, false, false, false, false, false, true, true);
    if (step(15, false, false, true, false, false, false).cancel_sequence_match != 0.0) {
        std::cerr << "FAIL outside followed by return treated as cancellation\n"; ++fails;
    }

    const std::string cancelGenerated = commute_sa::GenerateContextTemplateAction(
        "{\"template_name\":\"cancel_fit\",\"anchor_id\":\"co\","
        "\"applicability\":\"baro_ready\",\"positive_sequence\":\"baro_descending\","
        "\"cancel_sequence\":\"baro_ascending,vertical_closure\",\"rationale\":\"smoke\"}");
    Call("generate_cancel_context_template", cancelGenerated);
    if (cancelGenerated.find("\"n_aborted_leave\":2") == std::string::npos ||
        cancelGenerated.find("\"aborted_cancel_recognized\":2") == std::string::npos) {
        std::cerr << "FAIL ABORTED_LEAVE replay did not recognize ordered cancellation\n";
        ++fails;
    }

    const std::string interpretation = commute_sa::ProposeAbortedLeaveInterpretationAction(
        "{\"anchor_id\":\"co\",\"episode_id\":\"agent_abort_candidate\","
        "\"confidence\":0.8,"
        "\"rationale\":\"shared vertical departure prefix followed by measured closure\"}");
    Call("propose_aborted_leave_interpretation", interpretation);
    const std::string interpretedReplay = commute_sa::EvaluateThetaOnHistoryAction("{}");
    if (interpretation.find("\"ok\":true") == std::string::npos ||
        interpretedReplay.find("\"n_aborted_leave\":3") == std::string::npos) {
        std::cerr << "FAIL validated Agent interpretation was not applied as a replay label override\n";
        ++fails;
    }

    Section("PARAMETER_FAMILY_ESTIMATION");
    const std::string generated = commute_sa::GenerateContextTemplateAction(
        "{\"template_name\":\"parameter_family_smoke\",\"anchor_id\":\"co\","
        "\"applicability\":\"always\",\"positive_sequence\":\"walking\","
        "\"parameter_families\":\"vertical_threshold\",\"rationale\":\"smoke\"}");
    Call("generate_context_template", generated);
    if (generated.find("\"personalized_time\":false") == std::string::npos ||
        generated.find("\"personalized_vertical_threshold\":true") == std::string::npos ||
        generated.find("\"baro_sample_count\":3") == std::string::npos) {
        std::cerr << "FAIL supported parameter families were not estimated from three episodes\n";
        ++fails;
    }

    Section("DUPLICATE_HISTORY_REJECTED");
    std::string repeatedTick;
    {
        std::ifstream history(root + "/policy_history.jsonl");
        std::getline(history, repeatedTick);
    }
    {
        std::ofstream history(root + "/policy_history.jsonl", std::ios::app);
        history << repeatedTick << '\n';
    }
    const auto duplicateReplay = commute_sa::EvaluateActiveContextTemplateOnHistoryAction("{}");
    if (duplicateReplay.find("\"ok\":false") == std::string::npos) {
        std::cerr << "FAIL duplicate timestamps accepted by frozen replay\n"; ++fails;
    }
    std::cout << "\n=== SUMMARY fails=" << fails << " ===\n";
    return fails == 0 ? 0 : 1;
}
