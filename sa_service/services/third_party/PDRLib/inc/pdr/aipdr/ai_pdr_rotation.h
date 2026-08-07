/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ai-pdr mag rotation Fusion.
 * Author: g00848304
 * Create: 2024/12/23
 */

#ifndef CORE_AI_PDR_ROTATION_H
#define CORE_AI_PDR_ROTATION_H

#include <vector>
#include <string>

#include "higeo_interface_fused_manager.h"
#include "pdr_common_interface.h"
#include "interface.h"

using namespace std;

enum MotionStatus {
    STAY = 0,
    MOVE,
    MAGS_ABNORMAL,
    OFF_TRAJECTORY,
};

// 获取磁结果
int GetGeomag(const std::string& magDbPath, double lon, double lat);
// 磁融合
void CreatePdrResult(const MiptPdrData* miptPdrData);
void UpdateIsEleUpTheta(bool isEleUpTheta, long long utcTime);

void UpdateEscalatorLength(pair<bool, double>elvaResult);

typedef struct {
    double pX;
    double pY;
    double pZ;
    long timestamp;
} Position;

typedef struct {
    double vx;
    double vy;
    double cx;
    double cy;
    double rotationTheta; // 角度
    long long utcTime; // ms
    MotionStatus motionStatus = MOVE;
    Position pos;
} PdrResult;

#endif // CORE_AI_PDR_ROTATION_H
