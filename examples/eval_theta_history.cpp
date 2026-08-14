/**
 * Offline: evaluate default vs a candidate theta.json on leave-window HSMM history.
 * Usage: eval_theta_history <product_root> [theta_path] [max_episodes]
 */
#include "commute_sa/theta.h"
#include "commute_sa/theta_eval.h"

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "usage: eval_theta_history <product_root> [theta.json] [max_episodes]\n";
        return 2;
    }
    const std::string root = argv[1];
    const std::string thetaPath = (argc >= 3) ? argv[2] : (root + "/theta.json");
    const int maxEp = (argc >= 4) ? std::stoi(argv[3]) : 100;

    commute_sa::Theta def = commute_sa::DefaultTheta();
    def.focus_side = "company";
    std::cout << "=== DEFAULT (focus_side=company) ===\n"
              << commute_sa::EvaluateThetaOnHistoryJson(root, def, 0, maxEp) << "\n";

    commute_sa::Theta cand = commute_sa::DefaultTheta();
    std::string err;
    if (!commute_sa::LoadThetaFromFile(thetaPath, &cand, &err)) {
        std::cerr << "load theta failed: " << err << " path=" << thetaPath << "\n";
        return 1;
    }
    if (cand.focus_side.empty()) {
        cand.focus_side = "company";
    }
    std::cout << "=== CANDIDATE " << thetaPath << " ===\n"
              << commute_sa::EvaluateThetaOnHistoryJson(root, cand, 0, maxEp) << "\n";
    return 0;
}
