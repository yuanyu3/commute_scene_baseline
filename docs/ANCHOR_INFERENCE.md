# 家 / 公司锚点推断

## 原则

1. **禁止**把白天 GPS 密度峰直接当 HOME（易变成公司）。
2. **COMPANY** ≈ 工作日白天（默认 09:30–17:30）长时驻留簇。
3. **HOME** ≈ 夜间 Radio 稳定区 + 非白天长时驻留；GPS 用 **到家后静止段**（含网络定位），不用「路上最后一点」。
4. 全用 **WGS84**；若输入为 GCJ 真值/dump，先转换。
5. 锚点是 **行为学中心 + 内外半径**，不是建筑红线；形状可用圆，后续可换网格。

## HOME

### 证据优先级

| 优先级 | 证据 | 用法 |
|--------|------|------|
| 1 | 用户弱监督点选（可选） | 直接作中心 |
| 2 | 到家后静止 GPS（含 `sourceType=2` 且长时间不变） | 簇中心 |
| 3 | 夜间主 CELL + 强家侧 WiFi/BLE | 约束/校验 |
| 4 | 次日晨起前最后静止段 | 辅助 |

### 算法摘要

```text
1. 将 location 转到 WGS84；合并 sensor GPS（已是 WGS84）
2. 去掉明显通勤高速点（速度过大）
3. 候选静止：acc 不限死 50m——对「位置方差小、持续 ≥ T_still」的网络点保留
4. 夜间(0-5)∪晚间到家后(≥19:30 本地) 的静止点做密度投票
5. 得到 home_center；R_in = max(60, p90(在家候选到中心距离))
6. R_out = R_in + near_band（默认 +40~70m）
7. 校验：与 COMPANY 中心距离必须 > min_home_company_sep_m（默认 2000m）
```

### 反例（本数据教训）

- 仅用 sensor 轨迹「19:19 路边点」→ 偏真值家 ~500m（再叠加曾混用 GCJ 真值对比）。
- 正确：用到家后 `location`（转 WGS84）静止簇，接近真值家。

## COMPANY

```text
1. 工作日 09:30–17:30 GPS（WGS84）静止/低速点密度峰
2. 白天强 WiFi（如 Huawei-Employee / 公司 SSID）共现加强
3. company_center；半径同 HOME 分位数法
4. 与 HOME 距离校验
```

## 输出（`config/anchors.json`）

```json
{
  "coordinate_system": "WGS84",
  "home": {"id": "home_001", "lat": 0, "lon": 0, "r_in_m": 80, "r_out_m": 120},
  "company": {"id": "company_001", "lat": 0, "lon": 0, "r_in_m": 80, "r_out_m": 150},
  "method": "...",
  "updated_at": "..."
}
```

Agent 可微调 `r_in_m` / `r_out_m`，一般不频繁搬动中心；中心漂移大时触发重估任务。

## 端侧重估执行器（C++）

实现：`sa_cpp/anchor_reestimate.cpp` → `ProcessQueuedAnchorReestimateJobs`。

算法与上文 Python 同源（白天静止簇→公司，夜间/黏着静止簇→家），GPS 来自产品根下各 session 的 `location_data_*.csv` 以及 `leave_window_samples.jsonl`。

### 何时执行

| 时机 | 行为 |
|------|------|
| Agent 调 `request_anchor_reestimate` | **立刻**：先入队 `anchor_reestimate_jobs.jsonl`，再同步跑执行器（最多 2 个 job） |
| `DAY_END` 改参 tick（本地 ≥22 点） | **排空队列**：再跑最多 3 个排队 job（补跑失败/积压） |
| 主机/离线 | 直接调 `ProcessQueuedAnchorReestimateJobs(root)` |

成功后写回 `anchors.json`，若 `BaselineRuntime` 已启用则 `SetAnchors` 热更新；状态行追加到同一 jsonl（`status=done|failed`）。
