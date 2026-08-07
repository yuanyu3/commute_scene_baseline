# C++ 实时基线（无 Python）

权威实现：`sa_cpp/` → `SceneEngine` / `BaselineRuntime`。  
已接到 `sa_service` 的 `ProactiveAgentBusinessModule` tick。

## 模块

| 头文件 | 作用 |
|--------|------|
| `commute_sa/scene_engine.h` | Score + FSM（原 `python/.../engine.py`） |
| `commute_sa/anchors.h` | 加载 `anchors.json` |
| `commute_sa/theta.h` | 加载 / 改 θ |
| `commute_sa/baseline_runtime.h` | SA 单例封装 |
| `commute_sa/geo.h` / `crs.h` | 距离 / CRS |

## SA 行为

1. `Initialize` → `BaselineRuntime::Init(anchors.json, theta.json)`（缺文件用内置默认）
2. 步行事件 → `OnWalkingStarted/Stopped`
3. 每 tick → `OnTick`；若 `should_service` → `agent_responses` 记 `BaselinePush`，**跳过 LLM**
4. 设备路径：
   - `/data/service/el1/public/commuteagentservice/anchors.json`
   - `/data/service/el1/public/commuteagentservice/theta.json`

## 主机验证

```bash
cd sa_cpp
cmake -B build -DCOMMUTE_SA_BUILD_SMOKE=ON
cmake --build build
./build/commute_scene_smoke
```

## Python

`python/commute_baseline/` **仅离线回放/实验**，运行时不依赖。
