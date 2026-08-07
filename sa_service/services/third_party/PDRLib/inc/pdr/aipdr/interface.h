//
// Created by z00838343 on 2025/1/2.
//

#ifndef PDRTESTFORXW_INTERFACE_H
#define PDRTESTFORXW_INTERFACE_H
#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif
#ifdef __cplusplus
extern "C"
{
#endif
#ifndef _pdr_quat_stu
#define _pdr_quat_stu
typedef struct _pdr_quat_stu {
    double data[100][4];
    int len;
} pdr_quat_stu;
#endif
#ifndef _pdr_res_result_out_stu
#define _pdr_res_result_out_stu
typedef struct {
    double vx; // 原始x方向速度
    double vy; // 原始y方向速度
    double cx; // 磁融合后当秒位置x，基于初始点的偏移
    double cy; // 磁融合后当秒位置y，基于初始点的偏移
    double rotationTheta; // 相较于原始轨迹的磁旋转修正角
    long long utcTime; // ms
} PdrResultOut;
#endif
//-*********** 对外提供C接口 ***********-//
DLLEXPORT void Reset();

DLLEXPORT void AddAcc(long time, float x, float y, float z);
DLLEXPORT void AddGyro(long time, float x, float y, float z);
DLLEXPORT void AddMag(long time, float x, float y, float z);
DLLEXPORT void AddRv(long time, float w, float x, float y, float z);
DLLEXPORT int RunOne();
DLLEXPORT float GetX();
DLLEXPORT float GetY();
DLLEXPORT void ReleasePdr();
DLLEXPORT void InitPDR(const char *outputPath);
DLLEXPORT void SetModelPath(const char *inputModel, long inputLen);
DLLEXPORT void InitMagModel(const char *logFolderPath, double lon, double lat);
// 获取100hz实时I->N/S系姿态数据[boottime, roll(gamma), pitch(theta), yaw(psi)]
DLLEXPORT PdrResultOut GetResAfterMag();
#ifdef __cplusplus
}
#endif

#endif //PDRTESTFORXW_INTERFACE_H
