/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Host-side log stub for ProactiveAgentBusinessModule unit tests (no Hilog).
 */

#ifndef PROACTIVE_AGENT_HOST_LOG_STUB_H
#define PROACTIVE_AGENT_HOST_LOG_STUB_H

#ifdef PROACTIVE_AGENT_HOST_TEST

#define CAMERA_AGENT_LOG_DEBUG(fmt, ...) ((void)0)
#define CAMERA_AGENT_LOG_ERROR(fmt, ...) ((void)0)
#define CAMERA_AGENT_LOG_WARN(fmt, ...) ((void)0)
#define CAMERA_AGENT_LOG_INFO(fmt, ...) ((void)0)
#define CAMERA_AGENT_LOG_FATAL(fmt, ...) ((void)0)

#endif // PROACTIVE_AGENT_HOST_TEST

#endif // PROACTIVE_AGENT_HOST_LOG_STUB_H
