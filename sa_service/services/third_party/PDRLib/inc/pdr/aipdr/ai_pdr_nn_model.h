/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: mipt pdr sensor process.
 * Author: w00623447
 * Create: 2023-02-01
 */

#ifndef CORE_AI_PDR_NN_MODEL_H
#define CORE_AI_PDR_NN_MODEL_H
#include <stdio.h>

namespace AIPDR {

#define RUN_THREAD_NUM 2
#define MS_AFF_MODE_SMALL 2
constexpr size_t MS_PDR_OUTPUT_SIZE_1 = 2;  // 3 0906DEBUG 小模型为2
constexpr size_t MS_HANDLE_NUM = 1;         // 0906DEBUG 小模型为1

constexpr int RIEMANN_PDR_FREQ = 200;
constexpr int RIEMANN_PDR_ALEX = 6;
constexpr int RIEMANN_PDR_INPUT_LEN = 1200; // 1 * 6 * 200 * 1 = 1200

bool ExecuteAiPdrModel(float *featureInput, float *result);
bool LoadAiPdrModel(char *modelBuffer, size_t total);
void ReleaseAiPdrModel();
void ReleaseAiPdrContext();

bool ExecuteRiemannPdrModel(float *featureInput, float *result, int featureLen);
bool LoadRiemannPdrModel(const char *modelBuffer, size_t total);
void ReleaseRiemannPdrModel();
void ReleaseRiemannPdrContext();

}

#endif  // CORE_AI_PDR_NN_MODEL_H
