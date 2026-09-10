# 前缀回放与连续提前量校准

本文记录首轮实现；后续已增加逐episode保护、Agent只读消融工具及0814共享气压验证，见[后续开发记录](TEMPLATE_DIAGNOSTICS_AND_SHARED_BARO.md)。

## 本次落地的范围

保留 Agent 选择事件序列、C++ 受约束校准、HSMM 在线融合的架构。不增加 expert，不改全局传感器权重、enter_leave 或状态时长，不硬编码任何建筑/楼层的提前触发规则；`arm_delay` 已从推送门控移除。

本次复用既有0812 Agent模板，不再次调用LLM。实验验证的是校准与在线解释器的改进，不是新的Agent能力消融。

## 为什么原来偏晚

旧评分把所有晚于目标窗口的推送统一扣0.5分，无法区分提前20秒和75秒；低层平台软正样本不参与提前量评分。旧序列进度项更偏向PRE_LEAVE，完整序列项才更支持LEAVING，因此等最后一个事件可能损失提前量。

## 实现

### 1. 因果前缀回放

`sa_cpp/src/theta_eval.cpp: ScoreHsmmReplay` 按时间顺序调用一次HSMM，当前输出只依赖当前及之前观测。支持 `include_prefix_trace`，输出每tick四状态概率、进度、完整匹配、前缀就绪、负向匹配，以及概率阈值/产品/步行等待门控是否通过。

`cutoff_t_ms` 在指定时间停止向解释器和HSMM输入数据，用于检验前缀不变性；结果标记 `partial_replay=true`。部分回放的总分不应用于选优：标签仍来自完整episode，而且未来episode尚未回放。该接口是诊断，不是严格按日期推进的在线训练模拟。

回放补齐了运行时已有的10分钟序列过期、时间倒退、outside/approaching/attached重置。历史窗口仍独立初始化，不能声称等价于全天连续SceneEngine回放；旧历史缺少anchor_id，回放按side筛选，使用前必须确保数据根目录只有同一锚点。运行时仍检查准确anchor_id。

### 2. 连续提前量评分

`lead = max(0, outcome_time - first_eligible_push_time)`，单位秒。

每个成功推送的正样本新增效用：

```text
late  = max(0, lead_min - lead)
early = max(0, lead - lead_max)
lead < lead_min:              utility = 1 - late / 60
lead_min <= lead <= lead_max: utility = 1 + 0.5 × (lead-lead_min)/(lead_max-lead_min)
lead > lead_max:              utility = 1.5 - early / 60
```

区间内越早效用越高，但最多只增加0.5；超过 `lead_max` 后每过早60秒少1分，因此最优点位于 `lead_max`，而不是无限提前。`CONFIRMED_LEAVE` 和已恢复的 `MISSED_LEAVE` 参与，`FALSE_PUSH` 不会因低层平台形态改写为正样本。当前输出 `score_version=6`、`lead_utility`、`mean_lead_s`、`late_seconds`、`early_seconds` 和 `lead_mae_to_target_s`；旧 counterfactual 回退评分采用同一时间效用。

### 3. 模板前缀校准

`sa_cpp/src/context_template.cpp: GenerateContextTemplateAction` 对Agent给出的长度N序列枚举：

- 原有解释方式（ready_prefix_length=0）。
- 长度1至N的就绪前缀。
- 每种方式配合原有0.2/0.4/0.6三档strength；最多21个候选。

Agent仍不指定这些数值。每个候选回放同一训练历史，必须提升得分至少0.25、不增加硬误推和漏报、不损失已确认及软正样本召回；替换已有模板时还须通过已有模板的同类检查。前缀候选缺少硬负样本时不接受。合格候选按 `score - 0.1 * strength` 选择。检查是样本计数级，不是统计安全保证，也尚不是逐episode不退化/P10提前量约束。

保存 `ready_prefix_length`。该阶段产物为 schema v4；当前运行时 schema v5 在此基础上增加 `cancel_sequence`，缺少新字段的旧模板仍兼容。CLI增加 `template_fit`，在同一进程生成并提交候选，避免分两次CLI调用丢失内存trial。

### 4. 在线作用

完整序列仍用于记录progress/complete；达到校准前缀时 `sequence_ready=1`。它是截至当前的历史模式记忆，不表示整条序列已经完成，更不是独立的新传感器测量。

新模式只使用就绪项作为正向序列势函数：

```text
log_emission[state] += 2 * strength * sequence_ready * [-1.00, 0.15, 1.20, -0.35][state]
```

不再把progress、complete与ready同时累加。负向匹配保留原有作用。HSMM的状态时长、转移、当前观测与推送门控仍有效。序列及原子传感器并非统计独立，本方法是受约束校准的额外历史势函数，不应称为严格去相关的生成概率模型。

当只选择一个事件时，优势主要来自“该事件曾发生”的持续记忆；不能声称该样本证明了复杂多事件推理能力。10分钟有效期沿用现有机制，未来跨场景验证应特别覆盖短暂离位、返回及长停顿。

## 0812训练、0813冻结验证

数据：既有 `output/context_template_0812_history`（5条）和 `output/template_0812B_validate_0813`（12条）。在独立输出目录复制必要文件，不覆盖历史根目录。为可比性，两天的“修改前”均使用0812同一模板的旧解释方式，而非0813目录里另一份不同模板。

选中：前缀长度1、strength=0.6；原序列与负向规则不变。训练集选优后直接冻结给0813，未根据0813调整搜索空间或数值。departure_time与vertical_threshold均未启用个性化。

|指标|0812旧解释|0812新解释|0813旧解释|0813新解释|
|---|---:|---:|---:|---:|
|平均提前量（含软正，秒）|81.7|100.0|51.7|71.7|
|低于90秒的累计缺口（秒）|65|45|370|235|
|硬误推|0|0|0|0|
|漏报|0|0|0|0|

0813仅看6条CONFIRMED_LEAVE，平均提前量32.5→50.8秒；另外3条软正样本平均90→113.3秒。合计两天5条硬负样本均未推送，12条正/软正样本均保留推送。

新推送时间（北京时间）：

|日期|时间|提前量（秒）|
|---|---|---:|
|0812|11:16:53|155|
|0812|15:31:00|100|
|0812|15:40:50|45|
|0813|15:18:04|70|
|0813|15:27:57|55|
|0813|15:35:09|40|
|0813|15:58:47|60|
|0813|16:04:33|40|
|0813|16:09:38|40|
|0813|16:53:23|115|
|0813|16:57:19|135|
|0813|17:01:10|90|

0812三次推送恢复到此前旧版报告中的时间；0813仍有样本未恢复旧版提前量，且6条confirmed均未达到90秒目标。因此是明确缓解偏晚，不是彻底解决所有提前量问题。

重要限制：软正标签沿用现有“FALSE_PUSH且出现低层平台”规则，利用同一气压信息决定标签存在循环评价风险，并非独立人工真值。17条同一人的窗口不能证明跨人/跨建筑泛化。0813曾在历史开发中被分析过，本次冻结验证也不是全新盲测。下一步应优先补充独立标注及新日期连续数据，而非继续按这17条叠加补丁。

## 复现与测试

WSL下运行（输出目录必须尚不存在）：

```bash
cmake --build sa_cpp/build_github_sync -j4
python3 scripts/test_prefix_replay.py sa_cpp/build_github_sync/commute_offline_tools
python3 scripts/validate_prefix_replay.py \
  --binary sa_cpp/build_github_sync/commute_offline_tools \
  --train output/context_template_0812_history \
  --validation output/template_0812B_validate_0813 \
  --output output/prefix_lead_reproduction
```

本地详细报告：`output/prefix_lead_final/report.json`，包含候选、冻结模板和逐tick概率，不上传个人回放日志。C++ smoke覆盖旧模板、新的非气压前缀、过期、原观测保持、ready/complete不重复计数；Python合成CLI测试覆盖连续计分、软正计时、标签/结果时间不影响推断以及前缀截断不变性。

只完成工程源码和离线验证；未部署手机，未更新独立SA9902宿主。
