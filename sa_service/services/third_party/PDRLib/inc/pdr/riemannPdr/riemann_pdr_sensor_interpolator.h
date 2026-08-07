/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: riemann-pdr model.
 * Author: z00838343
 * Create: 2025/5/14
 */
#ifndef XDR_XDR_INCLUDE_SENSOR_INTERPOLATOR_H_
#define XDR_XDR_INCLUDE_SENSOR_INTERPOLATOR_H_

#include <map>
#include <functional>
#include <vector>
#include <deque>
#include <mutex>
#include <type_traits>
#include "riemann_pdr_common.h"

namespace AIPDR {
constexpr long XDR_SAMPLING_PEROID_MS = 5;
constexpr size_t XDR_OFFLOAD_SIZE = 20;
namespace XDR {
struct InterpedDataPack {
    std::vector<FxyzInfo> accs;
    std::vector<FxyzInfo> gyros;
    std::vector<FxyzInfo> mags;
    std::vector<FrotInfo> grvs;
    std::vector<FrotInfo> rvs;
};

enum class DataType { XYZ, ROT };

struct UnifiedData {
    long timestamp = 0;
    DataType type = DataType::XYZ;
    std::vector<double> data;
    UnifiedData();
    explicit UnifiedData(const FxyzInfo &input);
    explicit UnifiedData(const FrotInfo &input);
    FxyzInfo ToFxyzInfo();
    FrotInfo ToFrotInfo();
};

class RiemannPdrSensorInterpolator {
public:
    RiemannPdrSensorInterpolator(
        std::function<void(const InterpedDataPack &)> cb, const std::vector<SensorType> &sensors);
    bool IsTimestampInRange(long time_cursor, const std::deque<UnifiedData> &seq) const;
    void RemoveExtraPrevs(std::deque<UnifiedData> &seq) const;

    void PushAccData(const FxyzInfo &data);
    void PushGyroData(const FxyzInfo &data);
    void PushMagData(const FxyzInfo &data);
    void PushGrvData(const FrotInfo &data);
    void PushRvData(const FrotInfo &data);

    void BatchLoadAndInterp(const InterpedDataPack &rawData);
    void ResetCache();
    const std::deque<UnifiedData>& GetRvBuffer() const { return m_rvBuffer; }

private:
    void PushData(SensorType type, const UnifiedData &data);
    void PushDataRv(const UnifiedData &data);
    bool IsCursorInited() const;
    void InitCursor();
    void TryInitCursor();
    bool CanInterp();
    void RemoveExtraData();
    void Interp();
    void Offload();
    void LogInterpedDataPack(const InterpedDataPack& data_pack);
    bool IsInvalidRotation(const UnifiedData& data) const;
    void OffloadSingle(
        SensorType sensorType, std::deque<UnifiedData> &interpedDataQueue, InterpedDataPack &dataPack) const;
    long m_timeCursor = -1;
    bool m_cursorInitialized = false;
    std::deque<UnifiedData> m_rvBuffer;
    int m_rvCount = 0;
    std::map<SensorType, std::deque<UnifiedData>> m_sensorData;
    std::map<SensorType, std::deque<UnifiedData>> m_interpedSensorData;

    std::function<void(const InterpedDataPack &)> m_callback;
};

}  // namespace XDR
}

#endif  // XDR_XDR_INCLUDE_SENSOR_INTERPOLATOR_H_
