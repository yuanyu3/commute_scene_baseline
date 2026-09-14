# 个性化统一入口

Agent 通过四个工具完成一次干预：

1. propose_personalization：按 family 分派，生成候选。
2. get_personalization_trial：检查相同 family 的候选与回放结果。
3. commit_personalization：提交通过原有保护条件的候选。
4. discard_personalization：丢弃候选，允许下一次提案。

一次只允许一个未解决的 trial，跨 family 查询或提交会失败。
工具生成失败后也可 discard 释放该次尝试。进程重启后 trial 不保留，
已提交的模板和 anchor profile 仍由原有存储机制持久化。

| family | 提案参数 | 内部实现 | 审计 intervention_type |
|---|---|---|---|
| STRUCTURE | anchor_id、模板字段；parameter_families 为空 | Context Template 结构校准与回放 | STRUCTURE |
| PRIMITIVE_PARAMETER | anchor_id、模板字段、parameter_families=vertical_threshold | 模板内部的高度估计器及模板回放 | STRUCTURE，同时记录 family |
| EVIDENCE_STRENGTH | anchor_id、families | 原有通道强度估计器 | EVIDENCE_STRENGTH |
| DURATION | anchor_id、state=PRE_LEAVE 或 LEAVING | 原有稳健时长估计器 | DURATION |

原语参数仍与其依赖模板共同生成和验证；目前不是独立的任意原语参数优化器。
模板后端仍会校准前缀和上下文强度。要固定结构，应提交当前模板的结构字段。
不同 family 不在同一次候选中联合修改 evidence strength 与 duration。
字段 family 在使用统一工具提交审计时必填，避免只记录 STRUCTURE 而丢失高度参数申请。

示例：

```json
{"family":"EVIDENCE_STRENGTH","anchor_id":"company_001","families":"wifi,baro"}
```

```json
{"family":"DURATION","anchor_id":"company_001","state":"PRE_LEAVE"}
```

Agent 的主机、SA 工具注册表、agent_config 和工具契约均使用统一入口。
旧的 generate/commit_context_template、estimate/commit_evidence_strength、
fit/commit_duration_prior 不再作为 Agent 独立工具开放；底层 C++ 函数保留给分派器及测试。

## 清理边界

已删除未使用的 ApplyCommittedEvidenceStrengthProfile 别名、
arm_delay_s 和 allow_network_dwell_acc_m 成员及其读写，
并删除被替换的 Agent 工具包装函数。

以下仍有真实调用依赖，不能当成无用代码整文件删除：

- personalization_optimizer.cpp：结构化审计、规则对照和历史测试。
- action_ops.cpp / ApplyThetaDelta：规则对照和主机历史实验依赖；不在当前 Agent 白名单。
- personalization_policy：仍有运行时接口及旧实验调用，不能把 policy.json 当成当前推送控制面。
- 旧 w_* / w_radio 配置读取：历史训练快照迁移依赖；当前配置继续写 evidence_strength。
- 旧模板序列字段：已提交模板的加载和历史回放依赖。

这些保留项属于迁移或对照依赖，不代表 Agent 仍可直接改数值。
本次不改变 HSMM 发射、转移、Context Engine 数值公式或冻结模板。
