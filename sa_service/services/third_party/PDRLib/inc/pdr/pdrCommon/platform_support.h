/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2019-2019. All rights reserved.
 * Description: Control compilation platform.
 * Author: ������ x00416147
 * Create: 2019-05-14
 */
#ifndef PLATFORM_SUPPORT_H
#define PLATFORM_SUPPORT_H

/*
 * ֧�ֶ�LITOS ��vs2008
 */
// ���Ʊ���ƽ̨ѡ��
#define COMPILE_PLATFORM_STDC    0
#define COMPILE_PLATFORM_ANDROID 2

#define COMPILE_PLATFORM_FLAG    COMPILE_PLATFORM_ANDROID

#if COMPILE_PLATFORM_FLAG == COMPILE_PLATFORM_STDC
// vs2008
#include <stdlib.h>
#include <stdio.h>
#define PRINT_INFO               // printf
#define PRINT_DEBUG              // ���ò���ӡ printf
#define PRINT_WARN               printf  //
#define PRINT_ERR                printf  // ERR level

#define PRINT_VDR_REPLAY
#define PRINT_VDR_INFO
#define PRINT_VDR_WARN           printf
#define PRINT_VDR_DEBUG
#define PRINT_VDR_ERR            printf


#ifdef LOG_TO_FILE
extern FILE *debug_fp[];
#define LOG_FILE(FILE_ID, fmt, ...)                   \
    if (debug_fp[FILE_ID] != NULL) {                  \
        fprintf(debug_fp[FILE_ID], fmt, __VA_ARGS__); \
    }
#else
#define LOG_FILE(...)
#endif

#elif COMPILE_PLATFORM_FLAG == COMPILE_PLATFORM_ANDROID

// Higeo Android��ӡ��װ
#include "../other/higeo_interface_fused_manager.h"
#define PRINT_INFO               HIGEO_PDR_INFO
#define PRINT_DEBUG              HIGEO_PDR_DBG
#define PRINT_WARN               HIGEO_PDR_WNG
#define PRINT_ERR                HIGEO_PDR_ERR
#define PRINT_VDR_REPLAY         HIGEO_VDR_REPLAY_DATA
#define PRINT_VDR_INFO           HIGEO_VDR_INFO
#define PRINT_VDR_DEBUG          HIGEO_VDR_DBG
#define PRINT_VDR_WARN           HIGEO_VDR_WNG
#define PRINT_VDR_ERR            HIGEO_VDR_ERR

#define PRINT_PVT_REPLAY         HIGEO_PVT_REPLAY_DATA
#define PRINT_PVT_INFO           HIGEO_PVT_INFO
#define PRINT_PVT_DEBUG          HIGEO_PVT_DBG
#define PRINT_PVT_WARN           HIGEO_PVT_WNG
#define PRINT_PVT_ERR            HIGEO_PVT_ERR

#define LOG_FILE(...)

#endif

typedef unsigned long long UINT64;
typedef long long INT64;

#endif /* CROSS_PLATFORM_SUPPORT_H */

