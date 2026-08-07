#include "commute_sa/evidence_query.h"

#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
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
    const std::string root = "evidence_query_smoke_tmp";
    MKDIR(root.c_str());

    WriteFile(root + "/leave_episodes.jsonl",
        "{\"type\":\"push\",\"t_push_ms\":1000,\"intent\":\"DEPARTURE_NOTIFICATION\",\"scene\":\"LEAVING_HOME\"}\n"
        "{\"type\":\"label\",\"t_label_ms\":1200000,\"t_push_ms\":1000,\"label\":\"FALSE_PUSH\","
        "\"home_relation\":\"INSIDE\",\"dist_home_m\":10}\n");
    WriteFile(root + "/theta.json", "{\"enter_leave\":0.55}\n");
    WriteFile(root + "/anchors.json", "{\"coordinate_system\":\"WGS84\"}\n");

    commute_sa::EvidenceQuery::GetInstance().SetRootDir(root);

    const std::string stats = commute_sa::EvidenceQuery::GetInstance().GetErrorStatsJson("{}");
    std::cout << "stats=" << stats << "\n";
    if (stats.find("\"n_push\":1") == std::string::npos || stats.find("\"n_false_push\":1") == std::string::npos) {
        std::cerr << "FAIL error stats\n";
        return 1;
    }

    const std::string ep = commute_sa::EvidenceQuery::GetInstance().GetLeaveEpisodeJson("{}");
    if (ep.find("FALSE_PUSH") == std::string::npos) {
        std::cerr << "FAIL episode\n";
        return 1;
    }

    const std::string th = commute_sa::EvidenceQuery::GetInstance().GetThetaJson();
    if (th.find("enter_leave") == std::string::npos) {
        std::cerr << "FAIL theta\n";
        return 1;
    }

    const std::string wifi = commute_sa::EvidenceQuery::GetInstance().GetWifiWindowJson("{}");
    if (wifi.find("\"ok\":false") == std::string::npos) {
        std::cerr << "FAIL expected no session dump\n";
        return 1;
    }

    std::cout << "ok\n";
    return 0;
}
