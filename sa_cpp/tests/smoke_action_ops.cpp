#include "commute_sa/action_ops.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/product_store.h"
#include "commute_sa/theta.h"
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

static void WriteFixture(const std::string &root)
{
    std::ofstream th(root + "/theta.json", std::ios::trunc);
    th << "{\"enter_leave\":0.58,\"w_walk\":0.25,\"arm_delay_s\":25,\"min_evidence\":2,"
          "\"lead_min_s\":90,\"lead_max_s\":240}\n";
    std::ofstream policy(root + "/policy.json", std::ios::trunc);
    policy << "{\"schema_version\":1,\"revision\":0,\"enabled\":true,"
              "\"template_name\":\"confirmed_leaving\",\"trigger_phase\":\"LEAVING\","
              "\"probability_threshold\":0.58,\"min_duration_s\":0,\"min_independent_evidence\":2,"
              "\"require_walking\":false,\"require_wifi_detach\":false,\"require_radio\":false,"
              "\"allow_cell_pdr_pair\":true,\"gps_mode\":\"OPTIONAL\"}\n";
    std::ofstream ep(root + "/leave_episodes.jsonl", std::ios::trunc);
    ep << "{\"type\":\"push\",\"t_push_ms\":1000,\"intent\":\"LEAVE_COMPANY_NOTIFICATION\","
          "\"score_home\":0.1,\"score_company\":0.50}\n";
    ep << "{\"type\":\"label\",\"t_push_ms\":1000,\"label\":\"FALSE_PUSH\"}\n";
    ep << "{\"type\":\"push\",\"t_push_ms\":2000,\"intent\":\"LEAVE_COMPANY_NOTIFICATION\","
          "\"score_home\":0.1,\"score_company\":0.70}\n";
    ep << "{\"type\":\"label\",\"t_push_ms\":2000,\"label\":\"CONFIRMED_LEAVE\",\"lead_s\":120}\n";
    std::ofstream ph(root + "/policy_history.jsonl", std::ios::trunc);
    ph << "{\"t_ms\":1500,\"label\":\"FALSE_PUSH\",\"preleave_probability\":0.55,"
          "\"leaving_probability\":0.62,\"hits\":2,\"walking\":true,\"wifi_detach\":false,"
          "\"cell_leave\":true,\"ble_detach\":false,\"pdr_net_out_m\":2,\"evidence_duration_s\":6}\n";
    ph << "{\"t_ms\":2500,\"label\":\"CONFIRMED_LEAVE\",\"preleave_probability\":0.68,"
          "\"leaving_probability\":0.30,\"hits\":3,\"walking\":true,\"wifi_detach\":true,"
          "\"cell_leave\":true,\"ble_detach\":false,\"pdr_net_out_m\":4,\"evidence_duration_s\":8,"
          "\"lead_s\":43}\n";
}

int main()
{
    const std::string root = "action_ops_smoke_tmp";
    MKDIR(root.c_str());
    WriteFixture(root);

    commute_sa::Theta th0 = commute_sa::DefaultTheta();
    commute_sa::LoadThetaFromFile(root + "/theta.json", &th0, nullptr);
    const std::string eval0 = commute_sa::EvaluateThetaOnHistoryJson(root, th0, 0, 10);
    std::cout << "eval0=" << eval0 << "\n";
    if (eval0.find("\"n_episodes\":2") == std::string::npos) {
        std::cerr << "FAIL eval0\n";
        return 1;
    }

    commute_sa::ProductStore::GetInstance().Init(root);
    commute_sa::EvidenceQuery::GetInstance().SetRootDir(root);

    const std::string begin = commute_sa::BeginThetaTrialAction("{}");
    std::cout << "begin=" << begin << "\n";
    if (begin.find("\"ok\":true") == std::string::npos) {
        std::cerr << "FAIL begin\n";
        return 1;
    }

    const std::string apply = commute_sa::ApplyThetaDeltaAction(
        "{\"param\":\"enter_leave\",\"delta\":0.03,\"reason\":\"smoke_false_push\"}");
    std::cout << "apply=" << apply << "\n";
    if (apply.find("\"ok\":true") == std::string::npos) {
        std::cerr << "FAIL apply\n";
        return 1;
    }

    WriteFixture(root);  // refresh episodes if Init touched the jsonl handle
    commute_sa::Theta th1 = commute_sa::DefaultTheta();
    commute_sa::LoadThetaFromFile(root + "/theta.json", &th1, nullptr);
    const std::string eval1 = commute_sa::EvaluateThetaOnHistoryJson(root, th1, 0, 10);
    std::cout << "eval1=" << eval1 << "\n";
    if (eval1.find("\"false_avoided\":1") == std::string::npos) {
        std::cerr << "FAIL false_avoided\n";
        return 1;
    }

    const std::string revert = commute_sa::RevertThetaTrialAction("{}");
    std::cout << "revert=" << revert << "\n";
    if (revert.find("\"ok\":true") == std::string::npos) {
        std::cerr << "FAIL revert\n";
        return 1;
    }

    const std::string guardBegin = commute_sa::BeginThetaTrialAction("{}");
    const std::string guardedCommit = commute_sa::CommitThetaTrialAction("{}");
    std::cout << "guard_begin=" << guardBegin << "\nguarded_commit=" << guardedCommit << "\n";
    if (guardBegin.find("\"ok\":true") == std::string::npos ||
        guardedCommit.find("\"ok\":false") == std::string::npos ||
        guardedCommit.find("score improvement") == std::string::npos) {
        std::cerr << "FAIL theta commit guard\n";
        return 1;
    }
    if (commute_sa::RevertThetaTrialAction("{}").find("\"ok\":true") == std::string::npos) {
        std::cerr << "FAIL guarded trial cleanup\n";
        return 1;
    }

    const std::string audit = commute_sa::WriteAuditAction("{\"message\":\"trial smoke ok\"}");
    if (audit.find("\"ok\":true") == std::string::npos) {
        std::cerr << "FAIL audit\n";
        return 1;
    }

    const std::string policyBegin = commute_sa::BeginPolicyTrialAction("{}");
    const std::string policyBaseline = commute_sa::EvaluatePolicyOnHistoryAction("{}");
    const std::string policyRejected = commute_sa::ApplyPolicyCandidateAction(
        "{\"template_name\":\"wifi_first_preleave\",\"probability_threshold\":0.50,\"min_duration_s\":5}");
    const std::string policyApply = commute_sa::ApplyPolicyCandidateAction(
        "{\"template_name\":\"confirmed_leaving\"}");
    const std::string policyEval = commute_sa::EvaluatePolicyOnHistoryAction("{}");
    const std::string policyRevert = commute_sa::RevertPolicyTrialAction("{}");
    std::cout << "policy_begin=" << policyBegin << "\npolicy_baseline=" << policyBaseline
              << "\npolicy_rejected=" << policyRejected << "\npolicy_apply=" << policyApply
              << "\npolicy_eval=" << policyEval << "\npolicy_revert=" << policyRevert << "\n";
    if (policyBegin.find("\"ok\":true") == std::string::npos ||
        policyBaseline.find("\"ok\":true") == std::string::npos ||
        policyRejected.find("\"ok\":false") == std::string::npos ||
        policyApply.find("\"template_name\":\"confirmed_leaving\"") == std::string::npos ||
        policyEval.find("\"ok\":true") == std::string::npos ||
        policyRevert.find("\"ok\":true") == std::string::npos) {
        std::cerr << "FAIL policy trial\n";
        return 1;
    }

    std::cout << "ok\n";
    return 0;
}
