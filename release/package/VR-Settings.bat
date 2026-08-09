@echo off
rem KisakCOD VR release defaults (V56 configurator schema).
rem Do not put personal changes here: KisakCOD-VR-Configurator.exe writes a
rem separate VR-User-Settings.bat under LocalAppData so upgrades preserve them.

rem Runtime backend: auto, openxr, or the experimental openvr fallback.
set "KISAK_VR_BACKEND=auto"

rem Verified packed rendering layouts. Never use the obsolete 3072x1536 mode.
set "VR_CUSTOM_MODE=6016x2688"
set "KISAK_VR_OUTPUT_SCALE=1.00"
set "KISAK_VR_FSR=0"
set "KISAK_VR_FSR_SHARPNESS=0.60"
set "KISAK_VR_BRIGHTNESS=1.00"
set "KISAK_VR_SHADOWS=1"
set "KISAK_VR_GPU_BRIDGE=1"
set "KISAK_VR_ALLOW_OVERSIZED_WINDOW=1"

rem Comfort and locomotion.
set "KISAK_VR_TURN_MODE=snap"
set "KISAK_VR_SNAP_TURN_ANGLE=45"
set "KISAK_VR_SMOOTH_TURN_SPEED=120"
set "KISAK_VR_TURN_DEADZONE=0.25"
set "KISAK_VR_MOVEMENT_DIRECTION=head"
set "KISAK_VR_MOVEMENT_DEADZONE=0.18"

rem HUD, compass, and mission text placement.
set "KISAK_VR_HUD_SAFE_X=0.50"
set "KISAK_VR_HUD_SAFE_Y=1.00"
set "KISAK_VR_HUD_BOTTOM_LEFT_SCALE=0.50"
set "KISAK_VR_COMPASS_ENABLED=1"
set "KISAK_VR_COMPASS_SIZE=1.00"
set "KISAK_VR_COMPASS_ROTATION=1"
set "KISAK_VR_COMPASS_INSET_X=220"
set "KISAK_VR_COMPASS_INSET_Y=48"
set "KISAK_VR_GAME_MESSAGE_X_OFFSET=0"
set "KISAK_VR_GAME_MESSAGE_Y_OFFSET=72"
set "KISAK_VR_GAME_MESSAGE_SCALE=1.00"
set "KISAK_VR_CROSSHAIR=1"
set "KISAK_VR_SUBTITLES=1"

rem Right-hand weapon calibration and tracking response.
set "KISAK_VR_WEAPON_OFFSET_FORWARD=0.00"
set "KISAK_VR_WEAPON_OFFSET_LEFT=0.00"
set "KISAK_VR_WEAPON_OFFSET_UP=0.00"
set "KISAK_VR_WEAPON_PITCH=0.0"
set "KISAK_VR_WEAPON_YAW=0.0"
set "KISAK_VR_WEAPON_ROLL=0.0"
set "KISAK_VR_WEAPON_POSITION_RESPONSE=0.45"
set "KISAK_VR_WEAPON_ORIENTATION_RESPONSE=0.55"

rem Floating left-hand fit and two-hand stabilization.
set "KISAK_VR_TRACKED_HANDS=1"
set "KISAK_VR_LEFT_HAND_OFFSET_FORWARD=0.00"
set "KISAK_VR_LEFT_HAND_OFFSET_LEFT=0.00"
set "KISAK_VR_LEFT_HAND_OFFSET_UP=0.00"
set "KISAK_VR_LEFT_HAND_PITCH=0.0"
set "KISAK_VR_LEFT_HAND_YAW=0.0"
set "KISAK_VR_LEFT_HAND_ROLL=0.0"
set "KISAK_VR_LEFT_HAND_GRIP_RADIUS=14.0"
set "KISAK_VR_TWO_HAND_STRENGTH=1.00"

rem Physical interactions and shared belt calibration.
set "KISAK_VR_MANUAL_RELOAD=1"
set "KISAK_VR_MANUAL_GRENADES=1"
set "KISAK_VR_BELT_FORWARD_OFFSET=0.0"
set "KISAK_VR_BELT_HEIGHT=-28.0"
set "KISAK_VR_BELT_HIP_DISTANCE=13.0"
set "KISAK_VR_BELT_GRAB_RADIUS=11.0"
set "KISAK_VR_RELOAD_INSERT_RADIUS=6.5"

rem Physical grenade calibration. Defaults reproduce beta.6 behavior.
set "KISAK_VR_GRENADE_DROP_SPEED=35"
set "KISAK_VR_GRENADE_FULL_THROW_SPEED=260"
set "KISAK_VR_GRENADE_MIN_STRENGTH=0.70"
set "KISAK_VR_GRENADE_MAX_STRENGTH=1.15"
set "KISAK_VR_GRENADE_VERTICAL_SCALE=0.65"

rem Rifle-attached physical scope placement and capture quality.
set "KISAK_VR_SCOPE_FORWARD_METERS=-0.10"
set "KISAK_VR_SCOPE_LEFT_METERS=0.000"
set "KISAK_VR_SCOPE_UP_METERS=0.000"
set "KISAK_VR_SCOPE_RADIUS_METERS=0.024"
set "KISAK_VR_SCOPE_CAPTURE_SIZE=1024"

rem Remappable face-button roles. Trigger, grip, sticks, and the menu button
rem remain fixed because they own pose-sensitive or system interactions.
set "KISAK_VR_BIND_USE=x"
set "KISAK_VR_BIND_SPRINT=stick"
set "KISAK_VR_BIND_NEXT_WEAPON=y"
set "KISAK_VR_BIND_RELOAD=a"
set "KISAK_VR_BIND_MELEE=stick"
set "KISAK_VR_BIND_STANCE=b"

rem Advanced gameplay and diagnostics.
set "KISAK_VR_UNLOCK_MISSIONS=1"
set "KISAK_VR_VERBOSE_DIAGNOSTICS=0"
set "KISAK_VR_CAMERA_SHAKE=1"
set "KISAK_VR_WEAPON_BOB_AMPLITUDE=0.16"
