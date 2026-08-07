/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2017-2019. All rights reserved.
 * Description: Definition of Calling algorithm,Structure and entry function.
 * Author: 杨伟君 y00295064
 * Create: 2017-03-14
 * History: 2015-02-26 杨伟君 y00295064 Modify comment
 */
#ifndef __PDRAPI_H__
#define __PDRAPI_H__

#include "pdr_rename_function.h"
// version date 20151019
/* *******************************
用户个人信息操作定义:
******************************* */
#ifndef _userInformationTypeDef
#define _userInformationTypeDef
typedef struct {
    /* 用户的体重，千克为单位 */
    unsigned short weight;
    /* 用户的身高，厘米为单位 */
    unsigned short height;
    /* 用户的年龄，年为单位 */
    unsigned char age;
    /* 用户的性别，1=男，0 = 女，2 = 未知 */
    unsigned char sex;
    /* 步长校准系数，产品可根据wifi或者GPS获取步长,在PDR算法里对步长进行校准 */
    float calibrate_step_length;
    /* 校准行人前进方向，产品可根据wifi或者GPS获取行人航向，
     * 在PDR算法里对行人航向进行校准，单位度，范围0~360度，朝东向为0度，逆时针逐渐增加
     */
    float calibrate_direction;
} userInformationTypeDef;
#endif

/* **********************************
加速度数据结构
********************************** */
#ifndef _accRawDataTypeDef
#define _accRawDataTypeDef
typedef struct {
    float x_acc_raw_data;
    float y_acc_raw_data;
    float z_acc_raw_data;
} accRawDataTypeDef;
#endif

/* **********************************
陀螺仪数据结构
********************************** */
#ifndef _gyroRawDataTypeDef
#define _gyroRawDataTypeDef
typedef struct {
    float x_gyro_raw_data;
    float y_gyro_raw_data;
    float z_gyro_raw_data;
} gyroRawDataTypeDef;
#endif

/* **********************************
磁力计数据结构
********************************** */
#ifndef _magnoRawDataTypeDef
#define _magnoRawDataTypeDef
typedef struct {
    float x_magn_raw_data;
    float y_magn_raw_data;
    float z_magn_raw_data;
    unsigned short accuracy_flag;
} magnoRawDataTypeDef;
#endif

/* **********************************
九轴原始数据结构
********************************** */
#ifndef _sensorRawDataTypeDef
#define _sensorRawDataTypeDef
typedef struct {
    accRawDataTypeDef *acc_buf;    /* 加速度原始数据，单位为g */
    gyroRawDataTypeDef *gyro_buf;  /* 陀螺仪原始数据，单位为rad/s */
    magnoRawDataTypeDef *magn_buf; /* 磁力计原始数据，单位为uT */
} sensorRawDataTypeDef;
#endif

/* *******************************
上报结果定义:
******************************* */
#ifndef _pdrResultTypeDef
#define _pdrResultTypeDef
typedef struct {
    /* 步数信息 */
    unsigned int step_count;
    /* 总步行距离 */
    float total_distance;
    /* 坐标 */
    float location_x;
    float location_y;
    float location_z;
    /* 当前行走方向，单位度 */
    float speed; // cm/s
    float current_direction;
    unsigned int accuracy_flag;
} pdrResultTypeDef;
#endif
#ifdef __cplusplus
extern "C"
{
#endif
// FUNCTIONS
/* *****************************************
算法初始化，在算法运算结果之前调用
***************************************** */
unsigned char PdrAlgInit(userInformationTypeDef *user_info);

/* *****************************************
算法运算函数，输入数据获得结果
***************************************** */
unsigned char PdrAlgDataProcess(sensorRawDataTypeDef *data_buffer, unsigned short data_length, float dt);

unsigned char GetPdrResults(pdrResultTypeDef *rst);
#ifdef __cplusplus
}
#endif
#endif