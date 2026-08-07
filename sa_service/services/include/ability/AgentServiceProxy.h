/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
* Description: to be complete
* Author: to be complete
* Create: 2024/06/05
*/

#ifndef AGENTSERVICEPROXY_H
#define AGENTSERVICEPROXY_H

#include "IAgentService.h"
#include "iremote_proxy.h"
#include <string>

namespace OHOS::Multimedia::CameraAgentService {

class AgentServiceProxy : public IRemoteProxy<IAgentService> {
public:
    explicit AgentServiceProxy(const sptr<IRemoteObject> &object);

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
private:
    static inline BrokerDelegator<AgentServiceProxy> delegator_;
};
}

#endif
