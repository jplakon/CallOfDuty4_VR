@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"

echo KISAKCOD VR V48 CRASH-RECORDER SELF-TEST
echo.
echo This intentionally terminates KisakCOD-sp.exe before engine startup.
echo It does not launch a mission or modify saves, but it will create a test
echo report and minidump in CrashDumps.
echo.
set /p "V48_CONFIRM=Type TEST and press Enter to continue: "
if /I not "%V48_CONFIRM%"=="TEST" (
  echo Cancelled. Nothing was run.
  exit /b 2
)

if not exist "%~dp0KisakCOD-sp.exe" (
  echo ERROR: KisakCOD-sp.exe is missing beside this test.
  pause
  exit /b 1
)

set "KISAK_VR_CRASH_DIR=%~dp0CrashDumps"
if not exist "%KISAK_VR_CRASH_DIR%\" mkdir "%KISAK_VR_CRASH_DIR%" >nul 2>&1
set "KISAK_VR_CRASH_TEST=V48-EXPLICIT-TEST"

"%~dp0KisakCOD-sp.exe"
set "V48_TEST_EXIT=%ERRORLEVEL%"
set "KISAK_VR_CRASH_TEST="

echo.
if not exist "%KISAK_VR_CRASH_DIR%\LATEST.txt" (
  echo SELF-TEST FAILED: CrashDumps\LATEST.txt was not created.
  echo Process exit code: %V48_TEST_EXIT%
  pause
  exit /b 1
)

findstr /i /c:"status=CRASHED" "%KISAK_VR_CRASH_DIR%\KisakCOD-VR-Last-Session.txt" >nul 2>&1
if errorlevel 1 (
  echo SELF-TEST FAILED: the last-session record does not say CRASHED.
  echo Process exit code: %V48_TEST_EXIT%
  pause
  exit /b 1
)

echo SELF-TEST PASSED
echo Process exit code: %V48_TEST_EXIT%
type "%KISAK_VR_CRASH_DIR%\LATEST.txt"
echo.
echo The next real crash will replace LATEST.txt while preserving this test dump.
pause
exit /b 0

