#pragma once

#include <string>
#include <vector>

namespace sa_agent {

/** Register action tools (apply_theta_delta / write_audit / request_anchor_reestimate). */
std::vector<std::string> RegisterActionTools();

const std::vector<std::string> &ActionToolNames();

}  // namespace sa_agent
