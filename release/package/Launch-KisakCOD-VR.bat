@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"

if not exist "%~dp0iw3sp.exe" (
  echo ERROR: iw3sp.exe was not found beside this launcher.
  echo Extract the KisakCOD VR package into the original COD4 game folder.
  pause
  exit /b 1
)

if not exist "%~dp0KisakCOD-sp.exe" (
  echo ERROR: KisakCOD-sp.exe is missing from the package.
  pause
  exit /b 1
)

if not exist "%~dp0VR-Settings.bat" (
  echo ERROR: VR-Settings.bat is missing from the package.
  pause
  exit /b 1
)

call "%~dp0VR-Settings.bat"
if errorlevel 1 (
  echo ERROR: VR-Settings.bat could not be loaded.
  pause
  exit /b 1
)

set "VR_DEVELOPER=0"
set "VR_ERROR_TIME=0"
if "%KISAK_VR_DIAGNOSTICS%"=="1" (
  set "VR_DEVELOPER=1"
  set "VR_ERROR_TIME=8"
)

"%~dp0KisakCOD-sp.exe" ^
  +set developer %VR_DEVELOPER% ^
  +set logfile 2 ^
  +set r_fullscreen 0 ^
  +set r_customMode %VR_CUSTOM_MODE% ^
  +set r_aaSamples 1 ^
  +set r_scaleViewport 1 ^
  +set r_resampleScene 0 ^
  +set r_vsync 0 ^
  +set com_maxfps 0 ^
  +set r_smp_backend 1 ^
  +set r_smp_worker 1 ^
  +set cg_gun_z 36 ^
  +set sm_enable 1 ^
  +set sm_sunEnable 1 ^
  +set sm_spotEnable 1 ^
  +set sm_maxLights 4 ^
  +set com_statmon 0 ^
  +set cg_drawFPS 0 ^
  +set cg_drawPerformanceWarnings 0 ^
  +set con_minicon 0 ^
  +set con_errormessagetime %VR_ERROR_TIME% ^
  +spmap cargoship

set "VR_EXIT_CODE=%ERRORLEVEL%"
if not "%VR_EXIT_CODE%"=="0" (
  echo.
  echo KisakCOD VR exited with code %VR_EXIT_CODE%.
  echo See main\console.log for details.
  pause
)

endlocal & exit /b %VR_EXIT_CODE%
