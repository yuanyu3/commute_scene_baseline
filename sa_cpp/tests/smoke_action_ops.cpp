#include "commute_sa/action_ops.h"
#include "commute_sa/evidence_query.h"
#include "commute_sa/product_store.h"

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

int main()
{
    const std::string root = "action_ops_smoke_tmp";
    MKDIR(root.c_str());
    commute_sa::ProductStore::GetInstance().Init(root);
    commute_sa::EvidenceQuery::GetInstance().SetRootDir(root);

    {
        std::ofstream th(root + "/theta.json", std::ios::trunc);
        th << "{\"enter_leave\":0.58,\"w_walk\":0.25,\"arm_delay_s\":25,\"min_evidence\":2}\n";
    }

    const std::string r1 = commute_sa::ApplyThetaDeltaAction(
        "{\"param\":\"enter_leave\",\"delta\":0.03,\"reason\":\"smoke_false_push\"}");
    std::cout << "apply=" << r1 << "\n";
    if (r1.find("\"ok\":true") == std::string::npos) {
        std::cerr << "FAIL apply\n";
        return 1;
    }

    const std::string audit = commute_sa::WriteAuditAction(
        "{\"message\":\"no_op demo\",\"changes\":{\"n\":0}}");
    std::cout << "audit=" << audit << "\n";
    if (audit.find("\"ok\":true") == std::string::npos) {
        std::cerr << "FAIL audit\n";
        return 1;
    }

    const std::string job = commute_sa::RequestAnchorReestimateAction("{\"which\":\"home\"}");
    std::cout << "job=" << job << "\n";
    if (job.find("\"ok\":true") == std::string::npos || job.find("job_id") == std::string::npos) {
        std::cerr << "FAIL anchor job\n";
        return 1;
    }

    const std::string lim = commute_sa::GetParamLimitsJson();
    if (lim.find("enter_leave") == std::string::npos) {
        std::cerr << "FAIL limits\n";
        return 1;
    }

    std::cout << "ok\n";
    return 0;
}
