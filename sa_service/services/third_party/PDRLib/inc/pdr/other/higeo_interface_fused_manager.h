/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 * Description: ai-pdr mag rotation Fusion.
 * Author: z00838343
 * Create: 2024/12/23
 */
#ifndef PDRTESTFORXW_HIGEO_INTERFACE_FUSED_MANAGER_H
#define PDRTESTFORXW_HIGEO_INTERFACE_FUSED_MANAGER_H
#include "stdint.h"
#ifdef _WIN32
#include "corecrt_stdio_config.h"
#endif
#include "pdr/pdrCommon/platform_support.h"
#include "pdr_fusion_interface.h"

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned long long uint64;

typedef char int8;
typedef short int16;
typedef int int32;
typedef long long int64;

typedef float float32;
typedef double float64;

#define NULL_VAL 0LL

#ifndef _higeo_sensors_gyro_data
#define _higeo_sensors_gyro_data
typedef struct _higeo_sensors_gyro_data {
    int64 timestamp; // ms
    float32 x;
    float32 y;
    float32 z;
    float32 temperature;
} higeo_sensors_gyro_data_stru;
#endif

#ifndef _higeo_sensors_accel_data
#define _higeo_sensors_accel_data
typedef struct _higeo_sensors_accel_data {
    int64 timestamp; // ms
    float32 x;
    float32 y;
    float32 z;
} higeo_sensors_accel_data_stru;
#endif

#ifndef _higeo_mag_data
#define _higeo_mag_data
typedef struct _higeo_mag_data {
    int64 timestamp; // ms
    float32 x;
    float32 y;
    float32 z;
    uint32 us_accuracy_flag; // status
} higeo_mag_data_stru;
#endif

typedef struct _higeo_uncal_mag_data {
    int64 timestamp; // ms
    float32 uncal_x;
    float32 uncal_y;
    float32 uncal_z;
    float32 bias_x;
    float32 bias_y;
    float32 bias_z;
} higeo_uncal_mag_data_stru;

#ifndef _higeo_uncal_gyro_bias
#define _higeo_uncal_gyro_bias
typedef struct _higeo_uncal_gyro_bias {
    int64 timestamp; // ms
    float32 uncal_x;
    float32 uncal_y;
    float32 uncal_z;
    float32 bias_x;
    float32 bias_y;
    float32 bias_z;
} higeo_uncal_gyro_bias_stru;
#endif

#define AVG_MEMS_BUF_COUNT_MAX 160

#ifndef _higeo_fm_sens_acc_buf
#define _higeo_fm_sens_acc_buf
typedef struct _higeo_fm_sens_acc_buf {
    higeo_sensors_accel_data_stru acc_buf[AVG_MEMS_BUF_COUNT_MAX];
    uint32 buf_count;
    uint32 valid;
} higeo_fm_sens_acc_buf_stru;
#endif

#ifndef _higeo_fm_sens_gyro_buf
#define _higeo_fm_sens_gyro_buf
typedef struct _higeo_fm_sens_gyro_buf {
    higeo_sensors_gyro_data_stru gyro_buf[AVG_MEMS_BUF_COUNT_MAX];
    uint32 buf_count;
    uint32 valid;
    uint32 isTimeStampValid;
} higeo_fm_sens_gyro_buf_stru;
#endif

#ifndef _higeo_fm_sens_uncal_gyro_buf
#define _higeo_fm_sens_uncal_gyro_buf
typedef struct _higeo_fm_sens_uncal_gyro_buf {
    higeo_uncal_gyro_bias_stru uncal_gyro_buf[AVG_MEMS_BUF_COUNT_MAX];
    uint32 buf_count;
    uint32 valid;
} higeo_fm_sens_uncal_gyro_buf_stru;
#endif

#ifndef _higeo_fm_sens_mag_buf
#define _higeo_fm_sens_mag_buf
typedef struct _higeo_fm_sens_mag_buf {
    higeo_mag_data_stru mag_buf[AVG_MEMS_BUF_COUNT_MAX];
    uint32 buf_count;
    uint32 valid;
} higeo_fm_sens_mag_buf_stru;
#endif

#ifndef _higeo_fm_sens_uncal_mag_buf
#define _higeo_fm_sens_uncal_mag_buf
typedef struct _higeo_fm_sens_uncal_mag_buf {
    higeo_uncal_mag_data_stru uncal_mag_buf[AVG_MEMS_BUF_COUNT_MAX];
    uint32 buf_count;
    uint32 valid;
} higeo_fm_sens_uncal_mag_buf_stru;
#endif

typedef struct _higeo_sensors_rv_data {
    int64 timestamp;
    float32 x;
    float32 y;
    float32 z;
    float32 w;
} higeo_sensors_rv_data_stru;

typedef struct _higeo_fm_sens_rv_buf {
    higeo_sensors_rv_data_stru rv_buf[AVG_MEMS_BUF_COUNT_MAX];
    uint32 buf_count;
    uint32 valid;
} higeo_fm_sens_rv_buf_stru;

typedef struct _higeo_pvt_info {
    // GNSS UTC timestamp, Milliseconds since January 1, 1970
    int64 utc_time;
    // longitude, in degree
    double lon;
    // latitude, in degree
    double lat;
    // altitude, in meters
    double alt;
    // GNSS accuracy in meters, its called horizontal_pos_unc in 1102
    float accuracy;
    // GNSS speed in m/s
    float speed;
    // GNSS heading in degrees, its called yaw in 1102
    float heading;
    // bitmask, bit 0: GNSS, bit 1: sensor, bit2: WiFi, bit3: MM, bit4: G+P, bit5: G+V, bit6: G+W, bit7: QTTFF/NLP
    uint16 source_type;
    // bitmask, 0x0001:lat_lon, 0x0002:alt, 0x0004:speed, 0x0008:heading, 0x0010:accuracy
    uint16 flags;
    // speed Unc
    float speedUnc;
    // clock bias
    double clockBias;
    // clock drift
    double clockDrift;
} higeo_pvt_info_stru;

typedef struct pdrSensorIn {
    higeo_fm_sens_acc_buf_stru *accBufStru;
    higeo_fm_sens_gyro_buf_stru *gyroBufStru;
    higeo_fm_sens_mag_buf_stru *magBufStru;
    higeo_fm_sens_uncal_mag_buf_stru *uncalMagBufStru;
    higeo_fm_sens_rv_buf_stru *rvStru;
} pdrSensorIn;

#define GNSS_LOG_TYPE_HIGEO     3
#define HIGEO_LOG higeo_log_callback
typedef void (*HIGEO_LOG)(uint8 uc_type, const int8 *ac_tag, const int8 *ac_level, const int8 *ac_file_num,
                          int32 l_line_num, const int8 *ac_log_info, ...);
extern HIGEO_LOG g_p_log_cb;
#define COMMON_LOG(type, tag, level, fmt, args...) g_p_log_cb(type, tag, level, "replay", 0, fmt, ##args)

#define HIGEO_MNGR_DBG(fmt, arg...) COMMON_LOG(GNSS_LOG_TYPE_HIGEO, "HIGEO_MNGR", "D", fmt, ##arg)
#define HIGEO_MNGR_INFO(fmt, arg...) COMMON_LOG(GNSS_LOG_TYPE_HIGEO, "HIGEO_MNGR", "I", fmt, ##arg)
#define HIGEO_MNGR_WNG(fmt, arg...) COMMON_LOG(GNSS_LOG_TYPE_HIGEO, "HIGEO_MNGR", "W", fmt, ##arg)
#define HIGEO_MNGR_ERR(fmt, arg...) COMMON_LOG(GNSS_LOG_TYPE_HIGEO, "HIGEO_MNGR", "E", fmt, ##arg)
#define HIGEO_MNGR_REPLAY_DATA(fmt, arg...) COMMON_LOG(GNSS_LOG_TYPE_HIGEO, NULL_VAL, "DATA", fmt, ##arg)

#define HIGEO_PDR_DBG(fmt, arg...) COMMON_LOG(GNSS_LOG_TYPE_HIGEO, "HIGEO_PDR", "D", fmt, ##arg)
#define HIGEO_PDR_INFO(fmt, arg...) COMMON_LOG(GNSS_LOG_TYPE_HIGEO, "HIGEO_PDR", "I", fmt, ##arg)
#define HIGEO_PDR_WNG(fmt, arg...) COMMON_LOG(GNSS_LOG_TYPE_HIGEO, "HIGEO_PDR", "W", fmt, ##arg)
#define HIGEO_PDR_ERR(fmt, arg...) COMMON_LOG(GNSS_LOG_TYPE_HIGEO, "HIGEO_PDR", "E", fmt, ##arg)
#define HIGEO_PDR_REPLAY_DATA(fmt, arg...) COMMON_LOG(GNSS_LOG_TYPE_HIGEO, NULL_VAL, "DATA", fmt, ##arg)

void gnss_log_message(uint8 uc_type, const int8* ac_tag, const int8* ac_level,
                      const int8* ac_file_name, int32 l_line_num, const int8* ac_log_info, ...);


#endif // PDRTESTFORXW_HIGEO_INTERFACE_FUSED_MANAGER_H
