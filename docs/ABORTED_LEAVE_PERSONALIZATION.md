# 中止离开识别与返回序列个性化

schema 6 的可替代路径、证据接口修正、重复历史问题与最新实验结果见
[返回路径鲁棒性更新](RETURN_PATH_ROBUSTNESS.md)。本文的线性 cancel_sequence 作为兼容模式保留。

## 目标

`ABORTED_LEAVE` 表示传感器已经观察到与历史确认离开相似的离开前缀，但随后出现物理返回并且没有离开锚点。它与两类结果分开：

- `CONFIRMED_LEAVE`：离开前缀最终发展为 outside；
- `FALSE_PUSH` / `TRUE_NEGATIVE`：没有足够证据证明离开过程曾经启动；
- `ABORTED_LEAVE`：离开过程疑似启动，但之后反转。

这里的“意图”是基于行为序列的推断，不是对用户主观想法的事实认定。

## 在线观测

`BaroEvidence` 在原有 `descending`、`descent_m` 和 `lower_platform` 之外输出：

- `ascending`：短时气压变化显示高度正在回升；
- `vertical_closure`：本次锚点停留中曾达到显著下降，随后在稳定平台回到距起始高度 3 米以内。

二者通过 `TickFeatures` 写入 `LeaveObservation` 和 `policy_history.jsonl`。它们不直接加入 HSMM 九维原子发射向量，只能被受约束上下文模板使用，避免气压证据重复计算。

## Agent提议与C++验证

Agent 先通过只读 `get_aborted_leave_candidates` 获取每条误推的下降、低层平台、回升、闭合和 outside 时间证据，再对原始 `FALSE_PUSH` 调用 `propose_aborted_leave_interpretation`。输入必须包含准确的 `side`、`episode_id`、置信度和证据解释；当 episode ID 不唯一时还必须提供 `outcome_t_ms`，唯一时由 C++ 解析。

C++ 只有在以下条件全部成立时才接受：

1. 精确找到原始 `FALSE_PUSH` episode；
2. 最大下降达到当前垂直阈值；
3. 同 side 的 `CONFIRMED_LEAVE` 历史中存在“显著下降 + 至少一种支持事件”的参考前缀；
4. 目标 episode 在下降之后出现 `baro_ascending`；
5. 随后出现 `vertical_closure`；
6. 整个窗口没有 outside。

通过后只向 `episode_interpretations.jsonl` 追加非破坏性解释，不改写原始 `leave_episodes.jsonl` 或 `policy_history.jsonl`。回放器按 `side + outcome_t_ms + episode_id` 读取最新解释，将原始 `FALSE_PUSH` 映射为 `ABORTED_LEAVE`。

## 模板执行

模板 schema v5 新增 `cancel_sequence`。它是跨 tick 的有序序列，并且只有 `positive_sequence` 至少匹配一个事件后才开始匹配。例如：

```json
{
  "positive_sequence": "baro_descending,lower_platform",
  "cancel_sequence": "baro_ascending,vertical_closure"
}
```

取消序列完整匹配后：

- 清除当前正向序列的 progress、complete 和 ready；
- 输出 `cancel_sequence_match=1`；
- 同时向 HSMM 输出负向交互证据；
- 将取消证据保持 120 秒，防止刚返回锚点就由残余离开概率再次触发。

它不是全局负规则：未出现正向前缀时，单独的气压回升或位于起始高度不会触发取消。

## 回放评分与保护

回放 score v3 对 `ABORTED_LEAVE` 单独统计：

- `aborted_intent_recognized`：在 `abort_t_ms` 之前，`P(LEAVING)` 是否越过阈值；
- `aborted_cancel_recognized`：在 `abort_t_ms` 之后，取消序列是否匹配；
- `aborted_visible_push`：该 episode 是否已经满足用户可见推送条件。

中止离开不进入 hard false 计数，也不会因为最终没有 outside 而惩罚气压前缀。模板提交仍必须满足：

- 至少两个 `ABORTED_LEAVE` episode 才能生成 `cancel_sequence`；
- 中止前意图识别数量不能下降；
- 中止 episode 的用户可见推送不能增加；
- 原有确认离开、硬负样本、漏报和逐 episode 提前量保护继续生效。

`vertical_threshold` 估计不使用 `ABORTED_LEAVE`，也不使用旧 `lower_platform` 选择样本；它只依据气压有效的独立结果标签与原始下降量拟合。`ABORTED_LEAVE` 只参与返回序列学习和相应的回放保护。

## 当前限制

- 新字段只能作用于升级后记录的历史；旧数据若未保存完整 `baro_descent_m` 轨迹和回升/闭环字段，不能可靠补标。
- 当前共享前缀验证是确定性的事件重合校验，不是学习到的序列距离模型。
- 已经展示给用户的通知不能被返回序列撤回；`cancel_sequence` 主要防止尚未触发的候选和返回后的重复触发。若要在共享前缀阶段完全避免可见误推，仍需引入可撤销的内部候选或短暂通知确认门控，这会与提前量形成真实折中。
- 120 秒取消保持时间目前是代码固定安全值，尚未开放个性化。
- Agent提议通过物理证据验证后仍只是“疑似中止离开”；没有用户交互时无法证明主观意图。
