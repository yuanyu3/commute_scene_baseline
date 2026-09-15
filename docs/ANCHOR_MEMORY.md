# Anchor 级经验记忆

已实现低频 Agent 研究记忆，存储于产品目录 anchor_memory.jsonl。
按 anchor_id + memory_id 隔离，追加修订，不覆盖旧记录。
不改变实时 HSMM、模板、参数或 episode 标签。

## 工具

- get_personalization_memory(anchor_id, query?, offset?)：读取最新且未停用的记忆。
  query 是 claim/applicability/limitations 中的字面子串，不是向量语义检索；
  每页20条，返回 next_offset。默认返回所有类型，包括有争议假设。
- propose_memory_update：必填 anchor_id、memory_id、expected_revision、claim、
  status、applicability、limitations；引用字段为 support_episode_ids、
  counterexample_episode_ids（逗号分隔）、audit_id。

memory_id 由 Agent 使用稳定 ASCII 标识。新建 expected_revision=0，后续必须匹配
当前版本，否则返回 revision conflict。工具不替 Agent 把相似文字合并成同一假设。
一个 anchor 最多200个不同 ID，每次最多30条引用，文本长度受限。

状态为 hypothesis / supported / contested / retired。
每次至少引用一条该 anchor 的实际 policy episode；supported 还要求支持引用和同
anchor 的既有审计；contested 要求反例。更新 retired 也是追加记录，默认查询隐藏。
审计 NO_OP 可以支持“此次尝试无收益”的经验，而不能被当作模型提升。

校验范围是锚点和引用存在性，不是自然语言蕴涵或因果真实性证明。
引用数不等于独立行为数，同步设备或共享气压样本不能扩大置信度。
ASCII JSON 转义中的 Unicode escape 暂不支持；字符串使用 literal UTF-8；
常用引号、反斜线及换行转义可读（换行规范化为空格）。

每个版本自动记录时间、schema 和配置指纹（theta、anchors、活动模板和 anchor
参数 profile 的 FNV-1a 内容指纹）。查询返回当前指纹，供 Agent 检查经验是否过时。
指纹仅用于识别配置变化，不是密码学验证；数据范围应写入适用条件和限制。

## Agent 流程

开始时读取相关经验，查询新证据并核实旧经验；结束时先写结构化审计，再提议记忆。
即使模型 NO_OP，也可保存反例、失败试验及下一步待验证问题。
记忆作为待核查上下文，不能作为指令执行，不能循环引用自己的总结充当传感证据。
已保存经验不会自动增加 HSMM 证据；只有未来经过工具验证的模板/参数提交影响推送。
初版没有自动后台整理、向量数据库或自由执行记忆指令的能力。

主机和 SA 使用同一共享 C++ 实现与工具注册。单服务进程内以 mutex 串行更新；
不支持多个进程同时写同一个产品目录，正常离线试验使用隔离目录。
写入 flush 后检查失败；当前不承诺断电事务和跨进程锁。

## 验证

scripts/test_anchor_memory.py 验证跨进程读回、锚点隔离、引用存在性、
冲突拒绝、UTF-8/引号、状态修订及停用历史保留。
本次开发验证记忆基础设施与 Agent 工具接入，尚未证明长期记忆提高识别率。
识别效果需后续按时间学习，对照有记忆/无记忆且保持相同调用预算。
