@echo off
rem KisakCOD VR settings
rem Fully close and restart the game after editing this file.

rem Current Quest 3 / Virtual Desktop test mode.
rem Lower development fallback: 3072x1536
set "VR_CUSTOM_MODE=6016x2688"

rem Physical scope placement, measured in meters.
set "KISAK_VR_SCOPE_FORWARD_METERS=-0.10"
set "KISAK_VR_SCOPE_LEFT_METERS=0"
set "KISAK_VR_SCOPE_UP_METERS=0"
set "KISAK_VR_SCOPE_RADIUS_METERS=0.024"
set "KISAK_VR_SCOPE_CAPTURE_SIZE=1024"

rem GPU bridge and compositor.
set "KISAK_VR_GPU_BRIDGE=1"
set "KISAK_VR_ALLOW_OVERSIZED_WINDOW=1"
set "KISAK_VR_OUTPUT_SCALE=1.0"
set "KISAK_VR_FSR=0"
set "KISAK_VR_BRIGHTNESS=1.00"

rem Set to 0 if synchronized shadows cause corruption or poor performance.
set "KISAK_VR_SHADOWS=1"

rem HUD placement. Lower HUD_SAFE_X moves the left-side ammo/action HUD right.
set "KISAK_VR_HUD_SAFE_X=0.50"
set "KISAK_VR_HUD_SAFE_Y=1.0"
set "KISAK_VR_HUD_BOTTOM_LEFT_SCALE=0.50"

rem Compass inset in pixels.
set "KISAK_VR_COMPASS_INSET_X=220"
set "KISAK_VR_COMPASS_INSET_Y=48"
