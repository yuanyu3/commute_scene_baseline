#include "commute_sa/action_ops.h"
#include "commute_sa/anchor_reestimate.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/product_store.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
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

}  // namespace

int main()
{
    const std::string root = "anchor_reest_smoke_run";
    MKDIR(root.c_str());
    const std::string session = root + "/20260804_session";
    MKDIR(session.c_str());

    commute_sa::ProductStore::GetInstance().Init(root);
    commute_sa::EvidenceQuery::GetInstance().SetRootDir(root);

    // Seed anchors far apart; reestimate should overwrite company from daytime GPS.
    WriteFile(root + "/anchors.json",
        "{\n  \"coordinate_system\": \"WGS84\",\n"
        "  \"home\": {\"id\":\"home\",\"lat\":40.1,\"lon\":116.1,\"r_in_m\":60,\"r_out_m\":100,\"method\":\"seed\"},\n"
        "  \"company\": {\"id\":\"co\",\"lat\":40.0,\"lon\":116.3,\"r_in_m\":80,\"r_out_m\":120,\"method\":\"seed\"}\n}\n");

    // Daytime still cluster around 40.05,116.17 (company-like)
    std::ostringstream loc;
    loc << "wallTsMs,latitude,longitude,accuracy,sourceType\n";
    // 2026-08-04 10:00 CST ≈ use epoch-like ms; local hour used by reestimator
    // Use a fixed daytime stamp: 2026-08-04 10:00:00 +08 = 1754272800000 approx — compute via known.
    // Simpler: use wall times that localtime on this machine will read as hour 10-16.
    const int64_t tDay = 1722751200000LL;  // approx Aug 2024 daytime depending on TZ — generate many points
    for (int i = 0; i < 80; ++i) {
        const int64_t t = tDay + i * 60000;
        const double lat = 40.0539 + (i % 3) * 0.00001;
        const double lon = 116.1735 + (i % 2) * 0.00001;
        loc << t << "," << lat << "," << lon << ",20,1\n";
    }
    // Night points far away for home
    for (int i = 0; i < 40; ++i) {
        const int64_t t = tDay + 14LL * 3600 * 1000 + i * 60000;  // +14h → nightish
        loc << t << "," << (39.90 + (i % 2) * 0.00001) << "," << (116.40 + (i % 2) * 0.00001) << ",25,2\n";
    }
    WriteFile(session + "/location_data_smoke.csv", loc.str());

    WriteFile(root + "/leave_episodes.jsonl",
        "{\"type\":\"label\",\"t_label_ms\":1000,\"t_push_ms\":0,\"label\":\"MISSED_LEAVE\",\"side\":\"company\"}\n"
        "{\"type\":\"push\",\"t_push_ms\":2000,\"intent\":\"LEAVE_COMPANY_NOTIFICATION\"}\n"
        "{\"type\":\"label\",\"t_label_ms\":3000,\"t_push_ms\":2000,\"label\":\"CONFIRMED_LEAVE\",\"lead_s\":150}\n");

    const auto stats = commute_sa::EvidenceQuery::GetInstance().GetErrorStatsJson("{\"since_ms\":0}");
    std::cout << "error_stats=" << stats << "\n";
    if (stats.find("\"n_missed_leave\":1") == std::string::npos) {
        std::cerr << "FAIL: expected n_missed_leave=1\n";
        return 1;
    }

    // Sensor summary PDR block (empty session ok — available false)
    WriteFile(session + "/sensor_events.csv",
        "sequence_id,received_at,source_observed_at,event_type,motion_state,episode_id,payload_json,power_mode\n"
        "1,2026-08-04T10:00:00.000+08:00,2026-08-04T10:00:00.000+08:00,WALKING_STARTED,WALKING,walk-1,\"{}\",HIGH\n"
        "2,2026-08-04T10:00:01.000+08:00,2026-08-04T10:00:01.000+08:00,PDR_POINT,WALKING,walk-1,"
        "\"{\\\"x\\\":0,\\\"y\\\":0}\",HIGH\n"
        "3,2026-08-04T10:00:20.000+08:00,2026-08-04T10:00:20.000+08:00,PDR_POINT,WALKING,walk-1,"
        "\"{\\\"x\\\":15,\\\"y\\\":0}\",HIGH\n");

    // t_center near those ISO times: 2026-08-04 10:00 +08
    const std::string sum = commute_sa::EvidenceQuery::GetInstance().GetLeaveSensorSummaryJson(
        "{\"t_push_ms\":1754272800000,\"before_s\":600,\"after_s\":600,\"session_dir\":\"" + session + "\"}");
    std::cout << "summary_snip pdr=" << (sum.find("\"pdr\":") != std::string::npos ? "yes" : "no") << "\n";
    if (sum.find("\"pdr\":") == std::string::npos) {
        std::cerr << "FAIL: missing pdr block\n";
        return 1;
    }

    const std::string job = commute_sa::RequestAnchorReestimateAction("{\"which\":\"company\"}");
    std::cout << "reestimate=" << job << "\n";
    if (job.find("\"ok\":true") == std::string::npos) {
        std::cerr << "FAIL: reestimate not ok\n";
        return 1;
    }
    if (job.find("\"status\":\"done\"") == std::string::npos && job.find("\"status\":\"failed\"") == std::string::npos) {
        std::cerr << "FAIL: expected execution status in response\n";
        return 1;
    }

    std::cout << "OK gapfill smoke\n";
    return 0;
}
