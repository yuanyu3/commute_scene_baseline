/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Signal data types (WiFi/BLE/Cell/Baro).
 */

#ifndef SIGNAL_TYPES_H
#define SIGNAL_TYPES_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace OHOS::Multimedia::CameraAgentService {

enum WifiErrorCode : int32_t {
    WIFI_INVALID = -2,
    WIFI_FAILED = -1,
    WIFI_SUCCESS = 0,
};

struct WifiAp {
    int64_t timestamp = 0;
    std::string bssid;
    std::string ssid;
    int32_t rssi = 0;
    int32_t freq = 0;
};

struct WifiFp {
    int64_t timestamp = 0;
    std::vector<WifiAp> aps;
};

struct BleData {
    int64_t timestamp = 0;
    int32_t rssi = -120;
    std::string mac;
    std::string name;
};

enum CellType {
    CELL_TYPE_NONE = 0,
    GSM,
    CDMA,
    W_CDMA,
    TDS_CDMA,
    LTE,
    NR,
};

struct CellInfo {
    CellType celltype = LTE;
    int64_t timestamp = 0;
    int64_t cellId = 0;
    int32_t signalIntensity = 0;
    int32_t signalLevel = 0;
    std::string mcc;
    std::string mnc;
    virtual ~CellInfo() = default;

    std::string TypeToString() const
    {
        switch (celltype) {
            case GSM: return "gsm";
            case CDMA: return "cdma";
            case TDS_CDMA: return "tds_cdma";
            case W_CDMA: return "w_cdma";
            case LTE: return "lte";
            case NR: return "nr";
            default: return "unknown";
        }
    }
};

struct CellNr : public CellInfo {
    int32_t nrArfcn = 0;
    int32_t pci = 0;
    int32_t tac = 0;
    int64_t nci = 0;
};

struct CellLte : public CellInfo {
    int32_t pci = 0;
    int32_t tac = 0;
    int32_t earfcn = 0;
};

struct CellWcdma : public CellInfo {
    int32_t lac = 0;
    int32_t psc = 0;
    int32_t uarfcn = 0;
};

struct CellTdsCdma : public CellInfo {
    int32_t lac = 0;
    int32_t cpid = 0;
    int32_t uarfcn = 0;
};

struct CellCdma : public CellInfo {
    int32_t baseId = 0;
    int32_t latitude = 0;
    int32_t longitude = 0;
    int32_t nid = 0;
    int32_t sid = 0;
};

struct CellGsm : public CellInfo {
    int32_t lac = 0;
    int32_t bsic = 0;
    int32_t arfcn = 0;
};

struct BaroFrame {
    int64_t ts = 0;
    float pressure = 0.0f;
};

} // namespace OHOS::Multimedia::CameraAgentService

#endif // SIGNAL_TYPES_H
