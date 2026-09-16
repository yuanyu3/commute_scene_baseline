# Agent 个性化最终实验结果

## 1. 实验范围

- 锚点：`company_001`。
- LLM：`DeepSeek-V4-Flash-0731`。
- 训练集：0811、0812、0814，共71个episode；同一行为、时间接近的记录共享并对齐气压数据。
- 冻结测试集：0813，共34个episode；训练结束后才用于评估，没有参与结构、参数或记忆选择。
- 统计单位：每个episode，采用首次可见推送。全数据共105个episode，其中58个正例、47个负例。
- 同组episode分开评分，但共享传感器且可能来自同一行为，因此105条不是105次独立行为证据。
- 最终结论采用固定原始标签。Agent提出的`ABORTED_LEAVE`解释单独报告，不用于改写全数据准确率。

## 2. Agent 的主要分析输出

Agent首先进行正负样本横向对照，得到以下判断：

1. `walking`和`pdr_outbound`在正负样本中都普遍出现，区分力弱，不再放入最终正向序列。
2. 正例有效下降量中位数约19.945m，负例中位数约10.23m，但分布存在重叠，不能只用单一高度值解释全部负例。
3. 误推可分为三类：
   - 浅下降：8.8–10.23m，没有`lower_platform`；
   - 深下降后返回：存在`baro_ascending → vertical_closure`；
   - 深下降但没有观测到返回：与部分真正离开过程高度相似。
4. 最短有用正向结构为`baro_descending → lower_platform`，用`disambiguate`在关键前缀尚未完成时提供负向上下文。
5. `no_baro_descent`、`no_geo_outbound`、`approaching`、`attached`负向候选没有通过确定性工具验收。

Agent共完成三次结构提交：

| 阶段 | 提交内容 | 训练回放结果 |
|---|---|---|
| Candidate 40 | 两事件正向序列 | 普通误推17，漏报0 |
| Candidate 39 | 两事件序列，重新校准`disambiguate` | 普通误推12，漏报0 |
| Candidate 115 | 增加返回路径候选 | 采用解释口径时普通误推8，漏报0 |

Agent还提出6条返回解释。实际冻结回放只采用4条`ABORTED_LEAVE`；固定原始标签下，这些episode仍作为负例评估。

## 3. 最终生效模板

```text
anchor_id              = company_001
applicability           = baro_ready
positive_sequence       = baro_descending,lower_platform
readiness_policy        = disambiguate
ready_prefix_length     = 2
positive_strength       = 2.4
negative_strength       = 0.6
cancel_paths            = baro_ascending,vertical_closure
return_strength         = 0
negative_pattern        = empty
```

`return_strength=0`意味着返回路径虽然保留并可统计匹配，但当前不会对HSMM产生取消强度。因此不能把采用解释后普通误推从12变为8归因于在线返回抑制；4条返回样本仍存在首次可见推送。

## 4. 锚点记忆

```text
memory_id: company_001_baro_sequence_v1
status: supported
anchor_id: company_001

claim:
company_001的离开过程可用baro_descending→lower_platform描述；
浅下降负例常在lower_platform前停止；
部分返回过程出现baro_ascending→vertical_closure。

applicability:
baro_ready；company_001；0811/0812/0814训练历史。

counterexamples:
20260814_171505、171506、171507深下降且没有观测到返回，
与正例在现有垂直特征上难以区分。

limitations:
组内同步样本不独立；0813不支持误推改善；
返回路径强度为0；duration和evidence strength尚未试算。
```

该记忆是下一轮研究上下文，不是在线观测或永久真值。原记忆中“返回路径识别误推”的表述需要结合`return_strength=0`理解，只能说明匹配和解释，不能说明阻止了首次推送。

## 5. 优化前后效果

### 5.1 训练集：0811、0812、0814

| 版本 | 正例成功 | 漏报 | 负例正确抑制 | 误推 | 平均提前量 |
|---|---:|---:|---:|---:|---:|
| 优化前 | 38/42 | 4 | 12/29 | 17 | 54.395s |
| 优化后 | 42/42 | 0 | 17/29 | 12 | 49.881s |

训练集改善：减少5条误推、消除4条漏报；正确推送平均晚4.514秒。

### 5.2 冻结测试集：0813

| 版本 | 正例成功 | 漏报 | 负例正确抑制 | 误推 | 平均提前量 |
|---|---:|---:|---:|---:|---:|
| 优化前 | 13/16 | 3 | 9/18 | 9 | 24.615s |
| 优化后 | 16/16 | 0 | 9/18 | 9 | 26.250s |

测试集改善：消除3条漏报，误推数保持9条；平均提前量增加1.635秒。

### 5.3 全数据：105个episode

| 指标 | 优化前 | 优化后 | 变化 |
|---|---:|---:|---:|
| TP | 51 | 58 | +7 |
| FN | 7 | 0 | -7 |
| FP | 26 | 21 | -5 |
| TN | 21 | 26 | +5 |
| Accuracy | 68.57% | 80.00% | +11.43pp |
| Precision | 66.23% | 73.42% | +7.19pp |
| Recall | 87.93% | 100.00% | +12.07pp |
| Specificity | 44.68% | 55.32% | +10.64pp |
| F1 | 75.56% | 84.67% | +9.11pp |
| 正确推送平均提前量 | 46.804s | 43.362s | -3.442s |

全数据结果表明，主要收益是消除漏报并减少训练集中的浅下降误推。测试集误推没有改善，因此不能声称模板已泛化解决返回和深下降误推。

## 6. 被解决和未解决的错误

新模板避免了以下5条浅下降误推：

- `20260812_110128 / 110131 / 110132`
- `20260814_170055 / 170056`

训练集固定原始标签下仍有12条误推：

- `20260812_111456 / 111501 / 111502`
- `20260812_152909 / 152912 / 152920`
- `20260814_170939 / 170941 / 170944`
- `20260814_171505 / 171506 / 171507`

0813测试集仍有9条误推：

- `20260813_165121 / 165122 / 165123`
- `20260813_165547 / 165548 / 165550`
- `20260813_165943 / 165944 / 165945`

## 7. 参数试算结果

Agent训练中曾主动提出`vertical_threshold`，但首次请求缺少依赖模板字段，没有完成估计。修复统一入口后，确定性工具重新试算：

```text
有效确认离家样本 = 40
估计vertical_threshold = 14m
评估候选 = 45
```

14m候选没有改变71条训练episode中的任何首次推送时间，误推、漏报和提前量均与当前模板相同，因此被`incumbent_score_not_improved`拒绝，没有部署。最终模板仍为`personalized_vertical_threshold=false`。

## 8. 最终结论

这一版可以作为当前Demo的最终结果：Agent从正负历史中主动识别共享前缀和高度差异，删除低区分度的walking/PDR，组合出两事件短序列；确定性工具负责强度、ready前缀、回放、安全验收和参数试算。最终模型在105条episode上把逐episode准确率从68.57%提高到80.00%，召回率达到100%。

实验边界也很明确：测试集误推仍为9条，返回路径当前强度为0，组内episode并非独立行为，结果只证明在现有数据与回放实现上的可行性，尚不等于跨用户或跨锚点泛化。

## 9. 结果文件

- Agent完整收发记录：`output/deepseek_prefix_parameters_20260915/agent_trace.jsonl`
- 最终模板：`output/deepseek_prefix_parameters_20260915/active_context_template.json`
- 结构提交历史：`output/deepseek_prefix_parameters_20260915/context_templates.jsonl`
- 结构化审计：`output/deepseek_prefix_parameters_20260915/audit.jsonl`
- 锚点记忆：`output/deepseek_prefix_parameters_20260915/anchor_memory.jsonl`
- 训练固定标签回放：`output/deepseek_prefix_parameters_20260915/fixed_truth/frozen_result.json`
- 0813冻结回放：`output/deepseek_prefix_parameters_20260915/holdout_0813/frozen_result.json`
- vertical threshold试算：`output/deepseek_prefix_parameters_20260915/vertical_trial/trial.json`
