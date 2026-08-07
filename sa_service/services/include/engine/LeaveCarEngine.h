/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SA bridge to leave-car detector (C API only).
 */

#ifndef LEAVE_CAR_ENGINE_H
#define LEAVE_CAR_ENGINE_H

#include <cstdint>
#include <string>

namespace OHOS::Multimedia::CameraAgentService::LeaveCarEngine {

void PushAcc(int64_t timestampMs, double x, double y, double z);
void PushGyro(int64_t timestampMs, double x, double y, double z);
void PushMag(int64_t timestampMs, double x, double y, double z);

void Start();
void Stop();

std::string GetStateJson();

} // namespace OHOS::Multimedia::CameraAgentService::LeaveCarEngine

#endif // LEAVE_CAR_ENGINE_H
