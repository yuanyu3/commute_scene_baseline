#include "sa_agent/ThetaPersonalizerAgentConfig.h"

#include "Model.h"

#include <map>

namespace OHOS::Multimedia::CameraAgentService {
namespace sa_agent {

std::shared_ptr<jiuwen::AgentConfig> BuildThetaPersonalizerAgentConfig(
    const std::string &apiBaseUrl, const std::string &apiKey)
{
    auto agentConfig = std::make_shared<jiuwen::AgentConfig>();
    agentConfig->id = "commute-theta-personalizer";
    agentConfig->description = "Online theta personalizer (forced JSON I/O); SceneEngine does realtime scenes.";
    agentConfig->prompt = commute_sa::PersonalizationController::SystemPrompt();
    agentConfig->mode = jiuwen::AgentType::REACT;
    agentConfig->maxTurn = 1;

    jiuwen::ModelConfig modelConfig;
    modelConfig.modelType = "openai";
    modelConfig.modelName = "qwen3-8b";
    modelConfig.hyperParameters["temperature"] = "0.1";
    modelConfig.hyperParameters["max_tokens"] = "512";
    modelConfig.conf["api_base"] = apiBaseUrl;
    modelConfig.conf["api_key"] = apiKey;
    agentConfig->modelConfig = modelConfig;
    return agentConfig;
}

}  // namespace sa_agent
}  // namespace OHOS::Multimedia::CameraAgentService
