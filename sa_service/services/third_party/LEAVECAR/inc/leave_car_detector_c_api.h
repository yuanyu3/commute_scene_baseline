#ifndef XDR_XDR_INCLUDE_PDR_STEP_ROTATION_PROVIDER_CINTERFACE_H_
#define XDR_XDR_INCLUDE_PDR_STEP_ROTATION_PROVIDER_CINTERFACE_H_
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LeaveCarHandle LeaveCarHandle;

typedef enum LeaveCarState {
    LEAVE_CAR_STATE_UNKNOWN = 0,
    LEAVE_CAR_STATE_WALKING = 1,
    LEAVE_CAR_STATE_LEAVE_CAR = 2,
} LeaveCarState;

typedef struct LeaveCarDetectorResult {
    int32_t state;
    int32_t is_leave_car;
    int32_t is_walking;
    int32_t last_infer_result;
    long current_timestamp;
    long leave_car_timestamp;
    long start_walk_timestamp;
} LeaveCarDetectorResult;
// 定义回调函数类型
typedef void (*ResultCallback)(const void*);

// 开发流程：
// 1. 本地开发完能编通
// 2. 鸿蒙版本能编通
// 3. 接入hap调试

// 创建 PdrStepRotationProvider 实例

// 1. 入参是给前段返回结果用的
// 2. 里面要做什么 
//    初始化离车识别对象LeaveCarDetector
//    初始化差值器对象SyncedSensorInterpolatorAGM
//    在外侧定义差值器——离车模块的回调，形式为：入参：const InterpedDataPackAGM&，内部调用LeaveCarDetector的PushAccGyroAndInferIsLeaveCar接口
LeaveCarHandle* LeaveCarDetector_Create(ResultCallback on_result_callback);

// Update系列直接接入SyncedSensorInterpolatorAGM的void PushAccData PushGyroData PushMagData接口
void LeaveCarDetector_UpdateAcc(LeaveCarHandle* handle, double x, double y, double z, long timestamp);
void LeaveCarDetector_UpdateGyro(LeaveCarHandle* handle, double x, double y, double z, long timestamp);
void LeaveCarDetector_UpdateMag(LeaveCarHandle* handle, double x, double y, double z, long timestamp);

void LeaveCarDetector_Reset(LeaveCarHandle* handle);

// 参照PDR做析构即可
void LeaveCarDetector_Destroy(LeaveCarHandle* handle);

#ifdef __cplusplus
}
#endif

#endif  // XDR_XDR_INCLUDE_PDR_STEP_ROTATION_PROVIDER_CINTERFACE_H_
