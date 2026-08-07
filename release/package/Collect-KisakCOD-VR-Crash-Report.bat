@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"

if not exist "%~dp0Collect-KisakCOD-VR-Crash-Report.ps1" (
  echo ERROR: Collect-KisakCOD-VR-Crash-Report.ps1 is missing.
  echo Re-extract the complete V48 diagnostic update.
  pause
  exit /b 1
)

echo Collecting the latest KisakCOD VR crash artifacts...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0Collect-KisakCOD-VR-Crash-Report.ps1"

set "COLLECT_STATUS=%ERRORLEVEL%"
if not "%COLLECT_STATUS%"=="0" (
  echo.
  echo Crash-report collection failed with code %COLLECT_STATUS%.
  pause
  exit /b %COLLECT_STATUS%
)

echo.
echo Collection complete. Attach the ZIP shown above; do not attach only a screenshot.
pause
exit /b 0

