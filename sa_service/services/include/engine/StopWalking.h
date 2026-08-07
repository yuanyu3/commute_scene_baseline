/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: SA-side detector for walking-to-stationary transition.
 */

#ifndef STOP_WALKING_H
#define STOP_WALKING_H

#include <cstdint>

namespace OHOS::Multimedia::CameraAgentService::StopWalking {

void PushAcc(int64_t timestampMs, double x, double y, double z);
bool HasStoppedWalking();
void Reset();

} // namespace OHOS::Multimedia::CameraAgentService::StopWalking

#endif // STOP_WALKING_H
