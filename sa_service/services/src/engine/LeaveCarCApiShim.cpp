/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Weak fallback when prebuilt libleave_car.so was built without LeaveCarDetector_Reset.
 * If the .so exports a strong symbol, the linker uses that instead.
 */

#include "leave_car_detector_c_api.h"

extern "C" __attribute__((weak)) void LeaveCarDetector_Reset(LeaveCarHandle *handle)
{
    (void)handle;
}
