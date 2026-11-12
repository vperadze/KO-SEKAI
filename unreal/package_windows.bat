@echo off
rem Packages a Windows build of KO-SEKAI into .\Packaged\Windows
rem Run this on a Windows PC with Unreal Engine 5.8 and Visual Studio 2022 (C++ game workload) installed.
rem If the engine is somewhere else:  set UE_ROOT=D:\Epic\UE_5.8  before running.
setlocal
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
if not exist "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" (
  echo Unreal Engine 5.8 not found at "%UE_ROOT%". Set UE_ROOT to its folder and run again.
  exit /b 1
)
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
  -project="%~dp0KoSekai.uproject" -platform=Win64 -clientconfig=Development ^
  -build -cook -stage -pak -package -archive -archivedirectory="%~dp0Packaged" ^
  -unattended -utf8output -nop4
exit /b %ERRORLEVEL%
