#pragma once

#include "vr/vr_hud_layout.h"

#include <cstdint>

// Initializes OpenXR first and automatically falls back to SteamVR's 32-bit
// OpenVR client when no compatible 32-bit OpenXR runtime is available.
//
// The normal Call of Duty Direct3D 9 renderer remains untouched.
bool VR_Init();

// Returns the last human-readable VR initialization failure. This remains
// valid after VR_Init returns false so WinMain can stop with a useful message.
const char* VR_GetLastStartupError();

// Polls the active runtime and submits one stereo frame. Call once per game
// frame.
void VR_Frame();

// Releases the active runtime and its D3D11 resources.
void VR_Shutdown();

// Returns true after either backend and its D3D11 compositor have initialized.
bool VR_IsInitialized();

// Returns "OpenXR", "OpenVR/SteamVR", or "none".
const char* VR_GetActiveBackendName();

// KISAK_SP_VR_CAPTURE_POSE_METADATA_V32
// Associates the current OpenXR render views with the exact legacy renderer
// frame that is about to build the packed stereo scene.
void VR_RecordRenderFramePose(
    unsigned int renderFrameId);

// High-volume controller, hand-model, and retired mission diagnostics are
// release-disabled unless KISAK_VR_VERBOSE_DIAGNOSTICS=1 is set.
bool VR_VerboseDiagnosticsEnabled();

// KISAK_SP_VR_QUIT_CONFIRMATION_MONO_V45
// True while the top UI menu is a quit/leave-game confirmation.  These
// nested dialogs are painted as one full packed 2D canvas rather than as
// ordinary pause UI inside a stereo-eye command list.
bool VR_IsQuitConfirmationMenuActive();

// KISAK_SP_VR_FIXED_SCOPED_TURRET_VIEW_FIX_V1
// Publishes whether CoD4 currently has the player locked to a fixed weapon
// with a native scope overlay (the mission-start Barrett in sniperescape).
// The compositor centers its vignette and reticle independently per HMD eye.
void VR_SetFixedScopedTurretState(bool active);

// Publishes the current SP weapon-optic transition to the OpenXR compositor.
// Scoped ADS is magnified only inside the controller-aligned physical lens;
// the normal HMD field of view remains visible outside it.
void VR_SetPhysicalSniperScopeState(
    bool active,
    float adsFraction,
    float adsFovDegrees);

// True only while the SP physical sniper lens/reticle is visibly active.
// The authoritative bullet path uses this to remove random spread and
// converge the physical muzzle ray on the reticle target.
bool VR_IsPhysicalSniperScopeAimActive();

// Publishes the final rendered viewmodel optic pose.  The compositor uses
// the grip-relative anchor to keep the lens rigidly attached to the rifle;
// the SP game uses the world-space ray for ballistic convergence.
void VR_PublishPhysicalSniperScopePoseWorld(
    const float scopeOrigin[3],
    const float scopeAxis[3][3],
    const float cameraOrigin[3],
    const float cameraAxis[3][3]);

bool VR_GetPhysicalSniperScopeAimWorld(
    float scopeOrigin[3],
    float scopeForward[3]);

// KISAK_VR_DEDICATED_SCOPE_CAMERA_V2
// Returns a packed backbuffer layout that preserves the native side-by-side
// eye images and reserves one square panel for a weapon-free scope camera.
// The layout is available whenever the D3D9 backbuffer is wide enough,
// regardless of whether the player is currently aiming through a scope.
bool VR_GetPhysicalSniperScopeCaptureLayout(
    int backbufferWidth,
    int backbufferHeight,
    int* mainStereoWidth,
    int* scopePanelX,
    int* scopePanelY,
    int* scopePanelSize);

// Returns the current calibrated optic camera pose and its square projection.
// This uses the same origin and forward axis as ballistic convergence.
bool VR_GetPhysicalSniperScopeRenderView(
    float scopeOrigin[3],
    float scopeAxis[3][3],
    float* tanHalfFovX,
    float* tanHalfFovY);

// Applies the latest recentered OpenXR headset orientation to an
// existing CoD camera axis. Returns false until a valid pose exists.
bool VR_ApplyHeadPosition(
    float viewOrigin[3],
    const float viewAxis[3][3],
    float nativeViewHeightCurrent);

// Makes the latest physical headset center the zero-translation pose.
// Orientation is not changed. Returns false until a valid head pose exists.
bool VR_RecenterHeadPosition();

// Captures the latest tracked headset position and orientation as the new
// forward/level origin. This is used by first-gameplay and live guided
// calibration; controller poses remain rigidly head-relative.
bool VR_RecenterHeadPose();
bool VR_RecenterOnStartEnabled();

// V61 visual HUD editor. The runtime layout overrides environment defaults
// only while an in-headset edit is active (or after that edit is saved for
// the current process). Other render modules read this same snapshot so the
// boxes manipulated in the editor are the actual HUD groups being moved.
bool VR_GetActiveHudLayout(
    kisak::vr::hud::Layout* layout);
std::uint64_t VR_GetHudLayoutRevision();
bool VR_GetHudEditorSnapshot(
    kisak::vr::hud::EditorSnapshot* snapshot);
bool VR_HudEditorConsumesGameplayInput();

bool VR_ApplyHeadOrientation(float viewAxis[3][3]);
bool VR_ApplyStereoEyeOffsetForEye(
    float viewOrigin[3],
    const float viewAxis[3][3],
    unsigned int eyeIndex);

// Publishes which eye the following CL_RenderScene call is building.
// The renderer uses this only while constructing that eye's camera view.
void VR_BeginStereoEyeRender(unsigned int eyeIndex);
void VR_EndStereoEyeRender();

// Returns a conservative symmetric FOV bound for legacy CoD systems such as
// LOD selection. The actual render projection remains asymmetric.
bool VR_GetStereoEyeFovBounds(
    unsigned int eyeIndex,
    float* tanHalfFovX,
    float* tanHalfFovY);

// Returns the active eye's exact OpenXR frustum tangents. Values use the
// OpenXR convention: left/down negative, right/up positive.
bool VR_GetCurrentRenderEyeProjection(
    float* tanLeft,
    float* tanRight,
    float* tanDown,
    float* tanUp);

// Applies the tracked semantic weapon-controller pose to the already-built
// first-person weapon placement. The viewmodel attachment is calibrated
// against a canonical controller basis, so startup controller orientation
// cannot become a permanent neutral-pose offset. Positional calibration is
// also published for the final viewmodel grip-tag alignment, preventing that
// later correction from cancelling forward/left/up offsets.
bool VR_ApplyRightControllerToWeaponPlacement(
    int weaponIndex,
    const char* weaponId,
    const char* weaponName,
    float adsFraction,
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float weaponOrigin[3],
    float weaponAxis[3][3]);

// Returns the filtered weapon-controller grip target in world space, including
// the positional calibration published by the final weapon transform.
bool VR_GetRightControllerWeaponGripWorld(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float gripWorld[3]);

// Records proof that the final viewmodel grip-tag alignment reached the
// calibrated target. This is called only after the corrected pose is rebuilt.
void VR_ReportRightControllerWeaponGripAlignment(
    const char* gripTagName,
    const float calibratedTargetWorld[3],
    const float alignedTagWorld[3]);

// KISAK_SP_VR_TRACKED_HANDS_V1
// Returns the latest semantic grip pose in CoD world space.  The legacy
// leftHand=true branch is the off hand; false is the weapon hand. The off hand
// uses the raw grip pose; the weapon hand uses the filtered grip position but
// retains the runtime's grip-space orientation instead of its aim-space
// orientation.  This keeps independently rendered gloves attached to the
// physical controllers without changing weapon aiming.
bool VR_GetTrackedControllerGripPoseWorld(
    bool leftHand,
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float gripOrigin[3],
    float gripAxis[3][3]);

// KISAK_SP_VR_TRACKED_HANDS_V24_DIRECT_OPENXR_GRIP_QUATERNION
// Returns the off-hand grip origin in CoD world space plus the normalized OpenXR
// grip orientation relative to the current HMD.  The free-hand renderer uses
// this quaternion to reproduce the same rigid pose as the compositor proxy,
// bypassing the legacy shared foregrip matrix without changing reload or
// two-hand weapon consumers of that matrix.
bool VR_GetTrackedLeftControllerGripQuaternionWorld(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float gripOrigin[3],
    float gripOrientationHeadLocalOpenXr[4]);

// KISAK_SP_VR_TRACKED_HANDS_V25_OPENXR_PALM_SURFACE_POSE
// Returns XR_EXT_palm_pose when the runtime exposes it.  Unlike grip/pose,
// this controller-specific pose is defined at the physical palm surface and
// oriented for visual hand registration.  Callers must retain the V24 grip
// getter as a fallback for runtimes that do not publish the optional pose.
bool VR_GetTrackedLeftControllerPalmQuaternionWorld(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float palmOrigin[3],
    float palmOrientationHeadLocalOpenXr[4]);

// KISAK_SP_VR_TRACKED_HANDS_V22_CONTROLLER_SPACE_DIAGNOSTICS
// Enables the temporary in-game transform measurements and the compositor-
// true off-hand grip origin/axis overlay. KISAK_VR_HAND_DIAGNOSTICS=0 disables
// both without changing tracked-hand behavior.
bool VR_TrackedHandDiagnosticsEnabled();

// KISAK_SP_VR_TRACKED_HANDS_V14_QUATERNION_WEAPON_GRIP
// Returns the off-hand support state already gated by controller tracking and the
// manual-magazine reload state.  Rendering can therefore attach a support hand
// without duplicating or interfering with OpenXR input ownership.
bool VR_GetLeftControllerSupportGripPressed(
    bool* supportGripPressed);

// Automatic proximity keeps its logical support action held without a
// physical squeeze. The renderer uses this mode bit to apply a finite
// enter/exit radius while leaving Hold and Toggle behavior unchanged.
bool VR_SupportGripUsesAutomaticProximity();

// Returns the configured Attack action without requiring a rendered weapon
// aim pose. This is reserved for native pose-independent interactions such as
// C4 detonators; ordinary firearms must continue using the weapon command
// below so muzzle obstruction and visible-gun alignment remain authoritative.
bool VR_GetConfiguredAttackButton(
    bool* attackPressed);

// Returns the final rendered weapon-hand direction as CoD pitch/yaw
// and the configured Attack state. The direction is taken from
// the transformed viewmodel axis, keeping shots aligned with the visible gun.
bool VR_GetRightControllerWeaponCommand(
    float* gunPitch,
    float* gunYaw,
    bool* attackPressed);

// KISAK_SP_VR_MOUNTED_TURRET_AIM_V1
// Returns the live weapon-controller direction in world-space CoD angles.
// Unlike the normal weapon-command snapshot, this remains available while
// mounted weapons suppress the first-person viewmodel.
bool VR_GetRightControllerMountedWeaponAim(
    float* gunPitch,
    float* gunYaw);

// KISAK_SP_VR_FIXED_SCOPED_TURRET_CONTROLS_V3_HEADER
// The fixed Barrett's binocular reticle represents the completed HMD-center
// camera ray, not the freely moving weapon controller used by ordinary
// mounted guns.
bool VR_GetFixedScopedTurretAim(
    float* gunPitch,
    float* gunYaw);

// Returns true whenever the configured scope-zoom axis is available,
// including while the vertical axis is centered in its deadzone.
bool VR_GetFixedScopedTurretZoomAxis(
    float* zoomAxis);

// KISAK_SP_VR_FIXED_SCOPED_TURRET_RUNTIME_V4
// Publishes CoD4's scoped-turret FOV range to the compositor that presents
// the fixed Barrett in VR. This makes the already-working left-stick value
// visibly magnify the binocular scope instead of changing only a discarded
// flat-screen projection.
void VR_SetFixedScopedTurretZoomFov(
    float currentFovDegrees,
    float maximumFovDegrees);

// KISAK_SP_VR_MOUNTED_WEAPON_TRIGGER_BOOTSTRAP_V1
// Returns the configured Attack state while the live mounted-weapon
// controller pose is available.  This does not depend on a viewmodel.
bool VR_GetRightControllerMountedWeaponTrigger(
    bool* attackPressed);

// Returns configured movement input rotated by the selected reference yaw.
// Values are normalized to [-1, 1] after a circular remapped deadzone.
// Forward and right are expressed in the current CoD usercmd/body basis.
bool VR_GetHmdOrientedMovement(
    float* forward,
    float* right);

// Transfers the current horizontal HMD yaw into the game's body yaw.
// The HMD-relative camera axis is counter-rotated so the visible world
// does not jump when the body catches up to the player's facing direction.
bool VR_TransferHmdYawToBody(
    float* bodyYawDeltaDegrees);

// Returns this client frame's configured right-stick turn delta. Snap mode
// preserves the existing latched 45-degree turn. Smooth mode applies an
// analog degrees-per-second delta using elapsedSeconds. Positive CoD yaw
// turns left, so right-stick right returns a negative delta.
bool VR_GetTurnYawDelta(
    float elapsedSeconds,
    float* yawDeltaDegrees);

// Returns the first configured gameplay-control set. Physical two-hand
// shouldering remains available for ADS; the tested defaults are left trigger
// jump, left primary use, and right primary magazine eject/native reload.
bool VR_GetBasicGameplayButtons(
    bool* adsHeld,
    bool* jumpHeld,
    bool* useHeld,
    bool* reloadHeld);

// KISAK_SP_VR_MANUAL_MAGAZINE_RELOAD_V1
// Publishes the current weapon's physical magazine well and advances the
// OpenXR manual-reload state machine.  Detachable-magazine weapons use right A
// to eject, left squeeze near the hip to draw a fresh magazine, and release at
// the magazine well to insert it.
void VR_UpdateManualMagazineReload(
    int weaponIndex,
    bool supported,
    bool canReload,
    const float magazineWellOrigin[3],
    const float magazineWellAxis[3][3],
    const float cameraOrigin[3],
    const float cameraAxis[3][3]);

// Returns the two optional world-space clip poses rendered by cgame, plus
// whether the loaded viewmodel magazine bone must be hidden.
bool VR_GetManualMagazineReloadRenderState(
    int weaponIndex,
    bool* hideLoadedMagazine,
    bool* drawEjectedMagazine,
    float ejectedOrigin[3],
    float ejectedAxis[3][3],
    bool* drawHeldMagazine,
    float heldOrigin[3],
    float heldAxis[3][3]);

// Used by shared weapon simulation.  Automatic empty-mag reload is blocked
// while a supported VR weapon is active; insertion briefly exposes a native
// reload command whose ammo transfer is completed without replaying COD4's
// canned hand animation.
bool VR_ManualMagazineReloadSuppressesAutomaticReload(
    int weaponIndex);

bool VR_IsManualMagazineReloadCommitActive(
    int weaponIndex);

// KISAK_SP_VR_MANUAL_GRENADE_THROW_V53
// Advances the off-hand hip interaction and returns the native offhand
// buttons that must remain held for COD4's existing cook/throw state machine.
// The selected indices come from predicted player state; configurable belt
// layout decides which physical hip owns frag versus flash/smoke.
bool VR_UpdateManualGrenadeInput(
    int fragWeaponIndex,
    int tacticalWeaponIndex,
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    bool* manualModeEnabled,
    bool* fragHeld,
    bool* tacticalHeld);

// Returns the tracked grenade pose rendered in the off hand while grip is
// held.  The model is resolved by cgame from weaponIndex.
bool VR_GetManualGrenadeRenderState(
    int* weaponIndex,
    float heldOrigin[3],
    float heldAxis[3][3]);

// Keeps the ordinary firearm visible in the tracked weapon hand while the
// native offhand state temporarily selects the grenade viewmodel.
bool VR_IsManualGrenadeViewOverrideActive();

// Shared prediction uses this to remove the canned post-release delay.  The
// native pullback/cook states are preserved; only the final physical release
// is committed on the next simulation step.
bool VR_IsManualGrenadeReleasePending(
    int weaponIndex);

// The SP listen server consumes exactly one release sample when EV_USE_OFFHAND
// fires. Position, recent physical velocity, and the release-time view-forward
// fallback are already transformed into CoD world space. V54 also returns the
// selected history sample age for diagnostics.
bool VR_ConsumeManualGrenadeThrow(
    int weaponIndex,
    float releaseOrigin[3],
    float releaseVelocity[3],
    float releaseFallbackForward[3],
    unsigned int* velocitySampleAgeMilliseconds,
    unsigned int* releaseAgeMilliseconds);

// Returns the configured locomotion/combat controls. Tested defaults are left
// stick click for sprint, right stick click for melee, and right secondary for
// tap-crouch/stand or hold-prone/stand.
bool VR_GetLocomotionCombatButtons(
    bool* sprintHeld,
    bool* meleeHeld,
    bool* stanceHeld);

// Returns the remappable one-step lower-stance control. The tested default is
// right primary-axis down. Right primary-axis up belongs to Jump.
bool VR_GetLowerStanceButton(
    bool* lowerHeld);

// Returns optional weapon-utility controls. Manual-grenade mode consumes the
// support grip separately. When that mode is disabled, the configured native
// off-hand action (unbound by default) drives tactical grenades and the
// configured next-weapon action retains the legacy hold-frag/tap behavior.
bool VR_GetWeaponUtilityButtons(
    bool* offhandHeld,
    bool* leftYHeld);


// Legacy-named compatibility API: applies a short vibration pulse to the
// selected semantic weapon controller for confirmed local firearm events.
bool VR_ApplyRightControllerWeaponHaptic(
    float amplitude,
    float durationSeconds);

// Applies physical-object feedback to whichever controller is currently the
// semantic off hand. Left-handed mode therefore moves magazine/grenade
// feedback to the physical right controller.
bool VR_ApplyOffhandControllerHaptic(
    float amplitude,
    float durationSeconds);


// Publishes and retrieves the latest transformed viewmodel tag_flash world
// position. The client render path publishes it every frame; the local
// listen-server weapon path consumes it for authoritative bullet origins.
void VR_PublishRightControllerWeaponMuzzleWorld(
    const float muzzleOrigin[3]);

bool VR_GetRightControllerWeaponMuzzleWorld(
    float muzzleOrigin[3]);


// Records whether geometry lies between the player's view origin and the
// tracked physical muzzle. The local listen-server bullet path uses this to
// hard-suppress the shot instead of allowing normal wall penetration.
void VR_SetRightControllerWeaponMuzzleBlocked(
    bool blocked);

bool VR_ShouldSuppressRightControllerBlockedMuzzleShot();
