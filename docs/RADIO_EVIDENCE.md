# Radio leave evidence（半持久 soft set + 分模态权重）

权威实现：`sa_cpp/include/commute_sa/radio_evidence.h` + `BaselineRuntime` 接入。

## 离开打分里 WiFi / CELL / BLE 怎么参与

`ScoreLeaving` 不再把 WiFi+CELL OR 成一路 `w_radio`，而是：

| 通道 | 特征 | 证据分 | 权重 |
|------|------|--------|------|
| WiFi | `wifi_*_detach` | 0/1 | `w_wifi`（默认 0.08） |
| CELL | `cell_leave_*` | 0/1 | `w_cell`（默认 0.05） |
| BLE | `ble_*_detach` | 0/1 | `w_ble`（默认 0.02） |

```text
score = clip01(
  w_walk*sWalk + w_pdr*sPdr + w_geo*sGeo
  + w_wifi*sWifi + w_cell*sCell + w_ble*sBle
  + w_time*sTime)
```

每一路 `sX >= 0.5` 各计 **1 个 hit**（进 `min_evidence`）。

### WiFi → detach
1. Soft dwell set（INSIDE 学习 + `radio_soft.json` 半持久）
2. Jaccard / RSSI drop 相对 soft set
3. Soft 未就绪：相对 ~2 min 前扫描的 temporal churn

### CELL → leave
稳定 `cell_id` ≥ ~90s 后切到其它 id（带确认迟滞）。

### BLE → detach
MAC 集合 temporal churn（近 30s vs ~2 min 前）；尚无 BLE soft set。

旧配置仅有 `w_radio` 时，加载按 **0.55 / 0.30 / 0.15** 拆到 wifi/cell/ble。

## Soft set 半持久

`radio_soft.json`：TTL 7 天，Init 加载，ObserveDwell 更新后节流落盘。

## 主机验证

```bash
cd sa_cpp && cmake -B build -DCOMMUTE_SA_BUILD_SMOKE=ON
cmake --build build --target commute_radio_smoke commute_scene_smoke
./build/commute_radio_smoke
```
