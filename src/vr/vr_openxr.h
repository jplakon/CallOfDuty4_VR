#pragma once

// Initializes OpenXR, creates a dedicated D3D11 device, creates an OpenXR
// session, and prepares one color swapchain for each eye.
//
// The normal Call of Duty Direct3D 9 renderer remains untouched.
bool VR_Init();

// Polls OpenXR events and, while the session is running, renders a stereo, head-tracked cube and floor grid
// to the headset. Call once per game frame.
void VR_Frame();

// Releases swapchains, session, D3D11 resources, and the OpenXR instance.
void VR_Shutdown();

// Returns true after OpenXR and its D3D11 session have initialized.
bool VR_IsInitialized();

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
    const float viewAxis[3][3]);

// Makes the latest physical headset center the zero-translation pose.
// Orientation is not changed. Returns false until a valid head pose exists.
bool VR_RecenterHeadPosition();
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

// Applies the tracked right-controller pose to the already-built
// first-person weapon placement. The viewmodel attachment is calibrated
// against a canonical controller basis, so startup controller orientation
// cannot become a permanent neutral-pose offset.
bool VR_ApplyRightControllerToWeaponPlacement(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float weaponOrigin[3],
    float weaponAxis[3][3]);

// Returns the final rendered right-hand weapon direction as CoD pitch/yaw
// and the current right index-trigger state. The direction is taken from
// the transformed viewmodel axis, keeping shots aligned with the visible gun.
bool VR_GetRightControllerWeaponCommand(
    float* gunPitch,
    float* gunYaw,
    bool* attackPressed);

// KISAK_SP_VR_MOUNTED_TURRET_AIM_V1
// Returns the live right-controller direction in world-space CoD angles.
// Unlike the normal weapon-command snapshot, this remains available while
// mounted weapons suppress the first-person viewmodel.
bool VR_GetRightControllerMountedWeaponAim(
    float* gunPitch,
    float* gunYaw);

// KISAK_SP_VR_FIXED_SCOPED_TURRET_CONTROLS_V3_HEADER
// The fixed Barrett's binocular reticle represents the completed HMD-center
// camera ray, not the freely moving right controller used by ordinary
// mounted guns.
bool VR_GetFixedScopedTurretAim(
    float* gunPitch,
    float* gunYaw);

// Returns true whenever the raw OpenXR left-stick state is available,
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
// Returns the raw right-trigger state while the live mounted-weapon
// controller pose is available.  This does not depend on a viewmodel.
bool VR_GetRightControllerMountedWeaponTrigger(
    bool* attackPressed);

// Returns left-thumbstick movement rotated by the horizontal HMD yaw.
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

// Consumes one latched 45-degree right-stick snap turn.
// Positive CoD yaw turns left, so right-stick right returns -45 degrees.
bool VR_ConsumeSnapTurn(
    float* yawDeltaDegrees);

// Returns the first Touch gameplay-control set:
// a left-squeeze two-hand weapon pose near the HMD sight line = ADS,
// right A = jump, left X = pickup/activate, and left trigger = reload.
bool VR_GetBasicGameplayButtons(
    bool* adsHeld,
    bool* jumpHeld,
    bool* useHeld,
    bool* reloadHeld);

// Returns the next Touch gameplay-control set:
// left-stick click = sprint, right-stick click = melee,
// right B = tap crouch/stand or hold prone/stand.
bool VR_GetLocomotionCombatButtons(
    bool* sprintHeld,
    bool* meleeHeld,
    bool* stanceHeld);

// Returns weapon-utility controls:
// right grip = held offhand input, left Y = held utility input.
bool VR_GetWeaponUtilityButtons(
    bool* rightGripHeld,
    bool* leftYHeld);


// Applies a short vibration pulse to the right-hand OpenXR controller.
// Intended for confirmed local firearm events.
bool VR_ApplyRightControllerWeaponHaptic(
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
