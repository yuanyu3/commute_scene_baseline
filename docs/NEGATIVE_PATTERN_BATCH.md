# 原语语义与负向候选批量回放

catalog.event_semantics公开当前事件阈值、时间粒度和缺失语义。所有事件均在当前
tick判断；positive_sequence/cancel_paths才负责跨tick的顺序。
no_baro_descent要求baro_available、baro_descending<0.25和lower_platform<0.5；
episode曾下降不意味着之后不能匹配。no_geo_outbound仅检查geo_outbound<0.25，
可能混入定位缺失，不等于证明没有位移。

Agent调用evaluate_negative_pattern_candidates，传入真实anchor_id和candidates：
候选间用竖线分隔，每个候选内部用逗号表示同时成立。最多8个候选，每个最多6个
白名单事件；拒绝未知事件、重复事件和重复候选。此接口不接受数值调参。

有活动模板时只替换negative_pattern，保持其正向、返回、适用性、强度和theta。
没有匹配锚点模板时使用无正向/返回的纯负向诊断，固定LOW=0.2、always。
每个候选使用独立回放状态，返回reference和完整episode_results（最多1000条），
包括首次推送、负向匹配tick数、取消时刻和评分。负向匹配tick数包含返回取消产生
的负向证据，应与reference比较，不能全部归功于新增pattern。

eligible检查相对reference的聚合指标及逐episode保护；不是最终提交许可，也不
代表候选有改善。仍须由Agent选择结构，再调用generate_context_template和commit。
批量回放不写入活动模板、候选trial、theta、标签或audit，不消耗一次正式生成额度。
Prompt要求每任务最多两批；有表达能力且有区分假设的候选应实测后再否定。
未测试的假设必须列为缺失证据，不得声称已证明最优。一次冻结强度下的失败不能
推广为所有强度均失败。本次不改变HSMM实时语义或提升负向默认强度。

本地验证入口：scripts/check_negative_pattern_batch.py --binary <offline_tools>
--source <product_root> --anchor-id <id> --output <local_report.json>。
工具编译到sa_cpp并同步OHOS；主机personalizer和端侧ActionTools均注册同名接口。

Qwen3.7-Plus在0811/0812验证中自主调用新工具提交8个候选，包括两个no_*原语、
它们的合取、approaching、attached及组合。工具全部返回score_not_improved，Agent
最终NO_OP；本轮已实现实测后拒绝，而不是按名称提前否定。
模型仍在审计中使用“最优”等过强结论，并混用diagnose默认20条与批量47条的评分。
这不受本工具验证支持。应比较相同n_episodes的reference/candidate，且只能说当前
已测试候选在冻结强度下没有改善，不能证明整个搜索空间最优。

0811/0812的47条episode验证：冻结已有LOW=0.2模板，分别评估no_baro_descent、
no_geo_outbound及两者合取。三者均未减少14条普通误推，漏报仍为0，平均提前量
分别为124.630、124.444、124.630秒（reference为132.778秒），均以score_not_improved
拒绝。此结论来自本次确定性回放，不意味着原语不匹配，也不证明所有强度或结构均无效。
