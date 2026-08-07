# commute_scene_baseline/sa_service

绿区commute_scene_baseline/sa_service sa层

## 更改模型配置

LeavingHomeBaselineAgentConfig.h
LeavingHomeBaselineAgentConfig.cpp

## 业务层连接感知数据到agent构建输入返回
ProactiveAgentBusinessModule.cpp

## API key 注入
```
SA_AGENT_BASE_URL = YOUR_BASE_URL
SA_AGENT_API_KEY = YOUR_API_KEY
// 存到端侧路径 /data/service/el2/9903/agent.env
```


## 日志打印
StartSensorCollection后数据打印到以下路径
/data/service/el1/public/commuteagentservice/sa_sensor_test/CURRENT_TIMESTAMP
- raw data： sa_perception_ticks.csv
- 预处理后输入：semantic_snapshots.csv
- LLM输入和回复 ： agent_responses.csv 