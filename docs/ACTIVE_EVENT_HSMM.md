# HSMM 原子事件缺席中性

原子通道使用 `w * x * log(mu_state)`，替代原来的
`w * [x*log(mu_state) + (1-x)*log(1-mu_state)]`。
`x=0` 对所有状态贡献零对数似然，等价于乘以 1；弱激活按 x 连续缩放。
walking、PDR、GPS、Wi-Fi、Cell、BLE、时间、气压使用同一规则。
这是一种事件证据势函数，不再是完整的 Bernoulli 观测概率模型。

这里“发生”指当前原语激活，并非只消费事件上升沿：持续 walking、detach
等仍会在后续 tick 激活。GPS 新定位时间戳和跨 tick 去重尚未在本次实现。

HSMM 内 attached/approaching 的固定行为惩罚也被移除。
Agent 模板匹配出的 negative_pattern_match 通过现有独立序列项抑制 LEAVING；
取消路径也可输出这个负向匹配。没有模板、没有匹配或序列可靠度为零时不惩罚。
空间 relation、显式时长、状态转移及产品层 OUTSIDE/approaching/attach 禁推保持原有语义。
INSIDE 对 AT/PRE/LEAVING 仍使用相同的 0.65。

## 验证与限制

在相同的 0813 已存观测上比较变更前后，冻结同一个 Agent 模板和阈值：
34 条 episode，推送数从 24 增至 34；9 条五楼闲逛从全部不推变为全部推送。
冻结模板 negative_pattern 为空，仅有垂直返回 cancel_paths，因此未阻止这些误推。
本次没有重新训练 Agent，不能将结果解释为新的个性化模型准确率提升。

按历史逐 episode 真值表关联（16 正例、18 负例），baseline 的 TP/TN/FP/FN
从 16/10/8/0 变为 16/0/18/0；真阳性平均提前量从 28.125 秒变为 93.75 秒。
冻结模板的混淆矩阵相同，平均提前量从 28.75 秒变为 93.75 秒。
这些是固定观测 HSMM 回放结果，不是完整真机闭环测试。

缺席中性不等于后验保持不变：时长与转移先验仍会推进状态，持续出现的正向
原语也会累积证据。因此需用新的发射语义重新验证、学习负向模式。
不以恢复缺席惩罚或提高阈值来掩盖本次行为变化。

C++ HSMM 测试验证零原子与全禁用通道后验一致、负向序列抑制方向、
零序列可靠度中性，以及 OUTSIDE 确认；SceneEngine smoke 验证推送接口。
本地逐 episode 对照：output/active_event_comparison.json（不提交原始数据）。
