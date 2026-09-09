# Jiuwen 受约束个性化 Agent

当前 Agent 不参与每 tick 判定，也不能直接写任意 HSMM 参数。实时离家识别始终由端侧 C++ 基线完成；Agent 只在 episode 结算或日终低频运行。

## 职责边界

Agent 负责：

- 跨 episode 阅读语义证据，区分结构缺失、证据可靠度不匹配、持续时间先验不匹配和无需修改。
- 每次只选择一个干预类型：`STRUCTURE`、`EVIDENCE_STRENGTH`、`DURATION` 或 `NO_OP`。
- 在白名单原语中组织正向、负向和取消序列；或请求受约束工具估计证据强度/持续时间候选。
- 根据工具返回的历史回放结果决定提交或放弃，并用 `submit_agent_analysis` 留下唯一的最终审计。

Agent 不负责：

- 实时分类和推送。
- 直接指定任意浮点参数。
- 绕过候选范围、历史回放、产品安全门控或原子提交。

## 工具分组

| 分组 | 主要工具 | 作用 |
|------|----------|------|
| 证据 | `get_error_stats`、`get_leave_episode`、`get_semantic_timeline`、`get_anchor_history_summary` | 获取当前 episode 与 anchor 级历史事实 |
| 结构 | `get_context_template_catalog`、`generate_context_template`、`begin_context_template_trial`、`commit_context_template_trial`、`discard_context_template_trial` | 在白名单原语中生成并验证序列模板 |
| 强度 | `estimate_evidence_strength_profile`、`begin_evidence_strength_trial`、`commit_evidence_strength_trial`、`discard_evidence_strength_trial` | 由历史统计生成离散可靠度候选并验证 |
| 时长 | `fit_hsmm_duration_profile`、`begin_hsmm_duration_trial`、`commit_hsmm_duration_trial`、`discard_hsmm_duration_trial` | 由标注 episode 拟合受限 duration 候选并验证 |
| 审计 | `submit_agent_analysis` | 记录唯一的最终类型化决策 |

## 单次调用流程

```text
读取 anchor 级历史与当前 episode
        ↓
提出可证伪的错误归因
        ↓
选择一个干预族或 NO_OP
        ↓
受约束 trial → 历史前缀回放
        ↓
commit / discard
        ↓
submit_agent_analysis
```

结构、强度和时长不能在同一轮混改，以便审计能够把效果归因到一种变化。`submit_agent_analysis` 必须记录 `anchor_id`、干预类型、证据引用、候选/工具结果和最终 `decision`；`NO_OP` 也必须说明为什么没有足够证据修改。

## 实现位置

- Prompt：`jiuwen_agent/system_prompt.md`
- 工具契约：`jiuwen_agent/tools_contract.json`
- 端侧注册：`sa_service/services/src/sa_agent/ActionTools.cpp`
- 主机试跑注册：`examples/personalizer_llm/register_tools.cpp`
- 确定性候选与回放：`sa_cpp/src/personalization_optimizer.cpp`
- 结构模板：`sa_cpp/src/context_template.cpp`

直接 θ 修改和旧策略 API 只保留为底层离线消融实现，不注册给生产 Agent，也不出现在当前工具契约中。

更多语义输入见 [AGENT_SEMANTICS.md](AGENT_SEMANTICS.md)，结构模板细节见 [CONTEXT_TEMPLATE_PERSONALIZATION.md](CONTEXT_TEMPLATE_PERSONALIZATION.md)。
