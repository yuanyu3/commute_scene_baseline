/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 * Description: ai-pdr mag rotation Fusion.
 * Author: z00838343
 * Create: 2024/12/23
 */

#ifndef PDRTESTMAIN_M_RIEMAMMPDR_INTERFACE_H
#define PDRTESTMAIN_M_RIEMAMMPDR_INTERFACE_H
#include "stdbool.h"
#include "higeo_interface_fused_manager.h"
#include "pdr/riemannPdr/riemann_pdr_pdr_step_rotation_provider_c_api.h"

struct RiemannPdrHandle;
extern RiemannPdrHandle* g_pdrProvider;
#ifdef __cplusplus
extern "C"
{
#endif
void AIPDR_InitLoadRiemann(bool useRv);
int AIPDR_RunRiemannPdr(pdrSensorIn sensor, MiptPdrData *opData, bool useRv);
void AIPDR_ClearHistoryTrajectory();
RiemannPdrHandle* RiemannPdrGetProvider(void);
void RiemannPdrSetProvider(RiemannPdrHandle* provider);
#ifdef __cplusplus
}
#endif
#endif // PDRTESTMAIN_M_RIEMAMMPDR_INTERFACE_H
