# 离开锚点 HSMM

`sa_cpp/include/commute_sa/leave_hsmm.h` 与 `sa_cpp/src/leave_hsmm.cpp` 实现端侧在线显式持续时间隐半马尔可夫模型，替代原先 `ScoreLeaving` 的加权和判定。

## 状态

```text
AT_ANCHOR -> PRE_LEAVE -> LEAVING -> OUTSIDE
     ^          |           |
     +----------+-----------+   （取消、折返、重新附着）
```

- `AT_ANCHOR`：稳定处于家或公司锚点。
- `PRE_LEAVE`：出现准备离开的活动，但尚不足以触发服务。
- `LEAVING`：连续证据支持正在离开；`P(LEAVING)` 可进入产品 `LEAVING_*`。
- `OUTSIDE`：已越出锚点，只用于确认和回标，禁止此时推送“带钥匙”。家侧仍由 GPS 围栏确认；**公司侧以 `source_type` 为准**：附近时 `2`=公司内，`2→1`=出大门。

模型分别为家和公司维护 `(state, elapsed_second)` 概率质量。转移 hazard 只依赖状态已持续时间、固定拓扑以及 INSIDE/OUTSIDE 等结构关系；walking、PDR、Wi-Fi、Cell、time、baro 等原子证据只在发射似然中使用一次。超过 `hsmm_max_gap_s` 的采样间隔会重置过滤器，避免用过期状态污染当前判断。

## 观测

每个 tick 先生成 `[0,1]` 语义观测，不把多模态数据压成单一总分：

| 观测 | 来源 |
|------|------|
| walking | walking 状态 |
| pdr_outbound | PDR 净向外位移相对锚点半径归一化 |
| geo_outbound | 家：GPS 距锚点连续扩张。公司：附近时不采信围栏扩张；`source_type` 2→1 为出大门 |
| wifi_detach | Wi-Fi Jaccard 下降或 detach 事件 |
| cell_detach | Cell leave 事件 |
| ble_detach | BLE detach 事件 |
| time_prior | 个人典型离开时间窗 |
| baro_descending | 气压正在下行 |
| baro_lower_platform | 到达更低气压平台（如下到一楼/大堂） |
| sequence_progress | Agent 模板正向序列的完成比例 |
| sequence_complete | Agent 模板正向序列已经完整发生 |
| negative_pattern_match | Agent 模板负向模式同时成立 |
| cancel_sequence_match | 正向前缀启动后，有序返回序列已经完成；与负向交互项一起抑制 LEAVING |
| sequence_reliability | 经确定性验证器选定的模板可靠度/作用强度 |

各状态对上述观测有不同的初始期望，使用分数型 Bernoulli 似然更新后验。配置中的
`evidence_strength` 直接以 [0,1] 系数控制对应观测的发射似然（baro 同时作用于
descending / lower_platform 两项），不再经过 `0.25 + 3*w` 隐藏映射，也不直接相加产生离家分数。
旧 `w_*` 配置仅在读取时做一次等效迁移，详见 [EVIDENCE_STRENGTH.md](EVIDENCE_STRENGTH.md)。

`w_x=0` 的语义是“该原子观测通道对决策不可用”，而不是“观测值恰好为 0”。该通道会从 evidence hits、HSMM 发射似然、传感器派生 attach 门控和工作场所气压基线初始化中移除；上下文模板也不能重新注入它。原始传感器数据仍可采集和落盘，供诊断或以后重新启用。GPS 的 `INSIDE/OUTSIDE` 场景关系、OUTSIDE 禁推、冷却和一次一推属于独立的产品安全事实，不由 `w_geo` 关闭。

GPS `INSIDE/NEAR/OUTSIDE`、approaching 和 Wi-Fi attach 作为强观测。公司相对位置在校园附近时由 `source_type` 判定（`2` 内 / `1` 外），`r_in`/`r_out` 只作附近辅助；推送频控、一次一推、OUTSIDE 禁推、approaching 禁推仍是模型外硬约束。INSIDE 只排斥 OUTSIDE，不再根据 walking/radio/PDR 二次改写关系似然；这些原子证据通过发射模型将概率从 AT_ANCHOR/PRE_LEAVE 推向 LEAVING。

## 参数语义

| 参数 | 含义 |
|------|------|
| `enter_leave` | 进入 `LEAVING_*` 所需的 `P(LEAVING)` |
| `exit_leave` | 回到锚点场景所需的 `P(LEAVING)` 上限 |
| `hsmm_preleave_{min,mean,max}_s` | `PRE_LEAVE` 持续时间先验 |
| `hsmm_leaving_{min,mean,max}_s` | `LEAVING` 持续时间先验 |
| `hsmm_max_gap_s` | 过滤器可接受的最大 tick 间隔 |
| `min_evidence` | 仅用于 uncertainty 诊断（hits 少则 HIGH），**不再作为推送硬门控** |

`TickDecision.score_home/score_company` 为兼容既有落盘字段保留，但值已经是对应侧的 `P(LEAVING)`。新增 `hsmm_phase_*`、`hsmm_preleave_*`、`hsmm_outside_*` 用于调试和后续 episode 特征分析。

## 个性化边界

当前 Agent 不直接写任意 θ。它可以选择结构模板、证据强度档位或持续时间先验这一种干预族，再由受约束工具从允许集合中生成候选，并使用历史前缀回放验收后提交；数值边界与安全条件仍由 C++ 决定。

产品硬禁推（OUTSIDE / approaching / attach / 一次一推 / cooldown）留在 C++，不进入 Agent 可购物的硬门控目录。气压有观测就自动进入 HSMM，用 `w_baro` 调重要性。

## 验证

```powershell
cmake -S sa_cpp -B sa_cpp/build_hsmm -DCOMMUTE_SA_BUILD_SMOKE=ON
cmake --build sa_cpp/build_hsmm
.\sa_cpp\build_hsmm\commute_hsmm_smoke.exe
.\sa_cpp\build_hsmm\commute_scene_smoke.exe
```

`smoke_leave_hsmm.cpp` 覆盖稳定驻留、持续向外、OUTSIDE 确认、返回锚点和“只有室内走动”五种路径。

## Barometer evidence

`BaroEvidence` converts pressure into relative physical height (metres), never building-specific floor counts. A short stable pressure window plus company dwell/fingerprint evidence establishes the origin platform. When `baro_available && baro_baseline_ready`, descending/lower-platform observations are fed into HSMM automatically; importance is `w_baro`. Missing or untrusted baro is skipped. Wi-Fi attach is never a push permit.

## Persistent company radio fingerprint

`config/company_radio_fingerprint.json` is the reusable workplace profile. It is separate from the expiring on-device `radio_soft.json`. A floor-specific profile must be built only from explicitly labelled dwell recordings on that floor; do not build it from complete departure/return routes merely because their GPS remains inside the company fence. The current demo profile uses the three synchronized fifth-floor dwell recordings from 0812:

```bash
python examples/build_company_radio_fingerprint.py \
  --dwell-session /mnt/d/0812/20260812_105415 \
  --dwell-session /mnt/d/0812/20260812_105416 \
  --dwell-session /mnt/d/0812/20260812_105417 \
  --floor-label floor_5 --top-k 12 --preserve-cell
```

`--preserve-cell` keeps the existing company-wide cellular fingerprint unchanged; only WiFi is narrowed to the fifth-floor dwell context.

Offline replay loads this file by default. On device, copy it to `/data/service/el1/public/commuteagentservice/company_radio_fingerprint.json`; `BaselineRuntime` loads it at startup. WiFi coverage or Cell match can establish a trusted workplace context/barometer baseline, but is never push permission by itself.

For a static workplace-floor WiFi profile, `company_site_wifi_coverage` means fingerprint recall (`matched floor BSSIDs / all floor-profile BSSIDs`), not the fraction of every AP in the current scan. The default state machine is deliberately asymmetric:

- recall `<= 0.20` for three fresh observable scans (or 30 seconds) confirms detach;
- recall `>= 0.50` for two fresh scans (or 15 seconds) confirms reattach;
- values between the thresholds retain the prior state;
- repeated engine ticks without a new WiFi scan cannot advance confirmation;
- detach is armed only after the floor fingerprint has first been observed at attach-level recall.

Raw recall remains available for diagnostics and workplace/barometer readiness. The HSMM-facing WiFi similarity is gated by the confirmed state, preventing one stale low scan from being counted repeatedly as new leave evidence.

BLE samples continue to be collected, but BLE evidence is disabled by default (`ble_evidence_enabled=false`, `w_ble=0`) because ambient personal-device MAC churn is not a reliable anchor signal. It should only be enabled after stable beacon identities are captured.

The static Cell profile is a target-floor whitelist rather than a set of floor/outdoor classes. Cell IDs are categorical, so tolerance is temporal rather than numeric: the current implementation uses the fraction of valid samples matching the whitelist in a 15-second window. A ratio `<=0.20` confirms floor detach after three fresh samples; a ratio `>=0.60` confirms floor reattach. Intermediate ratios retain the prior state. The state is armed only after the target-floor whitelist has first been observed.
