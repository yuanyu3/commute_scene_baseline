#pragma once

#include <string>
#include <vector>

namespace sa_agent {

/**
 * Register read-only evidence tools with jiuwen ResourceManager.
 * Safe to call multiple times (re-register may fail silently).
 * Returns list of tool names for Agent::AddTools.
 */
std::vector<std::string> RegisterEvidenceTools();

/** Names only (no registration). */
const std::vector<std::string> &EvidenceToolNames();

}  // namespace sa_agent
