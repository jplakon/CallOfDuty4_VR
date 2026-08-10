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

if not exist "%~dp0KisakCOD-VR-Configurator.exe" (
  echo ERROR: KisakCOD-VR-Configurator.exe is missing from the package.
  echo Beta.8 requires it to validate the exact settings file before launch.
  pause
  exit /b 1
)

call "%~dp0VR-Settings.bat"
if errorlevel 1 (
  echo ERROR: VR-Settings.bat could not be loaded.
  pause
  exit /b 1
)
set "KISAK_VR_SETTINGS_SOURCE=%~dp0VR-Settings.bat"

rem KISAK_SP_VR_SETTINGS_APPLICATION_V62
rem Personal overrides live outside the game directory so Steam, extraction,
rem and future mod updates cannot overwrite them. A portable override beside
rem the launcher is also supported; LocalAppData wins when both exist.
if exist "%~dp0VR-User-Settings.bat" (
  "%~dp0KisakCOD-VR-Configurator.exe" --validate "%~dp0VR-User-Settings.bat"
  if errorlevel 1 (
    echo ERROR: Portable VR-User-Settings.bat failed validation and was not loaded.
    pause
    exit /b 1
  )
  set "KISAK_VR_SETTINGS_PROFILE="
  set "KISAK_VR_SETTINGS_REVISION="
  call "%~dp0VR-User-Settings.bat"
  if errorlevel 1 (
    echo ERROR: Portable VR-User-Settings.bat could not be loaded.
    pause
    exit /b 1
  )
  if not defined KISAK_VR_SETTINGS_PROFILE set "KISAK_VR_SETTINGS_PROFILE=Legacy portable overrides"
  if not defined KISAK_VR_SETTINGS_REVISION set "KISAK_VR_SETTINGS_REVISION=legacy-unverified"
  set "KISAK_VR_SETTINGS_SOURCE=%~dp0VR-User-Settings.bat"
)

set "KISAK_VR_USER_SETTINGS="
if defined LOCALAPPDATA set "KISAK_VR_USER_SETTINGS=%LOCALAPPDATA%\KisakCOD-VR\VR-User-Settings.bat"
if defined KISAK_VR_USER_SETTINGS if exist "%KISAK_VR_USER_SETTINGS%" (
  "%~dp0KisakCOD-VR-Configurator.exe" --validate "%KISAK_VR_USER_SETTINGS%"
  if errorlevel 1 (
    echo ERROR: User settings failed validation and were not loaded:
    echo   "%KISAK_VR_USER_SETTINGS%"
    pause
    exit /b 1
  )
  set "KISAK_VR_SETTINGS_PROFILE="
  set "KISAK_VR_SETTINGS_REVISION="
  call "%KISAK_VR_USER_SETTINGS%"
  if errorlevel 1 (
    echo ERROR: User settings could not be loaded:
    echo   "%KISAK_VR_USER_SETTINGS%"
    pause
    exit /b 1
  )
  if not defined KISAK_VR_SETTINGS_PROFILE set "KISAK_VR_SETTINGS_PROFILE=Legacy LocalAppData overrides"
  if not defined KISAK_VR_SETTINGS_REVISION set "KISAK_VR_SETTINGS_REVISION=legacy-unverified"
  set "KISAK_VR_SETTINGS_SOURCE=%KISAK_VR_USER_SETTINGS%"
)

if not defined KISAK_VR_SETTINGS_PROFILE set "KISAK_VR_SETTINGS_PROFILE=Unknown"
if not defined KISAK_VR_SETTINGS_REVISION set "KISAK_VR_SETTINGS_REVISION=legacy-unverified"

rem Record the exact effective environment before process creation. The game
rem appends RUNTIME_ACCEPTED to this same receipt after parsing its settings,
rem proving which profile and revision crossed both application boundaries.
if defined LOCALAPPDATA (
  set "KISAK_VR_SETTINGS_STATE_DIR=%LOCALAPPDATA%\KisakCOD-VR"
) else (
  set "KISAK_VR_SETTINGS_STATE_DIR=%~dp0UserSettings"
)
if not exist "%KISAK_VR_SETTINGS_STATE_DIR%\" mkdir "%KISAK_VR_SETTINGS_STATE_DIR%" >nul 2>&1
if not exist "%KISAK_VR_SETTINGS_STATE_DIR%\" (
  echo ERROR: Could not create the verified-settings state folder:
  echo   "%KISAK_VR_SETTINGS_STATE_DIR%"
  pause
  exit /b 1
)

set "KISAK_VR_SETTINGS_RECEIPT_PATH=%KISAK_VR_SETTINGS_STATE_DIR%\Active-VR-Settings.txt"
set "KISAK_VR_CALIBRATION_REQUEST_PATH=%KISAK_VR_SETTINGS_STATE_DIR%\Calibration-Request.txt"
set "KISAK_VR_CALIBRATION_STATUS_PATH=%KISAK_VR_SETTINGS_STATE_DIR%\Calibration-Status.txt"
set "KISAK_VR_HUD_EDITOR_REQUEST_PATH=%KISAK_VR_SETTINGS_STATE_DIR%\HUD-Editor-Request.txt"
set "KISAK_VR_HUD_EDITOR_STATUS_PATH=%KISAK_VR_SETTINGS_STATE_DIR%\HUD-Editor-Status.txt"
set "KISAK_VR_WEAPON_PROFILES_PATH=%KISAK_VR_SETTINGS_STATE_DIR%\VR-Weapon-Profiles.ini"
set "KISAK_VR_WEAPON_CALIBRATION_REQUEST_PATH=%KISAK_VR_SETTINGS_STATE_DIR%\Weapon-Calibration-Request.txt"
set "KISAK_VR_WEAPON_CALIBRATION_STATUS_PATH=%KISAK_VR_SETTINGS_STATE_DIR%\Weapon-Calibration-Status.txt"
set "KISAK_VR_COMPATIBILITY_REPORT_PATH=%KISAK_VR_SETTINGS_STATE_DIR%\Compatibility-Report.txt"

rem KISAK_SP_VR_UNIFIED_COMPATIBILITY_V65
rem Use the same evaluator as the first configurator page. Warnings are written
rem to the support report but do not block launch; missing required files,
rem DirectX, or the explicitly selected runtime backend do block before game
rem process creation.
"%~dp0KisakCOD-VR-Configurator.exe" --compatibility-report "%KISAK_VR_COMPATIBILITY_REPORT_PATH%"
set "KISAK_VR_COMPATIBILITY_EXIT=%ERRORLEVEL%"
if "%KISAK_VR_COMPATIBILITY_EXIT%"=="2" (
  echo ERROR: The beta.9 compatibility preflight found a launch blocker.
  echo Open KisakCOD-VR-Configurator.exe and review Setup ^& Compatibility.
  echo Support-ready report:
  echo   "%KISAK_VR_COMPATIBILITY_REPORT_PATH%"
  pause
  exit /b 1
)
if not "%KISAK_VR_COMPATIBILITY_EXIT%"=="0" (
  echo ERROR: The beta.9 compatibility report could not be written.
  echo   "%KISAK_VR_COMPATIBILITY_REPORT_PATH%"
  pause
  exit /b 1
)

del /q "%KISAK_VR_CALIBRATION_REQUEST_PATH%" "%KISAK_VR_CALIBRATION_STATUS_PATH%" >nul 2>&1
del /q "%KISAK_VR_WEAPON_CALIBRATION_REQUEST_PATH%" "%KISAK_VR_WEAPON_CALIBRATION_STATUS_PATH%" >nul 2>&1
set "KISAK_VR_SETTINGS_STATUS=LAUNCHER_VERIFIED"
>"%KISAK_VR_SETTINGS_RECEIPT_PATH%" echo KisakCOD VR beta.9 effective settings receipt
>>"%KISAK_VR_SETTINGS_RECEIPT_PATH%" echo STATUS=LAUNCHER_VERIFIED
>>"%KISAK_VR_SETTINGS_RECEIPT_PATH%" echo DATE=%DATE% %TIME%
>>"%KISAK_VR_SETTINGS_RECEIPT_PATH%" set KISAK_VR_
>>"%KISAK_VR_SETTINGS_RECEIPT_PATH%" set VR_CUSTOM_MODE
if errorlevel 1 (
  echo ERROR: Effective settings could not be written and verified:
  echo   "%KISAK_VR_SETTINGS_RECEIPT_PATH%"
  pause
  exit /b 1
)

rem The diagnostics wrapper is intentionally applied after defaults and user
rem settings so Save & Launch Diagnostics always enables the runtime markers
rem for that one process without changing the saved profile.
if "%KISAK_VR_DIAGNOSTICS%"=="1" set "KISAK_VR_VERBOSE_DIAGNOSTICS=1"

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

if /I not "%VR_CUSTOM_MODE%"=="6016x2688" if /I not "%VR_CUSTOM_MODE%"=="4768x2016" (
  echo ERROR: Unsupported VR_CUSTOM_MODE=%VR_CUSTOM_MODE%.
  echo Use one of the two verified packed renderer pairs:
  echo   VR_CUSTOM_MODE=6016x2688 with KISAK_VR_OUTPUT_SCALE=1.00
  echo   VR_CUSTOM_MODE=4768x2016 with KISAK_VR_OUTPUT_SCALE=0.75
  pause
  exit /b 1
)

if /I "%VR_CUSTOM_MODE%"=="6016x2688" if not "%KISAK_VR_OUTPUT_SCALE%"=="1.00" if not "%KISAK_VR_OUTPUT_SCALE%"=="1.0" (
  echo ERROR: 6016x2688 requires KISAK_VR_OUTPUT_SCALE=1.00.
  echo Select Native in KisakCOD-VR-Configurator.exe.
  pause
  exit /b 1
)

if /I "%VR_CUSTOM_MODE%"=="4768x2016" if not "%KISAK_VR_OUTPUT_SCALE%"=="0.75" (
  echo ERROR: 4768x2016 requires KISAK_VR_OUTPUT_SCALE=0.75.
  echo Select Performance in KisakCOD-VR-Configurator.exe.
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
if "%KISAK_VR_VERBOSE_DIAGNOSTICS%"=="1" (
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
  +set vr_leftHandModelOffsetForward %KISAK_VR_LEFT_HAND_OFFSET_FORWARD% ^
  +set vr_leftHandModelOffsetLeft %KISAK_VR_LEFT_HAND_OFFSET_LEFT% ^
  +set vr_leftHandModelOffsetUp %KISAK_VR_LEFT_HAND_OFFSET_UP% ^
  +set vr_leftHandModelPitch %KISAK_VR_LEFT_HAND_PITCH% ^
  +set vr_leftHandModelYaw %KISAK_VR_LEFT_HAND_YAW% ^
  +set vr_leftHandModelRoll %KISAK_VR_LEFT_HAND_ROLL% ^
  +set vr_leftHandGripRadius %KISAK_VR_LEFT_HAND_GRIP_RADIUS% ^
  +set compass %KISAK_VR_COMPASS_ENABLED% ^
  +set compassSize %KISAK_VR_COMPASS_SIZE% ^
  +set compassRotation %KISAK_VR_COMPASS_ROTATION% ^
  +set cg_drawCrosshair %KISAK_VR_CROSSHAIR% ^
  +set cg_subtitles %KISAK_VR_SUBTITLES% ^
  +set g_earthquakeEnable %KISAK_VR_CAMERA_SHAKE% ^
  +set cg_bobWeaponAmplitude %KISAK_VR_WEAPON_BOB_AMPLITUDE% ^
  +set mis_cheat %KISAK_VR_UNLOCK_MISSIONS% ^
  +set sm_enable 1 ^
  +set sm_sunEnable 1 ^
  +set sm_spotEnable 1 ^
  +set sm_maxLights 4 ^
  +set cg_drawPerformanceWarnings 0 ^
  +set developer %VR_DEVELOPER% ^
  +set developer_script 0 ^
  +set uiscript_debug 0 ^
  +set con_errormessagetime %VR_ERROR_TIME% ^
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
