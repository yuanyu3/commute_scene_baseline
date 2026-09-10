# 非特异基线的冻结留出验证

## 目的

该实验用于区分“训练历史上的拟合改善”和“Agent 个性化在未参与训练的数据上的真实收益”。通用 HSMM 不直接使用 `lower_platform`，模板可继续把它作为场景原语；`arm_delay_s` 与 `lead_max_s` 不作为实时推送门控，提前量只进入离线评分。

真实数据预处理由 Python 回放层完成，最终评分使用 `sa_cpp` 的 HSMM 与模板实现。为避免两套实现语义不一致，Python 镜像同步遵循以下约束：

- `python/commute_baseline/hsmm.py` 的通用发射不包含 `baro_lower_platform`；
- `python/commute_baseline/engine.py` 不用 `arm_delay_s` 或 `lead_max_s` 阻断推送；
- `lower_platform` 仍保留在观测与历史中，供 Agent 白名单原语和模板匹配使用；
- `lead_max_s` 仍是有界提前量评分的目标，不是在线门禁。

## 固定流程

1. 按 episode 独立回放；缺少 `sensor_events` 的 episode 直接排除。
2. 仅允许时间相近的同组三路记录共享气压数据，不跨行为组补齐。
3. 使用 0811、0812、0814 作为训练集，Agent 只能读取这部分历史。
4. Agent 完成后冻结 `theta.json` 与 `active_context_template.json`，记录 SHA-256。
5. 冻结后才载入 0813；测试阶段不得再解释标签、调参、筛选模板或调用 LLM。
6. 同时输出通用基线和冻结个性化版本的逐 episode 推送结果，并报告 TP、TN、FP、FN、准确率、精确率、召回率、特异度、F1、平均提前量及 score v6。
7. Agent 对训练 episode 的 `ABORTED_LEAVE` 重解释必须单独列出。训练分数比较还应保留原始标签再回放一次，避免把标签重解释误报为模型收益。

## 2026-09-10 留出结果

训练集包含 71 个有效 episode（42 正、29 负），测试集包含 34 个有效 episode（16 正、18 负）。Agent 没有修改 θ、reliability 或 duration，只提交了一个 `ready_prefix_length=4`、`strength=0.4` 的结构模板及垂直返回取消路径。

在 0813 冻结测试上，基线与个性化版本均为 TP=13、TN=10、FP=8、FN=3，准确率 67.65%，F1 0.703。平均提前量从 26.15 秒变为 26.54 秒：一条正样本提前 5 秒，但一条负样本也提前 5 秒，分类结果没有变化。

因此本轮结果证明模板已接入在线 HSMM，却没有证明跨日识别能力得到提升。主要原因是四事件 ready 前缀与通用 HSMM 已使用的证据高度重合，而返回取消通常发生在已推送之后，不能撤销用户已经看到的通知。完整逐 episode 结果、Agent trace 和冻结文件仅保存在本地忽略目录，避免上传原始传感器数据与个人日志。
