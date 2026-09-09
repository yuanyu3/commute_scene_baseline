#include "commute_sa/theta.h"
#include "commute_sa/evidence_strength_profile.h"
#include "commute_sa/product_store.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

namespace {

bool Near(double a, double b)
{
    return std::fabs(a - b) < 1e-9;
}

void Write(const std::string &path, const std::string &body)
{
    std::ofstream out(path, std::ios::trunc);
    out << body;
}

}  // namespace

int main()
{
    Write("legacy_strength_theta.json",
        R"({"w_walk":0.25,"w_pdr":0.20,"w_geo":0.20,"w_wifi":0.12,)"
        R"("w_cell":0.08,"w_ble":0.0,"w_time":0.20,"w_baro":0.20})");
    commute_sa::Theta legacy;
    if (!commute_sa::LoadThetaFromFile("legacy_strength_theta.json", &legacy, nullptr)) return 1;
    const auto legacyConfig = commute_sa::HsmmConfigFromTheta(legacy);
    const double expected[] = {1.0, 0.85, 0.85, 0.61, 0.49, 0.0, 0.85, 0.85, 0.85};
    for (size_t i = 0; i < legacyConfig.reliability.size(); ++i) {
        if (!Near(legacyConfig.reliability[i], expected[i])) {
            std::cerr << "legacy migration mismatch at " << i << "\n";
            return 2;
        }
    }

    Write("direct_strength_theta.json",
        R"({"evidence_strength":{"walking":0.01,"pdr":0.2,"geo":0.3,"wifi":0.0,)"
        R"("cell":0.4,"ble":0.5,"time":0.6,"baro":1.5}})");
    commute_sa::Theta direct;
    if (!commute_sa::LoadThetaFromFile("direct_strength_theta.json", &direct, nullptr)) return 3;
    const auto directConfig = commute_sa::HsmmConfigFromTheta(direct);
    if (!Near(directConfig.reliability[0], 0.01) ||
        !Near(directConfig.reliability[3], 0.0) ||
        !Near(directConfig.reliability[7], 1.0) ||
        !Near(directConfig.reliability[8], 1.0)) {
        std::cerr << "direct strength was remapped or not clamped\n";
        return 4;
    }

    commute_sa::Theta runtime = commute_sa::DefaultTheta();
    runtime.w_walk = -0.2;
    runtime.w_pdr = 0.07;
    runtime.w_geo = 2.0;
    const auto runtimeConfig = commute_sa::HsmmConfigFromTheta(runtime);
    if (!Near(runtimeConfig.reliability[0], 0.0) ||
        !Near(runtimeConfig.reliability[1], 0.07) ||
        !Near(runtimeConfig.reliability[2], 1.0)) {
        std::cerr << "runtime strength is not direct [0,1]\n";
        return 5;
    }

    const std::string root = "evidence_strength_profile_tmp";
    MKDIR(root.c_str());
    commute_sa::ProductStore::GetInstance().Init(root);
    Write(root + "/user_anchor_profile_company.json",
        R"({"schema_version":1,"side":"company","anchor_id":"company_001",)"
        R"("evidence_strength":{"wifi":0.10,"baro":0.90},)"
        R"("duration_prior":{"pre_leave_mean_s":135,"leaving_mean_s":180}})");
    const auto company = commute_sa::ApplyCommittedUserAnchorProfile(
        commute_sa::DefaultTheta(), "company", "company_001");
    const auto home = commute_sa::ApplyCommittedUserAnchorProfile(
        commute_sa::DefaultTheta(), "home", "home_001");
    if (!Near(company.w_wifi, 0.10) || !Near(company.w_baro, 0.90) ||
        !Near(company.hsmm_preleave_mean_s, 135.0) || !Near(company.hsmm_leaving_mean_s, 180.0) ||
        !Near(home.hsmm_leaving_mean_s, commute_sa::DefaultTheta().hsmm_leaving_mean_s) ||
        !Near(home.w_wifi, commute_sa::DefaultTheta().w_wifi)) {
        std::cerr << "anchor profile leaked across anchors\n";
        return 6;
    }

    const double discriminative = commute_sa::EstimateEvidenceStrengthValue(0.5, 8, 8, 0, 8, 16);
    const double nonSpecific = commute_sa::EstimateEvidenceStrengthValue(0.5, 8, 8, 8, 8, 16);
    const double bothLow = commute_sa::EstimateEvidenceStrengthValue(0.5, 0, 8, 0, 8, 16);
    const double sparse = commute_sa::EstimateEvidenceStrengthValue(0.5, 1, 1, 0, 1, 10);
    const double missing = commute_sa::EstimateEvidenceStrengthValue(0.5, 1, 1, 0, 1, 100);
    if (!(discriminative > nonSpecific && discriminative > bothLow &&
          std::fabs(sparse - 0.5) < 0.20 && missing < sparse)) {
        std::cerr << "deterministic estimator shrinkage/coverage mismatch\n";
        return 7;
    }

    double median = 0.0;
    double trimmed = 0.0;
    double mad = 0.0;
    double confidence = 0.0;
    const double noSamples = commute_sa::EstimateDurationMeanValue(
        120.0, {}, &median, &trimmed, &mad, &confidence);
    const double tooFew = commute_sa::EstimateDurationMeanValue(
        120.0, {180.0, 185.0}, &median, &trimmed, &mad, &confidence);
    const double sparseDuration = commute_sa::EstimateDurationMeanValue(
        120.0, {178.0, 180.0, 182.0}, &median, &trimmed, &mad, &confidence);
    const double stableDuration = commute_sa::EstimateDurationMeanValue(
        120.0, {176, 178, 179, 180, 181, 182, 183, 184}, &median, &trimmed, &mad, &confidence);
    const double withoutOutlier = commute_sa::EstimateDurationMeanValue(
        120.0, {176, 178, 179, 180, 181, 182, 183}, &median, &trimmed, &mad, &confidence);
    const double withOutlier = commute_sa::EstimateDurationMeanValue(
        120.0, {176, 178, 179, 180, 181, 182, 183, 1000}, &median, &trimmed, &mad, &confidence);
    if (!Near(noSamples, 120.0) || !Near(tooFew, 120.0) ||
        !(sparseDuration > 120.0 && sparseDuration < stableDuration) ||
        !(stableDuration > 160.0 && std::fabs(withOutlier - withoutOutlier) < 1e-9)) {
        std::cerr << "duration estimator robustness/shrinkage mismatch\n";
        return 8;
    }

    std::cout << "ok\n";
    return 0;
}
