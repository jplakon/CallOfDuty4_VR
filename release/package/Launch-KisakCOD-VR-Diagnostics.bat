@echo off
set "KISAK_VR_DIAGNOSTICS=1"
call "%~dp0Launch-KisakCOD-VR.bat"
exit /b %ERRORLEVEL%
