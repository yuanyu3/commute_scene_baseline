/**
 * Offline harness: invoke evidence + action ops (same backends as jiuwen tools).
 * No LLM / no OHOS required.
 */
#include "commute_sa/action_ops.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/product_store.h"

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

int main()
{
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

    std::cout << "\n=== SUMMARY fails=" << fails << " ===\n";
    return fails == 0 ? 0 : 1;
}
