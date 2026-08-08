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

set "VR_D3DX9_43_FOUND=0"
if exist "%~dp0d3dx9_43.dll" set "VR_D3DX9_43_FOUND=1"
if exist "%SystemRoot%\SysWOW64\d3dx9_43.dll" set "VR_D3DX9_43_FOUND=1"
if not exist "%SystemRoot%\SysWOW64\" if exist "%SystemRoot%\System32\d3dx9_43.dll" set "VR_D3DX9_43_FOUND=1"

if "%VR_D3DX9_43_FOUND%"=="0" (
  echo ERROR: Microsoft DirectX runtime file d3dx9_43.dll is missing.
  echo Install DirectX End-User Runtimes ^(June 2010^) from Microsoft:
  echo https://www.microsoft.com/en-us/download/details.aspx?id=8109
  echo.
  echo Do not rename d3dx9_34.dll and do not download loose DLL files.
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

rem KISAK_SP_VR_CRASH_DIAGNOSTICS_V48
rem Keep crash artifacts outside main\ so they survive console-log rotation and
rem can be collected even when the engine fails before normal logging starts.
set "KISAK_VR_CRASH_DIR=%~dp0CrashDumps"
if not exist "%KISAK_VR_CRASH_DIR%\" mkdir "%KISAK_VR_CRASH_DIR%" >nul 2>&1
if not exist "%KISAK_VR_CRASH_DIR%\" (
  echo ERROR: Could not create the crash-diagnostics folder:
  echo   "%KISAK_VR_CRASH_DIR%"
  pause
  exit /b 1
)

rem KISAK_SP_VR_PACKED_MODE_PREFLIGHT_V32
if /I "%VR_CUSTOM_MODE%"=="3072x1536" (
  echo ERROR: VR_CUSTOM_MODE=3072x1536 is incompatible with the packed renderer.
  echo It cannot hold two rectangular eye images plus the dedicated scope panel.
  echo Use either:
  echo   VR_CUSTOM_MODE=6016x2688 with KISAK_VR_OUTPUT_SCALE=1.0
  echo or:
  echo   VR_CUSTOM_MODE=4768x2016 with KISAK_VR_OUTPUT_SCALE=0.75
  pause
  exit /b 1
)

if /I "%VR_CUSTOM_MODE%"=="4768x2016" if "%KISAK_VR_OUTPUT_SCALE%"=="1.0" (
  echo ERROR: 4768x2016 is too small for KISAK_VR_OUTPUT_SCALE=1.0.
  echo Set KISAK_VR_OUTPUT_SCALE=0.75 for the lower packed preset.
  pause
  exit /b 1
)

rem KISAK_SP_VR_OPENVR_FALLBACK_V49
rem OpenXR remains primary. If its 32-bit loader cannot find a compatible
rem runtime, V49 uses SteamVR's architecture-matched 32-bit OpenVR client.
if not defined KISAK_VR_BACKEND set "KISAK_VR_BACKEND=auto"

rem KISAK_SP_VR_OPENXR_STARTUP_DIAGNOSTICS_V31
rem Capture the 32-bit runtime selected for this 32-bit game, registered API
rem layers, and the Khronos loader's own messages before creating an instance.
set "VR_OPENXR_STARTUP_LOG=%~dp0OpenXR-Startup.log"
set "KISAK_VR_LOADER_LOG=%VR_OPENXR_STARTUP_LOG%"
set "XR_LOADER_DEBUG=all"
set "VR_ACTIVE_RUNTIME_32="

for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\Khronos\OpenXR\1" /v ActiveRuntime /reg:32 2^>nul ^| findstr /i "ActiveRuntime"') do set "VR_ACTIVE_RUNTIME_32=%%B"

>"%VR_OPENXR_STARTUP_LOG%" echo KisakCOD VR startup diagnostics V49
>>"%VR_OPENXR_STARTUP_LOG%" echo Date: %DATE% %TIME%
>>"%VR_OPENXR_STARTUP_LOG%" echo Game binary: 32-bit x86
>>"%VR_OPENXR_STARTUP_LOG%" echo Windows architecture: %PROCESSOR_ARCHITECTURE%
>>"%VR_OPENXR_STARTUP_LOG%" echo Backend policy: %KISAK_VR_BACKEND%
>>"%VR_OPENXR_STARTUP_LOG%" echo ==== OpenXR environment overrides ====
>>"%VR_OPENXR_STARTUP_LOG%" set XR_RUNTIME_JSON 2>&1
>>"%VR_OPENXR_STARTUP_LOG%" set XR_API_LAYER_PATH 2>&1
>>"%VR_OPENXR_STARTUP_LOG%" set XR_ENABLE_API_LAYERS 2>&1
>>"%VR_OPENXR_STARTUP_LOG%" echo.
>>"%VR_OPENXR_STARTUP_LOG%" echo ==== 32-bit active runtime ====
>>"%VR_OPENXR_STARTUP_LOG%" reg query "HKLM\SOFTWARE\Khronos\OpenXR\1" /v ActiveRuntime /reg:32 2>&1

setlocal EnableDelayedExpansion
if defined VR_ACTIVE_RUNTIME_32 (
  >>"%VR_OPENXR_STARTUP_LOG%" echo Resolved manifest: !VR_ACTIVE_RUNTIME_32!
  if exist "!VR_ACTIVE_RUNTIME_32!" (
    >>"%VR_OPENXR_STARTUP_LOG%" echo Manifest exists: YES
  ) else (
    >>"%VR_OPENXR_STARTUP_LOG%" echo Manifest exists: NO
  )
) else (
  >>"%VR_OPENXR_STARTUP_LOG%" echo No 32-bit ActiveRuntime value was found.
)
endlocal

>>"%VR_OPENXR_STARTUP_LOG%" echo.
>>"%VR_OPENXR_STARTUP_LOG%" echo ==== 64-bit active runtime for comparison ====
>>"%VR_OPENXR_STARTUP_LOG%" reg query "HKLM\SOFTWARE\Khronos\OpenXR\1" /v ActiveRuntime /reg:64 2>&1
>>"%VR_OPENXR_STARTUP_LOG%" echo.
>>"%VR_OPENXR_STARTUP_LOG%" echo ==== 32-bit implicit API layers: HKLM ====
>>"%VR_OPENXR_STARTUP_LOG%" reg query "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /reg:32 2>&1
>>"%VR_OPENXR_STARTUP_LOG%" echo.
>>"%VR_OPENXR_STARTUP_LOG%" echo ==== 32-bit implicit API layers: HKCU ====
>>"%VR_OPENXR_STARTUP_LOG%" reg query "HKCU\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" /reg:32 2>&1
>>"%VR_OPENXR_STARTUP_LOG%" echo.
>>"%VR_OPENXR_STARTUP_LOG%" echo ==== 32-bit explicit API layers: HKLM ====
>>"%VR_OPENXR_STARTUP_LOG%" reg query "HKLM\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Explicit" /reg:32 2>&1
>>"%VR_OPENXR_STARTUP_LOG%" echo.
>>"%VR_OPENXR_STARTUP_LOG%" echo ==== 32-bit explicit API layers: HKCU ====
>>"%VR_OPENXR_STARTUP_LOG%" reg query "HKCU\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Explicit" /reg:32 2>&1
>>"%VR_OPENXR_STARTUP_LOG%" echo.
>>"%VR_OPENXR_STARTUP_LOG%" echo ==== OpenVR runtime registry ====
if exist "%LOCALAPPDATA%\openvr\openvrpaths.vrpath" (
  >>"%VR_OPENXR_STARTUP_LOG%" type "%LOCALAPPDATA%\openvr\openvrpaths.vrpath"
) else (
  >>"%VR_OPENXR_STARTUP_LOG%" echo %LOCALAPPDATA%\openvr\openvrpaths.vrpath was not found.
)
>>"%VR_OPENXR_STARTUP_LOG%" echo.

set "VR_DEVELOPER=0"
set "VR_ERROR_TIME=0"
if "%KISAK_VR_DIAGNOSTICS%"=="1" (
  set "VR_DEVELOPER=1"
  set "VR_ERROR_TIME=8"
)

"%~dp0KisakCOD-sp.exe" ^
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
  +set mis_cheat 1 ^
  +set sm_enable 1 ^
  +set sm_sunEnable 1 ^
  +set sm_spotEnable 1 ^
  +set sm_maxLights 4 ^
  +set cg_drawPerformanceWarnings 0 ^
  +set developer 0 ^
  +set developer_script 0 ^
  +set uiscript_debug 0 ^
  +set con_errormessagetime 0 ^
  +set con_minicon 0 ^
  +set cg_drawFPS 0 ^
  +set com_statmon 0

set "VR_EXIT_CODE=%ERRORLEVEL%"
if not "%VR_EXIT_CODE%"=="0" (
  echo.
  if "%VR_EXIT_CODE%"=="31" (
    echo KisakCOD VR stopped because both the requested primary backend and
    echo the V49 SteamVR fallback failed to initialize.
    echo See OpenXR-Startup.log and main\console.log for the exact cause.
  ) else (
    echo KisakCOD VR exited with code %VR_EXIT_CODE%.
    if exist "%KISAK_VR_CRASH_DIR%\LATEST.txt" (
      echo A V48 crash report and minidump were recorded in CrashDumps.
      echo Run Collect-KisakCOD-VR-Crash-Report.bat and send the ZIP privately.
    ) else (
      echo No V48 minidump was recorded.
      echo Run Collect-KisakCOD-VR-Crash-Report.bat to collect the remaining logs.
    )
    echo See OpenXR-Startup.log and main\console.log for additional details.
  )
  pause
)

endlocal & exit /b %VR_EXIT_CODE%
