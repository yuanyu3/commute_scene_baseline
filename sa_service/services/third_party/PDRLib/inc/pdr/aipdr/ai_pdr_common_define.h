/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ai-pdr interface.
 * Author: z00838343
 * Create: 2024/11/25
 */

#ifndef HIGEO50_AI_PDR_COMMON_DEFINE_H
#define HIGEO50_AI_PDR_COMMON_DEFINE_H

namespace AIPDR {

constexpr int TWO = 2;
constexpr int THREE = 3;
constexpr int FOUR = 4;
constexpr int FIVE = 5;
constexpr int SIX = 6;
constexpr int NINE = 9;
constexpr int TEN = 10;
#define THOUSAND 1000
#define MS_2_S 1000                // 毫秒转秒
#define AIPDR_SENSOR_LOSS_MAX 350 // 最多断掉350ms以内数据
#define AIPDR_SENSOR_CNT_MIN 10    // SENSOR数量最少10个
#define AIPDR_TIME_INTERVAL_MIN 900
#define AIPDR_TIME_INTERVAL_MAX 1100
#define SENSOR_INTERFACE_GRAVITY 9.8f
#define SENEOR_INTERVAL 0.01f
#define AIPDR_MISTAKE_VALUE 0.00001f          // 计算机运算误差
#define AIPDR_HISTORY_PUREPDR_INFO_LENGTH 20  // 最多保存数据
#define AIPDR_HISTORY_PVT_INFO_LENGTH 30
#define AIPDR_HISTORY_RTK_INFO_LENGTH 30
#define AIPDR_KF_INVALID 0       // KF无效标识
#define AIPDR_PVT_VALID 31       // pvt正常的标识
#define AIPDR_KF_GOOD_ACC_MAX 5  // kf acc较好阈值
#define AIPDR_PVT_JUMP_MAX 50    // pvt跳点最大值
#define AIPDR_RTK_MAX_ACC 10     // rtk解有效的acc值上限
#define QUARTER_FULL_DEGREE 90.0f

// 纯惯部分定义
constexpr int PREDICT_WINDOW_LEN = 100;    // every window has 100 sensor data
constexpr int SENSOR_BUFF_LEN = 600;       // sensor buffer size is 600
#define MODEL_BUFFER_LEN 4700000  // model buffer 4.5M
#define ONE_MILLISECOND 1000      // 1s = 1000ms
#define TIME_DELTA_THRESHOLD 5    // timestamp差异阈值 5ms
#define INFERENCE_STEP_TIME 100   // inference time 100ms
#define SENSOR_TIME 10            // 10 ms
#define SENSOR_STEP 10            // update 10 sensor data，10组IMU，共100ms
#define SLIDING_COUNT 10          // 滑窗10次，步进100ms，共1s
#define PREDICT_TIME 1000         // at least update 1000ms sensor data to predict
#define INIT_TIME 4000            // at least update 4000ms sensor data to initialize
#define BUFFER_READ_LEN 1024
// AIPDR错误码部分
constexpr int AIPDR_OK = 1;               // 正常
constexpr int AIPDR_ERR = 0;              // 未知错误
constexpr int AIPDR_ERR_INPUTNULL = 10;   // 输入指针参数为空
constexpr int AIPDR_STORE_SENSOR = 11;    // sensor数据不足（这个不是错误！）
constexpr int AIPDR_ERR_INIT_REATT = 12;  //  Reatt矩阵初始化错误（无法通过换缓存的sensor预估手机姿态）

constexpr int AIPDR_ERR_HISTORY_SENEOR_INVAILD = 14;  // 缓存历史sensor异常（通常为sensor时间不连续）
constexpr int AIPDR_ERR_PREPARE_SENEOR_FAIL = 15;     // 预处理sensro失败
constexpr int AIPDR_ERR_GEN_FEATURE = 51;             // 滑窗数据量不足100个
constexpr int AIPDR_ERR_EXEC_MODEL = 52;              // ai模型推理失败（后续还需细分）

constexpr int AIPDR_ERR_MODEL_FILE_MISSING = 61;  // 模型文件加载失败（找不到模型文件）

constexpr int AIPDR_ERR_RIEMANN_NO_RES = 81;  // 黎曼PDR没更新结果
constexpr int AIPDR_ERR_RIEMANN_MODEL_FAIL = 82;  // 黎曼PDR模型推理失败

constexpr int AIPDR_ERR_MALLOC_FAIL = 91;  // 安全函数报错malloc (申请内存失败)
constexpr int AIPDR_ERR_MEMSET_FAIL = 92;  // 安全函数报错memset_s
constexpr int AIPDR_ERR_MEMCPY_FAIL = 93;  // 安全函数报错memcpy_s

constexpr int FEATURE_DIM = 6;                                // 3 acc + 3 gyro
constexpr int FEATURE_LEN = FEATURE_DIM * PREDICT_WINDOW_LEN;  // feature length for model is 600, 6*100Hz
constexpr int DIM_3 = 3;

}

#endif  // HIGEO50_AI_PDR_COMMON_DEFINE_H
