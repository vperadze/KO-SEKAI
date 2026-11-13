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
rem The particle effects come from four free Fab packs that are not in the repository (docs\VFX.md). A package made
rem without them has no effects. To package anyway:  set KS_NO_PACKS=1  before running.
set "KS_MISSING="
if not exist "%~dp0Content\MixedVFX\" set "KS_MISSING=1" & echo MISSING: Mixed VFX, by G.G.  https://www.fab.com/listings/6518971c-c8ca-47d7-8611-1ff068b78dd5
if not exist "%~dp0Content\FreeStylizedLaserBeamVFX\" set "KS_MISSING=1" & echo MISSING: Stylized Laser Beam VFX - Free, by QP Phantom  https://www.fab.com/listings/e63c140a-4ef9-448a-8e58-133ae1487534
if not exist "%~dp0Content\PotaVFX_Smoke\" set "KS_MISSING=1" & echo MISSING: Stylized Smoke VFX, by PotaVFX  https://www.fab.com/listings/172235df-7a05-4d60-ac7b-23472e530ee7
if not exist "%~dp0Content\Vefects\VFX_Distortion\" set "KS_MISSING=1" & echo MISSING: Distortion VFX, by Vefects  https://www.fab.com/listings/4f001dd6-88d3-4189-9dde-150f6c7f71c9
if defined KS_MISSING if not defined KS_NO_PACKS (
  echo Add the packs above in the Unreal editor: Window, Fab, Add to Project. Then run this again.
  exit /b 1
)
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
  -project="%~dp0KoSekai.uproject" -platform=Win64 -clientconfig=Development ^
  -build -cook -stage -pak -package -archive -archivedirectory="%~dp0Packaged" ^
  -unattended -utf8output -nop4
exit /b %ERRORLEVEL%
