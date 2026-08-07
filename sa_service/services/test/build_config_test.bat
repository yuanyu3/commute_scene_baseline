@echo off
setlocal
cd /d c:\Users\l00972660\commute_scene_baseline/sa_service
if not defined JIUWEN_CORE_INCLUDE (
  echo ERROR: set JIUWEN_CORE_INCLUDE to jiuwen-lite include directory
  exit /b 1
)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cl /EHsc /std:c++17 /utf-8 /I services\include /I services\test /I "%JIUWEN_CORE_INCLUDE%" /Fe:leaving_home_baseline_agent_config_test.exe services\src\sa_agent\LeavingHomeBaselineAgentConfig.cpp services\src\sa_agent\LeavingHomeContextEngine.cpp services\test\stubs\AnyValueHostStub.cpp services\test\leaving_home_baseline_agent_config_test.cpp /link /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1
leaving_home_baseline_agent_config_test.exe
exit /b %ERRORLEVEL%
