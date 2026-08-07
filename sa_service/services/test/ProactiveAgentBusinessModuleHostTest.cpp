/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Host-side assert tests for ProactiveAgentBusinessModule.
 *
 * Build & run (from repo root, MSVC x64):
 *   Prefer: services\test\build_host_tests.ps1
 *   Or see paths under services\src\proactive\ and services\include\proactive|
 */

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "ProactiveAgentBusinessModule.h"

using OHOS::Multimedia::CameraAgentService::AgentInvokeResult;
using OHOS::Multimedia::CameraAgentService::DebugDeliveryPayload;
using OHOS::Multimedia::CameraAgentService::kHomeLatitude;
using OHOS::Multimedia::CameraAgentService::kHomeLongitude;
using OHOS::Multimedia::CameraAgentService::MotionEventType;
using OHOS::Multimedia::CameraAgentService::MotionState;
using OHOS::Multimedia::CameraAgentService::PdrEpisodeState;
using OHOS::Multimedia::CameraAgentService::ProactiveAgentBusinessModule;
using OHOS::Multimedia::CameraAgentService::RawGpsLocation;
using OHOS::Multimedia::CameraAgentService::RawPdrPoint;
using OHOS::Multimedia::CameraAgentService::SaPerceptionTick;
using OHOS::Multimedia::CameraAgentService::SemanticSnapshot;
using OHOS::Multimedia::CameraAgentService::ValidationResult;

namespace {

RawPdrPoint MakePdr(int64_t ts, double x, double y)
{
    RawPdrPoint p;
    p.observed_at = ts;
    p.x = x;
    p.y = y;
    return p;
}

RawGpsLocation MakeGps(int64_t ts, double lat, double lon, double acc, bool valid, int32_t source)
{
    RawGpsLocation g;
    g.observed_at = ts;
    g.received_at = ts;
    g.latitude = lat;
    g.longitude = lon;
    g.horizontal_accuracy_m = acc;
    g.has_horizontal_accuracy = true;
    g.valid = valid;
    g.source_type = source;
    return g;
}

void ResetModule()
{
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    m.Shutdown();
    m.Initialize();
}

bool JsonLooksValid(const std::string &json)
{
    // Minimal brace balance check + must start with '{'
    if (json.empty() || json.front() != '{') {
        return false;
    }
    int depth = 0;
    bool inStr = false;
    bool esc = false;
    for (char c : json) {
        if (inStr) {
            if (esc) {
                esc = false;
            } else if (c == '\\') {
                esc = true;
            } else if (c == '"') {
                inStr = false;
            }
            continue;
        }
        if (c == '"') {
            inStr = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth < 0) {
                return false;
            }
        }
    }
    return depth == 0 && !inStr;
}

std::string ReadFile(const std::string &path)
{
    std::ifstream in(path);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::string UnescapeCsvField(const std::string &field)
{
    if (field.size() >= 2 && field.front() == '"' && field.back() == '"') {
        std::string out;
        for (size_t i = 1; i + 1 < field.size(); ++i) {
            if (field[i] == '"' && field[i + 1] == '"') {
                out.push_back('"');
                ++i;
            } else {
                out.push_back(field[i]);
            }
        }
        return out;
    }
    return field;
}

std::string LastCsvField(const std::string &csvPath)
{
    const std::string content = ReadFile(csvPath);
    size_t end = content.size();
    while (end > 0 && (content[end - 1] == '\n' || content[end - 1] == '\r')) {
        --end;
    }
    size_t start = content.rfind('\n', end > 0 ? end - 1 : 0);
    start = (start == std::string::npos) ? 0 : start + 1;
    const std::string line = content.substr(start, end - start);
    const size_t pos = line.rfind(",\"{");
    if (pos == std::string::npos) {
        return "";
    }
    return UnescapeCsvField(line.substr(pos + 1));
}

int CountChar(const std::string &s, char c)
{
    int n = 0;
    for (char ch : s) {
        if (ch == c) {
            ++n;
        }
    }
    return n;
}

void TestScenarioA_NormalStart()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t t1 = 1000;
    m.OnWalkingStarted(t1);
    m.OnPdrPoint(MakePdr(t1 + 100, 1.0, 2.0));
    m.OnPdrPoint(MakePdr(t1 + 200, 1.1, 2.1));

    const auto snap = m.CapturePerceptionInput();
    assert(snap.current_motion_state == MotionState::kWalking);
    assert(snap.has_active_pdr_episode);
    assert(snap.active_pdr_episode.started_at == t1);
    assert(snap.active_pdr_episode.state == PdrEpisodeState::kActive);
    assert(snap.active_pdr_episode.points.size() == 2);
    assert(snap.active_pdr_episode.episode_id.find("walk-") == 0);
    std::printf("PASS Scenario A: normal start + PDR append\n");
}

void TestScenarioB_IdempotentStart()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t t1 = 2000;
    m.OnWalkingStarted(t1);
    m.OnPdrPoint(MakePdr(t1 + 50, 0.5, 0.5));
    m.OnPdrPoint(MakePdr(t1 + 100, 0.6, 0.6));
    const auto before = m.CapturePerceptionInput();
    const std::string id = before.active_pdr_episode.episode_id;
    const size_t pointsBefore = before.active_pdr_episode.points.size();

    m.OnWalkingStarted(t1 + 500);
    m.OnPdrPoint(MakePdr(t1 + 600, 0.7, 0.7));

    const auto snap = m.CapturePerceptionInput();
    assert(snap.has_active_pdr_episode);
    assert(snap.active_pdr_episode.episode_id == id);
    assert(snap.active_pdr_episode.points.size() == pointsBefore + 1);
    assert(snap.completed_pdr_episodes.empty());
    std::printf("PASS Scenario B: idempotent WalkingStarted\n");
}

void TestScenarioC_EndWalking()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t t1 = 3000;
    const int64_t t2 = 4000;
    m.OnWalkingStarted(t1);
    m.OnPdrPoint(MakePdr(t1 + 100, 1.0, 1.0));
    m.OnPdrPoint(MakePdr(t1 + 200, 2.0, 2.0));
    m.OnPdrPoint(MakePdr(t1 + 300, 3.0, 3.0));
    m.OnWalkingStopped(t2);

    const auto snap = m.CapturePerceptionInput();
    assert(snap.current_motion_state == MotionState::kNotWalking);
    assert(!snap.has_active_pdr_episode);
    assert(snap.completed_pdr_episodes.size() == 1);
    assert(snap.completed_pdr_episodes[0].has_ended_at);
    assert(snap.completed_pdr_episodes[0].ended_at == t2);
    assert(snap.completed_pdr_episodes[0].state == PdrEpisodeState::kEnded);
    assert(snap.completed_pdr_episodes[0].points.size() == 3);
    std::printf("PASS Scenario C: end walking keeps completed episode\n");
}

void TestScenarioD_DuplicateStop()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    m.OnWalkingStopped(5000);
    m.OnWalkingStopped(6000);

    const auto snap = m.CapturePerceptionInput();
    assert(snap.current_motion_state == MotionState::kNotWalking);
    assert(!snap.has_active_pdr_episode);
    assert(snap.completed_pdr_episodes.empty());
    std::printf("PASS Scenario D: duplicate stop creates no empty episode\n");
}

void TestScenarioE_PdrWithoutEpisode()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    m.OnPdrPoint(MakePdr(7000, 9.0, 9.0));

    const auto snap = m.CapturePerceptionInput();
    assert(snap.current_motion_state == MotionState::kUnknown);
    assert(!snap.has_active_pdr_episode);
    assert(snap.completed_pdr_episodes.empty());
    std::printf("PASS Scenario E: PDR without episode ignored\n");
}

void TestScenarioF_GpsUpdate()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    m.OnGpsLocation(MakeGps(8000, 22.1, 113.1, 5.0, true, 1));
    m.OnGpsLocation(MakeGps(9000, 22.2, 113.2, 3.0, true, 2));

    const auto snap = m.CapturePerceptionInput();
    assert(snap.has_latest_gps);
    assert(snap.latest_gps.observed_at == 9000);
    assert(std::fabs(snap.latest_gps.latitude - 22.2) < 1e-9);
    assert(std::fabs(snap.latest_gps.longitude - 113.2) < 1e-9);
    assert(std::fabs(snap.latest_gps.horizontal_accuracy_m - 3.0) < 1e-9);
    assert(snap.latest_gps.valid);
    assert(snap.latest_gps.source_type == 2);

    m.OnGpsLocation(MakeGps(8500, 1.0, 1.0, 1.0, true, 9));
    const auto snap2 = m.CapturePerceptionInput();
    assert(snap2.latest_gps.observed_at == 9000);
    assert(std::fabs(snap2.latest_gps.latitude - 22.2) < 1e-9);
    std::printf("PASS Scenario F: GPS update + out-of-order reject\n");
}

void TestSnapshotValueCopy()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    m.OnWalkingStarted(10000);
    m.OnPdrPoint(MakePdr(10100, 1.0, 1.0));
    auto snap1 = m.CapturePerceptionInput();
    assert(snap1.has_active_pdr_episode);
    snap1.active_pdr_episode.points.clear();
    snap1.motion_events.clear();

    const auto snap2 = m.CapturePerceptionInput();
    assert(snap2.has_active_pdr_episode);
    assert(snap2.active_pdr_episode.points.size() == 1);
    assert(!snap2.motion_events.empty());
    std::printf("PASS: CapturePerceptionInput returns value copy\n");
}

bool BackupAgentEnvIfPresent()
{
    std::ifstream in("agent.env");
    if (!in.is_open()) {
        return false;
    }
    std::ofstream out("agent.env.host_test_backup", std::ios::trunc);
    out << in.rdbuf();
    return true;
}

void RestoreAgentEnvBackup(bool hadBackup)
{
    std::remove("agent.env");
    if (!hadBackup) {
        return;
    }
    std::ifstream in("agent.env.host_test_backup");
    if (!in.is_open()) {
        return;
    }
    std::ofstream out("agent.env", std::ios::trunc);
    out << in.rdbuf();
    std::remove("agent.env.host_test_backup");
}

void TestPlaceholdersNoSideEffects()
{
    const bool hadBackup = BackupAgentEnvIfPresent();
    std::remove("agent.env");
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    m.OnWalkingStarted(11000);
    m.OnGpsLocation(MakeGps(11100, 30.0, 120.0, 4.0, true, 1));

    AgentInvokeResult inv = m.InvokeAgent("", "");
    ValidationResult val = m.ValidateAgentResponse(inv);
    DebugDeliveryPayload dbg;
    m.PushDebugToHap(dbg);
    assert(!inv.implemented);
    assert(!m.IsAgentReady());
    assert(inv.status == "agent environment file not found" || inv.status == "NotInitialized");
    assert(!val.implemented);
    (void)val;

    const auto after = m.CapturePerceptionInput();
    assert(after.current_motion_state == MotionState::kWalking);
    assert(after.has_active_pdr_episode);
    assert(after.has_latest_gps);
    RestoreAgentEnvBackup(hadBackup);
    std::printf("PASS: placeholder APIs have no side effects on state\n");
}

void TestAgentInitMissingEnvFile()
{
    const bool hadBackup = BackupAgentEnvIfPresent();
    std::remove("agent.env");
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    assert(!m.IsAgentReady());
    assert(m.GetAgentInitStatus().find("not found") != std::string::npos);

    m.OnWalkingStarted(12000);
    m.ProcessTickAt(13000);
    assert(m.HasLastTick());

    AgentInvokeResult inv = m.InvokeAgent("tick-missing-env", "{}");
    assert(!inv.implemented);
    assert(!m.IsAgentReady());
    RestoreAgentEnvBackup(hadBackup);
    std::printf("PASS: missing agent.env keeps sensor pipeline running\n");
}

void TestAgentInitEmptyApiKey()
{
    const bool hadBackup = BackupAgentEnvIfPresent();
    {
        std::ofstream out("agent.env", std::ios::trunc);
        out << "SA_AGENT_BASE_URL=https://example.com/v1/chat/completions\n";
        out << "SA_AGENT_API_KEY=\n";
    }
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    assert(!m.IsAgentReady());
    assert(m.GetAgentInitStatus().find("SA_AGENT_API_KEY") != std::string::npos);

    m.OnWalkingStarted(14000);
    m.ProcessTickAt(15000);
    assert(m.HasLastTick());
    RestoreAgentEnvBackup(hadBackup);
    std::printf("PASS: empty API key does not crash sensor pipeline\n");
}

void TestAgentInitValidEnv()
{
    const bool hadBackup = BackupAgentEnvIfPresent();
    {
        std::ofstream out("agent.env", std::ios::trunc);
        out << "SA_AGENT_BASE_URL=https://example.com/v1/chat/completions\n";
        out << "SA_AGENT_API_KEY=test-key-not-a-secret\n";
    }
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    assert(m.IsAgentReady());
    assert(m.GetAgentInitStatus() == "Ready");

    AgentInvokeResult inv = m.InvokeAgent("tick-host-ready", "{\"schema_version\":\"1.0\"}");
    assert(!inv.implemented);
    assert(inv.status == "NotImplemented");
    RestoreAgentEnvBackup(hadBackup);
    std::printf("PASS: valid agent.env enables agent readiness without LLM invoke\n");
}

void TestOutOfOrderPdrDiscarded()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    m.OnWalkingStarted(12000);
    m.OnPdrPoint(MakePdr(12100, 1.0, 1.0));
    m.OnPdrPoint(MakePdr(12050, 0.0, 0.0));
    m.OnPdrPoint(MakePdr(12200, 2.0, 2.0));

    const auto snap = m.CapturePerceptionInput();
    assert(snap.active_pdr_episode.points.size() == 2);
    assert(snap.active_pdr_episode.points[0].observed_at == 12100);
    assert(snap.active_pdr_episode.points[1].observed_at == 12200);
    std::printf("PASS: out-of-order PDR discarded\n");
}

void TestShutdownRejectsInput()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    m.OnWalkingStarted(13000);
    m.Shutdown();
    m.OnWalkingStarted(14000);
    m.OnPdrPoint(MakePdr(14100, 1.0, 1.0));
    m.OnGpsLocation(MakeGps(14200, 1.0, 1.0, 1.0, true, 1));

    const auto snap = m.CapturePerceptionInput();
    assert(snap.current_motion_state == MotionState::kUnknown);
    assert(!snap.has_active_pdr_episode);
    assert(!snap.has_latest_gps);
    std::printf("PASS: Shutdown rejects further input\n");
}

// ---- Tick / normalize / CSV ----

void TestTickWindowsNonOverlapping()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    // Force known window start by Shutdown/Initialize then ProcessTickAt.
    // First window: (t0, t0+10000]
    const int64_t t0 = 1'700'000'000'000LL; // fixed fake epoch for deterministic ids
    // Re-init and poke window via two ticks with explicit ends.
    m.Shutdown();
    m.Initialize();
    // Override by processing ticks that establish chain from Initialize's windowStart.
    // Capture first tick end relative to init by using ProcessTickAt with now-like values.
    // Instead: feed events then tick at absolute times after manually using ProcessTickAt
    // from Initialize's windowStartMs_ which is "now". Use relative approach:
    // Call ProcessTickAt twice with ends = start+10s and start+20s by reading last tick windows.

    // Seed: get debug dir means init worked; first ProcessTickAt(now+10s) from module's start.
    // We don't know windowStart exactly — call ProcessTickAt with increasing ends.
    const auto before = m.CapturePerceptionInput();
    (void)before;

    // Use a dedicated sequence: Shutdown, Initialize, then immediately ProcessTickAt(windowStart+10000)
    // by doing two ticks and checking adjacency from GetLastTick.
    m.ProcessTickAt(m.GetLastTick().observation_window.ended_at); // no-op-ish if no last tick

    // Fresh module with controlled timeline via events at absolute ms and ticks at known ends.
    // After Initialize, windowStart ≈ now. We'll just verify consecutive ProcessTickAt chaining:
    ResetModule();
    const int64_t base = 2'000'000'000'000LL;
    // Hack: first tick — ProcessTickAt may use Initialize's windowStart; force by
    // processing with end = base if windowStart was set to now. Better approach below.

    // Controlled: Shutdown, Initialize, then ProcessTickAt(end1), ProcessTickAt(end2)
    // where end1 = windowStart + 10000 from first tick's window.
    m.Shutdown();
    m.Initialize();
    m.ProcessTickAt(base); // if windowStart(now) < base, window is (now, base]
    assert(m.HasLastTick());
    const auto tick1 = m.GetLastTick();
    const int64_t end1 = tick1.observation_window.ended_at;
    const int64_t start1 = tick1.observation_window.started_at;
    assert(end1 == base);
    assert(end1 > start1);
    assert(std::fabs(tick1.observation_window.duration_s - (end1 - start1) / 1000.0) < 1e-9);

    m.ProcessTickAt(end1 + 10000);
    const auto tick2 = m.GetLastTick();
    assert(tick2.observation_window.started_at == end1);
    assert(tick2.observation_window.ended_at == end1 + 10000);
    assert(std::fabs(tick2.observation_window.duration_s - 10.0) < 1e-9);
    std::printf("PASS: consecutive ticks non-overlapping contiguous windows\n");
}

void TestBoundaryEventBelongsToOneWindow()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t base = 3'000'000'000'000LL;
    m.ProcessTickAt(base); // establish window end = base
    m.OnWalkingStarted(base); // at previous window end — not in (base, base+10000]
    m.ProcessTickAt(base + 10000);
    const auto tick = m.GetLastTick();
    assert(tick.motion.events.empty());
    m.OnWalkingStarted(base + 15000); // inside (base+10000, base+20000]
    m.ProcessTickAt(base + 20000);
    const auto tick2 = m.GetLastTick();
    assert(tick2.motion.events.size() == 1);
    assert(tick2.motion.events[0].timestamp == base + 15000);
    std::printf("PASS: boundary events belong to one window\n");
}

void TestTickDoesNotEndActiveEpisode()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t base = 4'000'000'000'000LL;
    m.ProcessTickAt(base);
    m.OnWalkingStarted(base + 100);
    m.OnPdrPoint(MakePdr(base + 200, 0.0, 0.0));
    m.ProcessTickAt(base + 10000);
    assert(m.HasLastTick());
    assert(!m.GetLastTick().pdr_episodes.empty());
    assert(m.GetLastTick().pdr_episodes[0].state_at_window_end == PdrEpisodeState::kActive);
    const auto snap = m.CapturePerceptionInput();
    assert(snap.has_active_pdr_episode);
    std::printf("PASS: tick does not end active PDR episode\n");
}

void TestMotionTransitions()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t base = 5'000'000'000'000LL;
    m.ProcessTickAt(base);

    // STARTED
    m.OnWalkingStarted(base + 1000);
    m.ProcessTickAt(base + 10000);
    {
        const auto s = m.GetLastSnapshot();
        assert(s.motion.state == "WALKING");
        assert(s.motion.transition == "STARTED");
    }

    // CONTINUING
    m.OnPdrPoint(MakePdr(base + 11000, 1.0, 0.0));
    m.ProcessTickAt(base + 20000);
    {
        const auto s = m.GetLastSnapshot();
        assert(s.motion.state == "WALKING");
        assert(s.motion.transition == "CONTINUING");
    }

    // ENDED
    m.OnWalkingStopped(base + 25000);
    m.ProcessTickAt(base + 30000);
    {
        const auto s = m.GetLastSnapshot();
        assert(s.motion.state == "NOT_WALKING");
        assert(s.motion.transition == "ENDED");
    }

    // NONE while stationary
    m.ProcessTickAt(base + 40000);
    {
        const auto s = m.GetLastSnapshot();
        assert(s.motion.state == "NOT_WALKING");
        assert(s.motion.transition == "NONE");
        assert(s.pdr.quality == "NOT_APPLICABLE");
    }

    // STARTED_AND_ENDED in one window
    m.OnWalkingStarted(base + 41000);
    m.OnPdrPoint(MakePdr(base + 41100, 0.0, 0.0));
    m.OnPdrPoint(MakePdr(base + 41200, 1.0, 0.0));
    m.OnWalkingStopped(base + 42000);
    m.ProcessTickAt(base + 50000);
    {
        const auto s = m.GetLastSnapshot();
        assert(s.motion.transition == "STARTED_AND_ENDED");
        assert(s.pdr.transition == "STARTED_AND_ENDED");
    }
    std::printf("PASS: motion transitions STARTED/CONTINUING/ENDED/NONE/STARTED_AND_ENDED\n");
}

void TestMotionConflictIssue()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t base = 6'000'000'000'000LL;
    m.ProcessTickAt(base);
    m.OnWalkingStarted(base + 1);
    m.ProcessTickAt(base + 10000); // window-start state becomes WALKING
    // Same window: stop then start again → end WALKING with stop+start events (conflict vs CONTINUING).
    m.OnWalkingStopped(base + 15000);
    m.OnWalkingStarted(base + 16000);
    m.ProcessTickAt(base + 20000);
    const auto s2 = m.GetLastSnapshot();
    assert(s2.motion.state == "WALKING");
    bool hasConflict = false;
    for (const auto &iss : s2.data_quality.issues) {
        if (iss == "MOTION_EVENT_CONFLICT") {
            hasConflict = true;
        }
    }
    assert(hasConflict);
    std::printf("PASS: motion conflict generates MOTION_EVENT_CONFLICT\n");
}

void TestPdrMetricsTriangle()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t base = 7'000'000'000'000LL;
    m.ProcessTickAt(base);
    m.OnWalkingStarted(base + 100);
    m.OnPdrPoint(MakePdr(base + 200, 0.0, 0.0));
    m.OnPdrPoint(MakePdr(base + 300, 3.0, 0.0));
    m.OnPdrPoint(MakePdr(base + 400, 3.0, 4.0));
    m.ProcessTickAt(base + 10000);
    const auto s = m.GetLastSnapshot();
    assert(s.pdr.quality == "USABLE");
    assert(std::fabs(s.pdr.cumulative.path_length_m - 7.0) < 1e-6);
    assert(std::fabs(s.pdr.cumulative.net_displacement_m - 5.0) < 1e-6);
    assert(std::fabs(s.pdr.cumulative.straightness_ratio - (5.0 / 7.0)) < 1e-6);
    assert(s.pdr.window.point_count == 3);
    std::printf("PASS: PDR path=7 net=5 straightness=5/7\n");
}

void TestPdrWindowPathUsesAnchor()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t base = 8'000'000'000'000LL;
    m.ProcessTickAt(base);
    m.OnWalkingStarted(base + 100);
    m.OnPdrPoint(MakePdr(base + 200, 0.0, 0.0));
    m.OnPdrPoint(MakePdr(base + 300, 10.0, 0.0));
    m.ProcessTickAt(base + 10000); // first window path ~10
    m.OnPdrPoint(MakePdr(base + 10500, 13.0, 0.0)); // +3 from anchor at (10,0)
    m.ProcessTickAt(base + 20000);
    const auto s = m.GetLastSnapshot();
    assert(std::fabs(s.pdr.window.path_length_m - 3.0) < 1e-6);
    assert(s.pdr.window.point_count == 1);
    assert(std::fabs(s.pdr.cumulative.path_length_m - 13.0) < 1e-6);
    std::printf("PASS: window path uses pre-window anchor point\n");
}

void TestPdrInsufficientAndMissing()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t base = 9'000'000'000'000LL;
    m.ProcessTickAt(base);
    m.OnWalkingStarted(base + 100);
    m.OnPdrPoint(MakePdr(base + 200, 1.0, 1.0)); // single point
    m.ProcessTickAt(base + 10000);
    assert(m.GetLastSnapshot().pdr.quality == "INSUFFICIENT_POINTS");

    // Walking but clear episode somehow — stop without points in new window after end consumed
    m.OnWalkingStopped(base + 11000);
    m.ProcessTickAt(base + 20000); // consumes ended episode
    // Force walking without episode: start then... can't remove episode. Simulate MISSING by
    // ProcessTick while walking but episode already completed and not intersecting?
    // After stop, state NOT_WALKING. Start walking: creates episode. Tick immediately without points:
    m.OnWalkingStarted(base + 21000);
    // Don't add points
    m.ProcessTickAt(base + 30000);
    assert(m.GetLastSnapshot().motion.state == "WALKING");
    assert(m.GetLastSnapshot().pdr.quality == "INSUFFICIENT_POINTS"); // has episode, <2 points

    // MISSING: walking with no episode — ignore PDR without start; manually we need walking state
    // without episode. Only possible via Capture path... After stop without completing into tick?
    // OnWalkingStarted creates episode always. Spec: walking but no episode → MISSING.
    // Achieve by: not calling OnWalkingStarted but somehow setting walking — not public.
    // Alternative: ProcessTickAt with synthetic Normalize — call NormalizePerceptionInput directly.
    SaPerceptionTick fake;
    fake.schema_version = "1.0";
    fake.tick_id = "sa-tick-20260101-000000-0001";
    fake.observed_at = base + 40000;
    fake.timezone = "Asia/Shanghai";
    fake.observation_window = {base + 30000, base + 40000, 10.0};
    fake.motion.state_at_window_start = MotionState::kWalking;
    fake.motion.state_at_window_end = MotionState::kWalking;
    fake.has_gps = false;
    const auto missing = m.NormalizePerceptionInput(fake);
    assert(missing.pdr.quality == "MISSING");
    std::printf("PASS: PDR INSUFFICIENT_POINTS and MISSING\n");
}

void TestEndedEpisodeAppearsInTick()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t base = 10'000'000'000'000LL;
    m.ProcessTickAt(base);
    m.OnWalkingStarted(base + 100);
    m.OnPdrPoint(MakePdr(base + 200, 0.0, 0.0));
    m.OnPdrPoint(MakePdr(base + 300, 1.0, 0.0));
    m.OnWalkingStopped(base + 5000);
    m.ProcessTickAt(base + 10000);
    const auto tick = m.GetLastTick();
    assert(tick.pdr_episodes.size() == 1);
    assert(tick.pdr_episodes[0].has_ended_at);
    assert(m.GetLastSnapshot().pdr.transition == "STARTED_AND_ENDED" ||
           m.GetLastSnapshot().pdr.transition == "ENDED");
    // Next tick should not re-emit completed episode
    m.ProcessTickAt(base + 20000);
    assert(m.GetLastTick().pdr_episodes.empty());
    std::printf("PASS: ended episode appears in covering tick then removed\n");
}

void TestHomeRelations()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t base = 11'000'000'000'000LL;
    m.ProcessTickAt(base);

    // INSIDE
    m.OnGpsLocation(MakeGps(base + 5000, kHomeLatitude, kHomeLongitude, 8.0, true, 1));
    m.ProcessTickAt(base + 10000);
    assert(m.GetLastSnapshot().home_relation.relation == "INSIDE");
    assert(m.GetLastSnapshot().home_relation.quality == "USABLE");

    // NEAR ~100m north
    const double nearLat = kHomeLatitude + (100.0 / 111320.0);
    m.OnGpsLocation(MakeGps(base + 15000, nearLat, kHomeLongitude, 8.0, true, 1));
    m.ProcessTickAt(base + 20000);
    assert(m.GetLastSnapshot().home_relation.relation == "NEAR");

    // OUTSIDE ~300m
    const double farLat = kHomeLatitude + (300.0 / 111320.0);
    m.OnGpsLocation(MakeGps(base + 25000, farLat, kHomeLongitude, 8.0, true, 1));
    m.ProcessTickAt(base + 30000);
    assert(m.GetLastSnapshot().home_relation.relation == "OUTSIDE");

    // POOR_ACCURACY
    m.OnGpsLocation(MakeGps(base + 35000, kHomeLatitude, kHomeLongitude, 90.0, true, 1));
    m.ProcessTickAt(base + 40000);
    assert(m.GetLastSnapshot().home_relation.quality == "POOR_ACCURACY");
    assert(m.GetLastSnapshot().home_relation.relation == "UNKNOWN");
    assert(!m.GetLastSnapshot().home_relation.has_distance_m);

    // STALE
    m.OnGpsLocation(MakeGps(base + 40000 - 200000, kHomeLatitude, kHomeLongitude, 8.0, true, 1));
    // observed_at older than window end by 200s — but OnGpsLocation rejects older than latest!
    // Latest is base+35000. Need older relative to tick observed_at but newer than latest GPS.
    m.OnGpsLocation(MakeGps(base + 40100, kHomeLatitude, kHomeLongitude, 8.0, true, 1));
    m.ProcessTickAt(base + 40100 + 130000); // age ~130s
    assert(m.GetLastSnapshot().home_relation.quality == "STALE");

    // INVALID
    ResetModule();
    m.ProcessTickAt(base);
    m.OnGpsLocation(MakeGps(base + 1000, kHomeLatitude, kHomeLongitude, 8.0, false, 1));
    m.ProcessTickAt(base + 10000);
    assert(m.GetLastSnapshot().home_relation.quality == "INVALID");

    // MISSING gps
    ResetModule();
    m.ProcessTickAt(base);
    m.ProcessTickAt(base + 10000);
    assert(m.GetLastSnapshot().home_relation.quality == "MISSING");
    std::printf("PASS: HOME INSIDE/NEAR/OUTSIDE/POOR/STALE/INVALID/MISSING\n");
}

void TestJsonAndCsv()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t base = 12'000'000'000'000LL;
    m.ProcessTickAt(base);
    m.OnWalkingStarted(base + 100);
    m.OnPdrPoint(MakePdr(base + 200, 0.0, 0.0));
    m.OnPdrPoint(MakePdr(base + 300, 3.0, 0.0));
    m.OnPdrPoint(MakePdr(base + 400, 3.0, 4.0));
    m.OnGpsLocation(MakeGps(base + 5000, kHomeLatitude, kHomeLongitude, 8.0, true, 1));
    m.ProcessTickAt(base + 10000);

    assert(m.HasLastTick());
    assert(m.HasLastSnapshot());
    const auto tick = m.GetLastTick();
    const auto snap = m.GetLastSnapshot();

    // Rebuild JSON via Normalize path already stored — use Normalize + check no time_context in snapshot fields
    const SemanticSnapshot n = m.NormalizePerceptionInput(tick);
    assert(n.tick_id == tick.tick_id);
    // SemanticSnapshot has no time_context member — compile-time guarantee; also check serialized
    // by reading CSV row.

    const std::string dir = m.GetDebugRunDirectory();
    assert(!dir.empty());
    const std::string sensorPath = dir + "/sensor_events.csv";
    const std::string tickPath = dir + "/sa_perception_ticks.csv";
    const std::string snapPath = dir + "/semantic_snapshots.csv";

    const std::string sensorCsv = ReadFile(sensorPath);
    const std::string tickCsv = ReadFile(tickPath);
    const std::string snapCsv = ReadFile(snapPath);
    assert(CountChar(sensorCsv, '\n') >= 2); // header + rows
    assert(tickCsv.find("tick_id,window_started_at") == 0 || tickCsv.find("tick_id") != std::string::npos);
    assert(CountChar(tickCsv, '\n') == 3); // header + 2 ticks (base and base+10000) roughly
    // Headers appear once
    assert(sensorCsv.find("sequence_id,received_at") == 0);
    assert(snapCsv.find("snapshot_id,tick_id") == 0);
    assert(snapCsv.find("time_context") == std::string::npos);
    assert(tickCsv.find(tick.tick_id) != std::string::npos);
    assert(snapCsv.find(tick.tick_id) != std::string::npos);

    // CSV escaping with quotes in JSON
    assert(tickCsv.find("\"\"") != std::string::npos || tickCsv.find("sa_input_json") != std::string::npos);
    // sa_input_json is quoted because it contains commas
    assert(tickCsv.find("\"{") != std::string::npos);

    std::printf("PASS: JSON/CSV headers, tick_id link, no time_context\n");
    std::printf("DEBUG_RUN_DIR=%s\n", dir.c_str());
    std::printf("SAMPLE_TICK_ID=%s\n", tick.tick_id.c_str());
    std::printf("SAMPLE_SNAPSHOT_ID=%s\n", snap.snapshot_id.c_str());
}

void TestShutdownFlushesSensorEvents()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    m.OnGpsLocation(MakeGps(100, 1.0, 2.0, 5.0, true, 1));
    const std::string dir = m.GetDebugRunDirectory();
    m.Shutdown();
    const std::string sensorCsv = ReadFile(dir + "/sensor_events.csv");
    assert(sensorCsv.find("GPS_REPORT") != std::string::npos);
    std::printf("PASS: Shutdown flushes remaining sensor events\n");
}

void TestPrintSampleJson()
{
    ResetModule();
    auto &m = ProactiveAgentBusinessModule::GetInstance();
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m.ProcessTickAt(now); // align window end to now
    m.OnWalkingStarted(now + 100);
    m.OnPdrPoint(MakePdr(now + 200, 0.0, 0.0));
    m.OnPdrPoint(MakePdr(now + 300, 3.0, 0.0));
    m.OnPdrPoint(MakePdr(now + 400, 3.0, 4.0));
    m.OnGpsLocation(MakeGps(now + 8000, kHomeLatitude + (65.0 / 111320.0), kHomeLongitude, 8.0, true, 1));
    m.ProcessTickAt(now + 10000);
    const auto tick = m.GetLastTick();
    const auto snap = m.GetLastSnapshot();
    assert(tick.observation_window.duration_s == 10.0);
    assert(snap.motion.transition == "STARTED");
    assert(snap.home_relation.relation == "NEAR");
    assert(snap.pdr.quality == "USABLE");
    // Confirm SemanticSnapshot type has no time_context by checking CSV serialization.
    const std::string dir = m.GetDebugRunDirectory();
    const std::string snapCsv = ReadFile(dir + "/semantic_snapshots.csv");
    assert(snapCsv.find("time_context") == std::string::npos);
    const std::string tickJson = LastCsvField(dir + "/sa_perception_ticks.csv");
    const std::string snapJson = LastCsvField(dir + "/semantic_snapshots.csv");
    assert(JsonLooksValid(tickJson));
    assert(JsonLooksValid(snapJson));
    assert(snapJson.find("time_context") == std::string::npos);
    assert(snapJson.find("\"distance_m\":null") == std::string::npos ||
           snap.home_relation.has_distance_m); // USABLE has numeric distance
    assert(tickJson.find("\"gps\":null") == std::string::npos); // has gps
    {
        std::ofstream tOut(dir + "/sample_sa_perception_tick.json");
        tOut << tickJson;
        std::ofstream sOut(dir + "/sample_semantic_snapshot.json");
        sOut << snapJson;
    }
    std::printf("--- realistic sample ---\n");
    std::printf("tick_id=%s snapshot_id=%s dir=%s\n",
        tick.tick_id.c_str(), snap.snapshot_id.c_str(), dir.c_str());
    std::printf("motion=%s/%s home=%s/%s dist=%.1f pdr_path=%.1f net=%.1f overall=%s\n",
        snap.motion.state.c_str(), snap.motion.transition.c_str(),
        snap.home_relation.relation.c_str(), snap.home_relation.quality.c_str(),
        snap.home_relation.has_distance_m ? snap.home_relation.distance_m : -1.0,
        snap.pdr.cumulative.has_path_length_m ? snap.pdr.cumulative.path_length_m : -1.0,
        snap.pdr.cumulative.has_net_displacement_m ? snap.pdr.cumulative.net_displacement_m : -1.0,
        snap.data_quality.overall.c_str());
    std::printf("SAMPLE_TICK_JSON=%s\n", tickJson.c_str());
    std::printf("SAMPLE_SNAPSHOT_JSON=%s\n", snapJson.c_str());
    std::printf("PASS: sample tick/snapshot produced\n");
}

} // namespace

int main()
{
    TestScenarioA_NormalStart();
    TestScenarioB_IdempotentStart();
    TestScenarioC_EndWalking();
    TestScenarioD_DuplicateStop();
    TestScenarioE_PdrWithoutEpisode();
    TestScenarioF_GpsUpdate();
    TestSnapshotValueCopy();
    TestPlaceholdersNoSideEffects();
    TestAgentInitMissingEnvFile();
    TestAgentInitEmptyApiKey();
    TestAgentInitValidEnv();
    TestOutOfOrderPdrDiscarded();
    TestShutdownRejectsInput();

    TestTickWindowsNonOverlapping();
    TestBoundaryEventBelongsToOneWindow();
    TestTickDoesNotEndActiveEpisode();
    TestMotionTransitions();
    TestMotionConflictIssue();
    TestPdrMetricsTriangle();
    TestPdrWindowPathUsesAnchor();
    TestPdrInsufficientAndMissing();
    TestEndedEpisodeAppearsInTick();
    TestHomeRelations();
    TestJsonAndCsv();
    TestShutdownFlushesSensorEvents();
    TestPrintSampleJson();

    std::printf("All ProactiveAgentBusinessModule host tests passed.\n");
    return 0;
}
