# 结构提案的模块组合搜索

STRUCTURE 提案不再只校准一份完整模板。保留完整 positive_sequence，
将 readiness_policy、完整 negative_pattern、每条返回路径作为可选模块，
枚举其所有子集（最多 5 个模块、32 个组合）。不拆散序列顺序或 AND 子句。
每个组合独立运行原有前缀及强度坐标搜索，避免一个组合的搜索种子污染其他组合。

readiness_policy 不再要求 Agent 先猜中 `disambiguate`。每个结构提案都固定生成
`support_only` 与 `disambiguate` 两个分支，在相同逐 episode 因果回放、保护条件和
评分下分别校准 `ready_prefix_length` 及各上下文强度。

返回路径独立检查至少两条已标注 ABORTED_LEAVE 匹配。缺少支持仅拒绝包含
该路径的组合，不拒绝整个提案。语法错误、未知原语、矛盾子句及不合法返回
顺序仍然拒绝提案：此功能不是静默修复任意错误 JSON。

每个候选仍接受 baseline、incumbent 和逐 episode 安全保护。优先误推/漏推、
中止离开可见推送，之后才是含提前量的评分。没有通过保护的组合仍然 NO_OP。
已经足够好的 incumbent 可能使所有候选因“没有进一步改善”被拒绝，这是正常结果。

工具返回 module_catalog（位号与模块名）、候选 combination_mask、拒绝原因、
模板及指标；每组合只展示最佳合格候选和最佳诊断候选，避免校准网格挤满上下文。
evaluated_candidate_count 包括校准候选及未回放的支持不足组合记录。
提交历史同时保留 module_search 摘要，实际落地的是选中候选而非原始大模板。

Agent 负责提出有证据的结构假设、阅读组合结果、决定提交或修订。
prompt/config 的单次任务预算为最多三轮 proposal，并非后端硬计数器。
每轮仍只能有一个未解决 trial，必须 commit/discard 后再提案；先提交合格改进
可将其保留为下一轮 incumbent。工具在已测试的有限组合中选优，不保证全局最优。
PRIMITIVE_PARAMETER 不执行结构删减，保持参数族边界；实时 HSMM 不增加搜索负担。

## 验证

scripts/validate_module_search.py 在隔离副本中比较旧模板逐 episode 回放，
并测试“有支持的垂直返回路径 + 无支持的地理返回路径”的组合隔离。
测试结构由开发者构造，不代表真实 LLM 自主发现；原始数据、详细审计仅留在 output。

2026-09-14：0811/0812 的 47 条已有 policy 历史，8 个组合、164 条候选/拒绝记录，
短名单 9 条。成功选择 readiness + 垂直返回路径，排除缺少支持的地理返回路径。
训练副本成功提交；相对原冻结模板，普通误推 0、漏推 0、平均提前量 51.4815s
均不变，ABORTED_LEAVE 可见推送仍为 6（不能表述成所有负标签均无推送）。
旧冻结模板的逐 episode 回放完全一致。本次验证不是新一轮 LLM 能力实验。

## 2026-09-15 readiness policy 双分支验证

工具现对每个正向序列固定比较 `support_only` 与 `disambiguate`。模块隔离回归实际
评估 164 个候选，提交前后 47 条冻结历史均为普通误推 0、漏推 0、平均提前量
51.4815s。

使用共享组内气压的 0811/0812/0814 共 71 个 episode，从零调用 Qwen3.7-Plus。
Agent 提出 `walking,pdr_outbound,baro_descending,lower_platform` 与
`no_baro_descent`，工具评估 170 个候选。无负向模式时，disambiguate 相比
support_only 将 false_kept 从 17 降至 16，40 个确认离家均保留；加入负向模式后，
最终按错误数优先选择 support_only：false_kept=13、missed_leave=1，优于
disambiguate 分支的 false_kept=16、missed_leave=0。冻结到 0813 的 34 条测试集后，
16 个确认离家全部识别，18 个负例中 9 个仍推送，平均提前量 28.125s；相对上一轮
模板错误数未改善且平均提前量减少 6.875s。该结果说明候选遗漏已修复，但不能说明
disambiguate 在扩展训练集和任意 Agent 序列上必然获胜。

后续受控试验将 `minimum_ready_event=lower_platform`，再重新校准全部上下文强度。
工具评估 130 个候选；最佳 disambiguate 分支选择 ready_prefix_length=4、
positive_strength=2.4、negative_strength=0，得到 false_kept=17、missed_leave=1。
非零旧强度直接迁移会显著增加漏报，故优化器关闭了该负向通道；最终仍选择
support_only + no_baro_descent（false_kept=13、missed_leave=1）。当前
disambiguate 与 negative_pattern 共用 negative_strength，因而关闭前缀压制也会同时
关闭独立负向模式。这是后续应解耦的实现限制。

同日使用 `DeepSeek-V4-Flash-0731` 从相同 71 条训练 episode 冷启动。Agent 提交
`walking,pdr_outbound,baro_descending,wifi_detach,lower_platform`，工具从 90 个
变体中选择 disambiguate、ready_prefix_length=3、positive_strength=0.4。训练冻结
回放为 false_kept=12、missed_leave=0、mean_lead_s=74.881s；冻结到 0813 后为
false_kept=9/18、missed_leave=0/16、mean_lead_s=38.125s。相对无模板基线，0813
漏报由 3 降至 0，但误推仍为 9，说明该长序列主要改善召回，并未提高测试集负例
区分率。prompt 因此进一步要求正式提案逐事件报告增量区分力，并在错误相同时强制
优先最短序列；这项新增约束尚未重新训练验证。
