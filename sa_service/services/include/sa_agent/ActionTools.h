#pragma once

#include <string>
#include <vector>

namespace sa_agent {

/** Register constrained personalization tools; direct theta mutation is intentionally not exposed. */
std::vector<std::string> RegisterActionTools();

const std::vector<std::string> &ActionToolNames();

}  // namespace sa_agent
