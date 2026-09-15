# Context Engine：逐 tick 四状态上下文证据

原语目录和GPS有效性见 [EVENT_CATALOG_SAFETY.md](EVENT_CATALOG_SAFETY.md)。
统一入口见 [PERSONALIZATION_ENTRY.md](PERSONALIZATION_ENTRY.md)。

## 当前在线链路

原子语义观测 → 模板匹配 → ComposeContextEvidence → 四状态修正 → HSMM发射。
模板不改写walking、PDR、Wi-Fi等原始语义，不改变HSMM转移拓扑。

保留三种组合：
- positive_sequence：有序正向过程。
- negative_pattern：同 tick 的负向事实合取。
- cancel_sequence / cancel_paths：有序返回及替代路径。

readiness_policy 控制正向序列未达到 ready_prefix_length 时的行为：
- support_only：中性。
- disambiguate：序列已经开始、尚未就绪且下一事实可观测时，输出 context_prefix_incomplete=1。
- 下一事实为UNKNOWN时，该tick不产生未完成前缀反证；达到ready立即解除。
- 未开始序列保持中性；保留现有时间倒退、tick gap、生命周期及返回重置。
- 对任意 Agent 正向序列提案，确定性工具都同时评估 support_only 和 disambiguate；
  Agent 不需要预先选中策略，最终策略由同一历史回放与安全保护决定。

## 四状态融合与校准

positive_strength、negative_strength、return_strength 分别校准。
negative_pattern_match 和 context_prefix_incomplete 取最大值，不相加。
返回匹配存在时由return通道处理，不重复计算普通负向。
sequence_ready 替换progress/complete加分；四状态修正有界，强度0自动中性。

确定性工具枚举前缀，使用两轮四轴搜索：
positive_strength、negative_strength、return_strength、ready_prefix_length。
强度候选为0、0.2、0.6、1.2、2.4。首先比较错误数量，随后比较提前量等评分。
逐episode保护、历史回放与提交门槛保持不变。

## 已删除功能

条件缺失等待机制已删除：不再提供absence_trigger、absence_expected、absence_wait_s，
不再包含ContextAbsenceClock或等待时间搜索轴。
context_absence 已更名为 context_prefix_incomplete；context_wait_s 已删除。
日志使用 obs_context_prefix_incomplete，回放trace使用 context_prefix_incomplete。
模板输出schema为9；旧JSON中的已删除字段不再读取，不再产生效果。
如果旧模板依赖条件缺失压制，应重新生成并回放验收，不能假定旧效果继续存在。

## 验证

覆盖前缀未完成、UNKNOWN中性、就绪解除、重置、负向去重、零强度和HSMM单次融合。
对当前已冻结的47条历史比较逐episode推送结果与时间；该模板使用disambiguate，
不依赖已删除的等待机制。旧历史不含完整GPS时间元数据，仍按UNKNOWN保守处理。
