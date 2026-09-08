# Agent 多分辨率时序证据

## 目标和边界

Agent 不再只依赖整段 episode 的最大值和布尔摘要。当前实现提供三级读取路径：

1. `get_leave_sensor_summary` 和 `get_aborted_leave_candidates` 用于低成本概览与候选筛选。
2. `get_episode_semantic_timeline` 按 5～60 秒对齐逐 tick 语义，显示均值、峰值、字段覆盖与空时间段。
3. `get_episode_dynamic_diagnostics` 由确定性代码计算事件区间、时序、持续、恢复和跨传感器时差。

这些接口读取 `policy_history.jsonl` 中端侧已经生成的语义观测，不读取高频原始波形，
也不暴露 GPS 坐标、BSSID、Cell ID。Agent 负责选择值得展开的 episode 和窗口，
不负责从长数组中手算斜率或持续时间。

## 对齐时间轴

`get_episode_semantic_timeline` 的身份键是 `episode_id + side + outcome_t_ms`；同名 episode
映射多个 outcome 时必须明确提供 outcome。可用 `start_ms/end_ms` 缩小窗口，默认 10 秒 bin，
最细 5 秒，每次最多 120 个 bin。超过预算时工具返回所需数量，不静默截断。

每个 bin 包含：

- `sample_count` 与 `quality=OBSERVED|MISSING`；
- 气压可用 tick 比例、下降均值和最大值；
- walking、PDR/GPS外向、Wi-Fi/Cell/BLE detach、时间先验、气压下降/平台/回升/闭合的均值、峰值和已知tick数；
- inside/near/outside/approaching/attached 的已知tick数和活跃tick数；
- 原始历史存在时的 PRE_LEAVE/LEAVING 概率、hits、lead 和PDR净位移上下文。

`known_ticks=0` 表示该字段在存储历史中缺失。已知且均值为0才表示这个时间段内语义值为0。
空 bin 保留为 `MISSING`，不做插值。连续信号聚合后仍会丢失 bin 内波形细节，
因此它用于假设筛选和时间定位，不替代原始传感器算法开发。

示例：

```json
{
  "episode_id": "20260814_170939",
  "side": "company",
  "bin_s": 10,
  "start_ms": 1786698755000,
  "end_ms": 1786698825000,
  "max_bins": 20
}
```

## 确定性动态诊断

`get_episode_dynamic_diagnostics` 返回：

- 存储tick数、tick中位间隔、气压覆盖率；
- 最大下降和发生时间；
- 下降阶段的近似单调比例，允许相邻0.5米噪声；
- 首次下降、低层平台、下降后回升、回升后闭合的严格顺序时间；
- 下降到峰值、峰值到闭合的耗时；
- 下降与PDR外向、GPS外向、Wi-Fi detach，以及峰值与回升的时差；
- 12类事件的连续区间、首末活跃tick、观测跨度、按一个中位tick估计的持续时间、支持tick数和峰值。

区间在相邻活跃tick间隔超过两个中位tick（至少10秒）时断开。`observed_span_s`
是首末tick之差；`estimated_duration_s` 额外计入一个中位tick，二者都只是存储语义分辨率下的描述。

实际 `0814_170939` 诊断显示：气压下降到14.21米峰值约115秒，峰值10秒后出现回升，
峰值后30秒出现闭合；Wi-Fi detach 比下降事件晚45秒，并持续到记录结束。
因此该历史支持垂直返回，不支持“闭合后Wi-Fi恢复”的说法。

## Agent 使用规则

Prompt 要求 Agent 先看概览，再展开能区分竞争假设的 episode。事件持续、恢复顺序和时差
必须引用动态诊断，不能只靠10秒表格目测。timeline 拒绝过长请求时，Agent应增大 bin 或缩小窗口。

这种安排扩大了 Agent 的分析视野，但没有允许它生成新C++特征、直接改HSMM参数或读取任意原始数据。
当前动态诊断集合仍由代码预先实现；白名单算子计算图、按信息增益选择下一实验、跨episode
相似前缀检索和原始Wi-Fi质量/真实重连语义仍属于后续工作。

## 验证

`commute_offline_tools` 新增本地命令：

```text
semantic_timeline
dynamic_diagnostics
```

smoke 覆盖有序返回时间、字段存在性、输出bin预算，以及重复时间戳拒绝。主机和OHOS使用同一
`evidence_query.cpp`，通过 `sync_scene_engine_to_sa.ps1` 同步。证据接口不进入逐tick推断路径，
因此不会改变HSMM概率或推送时间。

使用去重后的0812/0813/0814共20条episode执行了一次真实Jiuwen Agent隔离运行。
Agent实际调用了新时间轴和动态诊断工具，保持 `theta.json` 不变，最终仍选择：

```text
positive_sequence = baro_descending,lower_platform,geo_outbound
cancel_paths = baro_ascending,vertical_closure
ready_prefix_length = 2
strength = HIGH / 0.6
```

多分辨率信息没有使这批数据产生不同模板，但修正并扩展了归因：Agent指出一个确认离开episode
缺少气压数据，并将两个“显著下降但未回升闭合”的episode保留为不确定，而非强行标为返回。
它仍采用当前训练回放评分；该评分不代表独立泛化结果。接口本身只读，所以本次更新没有改变
此前记录的推送时间、65%原始标签回归正确率或7/13原始负样本误推结果。
