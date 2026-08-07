/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include "BleProvider.h"

#include <chrono>

#include "camera_agent_log.h"

namespace OHOS::Multimedia::CameraAgentService {

BleProvider *BleProvider::GetInstance()
{
    static BleProvider instance;
    return &instance;
}

BleProvider::BleProvider()
{
    bleCallback_ = std::make_shared<BleCallbackWrapper>();
    bleCentralManager_ = std::make_shared<Bluetooth::BleCentralManager>(bleCallback_);
}

BleProvider::~BleProvider() = default;

bool BleProvider::RegisterListener(const BleProviderListener &listener)
{
    if (listener == nullptr || bleCallback_ == nullptr) {
        return false;
    }
    std::lock_guard lock(mutex_);
    return bleCallback_->RegisterListener(listener);
}

bool BleProvider::UnRegisterListener()
{
    if (bleCallback_ == nullptr) {
        return false;
    }
    std::lock_guard lock(mutex_);
    return bleCallback_->UnRegisterListener();
}

void BleProvider::Enable()
{
    std::lock_guard lock(mutex_);
    if (bleCentralManager_ == nullptr) {
        return;
    }
    Bluetooth::BleScanSettings settings;
    settings.SetScanMode(Bluetooth::SCAN_MODE::SCAN_MODE_OP_P50_100_200);
    std::vector<Bluetooth::BleScanFilter> filters;
    filters.emplace_back();
    bleCentralManager_->StartScan(settings, filters);
}

void BleProvider::Disable()
{
    std::lock_guard lock(mutex_);
    if (bleCentralManager_ != nullptr) {
        bleCentralManager_->StopScan();
    }
}

void BleCallbackWrapper::OnScanCallback(const Bluetooth::BleScanResult &result)
{
    const int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    BleData bleData;
    bleData.mac = result.GetPeripheralDevice().GetDeviceAddr();
    bleData.rssi = result.GetRssi();
    bleData.name = result.GetPeripheralDevice().GetDeviceName();
    bleData.timestamp = timestamp;
    if (listener_ != nullptr) {
        listener_(bleData);
    }
}

bool BleCallbackWrapper::RegisterListener(const BleProviderListener &listener)
{
    listener_ = listener;
    return true;
}

bool BleCallbackWrapper::UnRegisterListener()
{
    listener_ = nullptr;
    return true;
}

void BleCallbackWrapper::OnFoundOrLostCallback(const Bluetooth::BleScanResult &result, uint8_t callbackType)
{
    if (callbackType != 0) {
        return;
    }
    BleData bleData;
    const int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    bleData.mac = result.GetPeripheralDevice().GetDeviceAddr();
    bleData.rssi = result.GetRssi();
    bleData.name = result.GetPeripheralDevice().GetDeviceName();
    bleData.timestamp = timestamp;
    if (listener_ != nullptr) {
        listener_(bleData);
    }
}

void BleCallbackWrapper::OnBleBatchScanResultsEvent(const std::vector<Bluetooth::BleScanResult> &results)
{
    if (results.empty() || listener_ == nullptr) {
        return;
    }
    const int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (const auto &result : results) {
        BleData bleData;
        bleData.mac = result.GetPeripheralDevice().GetDeviceAddr();
        bleData.rssi = result.GetRssi();
        bleData.name = result.GetPeripheralDevice().GetDeviceName();
        bleData.timestamp = timestamp;
        listener_(bleData);
    }
}

void BleCallbackWrapper::OnStartOrStopScanEvent(int resultCode, bool isStartScan)
{
    CAMERA_AGENT_LOG_INFO("bleCallbackWrapper::OnStartOrStopScanEvent, resultCode:%{public}d, isStartScan:%{public}d", 
        resultCode, isStartScan);
}

} // namespace OHOS::Multimedia::CameraAgentService
