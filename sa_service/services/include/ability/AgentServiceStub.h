/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
* Description: to be complete
* Author: to be complete
* Create: 2024/06/05
*/

#ifndef AGENTSERVICESTUB_H
#define AGENTSERVICESTUB_H

#include <cstdint>
#include "IAgentService.h"
#include "iremote_stub.h"

namespace OHOS::Multimedia::CameraAgentService {

class AgentServiceStub : public IRemoteStub<IAgentService> {
public:
    int32_t OnRemoteRequest(uint32_t code, MessageParcel &data, MessageParcel &reply, MessageOption &option) override;
};

}

#endif
