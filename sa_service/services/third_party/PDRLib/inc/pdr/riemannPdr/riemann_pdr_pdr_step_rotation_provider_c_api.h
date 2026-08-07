/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: riemann-pdr model.
 * Author: z00838343
 * Create: 2025/5/14
 */
#ifndef XDR_XDR_INCLUDE_PDR_STEP_ROTATION_PROVIDER_CINTERFACE_H_
#define XDR_XDR_INCLUDE_PDR_STEP_ROTATION_PROVIDER_CINTERFACE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RiemannPdrHandle RiemannPdrHandle;
constexpr int IDX_FOUR = 4;

typedef struct {
    long timestamp_ms;
    double lat_deg;
    double lon_deg;
    float accuracy_m;
    float heading_deg;
    uint8_t valid;
} RiemannGpsSample;

typedef struct {
    double east_m;
    double north_m;
    double lat_deg;
    double lon_deg;
    long timestamp_ms;
    uint32_t status;
} RiemannAbsPosition;

typedef void (*ResultCallback)(const void *);
typedef void (*OnPdrResultCallback)(const void *);
/** GPS 融合产生有效绝对位姿时回调，参数为 const RiemannAbsPosition * */
typedef void (*OnAbsPositionCallback)(const void *);

RiemannPdrHandle *RiemannPdrCreate(ResultCallback onResultCallback, bool useRv);
/** 返回 AIPDR_InitLoadRiemann 创建的全局实例；未初始化时返回 nullptr */
RiemannPdrHandle *RiemannPdrGetProvider(void);
void RiemannPdrDestroy(RiemannPdrHandle *handle);

void RiemannPdrUpdateAcc(RiemannPdrHandle *handle, double x, double y, double z, long timestamp);
void RiemannPdrUpdateGyro(RiemannPdrHandle *handle, double x, double y, double z, long timestamp);
void RiemannPdrUpdateMag(RiemannPdrHandle *handle, double x, double y, double z, long timestamp);
void RiemannPdrUpdateRv(RiemannPdrHandle *handle, double val[IDX_FOUR], long timestamp);
void RiemannPdrSetMagCalibStrategy(RiemannPdrHandle *handle, int type);
/** 仅清空传感器插值缓冲（传感器断流等场景） */
void RiemannPdrResetCache(RiemannPdrHandle *handle);
/** 清空整段步行 session：PDR 轨迹/窗口/姿态 + GPS 融合，不销毁实例与模型 */
void RiemannPdrResetSession(RiemannPdrHandle *handle);
void RiemannPdrSetOnPdrResultCallback(RiemannPdrHandle *handle, OnPdrResultCallback cb);
void RiemannPdrSetOnAbsPositionCallback(RiemannPdrHandle *handle, OnAbsPositionCallback cb);

void RiemannPdrUpdateGps(RiemannPdrHandle *handle, const RiemannGpsSample *gps);
int RiemannPdrGetAbsPosition(RiemannPdrHandle *handle, RiemannAbsPosition *out);
void RiemannPdrResetFusion(RiemannPdrHandle *handle);

#ifdef __cplusplus
}
#endif

#endif  // XDR_XDR_INCLUDE_PDR_STEP_ROTATION_PROVIDER_CINTERFACE_H_
