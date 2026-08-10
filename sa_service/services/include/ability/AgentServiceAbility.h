/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
* Description: to be complete
* Author: to be complete
* Create: 2024/06/05
*/

#ifndef AGENTSERVICEABILITY_H
#define AGENTSERVICEABILITY_H

#include "system_ability.h"
#include "AgentServiceStub.h"
#include <cstdint>
#include <string>

namespace OHOS::Multimedia::CameraAgentService {

class AgentServiceAbility : public SystemAbility, public AgentServiceStub {
DECLARE_SYSTEM_ABILITY(AgentServiceAbility);
public:
    AgentServiceAbility(int32_t saId, bool runOnCreate);
    ~AgentServiceAbility();
    std::string HelloWorld() override;
    virtual void UnLoadService() override;
    int32_t StartSensorCollection() override;
    int32_t StopSensorCollection() override;
    std::string GetRecentAccFrames() override;
    int32_t StartLocationCollection() override;
    int32_t StopLocationCollection() override;
    std::string DrainPdrResults() override;
    int32_t ClearPdrResults() override;
    std::string GetPdrDiag() override;
    std::string GetLeaveCarState() override;
    std::string GetStartPlaceInfo() override;
    int32_t StartWifiCollection() override;
    int32_t StopWifiCollection() override;
    int32_t StartBleCollection() override;
    int32_t StopBleCollection() override;
    int32_t StartCellCollection() override;
    int32_t StopCellCollection() override;
    std::string GetProductDebugTimeline() override;
    int32_t ClearProductDebugTimeline() override;

protected:
    void OnStart() override;
    void OnStop() override;

};

}

#endif
