@echo off
rem KisakCOD VR settings
rem Fully close and restart the game after editing this file.

rem Runtime backend selection:
rem   auto   = normal behavior; use OpenXR first and OpenVR only when no
rem            compatible 32-bit OpenXR runtime can be enumerated.
rem   openxr = force OpenXR (VDXR or Pimax OpenXR).
rem   openvr = force the experimental 32-bit SteamVR/OpenVR fallback.
rem OpenVR currently has no motion-controller input.
set "KISAK_VR_BACKEND=auto"

rem Current Quest 3 / Virtual Desktop native packed mode.
rem Valid lower preset: use VR_CUSTOM_MODE=4768x2016 together with
rem KISAK_VR_OUTPUT_SCALE=0.75 below. Do not use the obsolete 3072x1536 mode;
rem it cannot contain two rectangular eyes plus the dedicated scope panel.
set "VR_CUSTOM_MODE=6016x2688"

rem Physical scope placement, measured in meters.
set "KISAK_VR_SCOPE_FORWARD_METERS=-0.10"
set "KISAK_VR_SCOPE_LEFT_METERS=0"
set "KISAK_VR_SCOPE_UP_METERS=0"
set "KISAK_VR_SCOPE_RADIUS_METERS=0.024"
set "KISAK_VR_SCOPE_CAPTURE_SIZE=1024"

rem GPU bridge and compositor.
rem Keep 1.0 for 6016x2688; use 0.75 with the 4768x2016 lower preset.
set "KISAK_VR_GPU_BRIDGE=1"
set "KISAK_VR_ALLOW_OVERSIZED_WINDOW=1"
set "KISAK_VR_OUTPUT_SCALE=1.0"
set "KISAK_VR_FSR=0"
set "KISAK_VR_BRIGHTNESS=1.00"

rem High-volume retired mission/controller traces. Leave off for normal play.
set "KISAK_VR_VERBOSE_DIAGNOSTICS=0"

rem KISAK_SP_VR_SMOOTH_TURN_OPTION_V50
rem Right-stick turning: snap preserves the existing 45-degree comfort turn;
rem smooth applies analog continuous turning. Restart after changing either.
set "KISAK_VR_TURN_MODE=snap"
set "KISAK_VR_SMOOTH_TURN_SPEED=120"

rem Physical magazines for supported rifles, SMGs, and pistols.
rem Right A ejects; squeeze at the left hip draws; release at the magazine
rem well inserts. Set to 0 to make right A use COD4's native reload.
set "KISAK_VR_MANUAL_RELOAD=1"

rem Physical hip-drawn grenades. Left hip selects frag; right hip selects
rem the mission-equipped flashbang or smoke. Hold/cook, then physically swing
rem and release the left grip. Set to 0 for beta.5's legacy grenade controls.
set "KISAK_VR_MANUAL_GRENADES=1"

rem Two-bone IK left hand at the physical OpenXR grip pose without stretching.
rem The right hand follows the right-controller rifle. Set to 0 for stock arms.
set "KISAK_VR_TRACKED_HANDS=1"

rem Set to 0 if synchronized shadows cause corruption or poor performance.
set "KISAK_VR_SHADOWS=1"

rem HUD placement. Lower HUD_SAFE_X moves the left-side ammo/action HUD right.
set "KISAK_VR_HUD_SAFE_X=0.50"
set "KISAK_VR_HUD_SAFE_Y=1.0"
set "KISAK_VR_HUD_BOTTOM_LEFT_SCALE=0.50"

rem Compass inset in pixels.
set "KISAK_VR_COMPASS_INSET_X=220"
set "KISAK_VR_COMPASS_INSET_Y=48"
