# 阶段 1/2 实现与修改前后对照

日期：2026-09-04

## 本次范围

阶段 1 消除 HSMM 对同一原子传感器证据的重复消费：walking、PDR、GPS outward、Wi-Fi、Cell、BLE、时间先验和气压只进入发射似然一次。状态转移只保留显式时长 hazard、固定状态拓扑和 INSIDE/OUTSIDE 结构关系。INSIDE 在发射中只排斥 OUTSIDE，不再结合原子传感器二次判断“强外向”。

阶段 2 将上下文模板从“改写原始观测”改为“产生独立序列观测”。模板不再放大 `baro_descending`、`geo_outbound`，也不再压低 walking/PDR/Wi-Fi；它只输出：

- `sequence_progress`：正向序列完成比例，0~1；
- `sequence_complete`：正向序列是否完整发生；
- `negative_pattern_match`：负向模式是否成立；
- `sequence_reliability`：由 C++ 回放验证器选择的 LOW/MEDIUM/HIGH 可靠度，与序列结构分离。

Agent 负责提出有语义的事件顺序，C++ 负责事件合法性、数值强度、历史回放和提交硬门。旧 schema 1/2 活跃模板仍可读取，但按 schema 3 的独立序列语义执行。

## 修改前后结构

```text
修改前
原子观测 ──> HSMM 转移
    ├──────> HSMM 发射
    └─模板放大/压低原子观测 ──> HSMM

修改后
原子观测 ────────────────────> HSMM 发射（一次）
时长 + 固定拓扑 + 结构关系 ───> HSMM 转移
原子事件历史 ─> 模板状态机 ───> 独立序列观测 ─> HSMM 发射
                                  ↑
                         C++ 回放选择可靠度
```

## 真实数据对照

评分定义沿用 `hsmm_window_replay`。0813 的修改前结果来自已落盘的 `frozen_eval.json`；0812 的修改前结果来自本轮改造前冻结的同一历史回放和 Agent trace。`lead_s` 越大表示推送越早。

### 0812：5 条 episode

| 版本 | 硬误推避免/保留 | 正样本保留/漏报 | score | 三条正样本 lead_s |
|---|---:|---:|---:|---|
| 修改前，无模板 | 0 / 2 | 3 / 0 | 0 | 160, 105, 45 |
| 修改前，旧模板改写观测 | 2 / 0 | 3 / 0 | 8 | 155, 100, 45 |
| 修改后，无模板 | 2 / 0 | 3 / 0 | 8 | 135, 85, 30 |
| 修改后，冻结模板独立序列 | 2 / 0 | 3 / 0 | 8 | 130, 85, 30 |

阶段 1 单独取得了原先必须依赖旧模板才能得到的分类结果，但相对修改前无模板平均晚推 20 秒。新模板没有增加 score，其中一条还晚 5 秒；重新生成 LOW/MEDIUM/HIGH 三档候选时均因 `score_not_improved` 被拒绝，未提交无收益模板。

### 0813：12 条独立验证 episode

其中 6 条 confirmed leave、3 条到低层平台的有效正过程、3 条硬误推。

| 版本 | 硬误推避免/保留 | 正样本保留/漏报 | score | confirmed 平均 lead_s | 全部 9 条正过程平均 lead_s |
|---|---:|---:|---:|---:|---:|
| 修改前，无模板 | 0 / 3 | 9 / 0 | 4.5 | 55.8 s | 80.6 s |
| 修改前，旧模板改写观测 | 3 / 0 | 9 / 0 | 16.5 | 51.7 s | 75.6 s |
| 修改后，无模板 | 3 / 0 | 9 / 0 | 16.5 | 33.3 s | 52.2 s |
| 修改后，冻结模板独立序列 | 3 / 0 | 9 / 0 | 16.5 | 33.3 s | 52.2 s |

阶段 1 在 0813 上把三条硬误推全部消除，且没有漏掉九条正过程；代价是相对修改前无模板，confirmed 平均晚 22.5 秒，全部正过程平均晚 28.3 秒。阶段 2 对分类指标没有额外增益，只在两条 confirmed episode 上分别产生 +5 秒和 -5 秒的小幅时序变化，平均抵消。

## 序列通道的方向性验证

在完全相同的原子观测上，仅改变独立序列观测，C++ 12 tick 测试得到：

| 输入 | P(LEAVING) |
|---|---:|
| 无模板序列证据 | 0.987050 |
| 正向序列完整，reliability=0.4 | 0.992409 |
| 负向模式成立，reliability=0.4 | 0.962583 |

Python 镜像得到 `0.991135 < 0.995109`（正向）且 `0.960063 < 0.991135`（负向）。离线工具测试同时断言模板执行前后 walking、Wi-Fi、GPS 等原子值不变。因此阶段 2 的机制是有效的，但当前真实样本没有提供可转化为新增分类收益的余量。

## 能力变化结论

1. **精度能力提高**：当前 0812/0813 的 5 条硬误推均被阶段 1 的去重基线避免，正过程无漏报。
2. **提前量下降**：去重移除了原来同一外向证据在转移和发射中的叠加，推送变晚 15~70 秒；这是真实代价，后续应优化持续时间先验或序列通道，而不是恢复重复计权。
3. **Agent 接口更正确、更可审计**：Agent 只表达跨 tick 的事件顺序，不碰原子权重；确定性代码掌握强度和提交权。
4. **尚未证明 Agent 带来数据集级增益**：在这两天数据上，新序列模板是冗余证据。要验证 Agent 的必要性，需要加入“单 tick 原子统计近似、但事件顺序不同”的正负样本，并进行无模板、规则提序列、Agent 提序列三组盲测。

## 验证边界

- 样本量只有 17 条，且来自同一地点和相邻两天，不能作为泛化结论。
- 当前是已保存 observation window 的反事实 HSMM 回放，不等于手机连续运行的端到端测试。
- soft lower-platform 样本按现有项目评分口径视为有效正过程；若业务标签定义改变，应重新计算。
- 本结果证明“机制方向正确、当前基线更保守”，不证明所有建筑、楼层、出行方式都更优。

## 复现检查

```powershell
wsl cmake --build /mnt/d/commute_scene_baseline/sa_cpp/build_wifi_fix -j2
wsl /mnt/d/commute_scene_baseline/sa_cpp/build_wifi_fix/commute_hsmm_smoke
wsl /mnt/d/commute_scene_baseline/sa_cpp/build_wifi_fix/commute_offline_tools /mnt/d/commute_scene_baseline/output/context_template_0812_history evaluate
wsl /mnt/d/commute_scene_baseline/sa_cpp/build_wifi_fix/commute_offline_tools /mnt/d/commute_scene_baseline/output/context_template_0812_history template_evaluate_frozen
wsl /mnt/d/commute_scene_baseline/sa_cpp/build_wifi_fix/commute_offline_tools /mnt/d/commute_scene_baseline/output/template_0812B_validate_0813 evaluate
wsl /mnt/d/commute_scene_baseline/sa_cpp/build_wifi_fix/commute_offline_tools /mnt/d/commute_scene_baseline/output/template_0812B_validate_0813 template_evaluate_frozen
```
