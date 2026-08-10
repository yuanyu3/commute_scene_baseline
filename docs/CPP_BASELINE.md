# C++ 实时基线（无 Python）

权威实现：`sa_cpp/` → `SceneEngine` / `BaselineRuntime`。  
已接到 `sa_service` 的 `ProactiveAgentBusinessModule` tick。

默认 `theta.focus_side = "company"`：**仅下班离开公司**流程会推送（`LEAVE_COMPANY_NOTIFICATION`）；离家推送关闭。改回双边时设 `"both"`。

## 模块

| 头文件 | 作用 |
|--------|------|
| `commute_sa/scene_engine.h` | Score + FSM（原 `python/.../engine.py`） |
| `commute_sa/anchors.h` | 加载 `anchors.json` |
| `commute_sa/theta.h` | 加载 / 改 θ |
| `commute_sa/baseline_runtime.h` | SA 单例封装（含 RadioEvidence） |
| `commute_sa/radio_evidence.h` | 在线 WiFi/CELL leave（无先验指纹） |
| `commute_sa/pdr_evidence.h` | 步行 episode 净外向位移 → leave 打分 |
| `commute_sa/geo.h` / `crs.h` | 距离 / CRS |

详见 [RADIO_EVIDENCE.md](RADIO_EVIDENCE.md)、[PDR_EVIDENCE.md](PDR_EVIDENCE.md)。

## SA 行为

1. `Initialize` → `BaselineRuntime::Init(anchors.json, theta.json)`（缺文件用内置默认）
2. 步行事件 → `OnWalkingStarted/Stopped`
3. PDR 点 → `OnPdrPoint`（填充 `pdr_net_out_*`）
4. WiFi/CELL 回调 → `OnWifiScan` / `OnCellSample`（填充 radio detach）
5. 每 tick → `OnTick`；若 `should_service` → `agent_responses` 记 `BaselinePush`，**跳过 LLM**
6. 设备路径：
   - `/data/service/el1/public/commuteagentservice/anchors.json`
   - `/data/service/el1/public/commuteagentservice/theta.json`

## 主机验证

```bash
cd sa_cpp
cmake -B build -DCOMMUTE_SA_BUILD_SMOKE=ON
cmake --build build
./build/commute_scene_smoke
./build/commute_radio_smoke
./build/commute_pdr_smoke
```

## Python

`python/commute_baseline/` **仅离线回放/实验**，运行时不依赖。
