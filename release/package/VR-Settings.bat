@echo off
rem KisakCOD VR beta.9 defaults (setup, calibration, and interactions).
rem Do not put personal changes here: KisakCOD-VR-Configurator.exe writes a
rem separate VR-User-Settings.bat under LocalAppData so upgrades preserve them.
rem Profile: Tested Quest 3
rem Revision: beta9-unified-calibration-interactions-defaults

set "KISAK_VR_SETTINGS_PROFILE=Tested Quest 3"
set "KISAK_VR_SETTINGS_REVISION=beta9-unified-calibration-interactions-defaults"

rem Runtime backend: auto, openxr, or the experimental openvr fallback.
set "KISAK_VR_BACKEND=auto"

rem Configurator measurement presentation. Runtime values below retain their
rem canonical game-compatible units so existing profiles remain compatible.
set "KISAK_VR_UNIT_SYSTEM=metric"
set "KISAK_VR_DOMINANT_HAND=right"

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

rem Guided recenter and player-height calibration. COD4's native standing
rem camera is 60 game inches; both defaults therefore preserve beta.7 scale.
set "KISAK_VR_PLAY_MODE=standing"
set "KISAK_VR_STANDING_EYE_HEIGHT=60.0"
set "KISAK_VR_SEATED_EYE_HEIGHT=60.0"
set "KISAK_VR_RECENTER_ON_START=1"

rem HUD, compass, and mission text placement.
set "KISAK_VR_HUD_SAFE_X=0.50"
set "KISAK_VR_HUD_SAFE_Y=1.00"
set "KISAK_VR_HUD_BOTTOM_LEFT_X_OFFSET=0"
set "KISAK_VR_HUD_BOTTOM_LEFT_Y_OFFSET=0"
set "KISAK_VR_HUD_BOTTOM_LEFT_SCALE=0.50"
set "KISAK_VR_COMPASS_ENABLED=1"
set "KISAK_VR_COMPASS_SIZE=1.00"
set "KISAK_VR_COMPASS_ROTATION=1"
set "KISAK_VR_COMPASS_INSET_X=220"
set "KISAK_VR_COMPASS_INSET_Y=48"
set "KISAK_VR_GAME_MESSAGE_X_OFFSET=0"
set "KISAK_VR_GAME_MESSAGE_Y_OFFSET=72"
set "KISAK_VR_GAME_MESSAGE_SCALE=1.00"
set "KISAK_VR_OBJECTIVE_MESSAGE_X_OFFSET=0"
set "KISAK_VR_OBJECTIVE_MESSAGE_Y_OFFSET=0"
set "KISAK_VR_OBJECTIVE_MESSAGE_SCALE=1.00"
set "KISAK_VR_CROSSHAIR=1"
set "KISAK_VR_SUBTITLES=1"
set "KISAK_VR_SUBTITLE_X_OFFSET=0"
set "KISAK_VR_SUBTITLE_Y_OFFSET=0"
set "KISAK_VR_SUBTITLE_SCALE=1.00"

rem Dominant weapon-hand calibration and tracking response.
set "KISAK_VR_WEAPON_OFFSET_FORWARD=0.00"
set "KISAK_VR_WEAPON_OFFSET_LEFT=0.00"
set "KISAK_VR_WEAPON_OFFSET_UP=0.00"
set "KISAK_VR_WEAPON_PITCH=0.0"
set "KISAK_VR_WEAPON_YAW=0.0"
set "KISAK_VR_WEAPON_ROLL=0.0"
set "KISAK_VR_WEAPON_PROFILES_ENABLED=1"
set "KISAK_VR_WEAPON_POSITION_RESPONSE=0.45"
set "KISAK_VR_WEAPON_ORIENTATION_RESPONSE=0.55"

rem Floating off-hand fit and two-hand stabilization.
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
set "KISAK_VR_RELOAD_EJECT_MODE=button"
set "KISAK_VR_RELOAD_INSERT_MODE=release"
set "KISAK_VR_RELOAD_PULL_DISTANCE=8.0"
set "KISAK_VR_MAGAZINE_HIP=off_hand"
set "KISAK_VR_MANUAL_GRENADES=1"
set "KISAK_VR_GRENADE_BELT_LAYOUT=handed"
set "KISAK_VR_SUPPORT_GRIP_MODE=hold"
set "KISAK_VR_OBJECT_GRIP_MODE=hold"
set "KISAK_VR_MELEE_MODE=both"
set "KISAK_VR_MELEE_SPEED=95"
set "KISAK_VR_MELEE_FORWARD_BIAS=0.55"
set "KISAK_VR_MELEE_COOLDOWN_MS=550"
set "KISAK_VR_HAPTICS=1"
set "KISAK_VR_HAPTIC_STRENGTH=1.00"
set "KISAK_VR_MUZZLE_OBSTRUCTION=1"
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

rem Controller Input V4. Any gameplay action may use either controller.
rem Use + between inputs that must be held together; primary and alternate
rem slots remain alternatives. Unbound disables that slot.
set "KISAK_VR_INPUT_BINDINGS_VERSION=4"
set "KISAK_VR_BIND_ATTACK=right.trigger"
set "KISAK_VR_BIND_ATTACK_ALT=unbound"
set "KISAK_VR_BIND_AIM=unbound"
set "KISAK_VR_BIND_AIM_ALT=unbound"
set "KISAK_VR_BIND_JUMP=right.primary_axis.up"
set "KISAK_VR_BIND_JUMP_ALT=left.trigger"
set "KISAK_VR_BIND_USE=left.primary"
set "KISAK_VR_BIND_USE_ALT=unbound"
set "KISAK_VR_BIND_RELOAD=right.primary"
set "KISAK_VR_BIND_RELOAD_ALT=unbound"
set "KISAK_VR_BIND_SPRINT=left.thumbstick_click"
set "KISAK_VR_BIND_SPRINT_ALT=unbound"
set "KISAK_VR_BIND_MELEE=right.thumbstick_click"
set "KISAK_VR_BIND_MELEE_ALT=unbound"
set "KISAK_VR_BIND_STANCE=right.secondary"
set "KISAK_VR_BIND_STANCE_ALT=unbound"
set "KISAK_VR_BIND_LOWER_STANCE=right.primary_axis.down"
set "KISAK_VR_BIND_LOWER_STANCE_ALT=unbound"
set "KISAK_VR_BIND_NEXT_WEAPON=left.secondary"
set "KISAK_VR_BIND_NEXT_WEAPON_ALT=unbound"
set "KISAK_VR_BIND_OFFHAND=unbound"
set "KISAK_VR_BIND_OFFHAND_ALT=unbound"
set "KISAK_VR_BIND_SUPPORT_GRIP=left.squeeze"
set "KISAK_VR_BIND_SUPPORT_GRIP_ALT=unbound"
set "KISAK_VR_BIND_MENU=left.menu"
set "KISAK_VR_BIND_MENU_ALT=unbound"
set "KISAK_VR_BIND_MENU_CONFIRM=right.primary"
set "KISAK_VR_BIND_MENU_CONFIRM_ALT=unbound"
set "KISAK_VR_BIND_MENU_BACK=right.secondary"
set "KISAK_VR_BIND_MENU_BACK_ALT=unbound"
set "KISAK_VR_BIND_MENU_AXIS=left.primary_axis"
set "KISAK_VR_BIND_MENU_AXIS_ALT=unbound"
set "KISAK_VR_BIND_GRENADE_LAUNCHER=right.thumbrest_touch+left.primary_axis.up"
set "KISAK_VR_BIND_GRENADE_LAUNCHER_ALT=unbound"
set "KISAK_VR_BIND_NIGHT_VISION=right.thumbrest_touch+left.primary_axis.down"
set "KISAK_VR_BIND_NIGHT_VISION_ALT=unbound"
set "KISAK_VR_BIND_AIRSTRIKE=right.thumbrest_touch+left.primary_axis.left"
set "KISAK_VR_BIND_AIRSTRIKE_ALT=unbound"
set "KISAK_VR_BIND_C4=right.thumbrest_touch+left.primary_axis.right"
set "KISAK_VR_BIND_C4_ALT=unbound"
set "KISAK_VR_BIND_SCOPE_ZOOM_AXIS=left.primary_axis"
set "KISAK_VR_BIND_SCOPE_ZOOM_AXIS_ALT=unbound"
set "KISAK_VR_BIND_MOVE_AXIS=left.primary_axis"
set "KISAK_VR_BIND_MOVE_AXIS_ALT=unbound"
set "KISAK_VR_BIND_TURN_AXIS=right.primary_axis"
set "KISAK_VR_BIND_TURN_AXIS_ALT=unbound"

rem Advanced gameplay and diagnostics.
set "KISAK_VR_UNLOCK_MISSIONS=1"
set "KISAK_VR_VERBOSE_DIAGNOSTICS=0"
set "KISAK_VR_CAMERA_SHAKE=1"
set "KISAK_VR_WEAPON_BOB_AMPLITUDE=0.16"
