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
- `OUTSIDE`：GPS/围栏确认已越出锚点，只用于确认和回标，禁止此时推送“带钥匙”。

模型分别为家和公司维护 `(state, elapsed_second)` 概率质量。转移 hazard 依赖状态已持续时间，因此不是普通 HMM 的无记忆自循环。超过 `hsmm_max_gap_s` 的采样间隔会重置过滤器，避免用过期状态污染当前判断。

## 观测

每个 tick 先生成 `[0,1]` 语义观测，不把多模态数据压成单一总分：

| 观测 | 来源 |
|------|------|
| walking | walking 状态 |
| pdr_outbound | PDR 净向外位移相对锚点半径归一化 |
| geo_outbound | GPS 距锚点连续扩张 |
| wifi_detach | Wi-Fi Jaccard 下降或 detach 事件 |
| cell_detach | Cell leave 事件 |
| ble_detach | BLE detach 事件 |
| time_prior | 个人典型离开时间窗 |

各状态对上述观测有不同的初始期望，使用分数型 Bernoulli 似然更新后验。`w_walk`、`w_pdr`、`w_geo`、`w_wifi`、`w_cell`、`w_ble`、`w_time` 现在控制对应观测的可靠度，不再直接相加产生离家分数。

GPS `INSIDE/NEAR/OUTSIDE`、approaching 和 Wi-Fi attach 作为强观测；推送频控、一次一推、OUTSIDE 禁推、approaching 禁推仍是模型外硬约束。

## 参数语义

| 参数 | 含义 |
|------|------|
| `enter_leave` | 进入 `LEAVING_*` 所需的 `P(LEAVING)` |
| `exit_leave` | 回到锚点场景所需的 `P(LEAVING)` 上限 |
| `hsmm_preleave_{min,mean,max}_s` | `PRE_LEAVE` 持续时间先验 |
| `hsmm_leaving_{min,mean,max}_s` | `LEAVING` 持续时间先验 |
| `hsmm_max_gap_s` | 过滤器可接受的最大 tick 间隔 |
| `min_evidence` | 推送前至少需要的独立物理证据数，属于安全门控 |

`TickDecision.score_home/score_company` 为兼容既有落盘字段保留，但值已经是对应侧的 `P(LEAVING)`。新增 `hsmm_phase_*`、`hsmm_preleave_*`、`hsmm_outside_*` 用于调试和后续 episode 特征分析。

## 个性化边界

当前 Agent 可继续调整 `enter_leave/exit_leave` 和观测可靠度。持续时间参数已经进入 theta 配置和持久化，但在具备完整 tick 序列回放前，不通过 Agent action 暴露；现有 `evaluate_theta_on_history` 只基于推送时记录的后验概率，无法反事实重算持续时间或观测似然。

后续训练应使用 `CONFIRMED_LEAVE`、`SHORT_EXIT`、`ABORTED_LEAVE` 的完整窗口回放，比较固定误推约束下最早达到目标召回率的时点。

## 验证

```powershell
cmake -S sa_cpp -B sa_cpp/build_hsmm -DCOMMUTE_SA_BUILD_SMOKE=ON
cmake --build sa_cpp/build_hsmm
.\sa_cpp\build_hsmm\commute_hsmm_smoke.exe
.\sa_cpp\build_hsmm\commute_scene_smoke.exe
```

`smoke_leave_hsmm.cpp` 覆盖稳定驻留、持续向外、OUTSIDE 确认、返回锚点和“只有室内走动”五种路径。
