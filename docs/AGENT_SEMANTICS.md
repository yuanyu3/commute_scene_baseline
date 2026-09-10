# Agent 可读语义信息

> 本文的证据查询仍可参考，但末尾直接改 θ 的动作流程属于旧消融方案。当前动作边界见 [CONTEXT_TEMPLATE_PERSONALIZATION.md](CONTEXT_TEMPLATE_PERSONALIZATION.md)。

面向 **θ 个性化 Agent**（`jiuwen_agent`）：只读证据 Tool 能拿到什么。  
实时场景仍由 `SceneEngine` 判定；Agent **不读 raw CSV 行**，只读已压缩的语义。

实现：`sa_agent::RegisterEvidenceTools` → `commute_sa::EvidenceQuery`。  
契约：`jiuwen_agent/tools_contract.json`。

---

## 边界

| 能读 | 不能读（未注册给 Agent） |
|------|--------------------------|
| θ / 锚点 / 推送误差统计 | `get_wifi_window` 等 raw CSV |
| 单次离开 episode 的 push+label | 每 tick 的完整 perception JSON |
| 推送后稀疏 GPS/行走样本 | 原始 PDR `(x,y)` 点流 |
| 推送前后多模态**语义摘要** | 未落盘的实时 IMU |

坐标一律 **WGS84**。产品根默认：`/data/service/el1/public/commuteagentservice/`。

---

## 证据 Tool 一览

| Tool | 语义主题 |
|------|----------|
| `get_theta` | 当前 HSMM evidence strength、持续时间先验与门控参数 |
| `get_anchors` | 家/公司围栏中心与半径 |
| `get_error_stats` | 推送对错与 lead 分布 |
| `get_leave_episode` | 一次离开的 push + 自标注 |
| `get_leave_window_samples` | 推送后稀疏轨迹/行走 |
| `get_leave_sensor_summary` | 推送前后 WiFi/Cell/GPS/PDR/磁摘要 |

个性化动作只通过受约束的模板、证据强度和持续时间 trial 工具完成；最终统一调用 `submit_agent_analysis` 留下类型化审计。

---

## 1. `get_theta` — 当前参数

**入参：** 无  

**语义：** 端上正在用的 θ（`theta.json`），例如：

- 门控：`enter_leave` / `exit_leave` 是 `P(LEAVING)` 阈值；`arm_delay_s` 已停用（`min_evidence` 仅诊断）
- Evidence strength：新配置使用 `evidence_strength` 对象；旧 `w_*` 名称仅作兼容
- 持续时间先验：`hsmm_preleave_*` / `hsmm_leaving_*`（完整序列 replay 上线前不允许 Agent 直接修改）
- 时段先验：`weekday_leave_home_hour` / `weekday_leave_company_hour` / `leave_window_min`
- 提前量目标：`lead_min_s` 是最低提前量，`lead_max_s` 是评分中的合理最早边界、不是实时门控；另有 `away_confirm_s` / `min_away_s`
- 侧重点：`focus_side`（`company` | `home` | `both`）

Agent 用它了解「当前策略」，再决定改哪几个参。

---

## 2. `get_anchors` — 家/公司锚点

**入参：** 无  

**语义：** `anchors.json`（WGS84）

| 字段 | 含义 |
|------|------|
| `home` / `company`.`lat`,`lon` | 行为学中心 |
| `r_in_m` / `r_out_m` | 家侧 INSIDE/NEAR/OUTSIDE 围栏。公司侧只作「是否在附近」辅助；出大门以 `source_type` 2→1 为准 |
| `method` | 推断来源说明 |
| `coordinate_system` | 固定 `WGS84` |

---

## 3. `get_error_stats` — 推送质量总览

**入参：** `since_ms?`，`scene?`（目前主要回显）  

**语义：** 聚合 `leave_episodes.jsonl`

| 字段 | 含义 |
|------|------|
| `n_push` | 基线推送次数 |
| `n_false_push` | 推送后未真正离开（误推） |
| `n_confirmed_leave` | 推送后确认离开 |
| `n_missed_leave` | 真离开但未推送（漏推） |
| `n_lead_samples` / `lead_p50_s` / `lead_p90_s` | 确认离开样本的 lead 分布 |
| `n_lead_late` / `n_lead_ok` / `n_lead_early` | lead 相对评分区间的位置 |

`lead_s = t*_outside − t_push`：越大越「推得早」。

---

## 4. `get_leave_episode` — 单次离开台账

**入参：** `t_push_ms?`（缺省=最近一次 push）  

**语义：** 同一 `t_push_ms` 下的 push + label 行，例如：

**push 行**

- `intent`：`DEPARTURE_NOTIFICATION` / `LEAVE_COMPANY_NOTIFICATION`
- `scene`、`score_home` / `score_company`（兼容字段，当前值为 `P(LEAVING)`）
- `dist_home_m`、`walking`、`eta_leave_s`
- 当时 θ 快照片段（`enter_leave`、`min_evidence`）

**label 行**

| `label` | 含义 |
|---------|------|
| `CONFIRMED_LEAVE` | 公司：推送后首次 `source_type=1`（出大门）；家：首次围栏 OUTSIDE。含 `t_star_ms`、`lead_s` |
| `FALSE_PUSH` | 结算内未确认离开。公司侧整段无 `source_type=1` 即未离开公司（不再标 `UNKNOWN`） |
| `MISSED_LEAVE` | 持续 OUTSIDE 且 lookback 内无对应侧 push（`t_push_ms` 可为 0） |

---

## 5. `get_leave_window_samples` — 推送后稀疏轨迹

**入参：** `t_push_ms?`，`limit?`（默认 100，最大 500）  

**语义：** `leave_window_samples.jsonl` 中推送后稀疏点（非高频 dump）

| 字段 | 含义 |
|------|------|
| `t_ms` / `t_push_ms` | 采样时刻 / 所属推送 |
| `lat` / `lon` / `acc` | WGS84 位置与精度 |
| `walking` | 是否在走 |
| `home_relation` | `INSIDE` / `NEAR` / `OUTSIDE` / … |
| `dist_home_m` | 到家距离（米） |

用于看「推完之后人往哪走、是否出围栏」。

---

## 6. `get_leave_sensor_summary` — 多模态语义摘要（主证据）

**入参：**

| 参数 | 默认 | 说明 |
|------|------|------|
| `t_push_ms` / `t_center_ms` | 最近 push | 窗口中心 |
| `before_s` | 600 | 中心前秒数 |
| `after_s` | 1200 | 中心后秒数 |
| `session_dir` | 最新 Ability session | 读 dump 的目录 |

**原则：** 只给 Agent 可解释的统计与事件，不给原始扫描表。

### 6.1 WiFi

| 语义 | 说明 |
|------|------|
| `soft_side` / `soft_n` / `soft_ready` | 半持久 soft dwell 侧（home/company）及是否可用 |
| `snapshots`：`pre_3min` / `at_push` / `post_5min` | 相对 soft 的 Jaccard、弱/缺失 AP 数、`detach_hint` |
| `events` | Jaccard 跌破 0.3 的穿越事件 |

含义：推送前后「家/公司 WiFi 附着是否瓦解」。

### 6.2 CELL

| 语义 | 说明 |
|------|------|
| `dominant_pre` / `dominant_at_push` / `dominant_post` | 各时段主导 `cell_id` |
| `changed_around_push` | 推前 vs 推后主小区是否变化 |
| `changes` | 小区切换事件列表 |

含义：是否切出驻留小区。

### 6.3 GPS

| 语义 | 说明 |
|------|------|
| `snapshots`（pre / at / post） | `dist_home_m`、`dist_company_m`、`acc_m` |

含义：相对双锚点距离是否外扩（不暴露整条轨迹）。

### 6.4 PDR

| 语义 | 说明 |
|------|------|
| `n_episodes` / `n_points` | 窗口内步行段与点数 |
| `max_net_m` / `max_path_m` | 最大净位移 / 路径长 |
| `net_pre_3min_m` / `net_at_push_m` / `net_post_5min_m` | 相对推送时刻的净位移快照 |
| `path_at_push_m` / `straightness_at_push` | 推送时刻路径长与直行度 |
| `outbound_hint` | `net_at_push ≥ 8m` 是否像「向外走」 |
| `live_pdr_debug` | 若运行时启用，当前 PDR 证据调试 JSON |

**注意：** `net` = 本段步行起点→当前点的平面距离，**不是**到家 GPS 距离（与基线 `pdr_net_out_*` 一致）。

### 6.5 磁强（Mag）

| 语义 | 说明 |
|------|------|
| `mag_ema_pre` / `mag_ema_post` / `delta` | 推送前后 \|B\| EMA 及差 |

弱辅证环境变化；当前**不直接进入** HSMM 观测。

### 6.6 实时调试块（可选）

| 字段 | 说明 |
|------|------|
| `live_radio_debug` | 运行中 RadioEvidence 快照 |
| `live_pdr_debug` | 运行中 PdrEvidence 快照 |

无运行时或未 Init 时多为 `{}`。

---

## 推荐阅读顺序（改参时）

```text
1. get_error_stats          → 偏误推 / 漏推 / lead 早晚？
2. get_leave_episode        → 最近一次到底发生了什么？
3. get_leave_sensor_summary → WiFi/Cell/GPS/PDR 是否支持该标签？
4. get_leave_window_samples → 推后是否真出围栏？
5. get_theta + get_anchors  → 当前参数与围栏是否合理？
```

再选择一个干预族进入 trial：结构模板、证据强度或持续时间；由确定性工具回放验收，最后调用 `submit_agent_analysis`。

---

## 自标注标签 ↔ 传感语义

| 标签 | Agent 应对照的摘要 |
|------|-------------------|
| `CONFIRMED_LEAVE` | GPS 距离外扩、WiFi detach、PDR `outbound_hint`、CELL 切换 |
| `FALSE_PUSH` | 推后仍 INSIDE、WiFi 仍附着、净位移小 / 折返 |
| `MISSED_LEAVE` | 已持续 OUTSIDE，但当时无 push；看 lead 窗口内传感是否本可触发 |

---

## 相关文档

- [CONTEXT_TEMPLATE_PERSONALIZATION.md](CONTEXT_TEMPLATE_PERSONALIZATION.md) — 当前个性化闭环与动作 Tool
- [RADIO_EVIDENCE.md](RADIO_EVIDENCE.md) — WiFi/Cell 在线证据  
- [PDR_EVIDENCE.md](PDR_EVIDENCE.md) — PDR 净外向如何进入 HSMM 观测
- [PRODUCT_FLOW.md](PRODUCT_FLOW.md) — 何时 Invoke Agent  
