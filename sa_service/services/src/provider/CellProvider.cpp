/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include "CellProvider.h"

#include <chrono>
#include <thread>

#include "camera_agent_log.h"
#include "core_service_client.h"
#include "telephony_errors.h"

namespace OHOS::Multimedia::CameraAgentService {

CellProvider *CellProvider::GetInstance()
{
    static CellProvider instance;
    return &instance;
}

bool CellProvider::RegisterListener(const CellProviderListener &listener)
{
    if (listener == nullptr) {
        return false;
    }
    std::lock_guard lock(mutex_);
    listener_ = listener;
    return true;
}

bool CellProvider::UnRegisterListener()
{
    std::lock_guard lock(mutex_);
    listener_ = nullptr;
    return true;
}

void CellProvider::Enable()
{
    std::lock_guard lock(mutex_);
    if (running_) {
        return;
    }
    running_ = true;
    triggerThread_ = std::thread([this] {
        while (running_) {
            Operation();
            int64_t intervalMs = scanIntervalMs_.load();
            if (intervalMs < 100) {
                intervalMs = 100;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    });
    triggerThread_.detach();
}

void CellProvider::Disable()
{
    std::lock_guard lock(mutex_);
    running_ = false;
}

void CellProvider::SetScanIntervalMs(int64_t intervalMs)
{
    if (intervalMs < 100) {
        intervalMs = 100;
    }
    scanIntervalMs_.store(intervalMs);
}

int64_t CellProvider::GetScanIntervalMs() const
{
    return scanIntervalMs_.load();
}

void CellProvider::Operation()
{
    CellProviderListener listenerCopy;
    {
        std::lock_guard lock(mutex_);
        listenerCopy = listener_;
    }
    if (listenerCopy == nullptr) {
        return;
    }
    std::vector<std::shared_ptr<CellInfo>> cellVec = GetCellInfo(0);
    const auto slot1 = GetCellInfo(1);
    cellVec.insert(cellVec.end(), slot1.begin(), slot1.end());
    if (!cellVec.empty()) {
        listenerCopy(cellVec);
    }
}

std::vector<std::shared_ptr<CellInfo>> CellProvider::GetCellInfo(int32_t slotId)
{
    std::vector<std::shared_ptr<CellInfo>> cellVec;
    std::vector<sptr<OHOS::Telephony::CellInformation>> cellList;
    if (OHOS::Telephony::CoreServiceClient::GetInstance().GetCellInfoList(slotId, cellList) !=
        OHOS::Telephony::TELEPHONY_ERR_SUCCESS) {
        return cellVec;
    }
    const int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (const auto &cell : cellList) {
        if (cell == nullptr) {
            continue;
        }
        switch (cell->GetNetworkType()) {
            case OHOS::Telephony::CellInformation::CellType::CELL_TYPE_NR:
                cellVec.push_back(ParseNr(cell, timestamp));
                break;
            case OHOS::Telephony::CellInformation::CellType::CELL_TYPE_LTE:
                cellVec.push_back(ParseLte(cell, timestamp));
                break;
            case OHOS::Telephony::CellInformation::CellType::CELL_TYPE_TDSCDMA:
                cellVec.push_back(ParseTdsCdma(cell, timestamp));
                break;
            case OHOS::Telephony::CellInformation::CellType::CELL_TYPE_WCDMA:
                cellVec.push_back(ParseWCdma(cell, timestamp));
                break;
            case OHOS::Telephony::CellInformation::CellType::CELL_TYPE_CDMA:
                cellVec.push_back(ParseCdma(cell, timestamp));
                break;
            case OHOS::Telephony::CellInformation::CellType::CELL_TYPE_GSM:
                cellVec.push_back(ParseGsm(cell, timestamp));
                break;
            default:
                break;
        }
    }
    return cellVec;
}

std::shared_ptr<CellInfo> CellProvider::ParseNr(sptr<OHOS::Telephony::CellInformation> cell, int64_t ts)
{
    auto *nr = reinterpret_cast<OHOS::Telephony::NrCellInformation *>(cell.GetRefPtr());
    auto out = std::make_shared<CellNr>();
    out->celltype = NR;
    out->timestamp = ts;
    out->cellId = nr->GetNci();
    out->signalIntensity = nr->GetSignalIntensity();
    out->signalLevel = nr->GetSignalLevel();
    out->mcc = nr->GetMcc();
    out->mnc = nr->GetMnc();
    out->nrArfcn = nr->GetArfcn();
    out->pci = nr->GetPci();
    out->tac = nr->GetTac();
    out->nci = nr->GetNci();
    return out;
}

std::shared_ptr<CellInfo> CellProvider::ParseLte(sptr<OHOS::Telephony::CellInformation> cell, int64_t ts)
{
    auto *lte = reinterpret_cast<OHOS::Telephony::LteCellInformation *>(cell.GetRefPtr());
    auto out = std::make_shared<CellLte>();
    out->celltype = LTE;
    out->timestamp = ts;
    out->cellId = lte->GetCellId();
    out->signalIntensity = lte->GetSignalIntensity();
    out->signalLevel = lte->GetSignalLevel();
    out->mcc = lte->GetMcc();
    out->mnc = lte->GetMnc();
    out->pci = lte->GetPci();
    out->tac = lte->GetTac();
    out->earfcn = lte->GetArfcn();
    return out;
}

std::shared_ptr<CellInfo> CellProvider::ParseTdsCdma(sptr<OHOS::Telephony::CellInformation> cell, int64_t ts)
{
    auto *tds = reinterpret_cast<OHOS::Telephony::TdscdmaCellInformation *>(cell.GetRefPtr());
    auto out = std::make_shared<CellTdsCdma>();
    out->celltype = TDS_CDMA;
    out->timestamp = ts;
    out->cellId = tds->GetCellId();
    out->signalIntensity = tds->GetSignalIntensity();
    out->signalLevel = tds->GetSignalLevel();
    out->mcc = tds->GetMcc();
    out->mnc = tds->GetMnc();
    out->lac = tds->GetLac();
    out->cpid = tds->GetCpid();
    out->uarfcn = tds->GetArfcn();
    return out;
}

std::shared_ptr<CellInfo> CellProvider::ParseWCdma(sptr<OHOS::Telephony::CellInformation> cell, int64_t ts)
{
    auto *wcdma = reinterpret_cast<OHOS::Telephony::WcdmaCellInformation *>(cell.GetRefPtr());
    auto out = std::make_shared<CellWcdma>();
    out->celltype = W_CDMA;
    out->timestamp = ts;
    out->cellId = wcdma->GetCellId();
    out->signalIntensity = wcdma->GetSignalIntensity();
    out->signalLevel = wcdma->GetSignalLevel();
    out->mcc = wcdma->GetMcc();
    out->mnc = wcdma->GetMnc();
    out->lac = wcdma->GetLac();
    out->psc = wcdma->GetPsc();
    out->uarfcn = wcdma->GetArfcn();
    return out;
}

std::shared_ptr<CellInfo> CellProvider::ParseCdma(sptr<OHOS::Telephony::CellInformation> cell, int64_t ts)
{
    auto *cdma = reinterpret_cast<OHOS::Telephony::CdmaCellInformation *>(cell.GetRefPtr());
    auto out = std::make_shared<CellCdma>();
    out->celltype = CDMA;
    out->timestamp = ts;
    out->cellId = cdma->GetCellId();
    out->signalIntensity = cdma->GetSignalIntensity();
    out->signalLevel = cdma->GetSignalLevel();
    out->mcc = cdma->GetMcc();
    out->mnc = cdma->GetMnc();
    out->baseId = cdma->GetBaseId();
    out->latitude = cdma->GetLatitude();
    out->longitude = cdma->GetLongitude();
    out->nid = cdma->GetNid();
    out->sid = cdma->GetSid();
    return out;
}

std::shared_ptr<CellInfo> CellProvider::ParseGsm(sptr<OHOS::Telephony::CellInformation> cell, int64_t ts)
{
    auto *gsm = reinterpret_cast<OHOS::Telephony::GsmCellInformation *>(cell.GetRefPtr());
    auto out = std::make_shared<CellGsm>();
    out->celltype = GSM;
    out->timestamp = ts;
    out->cellId = gsm->GetCellId();
    out->signalIntensity = gsm->GetSignalIntensity();
    out->signalLevel = gsm->GetSignalLevel();
    out->mcc = gsm->GetMcc();
    out->mnc = gsm->GetMnc();
    out->lac = gsm->GetLac();
    out->bsic = gsm->GetBsic();
    out->arfcn = gsm->GetArfcn();
    return out;
}

} // namespace OHOS::Multimedia::CameraAgentService
