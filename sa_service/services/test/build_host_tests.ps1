# Host test build helper (Windows MSVC). Run from repo root.
$ErrorActionPreference = "Stop"
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
}

$common = @(
    "/EHsc", "/std:c++17",
    "/I", "services\include",
    "/I", "services\include\proactive",
    "/I", "services\include\power",
    "/I", "services\include\utils",
    "/I", "services\test",
    "/DPROACTIVE_AGENT_HOST_TEST", "/link", "/SUBSYSTEM:CONSOLE"
)

cmd /c "`"$vcvars`" && cl $($common -join ' ') /Fe:agent_runtime_config_test.exe services\src\proactive\AgentRuntimeConfig.cpp services\test\agent_runtime_config_test.cpp"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
.\agent_runtime_config_test.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmd /c "`"$vcvars`" && cl $($common -join ' ') /Fe:proactive_agent_host_test.exe services\src\proactive\ProactiveAgentBusinessModule.cpp services\src\proactive\AgentRuntimeConfig.cpp services\src\power\PowerModeController.cpp services\test\ProactiveAgentBusinessModuleHostTest.cpp"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
.\proactive_agent_host_test.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($env:JIUWEN_CORE_INCLUDE) {
    cmd /c "`"$vcvars`" && cl /EHsc /std:c++17 /I services\include /I services\test /I `"$env:JIUWEN_CORE_INCLUDE`" /Fe:leaving_home_baseline_agent_config_test.exe services\src\sa_agent\LeavingHomeBaselineAgentConfig.cpp services\test\stubs\AnyValueHostStub.cpp services\test\leaving_home_baseline_agent_config_test.cpp /link /SUBSYSTEM:CONSOLE"
    if ($LASTEXITCODE -eq 0) {
        .\leaving_home_baseline_agent_config_test.exe
    }
} else {
    Write-Host "SKIP: leaving_home_baseline_agent_config_test (set JIUWEN_CORE_INCLUDE to jiuwen_core headers)"
}

Write-Host "Host tests completed."
