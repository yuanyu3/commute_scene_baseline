/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include "WifiProvider.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "camera_agent_log.h"

namespace OHOS::Multimedia::CameraAgentService {

WifiProvider *WifiProvider::GetInstance()
{
    static WifiProvider instance;
    return &instance;
}

bool WifiProvider::RegisterListener(const WifiProviderListener &listener)
{
    std::lock_guard lock(mutex_);
    wifiScanEventCallback_.OnWifiScanStateChanged = WifiProvider::OnWifiScanStateChanged;
    if (RegisterWifiEvent(&wifiScanEventCallback_) != 0) {
        CAMERA_AGENT_LOG_ERROR("RegisterWifiEvent failed");
        return false;
    }
    listener_ = listener;
    return true;
}

bool WifiProvider::UnRegisterListener()
{
    std::lock_guard lock(mutex_);
    if (UnRegisterWifiEvent(&wifiScanEventCallback_) != 0) {
        CAMERA_AGENT_LOG_ERROR("UnRegisterWifiEvent failed");
        return false;
    }
    listener_ = {};
    return true;
}

void WifiProvider::Enable()
{
    std::lock_guard lock(mutex_);
    if (running_) {
        return;
    }
    running_ = true;
    triggerThread_ = std::thread([this] {
        while (running_) {
            std::shared_ptr<Wifi::WifiScan> scan = Wifi::WifiScan::GetInstance(WIFI_SCAN_ABILITY_ID);
            if (scan != nullptr) {
                scan->Scan();
            }
            int64_t intervalMs = scanIntervalMs_.load();
            if (intervalMs < 100) {
                intervalMs = 100;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    });
    triggerThread_.detach();
}

void WifiProvider::Disable()
{
    std::lock_guard lock(mutex_);
    running_ = false;
}

void WifiProvider::SetScanIntervalMs(int64_t intervalMs)
{
    if (intervalMs < 100) {
        intervalMs = 100;
    }
    scanIntervalMs_.store(intervalMs);
}

int64_t WifiProvider::GetScanIntervalMs() const
{
    return scanIntervalMs_.load();
}

void WifiProvider::OnWifiReceived(WifiFp wifiFp)
{
    if (listener_ == nullptr) {
        return;
    }
    listener_(WIFI_SUCCESS, wifiFp);
}

void WifiProvider::OnWifiScanStateChanged(int32_t state, int32_t size)
{
    (void)state;
    (void)size;
    std::vector<Wifi::WifiScanInfo> wifiScanInfoVec;
    std::shared_ptr<Wifi::WifiScan> ptrWifiScan = Wifi::WifiScan::GetInstance(WIFI_SCAN_ABILITY_ID);
    if (ptrWifiScan == nullptr) {
        return;
    }
    if (ptrWifiScan->GetScanInfoList(wifiScanInfoVec) != Wifi::WIFI_OPT_SUCCESS || wifiScanInfoVec.empty()) {
        return;
    }
    const int64_t curTs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    WifiFp wifiFp;
    int64_t latestApTs = -1;
    for (const auto &info : wifiScanInfoVec) {
        WifiAp ap;
        ap.timestamp = info.timestamp;
        ap.bssid = info.bssid;
        ap.ssid = info.ssid;
        ap.rssi = info.rssi;
        ap.freq = info.frequency;
        ap.ssid.erase(std::remove(ap.ssid.begin(), ap.ssid.end(), '\"'), ap.ssid.end());
        if (ap.timestamp > latestApTs) {
            latestApTs = ap.timestamp;
        }
        wifiFp.aps.push_back(ap);
    }
    const int64_t deltaT = curTs - latestApTs;
    wifiFp.timestamp = curTs;
    for (auto &ap : wifiFp.aps) {
        ap.timestamp += deltaT;
    }
    GetInstance()->OnWifiReceived(wifiFp);
}

} // namespace OHOS::Multimedia::CameraAgentService
