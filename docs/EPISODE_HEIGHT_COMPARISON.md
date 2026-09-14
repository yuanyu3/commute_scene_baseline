# Agent episode 清单与高度对照

get_personalization_history_summary 在原有聚合统计上增加：

- episode_catalog：当前锚点实际存在的正负 episode ID、原始标签、tick 数、
  有效高度 tick 数和最大下降量（米）。
- height_comparison：有效高度样本的正负组数量、最小值、中位数、最大值。

高度必须同时具有 obs_baro_available=true 和数值 baro_descent_m 才参与。
缺测输出 null，不补零；统计直接读取连续高度字段，不根据 lower_platform 反选样本。
已验证的 ABORTED_LEAVE 沿用现有统计口径从正负组排除，通过返回候选工具查询。
近同步记录仍按 episode 展示，不能把它们当作独立物理行为扩大置信度。

Agent 从清单选取真实 ID，查询正例和负例的动态时序，检查范围重叠后自主决定
是否提出高度参数或结构假设。没有指定目标阈值、楼层或正向序列。
完整 episode 最大高度仅供离线研究，在线运行只能使用当时已观测前缀。

验证入口：commute_offline_tools ROOT history_summary '{"anchor_id":"company_001"}'。
本次真实 LLM 重跑沿用先前 71 条语义训练历史、相同初始 theta、相同 query 和模型；
未重新抽取原始传感器，因此属于输入接口与研究流程的对照，非新采集或独立泛化验证。

## 2026-09-14 实测

Qwen3.7-Plus 读取了有效正例 ID 并实际查询其时序；指出正例最大下降范围
19.22–29.66m、负例 0–20.31m 存在重叠。自主提出并提交
walking,pdr_outbound,baro_descending,lower_platform。
最终 support_only、ready_prefix_length=0、positive_strength=2.4；
无负向模式、无返回路径，未估计 vertical_threshold。高度信息进入了结构，
不意味着阈值已学习或所有楼内行为被阻止。

冻结原始标签回放 71 episode：基线 FP=17/FN=9，新模板 FP=11/FN=0，
TP=42/TN=18，平均正例提前量 48.6905s；前轮 Agent 为 FP=14/FN=0，
提前量 66.3095s。目标 110128、110132 均不再推送。
Agent 尝试 DURATION/LEAVING，但由于丢失正例被逐 episode 保护拒绝。
工具名/状态处理仍有失败重试，单次结果不能证明稳定的自主研究能力。

结果与完整工具输入输出位于本地 output/height_catalog_agent_test。
