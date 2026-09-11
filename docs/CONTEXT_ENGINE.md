# Context Engine：逐 tick 四状态上下文证据

## 当前优化优先级

用户明确要求正确率优先。候选搜索和最终选择按以下顺序比较：普通假推数 + 漏报数、
ABORTED_LEAVE 的可见推送数、原有综合评分（包含提前量）。前两项更优时不要求综合
评分也更高；前两项相同才要求综合分改善至少 0.25。保留旧评分用于解释，不把它当作
跨错误数量的单一排序值。

误推、漏报均不可增加；保留逐 episode 的新假推/丢失正例保护，不能通过交换出错
样本伪装收益。正例晚推及返回取消稍晚不再作为独立硬否决；已识别返回不能丢失。
因此可以接受减少假推、漏报带来的提前量下降。以下首版结果属于修改前历史记录。

正确率优先改动后的 0811/0812 回放：同一 no_baro_descent 候选通过提交，普通假推
14→6、正例漏报 0→0、27 正例全部识别，平均提前量 132.778→91.2963 秒。
确定性工具选中 positive_strength=1.2、negative_strength=1.2、return_strength=0、
ready_prefix_length=1。返回路径匹配仍保留，但该候选返回通道不产生数值修正。
6 条 ABORTED_LEAVE 的可见推送数量没有改善。模板只提交到本次隔离实验目录，
未部署手机；本轮是既有结构的确定性复验，未重新运行 LLM，尚未验证 0813 泛化。

## 已实现的数据路径

传感器原语 → 有序正向/返回匹配、同 tick 负向匹配、条件缺失计时 →
`ComposeContextEvidence` → `context_scores[AT, PRE, LEAVE, OUT]` →
HSMM 发射分数 → 状态与年龄桶递推 → 原有推送门控。

`context_scores` 是有界对数证据，不是概率。每个状态的观测分数只加一次该向量。
`context_available=true` 时跳过旧的 sequence 加分路径。原始传感器语义不被改写，
HSMM 转移拓扑、duration、产品门控保持现有实现。

实现入口：`sa_cpp/include/commute_sa/context_engine.h`、
`sa_cpp/src/context_template.cpp`、`sa_cpp/src/leave_hsmm.cpp`。
新引擎为 header-only，主机和 OHOS 使用同一源；没有在线 LLM 调用。

## 证据融合

正向保留 sequence_progress / sequence_complete。工具可选择已校准前缀 readiness；
使用 readiness 时不再同时累加 progress 和 complete。前缀从 0 到序列长度都可测试，
无需强制等待完整过程。前缀本身的判别力由历史回放评分评估，不新增另一套分类器。

正向、负向、返回使用独立的 positive_strength、negative_strength、return_strength：

```
c[s] = clip(2 * (positive_strength * positive_term[s]
              + negative_strength * negative_support * negative_direction[s]
              + return_strength * return_support * negative_direction[s]), -6, 6)
negative_direction = [0.80, 0.25, -1.00, -0.25]
```

同 tick 负向模式与条件缺失取 max，避免重复叠加；返回已触发时由返回通道负责负向
修正，避免 cancel 同时被当作 negative_pattern 再算一次。强度为 0 自动中性。
这是有界、可校准的判别式上下文修正；不声称与原子观测统计独立或为精确贝叶斯似然。

## 条件缺失 DSL

Agent 的 generate_context_template 新增两个可选字符串：

```
absence_trigger: 逗号分隔的有序正向原语，最多 6 个
absence_expected: 触发之后预期出现的一个事件
```

二者必须成对提供，expected 不能同时位于 trigger 中。当前 expected 只允许
baro_descending、lower_platform、baro_ascending、vertical_closure，因为当前观测
只对这个传感器族提供明确 availability。该限制是数据契约限制，不是所有场景必须下楼。
其他传感器族在接入有效性/质量定义之前不能把零观测当成物理缺失。

触发序列每 tick 最多推进一步。触发完成后，连续两次有效观测之间的秒数才累积。
支持度为 clip(valid_seconds / absence_wait_s, 0, 1)，第一次触发为 0。
缺测 tick 输出 0，不增加计时；恢复后的第一 tick 也不计入缺测间隔。
expected 达标后锁存“已满足”，在本上下文内持续解除缺失反证。
计时不读取标签、outcome 或未来 tick。

上下文遇到时间倒退、超过 30 秒 tick 间隔、outside 或超过 600 秒生命周期时重置；
返回完成会重置缺失状态，返回保持期不累加负向通道。现有 attached/approaching
重置约定仍保留。30/600 秒是首版固定生命周期参数，尚未个性化。
baro_ready 不适用时保持中性并断开有效计时间隔；此处仅用可用性门控，尚未新增
连续气压质量估计或 freshness 元数据。

## Agent 和确定性工具的职责

Agent 提供正向、负向、返回和可选缺失结构及证据。工具不读取 Agent 提供的数值。
generate 自动启用新引擎：先枚举旧 LOW/MEDIUM/HIGH 与前缀，再做两轮有界坐标搜索。

- 三通道强度候选：0、0.2、0.6、1.2、2.4。
- 条件缺失等待时间：10、30、60、120 秒。
- 再次检查前缀长度，避免仅在旧共用强度下选择前缀。
- 所有拟合/比较使用同一历史集合，最多 1000 episode；冻结测试不拟合。
- 每个候选返回实际独立参数、评分、是否合格及拒绝原因，最终仅提交通过保护的候选。

坐标搜索不是穷举全部组合，不保证全局最优。当前使用既有全历史训练评分，无新增
嵌套验证、自动划分或泛化保证。batch_negative 仍是固定配置的只读结构诊断；其失败
不能替代 generate 的独立数值校准结果。

旧 schema 6 模板按旧方式运行，兼容性回放逐 episode 验证；新生成并通过提交的模板
写入 schema 7、context_engine=true 及独立参数。不会静默把旧模板换成新强度。
审计仍使用 STRUCTURE 类型，确定性拟合参数记录在 trial/模板历史中。

## 验证与实际限制

单元/接入 smoke 覆盖：有效时间增长、缺测中性、事件达成解除、时间倒退/长间隔重置、
负向去重、返回去重、零强度、HSMM 单次融合、概率归一化、模板持久化后的在线应用。
逐 tick 回放 trace 和线上 policy 日志新增 context_scores、context_absence、context_wait_s；
回放从原子观测重建上下文，不能复用旧日志的修正分数。

0811/0812 共 47 episode：27 正例、14 普通负例、6 已有解释的 ABORTED_LEAVE。
旧模板的假推 14、正例漏报 0、正例平均提前量 132.778 秒。
开发者定义的 no_baro_descent 独立强度候选可达到假推 6、漏报 0、平均提前量约
91.30 秒，但被 positive_push_delayed 拒绝。这是候选诊断，未提交，不能计作部署收益。
开发者定义的 walking,wifi_detach → 缺失 baro_descending 结构，在本次坐标搜索中
未得到优于旧模板的合格候选。其最高分候选中，19 个正例、3 个普通负例、2 个返回
episode 曾出现缺失证据，但全部首次出现于本次推送之后，无法撤回已推送通知。
这说明触发前缀必须足够早，新增时序结构本身并不保证收益。两次实验最终均保留旧模板。

首版的逐样本保护曾不允许任何正例推送延后，也不允许已识别返回的取消延后，会
拒绝“减少假推但略晚”的候选；该时间硬限制已按上述优先级取消。上下文能够改变
HSMM，不等于在当前数据与提交约束下必然产生更优的可部署模板。
本次只验证 0811/0812；未用 0813 调参，也未完成这次新引擎的 0813 独立泛化验证。

本地复现：scripts/validate_context_engine.py，指定 source、output、binary、old-binary。
输出区分旧模板、实际提交后结果和未提交候选诊断；真实日志/数据不进入 Git。

另做 Qwen3.7-Plus 工具接入验收：首轮只做旧固定强度 batch 后 NO_OP，未调用新校准。
第二轮明确要求调用一次 generate 验收接口，但未指定结构答案或数值；Agent 选择
保留既有正向及返回结构，未选择条件缺失结构，并实际调用 generate_context_template。
因此上文两个开发者指定候选的效果不能归因于 Agent 自主发现。
第二轮实际评估 40 个候选，best_candidate_id 为空，最终 NO_OP。模型审计仍错误地
把“本次没有更优候选”扩大为“结构最优/原语穷尽”，并把 14 条普通误推描述成仅剩
8 条；这些说法不被工具结果支持。此轮没有负向结构，因此没有拟合有效负向通道。
接口可用性已经验证，Agent 自主提出有效新结构的能力在本轮尚未得到证明。
