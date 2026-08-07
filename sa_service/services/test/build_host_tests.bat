@echo off
setlocal
cd /d c:\Users\l00972660\commute_scene_baseline/sa_service
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

set INC=/I services\include /I services\include\proactive /I services\include\power /I services\include\utils /I services\test

cl /EHsc /std:c++17 /utf-8 %INC% /DPROACTIVE_AGENT_HOST_TEST /Fe:agent_runtime_config_test.exe services\src\proactive\AgentRuntimeConfig.cpp services\test\agent_runtime_config_test.cpp /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
agent_runtime_config_test.exe
if errorlevel 1 exit /b 1

cl /EHsc /std:c++17 /utf-8 %INC% /DPROACTIVE_AGENT_HOST_TEST /Fe:proactive_agent_host_test.exe services\src\proactive\ProactiveAgentBusinessModule.cpp services\src\proactive\AgentRuntimeConfig.cpp services\src\power\PowerModeController.cpp services\test\ProactiveAgentBusinessModuleHostTest.cpp /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
proactive_agent_host_test.exe
if errorlevel 1 exit /b 1

if not defined JIUWEN_CORE_INCLUDE (
  echo SKIP: leaving_home_baseline_agent_config_test - set JIUWEN_CORE_INCLUDE to jiuwen-lite include dir
) else (
  cl /EHsc /std:c++17 /utf-8 /I services\include /I services\test /I "%JIUWEN_CORE_INCLUDE%" /Fe:leaving_home_baseline_agent_config_test.exe services\src\sa_agent\LeavingHomeBaselineAgentConfig.cpp services\src\sa_agent\LeavingHomeContextEngine.cpp services\test\stubs\AnyValueHostStub.cpp services\test\leaving_home_baseline_agent_config_test.cpp /link /SUBSYSTEM:CONSOLE
  if errorlevel 1 exit /b 1
  leaving_home_baseline_agent_config_test.exe
  if errorlevel 1 exit /b 1
)

echo All host tests completed.
exit /b 0
