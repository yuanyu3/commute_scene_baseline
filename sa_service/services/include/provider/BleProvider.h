/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: BLE scan provider.
 */

#ifndef BLE_PROVIDER_H
#define BLE_PROVIDER_H

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "SignalTypes.h"
#include "bluetooth_ble_central_manager.h"

namespace OHOS::Multimedia::CameraAgentService {

class BleCallbackWrapper;
using BleProviderListener = std::function<void(const BleData &bleData)>;

class BleProvider final {
public:
    static BleProvider *GetInstance();
    bool RegisterListener(const BleProviderListener &listener);
    bool UnRegisterListener();
    void Enable();
    void Disable();

private:
    BleProvider();
    ~BleProvider();
    std::mutex mutex_;
    std::shared_ptr<BleCallbackWrapper> bleCallback_ { nullptr };
    std::shared_ptr<Bluetooth::BleCentralManager> bleCentralManager_ { nullptr };
};

class BleCallbackWrapper : public Bluetooth::BleCentralManagerCallback {
public:
    void OnScanCallback(const Bluetooth::BleScanResult &result) override;
    bool RegisterListener(const BleProviderListener &listener);
    bool UnRegisterListener();
    void OnFoundOrLostCallback(const Bluetooth::BleScanResult &result, uint8_t callbackType) override;
    void OnBleBatchScanResultsEvent(const std::vector<Bluetooth::BleScanResult> &results) override;
    void OnStartOrStopScanEvent(int resultCode, bool isStartScan) override;

private:
    BleProviderListener listener_;
};

} // namespace OHOS::Multimedia::CameraAgentService

#endif // BLE_PROVIDER_H
