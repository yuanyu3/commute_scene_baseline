# Agent 受约束上下文模板个性化

## 1. 设计边界

实时检测仍是编译在端侧的 `SceneEngine + LeaveHsmm`。Agent 不逐 tick 推断、不提供数值、不改 `theta.json`、不生成代码；它从完整 episode 和跨 episode 对照中提出个人化传感器事件组合，并只选择“哪些参数族值得个性化”。数值估计和是否生效仍由确定性 C++ 完成。

```text
历史 episode → Agent 归因/组合结构/选择参数族 → DSL 白名单校验
             → C++ 稳健估计数值 + 试 LOW/MEDIUM/HIGH → 全历史 HSMM 回放
             → 硬门通过后提交 → SceneEngine 实时读取
```

## 2. 模板 DSL

Agent 可填写：

- `anchor_id`：Agent侧唯一的模板和历史主键；运行时由当前 `anchors.json` 映射为Home/Company产品角色；
- `applicability`：`always` 或 `baro_ready`；
- `positive_sequence`：有顺序的正向事件；
- `negative_pattern`：同时成立时抑制非特异证据的事件集合；
- `cancel_sequence`：正向前缀启动后才允许匹配的有序返回序列；
- `parameter_families`：当前可选 `vertical_threshold`，只表达参数类别；
- `rationale`：可审计解释。

事件只能来自 C++ catalog，例如 `baro_descending`、`lower_platform`、`baro_ascending`、`vertical_closure`、`geo_outbound`、`walking`、`wifi_detach`、`no_baro_descent`。每个子句最多 6 个原语。schema v5 不再放大或压低 walking/PDR/Wi-Fi/baro 等原子观测，而是独立输出 `sequence_progress`、`sequence_complete`、`negative_pattern_match` 和 `cancel_sequence_match`，由 HSMM 作为时序交互似然使用。序列进度保持为 0~1 的结构量，作用强度单独由 `sequence_reliability` 表达；该强度不在 Agent schema 中，而由 C++ 固定试验 LOW=0.2、MEDIUM=0.4、HIGH=0.6。

`ABORTED_LEAVE` 和返回序列的实现、验证条件及限制见 [ABORTED_LEAVE_PERSONALIZATION.md](ABORTED_LEAVE_PERSONALIZATION.md)。

当前只开放 `vertical_threshold`：使用至少 3 个经标签确认的低层平台 episode（包括最终确认离开，以及已判定为软正例的低层平台中间过程）估计稳定下降阈值，并保存在锚点模板内，不修改全局 theta。旧历史无法证明“低于旧阈值时平台是否稳定”，因此首版只允许气压阈值保持或升高，避免不可回放的激进放宽。

`departure_time` 暂时关闭。当前数据集包含刻意安排在非正常时间的采集过程，启用它会学习采集计划而非真实通勤习惯。代码仍可读取旧 schema 中的时间画像，但 catalog 和生成接口不再允许新模板申请该参数族。

全局传感器系数已经重构为直接 [0,1] 的 `evidence_strength`。Agent可以选择需要重估的
通道族，确定性估计器负责数值、回放与锚点级提交；模板本身仍只处理结构。旧 `w_*` 配置兼容、
默认值迁移和强度工具见 [EVIDENCE_STRENGTH.md](EVIDENCE_STRENGTH.md)。
`enter_leave` 和 HSMM 时长边界仍不由模板 Agent 修改。

### 默认 Prompt 策略

默认采用“方法约束、不给答案”的时序分析 Prompt：要求查看完整 episode、比较不同结果、区分传感器不可用与无变化，并记录支持、反证和缺失证据；不提供建筑类型、具体传感器或推荐事件序列示例。Agent 必须先读取真实锚点，`positive_sequence` 按顺序解释，`negative_pattern` 按 AND 合取解释。单个确认样本只能形成高风险候选，最终仍由 C++ 回放决定提交或 no-op。

## 3. 执行位置

| 阶段 | 实现位置 | 作用 |
|---|---|---|
| Agent 工具注册与权限门 | `examples/personalizer_llm/register_tools.cpp`、`main.cpp` | 注册模板工具并阻止旧直接改参工具 |
| DSL、回放、提交、实时应用 | `sa_cpp/src/context_template.cpp` | 校验结构、选择强度、保存并转换 HSMM 观测 |
| 历史反事实回放 | `sa_cpp/src/theta_eval.cpp` | 对每条 episode 顺序回放同一个 HSMM |
| 实时接入点 | `sa_cpp/src/scene_engine.cpp` | 构造观测后、`LeaveHsmm::Step` 前应用模板 |
| Agent 协议 | `jiuwen_agent/system_prompt.md` | 要求方向检查、反证、no-op 与审计 |

`sa_cpp/` 是权威源；`scripts/sync_scene_engine_to_sa.ps1` 将同一实现同步到 OHOS vendored 副本。

## 4. 安全门

候选只有同时满足以下条件才可提交：

1. 历史中存在正样本；
2. 总分至少提高 0.25；
3. 明确误推数量不得增加；
4. `missed_leave` 不增加；
5. 已确认离开不减少；
6. 带 `lower_platform` 的软正例不减少。

approaching、attached、OUTSIDE 等产品门控仍由编译代码决定，模板不能覆盖。若没有候选通过，工具返回空 `best_candidate_id`，Agent 只能 discard/no-op。

## 5. 结构化决策审计

`submit_agent_analysis` 不再接受模糊的 `intervention_block/direction` 记录。每次个性化流程结束后必须写入一个明确类别：

- `STRUCTURE`：事件顺序、返回路径或上下文结构；
- `EVIDENCE_STRENGTH`：一个或多个原子证据通道的锚点级强度重估；
- `DURATION`：`PRE_LEAVE` 或 `LEAVING` 的锚点级时长重估；
- `NO_OP`：证据不足或没有安全候选。

审计同时记录 `decision=COMMITTED|REJECTED|DISCARDED|NO_OP`。非 NO-OP 记录必须包含决定性工具、工具结果和回放结果；所有记录都包含 `anchor_id`、支持证据、反证、缺失证据、置信度及最终理由。标准化后的 schema v2 数据写入 `audit.jsonl` 的 `changes` 对象，可直接用于统计各类 Agent 决策和工具接受率。

## 6. 0812 真实数据结果

输入为 5 条可评估离开 episode，另有 1 条返回公司序列（方向检查后不标为漏报）。基线包含 2 条明确误推、2 条到达低层平台但尚未离开的中间过程、1 条确认离开。

真实 Qwen Agent 生成：

```text
positive_sequence = baro_descending,lower_platform,geo_outbound
negative_pattern  = walking,no_baro_descent
applicability     = baro_ready
```

C++ 选择 LOW 强度。旧 schema v2 的离线回放结果为：明确误推从 2 降到 0，2 条平台中间过程和 1 条确认离开均保留，漏报为 0，score 从 0 提升到 8。该结果属于“模板改写原子观测”的历史基线，不应再作为 schema v3 的性能结论。

阶段1/2重构后，同一5条0812窗口在**无模板**时已经避免2条硬误推，同时保留2条低层平台过程和1条确认离开，score=8；v3独立序列观测保持相同分类结果，没有进一步提高该小数据集总分。与旧基线相比，正例推送晚约15–40秒，因此新结构消除了重复计算并提高了精度，但时长先验仍需在独立训练集上重新标定。模板价值应继续通过更丰富反例、顺序打乱消融和隐藏验证集判断，不能沿用旧v2增益。

目前样本量仍小，这个结果证明的是链路可行性，不是泛化精度结论。上线前需要更多确认离开、无气压设备、一楼建筑和返回锚点样本。

### 0812 垂直阈值估计

0812 的 3 条有效低层平台过程最大气压下降为 `20.17 m`、`20.07 m`、`19.96 m`，两条硬负例为 `0.38 m`、`10.23 m`。确定性估计器取正例低分位与负例高分位之间的安全分界，按 2 米步长得到 `baro_min_descent_m=16 m`（原值为 `12 m`）。

在这 5 条 episode 上，`12–19.96 m` 属于同一分类平台，因此 `16 m` 没有带来额外的回放分数变化；它提供的是更大的抗气压噪声余量。后续必须用阈值落在 `12–20 m` 附近的边界样本验证 `16 m` 是否优于原值。
