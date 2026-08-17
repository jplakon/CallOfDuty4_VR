#include "vr/vr_openxr.h"
#include "vr/vr_input_bindings.h"
#include "vr/vr_calibration.h"
#include "vr/vr_gestures.h"
#include "vr/vr_hud_layout.h"
#include "vr/vr_interactions.h"
#include "vr/vr_openvr_input.h"
#include "vr/vr_openxr_profiles.h"
#include "vr/vr_prompt_labels.h"
#include "vr/vr_weapon_calibration.h"
#include "vr/vr_weapon_profiles.h"
#include "client/client.h"

void __cdecl UI_MouseEvent(int localClientNum, int x, int y);
#include "vr/vr_d3d9_capture.h"
#include "vr/vr_d3d9ex_interop_probe.h"
#include "gfx_d3d/r_init.h"

#include "qcommon/qcommon.h"
#include "win32/win_crash_diagnostics.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openvr.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <mutex>

void VR_ProcessCalibrationRequest(XrTime predictedDisplayTime);
void VR_ProcessHudEditorRequest();
void VR_ProcessWeaponCalibrationRequest();

namespace
{
using Microsoft::WRL::ComPtr;
namespace VrInput = kisak::vr::input;
namespace VrCalibration = kisak::vr::calibration;
namespace VrGestures = kisak::vr::gestures;
namespace VrHud = kisak::vr::hud;
namespace VrInteractions = kisak::vr::interactions;
namespace VrPrompts = kisak::vr::prompts;
namespace VrWeaponProfiles = kisak::vr::weapon_profiles;

constexpr XrViewConfigurationType kViewConfiguration =
    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

XrInstance g_vrInstance = XR_NULL_HANDLE;
XrSystemId g_vrSystemId = XR_NULL_SYSTEM_ID;
XrSession g_vrSession = XR_NULL_HANDLE;
XrSpace g_vrAppSpace = XR_NULL_HANDLE;
XrSpace g_vrCalibrationFloorSpace = XR_NULL_HANDLE;
bool g_vrCalibrationFloorSpaceAvailable = false;

// KISAK_SP_VR_OPENVR_FALLBACK_V49
// COD4 is a 32-bit process. Some current SteamVR/Pimax installations expose
// only a 64-bit OpenXR runtime, but SteamVR still supplies the architecture-
// matched 32-bit OpenVR client. Keep the working OpenXR path primary and use
// OpenVR only when requested or when OpenXR cannot enumerate a runtime.
enum class VrRuntimeBackend : std::uint32_t
{
    None = 0u,
    OpenXr,
    OpenVr,
};

VrRuntimeBackend g_vrRuntimeBackend =
    VrRuntimeBackend::None;

vr::IVRSystem* g_vrOpenVrSystem = nullptr;
vr::IVRCompositor* g_vrOpenVrCompositor = nullptr;
vr::IVRRenderModels* g_vrOpenVrRenderModels = nullptr;

std::array<
    vr::TrackedDevicePose_t,
    vr::k_unMaxTrackedDeviceCount>
    g_vrOpenVrRenderPoses = {};

bool g_vrOpenVrInitialized = false;
bool g_vrOpenVrLoggedFirstPose = false;
bool g_vrOpenVrLoggedFirstSubmit = false;

std::array<VrInput::OpenVrHandState, 2>
    g_vrOpenVrHands = {};

std::array<bool, 2>
    g_vrOpenVrLoggedController = {};

// KISAK_SP_VR_OPENVR_MISSION_SELECTOR_V79
// SteamVR's legacy Oculus state aliases thumbrest contact to joystick touch.
// Require a neutral-entry gesture for the default mission chords so walking
// plus turning cannot fire night vision or weapon-slot shortcuts.
VrInput::OpenVrMissionSelectorState
    g_vrOpenVrMissionSelector = {};
bool g_vrOpenVrLoggedMissionSelector = false;

// KISAK_SP_VR_OPENXR_MISSION_SELECTOR_V103
// Reuse the V79 neutral-entry state machine for native OpenXR controllers.
// Quest thumbrest contact must not suppress locomotion or complete a mission
// shortcut unless contact began while both physical primary axes were neutral.
VrInput::OpenVrMissionSelectorState
    g_vrOpenXrMissionSelector = {};
bool g_vrOpenXrLoggedMissionSelector = false;

// KISAK_SP_VR_OPENVR_SEMANTIC_CONTROLLER_POSES_V77
// OpenVR's tracked-device pose is the driver's controller origin, not a
// portable grip or pointing pose. Cache the render-model components SteamVR
// publishes for those semantic coordinate systems so Oculus Touch, Index,
// PSVR2, and other controllers do not need headset-specific angle offsets.
struct VrOpenVrControllerPoseComponents
{
    vr::TrackedDeviceIndex_t deviceIndex =
        vr::k_unTrackedDeviceIndexInvalid;
    bool resolved = false;
    bool stateFailureLogged = false;
    bool indexHandModelBasisLogged = false;
    std::array<char, 256> renderModelName = {};
    const char* gripComponent = nullptr;
    const char* palmComponent = nullptr;
    const char* aimComponent = nullptr;
};

std::array<VrOpenVrControllerPoseComponents, 2>
    g_vrOpenVrControllerPoseComponents = {};

std::array<char, 256> g_vrCompatibilityRuntimeName = {};
std::array<char, 256> g_vrCompatibilityHeadsetName = {};
std::array<std::array<char, 512>, 2>
    g_vrCompatibilityControllerProfiles = {};

constexpr std::uint32_t kVrControllerCount = 2u;

enum VrControllerHand : std::uint32_t
{
    VR_CONTROLLER_LEFT = 0u,
    VR_CONTROLLER_RIGHT = 1u,
};

XrActionSet g_vrControllerActionSet = XR_NULL_HANDLE;

XrAction g_vrGripPoseAction = XR_NULL_HANDLE;
XrAction g_vrPalmPoseAction = XR_NULL_HANDLE;
XrAction g_vrAimPoseAction = XR_NULL_HANDLE;
XrAction g_vrHapticOutputAction = XR_NULL_HANDLE;
XrAction g_vrNightVisionGestureGripAction = XR_NULL_HANDLE;

using VrInputTermActions = std::array<
    std::array<
        std::array<XrAction, VrInput::kMaxBindingSources>,
        2>,
    VrInput::kActionCount>;

using VrInputTermLatchState = std::array<
    std::array<
        std::array<bool, VrInput::kMaxBindingSources>,
        2>,
    VrInput::kActionCount>;

VrInputTermActions g_vrInputTermActions = {};
VrInputTermLatchState g_vrDirectionalTermLatched = {};

std::array<bool, VrInput::kActionCount>
    g_vrInputActionPreviousHeld = {};

bool g_vrMissionShortcutArmed = true;

// KISAK_SP_VR_NIGHT_VISION_VISOR_GESTURE_V80
// Both runtime backends feed the physical left grip and its tracked pose into
// one head-relative state machine. The detector consumes that grip only after
// a crown/visor press has armed, so ordinary support-grip use is unchanged.
// KISAK_SP_VR_NIGHT_VISION_VISOR_FOREGRIP_GUARD_V81
// Raising arms only inside a face-close ellipsoid; the broader visor region
// remains a forgiving destination for the crown-to-visor lowering motion.
VrGestures::NightVisionVisorState
    g_vrNightVisionVisorGesture = {};
bool g_vrLoggedNightVisionVisorGesture = false;

std::array<bool, VrInput::kOpenXrProfileCount>
    g_vrEnabledOpenXrProfiles = {};

std::array<XrPath, kVrControllerCount>
    g_vrControllerHandPaths = {
        XR_NULL_PATH,
        XR_NULL_PATH,
    };

std::array<XrSpace, kVrControllerCount>
    g_vrControllerGripSpaces = {
        XR_NULL_HANDLE,
        XR_NULL_HANDLE,
    };

std::array<XrSpace, kVrControllerCount>
    g_vrControllerPalmSpaces = {
        XR_NULL_HANDLE,
        XR_NULL_HANDLE,
    };

std::array<XrSpace, kVrControllerCount>
    g_vrControllerAimSpaces = {
        XR_NULL_HANDLE,
        XR_NULL_HANDLE,
    };

bool g_vrControllerActionsCreated = false;
bool g_vrControllerActionsAttached = false;
bool g_vrControllerSpacesCreated = false;
bool g_vrPalmPoseExtensionEnabled = false;

std::array<bool, kVrControllerCount>
    g_vrLoggedFirstGripPose = {};

std::array<bool, kVrControllerCount>
    g_vrLoggedFirstPalmPose = {};

std::array<bool, kVrControllerCount>
    g_vrLoggedFirstAimPose = {};

std::array<bool, kVrControllerCount>
    g_vrControllerTriggerPressed = {};

std::array<bool, kVrControllerCount>
    g_vrControllerSqueezePressed = {};

std::uint64_t g_vrControllerDiagnosticFrame = 0u;

struct VrControllerRenderPose
{
    bool gripValid = false;
    bool palmValid = false;
    bool aimValid = false;
    XrPosef gripPose = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
    };
    XrPosef palmPose = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
    };
    XrPosef aimPose = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
    };
};

std::array<
    VrControllerRenderPose,
    kVrControllerCount>
    g_vrControllerRenderPoses = {};

std::mutex g_vrWeaponControllerPoseMutex;

bool g_vrRightControllerWeaponPoseValid = false;
bool g_vrRightControllerWeaponFilterValid = false;

bool g_vrLeftControllerForegripPoseValid = false;
bool g_vrLeftControllerForegripPressed = false;
bool g_vrLeftControllerSqueezePressedRaw = false;
bool g_vrSupportGripBindingWasHeld = false;
bool g_vrSupportGripToggleLatched = false;
bool g_vrObjectGripBindingWasHeld = false;
bool g_vrObjectGripToggleLatched = false;

float g_vrLeftControllerForegripPosition[3] = {};

// KISAK_SP_VR_MANUAL_GRENADE_THROW_V53
// Linear velocity is stored in the same HMD-local CoD basis as the tracked
// grip position, in game units per second.  The gameplay thread transforms it
// through the current camera axis when grip release is observed.
bool g_vrLeftControllerLinearVelocityValid = false;
float g_vrLeftControllerLinearVelocity[3] = {};
bool g_vrLeftControllerPositionSampleValid = false;
std::uint32_t g_vrLeftControllerPositionSampleMilliseconds = 0u;
float g_vrLeftControllerPreviousPosition[3] = {};

float g_vrLeftControllerForegripAxis[3][3] = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
};

// KISAK_SP_VR_TRACKED_HANDS_V24_DIRECT_OPENXR_GRIP_QUATERNION
// Keep the normalized OpenXR grip orientation as a quaternion until cgame
// consumes it.  The compositor proxy renders this same runtime pose directly;
// carrying four scalar components avoids reconstructing the free hand from the
// legacy shared foregrip matrix, which V23 diagnostics proved can arrive
// scaled/sheared even though the underlying OpenXR pose is a rigid rotation.
XrQuaternionf g_vrLeftControllerGripOrientationHeadLocalOpenXr = {
    0.0f,
    0.0f,
    0.0f,
    1.0f,
};

// KISAK_SP_VR_TRACKED_HANDS_V25_OPENXR_PALM_SURFACE_POSE
// XR_EXT_palm_pose (promoted to grip_surface/pose in OpenXR 1.1) is the
// controller-specific hand-registration pose.  Keep it separate from the
// legacy grip pose because weapons, two-hand aiming, and manual reloads still
// require the physical controller grip while the standalone glove requires
// the user's palm centroid and palm-oriented frame.
bool g_vrLeftControllerPalmPoseValid = false;
float g_vrLeftControllerPalmPosition[3] = {};
XrQuaternionf g_vrLeftControllerPalmOrientationHeadLocalOpenXr = {
    0.0f,
    0.0f,
    0.0f,
    1.0f,
};

float g_vrTwoHandWeaponBlend = 0.0f;
bool g_vrTwoHandWeaponTargetActive = false;

bool g_vrPoseFocusAimPoseHeld = false;
std::uint32_t g_vrPoseFocusAimEngageFrames = 0u;
std::uint32_t g_vrPoseFocusAimReleaseFrames = 0u;

XrVector3f g_vrRightControllerFilteredGripPosition = {
    0.0f,
    0.0f,
    0.0f,
};

bool g_vrRightControllerLinearVelocityValid = false;
float g_vrRightControllerLinearVelocity[3] = {};
bool g_vrRightControllerPositionSampleValid = false;
std::uint32_t g_vrRightControllerPositionSampleMilliseconds = 0u;
float g_vrRightControllerPreviousPosition[3] = {};
bool g_vrPhysicalMeleeArmed = true;
std::uint32_t g_vrPhysicalMeleePulseUntilMilliseconds = 0u;
std::uint32_t g_vrLastPhysicalMeleeMilliseconds = 0u;

XrQuaternionf g_vrRightControllerFilteredAimOrientation = {
    0.0f,
    0.0f,
    0.0f,
    1.0f,
};

float g_vrRightControllerWeaponPosition[3] = {};
float g_vrRightControllerWeaponAxis[3][3] = {};

// KISAK_SP_VR_TRACKED_HANDS_V1
// The weapon uses OpenXR aim-space orientation, while a visible glove must
// use grip-space orientation.  Both remain HMD-local until cgame combines
// them with the current camera basis.
float g_vrRightControllerGripAxis[3][3] = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
};

// KISAK_SP_VR_MOUNTED_TURRET_AIM_V1
// The raw controller axis above is HMD-local.  Snapshot the completed
// camera world axis so mounted weapons can recover live controller aim even
// when CoD4 suppresses the ordinary first-person viewmodel.
float g_vrMountedWeaponCameraAxisWorld[3][3] = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
};
bool g_vrMountedWeaponCameraAxisWorldValid = false;

// The final viewmodel grip-tag alignment must target the calibrated grip,
// not the raw physical grip. Otherwise its correction exactly cancels every
// configured forward/left/up weapon offset.
float g_vrRightControllerWeaponCalibrationCameraLocal[3] = {};
bool g_vrRightControllerWeaponCalibrationValid = false;
bool g_vrReportedRightControllerWeaponCalibration = false;

bool g_vrLoggedRightControllerWeaponCalibration = false;
bool g_vrLoggedRightControllerWeaponApply = false;

float g_vrRightControllerFinalWeaponForward[3] = {
    1.0f,
    0.0f,
    0.0f,
};

// KISAK_VR_SCOPE_BALLISTIC_ALIGNMENT_V1
// Final rendered weapon basis relative to the current HMD camera.  The
// physical scope consumes this exact post-attachment, post-two-hand basis.
float g_vrRightControllerFinalWeaponAxisCameraLocal[3][3] = {};
bool g_vrRightControllerFinalWeaponAxisCameraLocalValid = false;

// KISAK_VR_RIFLE_ATTACHED_SCOPE_V1
// The viewmodel publishes its optic position relative to the tracked grip
// in the final rendered weapon basis.  The same pose also supplies the
// authoritative world-space scope ray used for ballistic convergence.
float g_vrPhysicalSniperScopeOffsetWeaponLocal[3] = {};
bool g_vrPhysicalSniperScopeOffsetWeaponLocalValid = false;

float g_vrPhysicalSniperScopeOriginWorld[3] = {};
float g_vrPhysicalSniperScopeForwardWorld[3] = {
    1.0f,
    0.0f,
    0.0f,
};
float g_vrPhysicalSniperScopeAxisWorld[3][3] = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
};
bool g_vrPhysicalSniperScopePoseWorldValid = false;

bool g_vrRightControllerFinalWeaponAimValid = false;

float g_vrRightControllerFinalWeaponMuzzleWorld[3] = {};
bool g_vrRightControllerFinalWeaponMuzzleValid = false;
bool g_vrRightControllerFinalWeaponMuzzleBlocked = false;

bool g_vrRightControllerAttackPressed = false;

bool g_vrLoggedRightControllerUsercmdAim = false;
bool g_vrLoggedRightControllerAttackInjection = false;

struct VrControllerProxyVertex
{
    float position[4];
    float color[4];
};

constexpr std::uint32_t
    kVrMaximumControllerProxyVertices = 48u;

ComPtr<ID3D11VertexShader>
    g_vrControllerProxyVertexShader;

ComPtr<ID3D11PixelShader>
    g_vrControllerProxyPixelShader;

ComPtr<ID3D11InputLayout>
    g_vrControllerProxyInputLayout;

ComPtr<ID3D11Buffer>
    g_vrControllerProxyVertexBuffer;

bool g_vrControllerProxyResourcesReady = false;
bool g_vrLoggedFirstControllerProxyDraw = false;

XrSessionState g_vrSessionState = XR_SESSION_STATE_UNKNOWN;
XrEnvironmentBlendMode g_vrBlendMode =
    XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

bool g_vrInitialized = false;
bool g_vrSessionRunning = false;
bool g_vrExitRequested = false;

ComPtr<ID3D11Device> g_vrD3dDevice;
ComPtr<ID3D11DeviceContext> g_vrD3dContext;

ComPtr<ID3D11VertexShader> g_vrTestVertexShader;
ComPtr<ID3D11PixelShader> g_vrTestPixelShader;
ComPtr<ID3D11InputLayout> g_vrTestInputLayout;
ComPtr<ID3D11Buffer> g_vrTestVertexBuffer;
ComPtr<ID3D11Buffer> g_vrTestIndexBuffer;
ComPtr<ID3D11Buffer> g_vrGridVertexBuffer;
ComPtr<ID3D11Buffer> g_vrTestConstantBuffer;
ComPtr<ID3D11RasterizerState> g_vrTestRasterizerState;
ComPtr<ID3D11DepthStencilState> g_vrTestDepthStencilState;

UINT g_vrTestIndexCount = 0;
UINT g_vrGridVertexCount = 0;
bool g_vrLoggedFirstTestFrame = false;

ComPtr<ID3D11VertexShader> g_vrBlitVertexShader;
ComPtr<ID3D11PixelShader> g_vrBlitPixelShader;
ComPtr<ID3D11PixelShader> g_vrScopePixelShader;
ComPtr<ID3D11Buffer> g_vrScopeConstantBuffer;
ComPtr<ID3D11Buffer> g_vrCompositorConstantBuffer;

// KISAK_SP_VR_NATIVE_FSR_COMPOSITOR_V1
ComPtr<ID3D11PixelShader> g_vrFsrEasuPixelShader;
ComPtr<ID3D11PixelShader> g_vrFsrRcasPixelShader;
ComPtr<ID3D11Buffer> g_vrFsrConstantBuffer;
ComPtr<ID3D11Texture2D> g_vrFsrIntermediateTexture;
ComPtr<ID3D11RenderTargetView> g_vrFsrIntermediateTarget;
ComPtr<ID3D11ShaderResourceView> g_vrFsrIntermediateView;

ComPtr<ID3D11InputLayout> g_vrBlitInputLayout;
std::array<ComPtr<ID3D11Buffer>, 2>
    g_vrBlitVertexBuffers;

// KISAK_SP_VR_EYE_LOCAL_MENU_AND_CURSOR_V83
// V82 authors ordinary UI in one-eye pixels.  Frontend/start menus therefore
// use the completed left-eye source instead of squeezing the full side-by-side
// frame (and both stereo copies) into each headset eye.
ComPtr<ID3D11Buffer> g_vrMenuBlitVertexBuffer;

// Active SP pause UI is painted into the right half of the SBS
// backbuffer. Sample only that completed eye for a clean mono screen.
ComPtr<ID3D11Buffer> g_vrPauseMenuBlitVertexBuffer;

// KISAK_SP_VR_EYE_LOCAL_SHARED_MODAL_V88
// Centered script modals remain a one-pass shared command list so they cannot
// appear twice, but V82 now authors that list in one-eye coordinates. Sample
// the completed left-eye region instead of beta.13's obsolete center crop.
ComPtr<ID3D11Buffer> g_vrCenteredModalBlitVertexBuffer;

ComPtr<ID3D11SamplerState> g_vrBlitSampler;
bool g_vrLoggedMenuComfortScreen = false;

// KISAK_SP_VR_CANONICAL_MENU_ASPECT_V101
// COD4 authors frontend, pause, and modal UI on its original 640x480 canvas.
// The one-eye capture can be taller than 4:3, so preserving the capture's
// pixel aspect leaves that already-scaled menu visibly squeezed. Present the
// complete eye-local source through the canonical canvas aspect to undo that
// authoring stretch without changing gameplay or HUD projection.
constexpr float kVrCanonicalMenuAspect = 4.0f / 3.0f;
bool g_vrLoggedCanonicalMenuAspect = false;

// KISAK_SP_VR_COMPOSITOR_BRIGHTNESS_V1
float g_vrCompositorBrightness = 1.0f;

bool g_vrFsrEnabled = true;
bool g_vrFsrShadersAvailable = false;
float g_vrFsrSharpness = 0.60f;
float g_vrOutputScale = 1.0f;
bool g_vrLoggedFirstFsrFrame = false;
bool g_vrLoggedFsrFallback = false;

std::mutex g_vrScopeStateMutex;
bool g_vrScopeActive = false;
float g_vrScopeAdsFraction = 0.0f;
float g_vrScopeAdsFovDegrees = 65.0f;
bool g_vrLoggedFirstPhysicalScopeDraw = false;

// KISAK_SP_VR_FIXED_SCOPED_TURRET_VIEW_FIX_V1
bool g_vrFixedScopedTurretActive = false;
bool g_vrLoggedFixedScopedTurretCompositor = false;

// KISAK_SP_VR_FIXED_SCOPED_TURRET_RUNTIME_V4
float g_vrFixedScopedTurretZoomFovDegrees = 20.0f;
float g_vrFixedScopedTurretMaximumZoomFovDegrees = 20.0f;
bool g_vrLoggedFixedScopedTurretVisibleZoom = false;
bool g_vrLoggedFixedScopedTurretFsrBypass = false;

float g_vrScopeForwardCalibrationMeters = 0.0f;
float g_vrScopeLeftCalibrationMeters = 0.0f;
float g_vrScopeUpCalibrationMeters = 0.0f;
float g_vrScopeLensRadiusMeters = 0.032f;
// KISAK_VR_DEDICATED_SCOPE_CAMERA_V2
int g_vrScopeCaptureSizePixels = 1024;
bool g_vrLoggedDedicatedScopeLayout = false;
bool g_vrLoggedDedicatedScopeLayoutMissing = false;
bool g_vrLoggedDedicatedScopeSample = false;

constexpr std::uint32_t kVrStereoEyeCount = 2u;

ComPtr<ID3D11Texture2D>
    g_vrCapturedStereoTexture;

ComPtr<ID3D11ShaderResourceView>
    g_vrCapturedStereoView;

// KISAK_SP_VR_CAPTURE_COLOR_TRANSFER_V78
// COD4's D3D9 backbuffer contains display-referred sRGB values, although
// D3D9Ex exposes the shared resource to D3D11 as plain BGRA UNORM. Keep one
// typeless copy per producer slot so OpenXR can sample through an sRGB SRV,
// while legacy OpenVR can preserve the encoded bytes through a UNORM SRV for
// its compatible 8-bit Auto/gamma submission path.
std::array<
    ComPtr<ID3D11Texture2D>,
    kVrD3D9SharedFrameSlotCount>
    g_vrCapturedSharedTextures = {};

std::array<
    ComPtr<ID3D11ShaderResourceView>,
    kVrD3D9SharedFrameSlotCount>
    g_vrCapturedSharedViews = {};

bool g_vrLoggedCaptureColorTransfer = false;

std::uint32_t g_vrCapturedStereoWidth = 0u;
std::uint32_t g_vrCapturedStereoHeight = 0u;
std::uint64_t g_vrUploadedStereoSerial = 0u;
std::vector<std::uint8_t>
    g_vrCapturedStereoUploadPixels;
bool g_vrLoggedFirstStereoUpload = false;
bool g_vrLoggedCaptureBufferHandoff = false;

// KISAK_SP_VR_GPU_SHARED_BRIDGE_V1
struct VrRetiredSharedFrame
{
    bool active = false;
    std::uint32_t slotIndex = 0u;
    std::uint64_t serial = 0u;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11Query> completionQuery;
};

std::array<
    VrRetiredSharedFrame,
    kVrD3D9SharedFrameSlotCount>
    g_vrRetiredSharedFrames = {};

bool g_vrCurrentSharedFrameActive = false;
std::uint32_t g_vrCurrentSharedSlot = 0u;
std::uint64_t g_vrCurrentSharedGeneration = 0u;
std::uint64_t g_vrCurrentSharedSerial = 0u;
bool g_vrLoggedFirstSharedFrameOpen = false;
bool g_vrLoggedSharedConsumerFailure = false;

std::array<XrView, kVrStereoEyeCount>
    g_vrPublishedRenderViews = {};

std::array<XrView, kVrStereoEyeCount>
    g_vrCapturedStereoViews = {};

bool g_vrPublishedRenderViewsValid = false;
bool g_vrCapturedStereoViewsValid = false;
bool g_vrLoggedCapturedPoseMatch = false;
bool g_vrLoggedCapturedPoseMiss = false;
std::mutex g_vrPublishedRenderViewsMutex;

// KISAK_SP_VR_CAPTURE_POSE_METADATA_V32
constexpr std::size_t kVrRenderPoseHistoryCount = 64u;

struct VrRenderPoseHistoryEntry
{
    bool valid = false;
    std::uint64_t renderFrameId = 0u;
    std::uint64_t recordedNanoseconds = 0u;
    std::array<XrView, kVrStereoEyeCount> views = {};
};

std::array<
    VrRenderPoseHistoryEntry,
    kVrRenderPoseHistoryCount>
    g_vrRenderPoseHistory = {};

std::mutex g_vrRenderPoseHistoryMutex;
std::size_t g_vrRenderPoseHistoryWriteIndex = 0u;
std::uint64_t g_vrPublishedRenderPoseNanoseconds = 0u;
std::uint64_t g_vrCapturedRenderPoseNanoseconds = 0u;
VrD3D9FrameMetadata g_vrCapturedStereoMetadata = {};
bool g_vrCapturedStereoPoseMatched = false;

std::mutex g_vrHeadOrientationMutex;

constexpr float kVrGameUnitsPerMeter =
    39.37007874015748f;

XrVector3f g_vrHeadPositionOrigin = {};
XrVector3f g_vrLatestHeadPosition = {};
XrQuaternionf g_vrLatestHeadOrientation = {
    0.0f,
    0.0f,
    0.0f,
    1.0f,
};
float g_vrHeadPositionLocal[3] = {};
bool g_vrHeadPositionOriginValid = false;
bool g_vrLatestHeadPositionValid = false;
bool g_vrLatestHeadOrientationValid = false;
bool g_vrHeadPositionValid = false;
bool g_vrLoggedFirstPositionApply = false;

float g_vrLiveTargetEyeHeightInches =
    VrCalibration::kNativeStandingEyeHeightInches;
bool g_vrLiveTargetEyeHeightValid = false;
std::string g_vrLastCalibrationRequestId;
std::uint32_t g_vrLastCalibrationPollMilliseconds = 0u;
std::atomic<bool> g_vrLoggedFirstHeightApply{false};

std::mutex g_vrHudEditorMutex;
VrHud::Layout g_vrHudEditorLayout = {};
VrHud::Layout g_vrHudEditorOriginalLayout = {};
bool g_vrHudLayoutInitialized = false;
bool g_vrHudEditorActive = false;
bool g_vrHudEditorDragging = false;
bool g_vrHudEditorPointerValid = false;
bool g_vrHudEditorSnapEnabled = true;
bool g_vrHudEditorTriggerWasHeld = false;
bool g_vrHudEditorConfirmWasHeld = false;
bool g_vrHudEditorBackWasHeld = false;
bool g_vrHudEditorScaleArmed = true;
// KISAK_SP_VR_HUD_EDITOR_RECOVERY_V69
// These semantic actions retain user remapping and work through both the
// OpenXR and OpenVR adapters. Keyboard latches provide the same recovery
// operations when a controller binding is unavailable.
bool g_vrHudEditorPreviousWasHeld = false;
bool g_vrHudEditorNextWasHeld = false;
bool g_vrHudEditorCenterWasHeld = false;
bool g_vrHudEditorResetWasHeld = false;
bool g_vrHudEditorKeyboardTabWasHeld = false;
bool g_vrHudEditorKeyboardCenterWasHeld = false;
bool g_vrHudEditorKeyboardResetWasHeld = false;
VrHud::Point g_vrHudEditorPointer = {};
VrHud::Element g_vrHudEditorSelected =
    VrHud::Element::AmmoEquipment;
std::string g_vrHudEditorRequestId;
std::string g_vrLastHudEditorRequestId;
std::uint32_t g_vrLastHudEditorPollMilliseconds = 0u;
std::uint64_t g_vrHudLayoutRevision = 1u;

std::mutex g_vrWeaponProfilesMutex;
VrWeaponProfiles::Document g_vrWeaponProfiles =
    VrWeaponProfiles::DefaultDocument();
std::string g_vrWeaponProfilesRevision =
    VrWeaponProfiles::DocumentRevision(g_vrWeaponProfiles);
std::string g_vrWeaponProfilesLoadError;
bool g_vrWeaponProfilesLoaded = false;
std::string g_vrLastWeaponCalibrationRequestId;
std::uint32_t g_vrLastWeaponCalibrationPollMilliseconds = 0u;
std::atomic<std::uint32_t> g_vrWeaponStatusHoldUntilMilliseconds{0u};
std::string g_vrLastWeaponStatusSignature;

struct VrWeaponAttachmentBaseline
{
    bool valid = false;
    std::string weaponId;
    float position[3] = {};
    float axis[3][3] = {};
};

std::array<VrWeaponAttachmentBaseline, 128>
    g_vrWeaponAttachmentBaselines = {};
int g_vrActiveCalibrationWeaponIndex = 0;
std::string g_vrActiveCalibrationWeaponId;
std::string g_vrActiveCalibrationWeaponName;
VrWeaponProfiles::EffectiveCalibration
    g_vrActiveEffectiveWeaponCalibration = {};
float g_vrActiveWeaponBaseAttachmentAxis[3][3] = {};
float g_vrActiveWeaponControllerAxis[3][3] = {};
bool g_vrActiveWeaponCapturePoseValid = false;

constexpr float kVrDefaultHalfIpdGameUnits =
    0.032f * kVrGameUnitsPerMeter;

float g_vrHalfIpdGameUnits =
    kVrDefaultHalfIpdGameUnits;

bool g_vrLoggedFirstStereoEyeOffset = false;



float g_vrHeadOrientationAxis[3][3] = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
};

XrQuaternionf g_vrHeadBaseOrientation = {
    0.0f,
    0.0f,
    0.0f,
    1.0f,
};

bool g_vrHeadBaseOrientationValid = false;
bool g_vrHeadOrientationValid = false;
bool g_vrLoggedLevelSafeHeadBase = false;
bool g_vrLoggedFirstHeadPose = false;
bool g_vrLoggedFirstCameraApply = false;

// Horizontal physical turning already transferred into clients[0]
// body yaw. Future HMD publications remove this amount from the
// camera-local pose so body alignment does not rotate the visible view.
float g_vrTransferredBodyYawDegrees = 0.0f;

// Translation needs its own yaw history. A direction/level-only recenter
// clears the rotational baseline, but preserving this value keeps the
// existing room-scale offset in the same world position after body-yaw
// transfer. Full recenter clears both histories for beta.10 compatibility.
float g_vrHeadPositionBodyYawDegrees = 0.0f;

float g_vrLeftThumbstick[2] = {};
bool g_vrLeftThumbstickValid = false;

float g_vrRightThumbstickX = 0.0f;
float g_vrRightThumbstickY = 0.0f;
bool g_vrRightThumbstickValid = false;

XrVector2f g_vrMenuNavigationAxis = {};
bool g_vrMenuNavigationAxisValid = false;
XrVector2f g_vrScopeZoomAxis = {};
bool g_vrScopeZoomAxisValid = false;

bool g_vrSnapTurnArmed = true;
bool g_vrRightThumbrestTouched = false;

// KISAK_SP_VR_SMOOTH_TURN_OPTION_V50
enum class VrTurnMode
{
    Snap,
    Smooth,
};

VrTurnMode g_vrTurnMode =
    VrTurnMode::Snap;

float g_vrSmoothTurnSpeedDegreesPerSecond =
    120.0f;

bool g_vrTurnSettingsLoaded = false;

// KISAK_SP_VR_CONFIGURATOR_V56
// Settings introduced by the standalone configurator remain environment
// variables so the game needs no writable in-game menu state. The launcher
// loads release defaults first and user overrides second before process start.
enum class VrMovementDirection
{
    Head,
    Body,
    OffHand,
    WeaponHand,
    PhysicalLeft,
    PhysicalRight,
};

enum class VrMeasurementUnitSystem
{
    Metric,
    Imperial,
};

const char* VR_MeasurementUnitSystemId(
    const VrMeasurementUnitSystem units)
{
    return units == VrMeasurementUnitSystem::Imperial
        ? "imperial"
        : "metric";
}

float VR_DisplayInches(
    const float inches,
    const VrMeasurementUnitSystem units)
{
    return units == VrMeasurementUnitSystem::Metric
        ? inches * 2.54f
        : inches;
}

float VR_DisplayMeters(
    const float meters,
    const VrMeasurementUnitSystem units)
{
    return units == VrMeasurementUnitSystem::Metric
        ? meters * 100.0f
        : meters * (100.0f / 2.54f);
}

const char* VR_DisplayLengthUnit(
    const VrMeasurementUnitSystem units)
{
    return units == VrMeasurementUnitSystem::Metric ? "cm" : "in";
}

struct VrConfiguratorSettings
{
    VrMeasurementUnitSystem measurementUnits =
        VrMeasurementUnitSystem::Metric;
    VrInteractions::DominantHand dominantHand =
        VrInteractions::DominantHand::Right;
    float snapTurnAngleDegrees = 45.0f;
    float turnDeadzone = 0.25f;
    VrMovementDirection movementDirection =
        VrMovementDirection::Head;
    float movementDeadzone = 0.18f;

    VrCalibration::PlayMode playMode =
        VrCalibration::PlayMode::Standing;
    float standingEyeHeightInches =
        VrCalibration::kNativeStandingEyeHeightInches;
    float seatedEyeHeightInches =
        VrCalibration::kNativeStandingEyeHeightInches;
    VrCalibration::RecenterMode firstGameplayRecenterMode =
        VrCalibration::RecenterMode::Full;

    VrHud::Layout hudLayout = {};

    float weaponOffset[3] = {};
    float weaponAngles[3] = {};
    bool weaponProfilesEnabled = true;
    float weaponPositionResponse = 0.45f;
    float weaponOrientationResponse = 0.55f;
    float twoHandStrength = 1.0f;

    float beltForwardOffset = 0.0f;
    float beltHeight = -28.0f;
    float beltHipDistance = 13.0f;
    float beltGrabRadius = 11.0f;
    float reloadInsertRadius = 6.5f;
    float reloadPullDistance = 8.0f;
    bool manualReload = true;
    bool manualGrenades = true;
    VrInteractions::ReloadEjectMode reloadEjectMode =
        VrInteractions::ReloadEjectMode::Button;
    VrInteractions::ReloadInsertMode reloadInsertMode =
        VrInteractions::ReloadInsertMode::Release;
    VrInteractions::MagazineHip magazineHip =
        VrInteractions::MagazineHip::OffHand;
    VrInteractions::GrenadeBeltLayout grenadeBeltLayout =
        VrInteractions::GrenadeBeltLayout::Handed;
    VrInteractions::SupportGripMode supportGripMode =
        VrInteractions::SupportGripMode::Hold;
    VrInteractions::ObjectGripMode objectGripMode =
        VrInteractions::ObjectGripMode::Hold;
    VrInteractions::MeleeMode meleeMode =
        VrInteractions::MeleeMode::Both;
    float meleeSpeed = 95.0f;
    float meleeForwardBias = 0.55f;
    std::uint32_t meleeCooldownMilliseconds = 550u;
    bool hapticsEnabled = true;
    float hapticStrength = 1.0f;
    bool muzzleObstruction = true;

    std::array<
        std::array<VrInput::Binding, 2>,
        VrInput::kActionCount> bindings = {};
};

float VR_ReadConfiguratorFloat(
    const char* name,
    const float defaultValue,
    const float minimumValue,
    const float maximumValue)
{
    const char* requestedValue = std::getenv(name);

    if (requestedValue == nullptr || requestedValue[0] == '\0')
    {
        return defaultValue;
    }

    char* parseEnd = nullptr;
    const float parsedValue = std::strtof(requestedValue, &parseEnd);
    if (parseEnd == requestedValue || parseEnd == nullptr ||
        parseEnd[0] != '\0' || !std::isfinite(parsedValue) ||
        parsedValue < minimumValue || parsedValue > maximumValue)
    {
        Com_PrintWarning(
            0,
            "[VR][CONFIG] Ignoring invalid %s='%s'; using %.3f. "
            "Valid range is %.3f through %.3f.\n",
            name,
            requestedValue,
            defaultValue,
            minimumValue,
            maximumValue);
        return defaultValue;
    }

    return parsedValue;
}

bool VR_ReadConfiguratorToggle(
    const char* const name,
    const bool defaultValue)
{
    const char* const requestedValue = std::getenv(name);
    if (requestedValue == nullptr || requestedValue[0] == '\0')
    {
        return defaultValue;
    }

    if (std::strcmp(requestedValue, "0") == 0)
    {
        return false;
    }
    if (std::strcmp(requestedValue, "1") == 0)
    {
        return true;
    }

    Com_PrintWarning(
        0,
        "[VR][CONFIG] Ignoring invalid %s='%s'; using %d. "
        "Valid values are 0 and 1.\n",
        name,
        requestedValue,
        defaultValue ? 1 : 0);
    return defaultValue;
}

VrCalibration::RecenterMode VR_ReadFirstGameplayRecenterMode()
{
    const char* const requestedValue =
        std::getenv("KISAK_VR_RECENTER_ON_START");
    if (requestedValue == nullptr || requestedValue[0] == '\0')
    {
        return VrCalibration::RecenterMode::Full;
    }

    if (_stricmp(requestedValue, "off") == 0 ||
        std::strcmp(requestedValue, "0") == 0)
    {
        return VrCalibration::RecenterMode::Disabled;
    }
    if (_stricmp(requestedValue, "position_only") == 0)
    {
        return VrCalibration::RecenterMode::PositionOnly;
    }
    if (_stricmp(requestedValue, "direction_level_only") == 0)
    {
        return VrCalibration::RecenterMode::DirectionLevelOnly;
    }
    if (_stricmp(requestedValue, "full") == 0 ||
        std::strcmp(requestedValue, "1") == 0)
    {
        return VrCalibration::RecenterMode::Full;
    }

    Com_PrintWarning(
        0,
        "[VR][CALIBRATION] Ignoring invalid "
        "KISAK_VR_RECENTER_ON_START='%s'; using full. "
        "Valid values are off, position_only, "
        "direction_level_only, and full.\n",
        requestedValue);
    return VrCalibration::RecenterMode::Full;
}

void VR_WriteConfiguratorRuntimeReceipt(
    const VrConfiguratorSettings& loaded)
{
    const char* const receiptPath =
        std::getenv("KISAK_VR_SETTINGS_RECEIPT_PATH");
    if (receiptPath == nullptr || receiptPath[0] == '\0')
    {
        Com_PrintWarning(
            0,
            "[VR][CONFIG] No settings receipt path was supplied. "
            "Launch through Launch-KisakCOD-VR.bat or the configurator.\n");
        return;
    }

    FILE* receipt = nullptr;
    if (fopen_s(&receipt, receiptPath, "ab") != 0 || receipt == nullptr)
    {
        Com_PrintWarning(
            0,
            "[VR][CONFIG] The runtime could not append its acceptance "
            "to the settings receipt.\n");
        return;
    }

    const char* const profile =
        std::getenv("KISAK_VR_SETTINGS_PROFILE");
    const char* const revision =
        std::getenv("KISAK_VR_SETTINGS_REVISION");
    const char* const source =
        std::getenv("KISAK_VR_SETTINGS_SOURCE");

    const float activeEyeHeight =
        loaded.playMode == VrCalibration::PlayMode::Seated
            ? loaded.seatedEyeHeightInches
            : loaded.standingEyeHeightInches;

    std::fprintf(
        receipt,
        "\r\nSTATUS=RUNTIME_ACCEPTED\r\n"
        "RUNTIME_PROFILE=%s\r\n"
        "RUNTIME_REVISION=%s\r\n"
        "RUNTIME_SOURCE=%s\r\n"
        "RUNTIME_MEASUREMENT_UNITS=%s\r\n"
        "RUNTIME_WEAPON_OFFSET=%.2f %.2f %.2f\r\n"
        "RUNTIME_WEAPON_ANGLES=%.1f %.1f %.1f\r\n"
        "RUNTIME_WEAPON_PROFILES_ENABLED=%d\r\n"
        "RUNTIME_MANUAL_RELOAD=%d\r\n"
        "RUNTIME_MANUAL_GRENADES=%d\r\n"
        "RUNTIME_PLAY_MODE=%s\r\n"
        "RUNTIME_STANDING_EYE_HEIGHT=%.1f\r\n"
        "RUNTIME_SEATED_EYE_HEIGHT=%.1f\r\n"
        "RUNTIME_ACTIVE_EYE_HEIGHT=%.1f\r\n"
        "RUNTIME_ACTIVE_EYE_HEIGHT_DISPLAY=%.1f %s\r\n"
        "RUNTIME_RECENTER_ON_START=%d\r\n"
        "RUNTIME_RECENTER_ON_START_MODE=%s\r\n"
        "RUNTIME_HUD_AMMO=%.0f %.0f %.2f\r\n"
        "RUNTIME_HUD_COMPASS=%.0f %.0f %.2f\r\n"
        "RUNTIME_HUD_NOTIFICATIONS=%.0f %.0f %.2f\r\n"
        "RUNTIME_HUD_OBJECTIVE=%.0f %.0f %.2f\r\n"
        "RUNTIME_HUD_SUBTITLES=%.0f %.0f %.2f\r\n"
        "RUNTIME_HUD_EDITOR_ELEMENTS=%u\r\n"
        "RUNTIME_BINDING_ACTIONS=%u\r\n",
        profile != nullptr ? profile : "Unknown",
        revision != nullptr ? revision : "legacy-unverified",
        source != nullptr ? source : "Unknown",
        VR_MeasurementUnitSystemId(loaded.measurementUnits),
        loaded.weaponOffset[0],
        loaded.weaponOffset[1],
        loaded.weaponOffset[2],
        loaded.weaponAngles[0],
        loaded.weaponAngles[1],
        loaded.weaponAngles[2],
        loaded.weaponProfilesEnabled ? 1 : 0,
        loaded.manualReload ? 1 : 0,
        loaded.manualGrenades ? 1 : 0,
        VrCalibration::PlayModeId(loaded.playMode),
        loaded.standingEyeHeightInches,
        loaded.seatedEyeHeightInches,
        activeEyeHeight,
        VR_DisplayInches(activeEyeHeight, loaded.measurementUnits),
        VR_DisplayLengthUnit(loaded.measurementUnits),
        loaded.firstGameplayRecenterMode !=
                VrCalibration::RecenterMode::Disabled
            ? 1
            : 0,
        VrCalibration::RecenterModeId(
            loaded.firstGameplayRecenterMode),
        loaded.hudLayout.ammoOffsetX,
        loaded.hudLayout.ammoOffsetY,
        loaded.hudLayout.ammoScale,
        loaded.hudLayout.compassInsetX,
        loaded.hudLayout.compassInsetY,
        loaded.hudLayout.compassScale,
        loaded.hudLayout.notificationOffsetX,
        loaded.hudLayout.notificationOffsetY,
        loaded.hudLayout.notificationScale,
        loaded.hudLayout.objectiveOffsetX,
        loaded.hudLayout.objectiveOffsetY,
        loaded.hudLayout.objectiveScale,
        loaded.hudLayout.subtitleOffsetX,
        loaded.hudLayout.subtitleOffsetY,
        loaded.hudLayout.subtitleScale,
        static_cast<unsigned int>(VrHud::kElementCount),
        static_cast<unsigned int>(VrInput::kActionCount));

    std::fprintf(
        receipt,
        "RUNTIME_DOMINANT_HAND=%s\r\n"
        "RUNTIME_SUPPORT_GRIP_MODE=%s\r\n"
        "RUNTIME_OBJECT_GRIP_MODE=%s\r\n"
        "RUNTIME_RELOAD_EJECT_MODE=%s\r\n"
        "RUNTIME_RELOAD_INSERT_MODE=%s\r\n"
        "RUNTIME_RELOAD_PULL_DISTANCE=%.1f\r\n"
        "RUNTIME_MAGAZINE_HIP=%s\r\n"
        "RUNTIME_GRENADE_BELT_LAYOUT=%s\r\n"
        "RUNTIME_MELEE_MODE=%s\r\n"
        "RUNTIME_MELEE_SPEED=%.1f\r\n"
        "RUNTIME_HAPTICS=%d\r\n"
        "RUNTIME_HAPTIC_STRENGTH=%.2f\r\n"
        "RUNTIME_MUZZLE_OBSTRUCTION=%d\r\n",
        VrInteractions::DominantHandId(loaded.dominantHand),
        VrInteractions::SupportGripModeId(loaded.supportGripMode),
        VrInteractions::ObjectGripModeId(loaded.objectGripMode),
        VrInteractions::ReloadEjectModeId(loaded.reloadEjectMode),
        VrInteractions::ReloadInsertModeId(loaded.reloadInsertMode),
        loaded.reloadPullDistance,
        VrInteractions::MagazineHipId(loaded.magazineHip),
        VrInteractions::GrenadeBeltLayoutId(loaded.grenadeBeltLayout),
        VrInteractions::MeleeModeId(loaded.meleeMode),
        loaded.meleeSpeed,
        loaded.hapticsEnabled ? 1 : 0,
        loaded.hapticStrength,
        loaded.muzzleObstruction ? 1 : 0);

    const bool writeSucceeded =
        std::fflush(receipt) == 0 && std::ferror(receipt) == 0;
    std::fclose(receipt);

    if (!writeSucceeded)
    {
        Com_PrintWarning(
            0,
            "[VR][CONFIG] The runtime settings receipt could not be "
            "flushed completely.\n");
    }
}

VrInput::Binding VR_ReadInputBinding(
    const VrInput::ActionDefinition& action,
    const bool alternate,
    const int bindingsVersion)
{
    const char* const name = alternate
        ? action.alternateSettingKey
        : action.settingKey;

    const char* const defaultValue = alternate
        ? action.defaultAlternateBinding
        : action.defaultBinding;

    VrInput::Binding defaultBinding;
    VrInput::ParseBinding(
        action.action,
        defaultValue,
        &defaultBinding);

    const char* requestedValue = std::getenv(name);
    std::string migratedJumpValue;

    if (bindingsVersion < 4 &&
        action.action == VrInput::Action::Jump)
    {
        const auto environmentValue = [](
            const char* const key,
            const char* const fallback) -> std::string
        {
            const char* const value = std::getenv(key);
            return value == nullptr || value[0] == '\0'
                ? std::string(fallback)
                : std::string(value);
        };

        std::array<std::string, 2> migrated = {{
            VrInput::CanonicalizeLegacyValue(
                action.settingKey,
                environmentValue(
                    action.settingKey,
                    "left.trigger")),
            VrInput::CanonicalizeLegacyValue(
                action.alternateSettingKey,
                environmentValue(
                    action.alternateSettingKey,
                    "unbound")),
        }};

        const std::array<std::string, 2> legacyRaise = {{
            environmentValue(
                "KISAK_VR_BIND_RAISE_STANCE",
                "right.primary_axis.up"),
            environmentValue(
                "KISAK_VR_BIND_RAISE_STANCE_ALT",
                "unbound"),
        }};

        const bool standardV3Layout =
            _stricmp(migrated[0].c_str(), "left.trigger") == 0 &&
            _stricmp(migrated[1].c_str(), "unbound") == 0 &&
            _stricmp(
                legacyRaise[0].c_str(),
                "right.primary_axis.up") == 0 &&
            _stricmp(legacyRaise[1].c_str(), "unbound") == 0;

        if (standardV3Layout)
        {
            migrated[0] = action.defaultBinding;
            migrated[1] = action.defaultAlternateBinding;
        }
        else
        {
            for (const std::string& legacyBinding : legacyRaise)
            {
                if (legacyBinding.empty() ||
                    _stricmp(legacyBinding.c_str(), "unbound") == 0 ||
                    _stricmp(legacyBinding.c_str(), migrated[0].c_str()) == 0 ||
                    _stricmp(legacyBinding.c_str(), migrated[1].c_str()) == 0)
                {
                    continue;
                }

                if (_stricmp(migrated[0].c_str(), "unbound") == 0)
                {
                    migrated[0] = legacyBinding;
                }
                else if (_stricmp(migrated[1].c_str(), "unbound") == 0)
                {
                    migrated[1] = legacyBinding;
                }
            }
        }

        migratedJumpValue = migrated[alternate ? 1u : 0u];
        requestedValue = migratedJumpValue.c_str();
    }
    if (requestedValue == nullptr || requestedValue[0] == '\0')
    {
        return defaultBinding;
    }

    const bool legacyMissionDefault =
        bindingsVersion < 3 &&
        !alternate &&
        (action.action == VrInput::Action::GrenadeLauncher ||
         action.action == VrInput::Action::NightVision ||
         action.action == VrInput::Action::Airstrike ||
         action.action == VrInput::Action::C4) &&
        _stricmp(requestedValue, "unbound") == 0;

    const bool legacyRightGripDefault =
        bindingsVersion < 3 &&
        !alternate &&
        action.action == VrInput::Action::Offhand &&
        _stricmp(requestedValue, "right.squeeze") == 0;

    if (legacyMissionDefault)
    {
        return defaultBinding;
    }
    if (legacyRightGripDefault)
    {
        return VrInput::Binding{};
    }

    const std::string canonical =
        VrInput::CanonicalizeLegacyValue(
            name,
            requestedValue);

    VrInput::Binding binding;
    if (VrInput::ParseBinding(
            action.action,
            canonical,
            &binding))
    {
        return binding;
    }

    Com_PrintWarning(
        0,
        "[VR][CONTROLS] Ignoring invalid V4 binding %s='%s'; "
        "using %s.\n",
        name,
        requestedValue,
        defaultValue);

    return defaultBinding;
}

const VrConfiguratorSettings& VR_GetConfiguratorSettings()
{
    static const VrConfiguratorSettings settings = []()
    {
        VrConfiguratorSettings loaded;

        const char* const requestedUnits =
            std::getenv("KISAK_VR_UNIT_SYSTEM");
        if (requestedUnits != nullptr && requestedUnits[0] != '\0')
        {
            if (_stricmp(requestedUnits, "imperial") == 0)
            {
                loaded.measurementUnits =
                    VrMeasurementUnitSystem::Imperial;
            }
            else if (_stricmp(requestedUnits, "metric") != 0)
            {
                Com_PrintWarning(
                    0,
                    "[VR][CONFIG] Ignoring invalid "
                    "KISAK_VR_UNIT_SYSTEM='%s'; using metric.\n",
                    requestedUnits);
            }
        }

        const char* const requestedDominantHand =
            std::getenv("KISAK_VR_DOMINANT_HAND");
        if (requestedDominantHand != nullptr &&
            requestedDominantHand[0] != '\0' &&
            !VrInteractions::ParseDominantHand(
                requestedDominantHand,
                &loaded.dominantHand))
        {
            Com_PrintWarning(
                0,
                "[VR][INTERACTIONS] Ignoring invalid "
                "KISAK_VR_DOMINANT_HAND='%s'; using right.\n",
                requestedDominantHand);
        }

        loaded.snapTurnAngleDegrees =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_SNAP_TURN_ANGLE",
                45.0f,
                15.0f,
                90.0f);
        loaded.turnDeadzone =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_TURN_DEADZONE",
                0.25f,
                0.10f,
                0.50f);
        loaded.movementDeadzone =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_MOVEMENT_DEADZONE",
                0.18f,
                0.05f,
                0.40f);

        const char* const playMode =
            std::getenv("KISAK_VR_PLAY_MODE");
        if (playMode != nullptr && playMode[0] != '\0')
        {
            if (_stricmp(playMode, "seated") == 0)
            {
                loaded.playMode =
                    VrCalibration::PlayMode::Seated;
            }
            else if (_stricmp(playMode, "standing") != 0)
            {
                Com_PrintWarning(
                    0,
                    "[VR][CALIBRATION] Ignoring invalid "
                    "KISAK_VR_PLAY_MODE='%s'; using standing.\n",
                    playMode);
            }
        }

        loaded.standingEyeHeightInches =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_STANDING_EYE_HEIGHT",
                VrCalibration::kNativeStandingEyeHeightInches,
                VrCalibration::kMinimumEyeHeightInches,
                VrCalibration::kMaximumEyeHeightInches);
        loaded.seatedEyeHeightInches =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_SEATED_EYE_HEIGHT",
                VrCalibration::kNativeStandingEyeHeightInches,
                VrCalibration::kMinimumEyeHeightInches,
                VrCalibration::kMaximumEyeHeightInches);
        loaded.firstGameplayRecenterMode =
            VR_ReadFirstGameplayRecenterMode();

        loaded.hudLayout.safeX =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_HUD_SAFE_X", 0.50f, 0.50f, 1.00f);
        loaded.hudLayout.safeY =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_HUD_SAFE_Y", 1.00f, 0.50f, 1.00f);
        loaded.hudLayout.ammoOffsetX =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_HUD_BOTTOM_LEFT_X_OFFSET",
                0.0f,
                -320.0f,
                640.0f);
        loaded.hudLayout.ammoOffsetY =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_HUD_BOTTOM_LEFT_Y_OFFSET",
                0.0f,
                -240.0f,
                480.0f);
        loaded.hudLayout.ammoScale =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_HUD_BOTTOM_LEFT_SCALE",
                0.50f,
                VrHud::kMinimumScale,
                VrHud::kMaximumScale);
        loaded.hudLayout.compassEnabled =
            VR_ReadConfiguratorToggle(
                "KISAK_VR_COMPASS_ENABLED",
                true);
        loaded.hudLayout.compassInsetX =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_COMPASS_INSET_X",
                220.0f,
                -80.0f,
                600.0f);
        loaded.hudLayout.compassInsetY =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_COMPASS_INSET_Y",
                48.0f,
                -80.0f,
                440.0f);
        loaded.hudLayout.compassScale =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_COMPASS_SIZE",
                1.00f,
                VrHud::kMinimumScale,
                VrHud::kMaximumScale);
        loaded.hudLayout.notificationOffsetX =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_GAME_MESSAGE_X_OFFSET",
                0.0f,
                -300.0f,
                300.0f);
        loaded.hudLayout.notificationOffsetY =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_GAME_MESSAGE_Y_OFFSET",
                72.0f,
                -240.0f,
                400.0f);
        loaded.hudLayout.notificationScale =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_GAME_MESSAGE_SCALE",
                1.00f,
                VrHud::kMinimumScale,
                VrHud::kMaximumScale);
        loaded.hudLayout.objectiveOffsetX =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_OBJECTIVE_MESSAGE_X_OFFSET",
                0.0f,
                -300.0f,
                300.0f);
        loaded.hudLayout.objectiveOffsetY =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_OBJECTIVE_MESSAGE_Y_OFFSET",
                0.0f,
                -180.0f,
                270.0f);
        loaded.hudLayout.objectiveScale =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_OBJECTIVE_MESSAGE_SCALE",
                1.00f,
                VrHud::kMinimumScale,
                VrHud::kMaximumScale);
        loaded.hudLayout.subtitleOffsetX =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_SUBTITLE_X_OFFSET",
                0.0f,
                -300.0f,
                300.0f);
        loaded.hudLayout.subtitleOffsetY =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_SUBTITLE_Y_OFFSET",
                0.0f,
                -400.0f,
                80.0f);
        loaded.hudLayout.subtitleScale =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_SUBTITLE_SCALE",
                1.00f,
                VrHud::kMinimumScale,
                VrHud::kMaximumScale);
        VrHud::ClampLayout(&loaded.hudLayout);

        const char* movementDirection =
            std::getenv("KISAK_VR_MOVEMENT_DIRECTION");
        if (movementDirection != nullptr && movementDirection[0] != '\0')
        {
            if (_stricmp(movementDirection, "body") == 0)
            {
                loaded.movementDirection = VrMovementDirection::Body;
            }
            else if (_stricmp(movementDirection, "off_hand") == 0)
            {
                loaded.movementDirection = VrMovementDirection::OffHand;
            }
            else if (_stricmp(movementDirection, "weapon_hand") == 0)
            {
                loaded.movementDirection = VrMovementDirection::WeaponHand;
            }
            else if (_stricmp(movementDirection, "left_hand") == 0)
            {
                loaded.movementDirection = VrMovementDirection::PhysicalLeft;
            }
            else if (_stricmp(movementDirection, "right_hand") == 0)
            {
                loaded.movementDirection = VrMovementDirection::PhysicalRight;
            }
            else if (_stricmp(movementDirection, "head") != 0)
            {
                Com_PrintWarning(
                    0,
                    "[VR][CONFIG] Ignoring invalid "
                    "KISAK_VR_MOVEMENT_DIRECTION='%s'; using head.\n",
                    movementDirection);
            }
        }

        loaded.weaponOffset[0] =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_WEAPON_OFFSET_FORWARD", 0.0f, -8.0f, 8.0f);
        loaded.weaponOffset[1] =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_WEAPON_OFFSET_LEFT", 0.0f, -8.0f, 8.0f);
        loaded.weaponOffset[2] =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_WEAPON_OFFSET_UP", 0.0f, -8.0f, 8.0f);
        loaded.weaponAngles[0] =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_WEAPON_PITCH", 0.0f, -45.0f, 45.0f);
        loaded.weaponAngles[1] =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_WEAPON_YAW", 0.0f, -45.0f, 45.0f);
        loaded.weaponAngles[2] =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_WEAPON_ROLL", 0.0f, -45.0f, 45.0f);
        loaded.weaponProfilesEnabled =
            VR_ReadConfiguratorToggle(
                "KISAK_VR_WEAPON_PROFILES_ENABLED", true);
        loaded.weaponPositionResponse =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_WEAPON_POSITION_RESPONSE", 0.45f, 0.10f, 1.0f);
        loaded.weaponOrientationResponse =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_WEAPON_ORIENTATION_RESPONSE", 0.55f, 0.10f, 1.0f);
        loaded.twoHandStrength =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_TWO_HAND_STRENGTH", 1.0f, 0.0f, 1.0f);

        loaded.beltForwardOffset =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_BELT_FORWARD_OFFSET", 0.0f, -12.0f, 12.0f);
        loaded.beltHeight =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_BELT_HEIGHT", -28.0f, -48.0f, -8.0f);
        loaded.beltHipDistance =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_BELT_HIP_DISTANCE", 13.0f, 4.0f, 24.0f);
        loaded.beltGrabRadius =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_BELT_GRAB_RADIUS", 11.0f, 3.0f, 18.0f);
        loaded.reloadInsertRadius =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_RELOAD_INSERT_RADIUS", 6.5f, 3.0f, 12.0f);
        loaded.reloadPullDistance =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_RELOAD_PULL_DISTANCE", 8.0f, 4.0f, 18.0f);
        loaded.manualReload =
            VR_ReadConfiguratorToggle("KISAK_VR_MANUAL_RELOAD", true);
        loaded.manualGrenades =
            VR_ReadConfiguratorToggle("KISAK_VR_MANUAL_GRENADES", true);

        const auto parseInteractionChoice = [](
            const char* const settingName,
            const auto parser,
            auto* const destination)
        {
            const char* const requested = std::getenv(settingName);
            if (requested != nullptr && requested[0] != '\0' &&
                !parser(requested, destination))
            {
                Com_PrintWarning(
                    0,
                    "[VR][INTERACTIONS] Ignoring invalid %s='%s'.\n",
                    settingName,
                    requested);
            }
        };

        parseInteractionChoice(
            "KISAK_VR_RELOAD_EJECT_MODE",
            VrInteractions::ParseReloadEjectMode,
            &loaded.reloadEjectMode);
        parseInteractionChoice(
            "KISAK_VR_RELOAD_INSERT_MODE",
            VrInteractions::ParseReloadInsertMode,
            &loaded.reloadInsertMode);
        parseInteractionChoice(
            "KISAK_VR_MAGAZINE_HIP",
            VrInteractions::ParseMagazineHip,
            &loaded.magazineHip);
        parseInteractionChoice(
            "KISAK_VR_GRENADE_BELT_LAYOUT",
            VrInteractions::ParseGrenadeBeltLayout,
            &loaded.grenadeBeltLayout);
        parseInteractionChoice(
            "KISAK_VR_SUPPORT_GRIP_MODE",
            VrInteractions::ParseSupportGripMode,
            &loaded.supportGripMode);
        parseInteractionChoice(
            "KISAK_VR_OBJECT_GRIP_MODE",
            VrInteractions::ParseObjectGripMode,
            &loaded.objectGripMode);
        parseInteractionChoice(
            "KISAK_VR_MELEE_MODE",
            VrInteractions::ParseMeleeMode,
            &loaded.meleeMode);

        loaded.meleeSpeed =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_MELEE_SPEED", 95.0f, 50.0f, 240.0f);
        loaded.meleeForwardBias =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_MELEE_FORWARD_BIAS", 0.55f, 0.20f, 0.95f);
        loaded.meleeCooldownMilliseconds =
            static_cast<std::uint32_t>(
                VR_ReadConfiguratorFloat(
                    "KISAK_VR_MELEE_COOLDOWN_MS",
                    550.0f,
                    250.0f,
                    1500.0f));
        loaded.hapticsEnabled =
            VR_ReadConfiguratorToggle("KISAK_VR_HAPTICS", true);
        loaded.hapticStrength =
            VR_ReadConfiguratorFloat(
                "KISAK_VR_HAPTIC_STRENGTH", 1.0f, 0.0f, 1.5f);
        loaded.muzzleObstruction =
            VR_ReadConfiguratorToggle(
                "KISAK_VR_MUZZLE_OBSTRUCTION", true);

        if (loaded.reloadEjectMode ==
                VrInteractions::ReloadEjectMode::Pull &&
            loaded.reloadPullDistance <= loaded.reloadInsertRadius)
        {
            const float requestedPullDistance =
                loaded.reloadPullDistance;
            loaded.reloadPullDistance =
                (std::min)(
                    18.0f,
                    loaded.reloadInsertRadius + 1.5f);
            Com_PrintWarning(
                0,
                "[VR][INTERACTIONS] Physical magazine pull distance "
                "%.1f must exceed insertion radius %.1f; using %.1f "
                "%s.\n",
                VR_DisplayInches(
                    requestedPullDistance,
                    loaded.measurementUnits),
                VR_DisplayInches(
                    loaded.reloadInsertRadius,
                    loaded.measurementUnits),
                VR_DisplayInches(
                    loaded.reloadPullDistance,
                    loaded.measurementUnits),
                VR_DisplayLengthUnit(loaded.measurementUnits));
        }

        if (loaded.beltGrabRadius >= loaded.beltHipDistance)
        {
            Com_PrintWarning(
                0,
                "[VR][CONFIG] Belt grab radius %.1f overlaps the left and "
                "right hip zones; using tested %.1f/%.1f %s spacing.\n",
                VR_DisplayInches(
                    loaded.beltGrabRadius,
                    loaded.measurementUnits),
                VR_DisplayInches(13.0f, loaded.measurementUnits),
                VR_DisplayInches(11.0f, loaded.measurementUnits),
                VR_DisplayLengthUnit(loaded.measurementUnits));
            loaded.beltHipDistance = 13.0f;
            loaded.beltGrabRadius = 11.0f;
        }

        int bindingsVersion = 2;
        const char* const requestedBindingsVersion =
            std::getenv("KISAK_VR_INPUT_BINDINGS_VERSION");
        if (requestedBindingsVersion != nullptr &&
            requestedBindingsVersion[0] != '\0')
        {
            bindingsVersion = std::atoi(requestedBindingsVersion);
        }

        for (const VrInput::ActionDefinition& action :
             VrInput::ActionDefinitions())
        {
            const std::size_t index =
                static_cast<std::size_t>(action.action);

            loaded.bindings[index][0] =
                VR_ReadInputBinding(
                    action,
                    false,
                    bindingsVersion);

            loaded.bindings[index][1] =
                VR_ReadInputBinding(
                    action,
                    true,
                    bindingsVersion);
        }

        Com_Printf(
            0,
            "[VR][CONFIG] beta.13 customization loaded: snap %.0f, turn "
            "deadzone %.2f, movement deadzone %.2f, two-hand %.2f; "
            "belt forward %.1f, height %.1f, hip %.1f +/- %.1f %s.\n",
            loaded.snapTurnAngleDegrees,
            loaded.turnDeadzone,
            loaded.movementDeadzone,
            loaded.twoHandStrength,
            VR_DisplayInches(
                loaded.beltForwardOffset,
                loaded.measurementUnits),
            VR_DisplayInches(
                loaded.beltHeight,
                loaded.measurementUnits),
            VR_DisplayInches(
                loaded.beltHipDistance,
                loaded.measurementUnits),
            VR_DisplayInches(
                loaded.beltGrabRadius,
                loaded.measurementUnits),
            VR_DisplayLengthUnit(loaded.measurementUnits));

        Com_Printf(
            0,
            "[VR][CONFIG] Measurement presentation is %s; canonical "
            "COD4 calibration values remain unchanged.\n",
            VR_MeasurementUnitSystemId(loaded.measurementUnits));

        const char* const profile =
            std::getenv("KISAK_VR_SETTINGS_PROFILE");
        const char* const revision =
            std::getenv("KISAK_VR_SETTINGS_REVISION");
        const char* const source =
            std::getenv("KISAK_VR_SETTINGS_SOURCE");
        Com_Printf(
            0,
            "[VR][CONFIG] Active profile '%s', revision '%s', source '%s'.\n",
            profile != nullptr ? profile : "Unknown",
            revision != nullptr ? revision : "legacy-unverified",
            source != nullptr ? source : "Unknown");

        Com_Printf(
            0,
            "[VR][CONFIG] Application-sensitive settings: weapon offset "
            "%.2f %.2f %.2f %s; manual reload %s; manual grenades %s.\n",
            VR_DisplayInches(
                loaded.weaponOffset[0],
                loaded.measurementUnits),
            VR_DisplayInches(
                loaded.weaponOffset[1],
                loaded.measurementUnits),
            VR_DisplayInches(
                loaded.weaponOffset[2],
                loaded.measurementUnits),
            VR_DisplayLengthUnit(loaded.measurementUnits),
            loaded.manualReload ? "enabled" : "disabled",
            loaded.manualGrenades ? "enabled" : "disabled");

        Com_Printf(
            0,
            "[VR][INTERACTIONS] Weapon hand %s; support %s; object grip "
            "%s; reload eject/insert %s/%s; magazine hip %s; grenades "
            "%s; melee %s; haptics %s at %.2f; muzzle obstruction %s.\n",
            VrInteractions::DominantHandId(loaded.dominantHand),
            VrInteractions::SupportGripModeId(loaded.supportGripMode),
            VrInteractions::ObjectGripModeId(loaded.objectGripMode),
            VrInteractions::ReloadEjectModeId(loaded.reloadEjectMode),
            VrInteractions::ReloadInsertModeId(loaded.reloadInsertMode),
            VrInteractions::MagazineHipId(loaded.magazineHip),
            VrInteractions::GrenadeBeltLayoutId(loaded.grenadeBeltLayout),
            VrInteractions::MeleeModeId(loaded.meleeMode),
            loaded.hapticsEnabled ? "enabled" : "disabled",
            loaded.hapticStrength,
            loaded.muzzleObstruction ? "enabled" : "disabled");

        const float activeEyeHeight =
            loaded.playMode == VrCalibration::PlayMode::Seated
                ? loaded.seatedEyeHeightInches
                : loaded.standingEyeHeightInches;

        Com_Printf(
            0,
            "[VR][CALIBRATION] V70 posture %s; target eye height %.1f "
            "%s; first-gameplay recenter %s.\n",
            VrCalibration::PlayModeId(loaded.playMode),
            VR_DisplayInches(
                activeEyeHeight,
                loaded.measurementUnits),
            VR_DisplayLengthUnit(loaded.measurementUnits),
            VrCalibration::RecenterModeId(
                loaded.firstGameplayRecenterMode));

        Com_Printf(
            0,
            "[VR][HUD] V61 visual layout loaded: ammo %.0f/%.0f at "
            "%.2f; compass %.0f/%.0f at %.2f; notifications "
            "%.0f/%.0f; objective %.0f/%.0f; subtitles %.0f/%.0f.\n",
            loaded.hudLayout.ammoOffsetX,
            loaded.hudLayout.ammoOffsetY,
            loaded.hudLayout.ammoScale,
            loaded.hudLayout.compassInsetX,
            loaded.hudLayout.compassInsetY,
            loaded.hudLayout.compassScale,
            loaded.hudLayout.notificationOffsetX,
            loaded.hudLayout.notificationOffsetY,
            loaded.hudLayout.objectiveOffsetX,
            loaded.hudLayout.objectiveOffsetY,
            loaded.hudLayout.subtitleOffsetX,
            loaded.hudLayout.subtitleOffsetY);

        Com_Printf(
            0,
            "[VR][CONTROLS] Controller Input V4 loaded %u actions "
            "with primary/alternate slots and input chords.\n",
            static_cast<unsigned int>(VrInput::kActionCount));

        VR_WriteConfiguratorRuntimeReceipt(loaded);

        return loaded;
    }();

    return settings;
}

void VR_EnsureHudLayoutInitialized()
{
    if (g_vrHudLayoutInitialized)
    {
        return;
    }

    const VrHud::Layout configured =
        VR_GetConfiguratorSettings().hudLayout;

    std::lock_guard<std::mutex> lock(
        g_vrHudEditorMutex);
    if (!g_vrHudLayoutInitialized)
    {
        g_vrHudEditorLayout = configured;
        g_vrHudEditorOriginalLayout = configured;
        g_vrHudLayoutInitialized = true;
        ++g_vrHudLayoutRevision;
    }
}

bool VR_WriteHudEditorResponse(
    const VrHud::Response& response)
{
    const char* const statusPath =
        std::getenv("KISAK_VR_HUD_EDITOR_STATUS_PATH");
    if (statusPath == nullptr || statusPath[0] == '\0')
    {
        return false;
    }

    const std::string temporaryPath =
        std::string(statusPath) + ".tmp";
    {
        std::ofstream output(
            temporaryPath,
            std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return false;
        }
        output << VrHud::SerializeResponse(response);
        output.flush();
        if (!output)
        {
            return false;
        }
    }

    if (!MoveFileExA(
            temporaryPath.c_str(),
            statusPath,
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH))
    {
        std::remove(temporaryPath.c_str());
        return false;
    }
    return true;
}

void VR_AppendHudEditorReceipt(
    const VrHud::Response& response)
{
    const char* const receiptPath =
        std::getenv("KISAK_VR_SETTINGS_RECEIPT_PATH");
    if (receiptPath == nullptr || receiptPath[0] == '\0')
    {
        return;
    }

    FILE* receipt = nullptr;
    if (fopen_s(&receipt, receiptPath, "ab") != 0 || receipt == nullptr)
    {
        return;
    }

    const VrHud::Layout& layout = response.layout;
    std::fprintf(
        receipt,
        "\r\nSTATUS=RUNTIME_HUD_EDITOR_%s\r\n"
        "RUNTIME_HUD_EDITOR_REQUEST=%s\r\n"
        "RUNTIME_HUD_EDITOR_AMMO=%.0f %.0f %.2f\r\n"
        "RUNTIME_HUD_EDITOR_COMPASS=%.0f %.0f %.2f\r\n"
        "RUNTIME_HUD_EDITOR_NOTIFICATIONS=%.0f %.0f %.2f\r\n"
        "RUNTIME_HUD_EDITOR_OBJECTIVE=%.0f %.0f %.2f\r\n"
        "RUNTIME_HUD_EDITOR_SUBTITLES=%.0f %.0f %.2f\r\n",
        response.status == VrHud::ResponseStatus::Saved
            ? "SAVED"
            : "CANCELED",
        response.requestId.c_str(),
        layout.ammoOffsetX,
        layout.ammoOffsetY,
        layout.ammoScale,
        layout.compassInsetX,
        layout.compassInsetY,
        layout.compassScale,
        layout.notificationOffsetX,
        layout.notificationOffsetY,
        layout.notificationScale,
        layout.objectiveOffsetX,
        layout.objectiveOffsetY,
        layout.objectiveScale,
        layout.subtitleOffsetX,
        layout.subtitleOffsetY,
        layout.subtitleScale);
    std::fflush(receipt);
    std::fclose(receipt);
}

void VR_FinishHudEditor(const bool save)
{
    VrHud::Response response;
    {
        std::lock_guard<std::mutex> lock(
            g_vrHudEditorMutex);
        if (!g_vrHudEditorActive)
        {
            return;
        }

        if (!save)
        {
            g_vrHudEditorLayout =
                g_vrHudEditorOriginalLayout;
        }

        response.requestId = g_vrHudEditorRequestId;
        response.status = save
            ? VrHud::ResponseStatus::Saved
            : VrHud::ResponseStatus::Canceled;
        response.layout = g_vrHudEditorLayout;
        response.message = save
            ? "Layout saved by the in-headset editor"
            : "In-headset edit canceled; original layout restored";

        g_vrHudEditorActive = false;
        g_vrHudEditorDragging = false;
        g_vrHudEditorPointerValid = false;
        g_vrHudEditorPreviousWasHeld = false;
        g_vrHudEditorNextWasHeld = false;
        g_vrHudEditorCenterWasHeld = false;
        g_vrHudEditorResetWasHeld = false;
        g_vrHudEditorKeyboardTabWasHeld = false;
        g_vrHudEditorKeyboardCenterWasHeld = false;
        g_vrHudEditorKeyboardResetWasHeld = false;
        ++g_vrHudLayoutRevision;
    }

    VR_WriteHudEditorResponse(response);
    VR_AppendHudEditorReceipt(response);
    Com_Printf(
        0,
        "[VR][HUD][EDITOR] Request %s %s; ammo %.0f/%.0f, "
        "compass %.0f/%.0f, notifications %.0f/%.0f, "
        "objective %.0f/%.0f, subtitles %.0f/%.0f.\n",
        response.requestId.c_str(),
        save ? "saved" : "canceled",
        response.layout.ammoOffsetX,
        response.layout.ammoOffsetY,
        response.layout.compassInsetX,
        response.layout.compassInsetY,
        response.layout.notificationOffsetX,
        response.layout.notificationOffsetY,
        response.layout.objectiveOffsetX,
        response.layout.objectiveOffsetY,
        response.layout.subtitleOffsetX,
        response.layout.subtitleOffsetY);
}

// KISAK_SP_VR_POSE_FOCUS_AIM_V1
// ADS is published by the two-hand eye-level pose detector instead of the
// left index trigger, leaving that trigger available for jump. Controller
// Input V4 can optionally OR in a conventional held ADS binding.
bool g_vrPoseFocusAimHeld = false;
bool g_vrConfiguredAimHeld = false;

// KISAK_SP_VR_SEPARATE_USE_RELOAD_V1
// Keep pickup/activate and reload on independent usercmd bits.
// KISAK_SP_VR_CONTROLS_V29_A_EJECT_LEFT_TRIGGER_JUMP
// Right A owns magazine ejection/native reload and remains menu confirm;
// the left index trigger now owns jump.
bool g_vrLeftTriggerJumpHeld = false;
bool g_vrRightAButtonHeld = false;
bool g_vrLeftXUseHeld = false;

bool g_vrLeftStickSprintHeld = false;
bool g_vrRightStickMeleeHeld = false;
bool g_vrRightBStanceHeld = false;
bool g_vrLowerStanceHeld = false;

bool g_vrNativeOffhandHeld = false;
bool g_vrLeftYNextWeaponHeld = false;

bool g_vrLeftMenuHeld = false;
bool g_vrLeftMenuWasHeld = false;
bool g_vrMenuConfirmHeld = false;
bool g_vrMenuBackHeld = false;

// KISAK_SP_VR_MANUAL_MAGAZINE_RELOAD_V1
enum class VrManualMagazineReloadStage : std::uint32_t
{
    Ready = 0u,
    HoldingLoaded,
    Ejected,
    HoldingFresh,
};

struct VrManualMagazineReloadState
{
    bool settingRead = false;
    bool enabled = true;
    bool supported = false;
    bool canReload = false;
    int weaponIndex = 0;
    VrManualMagazineReloadStage stage =
        VrManualMagazineReloadStage::Ready;
    bool ejectButtonWasHeld = false;
    bool leftSqueezeWasHeld = false;
    bool heldNearMagazineWell = false;
    bool heldPoseValid = false;
    std::uint32_t ejectedAtMilliseconds = 0u;
    std::uint32_t commitUntilMilliseconds = 0u;
    float magazineWellOrigin[3] = {};
    float magazineWellAxis[3][3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
    float ejectedOrigin[3] = {};
    float ejectedAxis[3][3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
    float ejectedVelocity[3] = {};
    float heldOrigin[3] = {};
    float heldAxis[3][3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
};

VrManualMagazineReloadState
    g_vrManualMagazineReload;

// KISAK_SP_VR_MANUAL_GRENADE_THROW_V53
enum class VrManualGrenadeStage : std::uint32_t
{
    Ready = 0u,
    Holding,
    ReleasedPending,
};

enum class VrManualGrenadeSlot : std::uint32_t
{
    None = 0u,
    Frag,
    Tactical,
};

// KISAK_SP_VR_MANUAL_GRENADE_RELEASE_CALIBRATION_V54
// OpenXR supplies a smooth physical velocity, but the button-release edge can
// arrive just after the fastest part of a throw.  Retain a short, fixed-size
// history so release can select a recent swing sample without allocating in
// the input path.
constexpr std::size_t
    kVrManualGrenadeVelocitySampleCapacity = 12u;

struct VrManualGrenadeVelocitySample
{
    bool valid = false;
    std::uint32_t recordedAtMilliseconds = 0u;
    float velocity[3] = {};
};

struct VrManualGrenadeState
{
    bool settingRead = false;
    bool enabled = true;
    bool inputInitialized = false;
    bool leftSqueezeWasHeld = false;
    VrManualGrenadeStage stage =
        VrManualGrenadeStage::Ready;
    VrManualGrenadeSlot slot =
        VrManualGrenadeSlot::None;
    int weaponIndex = 0;
    std::uint32_t releasedAtMilliseconds = 0u;
    std::uint32_t pendingUntilMilliseconds = 0u;
    std::uint32_t viewOverrideUntilMilliseconds = 0u;
    std::uint32_t releaseVelocitySampleAgeMilliseconds = 0u;
    std::size_t velocitySampleWriteIndex = 0u;
    float heldOrigin[3] = {};
    float heldAxis[3][3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
    float releaseOrigin[3] = {};
    float releaseVelocity[3] = {};
    float releaseFallbackForward[3] = {
        1.0f,
        0.0f,
        0.0f,
    };
    std::array<
        VrManualGrenadeVelocitySample,
        kVrManualGrenadeVelocitySampleCapacity>
        velocitySamples = {};
};

VrManualGrenadeState
    g_vrManualGrenade;


std::vector<XrViewConfigurationView> g_vrViewConfigs;
std::vector<XrView> g_vrViews;

struct VrEyeProjectionTangents
{
    float left = 0.0f;
    float right = 0.0f;
    float down = 0.0f;
    float up = 0.0f;
};

std::mutex g_vrProjectionMutex;

std::array<
    VrEyeProjectionTangents,
    kVrStereoEyeCount>
    g_vrEyeProjectionTangents = {};

bool g_vrEyeProjectionValid = false;

thread_local int g_vrCurrentRenderEye = -1;

std::array<bool, kVrStereoEyeCount>
    g_vrLoggedProjectionApply = {};

bool g_vrLoggedProjectionPublish = false;

struct VrEyeSwapchain
{
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;

    std::vector<XrSwapchainImageD3D11KHR> images;
    std::vector<ComPtr<ID3D11RenderTargetView>> renderTargetViews;
};

std::vector<VrEyeSwapchain> g_vrEyeSwapchains;

struct VrOpenVrEyeTarget
{
    int32_t width = 0;
    int32_t height = 0;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> renderTargetView;
};

std::array<VrOpenVrEyeTarget, kVrStereoEyeCount>
    g_vrOpenVrEyeTargets = {};

// KISAK_SP_VR_OPENXR_STARTUP_DIAGNOSTICS_V31
// Preserve a human-readable startup failure even before an XrInstance exists,
// when xrResultToString cannot be called.  WinMain uses this to stop instead of
// silently continuing into the flat renderer.
std::array<char, 1024> g_vrLastStartupError = {};

const char* VR_XrResultName(const XrResult result)
{
    switch (static_cast<int>(result))
    {
        case -1: return "XR_ERROR_VALIDATION_FAILURE";
        case -2: return "XR_ERROR_RUNTIME_FAILURE";
        case -3: return "XR_ERROR_OUT_OF_MEMORY";
        case -4: return "XR_ERROR_API_VERSION_UNSUPPORTED";
        case -6: return "XR_ERROR_INITIALIZATION_FAILED";
        case -7: return "XR_ERROR_FUNCTION_UNSUPPORTED";
        case -8: return "XR_ERROR_FEATURE_UNSUPPORTED";
        case -9: return "XR_ERROR_EXTENSION_NOT_PRESENT";
        case -10: return "XR_ERROR_LIMIT_REACHED";
        case -11: return "XR_ERROR_SIZE_INSUFFICIENT";
        case -12: return "XR_ERROR_HANDLE_INVALID";
        case -13: return "XR_ERROR_INSTANCE_LOST";
        case -14: return "XR_ERROR_SESSION_RUNNING";
        case -16: return "XR_ERROR_SESSION_NOT_RUNNING";
        case -17: return "XR_ERROR_SESSION_LOST";
        case -18: return "XR_ERROR_SYSTEM_INVALID";
        case -19: return "XR_ERROR_PATH_INVALID";
        case -20: return "XR_ERROR_PATH_COUNT_EXCEEDED";
        case -21: return "XR_ERROR_PATH_FORMAT_INVALID";
        case -22: return "XR_ERROR_PATH_UNSUPPORTED";
        case -23: return "XR_ERROR_LAYER_INVALID";
        case -24: return "XR_ERROR_LAYER_LIMIT_EXCEEDED";
        case -25: return "XR_ERROR_SWAPCHAIN_RECT_INVALID";
        case -26: return "XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED";
        case -27: return "XR_ERROR_ACTION_TYPE_MISMATCH";
        case -28: return "XR_ERROR_SESSION_NOT_READY";
        case -29: return "XR_ERROR_SESSION_NOT_STOPPING";
        case -30: return "XR_ERROR_TIME_INVALID";
        case -31: return "XR_ERROR_REFERENCE_SPACE_UNSUPPORTED";
        case -32: return "XR_ERROR_FILE_ACCESS_ERROR";
        case -33: return "XR_ERROR_FILE_CONTENTS_INVALID";
        case -34: return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
        case -35: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
        case -36: return "XR_ERROR_API_LAYER_NOT_PRESENT";
        case -37: return "XR_ERROR_CALL_ORDER_INVALID";
        case -38: return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
        case -39: return "XR_ERROR_POSE_INVALID";
        case -40: return "XR_ERROR_INDEX_OUT_OF_RANGE";
        case -41: return "XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED";
        case -42: return "XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED";
        case -44: return "XR_ERROR_NAME_DUPLICATED";
        case -45: return "XR_ERROR_NAME_INVALID";
        case -46: return "XR_ERROR_ACTIONSET_NOT_ATTACHED";
        case -47: return "XR_ERROR_ACTIONSETS_ALREADY_ATTACHED";
        case -48: return "XR_ERROR_LOCALIZED_NAME_DUPLICATED";
        case -49: return "XR_ERROR_LOCALIZED_NAME_INVALID";
        case -50: return "XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING";
        case -51: return "XR_ERROR_RUNTIME_UNAVAILABLE";
        default: return "XR_ERROR_UNRECOGNIZED";
    }
}

const char* VR_XrStartupAdvice(const XrResult result)
{
    switch (static_cast<int>(result))
    {
        case -32:
            return
                "The active OpenXR runtime or an enabled API layer could not "
                "access a required manifest or DLL. Check OpenXR-Startup.log "
                "for the exact runtime/layer path.";
        case -33:
            return
                "An OpenXR runtime or API-layer manifest is invalid. Check "
                "OpenXR-Startup.log for the selected JSON file.";
        case -35:
            return
                "The runtime is installed, but no ready headset is available. "
                "Connect and wake the headset before launching.";
        case -36:
            return
                "A requested or implicit OpenXR API layer could not be loaded. "
                "Check OpenXR-Startup.log for registered layers.";
        case -38:
            return
                "The OpenXR runtime rejected the graphics adapter. Ensure the "
                "headset runtime and KisakCOD use the same GPU.";
        case -51:
            return
                "No usable 32-bit OpenXR runtime was found. Select an active "
                "OpenXR runtime and restart its headset software.";
        default:
            return
                "Check OpenXR-Startup.log and main\\console.log for the failing "
                "runtime stage.";
    }
}

void VR_RecordStartupFailure(
    const char* operation,
    const char* resultName,
    const int resultValue,
    const char* advice)
{
    std::snprintf(
        g_vrLastStartupError.data(),
        g_vrLastStartupError.size(),
        "%s failed: %s (%d). %s",
        operation != nullptr ? operation : "OpenXR initialization",
        resultName != nullptr ? resultName : "unknown error",
        resultValue,
        advice != nullptr ? advice : "See the startup logs.");
}

const char* VR_RuntimeBackendName()
{
    switch (g_vrRuntimeBackend)
    {
        case VrRuntimeBackend::OpenXr:
            return "OpenXR";
        case VrRuntimeBackend::OpenVr:
            return "OpenVR/SteamVR";
        default:
            return "none";
    }
}

void VR_AppendCompatibilityRuntimeReceipt()
{
    const char* const receiptPath =
        std::getenv("KISAK_VR_SETTINGS_RECEIPT_PATH");
    if (receiptPath == nullptr || receiptPath[0] == '\0')
    {
        return;
    }

    FILE* receipt = nullptr;
    if (fopen_s(&receipt, receiptPath, "ab") != 0 || receipt == nullptr)
    {
        return;
    }

    std::fprintf(
        receipt,
        "\r\nSTATUS=RUNTIME_COMPATIBILITY_READY\r\n"
        "RUNTIME_COMPATIBILITY_STATUS=READY\r\n"
        "RUNTIME_COMPATIBILITY_BACKEND=%s\r\n"
        "RUNTIME_COMPATIBILITY_RUNTIME=%s\r\n"
        "RUNTIME_COMPATIBILITY_HEADSET=%s\r\n"
        "RUNTIME_COMPATIBILITY_LEFT_CONTROLLER=%s\r\n"
        "RUNTIME_COMPATIBILITY_RIGHT_CONTROLLER=%s\r\n",
        VR_RuntimeBackendName(),
        g_vrCompatibilityRuntimeName.data(),
        g_vrCompatibilityHeadsetName.data(),
        g_vrCompatibilityControllerProfiles[VR_CONTROLLER_LEFT].data(),
        g_vrCompatibilityControllerProfiles[VR_CONTROLLER_RIGHT].data());
    std::fflush(receipt);
    std::fclose(receipt);
}

void VR_RecordOpenVrStartupFailure(
    const char* openXrFailure,
    const char* operation,
    const char* resultName,
    const int resultValue)
{
    std::snprintf(
        g_vrLastStartupError.data(),
        g_vrLastStartupError.size(),
        "%s OpenVR fallback %s: %s (%d). Start SteamVR, connect "
        "the headset, and see OpenXR-Startup.log plus main\\console.log.",
        openXrFailure != nullptr && openXrFailure[0] != '\0'
            ? openXrFailure
            : "OpenXR was unavailable.",
        operation != nullptr ? operation : "failed",
        resultName != nullptr ? resultName : "unknown error",
        resultValue);
}

const char* VR_SessionStateName(const XrSessionState state)
{
    switch (state)
    {
        case XR_SESSION_STATE_UNKNOWN:
            return "UNKNOWN";
        case XR_SESSION_STATE_IDLE:
            return "IDLE";
        case XR_SESSION_STATE_READY:
            return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED:
            return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE:
            return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED:
            return "FOCUSED";
        case XR_SESSION_STATE_STOPPING:
            return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING:
            return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING:
            return "EXITING";
        default:
            return "UNRECOGNIZED";
    }
}

void VR_LogXrFailure(const char* operation, const XrResult result)
{
    char runtimeResultText[XR_MAX_RESULT_STRING_SIZE] = {};
    const char* resultName = VR_XrResultName(result);

    if (g_vrInstance != XR_NULL_HANDLE &&
        XR_SUCCEEDED(
            xrResultToString(
                g_vrInstance,
                result,
                runtimeResultText)) &&
        runtimeResultText[0] != '\0')
    {
        resultName = runtimeResultText;
    }

    VR_RecordStartupFailure(
        operation,
        resultName,
        static_cast<int>(result),
        VR_XrStartupAdvice(result));

    Com_PrintWarning(
        0,
        "[VR] %s\n",
        g_vrLastStartupError.data());
}

void VR_LogHrFailure(const char* operation, const HRESULT hr)
{
    char resultName[32] = {};

    std::snprintf(
        resultName,
        sizeof(resultName),
        "HRESULT 0x%08lX",
        static_cast<unsigned long>(hr));

    VR_RecordStartupFailure(
        operation,
        resultName,
        static_cast<int>(hr),
        "The Direct3D VR graphics bridge failed. Check main\\console.log.");

    Com_PrintWarning(
        0,
        "[VR] %s\n",
        g_vrLastStartupError.data());
}

bool VR_HasInstanceExtension(const char* requestedExtension)
{
    uint32_t extensionCount = 0;

    XrResult result =
        xrEnumerateInstanceExtensionProperties(
            nullptr,
            0,
            &extensionCount,
            nullptr);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrEnumerateInstanceExtensionProperties(count)",
            result);

        return false;
    }

    std::vector<XrExtensionProperties> extensions(extensionCount);

    for (XrExtensionProperties& extension : extensions)
    {
        extension = XrExtensionProperties{
            XR_TYPE_EXTENSION_PROPERTIES
        };
    }

    result =
        xrEnumerateInstanceExtensionProperties(
            nullptr,
            extensionCount,
            &extensionCount,
            extensions.data());

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrEnumerateInstanceExtensionProperties(list)",
            result);

        return false;
    }

    for (const XrExtensionProperties& extension : extensions)
    {
        if (std::strcmp(
                extension.extensionName,
                requestedExtension) == 0)
        {
            return true;
        }
    }

    return false;
}

bool VR_LuidMatches(const LUID& left, const LUID& right)
{
    return
        left.LowPart == right.LowPart &&
        left.HighPart == right.HighPart;
}

bool VR_CreateD3D11Device(
    const XrGraphicsRequirementsD3D11KHR& requirements)
{
    // KISAK_SP_VR_OPENXR_DXGI_1_1_FACTORY_V84
    // SteamVR's x86 OpenXR compositor imports submitted D3D11 eye textures
    // through DXGI 1.1. Create the OpenXR device from the matching factory
    // and adapter interfaces so the compositor can create its sync texture.
    ComPtr<IDXGIFactory1> factory;

    HRESULT hr =
        CreateDXGIFactory1(
            IID_PPV_ARGS(factory.GetAddressOf()));

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateDXGIFactory1(OpenXR)",
            hr);
        return false;
    }

    ComPtr<IDXGIAdapter1> selectedAdapter;

    for (UINT adapterIndex = 0; ; ++adapterIndex)
    {
        ComPtr<IDXGIAdapter1> candidateAdapter;

        hr = factory->EnumAdapters1(
            adapterIndex,
            candidateAdapter.GetAddressOf());

        if (hr == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        if (FAILED(hr))
        {
            VR_LogHrFailure(
                "IDXGIFactory1::EnumAdapters1(OpenXR)",
                hr);
            return false;
        }

        DXGI_ADAPTER_DESC1 description = {};

        hr = candidateAdapter->GetDesc1(&description);

        if (FAILED(hr))
        {
            VR_LogHrFailure(
                "IDXGIAdapter1::GetDesc1(OpenXR)",
                hr);
            return false;
        }

        if (VR_LuidMatches(
                description.AdapterLuid,
                requirements.adapterLuid))
        {
            selectedAdapter = candidateAdapter;
            break;
        }
    }

    if (!selectedAdapter)
    {
        Com_PrintWarning(
            0,
            "[VR] Could not find the DXGI adapter requested by OpenXR.\n");

        return false;
    }

    // The June 2010 DirectX SDK headers stop at feature level 11_0.
    // OpenXR already tells us the minimum level required by the runtime, so
    // request that exact level without referring to newer enum constants.
    D3D_FEATURE_LEVEL requestedFeatureLevel =
        requirements.minFeatureLevel;

    D3D_FEATURE_LEVEL createdFeatureLevel =
        D3D_FEATURE_LEVEL_10_0;

    hr = D3D11CreateDevice(
        selectedAdapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        &requestedFeatureLevel,
        1,
        D3D11_SDK_VERSION,
        g_vrD3dDevice.GetAddressOf(),
        &createdFeatureLevel,
        g_vrD3dContext.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("D3D11CreateDevice", hr);
        return false;
    }

    if (!VR_ProbeD3D9ExD3D11Interop(
            dx.device,
            g_vrD3dDevice.Get(),
            requirements.adapterLuid))
    {
        Com_PrintWarning(
            0,
            "[VR] D3D9Ex/D3D11 shared-texture "
            "interop probe did not pass. "
            "The current CPU bridge remains active.\n");
    }

    Com_Printf(
        0,
        "[VR][OPENXR] V84 created the D3D11 device through "
        "DXGI 1.1 at feature level 0x%X.\n",
        static_cast<unsigned int>(createdFeatureLevel));

    return true;
}

bool VR_CreateOpenVrD3D11Device()
{
    if (g_vrOpenVrSystem == nullptr)
    {
        return false;
    }

    int32_t adapterIndex = -1;
    g_vrOpenVrSystem->GetDXGIOutputInfo(
        &adapterIndex);

    if (adapterIndex < 0)
    {
        VR_RecordOpenVrStartupFailure(
            nullptr,
            "did not identify a compositor DXGI adapter",
            "VRInitError_Driver_Failed",
            static_cast<int>(
                vr::VRInitError_Driver_Failed));

        Com_PrintWarning(
            0,
            "[VR][OPENVR] SteamVR did not return a valid DXGI "
            "adapter index.\n");

        return false;
    }

    ComPtr<IDXGIFactory1> factory;

    HRESULT hr =
        CreateDXGIFactory1(
            IID_PPV_ARGS(factory.GetAddressOf()));

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateDXGIFactory1(OpenVR)",
            hr);
        return false;
    }

    ComPtr<IDXGIAdapter1> selectedAdapter;

    hr = factory->EnumAdapters1(
        static_cast<UINT>(adapterIndex),
        selectedAdapter.GetAddressOf());

    if (FAILED(hr) || selectedAdapter == nullptr)
    {
        VR_LogHrFailure(
            "IDXGIFactory1::EnumAdapters1(OpenVR)",
            hr);
        return false;
    }

    DXGI_ADAPTER_DESC1 adapterDescription = {};

    hr = selectedAdapter->GetDesc1(
        &adapterDescription);

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "IDXGIAdapter1::GetDesc1(OpenVR)",
            hr);
        return false;
    }

    constexpr std::array<D3D_FEATURE_LEVEL, 3>
        requestedFeatureLevels = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

    D3D_FEATURE_LEVEL createdFeatureLevel =
        D3D_FEATURE_LEVEL_10_0;

    hr = D3D11CreateDevice(
        selectedAdapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        requestedFeatureLevels.data(),
        static_cast<UINT>(
            requestedFeatureLevels.size()),
        D3D11_SDK_VERSION,
        g_vrD3dDevice.GetAddressOf(),
        &createdFeatureLevel,
        g_vrD3dContext.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "D3D11CreateDevice(OpenVR)",
            hr);
        return false;
    }

    if (!VR_ProbeD3D9ExD3D11Interop(
            dx.device,
            g_vrD3dDevice.Get(),
            adapterDescription.AdapterLuid))
    {
        Com_PrintWarning(
            0,
            "[VR][OPENVR] D3D9Ex/D3D11 shared-texture "
            "interop did not pass; the CPU capture bridge "
            "will remain available.\n");
    }

    Com_Printf(
        0,
        "[VR][OPENVR] Created compositor-matched D3D11 "
        "device on adapter %d at feature level 0x%X.\n",
        adapterIndex,
        static_cast<unsigned int>(
            createdFeatureLevel));

    return true;
}


struct VrTestMatrix
{
    float m[4][4];
};

struct VrTestVertex
{
    float position[3];
    float color[3];
};

struct VrTestConstants
{
    VrTestMatrix modelViewProjection;
};

VrTestMatrix VR_TestIdentity()
{
    VrTestMatrix result = {};
    result.m[0][0] = 1.0f;
    result.m[1][1] = 1.0f;
    result.m[2][2] = 1.0f;
    result.m[3][3] = 1.0f;
    return result;
}

VrTestMatrix VR_TestMultiply(
    const VrTestMatrix& left,
    const VrTestMatrix& right)
{
    VrTestMatrix result = {};

    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int element = 0; element < 4; ++element)
            {
                result.m[row][column] +=
                    left.m[row][element] *
                    right.m[element][column];
            }
        }
    }

    return result;
}

VrTestMatrix VR_TestTranslation(
    const float x,
    const float y,
    const float z)
{
    VrTestMatrix result = VR_TestIdentity();
    result.m[3][0] = x;
    result.m[3][1] = y;
    result.m[3][2] = z;
    return result;
}

VrTestMatrix VR_TestRotation(
    const XrQuaternionf& q)
{
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float xw = q.x * q.w;
    const float yw = q.y * q.w;
    const float zw = q.z * q.w;

    VrTestMatrix result = VR_TestIdentity();

    result.m[0][0] = 1.0f - 2.0f * (yy + zz);
    result.m[0][1] = 2.0f * (xy + zw);
    result.m[0][2] = 2.0f * (xz - yw);

    result.m[1][0] = 2.0f * (xy - zw);
    result.m[1][1] = 1.0f - 2.0f * (xx + zz);
    result.m[1][2] = 2.0f * (yz + xw);

    result.m[2][0] = 2.0f * (xz + yw);
    result.m[2][1] = 2.0f * (yz - xw);
    result.m[2][2] = 1.0f - 2.0f * (xx + yy);

    return result;
}

VrTestMatrix VR_TestView(const XrPosef& pose)
{
    const XrQuaternionf inverseOrientation = {
        -pose.orientation.x,
        -pose.orientation.y,
        -pose.orientation.z,
        pose.orientation.w,
    };

    return VR_TestMultiply(
        VR_TestTranslation(
            -pose.position.x,
            -pose.position.y,
            -pose.position.z),
        VR_TestRotation(inverseOrientation));
}

VrTestMatrix VR_TestProjection(
    const XrFovf& fov,
    const float nearDistance,
    const float farDistance)
{
    const float left = std::tan(fov.angleLeft);
    const float right = std::tan(fov.angleRight);
    const float down = std::tan(fov.angleDown);
    const float up = std::tan(fov.angleUp);

    const float width = right - left;
    const float height = up - down;

    VrTestMatrix result = {};

    result.m[0][0] = 2.0f / width;
    result.m[1][1] = 2.0f / height;
    result.m[2][0] = (right + left) / width;
    result.m[2][1] = (up + down) / height;
    result.m[2][2] =
        -farDistance / (farDistance - nearDistance);
    result.m[2][3] = -1.0f;
    result.m[3][2] =
        -(farDistance * nearDistance) /
        (farDistance - nearDistance);

    return result;
}

using VrD3DCompileFunction = HRESULT (WINAPI*)(
    LPCVOID,
    SIZE_T,
    LPCSTR,
    const D3D_SHADER_MACRO*,
    ID3DInclude*,
    LPCSTR,
    LPCSTR,
    UINT,
    UINT,
    ID3DBlob**,
    ID3DBlob**);

bool VR_TestCompileShader(
    const char* source,
    const char* entryPoint,
    const char* target,
    ComPtr<ID3DBlob>& output)
{
    const char* compilerDlls[] = {
        "d3dcompiler_47.dll",
        "d3dcompiler_46.dll",
        "d3dcompiler_43.dll",
    };

    HMODULE module = nullptr;

    for (const char* dllName : compilerDlls)
    {
        module = LoadLibraryA(dllName);

        if (module != nullptr)
        {
            break;
        }
    }

    if (module == nullptr)
    {
        Com_PrintWarning(
            0,
            "[VR] No D3DCompiler DLL could be loaded.\n");
        return false;
    }

    const auto compile =
        reinterpret_cast<VrD3DCompileFunction>(
            GetProcAddress(module, "D3DCompile"));

    if (compile == nullptr)
    {
        // Keep the D3DCompiler module loaded for the process lifetime.
// ID3DBlob instances returned by D3DCompile are implemented by this
// DLL and may still be alive after this function returns.
        Com_PrintWarning(
            0,
            "[VR] D3DCompile export was not found.\n");
        return false;
    }

    ComPtr<ID3DBlob> errors;

    const HRESULT hr =
        compile(
            source,
            std::strlen(source),
            "KisakCOD VR test shader",
            nullptr,
            nullptr,
            entryPoint,
            target,
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            output.GetAddressOf(),
            errors.GetAddressOf());

    if (FAILED(hr))
    {
        if (errors)
        {
            Com_PrintWarning(
                0,
                "[VR] Shader compiler output: %s\n",
                static_cast<const char*>(
                    errors->GetBufferPointer()));
        }

        VR_LogHrFailure("D3DCompile", hr);
        // Keep the D3DCompiler module loaded for the process lifetime.
// ID3DBlob instances returned by D3DCompile are implemented by this
// DLL and may still be alive after this function returns.
        return false;
    }

    // Keep the D3DCompiler module loaded for the process lifetime.
// ID3DBlob instances returned by D3DCompile are implemented by this
// DLL and may still be alive after this function returns.
    return true;
}


struct VrBlitVertex
{
    float position[2];
    float uv[2];
};

struct VrScopeConstants
{
    float lens[4];
    float basis[4];
    float sample[4];
    float bounds[4];
    float viewport[4];
};

struct VrCompositorConstants
{
    float settings[4];
    float fixedScope[4];

    // KISAK_SP_VR_FIXED_SCOPED_TURRET_RUNTIME_V4
    // xy scale source rays around the fixed scope's center.
    // KISAK_SP_VR_FIXED_SCOPE_SHARP_VIEW_AND_TRACER_V5
    // z selects the dedicated fixed-scope source.
    float fixedScopeZoom[4];

    // xy are the dedicated panel center in captured-texture UV; zw are its
    // inset half-extents. They remain zero when the crop fallback is used.
    float fixedScopeSource[4];
};

struct VrFsrConstants
{
    // Source eye origin and dimensions inside the side-by-side texture.
    float inputRect[4];

    // Full source dimensions plus inclusive eye-rectangle maximum.
    float inputSize[4];

    // Output dimensions and their reciprocals.
    float outputSize[4];

    // RCAS strength and reserved values.
    float settings[4];
};

bool VR_CreateCapturedFrameBlitResources()
{
    // KISAK_VR_DEDICATED_SCOPE_CAMERA_V2_SHADER_SPLIT_FIX
    static const char shaderSourcePart1[] = R"(
Texture2D capturedFrame : register(t0);
SamplerState capturedSampler : register(s0);

cbuffer VrCompositorConstants : register(b1)
{
    float4 vrCompositorSettings;
    float4 vrFixedScopeSettings;
    float4 vrFixedScopeZoom;

    // KISAK_SP_VR_FIXED_SCOPE_SHARP_VIEW_AND_TRACER_V5
    float4 vrFixedScopeSource;
};

cbuffer VrFsrConstants : register(b2)
{
    float4 vrFsrInputRect;
    float4 vrFsrInputSize;
    float4 vrFsrOutputSize;
    float4 vrFsrSettings;
};

struct VertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.uv = input.uv;
    return output;
}

// KISAK_SP_VR_FIXED_SCOPED_TURRET_VIEW_FIX_V1
float4 VR_ApplyFixedScopedTurretOverlay(
    float4 color,
    float2 pixelPosition)
{
    if (vrFixedScopeSettings.x <= 0.5f)
    {
        return color;
    }

    // KISAK_SP_VR_FIXED_SCOPE_BINOCULAR_RETICLE_FIX_V2
    // SV_POSITION is in full render-target pixels.  The compositor settings
    // convert it to eye-local UV, while fixedScope.zw stores the center of
    // this eye's asymmetric-frustum viewport.  That viewport center is the
    // projection of CoD4's shared forward aim ray; using it makes the two
    // reticles fuse instead of placing one at each raw texture center.
    const float2 eyeUv =
        pixelPosition *
        vrCompositorSettings.zw;

    // Convert the ray-relative UV to a height-normalized coordinate so the
    // aperture stays circular even when the eye texture is not square.
    float2 scopePosition =
        (eyeUv - vrFixedScopeSettings.zw) * 2.0f;

    scopePosition.x *=
        max(vrFixedScopeSettings.y, 0.01f);

    const float scopeRadius =
        length(scopePosition);

    // A soft one-pixel-scale edge avoids shimmer while retaining CoD4's
    // intended black surround.
    const float outsideScope =
        smoothstep(0.815f, 0.835f, scopeRadius);

    color.rgb = lerp(
        color.rgb,
        float3(0.0f, 0.0f, 0.0f),
        outsideScope);

    const float verticalLine =
        1.0f - smoothstep(
            0.0015f,
            0.0030f,
            abs(scopePosition.x));

    const float horizontalLine =
        1.0f - smoothstep(
            0.0015f,
            0.0030f,
            abs(scopePosition.y));

    const float centerGap =
        smoothstep(
            0.030f,
            0.050f,
            scopeRadius);

    const float reticleLimit =
        1.0f - smoothstep(
            0.700f,
            0.750f,
            scopeRadius);

    const float centerDot =
        1.0f - smoothstep(
            0.0015f,
            0.0035f,
            scopeRadius);

    const float reticle =
        saturate(
            max(
                max(verticalLine, horizontalLine) *
                    centerGap,
                centerDot) *
            reticleLimit *
            (1.0f - outsideScope));

    color.rgb = lerp(
        color.rgb,
        float3(0.01f, 0.01f, 0.01f),
        reticle);

    color.a = 1.0f;
    return color;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    float2 sourceUv = input.uv;
    bool scaleIntoMainStereoRegion = true;

    if (vrFixedScopeSettings.x > 0.5f)
    {
        // KISAK_SP_VR_FIXED_SCOPE_SHARP_VIEW_AND_TRACER_V5
        // A value above 0.5 means the packed square scope camera was rendered
        // at the requested optical FOV. Duplicate that one sharp source into
        // both eyes; the existing overlay projects the reticle itself onto
        // the common binocular forward ray.
        if (vrFixedScopeZoom.z > 0.5f)
        {
            const float eyeLocalX =
                input.uv.x < 0.5f
                    ? input.uv.x * 2.0f
                    : (input.uv.x - 0.5f) * 2.0f;

            const float2 eyeLocalUv =
                float2(eyeLocalX, input.uv.y);

            sourceUv =
                vrFixedScopeSource.xy +
                (eyeLocalUv - float2(0.5f, 0.5f)) *
                    2.0f *
                    vrFixedScopeSource.zw;

            scaleIntoMainStereoRegion = false;
        }
        else
        {
            // Preserve the old crop as a safe fallback when the packed panel
            // is unavailable.
            const float sourceCenterX =
                input.uv.x < 0.5f
                    ? 0.25f
                    : 0.75f;

            const float2 sourceCenter =
                float2(sourceCenterX, 0.5f);

            sourceUv =
                sourceCenter +
                (sourceUv - sourceCenter) *
                    max(
                        vrFixedScopeZoom.xy,
                        float2(0.01f, 0.01f));
        }
    }

    if (scaleIntoMainStereoRegion)
    {
        sourceUv.x *= vrCompositorSettings.y;
    }

    float4 color = capturedFrame.Sample(
        capturedSampler,
        sourceUv);

    color = VR_ApplyFixedScopedTurretOverlay(
        color,
        input.position.xy);

    color.rgb *= vrCompositorSettings.x;
    return color;
}

// FidelityFX Super Resolution 1.0 EASU/RCAS algorithm.
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to inclusion of this notice.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY.

int2 VR_FsrClampSourcePixel(int2 pixel)
{
    const int2 minimumPixel =
        int2(vrFsrInputRect.xy);

    const int2 maximumPixel =
        int2(vrFsrInputSize.zw);

    return clamp(
        pixel,
        minimumPixel,
        maximumPixel);
}

float3 VR_FsrLoadSource(int2 pixel)
{
    return capturedFrame.Load(
        int3(
            VR_FsrClampSourcePixel(pixel),
            0)).rgb;
}

float VR_FsrLuma(float3 color)
{
    return
        color.b * 0.5f +
        color.r * 0.5f +
        color.g;
}

void VR_FsrEasuSet(
    inout float2 direction,
    inout float edgeLength,
    float weight,
    float lumaA,
    float lumaB,
    float lumaC,
    float lumaD,
    float lumaE)
{
    const float dc = lumaD - lumaC;
    const float cb = lumaC - lumaB;
    const float directionX = lumaD - lumaB;

    float lengthX =
        abs(directionX) /
        max(
            max(abs(dc), abs(cb)),
            0.00001f);

    lengthX = saturate(lengthX);
    lengthX *= lengthX;

    const float ec = lumaE - lumaC;
    const float ca = lumaC - lumaA;
    const float directionY = lumaE - lumaA;

    float lengthY =
        abs(directionY) /
        max(
            max(abs(ec), abs(ca)),
            0.00001f);

    lengthY = saturate(lengthY);
    lengthY *= lengthY;

    direction +=
        float2(directionX, directionY) *
        weight;

    edgeLength +=
        (lengthX + lengthY) *
        weight;
}

void VR_FsrEasuTap(
    inout float3 accumulatedColor,
    inout float accumulatedWeight,
    float2 offset,
    float2 direction,
    float2 anisotropicLength,
    float negativeLobe,
    float clippingPoint,
    float3 color)
{
    float2 rotatedOffset = float2(
        offset.x * direction.x +
            offset.y * direction.y,
        offset.x * -direction.y +
            offset.y * direction.x);

    rotatedOffset *= anisotropicLength;

    float distanceSquared =
        dot(rotatedOffset, rotatedOffset);

    distanceSquared =
        min(distanceSquared, clippingPoint);

    float baseWindow =
        0.4f * distanceSquared -
        1.0f;

    float lobeWindow =
        negativeLobe * distanceSquared -
        1.0f;

    baseWindow *= baseWindow;
    lobeWindow *= lobeWindow;

    baseWindow =
        1.5625f * baseWindow -
        0.5625f;

    const float weight =
        baseWindow * lobeWindow;

    accumulatedColor +=
        color * weight;

    accumulatedWeight +=
        weight;
}
)"
    // KISAK_SP_VR_FIXED_SCOPE_MSVC_SHADER_LITERAL_SPLIT_V1
    R"(
float4 PSEasu(PixelInput input) : SV_TARGET
{
    const float2 sourceToOutput =
        vrFsrInputRect.zw /
        vrFsrOutputSize.xy;

    float2 sourcePosition =
        input.position.xy *
            sourceToOutput -
        0.5f +
        vrFsrInputRect.xy;

    const float2 sourceFloor =
        floor(sourcePosition);

    const float2 fractionalPosition =
        sourcePosition - sourceFloor;

    const int2 basePixel =
        int2(sourceFloor);

    const float3 b =
        VR_FsrLoadSource(basePixel + int2(0, -1));
    const float3 c =
        VR_FsrLoadSource(basePixel + int2(1, -1));
    const float3 e =
        VR_FsrLoadSource(basePixel + int2(-1, 0));
    const float3 f =
        VR_FsrLoadSource(basePixel + int2(0, 0));
    const float3 g =
        VR_FsrLoadSource(basePixel + int2(1, 0));
    const float3 h =
        VR_FsrLoadSource(basePixel + int2(2, 0));
    const float3 i =
        VR_FsrLoadSource(basePixel + int2(-1, 1));
    const float3 j =
        VR_FsrLoadSource(basePixel + int2(0, 1));
    const float3 k =
        VR_FsrLoadSource(basePixel + int2(1, 1));
    const float3 l =
        VR_FsrLoadSource(basePixel + int2(2, 1));
    const float3 n =
        VR_FsrLoadSource(basePixel + int2(0, 2));
    const float3 o =
        VR_FsrLoadSource(basePixel + int2(1, 2));

    const float bL = VR_FsrLuma(b);
    const float cL = VR_FsrLuma(c);
    const float eL = VR_FsrLuma(e);
    const float fL = VR_FsrLuma(f);
    const float gL = VR_FsrLuma(g);
    const float hL = VR_FsrLuma(h);
    const float iL = VR_FsrLuma(i);
    const float jL = VR_FsrLuma(j);
    const float kL = VR_FsrLuma(k);
    const float lL = VR_FsrLuma(l);
    const float nL = VR_FsrLuma(n);
    const float oL = VR_FsrLuma(o);

    float2 direction = float2(0.0f, 0.0f);
    float edgeLength = 0.0f;

    VR_FsrEasuSet(
        direction,
        edgeLength,
        (1.0f - fractionalPosition.x) *
            (1.0f - fractionalPosition.y),
        bL, eL, fL, gL, jL);

    VR_FsrEasuSet(
        direction,
        edgeLength,
        fractionalPosition.x *
            (1.0f - fractionalPosition.y),
        cL, fL, gL, hL, kL);

    VR_FsrEasuSet(
        direction,
        edgeLength,
        (1.0f - fractionalPosition.x) *
            fractionalPosition.y,
        fL, iL, jL, kL, nL);

    VR_FsrEasuSet(
        direction,
        edgeLength,
        fractionalPosition.x *
            fractionalPosition.y,
        gL, jL, kL, lL, oL);

    const float directionMagnitudeSquared =
        dot(direction, direction);

    if (directionMagnitudeSquared <
        (1.0f / 32768.0f))
    {
        direction = float2(1.0f, 0.0f);
    }
    else
    {
        direction *=
            rsqrt(directionMagnitudeSquared);
    }

    edgeLength *= 0.5f;
    edgeLength *= edgeLength;

    const float stretch =
        dot(direction, direction) /
        max(
            max(abs(direction.x), abs(direction.y)),
            0.00001f);

    const float2 anisotropicLength = float2(
        1.0f +
            (stretch - 1.0f) * edgeLength,
        1.0f -
            0.5f * edgeLength);

    const float negativeLobe =
        0.5f -
        0.29f * edgeLength;

    const float clippingPoint =
        1.0f /
        max(negativeLobe, 0.00001f);

    const float3 minimumColor =
        min(min(f, g), min(j, k));

    const float3 maximumColor =
        max(max(f, g), max(j, k));

    float3 accumulatedColor =
        float3(0.0f, 0.0f, 0.0f);

    float accumulatedWeight = 0.0f;

    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(0.0f, -1.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, b);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(1.0f, -1.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, c);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(-1.0f, 1.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, i);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(0.0f, 1.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, j);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(0.0f, 0.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, f);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(-1.0f, 0.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, e);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(1.0f, 1.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, k);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(2.0f, 1.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, l);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(2.0f, 0.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, h);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(1.0f, 0.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, g);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(1.0f, 2.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, o);
    VR_FsrEasuTap(accumulatedColor, accumulatedWeight,
        float2(0.0f, 2.0f) - fractionalPosition,
        direction, anisotropicLength, negativeLobe, clippingPoint, n);

    const float3 upscaledColor =
        min(
            maximumColor,
            max(
                minimumColor,
                accumulatedColor /
                    max(accumulatedWeight, 0.00001f)));

    return float4(upscaledColor, 1.0f);
}

int2 VR_FsrClampOutputPixel(int2 pixel)
{
    return clamp(
        pixel,
        int2(0, 0),
        int2(vrFsrOutputSize.xy) - int2(1, 1));
}

float3 VR_FsrLoadUpscaled(int2 pixel)
{
    return capturedFrame.Load(
        int3(
            VR_FsrClampOutputPixel(pixel),
            0)).rgb;
}

float4 PSRcas(PixelInput input) : SV_TARGET
{
    const int2 centerPixel =
        int2(
            floor(
                input.uv *
                vrFsrOutputSize.xy));

    const float3 north =
        VR_FsrLoadUpscaled(centerPixel + int2(0, -1));
    const float3 west =
        VR_FsrLoadUpscaled(centerPixel + int2(-1, 0));
    const float3 center =
        VR_FsrLoadUpscaled(centerPixel);
    const float3 east =
        VR_FsrLoadUpscaled(centerPixel + int2(1, 0));
    const float3 south =
        VR_FsrLoadUpscaled(centerPixel + int2(0, 1));

    const float northLuma = VR_FsrLuma(north);
    const float westLuma = VR_FsrLuma(west);
    const float centerLuma = VR_FsrLuma(center);
    const float eastLuma = VR_FsrLuma(east);
    const float southLuma = VR_FsrLuma(south);

    const float minimumLuma =
        min(
            min(min(northLuma, westLuma), centerLuma),
            min(eastLuma, southLuma));

    const float maximumLuma =
        max(
            max(max(northLuma, westLuma), centerLuma),
            max(eastLuma, southLuma));

    const float noise = saturate(
        abs(
            0.25f *
                (northLuma + westLuma + eastLuma + southLuma) -
            centerLuma) /
        max(maximumLuma - minimumLuma, 0.00001f));

    const float3 minimumRing =
        min(min(north, west), min(east, south));

    const float3 maximumRing =
        max(max(north, west), max(east, south));

    const float3 hitMinimum =
        min(minimumRing, center) /
        max(
            4.0f * maximumRing,
            float3(0.00001f, 0.00001f, 0.00001f));

    const float3 hitMaximum =
        (1.0f - max(maximumRing, center)) /
        min(
            4.0f * minimumRing - 4.0f,
            float3(-0.00001f, -0.00001f, -0.00001f));

    const float3 lobePerChannel =
        max(-hitMinimum, hitMaximum);

    float lobe =
        max(
            -0.1875f,
            min(
                max(
                    max(lobePerChannel.r, lobePerChannel.g),
                    lobePerChannel.b),
                0.0f));

    lobe *=
        saturate(vrFsrSettings.x) *
        (1.0f - 0.5f * noise);

    float3 sharpenedColor =
        (lobe *
            (north + west + east + south) +
         center) /
        max(4.0f * lobe + 1.0f, 0.00001f);

    float4 outputColor =
        float4(
            saturate(sharpenedColor),
            1.0f);

    // Apply the same render-target-centered fixed-scope mask after
    // sharpening.
    outputColor =
        VR_ApplyFixedScopedTurretOverlay(
            outputColor,
            input.position.xy);

    outputColor.rgb *=
        vrCompositorSettings.x;

    return outputColor;
}
)";

    static const char shaderSourcePart2[] = R"(
cbuffer ScopeConstants : register(b0)
{
    float4 scopeLens;
    float4 scopeBasis;
    float4 scopeSample;
    float4 scopeBounds;
    float4 scopeViewport;
};

float4 PSScope(PixelInput input) : SV_TARGET
{
    const float2 outputUv =
        input.position.xy * scopeViewport.xy;

    const float2 screenDelta =
        outputUv - scopeLens.xy;

    const float determinant =
        scopeBasis.x * scopeBasis.w -
        scopeBasis.z * scopeBasis.y;

    if (abs(determinant) < 0.00001f)
    {
        discard;
    }

    const float2 lensDelta = float2(
        (screenDelta.x * scopeBasis.w -
         screenDelta.y * scopeBasis.z) /
            determinant,
        (-screenDelta.x * scopeBasis.y +
         screenDelta.y * scopeBasis.x) /
            determinant);

    const float lensRadius = length(lensDelta);

    if (lensRadius > 1.0f)
    {
        discard;
    }

    float2 sourceUv = float2(0.0f, 0.0f);

    if (scopeLens.w > 0.5f)
    {
        // KISAK_VR_DEDICATED_SCOPE_CAMERA_V2_VERTICAL_FLIP_FIX
        const float2 dedicatedScopeLensDelta =
            float2(
                lensDelta.x,
                -lensDelta.y);

        sourceUv = clamp(
            scopeSample.xy +
                dedicatedScopeLensDelta *
                    scopeSample.zw,
            scopeBounds.xz,
            scopeBounds.yw);
    }
    else
    {
        sourceUv = clamp(
            scopeSample.xy +
                screenDelta *
                    float2(0.5f, 1.0f) *
                    scopeSample.z,
            scopeBounds.xz,
            scopeBounds.yw);
    }

    float2 normalUv = input.uv;
    normalUv.x *= vrCompositorSettings.y;

    const float4 normalColor =
        capturedFrame.Sample(
            capturedSampler,
            normalUv);

    const float2 sourceTexel =
        scopeViewport.zw;

    const float4 magnifiedCenter =
        capturedFrame.Sample(
            capturedSampler,
            sourceUv);

    const float3 magnifiedNeighbors =
        capturedFrame.Sample(
            capturedSampler,
            sourceUv + float2(sourceTexel.x, 0.0f)).rgb +
        capturedFrame.Sample(
            capturedSampler,
            sourceUv - float2(sourceTexel.x, 0.0f)).rgb +
        capturedFrame.Sample(
            capturedSampler,
            sourceUv + float2(0.0f, sourceTexel.y)).rgb +
        capturedFrame.Sample(
            capturedSampler,
            sourceUv - float2(0.0f, sourceTexel.y)).rgb;

    float3 sharpenedScope =
        magnifiedCenter.rgb * 1.72f -
        magnifiedNeighbors * 0.18f;

    sharpenedScope = saturate(
        (sharpenedScope - 0.5f) * 1.06f +
        0.5f);

    const float4 magnifiedColor =
        float4(
            sharpenedScope,
            magnifiedCenter.a);

    const float opacity = saturate(scopeLens.z);

    float4 color = lerp(
        normalColor,
        magnifiedColor,
        opacity);

    const float edge =
        smoothstep(0.82f, 1.0f, lensRadius) *
        opacity;

    color.rgb = lerp(
        color.rgb,
        float3(0.005f, 0.007f, 0.009f),
        edge);

    const float centerGap =
        smoothstep(0.055f, 0.09f, lensRadius);

    const float verticalLine =
        1.0f - smoothstep(
            0.008f,
            0.016f,
            abs(lensDelta.x));

    const float horizontalLine =
        1.0f - smoothstep(
            0.008f,
            0.016f,
            abs(lensDelta.y));

    const float reticle =
        max(verticalLine, horizontalLine) *
        centerGap *
        (1.0f - smoothstep(
            0.72f,
            0.88f,
            lensRadius)) *
        opacity;

    color.rgb = lerp(
        color.rgb,
        float3(0.01f, 0.01f, 0.01f),
        reticle);

    color.rgb *= vrCompositorSettings.x;
    color.a = 1.0f;
    return color;
}
)";

    std::vector<char> shaderSourceStorage;
    shaderSourceStorage.reserve(
        sizeof(shaderSourcePart1) +
        sizeof(shaderSourcePart2) - 1u);

    shaderSourceStorage.insert(
        shaderSourceStorage.end(),
        shaderSourcePart1,
        shaderSourcePart1 + sizeof(shaderSourcePart1) - 1u);

    shaderSourceStorage.insert(
        shaderSourceStorage.end(),
        shaderSourcePart2,
        shaderSourcePart2 + sizeof(shaderSourcePart2) - 1u);

    shaderSourceStorage.push_back('\0');

    const char* shaderSource =
        shaderSourceStorage.data();

    ComPtr<ID3DBlob> vertexCode;
    ComPtr<ID3DBlob> pixelCode;
    ComPtr<ID3DBlob> easuPixelCode;
    ComPtr<ID3DBlob> rcasPixelCode;
    ComPtr<ID3DBlob> scopePixelCode;

    if (!VR_TestCompileShader(
            shaderSource,
            "VSMain",
            "vs_4_0",
            vertexCode))
    {
        return false;
    }

    if (!VR_TestCompileShader(
            shaderSource,
            "PSMain",
            "ps_4_0",
            pixelCode))
    {
        return false;
    }

    g_vrFsrShadersAvailable = false;

    const bool fsrShadersCompiled =
        VR_TestCompileShader(
            shaderSource,
            "PSEasu",
            "ps_4_0",
            easuPixelCode) &&
        VR_TestCompileShader(
            shaderSource,
            "PSRcas",
            "ps_4_0",
            rcasPixelCode);

    if (!fsrShadersCompiled)
    {
        Com_PrintWarning(
            0,
            "[VR] FSR shaders could not be compiled; "
            "using the bilinear compositor fallback.\n");
    }

    if (!VR_TestCompileShader(
            shaderSource,
            "PSScope",
            "ps_4_0",
            scopePixelCode))
    {
        return false;
    }

    HRESULT hr =
        g_vrD3dDevice->CreateVertexShader(
            vertexCode->GetBufferPointer(),
            vertexCode->GetBufferSize(),
            nullptr,
            g_vrBlitVertexShader.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateVertexShader(capture blit)",
            hr);

        return false;
    }

    hr =
        g_vrD3dDevice->CreatePixelShader(
            pixelCode->GetBufferPointer(),
            pixelCode->GetBufferSize(),
            nullptr,
            g_vrBlitPixelShader.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreatePixelShader(capture blit)",
            hr);

        return false;
    }

    if (fsrShadersCompiled)
    {
        HRESULT fsrShaderHr =
            g_vrD3dDevice->CreatePixelShader(
                easuPixelCode->GetBufferPointer(),
                easuPixelCode->GetBufferSize(),
                nullptr,
                g_vrFsrEasuPixelShader.GetAddressOf());

        if (SUCCEEDED(fsrShaderHr))
        {
            fsrShaderHr =
                g_vrD3dDevice->CreatePixelShader(
                    rcasPixelCode->GetBufferPointer(),
                    rcasPixelCode->GetBufferSize(),
                    nullptr,
                    g_vrFsrRcasPixelShader.GetAddressOf());
        }

        if (FAILED(fsrShaderHr))
        {
            VR_LogHrFailure(
                "CreatePixelShader(FSR EASU/RCAS)",
                fsrShaderHr);
        }
    }

    g_vrFsrShadersAvailable =
        g_vrFsrEasuPixelShader.Get() != nullptr &&
        g_vrFsrRcasPixelShader.Get() != nullptr;

    if (!g_vrFsrShadersAvailable)
    {
        g_vrFsrRcasPixelShader.Reset();
        g_vrFsrEasuPixelShader.Reset();
    }

    hr =
        g_vrD3dDevice->CreatePixelShader(
            scopePixelCode->GetBufferPointer(),
            scopePixelCode->GetBufferSize(),
            nullptr,
            g_vrScopePixelShader.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreatePixelShader(physical scope)",
            hr);

        return false;
    }

    D3D11_BUFFER_DESC scopeConstantDescription = {};
    scopeConstantDescription.ByteWidth =
        static_cast<UINT>(
            sizeof(VrScopeConstants));
    scopeConstantDescription.Usage =
        D3D11_USAGE_DYNAMIC;
    scopeConstantDescription.BindFlags =
        D3D11_BIND_CONSTANT_BUFFER;
    scopeConstantDescription.CPUAccessFlags =
        D3D11_CPU_ACCESS_WRITE;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &scopeConstantDescription,
            nullptr,
            g_vrScopeConstantBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateBuffer(physical scope constants)",
            hr);

        return false;
    }

    const auto readScopeSetting =
        [](
            const char* name,
            const float defaultValue,
            const float minimumValue,
            const float maximumValue)
        {
            const char* requestedValue =
                std::getenv(name);

            if (requestedValue == nullptr ||
                requestedValue[0] == '\0')
            {
                return defaultValue;
            }

            char* parseEnd = nullptr;

            const float parsedValue =
                std::strtof(
                    requestedValue,
                    &parseEnd);

            if (parseEnd == requestedValue ||
                parseEnd == nullptr ||
                parseEnd[0] != '\0' ||
                !std::isfinite(parsedValue) ||
                parsedValue < minimumValue ||
                parsedValue > maximumValue)
            {
                Com_PrintWarning(
                    0,
                    "[VR] Ignoring invalid %s='%s'; using %.3f. "
                    "Valid range is %.3f through %.3f.\n",
                    name,
                    requestedValue,
                    defaultValue,
                    minimumValue,
                    maximumValue);

                return defaultValue;
            }

            return parsedValue;
        };

    g_vrScopeForwardCalibrationMeters =
        readScopeSetting(
            "KISAK_VR_SCOPE_FORWARD_METERS",
            0.0f,
            -0.25f,
            0.25f);

    g_vrScopeLeftCalibrationMeters =
        readScopeSetting(
            "KISAK_VR_SCOPE_LEFT_METERS",
            0.0f,
            -0.25f,
            0.25f);

    g_vrScopeUpCalibrationMeters =
        readScopeSetting(
            "KISAK_VR_SCOPE_UP_METERS",
            0.0f,
            -0.25f,
            0.25f);

    g_vrScopeLensRadiusMeters =
        readScopeSetting(
            "KISAK_VR_SCOPE_RADIUS_METERS",
            0.032f,
            0.015f,
            0.080f);

    g_vrScopeCaptureSizePixels =
        static_cast<int>(
            readScopeSetting(
                "KISAK_VR_SCOPE_CAPTURE_SIZE",
                1024.0f,
                512.0f,
                1536.0f));

    const VrMeasurementUnitSystem units =
        VR_GetConfiguratorSettings().measurementUnits;
    Com_Printf(
        0,
        "[VR] Rifle-attached scope calibration: "
        "forward %.2f, left %.2f, up %.2f, radius %.2f %s; "
        "dedicated source %d px.\n",
        VR_DisplayMeters(g_vrScopeForwardCalibrationMeters, units),
        VR_DisplayMeters(g_vrScopeLeftCalibrationMeters, units),
        VR_DisplayMeters(g_vrScopeUpCalibrationMeters, units),
        VR_DisplayMeters(g_vrScopeLensRadiusMeters, units),
        VR_DisplayLengthUnit(units),
        g_vrScopeCaptureSizePixels);

    constexpr float defaultBrightness = 1.0f;
    constexpr float minimumBrightness = 0.20f;
    constexpr float maximumBrightness = 1.0f;

    g_vrCompositorBrightness = defaultBrightness;

    const char* requestedBrightness =
        std::getenv("KISAK_VR_BRIGHTNESS");

    if (requestedBrightness != nullptr &&
        requestedBrightness[0] != '\0')
    {
        char* parseEnd = nullptr;

        const float parsedBrightness =
            std::strtof(
                requestedBrightness,
                &parseEnd);

        if (parseEnd == requestedBrightness ||
            parseEnd == nullptr ||
            parseEnd[0] != '\0' ||
            !std::isfinite(parsedBrightness) ||
            parsedBrightness < minimumBrightness ||
            parsedBrightness > maximumBrightness)
        {
            Com_PrintWarning(
                0,
                "[VR] Ignoring invalid KISAK_VR_BRIGHTNESS='%s'; "
                "using %.2f. Valid range is %.2f through %.2f.\n",
                requestedBrightness,
                defaultBrightness,
                minimumBrightness,
                maximumBrightness);
        }
        else
        {
            g_vrCompositorBrightness =
                parsedBrightness;
        }
    }

    VrCompositorConstants compositorConstants = {};
    compositorConstants.settings[0] =
        g_vrCompositorBrightness;
    compositorConstants.settings[1] = 1.0f;

    D3D11_BUFFER_DESC compositorConstantDescription = {};
    compositorConstantDescription.ByteWidth =
        static_cast<UINT>(
            sizeof(VrCompositorConstants));
    compositorConstantDescription.Usage =
        D3D11_USAGE_DYNAMIC;
    compositorConstantDescription.BindFlags =
        D3D11_BIND_CONSTANT_BUFFER;
    compositorConstantDescription.CPUAccessFlags =
        D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA compositorConstantData = {};
    compositorConstantData.pSysMem =
        &compositorConstants;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &compositorConstantDescription,
            &compositorConstantData,
            g_vrCompositorConstantBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateBuffer(VR compositor brightness)",
            hr);

        return false;
    }

    Com_Printf(
        0,
        "[VR] OpenXR compositor brightness scale is %.2f.\n",
        g_vrCompositorBrightness);

    g_vrFsrEnabled =
        g_vrFsrShadersAvailable;

    const char* requestedFsr =
        std::getenv("KISAK_VR_FSR");

    if (requestedFsr != nullptr &&
        requestedFsr[0] != '\0')
    {
        if (std::strcmp(requestedFsr, "0") == 0)
        {
            g_vrFsrEnabled = false;
        }
        else if (std::strcmp(requestedFsr, "1") == 0 &&
                 !g_vrFsrShadersAvailable)
        {
            Com_PrintWarning(
                0,
                "[VR] KISAK_VR_FSR=1 was requested, but the "
                "FSR shader path is unavailable; using bilinear.\n");
        }
        else if (std::strcmp(requestedFsr, "1") != 0)
        {
            Com_PrintWarning(
                0,
                "[VR] Ignoring invalid KISAK_VR_FSR='%s'; "
                "using 1. Valid values are 0 and 1.\n",
                requestedFsr);
        }
    }

    constexpr float defaultFsrSharpness = 0.60f;
    constexpr float minimumFsrSharpness = 0.00f;
    constexpr float maximumFsrSharpness = 1.00f;

    g_vrFsrSharpness =
        defaultFsrSharpness;

    const char* requestedFsrSharpness =
        std::getenv("KISAK_VR_FSR_SHARPNESS");

    if (requestedFsrSharpness != nullptr &&
        requestedFsrSharpness[0] != '\0')
    {
        char* parseEnd = nullptr;

        const float parsedFsrSharpness =
            std::strtof(
                requestedFsrSharpness,
                &parseEnd);

        if (parseEnd == requestedFsrSharpness ||
            parseEnd == nullptr ||
            parseEnd[0] != '\0' ||
            !std::isfinite(parsedFsrSharpness) ||
            parsedFsrSharpness < minimumFsrSharpness ||
            parsedFsrSharpness > maximumFsrSharpness)
        {
            Com_PrintWarning(
                0,
                "[VR] Ignoring invalid "
                "KISAK_VR_FSR_SHARPNESS='%s'; using %.2f. "
                "Valid range is %.2f through %.2f.\n",
                requestedFsrSharpness,
                defaultFsrSharpness,
                minimumFsrSharpness,
                maximumFsrSharpness);
        }
        else
        {
            g_vrFsrSharpness =
                parsedFsrSharpness;
        }
    }

    if (g_vrFsrEnabled)
    {
        D3D11_BUFFER_DESC fsrConstantDescription = {};
        fsrConstantDescription.ByteWidth =
            static_cast<UINT>(
                sizeof(VrFsrConstants));
        fsrConstantDescription.Usage =
            D3D11_USAGE_DYNAMIC;
        fsrConstantDescription.BindFlags =
            D3D11_BIND_CONSTANT_BUFFER;
        fsrConstantDescription.CPUAccessFlags =
            D3D11_CPU_ACCESS_WRITE;

        const HRESULT fsrConstantHr =
            g_vrD3dDevice->CreateBuffer(
                &fsrConstantDescription,
                nullptr,
                g_vrFsrConstantBuffer.GetAddressOf());

        if (FAILED(fsrConstantHr))
        {
            VR_LogHrFailure(
                "CreateBuffer(FSR constants)",
                fsrConstantHr);

            g_vrFsrConstantBuffer.Reset();
            g_vrFsrEnabled = false;

            Com_PrintWarning(
                0,
                "[VR] FSR constant-buffer allocation failed; "
                "using the bilinear compositor fallback.\n");
        }
    }

    if (g_vrFsrEnabled)
    {
        std::uint32_t intermediateWidth = 0u;
        std::uint32_t intermediateHeight = 0u;

        for (const VrEyeSwapchain& eyeSwapchain :
             g_vrEyeSwapchains)
        {
            intermediateWidth =
                (std::max)(
                    intermediateWidth,
                    static_cast<std::uint32_t>(
                        eyeSwapchain.width));

            intermediateHeight =
                (std::max)(
                    intermediateHeight,
                    static_cast<std::uint32_t>(
                        eyeSwapchain.height));
        }

        // OpenVR owns persistent eye textures instead of OpenXR
        // swapchains. Reuse their extent so the established FSR path does
        // not attempt a zero-sized intermediate on the fallback backend.
        for (const VrOpenVrEyeTarget& eyeTarget :
             g_vrOpenVrEyeTargets)
        {
            if (eyeTarget.width > 0 &&
                eyeTarget.height > 0)
            {
                intermediateWidth =
                    (std::max)(
                        intermediateWidth,
                        static_cast<std::uint32_t>(
                            eyeTarget.width));

                intermediateHeight =
                    (std::max)(
                        intermediateHeight,
                        static_cast<std::uint32_t>(
                            eyeTarget.height));
            }
        }

        D3D11_TEXTURE2D_DESC intermediateDescription = {};
        intermediateDescription.Width =
            intermediateWidth;
        intermediateDescription.Height =
            intermediateHeight;
        intermediateDescription.MipLevels = 1;
        intermediateDescription.ArraySize = 1;
        intermediateDescription.Format =
            DXGI_FORMAT_R8G8B8A8_UNORM;
        intermediateDescription.SampleDesc.Count = 1;
        intermediateDescription.Usage =
            D3D11_USAGE_DEFAULT;
        intermediateDescription.BindFlags =
            D3D11_BIND_RENDER_TARGET |
            D3D11_BIND_SHADER_RESOURCE;

        HRESULT fsrHr =
            g_vrD3dDevice->CreateTexture2D(
                &intermediateDescription,
                nullptr,
                g_vrFsrIntermediateTexture.GetAddressOf());

        if (SUCCEEDED(fsrHr))
        {
            fsrHr =
                g_vrD3dDevice->CreateRenderTargetView(
                    g_vrFsrIntermediateTexture.Get(),
                    nullptr,
                    g_vrFsrIntermediateTarget.GetAddressOf());
        }

        if (SUCCEEDED(fsrHr))
        {
            fsrHr =
                g_vrD3dDevice->CreateShaderResourceView(
                    g_vrFsrIntermediateTexture.Get(),
                    nullptr,
                    g_vrFsrIntermediateView.GetAddressOf());
        }

        if (FAILED(fsrHr))
        {
            VR_LogHrFailure(
                "CreateTexture2D(FSR intermediate)",
                fsrHr);

            g_vrFsrIntermediateView.Reset();
            g_vrFsrIntermediateTarget.Reset();
            g_vrFsrIntermediateTexture.Reset();
            g_vrFsrEnabled = false;

            Com_PrintWarning(
                0,
                "[VR] FSR intermediate allocation failed; "
                "using the bilinear compositor fallback.\n");
        }
        else
        {
            Com_Printf(
                0,
                "[VR] Created one reusable FSR intermediate: "
                "%u x %u. RCAS strength %.2f.\n",
                intermediateWidth,
                intermediateHeight,
                g_vrFsrSharpness);
        }
    }
    else
    {
        if (g_vrFsrShadersAvailable)
        {
            Com_Printf(
                0,
                "[VR] FSR compositor disabled or unavailable; "
                "using bilinear output.\n");
        }
        else
        {
            Com_PrintWarning(
                0,
                "[VR] FSR shaders are unavailable; "
                "using bilinear output.\n");
        }
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            0,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
        {
            "TEXCOORD",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            8,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
    };

    hr =
        g_vrD3dDevice->CreateInputLayout(
            layout,
            2,
            vertexCode->GetBufferPointer(),
            vertexCode->GetBufferSize(),
            g_vrBlitInputLayout.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateInputLayout(capture blit)",
            hr);

        return false;
    }

    static const VrBlitVertex eyeVertices[2][4] = {
        {
            {{-1.0f,  1.0f}, {0.0f, 0.0f}},
            {{ 1.0f,  1.0f}, {0.5f, 0.0f}},
            {{-1.0f, -1.0f}, {0.0f, 1.0f}},
            {{ 1.0f, -1.0f}, {0.5f, 1.0f}},
        },
        {
            {{-1.0f,  1.0f}, {0.5f, 0.0f}},
            {{ 1.0f,  1.0f}, {1.0f, 0.0f}},
            {{-1.0f, -1.0f}, {0.5f, 1.0f}},
            {{ 1.0f, -1.0f}, {1.0f, 1.0f}},
        },
    };

    D3D11_BUFFER_DESC vertexDescription = {};
    vertexDescription.ByteWidth =
        static_cast<UINT>(
            sizeof(eyeVertices[0]));
    vertexDescription.Usage =
        D3D11_USAGE_IMMUTABLE;
    vertexDescription.BindFlags =
        D3D11_BIND_VERTEX_BUFFER;

    for (std::uint32_t eyeIndex = 0;
         eyeIndex < 2u;
         ++eyeIndex)
    {
        D3D11_SUBRESOURCE_DATA vertexData = {};
        vertexData.pSysMem =
            eyeVertices[eyeIndex];

        hr =
            g_vrD3dDevice->CreateBuffer(
                &vertexDescription,
                &vertexData,
                g_vrBlitVertexBuffers[eyeIndex]
                    .GetAddressOf());

        if (FAILED(hr))
        {
            VR_LogHrFailure(
                "CreateBuffer(stereo capture blit)",
                hr);

            return false;
        }
    }

    static const VrBlitVertex menuVertices[4] = {
        {{-1.0f,  1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f}, {0.5f, 0.0f}},
        {{-1.0f, -1.0f}, {0.0f, 1.0f}},
        {{ 1.0f, -1.0f}, {0.5f, 1.0f}},
    };

    D3D11_SUBRESOURCE_DATA menuVertexData = {};
    menuVertexData.pSysMem =
        menuVertices;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &vertexDescription,
            &menuVertexData,
            g_vrMenuBlitVertexBuffer
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateBuffer(eye-local frontend menu blit)",
            hr);

        return false;
    }

    static const VrBlitVertex pauseMenuVertices[4] = {
        {{-1.0f,  1.0f}, {0.5f, 0.0f}},
        {{ 1.0f,  1.0f}, {1.0f, 0.0f}},
        {{-1.0f, -1.0f}, {0.5f, 1.0f}},
        {{ 1.0f, -1.0f}, {1.0f, 1.0f}},
    };

    D3D11_SUBRESOURCE_DATA pauseMenuVertexData = {};
    pauseMenuVertexData.pSysMem =
        pauseMenuVertices;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &vertexDescription,
            &pauseMenuVertexData,
            g_vrPauseMenuBlitVertexBuffer
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateBuffer(pause menu mono blit)",
            hr);

        return false;
    }

    static const VrBlitVertex centeredModalVertices[4] = {
        {{-1.0f,  1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f}, {0.5f, 0.0f}},
        {{-1.0f, -1.0f}, {0.0f, 1.0f}},
        {{ 1.0f, -1.0f}, {0.5f, 1.0f}},
    };

    D3D11_SUBRESOURCE_DATA centeredModalVertexData = {};
    centeredModalVertexData.pSysMem =
        centeredModalVertices;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &vertexDescription,
            &centeredModalVertexData,
            g_vrCenteredModalBlitVertexBuffer
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateBuffer(eye-local shared modal blit)",
            hr);

        return false;
    }

    D3D11_SAMPLER_DESC samplerDescription = {};
    samplerDescription.Filter =
        D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.MaxLOD =
        D3D11_FLOAT32_MAX;

    hr =
        g_vrD3dDevice->CreateSamplerState(
            &samplerDescription,
            g_vrBlitSampler.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateSamplerState(capture blit)",
            hr);

        return false;
    }

    Com_Printf(
        0,
        "[VR] D3D9 captured-frame blit resources created.\n");

    return true;
}

std::uint32_t VR_GetCapturedMainStereoWidth()
{
    int mainStereoWidth = 0;
    int scopePanelX = 0;
    int scopePanelY = 0;
    int scopePanelSize = 0;

    if (VR_GetPhysicalSniperScopeCaptureLayout(
            static_cast<int>(
                g_vrCapturedStereoWidth),
            static_cast<int>(
                g_vrCapturedStereoHeight),
            &mainStereoWidth,
            &scopePanelX,
            &scopePanelY,
            &scopePanelSize))
    {
        return static_cast<std::uint32_t>(
            mainStereoWidth);
    }

    return g_vrCapturedStereoWidth;
}

// KISAK_SP_VR_FIXED_SCOPE_BINOCULAR_RETICLE_FIX_V2
void VR_UpdateCompositorConstantsForEye(
    const D3D11_VIEWPORT& eyeViewport,
    const int eyeWidth,
    const int eyeHeight)
{
    if (!g_vrCompositorConstantBuffer ||
        !g_vrD3dContext ||
        eyeWidth <= 0 ||
        eyeHeight <= 0)
    {
        return;
    }

    float mainStereoFraction = 1.0f;

    if (g_vrCapturedStereoWidth > 0u)
    {
        mainStereoFraction =
            static_cast<float>(
                VR_GetCapturedMainStereoWidth()) /
            static_cast<float>(
                g_vrCapturedStereoWidth);
    }

    VrCompositorConstants constants = {};
    constants.settings[0] =
        g_vrCompositorBrightness;
    constants.settings[1] =
        mainStereoFraction;

    // zw convert SV_POSITION from full-eye pixels to normalized eye UV.
    constants.settings[2] =
        1.0f /
        static_cast<float>(eyeWidth);
    constants.settings[3] =
        1.0f /
        static_cast<float>(eyeHeight);

    bool fixedScopedTurretActive = false;
    float fixedScopedTurretZoomFovDegrees = 20.0f;
    float fixedScopedTurretMaximumZoomFovDegrees = 20.0f;

    {
        std::lock_guard<std::mutex> lock(
            g_vrScopeStateMutex);

        fixedScopedTurretActive =
            g_vrFixedScopedTurretActive;

        fixedScopedTurretZoomFovDegrees =
            g_vrFixedScopedTurretZoomFovDegrees;

        fixedScopedTurretMaximumZoomFovDegrees =
            g_vrFixedScopedTurretMaximumZoomFovDegrees;
    }

    // x selects the compositor-owned fixed scope.  y converts normalized
    // horizontal distance to height-normalized distance.  zw hold the
    // eye-local UV of CoD4's centered source ray after the symmetric source
    // is remapped into this eye's asymmetric OpenXR frustum.
    constants.fixedScope[0] =
        fixedScopedTurretActive
            ? 1.0f
            : 0.0f;

    constants.fixedScope[1] =
        static_cast<float>(eyeWidth) /
        static_cast<float>(eyeHeight);

    constants.fixedScope[2] =
        (
            eyeViewport.TopLeftX +
            0.5f * eyeViewport.Width
        ) /
        static_cast<float>(eyeWidth);

    constants.fixedScope[3] =
        (
            eyeViewport.TopLeftY +
            0.5f * eyeViewport.Height
        ) /
        static_cast<float>(eyeHeight);

    // KISAK_SP_VR_FIXED_SCOPED_TURRET_RUNTIME_V4
    // CoD's turretScopeZoom is a 4:3 horizontal FOV. The ratio of tangents
    // is the exact rectilinear sample scale, and the same ratio applies to
    // both axes. At the normal 20-degree setting the scale is 1; at the
    // maximum 5-degree magnification it is about 0.248.
    constexpr float pi =
        3.14159265358979323846f;

    const float currentHalfFovTangent =
        std::tan(
            0.5f *
            fixedScopedTurretZoomFovDegrees *
            (pi / 180.0f));

    const float maximumHalfFovTangent =
        std::tan(
            0.5f *
            fixedScopedTurretMaximumZoomFovDegrees *
            (pi / 180.0f));

    float fixedScopeZoomScale =
        maximumHalfFovTangent > 0.0001f
            ? currentHalfFovTangent /
                maximumHalfFovTangent
            : 1.0f;

    fixedScopeZoomScale =
        (std::max)(
            0.01f,
            (std::min)(
                fixedScopeZoomScale,
                1.0f));

    constants.fixedScopeZoom[0] =
        fixedScopeZoomScale;
    constants.fixedScopeZoom[1] =
        fixedScopeZoomScale;
    constants.fixedScopeZoom[2] = 0.0f;
    constants.fixedScopeZoom[3] = 0.0f;

    // KISAK_SP_VR_FIXED_SCOPE_SHARP_VIEW_AND_TRACER_V5
    // The packed panel is rendered at the current optical FOV, so it needs
    // neither crop magnification nor FSR. Publish its exact captured-texture
    // rectangle to the direct compositor.
    int fixedScopeMainStereoWidth = 0;
    int fixedScopePanelX = 0;
    int fixedScopePanelY = 0;
    int fixedScopePanelSize = 0;

    const bool dedicatedFixedScopeSource =
        fixedScopedTurretActive &&
        VR_GetPhysicalSniperScopeCaptureLayout(
            static_cast<int>(
                g_vrCapturedStereoWidth),
            static_cast<int>(
                g_vrCapturedStereoHeight),
            &fixedScopeMainStereoWidth,
            &fixedScopePanelX,
            &fixedScopePanelY,
            &fixedScopePanelSize);

    if (dedicatedFixedScopeSource &&
        fixedScopePanelSize > 4 &&
        g_vrCapturedStereoWidth > 0u &&
        g_vrCapturedStereoHeight > 0u)
    {
        const float inverseCaptureWidth =
            1.0f /
            static_cast<float>(
                g_vrCapturedStereoWidth);

        const float inverseCaptureHeight =
            1.0f /
            static_cast<float>(
                g_vrCapturedStereoHeight);

        const float sourceInsetPixels = 2.0f;

        constants.fixedScopeZoom[2] = 1.0f;

        constants.fixedScopeSource[0] =
            (static_cast<float>(
                 fixedScopePanelX) +
             0.5f * static_cast<float>(
                 fixedScopePanelSize)) *
            inverseCaptureWidth;

        constants.fixedScopeSource[1] =
            (static_cast<float>(
                 fixedScopePanelY) +
             0.5f * static_cast<float>(
                 fixedScopePanelSize)) *
            inverseCaptureHeight;

        constants.fixedScopeSource[2] =
            (0.5f * static_cast<float>(
                 fixedScopePanelSize) -
             sourceInsetPixels) *
            inverseCaptureWidth;

        constants.fixedScopeSource[3] =
            (0.5f * static_cast<float>(
                 fixedScopePanelSize) -
             sourceInsetPixels) *
            inverseCaptureHeight;

        static bool loggedDedicatedFixedScopeSource = false;

        if (!loggedDedicatedFixedScopeSource)
        {
            Com_Printf(
                0,
                "[VR][FIXED SCOPE] Sampling the dedicated %d x %d "
                "scope camera at %.2f degree FOV; eye-image "
                "crop upscaling is disabled.\n",
                fixedScopePanelSize,
                fixedScopePanelSize,
                fixedScopedTurretZoomFovDegrees);

            loggedDedicatedFixedScopeSource = true;
        }
    }
    else if (fixedScopedTurretActive &&
             fixedScopeZoomScale < 0.999f &&
             !g_vrLoggedFixedScopedTurretVisibleZoom)
    {
        Com_Printf(
            0,
            "[VR][FIXED SCOPE] Dedicated scope panel unavailable; "
            "using the eye-image crop fallback at %.2f degree FOV "
            "(sample scale %.4f).\n",
            fixedScopedTurretZoomFovDegrees,
            fixedScopeZoomScale);

        g_vrLoggedFixedScopedTurretVisibleZoom = true;
    }

    if (fixedScopedTurretActive &&
        !g_vrLoggedFixedScopedTurretCompositor)
    {
        Com_Printf(
            0,
            "[VR][FIXED SCOPE] Projected both scope reticles onto "
            "one binocular forward ray.\n");

        g_vrLoggedFixedScopedTurretCompositor = true;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};

    const HRESULT hr =
        g_vrD3dContext->Map(
            g_vrCompositorConstantBuffer.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped);

    if (FAILED(hr) ||
        mapped.pData == nullptr)
    {
        return;
    }

    std::memcpy(
        mapped.pData,
        &constants,
        sizeof(constants));

    g_vrD3dContext->Unmap(
        g_vrCompositorConstantBuffer.Get(),
        0);
}

std::uint64_t VR_OpenXrClockNanoseconds()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<
            std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()
                .time_since_epoch())
            .count());
}

void VR_RecordCapturedStereoPose(
    const VrD3D9FrameMetadata& metadata)
{
    std::array<XrView, kVrStereoEyeCount>
        matchedViews = {};

    std::uint64_t matchedPoseNanoseconds = 0u;
    bool matched = false;
    bool poseAvailable = false;

    if (metadata.renderFrameId != 0u)
    {
        std::lock_guard<std::mutex> lock(
            g_vrRenderPoseHistoryMutex);

        for (const VrRenderPoseHistoryEntry& entry :
             g_vrRenderPoseHistory)
        {
            if (entry.valid &&
                entry.renderFrameId ==
                    metadata.renderFrameId)
            {
                matchedViews = entry.views;
                matchedPoseNanoseconds =
                    entry.recordedNanoseconds;
                matched = true;
                poseAvailable = true;
                break;
            }
        }
    }

    if (!matched)
    {
        std::lock_guard<std::mutex> lock(
            g_vrPublishedRenderViewsMutex);

        if (g_vrPublishedRenderViewsValid)
        {
            matchedViews =
                g_vrPublishedRenderViews;
            matchedPoseNanoseconds =
                g_vrPublishedRenderPoseNanoseconds;
            poseAvailable = true;
        }
    }

    g_vrCapturedStereoMetadata = metadata;
    g_vrCapturedStereoPoseMatched = matched;
    g_vrCapturedRenderPoseNanoseconds =
        matchedPoseNanoseconds;

    if (!poseAvailable)
    {
        return;
    }

    g_vrCapturedStereoViews =
        matchedViews;

    g_vrCapturedStereoViewsValid =
        true;

    if (matched &&
        !g_vrLoggedCapturedPoseMatch)
    {
        Com_Printf(
            0,
            "[VR] D3D9 capture frame IDs now resolve to the exact "
            "OpenXR views used to render their pixels.\n");

        g_vrLoggedCapturedPoseMatch =
            true;
    }
    else if (!matched &&
             !g_vrLoggedCapturedPoseMiss)
    {
        Com_PrintWarning(
            0,
            "[VR] A captured D3D9 frame had no matching render-pose "
            "history entry; using the newest published pose for this "
            "frame.\n");

        g_vrLoggedCapturedPoseMiss = true;
    }
}

void VR_PollRetiredSharedFrames()
{
    if (g_vrD3dContext == nullptr)
    {
        return;
    }

    for (VrRetiredSharedFrame& retired :
         g_vrRetiredSharedFrames)
    {
        if (!retired.active ||
            retired.completionQuery == nullptr)
        {
            continue;
        }

        BOOL completed = FALSE;

        const HRESULT hr =
            g_vrD3dContext->GetData(
                retired.completionQuery.Get(),
                &completed,
                sizeof(completed),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);

        if (hr == S_OK &&
            completed)
        {
            VR_D3D9ReleaseSharedFrame(
                retired.slotIndex,
                retired.serial);

            retired.active = false;
            retired.slotIndex = 0u;
            retired.serial = 0u;
            retired.view.Reset();
            retired.texture.Reset();
        }
        else if (FAILED(hr) &&
                 !g_vrLoggedSharedConsumerFailure)
        {
            VR_LogHrFailure(
                "GetData(D3D11 shared consumer fence)",
                hr);

            VR_D3D9DisableSharedBridge();

            g_vrLoggedSharedConsumerFailure = true;
        }
    }
}

VrRetiredSharedFrame*
VR_FindFreeRetiredSharedFrame()
{
    for (VrRetiredSharedFrame& retired :
         g_vrRetiredSharedFrames)
    {
        if (!retired.active)
        {
            return &retired;
        }
    }

    return nullptr;
}

bool VR_RetireCurrentSharedFrame()
{
    if (!g_vrCurrentSharedFrameActive)
    {
        return true;
    }

    VrRetiredSharedFrame* retired =
        VR_FindFreeRetiredSharedFrame();

    if (retired == nullptr ||
        g_vrD3dDevice == nullptr ||
        g_vrD3dContext == nullptr)
    {
        return false;
    }

    if (retired->completionQuery == nullptr)
    {
        D3D11_QUERY_DESC queryDescription = {};
        queryDescription.Query =
            D3D11_QUERY_EVENT;

        const HRESULT hr =
            g_vrD3dDevice->CreateQuery(
                &queryDescription,
                retired->completionQuery
                    .GetAddressOf());

        if (FAILED(hr))
        {
            VR_LogHrFailure(
                "CreateQuery(D3D11 shared consumer)",
                hr);

            return false;
        }
    }

    // Ensure the resource is no longer bound before placing the fence after
    // every D3D11 draw that sampled this frame.
    ID3D11ShaderResourceView* nullView = nullptr;

    g_vrD3dContext->PSSetShaderResources(
        0,
        1,
        &nullView);

    retired->active = true;
    retired->slotIndex =
        g_vrCurrentSharedSlot;
    retired->serial =
        g_vrCurrentSharedSerial;
    retired->texture =
        g_vrCapturedStereoTexture;
    retired->view =
        g_vrCapturedStereoView;

    g_vrD3dContext->End(
        retired->completionQuery.Get());
    g_vrD3dContext->Flush();

    g_vrCapturedStereoView.Reset();
    g_vrCapturedStereoTexture.Reset();

    g_vrCurrentSharedFrameActive = false;
    g_vrCurrentSharedSlot = 0u;
    g_vrCurrentSharedGeneration = 0u;
    g_vrCurrentSharedSerial = 0u;

    return true;
}

void VR_AbandonCurrentSharedFrameForFallback()
{
    if (!g_vrCurrentSharedFrameActive)
    {
        return;
    }

    if (g_vrD3dContext != nullptr)
    {
        ID3D11ShaderResourceView* nullView =
            nullptr;

        g_vrD3dContext->PSSetShaderResources(
            0,
            1,
            &nullView);

        g_vrD3dContext->Flush();
    }

    // The bridge is disabled before this path runs, so D3D9 will not
    // overwrite this slot.  Keep the producer slot acquired until shutdown
    // if a consumer fence could not be created.
    g_vrCapturedStereoView.Reset();
    g_vrCapturedStereoTexture.Reset();

    g_vrCurrentSharedFrameActive = false;
    g_vrCurrentSharedSlot = 0u;
    g_vrCurrentSharedGeneration = 0u;
    g_vrCurrentSharedSerial = 0u;
}

DXGI_FORMAT VR_CapturedStereoSampleFormat()
{
    return
        g_vrRuntimeBackend ==
                VrRuntimeBackend::OpenVr
            ? DXGI_FORMAT_B8G8R8A8_UNORM
            : DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

void VR_LogCapturedStereoColorTransfer()
{
    if (g_vrLoggedCaptureColorTransfer)
    {
        return;
    }

    if (g_vrRuntimeBackend ==
        VrRuntimeBackend::OpenVr)
    {
        Com_Printf(
            0,
            "[VR][OPENVR][COLOR] V78 preserving gamma-encoded "
            "D3D9 capture through a BGRA8 UNORM view; SteamVR "
            "submission uses ColorSpace_Auto.\n");
    }
    else
    {
        Com_Printf(
            0,
            "[VR] D3D9 capture is now sampled as sRGB before "
            "the sRGB OpenXR swapchain; double-gamma "
            "brightness is removed.\n");
    }

    g_vrLoggedCaptureColorTransfer = true;
}

bool VR_OpenGpuSharedStereoFrame(
    const VrD3D9SharedFrame& frame)
{
    if (g_vrD3dDevice == nullptr ||
        frame.sharedHandle == nullptr ||
        frame.width < 2u ||
        frame.height == 0u)
    {
        return false;
    }

    ComPtr<ID3D11Texture2D> sharedTexture;

    HRESULT hr =
        g_vrD3dDevice->OpenSharedResource(
            static_cast<HANDLE>(
                frame.sharedHandle),
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(
                sharedTexture.GetAddressOf()));

    if (FAILED(hr) ||
        sharedTexture == nullptr)
    {
        VR_LogHrFailure(
            "OpenSharedResource(D3D9Ex stereo frame)",
            hr);

        return false;
    }

    D3D11_TEXTURE2D_DESC description = {};
    sharedTexture->GetDesc(&description);

    if (frame.slotIndex >=
            kVrD3D9SharedFrameSlotCount ||
        description.Width != frame.width ||
        description.Height != frame.height ||
        description.MipLevels != 1u ||
        description.ArraySize != 1u ||
        description.SampleDesc.Count != 1u ||
        description.Format !=
            DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        Com_PrintWarning(
            0,
            "[VR] Rejected mismatched D3D9Ex shared "
            "frame description.\n");

        return false;
    }

    ComPtr<ID3D11Texture2D>& decodedTexture =
        g_vrCapturedSharedTextures[
            frame.slotIndex];

    ComPtr<ID3D11ShaderResourceView>& decodedView =
        g_vrCapturedSharedViews[
            frame.slotIndex];

    const DXGI_FORMAT capturedViewFormat =
        VR_CapturedStereoSampleFormat();

    bool recreateDecodedTexture =
        decodedTexture == nullptr ||
        decodedView == nullptr;

    if (!recreateDecodedTexture)
    {
        D3D11_TEXTURE2D_DESC decodedDescription = {};
        decodedTexture->GetDesc(&decodedDescription);

        recreateDecodedTexture =
            decodedDescription.Width != frame.width ||
            decodedDescription.Height != frame.height ||
            decodedDescription.Format !=
                DXGI_FORMAT_B8G8R8A8_TYPELESS;

        if (!recreateDecodedTexture)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC
                decodedViewDescription = {};

            decodedView->GetDesc(
                &decodedViewDescription);

            recreateDecodedTexture =
                decodedViewDescription.Format !=
                    capturedViewFormat;
        }
    }

    if (recreateDecodedTexture)
    {
        decodedView.Reset();
        decodedTexture.Reset();

        D3D11_TEXTURE2D_DESC decodedDescription =
            description;

        decodedDescription.Format =
            DXGI_FORMAT_B8G8R8A8_TYPELESS;
        decodedDescription.Usage =
            D3D11_USAGE_DEFAULT;
        decodedDescription.BindFlags =
            D3D11_BIND_SHADER_RESOURCE;
        decodedDescription.CPUAccessFlags = 0u;
        decodedDescription.MiscFlags = 0u;

        hr =
            g_vrD3dDevice->CreateTexture2D(
                &decodedDescription,
                nullptr,
                decodedTexture.GetAddressOf());

        if (FAILED(hr) ||
            decodedTexture == nullptr)
        {
            VR_LogHrFailure(
                "CreateTexture2D(capture color transfer)",
                hr);

            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC
            decodedViewDescription = {};

        decodedViewDescription.Format =
            capturedViewFormat;
        decodedViewDescription.ViewDimension =
            D3D11_SRV_DIMENSION_TEXTURE2D;
        decodedViewDescription.Texture2D.MostDetailedMip = 0u;
        decodedViewDescription.Texture2D.MipLevels = 1u;

        hr =
            g_vrD3dDevice->CreateShaderResourceView(
                decodedTexture.Get(),
                &decodedViewDescription,
                decodedView.GetAddressOf());

        if (FAILED(hr) ||
            decodedView == nullptr)
        {
            VR_LogHrFailure(
                "CreateShaderResourceView(capture color transfer)",
                hr);

            decodedTexture.Reset();
            return false;
        }
    }

    if (!VR_RetireCurrentSharedFrame())
    {
        return false;
    }

    // Copying within the BGRA8 format family preserves the encoded bytes.
    // OpenXR decodes them through an sRGB view. OpenVR deliberately samples
    // them through a UNORM view so the working Auto/gamma compositor path
    // receives display-referred values instead of rejecting Linear mode.
    g_vrD3dContext->CopyResource(
        decodedTexture.Get(),
        sharedTexture.Get());

    g_vrCapturedStereoTexture =
        decodedTexture;
    g_vrCapturedStereoView =
        decodedView;

    VR_LogCapturedStereoColorTransfer();

    g_vrCapturedStereoWidth =
        frame.width;
    g_vrCapturedStereoHeight =
        frame.height;
    g_vrUploadedStereoSerial =
        frame.serial;

    g_vrCurrentSharedFrameActive = true;
    g_vrCurrentSharedSlot =
        frame.slotIndex;
    g_vrCurrentSharedGeneration =
        frame.generation;
    g_vrCurrentSharedSerial =
        frame.serial;

    VR_RecordCapturedStereoPose(
        frame.metadata);

    if (!g_vrLoggedFirstSharedFrameOpen)
    {
        Com_Printf(
            0,
            "[VR] D3D11 opened the GPU-shared D3D9Ex "
            "stereo frame directly: %u x %u; "
            "CPU readback and upload are bypassed.\n",
            frame.width,
            frame.height);

        g_vrLoggedFirstSharedFrameOpen = true;
    }

    return true;
}

bool VR_UpdateCapturedStereoTexture()
{
    VR_PollRetiredSharedFrames();

    const bool canRetireCurrent =
        !g_vrCurrentSharedFrameActive ||
        VR_FindFreeRetiredSharedFrame() !=
            nullptr;

    if (canRetireCurrent)
    {
        VrD3D9SharedFrame sharedFrame = {};

        if (VR_D3D9AcquireLatestSharedFrame(
                g_vrUploadedStereoSerial,
                sharedFrame))
        {
            if (VR_OpenGpuSharedStereoFrame(
                    sharedFrame))
            {
                return true;
            }

            VR_D3D9ReleaseSharedFrame(
                sharedFrame.slotIndex,
                sharedFrame.serial);

            VR_D3D9DisableSharedBridge();
        }
    }

    if (VR_D3D9SharedBridgeActive())
    {
        return
            g_vrCapturedStereoView !=
            nullptr;
    }

    if (g_vrCurrentSharedFrameActive &&
        !VR_RetireCurrentSharedFrame())
    {
        VR_AbandonCurrentSharedFrameForFallback();
    }

    std::vector<std::uint8_t>& pixels =
        g_vrCapturedStereoUploadPixels;

    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint64_t serial = 0u;
    VrD3D9FrameMetadata metadata = {};

    if (!VR_D3D9CopyLatestStereoFrame(
            g_vrUploadedStereoSerial,
            pixels,
            width,
            height,
            serial,
            metadata))
    {
        return
            g_vrCapturedStereoView !=
            nullptr;
    }

    const DXGI_FORMAT capturedViewFormat =
        VR_CapturedStereoSampleFormat();

    bool recreateCapturedTexture =
        !g_vrCapturedStereoTexture ||
        !g_vrCapturedStereoView ||
        width != g_vrCapturedStereoWidth ||
        height != g_vrCapturedStereoHeight;

    if (!recreateCapturedTexture)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC
            currentViewDescription = {};

        g_vrCapturedStereoView->GetDesc(
            &currentViewDescription);

        recreateCapturedTexture =
            currentViewDescription.Format !=
                capturedViewFormat;
    }

    if (recreateCapturedTexture)
    {
        g_vrCapturedStereoView.Reset();
        g_vrCapturedStereoTexture.Reset();

        D3D11_TEXTURE2D_DESC textureDescription = {};
        textureDescription.Width = width;
        textureDescription.Height = height;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        // Upload the gamma-encoded D3D9 bytes into a typeless texture so the
        // runtime-specific SRV can either decode or preserve them.
        textureDescription.Format =
            DXGI_FORMAT_B8G8R8A8_TYPELESS;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage =
            D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags =
            D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr =
            g_vrD3dDevice->CreateTexture2D(
                &textureDescription,
                nullptr,
                g_vrCapturedStereoTexture
                    .GetAddressOf());

        if (FAILED(hr))
        {
            VR_LogHrFailure(
                "CreateTexture2D(stereo capture)",
                hr);

            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC
            capturedViewDescription = {};

        capturedViewDescription.Format =
            capturedViewFormat;
        capturedViewDescription.ViewDimension =
            D3D11_SRV_DIMENSION_TEXTURE2D;
        capturedViewDescription.Texture2D.MostDetailedMip = 0u;
        capturedViewDescription.Texture2D.MipLevels = 1u;

        hr =
            g_vrD3dDevice->CreateShaderResourceView(
                g_vrCapturedStereoTexture.Get(),
                &capturedViewDescription,
                g_vrCapturedStereoView
                    .GetAddressOf());

        if (FAILED(hr))
        {
            VR_LogHrFailure(
                "CreateShaderResourceView("
                "stereo capture)",
                hr);

            g_vrCapturedStereoTexture.Reset();
            return false;
        }

        g_vrCapturedStereoWidth = width;
        g_vrCapturedStereoHeight = height;
    }

    g_vrD3dContext->UpdateSubresource(
        g_vrCapturedStereoTexture.Get(),
        0,
        nullptr,
        pixels.data(),
        width * 4u,
        0);

    VR_RecordCapturedStereoPose(metadata);

    g_vrUploadedStereoSerial = serial;

    VR_LogCapturedStereoColorTransfer();

    if (!g_vrLoggedCaptureBufferHandoff)
    {
        Com_Printf(
            0,
            "[VR] CPU fallback handed its capture buffer "
            "to the persistent D3D11 upload texture.\n");

        g_vrLoggedCaptureBufferHandoff = true;
    }

    if (!g_vrLoggedFirstStereoUpload)
    {
        Com_Printf(
            0,
            "[VR] CPU fallback uploaded the first complete "
            "side-by-side D3D9 frame: %u x %u.\n",
            width,
            height);

        g_vrLoggedFirstStereoUpload = true;
    }

    return true;
}

bool VR_CreateHeadTrackedScene()
{
    static const char* shaderSource = R"(
cbuffer TestConstants : register(b0)
{
    row_major float4x4 modelViewProjection;
};

struct VertexInput
{
    float3 position : POSITION;
    float3 color : COLOR;
};

struct PixelInput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

PixelInput VSMain(VertexInput input)
{
    PixelInput output;

    output.position =
        mul(float4(input.position, 1.0f),
            modelViewProjection);

    output.color = input.color;

    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return float4(input.color, 1.0f);
}
)";

    ComPtr<ID3DBlob> vertexCode;
    ComPtr<ID3DBlob> pixelCode;

    if (!VR_TestCompileShader(
            shaderSource,
            "VSMain",
            "vs_4_0",
            vertexCode))
    {
        return false;
    }

    if (!VR_TestCompileShader(
            shaderSource,
            "PSMain",
            "ps_4_0",
            pixelCode))
    {
        return false;
    }

    HRESULT hr =
        g_vrD3dDevice->CreateVertexShader(
            vertexCode->GetBufferPointer(),
            vertexCode->GetBufferSize(),
            nullptr,
            g_vrTestVertexShader.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateVertexShader", hr);
        return false;
    }

    hr =
        g_vrD3dDevice->CreatePixelShader(
            pixelCode->GetBufferPointer(),
            pixelCode->GetBufferSize(),
            nullptr,
            g_vrTestPixelShader.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreatePixelShader", hr);
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            0,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
        {
            "COLOR",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            12,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
    };

    hr =
        g_vrD3dDevice->CreateInputLayout(
            layout,
            2,
            vertexCode->GetBufferPointer(),
            vertexCode->GetBufferSize(),
            g_vrTestInputLayout.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateInputLayout", hr);
        return false;
    }

    static const VrTestVertex cubeVertices[] = {
        {{-0.40f, -0.40f, -0.40f}, {1.0f, 0.1f, 0.1f}},
        {{-0.40f,  0.40f, -0.40f}, {0.1f, 1.0f, 0.1f}},
        {{ 0.40f,  0.40f, -0.40f}, {0.1f, 0.2f, 1.0f}},
        {{ 0.40f, -0.40f, -0.40f}, {1.0f, 1.0f, 0.1f}},
        {{-0.40f, -0.40f,  0.40f}, {1.0f, 0.1f, 1.0f}},
        {{-0.40f,  0.40f,  0.40f}, {0.1f, 1.0f, 1.0f}},
        {{ 0.40f,  0.40f,  0.40f}, {1.0f, 1.0f, 1.0f}},
        {{ 0.40f, -0.40f,  0.40f}, {0.8f, 0.4f, 0.1f}},
    };

    static const unsigned short cubeIndices[] = {
        0, 1, 2,  0, 2, 3,
        4, 6, 5,  4, 7, 6,
        4, 5, 1,  4, 1, 0,
        3, 2, 6,  3, 6, 7,
        1, 5, 6,  1, 6, 2,
        4, 0, 3,  4, 3, 7,
    };

    g_vrTestIndexCount =
        static_cast<UINT>(
            sizeof(cubeIndices) /
            sizeof(cubeIndices[0]));

    D3D11_BUFFER_DESC cubeVertexDescription = {};
    cubeVertexDescription.ByteWidth =
        static_cast<UINT>(sizeof(cubeVertices));
    cubeVertexDescription.Usage =
        D3D11_USAGE_IMMUTABLE;
    cubeVertexDescription.BindFlags =
        D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA cubeVertexData = {};
    cubeVertexData.pSysMem = cubeVertices;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &cubeVertexDescription,
            &cubeVertexData,
            g_vrTestVertexBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateBuffer(cube vertices)", hr);
        return false;
    }

    D3D11_BUFFER_DESC cubeIndexDescription = {};
    cubeIndexDescription.ByteWidth =
        static_cast<UINT>(sizeof(cubeIndices));
    cubeIndexDescription.Usage =
        D3D11_USAGE_IMMUTABLE;
    cubeIndexDescription.BindFlags =
        D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA cubeIndexData = {};
    cubeIndexData.pSysMem = cubeIndices;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &cubeIndexDescription,
            &cubeIndexData,
            g_vrTestIndexBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateBuffer(cube indices)", hr);
        return false;
    }

    std::vector<VrTestVertex> gridVertices;

    constexpr int gridHalfSize = 10;
    constexpr float gridSpacing = 0.50f;
    constexpr float gridY = -1.40f;

    for (int line = -gridHalfSize;
         line <= gridHalfSize;
         ++line)
    {
        const float coordinate =
            static_cast<float>(line) * gridSpacing;

        const bool majorLine = (line % 5) == 0;

        const float brightness =
            majorLine ? 0.50f : 0.20f;

        const float extent =
            static_cast<float>(gridHalfSize) *
            gridSpacing;

        gridVertices.push_back({
            {-extent, gridY, coordinate},
            {brightness, brightness, brightness},
        });

        gridVertices.push_back({
            { extent, gridY, coordinate},
            {brightness, brightness, brightness},
        });

        gridVertices.push_back({
            {coordinate, gridY, -extent},
            {brightness, brightness, brightness},
        });

        gridVertices.push_back({
            {coordinate, gridY,  extent},
            {brightness, brightness, brightness},
        });
    }

    g_vrGridVertexCount =
        static_cast<UINT>(gridVertices.size());

    D3D11_BUFFER_DESC gridDescription = {};
    gridDescription.ByteWidth =
        static_cast<UINT>(
            gridVertices.size() *
            sizeof(VrTestVertex));
    gridDescription.Usage =
        D3D11_USAGE_IMMUTABLE;
    gridDescription.BindFlags =
        D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA gridData = {};
    gridData.pSysMem = gridVertices.data();

    hr =
        g_vrD3dDevice->CreateBuffer(
            &gridDescription,
            &gridData,
            g_vrGridVertexBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateBuffer(grid vertices)", hr);
        return false;
    }

    D3D11_BUFFER_DESC constantDescription = {};
    constantDescription.ByteWidth =
        sizeof(VrTestConstants);
    constantDescription.Usage =
        D3D11_USAGE_DEFAULT;
    constantDescription.BindFlags =
        D3D11_BIND_CONSTANT_BUFFER;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &constantDescription,
            nullptr,
            g_vrTestConstantBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateBuffer(constants)", hr);
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizerDescription = {};
    rasterizerDescription.FillMode = D3D11_FILL_SOLID;
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    rasterizerDescription.DepthClipEnable = TRUE;

    hr =
        g_vrD3dDevice->CreateRasterizerState(
            &rasterizerDescription,
            g_vrTestRasterizerState.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateRasterizerState", hr);
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC depthDescription = {};
    depthDescription.DepthEnable = TRUE;
    depthDescription.DepthWriteMask =
        D3D11_DEPTH_WRITE_MASK_ALL;
    depthDescription.DepthFunc =
        D3D11_COMPARISON_LESS_EQUAL;

    hr =
        g_vrD3dDevice->CreateDepthStencilState(
            &depthDescription,
            g_vrTestDepthStencilState.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure("CreateDepthStencilState", hr);
        return false;
    }

    Com_Printf(
        0,
        "[VR] Head-tracked cube and floor-grid resources created.\n");

    return true;
}


struct VrHeadVector
{
    float x;
    float y;
    float z;
};

XrQuaternionf VR_NormalizeQuaternion(
    const XrQuaternionf& quaternion)
{
    const float lengthSquared =
        quaternion.x * quaternion.x +
        quaternion.y * quaternion.y +
        quaternion.z * quaternion.z +
        quaternion.w * quaternion.w;

    if (lengthSquared <= 0.000001f)
    {
        return {
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    }

    const float inverseLength =
        1.0f / std::sqrt(lengthSquared);

    return {
        quaternion.x * inverseLength,
        quaternion.y * inverseLength,
        quaternion.z * inverseLength,
        quaternion.w * inverseLength,
    };
}

XrQuaternionf VR_ConjugateQuaternion(
    const XrQuaternionf& quaternion)
{
    return {
        -quaternion.x,
        -quaternion.y,
        -quaternion.z,
        quaternion.w,
    };
}

XrQuaternionf VR_MultiplyQuaternion(
    const XrQuaternionf& left,
    const XrQuaternionf& right)
{
    XrQuaternionf result = {};

    result.x =
        left.w * right.x +
        left.x * right.w +
        left.y * right.z -
        left.z * right.y;

    result.y =
        left.w * right.y -
        left.x * right.z +
        left.y * right.w +
        left.z * right.x;

    result.z =
        left.w * right.z +
        left.x * right.y -
        left.y * right.x +
        left.z * right.w;

    result.w =
        left.w * right.w -
        left.x * right.x -
        left.y * right.y -
        left.z * right.z;

    return VR_NormalizeQuaternion(result);
}

VrHeadVector VR_CrossHeadVector(
    const VrHeadVector& left,
    const VrHeadVector& right)
{
    return {
        left.y * right.z -
            left.z * right.y,

        left.z * right.x -
            left.x * right.z,

        left.x * right.y -
            left.y * right.x,
    };
}

VrHeadVector VR_RotateHeadVector(
    const XrQuaternionf& orientation,
    const VrHeadVector& vector)
{
    const VrHeadVector quaternionVector = {
        orientation.x,
        orientation.y,
        orientation.z,
    };

    VrHeadVector twiceCross =
        VR_CrossHeadVector(
            quaternionVector,
            vector);

    twiceCross.x *= 2.0f;
    twiceCross.y *= 2.0f;
    twiceCross.z *= 2.0f;

    const VrHeadVector secondCross =
        VR_CrossHeadVector(
            quaternionVector,
            twiceCross);

    return {
        vector.x +
            orientation.w * twiceCross.x +
            secondCross.x,

        vector.y +
            orientation.w * twiceCross.y +
            secondCross.y,

        vector.z +
            orientation.w * twiceCross.z +
            secondCross.z,
    };
}

// Based on Optimumbox's beta.14 leveling proposal. This guarded form keeps
// pitch and roll out of the persistent HMD base while refusing to invent a
// yaw when the headset is pointed too close to vertical.
bool VR_TryGetLevelYawOnlyOrientation(
    const XrQuaternionf& orientation,
    XrQuaternionf* levelYawOnlyOrientation)
{
    if (levelYawOnlyOrientation == nullptr)
    {
        return false;
    }

    const VrHeadVector liveForward =
        VR_RotateHeadVector(
            VR_NormalizeQuaternion(orientation),
            {0.0f, 0.0f, -1.0f});

    const float horizontalLengthSquared =
        liveForward.x * liveForward.x +
        liveForward.z * liveForward.z;

    if (horizontalLengthSquared <= 0.0001f)
    {
        return false;
    }

    const float levelYawRadians =
        std::atan2(
            -liveForward.x,
            -liveForward.z);

    *levelYawOnlyOrientation =
        VR_NormalizeQuaternion(
            {
                0.0f,
                std::sin(levelYawRadians * 0.5f),
                0.0f,
                std::cos(levelYawRadians * 0.5f),
            });

    return true;
}

void VR_LogLevelSafeHeadBaseOnce()
{
    if (g_vrLoggedLevelSafeHeadBase)
    {
        return;
    }

    Com_Printf(
        0,
        "[VR][CALIBRATION][LEVEL] V104 level-safe HMD yaw-only base is active.\n");

    g_vrLoggedLevelSafeHeadBase = true;
}

XrPosef VR_OpenVrMatrixToPose(
    const vr::HmdMatrix34_t& matrix)
{
    XrPosef pose = {};
    pose.position = {
        matrix.m[0][3],
        matrix.m[1][3],
        matrix.m[2][3],
    };

    const float trace =
        matrix.m[0][0] +
        matrix.m[1][1] +
        matrix.m[2][2];

    XrQuaternionf orientation = {};

    if (trace > 0.0f)
    {
        const float scale =
            2.0f * std::sqrt(trace + 1.0f);

        orientation.w = 0.25f * scale;
        orientation.x =
            (matrix.m[2][1] - matrix.m[1][2]) /
            scale;
        orientation.y =
            (matrix.m[0][2] - matrix.m[2][0]) /
            scale;
        orientation.z =
            (matrix.m[1][0] - matrix.m[0][1]) /
            scale;
    }
    else if (matrix.m[0][0] > matrix.m[1][1] &&
             matrix.m[0][0] > matrix.m[2][2])
    {
        const float scale =
            2.0f * std::sqrt(
                1.0f + matrix.m[0][0] -
                matrix.m[1][1] - matrix.m[2][2]);

        orientation.w =
            (matrix.m[2][1] - matrix.m[1][2]) /
            scale;
        orientation.x = 0.25f * scale;
        orientation.y =
            (matrix.m[0][1] + matrix.m[1][0]) /
            scale;
        orientation.z =
            (matrix.m[0][2] + matrix.m[2][0]) /
            scale;
    }
    else if (matrix.m[1][1] > matrix.m[2][2])
    {
        const float scale =
            2.0f * std::sqrt(
                1.0f + matrix.m[1][1] -
                matrix.m[0][0] - matrix.m[2][2]);

        orientation.w =
            (matrix.m[0][2] - matrix.m[2][0]) /
            scale;
        orientation.x =
            (matrix.m[0][1] + matrix.m[1][0]) /
            scale;
        orientation.y = 0.25f * scale;
        orientation.z =
            (matrix.m[1][2] + matrix.m[2][1]) /
            scale;
    }
    else
    {
        const float scale =
            2.0f * std::sqrt(
                1.0f + matrix.m[2][2] -
                matrix.m[0][0] - matrix.m[1][1]);

        orientation.w =
            (matrix.m[1][0] - matrix.m[0][1]) /
            scale;
        orientation.x =
            (matrix.m[0][2] + matrix.m[2][0]) /
            scale;
        orientation.y =
            (matrix.m[1][2] + matrix.m[2][1]) /
            scale;
        orientation.z = 0.25f * scale;
    }

    pose.orientation =
        VR_NormalizeQuaternion(
            orientation);

    return pose;
}

XrPosef VR_ComposePose(
    const XrPosef& parent,
    const XrPosef& child)
{
    const VrHeadVector rotatedChildPosition =
        VR_RotateHeadVector(
            parent.orientation,
            {
                child.position.x,
                child.position.y,
                child.position.z,
            });

    XrPosef result = {};
    result.orientation =
        VR_MultiplyQuaternion(
            parent.orientation,
            child.orientation);

    result.position = {
        parent.position.x +
            rotatedChildPosition.x,
        parent.position.y +
            rotatedChildPosition.y,
        parent.position.z +
            rotatedChildPosition.z,
    };

    return result;
}

XrPosef VR_InvertPose(
    const XrPosef& pose)
{
    XrPosef inverse = {};
    inverse.orientation =
        VR_ConjugateQuaternion(
            VR_NormalizeQuaternion(
                pose.orientation));

    const VrHeadVector inversePosition =
        VR_RotateHeadVector(
            inverse.orientation,
            {
                -pose.position.x,
                -pose.position.y,
                -pose.position.z,
            });

    inverse.position = {
        inversePosition.x,
        inversePosition.y,
        inversePosition.z,
    };

    return inverse;
}

XrPosef VR_OpenVrHeadPoseFromViews(
    const std::array<XrView, kVrStereoEyeCount>& views)
{
    if (g_vrOpenVrSystem != nullptr)
    {
        const XrPosef leftEyeToHead =
            VR_OpenVrMatrixToPose(
                g_vrOpenVrSystem
                    ->GetEyeToHeadTransform(
                        vr::Eye_Left));

        return VR_ComposePose(
            views[0].pose,
            VR_InvertPose(
                leftEyeToHead));
    }

    XrPosef headPose =
        views[0].pose;

    headPose.position = {
        0.5f *
            (views[0].pose.position.x +
             views[1].pose.position.x),
        0.5f *
            (views[0].pose.position.y +
             views[1].pose.position.y),
        0.5f *
            (views[0].pose.position.z +
             views[1].pose.position.z),
    };

    return headPose;
}

vr::HmdMatrix34_t VR_OpenVrHeadMatrixFromViews(
    const std::array<XrView, kVrStereoEyeCount>& views)
{
    const XrPosef headPose =
        VR_OpenVrHeadPoseFromViews(
            views);

    const XrQuaternionf orientation =
        VR_NormalizeQuaternion(
            headPose.orientation);

    const float x = orientation.x;
    const float y = orientation.y;
    const float z = orientation.z;
    const float w = orientation.w;

    vr::HmdMatrix34_t matrix = {};

    matrix.m[0][0] =
        1.0f - 2.0f * (y * y + z * z);
    matrix.m[0][1] =
        2.0f * (x * y - z * w);
    matrix.m[0][2] =
        2.0f * (x * z + y * w);

    matrix.m[1][0] =
        2.0f * (x * y + z * w);
    matrix.m[1][1] =
        1.0f - 2.0f * (x * x + z * z);
    matrix.m[1][2] =
        2.0f * (y * z - x * w);

    matrix.m[2][0] =
        2.0f * (x * z - y * w);
    matrix.m[2][1] =
        2.0f * (y * z + x * w);
    matrix.m[2][2] =
        1.0f - 2.0f * (x * x + y * y);

    matrix.m[0][3] =
        headPose.position.x;

    matrix.m[1][3] =
        headPose.position.y;

    matrix.m[2][3] =
        headPose.position.z;

    return matrix;
}

VrHeadVector VR_OpenXrVectorToCod(
    const VrHeadVector& vector)
{
    // OpenXR: +X right, +Y up, -Z forward.
    // CoD camera-local basis: +X forward, +Y left, +Z up.
    return {
        -vector.z,
        -vector.x,
        vector.y,
    };
}

void VR_ResetHeadOrientation()
{
    std::lock_guard<std::mutex> lock(
        g_vrHeadOrientationMutex);

    g_vrHeadOrientationAxis[0][0] = 1.0f;
    g_vrHeadOrientationAxis[0][1] = 0.0f;
    g_vrHeadOrientationAxis[0][2] = 0.0f;

    g_vrHeadOrientationAxis[1][0] = 0.0f;
    g_vrHeadOrientationAxis[1][1] = 1.0f;
    g_vrHeadOrientationAxis[1][2] = 0.0f;

    g_vrHeadOrientationAxis[2][0] = 0.0f;
    g_vrHeadOrientationAxis[2][1] = 0.0f;
    g_vrHeadOrientationAxis[2][2] = 1.0f;

    g_vrHeadBaseOrientation = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };

    g_vrHeadBaseOrientationValid = false;
    g_vrHeadOrientationValid = false;
    g_vrLoggedLevelSafeHeadBase = false;
    g_vrTransferredBodyYawDegrees = 0.0f;
    g_vrHeadPositionBodyYawDegrees = 0.0f;
    g_vrLoggedFirstHeadPose = false;
    g_vrLoggedFirstCameraApply = false;

    g_vrHeadPositionOrigin = {};
    g_vrLatestHeadPosition = {};
    g_vrLatestHeadOrientation = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };

    g_vrHeadPositionLocal[0] = 0.0f;
    g_vrHeadPositionLocal[1] = 0.0f;
    g_vrHeadPositionLocal[2] = 0.0f;

    g_vrHeadPositionOriginValid = false;
    g_vrLatestHeadPositionValid = false;
    g_vrLatestHeadOrientationValid = false;
    g_vrHeadPositionValid = false;
    g_vrLoggedFirstPositionApply = false;
    g_vrLiveTargetEyeHeightInches =
        VrCalibration::kNativeStandingEyeHeightInches;
    g_vrLiveTargetEyeHeightValid = false;
    g_vrLastCalibrationRequestId.clear();
    g_vrLastCalibrationPollMilliseconds = 0u;
    g_vrLoggedFirstHeightApply.store(false);

    {
        std::lock_guard<std::mutex> hudLock(
            g_vrHudEditorMutex);
        if (g_vrHudEditorActive)
        {
            g_vrHudEditorLayout =
                g_vrHudEditorOriginalLayout;
        }
        g_vrHudEditorActive = false;
        g_vrHudEditorDragging = false;
        g_vrHudEditorPointerValid = false;
        g_vrHudEditorPreviousWasHeld = false;
        g_vrHudEditorNextWasHeld = false;
        g_vrHudEditorCenterWasHeld = false;
        g_vrHudEditorResetWasHeld = false;
        g_vrHudEditorKeyboardTabWasHeld = false;
        g_vrHudEditorKeyboardCenterWasHeld = false;
        g_vrHudEditorKeyboardResetWasHeld = false;
        g_vrHudEditorRequestId.clear();
        g_vrLastHudEditorRequestId.clear();
        g_vrLastHudEditorPollMilliseconds = 0u;
        ++g_vrHudLayoutRevision;
    }
}

void VR_PublishHeadOrientation(
    const XrQuaternionf& currentOrientation)
{
    const XrQuaternionf normalizedCurrent =
        VR_NormalizeQuaternion(
            currentOrientation);

    std::lock_guard<std::mutex> lock(
        g_vrHeadOrientationMutex);

    g_vrLatestHeadOrientation =
        normalizedCurrent;

    g_vrLatestHeadOrientationValid = true;

    if (!g_vrHeadBaseOrientationValid)
    {
        XrQuaternionf levelBaseOrientation = {};

        if (!VR_TryGetLevelYawOnlyOrientation(
                normalizedCurrent,
                &levelBaseOrientation))
        {
            // Defer capture until yaw is observable. Keeping the base invalid
            // avoids an arbitrary yaw snap after a near-vertical startup pose.
            return;
        }

        g_vrHeadBaseOrientation =
            levelBaseOrientation;

        g_vrHeadBaseOrientationValid = true;
        VR_LogLevelSafeHeadBaseOnce();
    }

    const XrQuaternionf relativeOrientation =
        VR_MultiplyQuaternion(
            VR_ConjugateQuaternion(
                g_vrHeadBaseOrientation),
            normalizedCurrent);

    const VrHeadVector forwardOpenXr =
        VR_RotateHeadVector(
            relativeOrientation,
            {0.0f, 0.0f, -1.0f});

    const VrHeadVector leftOpenXr =
        VR_RotateHeadVector(
            relativeOrientation,
            {-1.0f, 0.0f, 0.0f});

    const VrHeadVector upOpenXr =
        VR_RotateHeadVector(
            relativeOrientation,
            {0.0f, 1.0f, 0.0f});

    VrHeadVector forwardCod =
        VR_OpenXrVectorToCod(
            forwardOpenXr);

    VrHeadVector leftCod =
        VR_OpenXrVectorToCod(
            leftOpenXr);

    VrHeadVector upCod =
        VR_OpenXrVectorToCod(
            upOpenXr);

    constexpr float degreesToRadians =
        0.01745329251994329577f;

    const float transferredYawRadians =
        g_vrTransferredBodyYawDegrees *
        degreesToRadians;

    const float transferredYawCos =
        std::cos(
            transferredYawRadians);

    const float transferredYawSin =
        std::sin(
            transferredYawRadians);

    const auto removeTransferredYaw =
        [transferredYawCos,
         transferredYawSin](
            VrHeadVector& vector)
        {
            const float originalForward =
                vector.x;

            const float originalLeft =
                vector.y;

            // Rotate by the negative transferred yaw in CoD's
            // +forward/+left horizontal basis.
            vector.x =
                transferredYawCos *
                    originalForward +
                transferredYawSin *
                    originalLeft;

            vector.y =
                -transferredYawSin *
                    originalForward +
                transferredYawCos *
                    originalLeft;
        };

    removeTransferredYaw(
        forwardCod);

    removeTransferredYaw(
        leftCod);

    removeTransferredYaw(
        upCod);

    g_vrHeadOrientationAxis[0][0] =
        forwardCod.x;
    g_vrHeadOrientationAxis[0][1] =
        forwardCod.y;
    g_vrHeadOrientationAxis[0][2] =
        forwardCod.z;

    g_vrHeadOrientationAxis[1][0] =
        leftCod.x;
    g_vrHeadOrientationAxis[1][1] =
        leftCod.y;
    g_vrHeadOrientationAxis[1][2] =
        leftCod.z;

    g_vrHeadOrientationAxis[2][0] =
        upCod.x;
    g_vrHeadOrientationAxis[2][1] =
        upCod.y;
    g_vrHeadOrientationAxis[2][2] =
        upCod.z;

    // Publish center-head translation in the same protected snapshot as
        // the headset orientation. Averaging the two eye poses removes the
        // per-eye IPD offset and yields the physical head center.
        const XrVector3f centerHeadPosition = {
            (g_vrViews[0].pose.position.x +
             g_vrViews[1].pose.position.x) * 0.5f,
            (g_vrViews[0].pose.position.y +
             g_vrViews[1].pose.position.y) * 0.5f,
            (g_vrViews[0].pose.position.z +
             g_vrViews[1].pose.position.z) * 0.5f};

        g_vrLatestHeadPosition =
            centerHeadPosition;

        g_vrLatestHeadPositionValid =
            true;

        if (!g_vrHeadPositionOriginValid)
        {
            g_vrHeadPositionOrigin =
                centerHeadPosition;

            g_vrHeadPositionOriginValid = true;
        }

        const float openXrDeltaX =
            centerHeadPosition.x -
            g_vrHeadPositionOrigin.x;

        const float openXrDeltaY =
            centerHeadPosition.y -
            g_vrHeadPositionOrigin.y;

        const float openXrDeltaZ =
            centerHeadPosition.z -
            g_vrHeadPositionOrigin.z;

        // OpenXR: +X right, +Y up, -Z forward.
        // CoD camera-local basis: +X forward, +Y left, +Z up.
        const float rawPositionForward =
            -openXrDeltaZ *
            kVrGameUnitsPerMeter;

        const float rawPositionLeft =
            -openXrDeltaX *
            kVrGameUnitsPerMeter;

        const float positionBodyYawRadians =
            g_vrHeadPositionBodyYawDegrees *
            degreesToRadians;

        const float positionBodyYawCos =
            std::cos(positionBodyYawRadians);

        const float positionBodyYawSin =
            std::sin(positionBodyYawRadians);

        g_vrHeadPositionLocal[0] =
            positionBodyYawCos *
                rawPositionForward +
            positionBodyYawSin *
                rawPositionLeft;

        g_vrHeadPositionLocal[1] =
            -positionBodyYawSin *
                rawPositionForward +
            positionBodyYawCos *
                rawPositionLeft;

        g_vrHeadPositionLocal[2] =
            openXrDeltaY *
            kVrGameUnitsPerMeter;

        g_vrHeadPositionValid = true;

        if (g_vrViews.size() >= kVrStereoEyeCount)
        {
            const float eyeDeltaX =
                g_vrViews[0].pose.position.x -
                g_vrViews[1].pose.position.x;

            const float eyeDeltaY =
                g_vrViews[0].pose.position.y -
                g_vrViews[1].pose.position.y;

            const float eyeDeltaZ =
                g_vrViews[0].pose.position.z -
                g_vrViews[1].pose.position.z;

            const float detectedHalfIpdGameUnits =
                0.5f *
                std::sqrt(
                    eyeDeltaX * eyeDeltaX +
                    eyeDeltaY * eyeDeltaY +
                    eyeDeltaZ * eyeDeltaZ) *
                kVrGameUnitsPerMeter;

            if (detectedHalfIpdGameUnits > 0.1f &&
                detectedHalfIpdGameUnits < 3.0f)
            {
                g_vrHalfIpdGameUnits =
                    detectedHalfIpdGameUnits;
            }
        }

        g_vrHeadOrientationValid = true;

    if (!g_vrLoggedFirstHeadPose)
    {
        Com_Printf(
            0,
            "[VR] Recentered and published first "
            "OpenXR headset orientation.\n");

        g_vrLoggedFirstHeadPose = true;
    }
}

bool VR_SelectEnvironmentBlendMode()
{
    uint32_t blendModeCount = 0;

    XrResult result =
        xrEnumerateEnvironmentBlendModes(
            g_vrInstance,
            g_vrSystemId,
            kViewConfiguration,
            0,
            &blendModeCount,
            nullptr);

    if (XR_FAILED(result) || blendModeCount == 0)
    {
        VR_LogXrFailure(
            "xrEnumerateEnvironmentBlendModes(count)",
            result);

        return false;
    }

    std::vector<XrEnvironmentBlendMode> blendModes(
        blendModeCount);

    result =
        xrEnumerateEnvironmentBlendModes(
            g_vrInstance,
            g_vrSystemId,
            kViewConfiguration,
            blendModeCount,
            &blendModeCount,
            blendModes.data());

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrEnumerateEnvironmentBlendModes(list)",
            result);

        return false;
    }

    const auto opaque =
        std::find(
            blendModes.begin(),
            blendModes.end(),
            XR_ENVIRONMENT_BLEND_MODE_OPAQUE);

    g_vrBlendMode =
        opaque != blendModes.end()
            ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE
            : blendModes.front();

    return true;
}



XrQuaternionf VR_ControllerConjugateQuaternion(
    const XrQuaternionf& quaternion)
{
    return {
        -quaternion.x,
        -quaternion.y,
        -quaternion.z,
        quaternion.w,
    };
}

XrVector3f VR_ControllerSubtract(
    const XrVector3f& left,
    const XrVector3f& right)
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

XrVector3f VR_ControllerAddScaled(
    const XrVector3f& origin,
    const VrHeadVector& direction,
    const float scale)
{
    return {
        origin.x + direction.x * scale,
        origin.y + direction.y * scale,
        origin.z + direction.z * scale,
    };
}

bool VR_ProjectAppSpacePointToEye(
    const XrVector3f& point,
    const XrView& eyeView,
    float* ndcX,
    float* ndcY)
{
    if (ndcX == nullptr ||
        ndcY == nullptr)
    {
        return false;
    }

    const XrVector3f relativePosition =
        VR_ControllerSubtract(
            point,
            eyeView.pose.position);

    const VrHeadVector relativeVector = {
        relativePosition.x,
        relativePosition.y,
        relativePosition.z,
    };

    const VrHeadVector eyeLocal =
        VR_RotateHeadVector(
            VR_ControllerConjugateQuaternion(
                eyeView.pose.orientation),
            relativeVector);

    const float forwardDepth =
        -eyeLocal.z;

    if (forwardDepth <= 0.03f)
    {
        return false;
    }

    const float tanLeft =
        std::tan(eyeView.fov.angleLeft);

    const float tanRight =
        std::tan(eyeView.fov.angleRight);

    const float tanDown =
        std::tan(eyeView.fov.angleDown);

    const float tanUp =
        std::tan(eyeView.fov.angleUp);

    const float horizontalRange =
        tanRight - tanLeft;

    const float verticalRange =
        tanUp - tanDown;

    if (horizontalRange <= 0.0f ||
        verticalRange <= 0.0f)
    {
        return false;
    }

    const float projectedX =
        eyeLocal.x / forwardDepth;

    const float projectedY =
        eyeLocal.y / forwardDepth;

    *ndcX =
        2.0f *
            (projectedX - tanLeft) /
            horizontalRange -
        1.0f;

    *ndcY =
        2.0f *
            (projectedY - tanDown) /
            verticalRange -
        1.0f;

    return
        *ndcX > -1.5f &&
        *ndcX < 1.5f &&
        *ndcY > -1.5f &&
        *ndcY < 1.5f;
}

void VR_AppendControllerProxyVertex(
    std::array<
        VrControllerProxyVertex,
        kVrMaximumControllerProxyVertices>& vertices,
    std::uint32_t* vertexCount,
    const float x,
    const float y,
    const float red,
    const float green,
    const float blue,
    const float alpha)
{
    if (vertexCount == nullptr ||
        *vertexCount >= vertices.size())
    {
        return;
    }

    VrControllerProxyVertex& vertex =
        vertices[*vertexCount];

    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.position[2] = 0.0f;
    vertex.position[3] = 1.0f;

    vertex.color[0] = red;
    vertex.color[1] = green;
    vertex.color[2] = blue;
    vertex.color[3] = alpha;

    ++(*vertexCount);
}

void VR_AppendControllerProxyTriangle(
    std::array<
        VrControllerProxyVertex,
        kVrMaximumControllerProxyVertices>& vertices,
    std::uint32_t* vertexCount,
    const float x0,
    const float y0,
    const float x1,
    const float y1,
    const float x2,
    const float y2,
    const float red,
    const float green,
    const float blue)
{
    VR_AppendControllerProxyVertex(
        vertices,
        vertexCount,
        x0,
        y0,
        red,
        green,
        blue,
        1.0f);

    VR_AppendControllerProxyVertex(
        vertices,
        vertexCount,
        x1,
        y1,
        red,
        green,
        blue,
        1.0f);

    VR_AppendControllerProxyVertex(
        vertices,
        vertexCount,
        x2,
        y2,
        red,
        green,
        blue,
        1.0f);
}

void VR_AppendControllerProxyDiamond(
    std::array<
        VrControllerProxyVertex,
        kVrMaximumControllerProxyVertices>& vertices,
    std::uint32_t* vertexCount,
    const float centerX,
    const float centerY,
    const float halfWidth,
    const float halfHeight,
    const float red,
    const float green,
    const float blue)
{
    VR_AppendControllerProxyTriangle(
        vertices,
        vertexCount,
        centerX,
        centerY + halfHeight,
        centerX - halfWidth,
        centerY,
        centerX,
        centerY - halfHeight,
        red,
        green,
        blue);

    VR_AppendControllerProxyTriangle(
        vertices,
        vertexCount,
        centerX,
        centerY + halfHeight,
        centerX,
        centerY - halfHeight,
        centerX + halfWidth,
        centerY,
        red,
        green,
        blue);
}

void VR_AppendControllerProxyRay(
    std::array<
        VrControllerProxyVertex,
        kVrMaximumControllerProxyVertices>& vertices,
    std::uint32_t* vertexCount,
    const float startX,
    const float startY,
    const float endX,
    const float endY,
    const float halfThickness,
    const float red,
    const float green,
    const float blue)
{
    const float deltaX = endX - startX;
    const float deltaY = endY - startY;

    const float length =
        std::sqrt(
            deltaX * deltaX +
            deltaY * deltaY);

    if (length <= 0.0001f)
    {
        return;
    }

    const float normalX =
        -deltaY / length *
        halfThickness;

    const float normalY =
        deltaX / length *
        halfThickness;

    const float startLeftX = startX + normalX;
    const float startLeftY = startY + normalY;
    const float startRightX = startX - normalX;
    const float startRightY = startY - normalY;
    const float endLeftX = endX + normalX;
    const float endLeftY = endY + normalY;
    const float endRightX = endX - normalX;
    const float endRightY = endY - normalY;

    VR_AppendControllerProxyTriangle(
        vertices,
        vertexCount,
        startLeftX,
        startLeftY,
        endLeftX,
        endLeftY,
        endRightX,
        endRightY,
        red,
        green,
        blue);

    VR_AppendControllerProxyTriangle(
        vertices,
        vertexCount,
        startLeftX,
        startLeftY,
        endRightX,
        endRightY,
        startRightX,
        startRightY,
        red,
        green,
        blue);
}

bool VR_CreateControllerProxyResources()
{
    if (g_vrD3dDevice == nullptr)
    {
        return false;
    }

    if (g_vrControllerProxyResourcesReady)
    {
        return true;
    }

    static const char* vertexShaderSource = R"(
struct ControllerProxyVertex
{
    float4 position : POSITION;
    float4 color : COLOR0;
};

struct ControllerProxyPixel
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

ControllerProxyPixel VSMain(
    ControllerProxyVertex input)
{
    ControllerProxyPixel output;
    output.position = input.position;
    output.color = input.color;
    return output;
}
)";

    static const char* pixelShaderSource = R"(
struct ControllerProxyPixel
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

float4 PSMain(
    ControllerProxyPixel input) : SV_TARGET
{
    return input.color;
}
)";

    ComPtr<ID3DBlob> vertexShaderBlob;
    ComPtr<ID3DBlob> pixelShaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr =
        D3DCompile(
            vertexShaderSource,
            std::strlen(vertexShaderSource),
            "controller_proxy_vs",
            nullptr,
            nullptr,
            "VSMain",
            "vs_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            vertexShaderBlob.GetAddressOf(),
            errorBlob.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "D3DCompile(controller proxy VS)",
            hr);

        return false;
    }

    errorBlob.Reset();

    hr =
        D3DCompile(
            pixelShaderSource,
            std::strlen(pixelShaderSource),
            "controller_proxy_ps",
            nullptr,
            nullptr,
            "PSMain",
            "ps_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            pixelShaderBlob.GetAddressOf(),
            errorBlob.GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "D3DCompile(controller proxy PS)",
            hr);

        return false;
    }

    hr =
        g_vrD3dDevice->CreateVertexShader(
            vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(),
            nullptr,
            g_vrControllerProxyVertexShader
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateVertexShader(controller proxy)",
            hr);

        return false;
    }

    hr =
        g_vrD3dDevice->CreatePixelShader(
            pixelShaderBlob->GetBufferPointer(),
            pixelShaderBlob->GetBufferSize(),
            nullptr,
            g_vrControllerProxyPixelShader
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreatePixelShader(controller proxy)",
            hr);

        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {
            "POSITION",
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            0,
            0,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
        {
            "COLOR",
            0,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            0,
            16,
            D3D11_INPUT_PER_VERTEX_DATA,
            0,
        },
    };

    hr =
        g_vrD3dDevice->CreateInputLayout(
            inputElements,
            static_cast<UINT>(
                sizeof(inputElements) /
                sizeof(inputElements[0])),
            vertexShaderBlob->GetBufferPointer(),
            vertexShaderBlob->GetBufferSize(),
            g_vrControllerProxyInputLayout
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateInputLayout(controller proxy)",
            hr);

        return false;
    }

    D3D11_BUFFER_DESC bufferDescription = {};
    bufferDescription.ByteWidth =
        sizeof(VrControllerProxyVertex) *
        kVrMaximumControllerProxyVertices;
    bufferDescription.Usage =
        D3D11_USAGE_DYNAMIC;
    bufferDescription.BindFlags =
        D3D11_BIND_VERTEX_BUFFER;
    bufferDescription.CPUAccessFlags =
        D3D11_CPU_ACCESS_WRITE;

    hr =
        g_vrD3dDevice->CreateBuffer(
            &bufferDescription,
            nullptr,
            g_vrControllerProxyVertexBuffer
                .GetAddressOf());

    if (FAILED(hr))
    {
        VR_LogHrFailure(
            "CreateBuffer(controller proxy)",
            hr);

        return false;
    }

    g_vrControllerProxyResourcesReady = true;

    Com_Printf(
        0,
        "[VR] Created visible OpenXR controller "
        "proxy renderer.\n");

    return true;
}

bool VR_TrackedHandDiagnosticsEnabledInternal()
{
    if (!VR_VerboseDiagnosticsEnabled())
    {
        return false;
    }

    static const bool enabled = []()
    {
        const char* setting =
            std::getenv(
                "KISAK_VR_HAND_DIAGNOSTICS");

        const bool diagnosticsEnabled =
            setting == nullptr ||
            std::strcmp(setting, "0") != 0;

        if (diagnosticsEnabled)
        {
            Com_Printf(
                0,
                "[VR][HANDS][DIAG] Left-hand transform diagnostics remain enabled; V28 removed the colored controller origin/axis overlay.\n");
        }
        else
        {
            Com_Printf(
                0,
                "[VR][HANDS][DIAG] Left-hand transform diagnostics are disabled by KISAK_VR_HAND_DIAGNOSTICS=0.\n");
        }

        return diagnosticsEnabled;
    }();

    return enabled;
}

void VR_RenderControllerProxies(
    const std::uint32_t eyeIndex,
    const int32_t viewportWidth,
    const int32_t viewportHeight)
{
    {
        std::lock_guard<std::mutex> lock(
            g_vrScopeStateMutex);

        if (g_vrScopeActive)
        {
            return;
        }
    }

    if (!VR_TrackedHandDiagnosticsEnabledInternal() ||
        !g_vrControllerProxyResourcesReady ||
        eyeIndex >= g_vrViews.size() ||
        viewportWidth <= 0 ||
        viewportHeight <= 0)
    {
        return;
    }

    std::array<
        VrControllerProxyVertex,
        kVrMaximumControllerProxyVertices>
        vertices = {};

    std::uint32_t vertexCount = 0u;

    const float markerHalfWidth =
        9.0f /
        static_cast<float>(viewportWidth);

    const float markerHalfHeight =
        9.0f /
        static_cast<float>(viewportHeight);

    const float rayHalfThickness =
        2.5f /
        static_cast<float>(viewportHeight);

    const VrControllerRenderPose& controller =
        g_vrControllerRenderPoses[
            VrInteractions::OffHandControllerIndex(
                VR_GetConfiguratorSettings().dominantHand)];

    const bool handSurfaceValid =
        controller.palmValid ||
        controller.gripValid;

    const XrPosef& handSurfacePose =
        controller.palmValid
            ? controller.palmPose
            : controller.gripPose;

    if (handSurfaceValid)
    {
        float gripX = 0.0f;
        float gripY = 0.0f;

        const bool gripVisible =
            VR_ProjectAppSpacePointToEye(
                handSurfacePose.position,
                g_vrViews[eyeIndex],
                &gripX,
                &gripY);

        if (gripVisible)
        {
            VR_AppendControllerProxyDiamond(
                vertices,
                &vertexCount,
                gripX,
                gripY,
                markerHalfWidth,
                markerHalfHeight,
                0.1f,
                0.85f,
                1.0f);
        }

        const VrHeadVector gripDirections[3] = {
            VR_RotateHeadVector(
                handSurfacePose.orientation,
                {0.0f, 0.0f, -1.0f}),
            VR_RotateHeadVector(
                handSurfacePose.orientation,
                {-1.0f, 0.0f, 0.0f}),
            VR_RotateHeadVector(
                handSurfacePose.orientation,
                {0.0f, 1.0f, 0.0f}),
        };

        const float axisColors[3][3] = {
            {1.0f, 0.05f, 0.05f},
            {0.05f, 1.0f, 0.15f},
            {0.1f, 0.35f, 1.0f},
        };

        constexpr float axisLengthMeters =
            0.14f;

        for (int axisIndex = 0;
             axisIndex < 3;
             ++axisIndex)
        {
            const XrVector3f axisEnd =
                VR_ControllerAddScaled(
                    handSurfacePose.position,
                    gripDirections[axisIndex],
                    axisLengthMeters);

            float axisEndX = 0.0f;
            float axisEndY = 0.0f;

            const bool axisEndVisible =
                VR_ProjectAppSpacePointToEye(
                    axisEnd,
                    g_vrViews[eyeIndex],
                    &axisEndX,
                    &axisEndY);

            if (gripVisible &&
                axisEndVisible)
            {
                VR_AppendControllerProxyRay(
                    vertices,
                    &vertexCount,
                    gripX,
                    gripY,
                    axisEndX,
                    axisEndY,
                    rayHalfThickness,
                    axisColors[axisIndex][0],
                    axisColors[axisIndex][1],
                    axisColors[axisIndex][2]);
            }
        }
    }

    if (vertexCount == 0u)
    {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};

    const HRESULT mapResult =
        g_vrD3dContext->Map(
            g_vrControllerProxyVertexBuffer.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped);

    if (FAILED(mapResult) ||
        mapped.pData == nullptr)
    {
        return;
    }

    std::memcpy(
        mapped.pData,
        vertices.data(),
        sizeof(VrControllerProxyVertex) *
            vertexCount);

    g_vrD3dContext->Unmap(
        g_vrControllerProxyVertexBuffer.Get(),
        0);

    const UINT stride =
        sizeof(VrControllerProxyVertex);

    const UINT offset = 0u;

    ID3D11Buffer* vertexBuffer =
        g_vrControllerProxyVertexBuffer.Get();

    g_vrD3dContext->IASetInputLayout(
        g_vrControllerProxyInputLayout.Get());

    g_vrD3dContext->IASetVertexBuffers(
        0,
        1,
        &vertexBuffer,
        &stride,
        &offset);

    g_vrD3dContext->IASetIndexBuffer(
        nullptr,
        DXGI_FORMAT_UNKNOWN,
        0);

    g_vrD3dContext->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_vrD3dContext->VSSetShader(
        g_vrControllerProxyVertexShader.Get(),
        nullptr,
        0);

    g_vrD3dContext->PSSetShader(
        g_vrControllerProxyPixelShader.Get(),
        nullptr,
        0);

    g_vrD3dContext->RSSetState(nullptr);

    g_vrD3dContext->OMSetDepthStencilState(
        nullptr,
        0);

    g_vrD3dContext->OMSetBlendState(
        nullptr,
        nullptr,
        0xFFFFFFFFu);

    g_vrD3dContext->Draw(
        vertexCount,
        0);

    if (!g_vrLoggedFirstControllerProxyDraw)
    {
        Com_Printf(
            0,
            "[VR][HANDS][DIAG] Rendered first compositor-true "
            "off-hand grip origin/axis proxy.\n");

        g_vrLoggedFirstControllerProxyDraw = true;
    }
}

bool VR_ConfigureHudConvergedCaptureViewport(
    const XrFovf& destinationFov,
    const int32_t targetWidth,
    const int32_t targetHeight,
    D3D11_VIEWPORT* viewport)
{
    if (targetWidth <= 0 ||
        targetHeight <= 0 ||
        viewport == nullptr)
    {
        return false;
    }

    const float tanLeft =
        std::tan(destinationFov.angleLeft);

    const float tanRight =
        std::tan(destinationFov.angleRight);

    const float tanDown =
        std::tan(destinationFov.angleDown);

    const float tanUp =
        std::tan(destinationFov.angleUp);

    const float horizontalSpan =
        tanRight - tanLeft;

    const float verticalSpan =
        tanUp - tanDown;

    const float symmetricHorizontal =
        (std::max)(-tanLeft, tanRight);

    const float symmetricVertical =
        (std::max)(-tanDown, tanUp);

    if (horizontalSpan <= 0.0f ||
        verticalSpan <= 0.0f ||
        symmetricHorizontal <= 0.0f ||
        symmetricVertical <= 0.0f)
    {
        return false;
    }

    // Map centered source NDC into the runtime's asymmetric destination
    // NDC.  Because each symmetric half-FOV is the larger side, both scales
    // are at least one and the oversized viewport is safely cropped rather
    // than exposing an unrendered border.
    const float scaleX =
        2.0f * symmetricHorizontal /
        horizontalSpan;

    const float offsetX =
        -(tanRight + tanLeft) /
        horizontalSpan;

    const float scaleY =
        2.0f * symmetricVertical /
        verticalSpan;

    const float offsetY =
        -(tanUp + tanDown) /
        verticalSpan;

    viewport->TopLeftX =
        0.5f *
        (1.0f + offsetX - scaleX) *
        static_cast<float>(targetWidth);

    viewport->TopLeftY =
        0.5f *
        (1.0f - offsetY - scaleY) *
        static_cast<float>(targetHeight);

    viewport->Width =
        scaleX *
        static_cast<float>(targetWidth);

    viewport->Height =
        scaleY *
        static_cast<float>(targetHeight);

    viewport->MinDepth = 0.0f;
    viewport->MaxDepth = 1.0f;

    static bool loggedHudConvergence = false;

    if (!loggedHudConvergence)
    {
        Com_Printf(
            0,
            "[VR][HUD] Applied asymmetric-frustum correction; "
            "shared 2D HUD elements now converge between eyes.\n");

        loggedHudConvergence = true;
    }

    return true;
}

void VR_SetFullEyeViewport(
    const int32_t targetWidth,
    const int32_t targetHeight)
{
    if (targetWidth <= 0 ||
        targetHeight <= 0)
    {
        return;
    }

    D3D11_VIEWPORT viewport = {};
    viewport.Width =
        static_cast<float>(targetWidth);
    viewport.Height =
        static_cast<float>(targetHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    g_vrD3dContext->RSSetViewports(
        1,
        &viewport);
}

bool VR_RenderFsrUpscaledEye(
    const std::uint32_t eyeIndex,
    const int32_t outputWidth,
    const int32_t outputHeight,
    ID3D11ShaderResourceView* capturedView,
    ID3D11RenderTargetView* outputTarget,
    const D3D11_VIEWPORT& outputViewport)
{
    if (!g_vrFsrEnabled ||
        capturedView == nullptr ||
        outputTarget == nullptr ||
        eyeIndex >= g_vrBlitVertexBuffers.size() ||
        outputWidth <= 0 ||
        outputHeight <= 0 ||
        g_vrCapturedStereoWidth < 2u ||
        g_vrCapturedStereoHeight == 0u ||
        !g_vrFsrEasuPixelShader ||
        !g_vrFsrRcasPixelShader ||
        !g_vrFsrConstantBuffer ||
        !g_vrFsrIntermediateTarget ||
        !g_vrFsrIntermediateView)
    {
        return false;
    }

    // KISAK_SP_VR_FIXED_SCOPED_TURRET_RUNTIME_V4
    // The fixed scope's variable magnification is performed by the direct
    // compositor shader. Temporarily use that path even if FSR is generally
    // enabled; ordinary gameplay resumes FSR as soon as the scope is left.
    bool fixedScopedTurretActive = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrScopeStateMutex);

        fixedScopedTurretActive =
            g_vrFixedScopedTurretActive;
    }

    if (fixedScopedTurretActive)
    {
        if (!g_vrLoggedFixedScopedTurretFsrBypass)
        {
            Com_Printf(
                0,
                "[VR][FIXED SCOPE] Using the direct compositor while "
                "variable scope zoom is active.\n");

            g_vrLoggedFixedScopedTurretFsrBypass = true;
        }

        return false;
    }

    const std::uint32_t inputWidth =
        VR_GetCapturedMainStereoWidth() / 2u;

    const std::uint32_t inputHeight =
        g_vrCapturedStereoHeight;

    const std::uint64_t inputArea =
        static_cast<std::uint64_t>(inputWidth) *
        static_cast<std::uint64_t>(inputHeight);

    const std::uint64_t outputArea =
        static_cast<std::uint64_t>(outputWidth) *
        static_cast<std::uint64_t>(outputHeight);

    // FSR 1 EASU is defined for 1x through 4x area upscaling.
    if (static_cast<std::uint32_t>(outputWidth) < inputWidth ||
        static_cast<std::uint32_t>(outputHeight) < inputHeight ||
        outputArea > inputArea * 4u)
    {
        if (!g_vrLoggedFsrFallback)
        {
            Com_PrintWarning(
                0,
                "[VR] FSR bypassed because %u x %u to %d x %d "
                "is outside its 1x-to-4x area upscale range; "
                "using bilinear fallback.\n",
                inputWidth,
                inputHeight,
                outputWidth,
                outputHeight);

            g_vrLoggedFsrFallback = true;
        }

        return false;
    }

    VrFsrConstants constants = {};

    const std::uint32_t inputOriginX =
        eyeIndex * inputWidth;

    constants.inputRect[0] =
        static_cast<float>(inputOriginX);
    constants.inputRect[1] = 0.0f;
    constants.inputRect[2] =
        static_cast<float>(inputWidth);
    constants.inputRect[3] =
        static_cast<float>(inputHeight);

    constants.inputSize[0] =
        static_cast<float>(g_vrCapturedStereoWidth);
    constants.inputSize[1] =
        static_cast<float>(g_vrCapturedStereoHeight);
    constants.inputSize[2] =
        static_cast<float>(
            inputOriginX + inputWidth - 1u);
    constants.inputSize[3] =
        static_cast<float>(inputHeight - 1u);

    constants.outputSize[0] =
        static_cast<float>(outputWidth);
    constants.outputSize[1] =
        static_cast<float>(outputHeight);
    constants.outputSize[2] =
        1.0f / static_cast<float>(outputWidth);
    constants.outputSize[3] =
        1.0f / static_cast<float>(outputHeight);

    constants.settings[0] =
        g_vrFsrSharpness;

    D3D11_MAPPED_SUBRESOURCE mapped = {};

    const HRESULT mapResult =
        g_vrD3dContext->Map(
            g_vrFsrConstantBuffer.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped);

    if (FAILED(mapResult) ||
        mapped.pData == nullptr)
    {
        return false;
    }

    std::memcpy(
        mapped.pData,
        &constants,
        sizeof(constants));

    g_vrD3dContext->Unmap(
        g_vrFsrConstantBuffer.Get(),
        0);

    ID3D11ShaderResourceView* nullView = nullptr;

    g_vrD3dContext->PSSetShaderResources(
        0,
        1,
        &nullView);

    ID3D11RenderTargetView* intermediateTarget =
        g_vrFsrIntermediateTarget.Get();

    g_vrD3dContext->OMSetRenderTargets(
        1,
        &intermediateTarget,
        nullptr);

    D3D11_VIEWPORT intermediateViewport = {};
    intermediateViewport.Width =
        static_cast<float>(outputWidth);
    intermediateViewport.Height =
        static_cast<float>(outputHeight);
    intermediateViewport.MinDepth = 0.0f;
    intermediateViewport.MaxDepth = 1.0f;

    g_vrD3dContext->RSSetViewports(
        1,
        &intermediateViewport);

    g_vrD3dContext->RSSetState(
        g_vrTestRasterizerState.Get());

    g_vrD3dContext->OMSetDepthStencilState(
        nullptr,
        0);

    g_vrD3dContext->IASetInputLayout(
        g_vrBlitInputLayout.Get());

    const UINT stride =
        sizeof(VrBlitVertex);

    const UINT offset = 0u;

    ID3D11Buffer* eyeVertexBuffer =
        g_vrBlitVertexBuffers[eyeIndex].Get();

    g_vrD3dContext->IASetVertexBuffers(
        0,
        1,
        &eyeVertexBuffer,
        &stride,
        &offset);

    g_vrD3dContext->IASetIndexBuffer(
        nullptr,
        DXGI_FORMAT_UNKNOWN,
        0);

    g_vrD3dContext->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    g_vrD3dContext->VSSetShader(
        g_vrBlitVertexShader.Get(),
        nullptr,
        0);

    g_vrD3dContext->PSSetShader(
        g_vrFsrEasuPixelShader.Get(),
        nullptr,
        0);

    ID3D11Buffer* fsrConstantBuffer =
        g_vrFsrConstantBuffer.Get();

    g_vrD3dContext->PSSetConstantBuffers(
        2,
        1,
        &fsrConstantBuffer);

    g_vrD3dContext->PSSetShaderResources(
        0,
        1,
        &capturedView);

    g_vrD3dContext->Draw(4, 0);

    g_vrD3dContext->PSSetShaderResources(
        0,
        1,
        &nullView);

    g_vrD3dContext->OMSetRenderTargets(
        1,
        &outputTarget,
        nullptr);

    g_vrD3dContext->RSSetViewports(
        1,
        &outputViewport);

    ID3D11Buffer* fullTextureVertexBuffer =
        g_vrMenuBlitVertexBuffer.Get();

    g_vrD3dContext->IASetVertexBuffers(
        0,
        1,
        &fullTextureVertexBuffer,
        &stride,
        &offset);

    g_vrD3dContext->PSSetShader(
        g_vrFsrRcasPixelShader.Get(),
        nullptr,
        0);

    ID3D11Buffer* compositorConstantBuffer =
        g_vrCompositorConstantBuffer.Get();

    g_vrD3dContext->PSSetConstantBuffers(
        1,
        1,
        &compositorConstantBuffer);

    ID3D11ShaderResourceView* intermediateView =
        g_vrFsrIntermediateView.Get();

    g_vrD3dContext->PSSetShaderResources(
        0,
        1,
        &intermediateView);

    g_vrD3dContext->Draw(4, 0);

    g_vrD3dContext->PSSetShaderResources(
        0,
        1,
        &nullView);

    if (!g_vrLoggedFirstFsrFrame)
    {
        Com_Printf(
            0,
            "[VR] FSR 1 EASU/RCAS compositor active: "
            "%u x %u per eye to %d x %d, RCAS %.2f.\n",
            inputWidth,
            inputHeight,
            outputWidth,
            outputHeight,
            g_vrFsrSharpness);

        g_vrLoggedFirstFsrFrame = true;
    }

    return true;
}

void VR_RenderPhysicalSniperScope(
    const std::uint32_t eyeIndex,
    const int32_t viewportWidth,
    const int32_t viewportHeight,
    const XrView& sourceView)
{
    if (eyeIndex >= g_vrViews.size() ||
        eyeIndex >= g_vrBlitVertexBuffers.size() ||
        viewportWidth <= 0 ||
        viewportHeight <= 0 ||
        !g_vrScopePixelShader ||
        !g_vrScopeConstantBuffer ||
        !g_vrCapturedStereoView)
    {
        return;
    }

    bool scopeActive = false;
    float adsFraction = 0.0f;
    float adsFovDegrees = 65.0f;

    {
        std::lock_guard<std::mutex> lock(
            g_vrScopeStateMutex);

        scopeActive = g_vrScopeActive;
        adsFraction = g_vrScopeAdsFraction;
        adsFovDegrees = g_vrScopeAdsFovDegrees;
    }

    if (!scopeActive ||
        adsFraction <= 0.01f ||
        adsFovDegrees <= 1.0f)
    {
        return;
    }

    XrVector3f controllerPosition = {};
    XrQuaternionf controllerOrientation = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };

    float finalWeaponAxisCameraLocal[3][3] = {};
    bool finalWeaponAxisCameraLocalValid = false;
    float scopeOffsetWeaponLocal[3] = {};
    bool scopeOffsetWeaponLocalValid = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrRightControllerWeaponFilterValid)
        {
            return;
        }

        controllerPosition =
            g_vrRightControllerFilteredGripPosition;

        controllerOrientation =
            g_vrRightControllerFilteredAimOrientation;

        finalWeaponAxisCameraLocalValid =
            g_vrRightControllerFinalWeaponAxisCameraLocalValid;

        if (finalWeaponAxisCameraLocalValid)
        {
            std::memcpy(
                finalWeaponAxisCameraLocal,
                g_vrRightControllerFinalWeaponAxisCameraLocal,
                sizeof(finalWeaponAxisCameraLocal));
        }

        scopeOffsetWeaponLocalValid =
            g_vrPhysicalSniperScopeOffsetWeaponLocalValid;

        if (scopeOffsetWeaponLocalValid)
        {
            std::memcpy(
                scopeOffsetWeaponLocal,
                g_vrPhysicalSniperScopeOffsetWeaponLocal,
                sizeof(scopeOffsetWeaponLocal));
        }
    }

    VrHeadVector scopeForward =
        VR_RotateHeadVector(
            controllerOrientation,
            {0.0f, 0.0f, -1.0f});

    VrHeadVector scopeRight =
        VR_RotateHeadVector(
            controllerOrientation,
            {1.0f, 0.0f, 0.0f});

    VrHeadVector scopeUp =
        VR_RotateHeadVector(
            controllerOrientation,
            {0.0f, 1.0f, 0.0f});

    if (finalWeaponAxisCameraLocalValid)
    {
        const XrQuaternionf headOrientation =
            VR_NormalizeQuaternion(
                g_vrViews[0].pose.orientation);

        // Inverse of VR_OpenXrVectorToCod():
        // CoD (+forward,+left,+up) -> OpenXR (-left,+up,-forward).
        const VrHeadVector scopeForwardHeadLocal = {
            -finalWeaponAxisCameraLocal[0][1],
            finalWeaponAxisCameraLocal[0][2],
            -finalWeaponAxisCameraLocal[0][0],
        };

        // Weapon axis row 1 is left; negate its OpenXR conversion for right.
        const VrHeadVector scopeRightHeadLocal = {
            finalWeaponAxisCameraLocal[1][1],
            -finalWeaponAxisCameraLocal[1][2],
            finalWeaponAxisCameraLocal[1][0],
        };

        const VrHeadVector scopeUpHeadLocal = {
            -finalWeaponAxisCameraLocal[2][1],
            finalWeaponAxisCameraLocal[2][2],
            -finalWeaponAxisCameraLocal[2][0],
        };

        scopeForward =
            VR_RotateHeadVector(
                headOrientation,
                scopeForwardHeadLocal);

        scopeRight =
            VR_RotateHeadVector(
                headOrientation,
                scopeRightHeadLocal);

        scopeUp =
            VR_RotateHeadVector(
                headOrientation,
                scopeUpHeadLocal);

        static bool loggedFinalWeaponScopeAxis = false;

        if (!loggedFinalWeaponScopeAxis)
        {
            Com_Printf(
                0,
                "[VR] Physical sniper scope now follows the final "
                "rendered weapon axis.\n");

            loggedFinalWeaponScopeAxis = true;
        }
    }

    const float scopeForwardMeters =
        scopeOffsetWeaponLocalValid
            ? scopeOffsetWeaponLocal[0] /
                kVrGameUnitsPerMeter
            : 0.22f +
                g_vrScopeForwardCalibrationMeters;

    const float scopeLeftMeters =
        scopeOffsetWeaponLocalValid
            ? scopeOffsetWeaponLocal[1] /
                kVrGameUnitsPerMeter
            : g_vrScopeLeftCalibrationMeters;

    const float scopeUpMeters =
        scopeOffsetWeaponLocalValid
            ? scopeOffsetWeaponLocal[2] /
                kVrGameUnitsPerMeter
            : 0.055f +
                g_vrScopeUpCalibrationMeters;

    XrVector3f lensCenter =
        VR_ControllerAddScaled(
            controllerPosition,
            scopeForward,
            scopeForwardMeters);

    lensCenter =
        VR_ControllerAddScaled(
            lensCenter,
            scopeRight,
            -scopeLeftMeters);

    lensCenter =
        VR_ControllerAddScaled(
            lensCenter,
            scopeUp,
            scopeUpMeters);

    const XrVector3f lensRight =
        VR_ControllerAddScaled(
            lensCenter,
            scopeRight,
            g_vrScopeLensRadiusMeters);

    const XrVector3f lensUp =
        VR_ControllerAddScaled(
            lensCenter,
            scopeUp,
            g_vrScopeLensRadiusMeters);

    const XrVector3f aimPoint =
        VR_ControllerAddScaled(
            lensCenter,
            scopeForward,
            32.0f);

    // The captured source is now rendered with a centered symmetric
    // frustum.  Project its magnified sample through that same source
    // frustum; the physical lens itself remains in the true asymmetric
    // destination eye view.
    XrView symmetricSourceView = sourceView;

    const float sourceTanLeft =
        std::tan(sourceView.fov.angleLeft);

    const float sourceTanRight =
        std::tan(sourceView.fov.angleRight);

    const float sourceTanDown =
        std::tan(sourceView.fov.angleDown);

    const float sourceTanUp =
        std::tan(sourceView.fov.angleUp);

    const float sourceHalfFovX =
        (std::max)(-sourceTanLeft, sourceTanRight);

    const float sourceHalfFovY =
        (std::max)(-sourceTanDown, sourceTanUp);

    symmetricSourceView.fov.angleLeft =
        std::atan(-sourceHalfFovX);

    symmetricSourceView.fov.angleRight =
        std::atan(sourceHalfFovX);

    symmetricSourceView.fov.angleDown =
        std::atan(-sourceHalfFovY);

    symmetricSourceView.fov.angleUp =
        std::atan(sourceHalfFovY);

    float lensCenterX = 0.0f;
    float lensCenterY = 0.0f;
    float lensRightX = 0.0f;
    float lensRightY = 0.0f;
    float lensUpX = 0.0f;
    float lensUpY = 0.0f;
    float aimX = 0.0f;
    float aimY = 0.0f;

    if (!VR_ProjectAppSpacePointToEye(
            lensCenter,
            g_vrViews[eyeIndex],
            &lensCenterX,
            &lensCenterY) ||
        !VR_ProjectAppSpacePointToEye(
            lensRight,
            g_vrViews[eyeIndex],
            &lensRightX,
            &lensRightY) ||
        !VR_ProjectAppSpacePointToEye(
            lensUp,
            g_vrViews[eyeIndex],
            &lensUpX,
            &lensUpY) ||
        !VR_ProjectAppSpacePointToEye(
            aimPoint,
            symmetricSourceView,
            &aimX,
            &aimY))
    {
        return;
    }

    const float lensCenterU =
        0.5f * (lensCenterX + 1.0f);

    const float lensCenterV =
        0.5f * (1.0f - lensCenterY);

    const float lensRightU =
        0.5f * (lensRightX + 1.0f);

    const float lensRightV =
        0.5f * (1.0f - lensRightY);

    const float lensUpU =
        0.5f * (lensUpX + 1.0f);

    const float lensUpV =
        0.5f * (1.0f - lensUpY);

    // KISAK_VR_RIFLE_ATTACHED_SCOPE_V1
    // Preserve the optic's physical angular size.  The former 11.5 percent
    // minimum made the lens look detached from the rifle whenever it was
    // held farther from the eye.
    float basisRightU =
        lensRightU - lensCenterU;

    float basisRightV =
        lensRightV - lensCenterV;

    float basisUpU =
        lensUpU - lensCenterU;

    float basisUpV =
        lensUpV - lensCenterV;

    const float rightBasisLength =
        std::sqrt(
            basisRightU * basisRightU +
            basisRightV * basisRightV);

    const float upBasisLength =
        std::sqrt(
            basisUpU * basisUpU +
            basisUpV * basisUpV);

    const float averageBasisLength =
        0.5f *
        (rightBasisLength + upBasisLength);

    float basisScale = 1.0f;

    if (averageBasisLength > 0.0001f)
    {
        if (averageBasisLength > 0.18f)
        {
            basisScale =
                0.18f /
                averageBasisLength;
        }
    }

    basisRightU *= basisScale;
    basisRightV *= basisScale;
    basisUpU *= basisScale;
    basisUpV *= basisScale;

    const float determinant =
        basisRightU * basisUpV -
        basisUpU * basisRightV;

    if (std::abs(determinant) < 0.00001f)
    {
        return;
    }

    constexpr float kPi =
        3.14159265358979323846f;

    const float sourceVerticalFov =
        symmetricSourceView.fov.angleUp -
        symmetricSourceView.fov.angleDown;

    const float adsFovRadians =
        adsFovDegrees *
        (kPi / 180.0f);

    float magnification =
        std::tan(0.5f * sourceVerticalFov) /
        std::tan(0.5f * adsFovRadians);

    magnification =
        (std::max)(
            1.5f,
            (std::min)(3.0f, magnification));

    int mainStereoWidth = 0;
    int scopePanelX = 0;
    int scopePanelY = 0;
    int scopePanelSize = 0;

    const bool dedicatedScopeSource =
        VR_GetPhysicalSniperScopeCaptureLayout(
            static_cast<int>(
                g_vrCapturedStereoWidth),
            static_cast<int>(
                g_vrCapturedStereoHeight),
            &mainStereoWidth,
            &scopePanelX,
            &scopePanelY,
            &scopePanelSize);

    if (!dedicatedScopeSource &&
        !g_vrLoggedDedicatedScopeLayoutMissing &&
        g_vrEyeSwapchains.size() >=
            kVrStereoEyeCount)
    {
        const int requiredWidth =
            g_vrEyeSwapchains[0].width +
            g_vrEyeSwapchains[1].width +
            g_vrScopeCaptureSizePixels;

        const int requiredHeight =
            (std::max)(
                g_vrScopeCaptureSizePixels,
                (std::max)(
                    g_vrEyeSwapchains[0].height,
                    g_vrEyeSwapchains[1].height));

        Com_PrintWarning(
            0,
            "[VR] Dedicated scope camera is inactive: "
            "the %u x %u capture needs at least %d x %d "
            "for native stereo plus the %d px scope panel. "
            "Using the legacy eye-image crop.\n",
            g_vrCapturedStereoWidth,
            g_vrCapturedStereoHeight,
            requiredWidth,
            requiredHeight,
            g_vrScopeCaptureSizePixels);

        g_vrLoggedDedicatedScopeLayoutMissing =
            true;
    }

    VrScopeConstants constants = {};

    constants.lens[0] = lensCenterU;
    constants.lens[1] = lensCenterV;
    constants.lens[2] = adsFraction;
    constants.lens[3] =
        dedicatedScopeSource
            ? 1.0f
            : 0.0f;

    constants.basis[0] = basisRightU;
    constants.basis[1] = basisRightV;
    constants.basis[2] = basisUpU;
    constants.basis[3] = basisUpV;

    if (dedicatedScopeSource)
    {
        const float inverseCaptureWidth =
            1.0f /
            static_cast<float>(
                g_vrCapturedStereoWidth);

        const float inverseCaptureHeight =
            1.0f /
            static_cast<float>(
                g_vrCapturedStereoHeight);

        const float sourceInsetPixels = 2.0f;

        constants.sample[0] =
            (static_cast<float>(scopePanelX) +
             0.5f * static_cast<float>(scopePanelSize)) *
            inverseCaptureWidth;

        constants.sample[1] =
            (static_cast<float>(scopePanelY) +
             0.5f * static_cast<float>(scopePanelSize)) *
            inverseCaptureHeight;

        constants.sample[2] =
            (0.5f * static_cast<float>(scopePanelSize) -
             sourceInsetPixels) *
            inverseCaptureWidth;

        constants.sample[3] =
            (0.5f * static_cast<float>(scopePanelSize) -
             sourceInsetPixels) *
            inverseCaptureHeight;

        constants.bounds[0] =
            (static_cast<float>(scopePanelX) +
             sourceInsetPixels) *
            inverseCaptureWidth;

        constants.bounds[1] =
            (static_cast<float>(
                 scopePanelX + scopePanelSize) -
             sourceInsetPixels) *
            inverseCaptureWidth;

        constants.bounds[2] =
            (static_cast<float>(scopePanelY) +
             sourceInsetPixels) *
            inverseCaptureHeight;

        constants.bounds[3] =
            (static_cast<float>(
                 scopePanelY + scopePanelSize) -
             sourceInsetPixels) *
            inverseCaptureHeight;

        if (!g_vrLoggedDedicatedScopeSample)
        {
            Com_Printf(
                0,
                "[VR] Physical lens now samples the dedicated "
                "%d x %d weapon-free scope camera.\n",
                scopePanelSize,
                scopePanelSize);

            g_vrLoggedDedicatedScopeSample = true;
        }
    }
    else
    {
        const float mainStereoFraction =
            g_vrCapturedStereoWidth > 0u
                ? static_cast<float>(
                      VR_GetCapturedMainStereoWidth()) /
                    static_cast<float>(
                        g_vrCapturedStereoWidth)
                : 1.0f;

        constants.sample[0] =
            mainStereoFraction *
            (static_cast<float>(eyeIndex) * 0.5f +
             0.25f * (aimX + 1.0f));

        constants.sample[1] =
            0.5f * (1.0f - aimY);

        constants.sample[2] =
            1.0f / magnification;

        constants.bounds[0] =
            mainStereoFraction *
            (static_cast<float>(eyeIndex) * 0.5f +
             0.001f);

        constants.bounds[1] =
            mainStereoFraction *
            (static_cast<float>(eyeIndex + 1u) * 0.5f -
             0.001f);

        constants.bounds[2] = 0.001f;
        constants.bounds[3] = 0.999f;
    }

    constants.viewport[0] =
        1.0f /
        static_cast<float>(viewportWidth);

    constants.viewport[1] =
        1.0f /
        static_cast<float>(viewportHeight);

    constants.viewport[2] =
        1.0f /
        static_cast<float>(
            g_vrCapturedStereoWidth);

    constants.viewport[3] =
        1.0f /
        static_cast<float>(
            g_vrCapturedStereoHeight);

    D3D11_MAPPED_SUBRESOURCE mapped = {};

    const HRESULT mapResult =
        g_vrD3dContext->Map(
            g_vrScopeConstantBuffer.Get(),
            0,
            D3D11_MAP_WRITE_DISCARD,
            0,
            &mapped);

    if (FAILED(mapResult) ||
        mapped.pData == nullptr)
    {
        return;
    }

    std::memcpy(
        mapped.pData,
        &constants,
        sizeof(constants));

    g_vrD3dContext->Unmap(
        g_vrScopeConstantBuffer.Get(),
        0);

    const UINT stride =
        sizeof(VrBlitVertex);

    const UINT offset = 0u;

    ID3D11Buffer* vertexBuffer =
        g_vrBlitVertexBuffers[eyeIndex].Get();

    g_vrD3dContext->IASetInputLayout(
        g_vrBlitInputLayout.Get());

    g_vrD3dContext->IASetVertexBuffers(
        0,
        1,
        &vertexBuffer,
        &stride,
        &offset);

    g_vrD3dContext->IASetIndexBuffer(
        nullptr,
        DXGI_FORMAT_UNKNOWN,
        0);

    g_vrD3dContext->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    g_vrD3dContext->VSSetShader(
        g_vrBlitVertexShader.Get(),
        nullptr,
        0);

    g_vrD3dContext->PSSetShader(
        g_vrScopePixelShader.Get(),
        nullptr,
        0);

    ID3D11ShaderResourceView* capturedView =
        g_vrCapturedStereoView.Get();

    g_vrD3dContext->PSSetShaderResources(
        0,
        1,
        &capturedView);

    ID3D11SamplerState* sampler =
        g_vrBlitSampler.Get();

    g_vrD3dContext->PSSetSamplers(
        0,
        1,
        &sampler);

    ID3D11Buffer* constantBuffer =
        g_vrScopeConstantBuffer.Get();

    g_vrD3dContext->PSSetConstantBuffers(
        0,
        1,
        &constantBuffer);

    g_vrD3dContext->OMSetBlendState(
        nullptr,
        nullptr,
        0xFFFFFFFFu);

    g_vrD3dContext->Draw(4, 0);

    ID3D11ShaderResourceView* nullView = nullptr;
    ID3D11Buffer* nullBuffer = nullptr;

    g_vrD3dContext->PSSetShaderResources(
        0,
        1,
        &nullView);

    g_vrD3dContext->PSSetConstantBuffers(
        0,
        1,
        &nullBuffer);

    if (!g_vrLoggedFirstPhysicalScopeDraw)
    {
        Com_Printf(
            0,
            "[VR] Rendered first rifle-attached physical scope.\n");

        g_vrLoggedFirstPhysicalScopeDraw = true;
    }
}

const char* VR_ControllerHandName(
    const std::uint32_t handIndex)
{
    return
        handIndex == VR_CONTROLLER_LEFT
            ? "left"
            : "right";
}

bool VR_ControllerStringToPath(
    const char* pathString,
    XrPath* path)
{
    if (pathString == nullptr ||
        path == nullptr)
    {
        return false;
    }

    const XrResult result =
        xrStringToPath(
            g_vrInstance,
            pathString,
            path);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrStringToPath",
            result);

        return false;
    }

    return true;
}

void VR_DestroyControllerInput()
{
    for (XrSpace& space :
         g_vrControllerAimSpaces)
    {
        if (space != XR_NULL_HANDLE)
        {
            xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }

    for (XrSpace& space :
         g_vrControllerGripSpaces)
    {
        if (space != XR_NULL_HANDLE)
        {
            xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }

    for (XrSpace& space :
         g_vrControllerPalmSpaces)
    {
        if (space != XR_NULL_HANDLE)
        {
            xrDestroySpace(space);
            space = XR_NULL_HANDLE;
        }
    }

    if (g_vrHapticOutputAction != XR_NULL_HANDLE)
    {
        xrDestroyAction(g_vrHapticOutputAction);
        g_vrHapticOutputAction = XR_NULL_HANDLE;
    }

    if (g_vrNightVisionGestureGripAction != XR_NULL_HANDLE)
    {
        xrDestroyAction(
            g_vrNightVisionGestureGripAction);
        g_vrNightVisionGestureGripAction =
            XR_NULL_HANDLE;
    }

    for (auto& actionBindings : g_vrInputTermActions)
    {
        for (auto& bindingTerms : actionBindings)
        {
            for (XrAction& action : bindingTerms)
            {
                if (action != XR_NULL_HANDLE)
                {
                    xrDestroyAction(action);
                    action = XR_NULL_HANDLE;
                }
            }
        }
    }

    if (g_vrAimPoseAction != XR_NULL_HANDLE)
    {
        xrDestroyAction(g_vrAimPoseAction);
        g_vrAimPoseAction = XR_NULL_HANDLE;
    }

    if (g_vrGripPoseAction != XR_NULL_HANDLE)
    {
        xrDestroyAction(g_vrGripPoseAction);
        g_vrGripPoseAction = XR_NULL_HANDLE;
    }

    if (g_vrPalmPoseAction != XR_NULL_HANDLE)
    {
        xrDestroyAction(g_vrPalmPoseAction);
        g_vrPalmPoseAction = XR_NULL_HANDLE;
    }

    if (g_vrControllerActionSet != XR_NULL_HANDLE)
    {
        xrDestroyActionSet(g_vrControllerActionSet);
        g_vrControllerActionSet = XR_NULL_HANDLE;
    }

    g_vrControllerHandPaths = {
        XR_NULL_PATH,
        XR_NULL_PATH,
    };

    g_vrControllerActionsCreated = false;
    g_vrControllerActionsAttached = false;
    g_vrControllerSpacesCreated = false;

    g_vrLoggedFirstGripPose.fill(false);
    g_vrLoggedFirstPalmPose.fill(false);
    g_vrLoggedFirstAimPose.fill(false);
    g_vrControllerTriggerPressed.fill(false);
    g_vrControllerSqueezePressed.fill(false);
    g_vrControllerDiagnosticFrame = 0u;
    g_vrControllerRenderPoses = {};
    g_vrInputActionPreviousHeld.fill(false);
    g_vrDirectionalTermLatched = {};
    g_vrMissionShortcutArmed = true;
    g_vrOpenXrMissionSelector = {};
    g_vrOpenXrLoggedMissionSelector = false;
    g_vrNightVisionVisorGesture = {};
    g_vrLoggedNightVisionVisorGesture = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        g_vrLeftThumbstick[0] = 0.0f;
        g_vrLeftThumbstick[1] = 0.0f;
        g_vrLeftThumbstickValid = false;

        g_vrRightThumbstickX = 0.0f;
        g_vrRightThumbstickY = 0.0f;
        g_vrRightThumbstickValid = false;
        g_vrMenuNavigationAxis = {};
        g_vrMenuNavigationAxisValid = false;
        g_vrScopeZoomAxis = {};
        g_vrScopeZoomAxisValid = false;
        g_vrSnapTurnArmed = true;
        g_vrRightThumbrestTouched = false;

        g_vrPoseFocusAimHeld = false;
        g_vrConfiguredAimHeld = false;
        g_vrLeftTriggerJumpHeld = false;
        g_vrRightAButtonHeld = false;
        g_vrLeftXUseHeld = false;

        g_vrLeftStickSprintHeld = false;
        g_vrRightStickMeleeHeld = false;
        g_vrRightBStanceHeld = false;
        g_vrLowerStanceHeld = false;

        g_vrNativeOffhandHeld = false;
        g_vrLeftYNextWeaponHeld = false;

        g_vrLeftMenuHeld = false;
        g_vrLeftMenuWasHeld = false;
        g_vrMenuConfirmHeld = false;
        g_vrMenuBackHeld = false;
    }

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        g_vrRightControllerWeaponPoseValid = false;
        g_vrRightControllerWeaponFilterValid = false;
        g_vrRightControllerLinearVelocityValid = false;
        g_vrRightControllerPositionSampleValid = false;
        g_vrRightControllerPositionSampleMilliseconds = 0u;
        g_vrPhysicalMeleeArmed = true;
        g_vrPhysicalMeleePulseUntilMilliseconds = 0u;
        g_vrLastPhysicalMeleeMilliseconds = 0u;
        g_vrRightControllerWeaponCalibrationValid = false;
        g_vrMountedWeaponCameraAxisWorldValid = false;
        g_vrRightControllerFinalWeaponAimValid = false;
        g_vrRightControllerFinalWeaponAxisCameraLocalValid = false;
        g_vrPhysicalSniperScopeOffsetWeaponLocalValid = false;
        g_vrPhysicalSniperScopePoseWorldValid = false;
        g_vrRightControllerFinalWeaponMuzzleValid = false;
        g_vrRightControllerFinalWeaponMuzzleBlocked = false;
        g_vrRightControllerAttackPressed = false;

        g_vrRightControllerFinalWeaponForward[0] = 1.0f;
        g_vrRightControllerFinalWeaponForward[1] = 0.0f;
        g_vrRightControllerFinalWeaponForward[2] = 0.0f;

        g_vrRightControllerFilteredGripPosition = {
            0.0f,
            0.0f,
            0.0f,
        };

        g_vrRightControllerFilteredAimOrientation = {
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };

        memset(
            g_vrRightControllerWeaponPosition,
            0,
                sizeof(g_vrRightControllerWeaponPosition));

        std::memset(
            g_vrRightControllerLinearVelocity,
            0,
            sizeof(g_vrRightControllerLinearVelocity));

        std::memset(
            g_vrRightControllerPreviousPosition,
            0,
            sizeof(g_vrRightControllerPreviousPosition));

        memset(
            g_vrRightControllerWeaponAxis,
            0,
            sizeof(g_vrRightControllerWeaponAxis));

        std::memset(
            g_vrRightControllerGripAxis,
            0,
            sizeof(g_vrRightControllerGripAxis));

        g_vrRightControllerGripAxis[0][0] = 1.0f;
        g_vrRightControllerGripAxis[1][1] = 1.0f;
        g_vrRightControllerGripAxis[2][2] = 1.0f;

        memset(
            g_vrRightControllerFinalWeaponAxisCameraLocal,
            0,
            sizeof(
                g_vrRightControllerFinalWeaponAxisCameraLocal));

        memset(
            g_vrPhysicalSniperScopeOffsetWeaponLocal,
            0,
            sizeof(
                g_vrPhysicalSniperScopeOffsetWeaponLocal));

        memset(
            g_vrPhysicalSniperScopeOriginWorld,
            0,
            sizeof(g_vrPhysicalSniperScopeOriginWorld));

        g_vrPhysicalSniperScopeForwardWorld[0] = 1.0f;
        g_vrPhysicalSniperScopeForwardWorld[1] = 0.0f;
        g_vrPhysicalSniperScopeForwardWorld[2] = 0.0f;

        std::memset(
            g_vrPhysicalSniperScopeAxisWorld,
            0,
            sizeof(g_vrPhysicalSniperScopeAxisWorld));

        g_vrPhysicalSniperScopeAxisWorld[0][0] = 1.0f;
        g_vrPhysicalSniperScopeAxisWorld[1][1] = 1.0f;
        g_vrPhysicalSniperScopeAxisWorld[2][2] = 1.0f;

        memset(
            g_vrRightControllerFinalWeaponMuzzleWorld,
            0,
            sizeof(g_vrRightControllerFinalWeaponMuzzleWorld));

        memset(
            g_vrRightControllerWeaponCalibrationCameraLocal,
            0,
            sizeof(
                g_vrRightControllerWeaponCalibrationCameraLocal));

        g_vrWeaponAttachmentBaselines = {};
    }

    {
        std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
        g_vrActiveCalibrationWeaponIndex = 0;
        g_vrActiveCalibrationWeaponId.clear();
        g_vrActiveCalibrationWeaponName.clear();
        g_vrActiveEffectiveWeaponCalibration = {};
        std::memset(
            g_vrActiveWeaponBaseAttachmentAxis,
            0,
            sizeof(g_vrActiveWeaponBaseAttachmentAxis));
        std::memset(
            g_vrActiveWeaponControllerAxis,
            0,
            sizeof(g_vrActiveWeaponControllerAxis));
        g_vrActiveWeaponCapturePoseValid = false;
        g_vrLastWeaponStatusSignature.clear();
    }

    g_vrLoggedRightControllerWeaponCalibration = false;
    g_vrLoggedRightControllerWeaponApply = false;
    g_vrReportedRightControllerWeaponCalibration = false;
    g_vrLoggedRightControllerUsercmdAim = false;
    g_vrLoggedRightControllerAttackInjection = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        g_vrLeftControllerForegripPoseValid = false;
        g_vrLeftControllerPalmPoseValid = false;
        g_vrLeftControllerForegripPressed = false;
        g_vrLeftControllerSqueezePressedRaw = false;
        g_vrSupportGripBindingWasHeld = false;
        g_vrSupportGripToggleLatched = false;
        g_vrObjectGripBindingWasHeld = false;
        g_vrObjectGripToggleLatched = false;
        g_vrLeftControllerLinearVelocityValid = false;
        g_vrLeftControllerPositionSampleValid = false;
        g_vrLeftControllerPositionSampleMilliseconds = 0u;
        g_vrTwoHandWeaponBlend = 0.0f;
        g_vrTwoHandWeaponTargetActive = false;
        g_vrPoseFocusAimPoseHeld = false;
        g_vrPoseFocusAimEngageFrames = 0u;
        g_vrPoseFocusAimReleaseFrames = 0u;

        memset(
            g_vrLeftControllerForegripPosition,
            0,
            sizeof(
                g_vrLeftControllerForegripPosition));

        std::memset(
            g_vrLeftControllerLinearVelocity,
            0,
            sizeof(
                g_vrLeftControllerLinearVelocity));

        std::memset(
            g_vrLeftControllerPreviousPosition,
            0,
            sizeof(
                g_vrLeftControllerPreviousPosition));

        std::memset(
            g_vrLeftControllerForegripAxis,
            0,
            sizeof(
                g_vrLeftControllerForegripAxis));

        g_vrLeftControllerForegripAxis[0][0] = 1.0f;
        g_vrLeftControllerForegripAxis[1][1] = 1.0f;
        g_vrLeftControllerForegripAxis[2][2] = 1.0f;

        g_vrLeftControllerGripOrientationHeadLocalOpenXr = {
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };

        std::memset(
            g_vrLeftControllerPalmPosition,
            0,
            sizeof(
                g_vrLeftControllerPalmPosition));

        g_vrLeftControllerPalmOrientationHeadLocalOpenXr = {
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };

        g_vrManualMagazineReload =
            VrManualMagazineReloadState{};

        g_vrManualGrenade =
            VrManualGrenadeState{};
    }
}

bool VR_CreateControllerAction(
    const XrActionType actionType,
    const char* actionName,
    const char* localizedName,
    XrAction* action)
{
    if (actionName == nullptr ||
        localizedName == nullptr ||
        action == nullptr)
    {
        return false;
    }

    XrActionCreateInfo createInfo{
        XR_TYPE_ACTION_CREATE_INFO
    };

    createInfo.actionType = actionType;

    std::snprintf(
        createInfo.actionName,
        XR_MAX_ACTION_NAME_SIZE,
        "%s",
        actionName);

    std::snprintf(
        createInfo.localizedActionName,
        XR_MAX_LOCALIZED_ACTION_NAME_SIZE,
        "%s",
        localizedName);

    createInfo.countSubactionPaths =
        static_cast<std::uint32_t>(
            g_vrControllerHandPaths.size());

    createInfo.subactionPaths =
        g_vrControllerHandPaths.data();

    const XrResult result =
        xrCreateAction(
            g_vrControllerActionSet,
            &createInfo,
            action);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrCreateAction",
            result);

        return false;
    }

    return true;
}

#if 0
bool VR_SuggestControllerBindingsLegacy()
{
    XrPath touchProfile = XR_NULL_PATH;

    if (!VR_ControllerStringToPath(
            "/interaction_profiles/oculus/"
            "touch_controller",
            &touchProfile))
    {
        return false;
    }

    std::array<XrPath, 22> touchBindingPaths = {};

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    const auto touchButtonPath = [](
        const bool leftHand,
        const VrTouchButton button) -> const char*
    {
        if (leftHand)
        {
            switch (button)
            {
            case VrTouchButton::Primary:
                return "/user/hand/left/input/x/click";
            case VrTouchButton::Secondary:
                return "/user/hand/left/input/y/click";
            case VrTouchButton::Stick:
                return "/user/hand/left/input/thumbstick/click";
            }
        }
        else
        {
            switch (button)
            {
            case VrTouchButton::Primary:
                return "/user/hand/right/input/a/click";
            case VrTouchButton::Secondary:
                return "/user/hand/right/input/b/click";
            case VrTouchButton::Stick:
                return "/user/hand/right/input/thumbstick/click";
            }
        }

        return leftHand
            ? "/user/hand/left/input/x/click"
            : "/user/hand/right/input/a/click";
    };

    const std::array<const char*, 22>
        touchBindingStrings = {
            "/user/hand/left/input/grip/pose",
            "/user/hand/right/input/grip/pose",
            "/user/hand/left/input/aim/pose",
            "/user/hand/right/input/aim/pose",
            "/user/hand/left/input/trigger/value",
            "/user/hand/right/input/trigger/value",
            "/user/hand/left/input/squeeze/value",
            "/user/hand/right/input/squeeze/value",
            "/user/hand/left/output/haptic",
            "/user/hand/right/output/haptic",
            "/user/hand/left/input/thumbstick",
            "/user/hand/right/input/thumbstick",
            touchButtonPath(false, configurable.reloadButton),
            touchButtonPath(true, configurable.useButton),
            touchButtonPath(true, configurable.sprintButton),
            touchButtonPath(false, configurable.meleeButton),
            touchButtonPath(false, configurable.stanceButton),
            touchButtonPath(true, configurable.nextWeaponButton),
            "/user/hand/left/input/menu/click",
            "/user/hand/right/input/thumbrest/touch",
            "/user/hand/left/input/palm_ext/pose",
            "/user/hand/right/input/palm_ext/pose",
        };

    const std::uint32_t touchBindingCount =
        g_vrPalmPoseExtensionEnabled
            ? static_cast<std::uint32_t>(
                  touchBindingPaths.size())
            : 20u;

    for (std::uint32_t bindingIndex = 0u;
         bindingIndex < touchBindingCount;
         ++bindingIndex)
    {
        if (!VR_ControllerStringToPath(
                touchBindingStrings[bindingIndex],
                &touchBindingPaths[bindingIndex]))
        {
            return false;
        }
    }

    const std::array<XrActionSuggestedBinding, 22>
        touchBindings = {{
            {
                g_vrGripPoseAction,
                touchBindingPaths[0],
            },
            {
                g_vrGripPoseAction,
                touchBindingPaths[1],
            },
            {
                g_vrAimPoseAction,
                touchBindingPaths[2],
            },
            {
                g_vrAimPoseAction,
                touchBindingPaths[3],
            },
            {
                g_vrTriggerValueAction,
                touchBindingPaths[4],
            },
            {
                g_vrTriggerValueAction,
                touchBindingPaths[5],
            },
            {
                g_vrSqueezeValueAction,
                touchBindingPaths[6],
            },
            {
                g_vrSqueezeValueAction,
                touchBindingPaths[7],
            },
            {
                g_vrHapticOutputAction,
                touchBindingPaths[8],
            },
            {
                g_vrHapticOutputAction,
                touchBindingPaths[9],
            },
            {
                g_vrMoveThumbstickAction,
                touchBindingPaths[10],
            },
            {
                g_vrTurnThumbstickAction,
                touchBindingPaths[11],
            },
            {
                g_vrRightAButtonAction,
                touchBindingPaths[12],
            },
            {
                g_vrUseReloadButtonAction,
                touchBindingPaths[13],
            },
            {
                g_vrSprintButtonAction,
                touchBindingPaths[14],
            },
            {
                g_vrMeleeButtonAction,
                touchBindingPaths[15],
            },
            {
                g_vrStanceButtonAction,
                touchBindingPaths[16],
            },
            {
                g_vrNextWeaponButtonAction,
                touchBindingPaths[17],
            },
            {
                g_vrMenuButtonAction,
                touchBindingPaths[18],
            },
            {
                g_vrRightThumbrestTouchAction,
                touchBindingPaths[19],
            },
            {
                g_vrPalmPoseAction,
                touchBindingPaths[20],
            },
            {
                g_vrPalmPoseAction,
                touchBindingPaths[21],
            },
        }};

    XrInteractionProfileSuggestedBinding
        touchSuggestion{
            XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
        };

    touchSuggestion.interactionProfile =
        touchProfile;

    touchSuggestion.countSuggestedBindings =
        touchBindingCount;

    touchSuggestion.suggestedBindings =
        touchBindings.data();

    XrResult result =
        xrSuggestInteractionProfileBindings(
            g_vrInstance,
            &touchSuggestion);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrSuggestInteractionProfileBindings("
            "Oculus Touch)",
            result);

        return false;
    }

    XrPath simpleProfile = XR_NULL_PATH;

    if (VR_ControllerStringToPath(
            "/interaction_profiles/khr/"
            "simple_controller",
            &simpleProfile))
    {
        std::array<XrPath, 8> simpleBindingPaths = {};

        const std::array<const char*, 8>
            simpleBindingStrings = {
                "/user/hand/left/input/grip/pose",
                "/user/hand/right/input/grip/pose",
                "/user/hand/left/input/aim/pose",
                "/user/hand/right/input/aim/pose",
                "/user/hand/left/output/haptic",
                "/user/hand/right/output/haptic",
                "/user/hand/left/input/palm_ext/pose",
                "/user/hand/right/input/palm_ext/pose",
            };

        const std::uint32_t simpleBindingCount =
            g_vrPalmPoseExtensionEnabled
                ? static_cast<std::uint32_t>(
                      simpleBindingPaths.size())
                : 6u;

        bool simplePathsValid = true;

        for (std::uint32_t bindingIndex = 0u;
             bindingIndex < simpleBindingCount;
             ++bindingIndex)
        {
            simplePathsValid =
                simplePathsValid &&
                VR_ControllerStringToPath(
                    simpleBindingStrings[bindingIndex],
                    &simpleBindingPaths[bindingIndex]);
        }

        if (simplePathsValid)
        {
            const std::array<
                XrActionSuggestedBinding,
                8>
                simpleBindings = {{
                    {
                        g_vrGripPoseAction,
                        simpleBindingPaths[0],
                    },
                    {
                        g_vrGripPoseAction,
                        simpleBindingPaths[1],
                    },
                    {
                        g_vrAimPoseAction,
                        simpleBindingPaths[2],
                    },
                    {
                        g_vrAimPoseAction,
                        simpleBindingPaths[3],
                    },
                    {
                        g_vrHapticOutputAction,
                        simpleBindingPaths[4],
                    },
                    {
                        g_vrHapticOutputAction,
                        simpleBindingPaths[5],
                    },
                    {
                        g_vrPalmPoseAction,
                        simpleBindingPaths[6],
                    },
                    {
                        g_vrPalmPoseAction,
                        simpleBindingPaths[7],
                    },
                }};

            XrInteractionProfileSuggestedBinding
                simpleSuggestion{
                    XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
                };

            simpleSuggestion.interactionProfile =
                simpleProfile;

            simpleSuggestion.countSuggestedBindings =
                simpleBindingCount;

            simpleSuggestion.suggestedBindings =
                simpleBindings.data();

            result =
                xrSuggestInteractionProfileBindings(
                    g_vrInstance,
                    &simpleSuggestion);

            if (XR_FAILED(result))
            {
                Com_PrintWarning(
                    0,
                    "[VR] Simple-controller fallback "
                    "bindings were not accepted.\n");
            }
        }
    }

    Com_Printf(
        0,
        "[VR] Suggested Oculus Touch controller "
        "pose, analog, and haptic bindings%s.\n",
        g_vrPalmPoseExtensionEnabled
            ? " including XR_EXT_palm_pose"
            : "");

    return true;
}
#endif

XrAction VR_GetInputTermAction(
    const VrInput::Action action,
    const std::size_t bindingIndex,
    const std::size_t termIndex)
{
    const std::size_t index =
        static_cast<std::size_t>(action);

    return index < g_vrInputTermActions.size() &&
            bindingIndex < g_vrInputTermActions[index].size() &&
            termIndex < g_vrInputTermActions[index][bindingIndex].size()
        ? g_vrInputTermActions[index][bindingIndex][termIndex]
        : XR_NULL_HANDLE;
}

XrAction VR_FindInputTermActionForPhysicalSource(
    const VrInput::Source physicalSource)
{
    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    for (const VrInput::ActionDefinition& action :
         VrInput::ActionDefinitions())
    {
        const std::size_t actionIndex =
            static_cast<std::size_t>(action.action);

        for (std::size_t bindingIndex = 0u;
             bindingIndex < 2u;
             ++bindingIndex)
        {
            const VrInput::Binding& binding =
                configurable.bindings[actionIndex][bindingIndex];

            for (std::size_t termIndex = 0u;
                 termIndex < binding.sourceCount;
                 ++termIndex)
            {
                if (VrInput::PhysicalSource(
                        binding.sources[termIndex]) !=
                    physicalSource)
                {
                    continue;
                }

                const XrAction termAction =
                    VR_GetInputTermAction(
                        action.action,
                        bindingIndex,
                        termIndex);
                if (termAction != XR_NULL_HANDLE)
                {
                    return termAction;
                }
            }
        }
    }

    return XR_NULL_HANDLE;
}

bool VR_SuggestControllerBindings()
{
    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    std::uint32_t acceptedProfileCount = 0u;

    for (const VrInput::OpenXrProfileDefinition& profile :
         VrInput::OpenXrProfileDefinitions())
    {
        const std::size_t profileIndex =
            static_cast<std::size_t>(profile.profile);

        if (profileIndex >= g_vrEnabledOpenXrProfiles.size() ||
            !g_vrEnabledOpenXrProfiles[profileIndex])
        {
            continue;
        }

        XrPath interactionProfile = XR_NULL_PATH;
        if (!VR_ControllerStringToPath(
                profile.path,
                &interactionProfile))
        {
            continue;
        }

        std::vector<XrActionSuggestedBinding> bindings;
        bindings.reserve(48u);

        const auto appendPath = [&bindings](
            const XrAction action,
            const char* const pathString) -> bool
        {
            if (action == XR_NULL_HANDLE ||
                pathString == nullptr ||
                pathString[0] == '\0')
            {
                return true;
            }

            XrPath path = XR_NULL_PATH;
            if (!VR_ControllerStringToPath(pathString, &path))
            {
                return false;
            }

            const auto duplicate =
                std::find_if(
                    bindings.begin(),
                    bindings.end(),
                    [&](const XrActionSuggestedBinding& binding)
                    {
                        return binding.action == action &&
                            binding.binding == path;
                    });

            if (duplicate == bindings.end())
            {
                bindings.push_back({action, path});
            }

            return true;
        };

        bool pathsValid =
            appendPath(
                g_vrGripPoseAction,
                "/user/hand/left/input/grip/pose") &&
            appendPath(
                g_vrGripPoseAction,
                "/user/hand/right/input/grip/pose") &&
            appendPath(
                g_vrAimPoseAction,
                "/user/hand/left/input/aim/pose") &&
            appendPath(
                g_vrAimPoseAction,
                "/user/hand/right/input/aim/pose");

        if (VrInput::OpenXrProfileHasHaptics(profile.profile))
        {
            pathsValid =
                pathsValid &&
                appendPath(
                    g_vrHapticOutputAction,
                    "/user/hand/left/output/haptic") &&
                appendPath(
                    g_vrHapticOutputAction,
                    "/user/hand/right/output/haptic");
        }

        if (pathsValid)
        {
            const char* const leftSqueezeComponent =
                VrInput::ResolveOpenXrComponent(
                    profile.profile,
                    VrInput::Source::LeftSqueeze);

            if (leftSqueezeComponent != nullptr)
            {
                std::array<char, 128> leftSqueezePath = {};
                std::snprintf(
                    leftSqueezePath.data(),
                    leftSqueezePath.size(),
                    "/user/hand/left%s",
                    leftSqueezeComponent);

                pathsValid =
                    appendPath(
                        g_vrNightVisionGestureGripAction,
                        leftSqueezePath.data());
            }
        }

        if (pathsValid &&
            VrInput::OpenXrProfileHasPalmPose(profile.profile) &&
            g_vrPalmPoseExtensionEnabled &&
            g_vrPalmPoseAction != XR_NULL_HANDLE)
        {
            pathsValid =
                appendPath(
                    g_vrPalmPoseAction,
                    "/user/hand/left/input/palm_ext/pose") &&
                appendPath(
                    g_vrPalmPoseAction,
                    "/user/hand/right/input/palm_ext/pose");
        }

        for (const VrInput::ActionDefinition& action :
             VrInput::ActionDefinitions())
        {
            const std::size_t actionIndex =
                static_cast<std::size_t>(action.action);

            for (std::size_t bindingIndex = 0u;
                 bindingIndex < 2u;
                 ++bindingIndex)
            {
                const VrInput::Binding& binding =
                    configurable.bindings[actionIndex][bindingIndex];

                for (std::size_t termIndex = 0u;
                     termIndex < binding.sourceCount;
                     ++termIndex)
                {
                    const VrInput::Source source =
                        binding.sources[termIndex];
                    const char* const component =
                        VrInput::ResolveOpenXrComponent(
                            profile.profile,
                            source);

                    if (component == nullptr)
                    {
                        continue;
                    }

                    const VrInput::SourceDefinition& sourceDefinition =
                        VrInput::GetSourceDefinition(source);

                    const char* const hand =
                        sourceDefinition.hand == VrInput::Hand::Left
                            ? "left"
                            : sourceDefinition.hand == VrInput::Hand::Right
                                ? "right"
                                : nullptr;

                    if (hand == nullptr)
                    {
                        continue;
                    }

                    std::array<char, 128> fullPath = {};
                    std::snprintf(
                        fullPath.data(),
                        fullPath.size(),
                        "/user/hand/%s%s",
                        hand,
                        component);

                    pathsValid =
                        pathsValid &&
                        appendPath(
                            VR_GetInputTermAction(
                                action.action,
                                bindingIndex,
                                termIndex),
                            fullPath.data());
                }
            }
        }

        if (!pathsValid || bindings.empty())
        {
            Com_PrintWarning(
                0,
                "[VR][CONTROLS] Could not construct bindings for %s.\n",
                profile.label);
            continue;
        }

        XrInteractionProfileSuggestedBinding suggestion{
            XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
        };

        suggestion.interactionProfile = interactionProfile;
        suggestion.countSuggestedBindings =
            static_cast<std::uint32_t>(bindings.size());
        suggestion.suggestedBindings = bindings.data();

        const XrResult result =
            xrSuggestInteractionProfileBindings(
                g_vrInstance,
                &suggestion);

        if (XR_FAILED(result))
        {
            Com_PrintWarning(
                0,
                "[VR][CONTROLS] Runtime rejected %s bindings: %s (%d).\n",
                profile.label,
                VR_XrResultName(result),
                static_cast<int>(result));
            continue;
        }

        ++acceptedProfileCount;

        Com_Printf(
            0,
            "[VR][CONTROLS] Suggested %u bindings for %s.\n",
            static_cast<unsigned int>(bindings.size()),
            profile.label);
    }

    if (acceptedProfileCount == 0u)
    {
        Com_PrintWarning(
            0,
            "[VR][CONTROLS] No OpenXR controller profile accepted "
            "Controller Input V4 bindings.\n");
        return false;
    }

    Com_Printf(
        0,
        "[VR][CONTROLS] Controller Input V4 enabled %u OpenXR "
        "interaction profiles.\n",
        acceptedProfileCount);

    return true;
}

bool VR_CreateControllerActions()
{
    if (g_vrInstance == XR_NULL_HANDLE)
    {
        return false;
    }

    if (g_vrControllerActionsCreated)
    {
        return true;
    }

    if (!VR_ControllerStringToPath(
            "/user/hand/left",
            &g_vrControllerHandPaths[
                VR_CONTROLLER_LEFT]) ||
        !VR_ControllerStringToPath(
            "/user/hand/right",
            &g_vrControllerHandPaths[
                VR_CONTROLLER_RIGHT]))
    {
        VR_DestroyControllerInput();
        return false;
    }

    XrActionSetCreateInfo actionSetInfo{
        XR_TYPE_ACTION_SET_CREATE_INFO
    };

    std::snprintf(
        actionSetInfo.actionSetName,
        XR_MAX_ACTION_SET_NAME_SIZE,
        "%s",
        "gameplay");

    std::snprintf(
        actionSetInfo.localizedActionSetName,
        XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE,
        "%s",
        "KisakCOD Gameplay");

    actionSetInfo.priority = 0u;

    XrResult result =
        xrCreateActionSet(
            g_vrInstance,
            &actionSetInfo,
            &g_vrControllerActionSet);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrCreateActionSet",
            result);

        VR_DestroyControllerInput();
        return false;
    }

    if (!VR_CreateControllerAction(
            XR_ACTION_TYPE_POSE_INPUT,
            "grip_pose",
            "Grip Pose",
            &g_vrGripPoseAction) ||
        !VR_CreateControllerAction(
            XR_ACTION_TYPE_POSE_INPUT,
            "aim_pose",
            "Aim Pose",
            &g_vrAimPoseAction) ||
        !VR_CreateControllerAction(
            XR_ACTION_TYPE_VIBRATION_OUTPUT,
            "haptic_output",
            "Haptic Output",
            &g_vrHapticOutputAction) ||
        !VR_CreateControllerAction(
            XR_ACTION_TYPE_BOOLEAN_INPUT,
            "night_vision_gesture_grip",
            "Night Vision Visor Grip",
            &g_vrNightVisionGestureGripAction))
    {
        VR_DestroyControllerInput();
        return false;
    }

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    for (const VrInput::ActionDefinition& action :
         VrInput::ActionDefinitions())
    {
        const std::size_t actionIndex =
            static_cast<std::size_t>(action.action);

        for (std::size_t bindingIndex = 0u;
             bindingIndex < 2u;
             ++bindingIndex)
        {
            const VrInput::Binding& binding =
                configurable.bindings[actionIndex][bindingIndex];

            for (std::size_t termIndex = 0u;
                 termIndex < binding.sourceCount;
                 ++termIndex)
            {
                const VrInput::Source source =
                    binding.sources[termIndex];
                const XrActionType actionType =
                    VrInput::PhysicalSourceValueType(source) ==
                            VrInput::ValueType::Vector2
                        ? XR_ACTION_TYPE_VECTOR2F_INPUT
                        : XR_ACTION_TYPE_BOOLEAN_INPUT;

                std::array<char, XR_MAX_ACTION_NAME_SIZE> actionName = {};
                std::array<char, XR_MAX_LOCALIZED_ACTION_NAME_SIZE>
                    localizedName = {};
                std::snprintf(
                    actionName.data(),
                    actionName.size(),
                    "%s_%c%u",
                    action.openXrName,
                    bindingIndex == 0u ? 'p' : 'a',
                    static_cast<unsigned int>(termIndex + 1u));
                std::snprintf(
                    localizedName.data(),
                    localizedName.size(),
                    "%s %s input %u",
                    action.label,
                    bindingIndex == 0u ? "primary" : "alternate",
                    static_cast<unsigned int>(termIndex + 1u));

                if (!VR_CreateControllerAction(
                        actionType,
                        actionName.data(),
                        localizedName.data(),
                        &g_vrInputTermActions[actionIndex]
                            [bindingIndex][termIndex]))
                {
                    VR_DestroyControllerInput();
                    return false;
                }
            }
        }
    }

    if (g_vrPalmPoseExtensionEnabled &&
        !VR_CreateControllerAction(
            XR_ACTION_TYPE_POSE_INPUT,
            "palm_pose",
            "Palm Surface Pose",
            &g_vrPalmPoseAction))
    {
        VR_DestroyControllerInput();
        return false;
    }

    if (!VR_SuggestControllerBindings())
    {
        VR_DestroyControllerInput();
        return false;
    }

    g_vrControllerActionsCreated = true;

    Com_Printf(
        0,
        "[VR] Created OpenXR controller action set.\n");

    return true;
}

bool VR_CreateControllerActionSpaces()
{
    for (std::uint32_t handIndex = 0u;
         handIndex < kVrControllerCount;
         ++handIndex)
    {
        XrActionSpaceCreateInfo gripSpaceInfo{
            XR_TYPE_ACTION_SPACE_CREATE_INFO
        };

        gripSpaceInfo.action =
            g_vrGripPoseAction;

        gripSpaceInfo.subactionPath =
            g_vrControllerHandPaths[handIndex];

        gripSpaceInfo.poseInActionSpace.orientation.w =
            1.0f;

        XrResult result =
            xrCreateActionSpace(
                g_vrSession,
                &gripSpaceInfo,
                &g_vrControllerGripSpaces[
                    handIndex]);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrCreateActionSpace(grip)",
                result);

            return false;
        }

        if (g_vrPalmPoseAction != XR_NULL_HANDLE)
        {
            XrActionSpaceCreateInfo palmSpaceInfo{
                XR_TYPE_ACTION_SPACE_CREATE_INFO
            };

            palmSpaceInfo.action =
                g_vrPalmPoseAction;

            palmSpaceInfo.subactionPath =
                g_vrControllerHandPaths[handIndex];

            palmSpaceInfo.poseInActionSpace.orientation.w =
                1.0f;

            result =
                xrCreateActionSpace(
                    g_vrSession,
                    &palmSpaceInfo,
                    &g_vrControllerPalmSpaces[
                        handIndex]);

            if (XR_FAILED(result))
            {
                VR_LogXrFailure(
                    "xrCreateActionSpace(palm_ext)",
                    result);

                return false;
            }
        }

        XrActionSpaceCreateInfo aimSpaceInfo{
            XR_TYPE_ACTION_SPACE_CREATE_INFO
        };

        aimSpaceInfo.action =
            g_vrAimPoseAction;

        aimSpaceInfo.subactionPath =
            g_vrControllerHandPaths[handIndex];

        aimSpaceInfo.poseInActionSpace.orientation.w =
            1.0f;

        result =
            xrCreateActionSpace(
                g_vrSession,
                &aimSpaceInfo,
                &g_vrControllerAimSpaces[
                    handIndex]);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrCreateActionSpace(aim)",
                result);

            return false;
        }
    }

    g_vrControllerSpacesCreated = true;
    return true;
}

bool VR_AttachControllerActions()
{
    if (!g_vrControllerActionsCreated ||
        g_vrSession == XR_NULL_HANDLE)
    {
        return false;
    }

    const XrActionSet actionSets[] = {
        g_vrControllerActionSet,
    };

    XrSessionActionSetsAttachInfo attachInfo{
        XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO
    };

    attachInfo.countActionSets = 1u;
    attachInfo.actionSets = actionSets;

    XrResult result =
        xrAttachSessionActionSets(
            g_vrSession,
            &attachInfo);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrAttachSessionActionSets",
            result);

        return false;
    }

    g_vrControllerActionsAttached = true;

    if (!VR_CreateControllerActionSpaces())
    {
        return false;
    }

    Com_Printf(
        0,
        "[VR] OpenXR controller action set and "
        "pose spaces are ready.\n");

    return true;
}

bool VR_GetControllerPoseState(
    const XrAction action,
    const XrPath handPath,
    bool* active)
{
    if (active == nullptr)
    {
        return false;
    }

    XrActionStateGetInfo getInfo{
        XR_TYPE_ACTION_STATE_GET_INFO
    };

    getInfo.action = action;
    getInfo.subactionPath = handPath;

    XrActionStatePose state{
        XR_TYPE_ACTION_STATE_POSE
    };

    const XrResult result =
        xrGetActionStatePose(
            g_vrSession,
            &getInfo,
            &state);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetActionStatePose",
            result);

        return false;
    }

    *active = state.isActive == XR_TRUE;
    return true;
}

bool VR_GetControllerFloatState(
    const XrAction action,
    const XrPath handPath,
    float* value,
    bool* active)
{
    if (value == nullptr ||
        active == nullptr)
    {
        return false;
    }

    XrActionStateGetInfo getInfo{
        XR_TYPE_ACTION_STATE_GET_INFO
    };

    getInfo.action = action;
    getInfo.subactionPath = handPath;

    XrActionStateFloat state{
        XR_TYPE_ACTION_STATE_FLOAT
    };

    const XrResult result =
        xrGetActionStateFloat(
            g_vrSession,
            &getInfo,
            &state);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetActionStateFloat",
            result);

        return false;
    }

    *active = state.isActive == XR_TRUE;
    *value = *active ? state.currentState : 0.0f;

    return true;
}

bool VR_GetControllerVector2State(
    const XrAction action,
    const XrPath handPath,
    XrVector2f* value,
    bool* active)
{
    if (value == nullptr ||
        active == nullptr)
    {
        return false;
    }

    XrActionStateGetInfo getInfo{
        XR_TYPE_ACTION_STATE_GET_INFO
    };

    getInfo.action = action;
    getInfo.subactionPath = handPath;

    XrActionStateVector2f state{
        XR_TYPE_ACTION_STATE_VECTOR2F
    };

    const XrResult result =
        xrGetActionStateVector2f(
            g_vrSession,
            &getInfo,
            &state);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetActionStateVector2f",
            result);

        return false;
    }

    *active = state.isActive == XR_TRUE;

    if (*active)
    {
        *value = state.currentState;
    }
    else
    {
        *value = {};
    }

    return true;
}

bool VR_GetControllerBooleanState(
    const XrAction action,
    const XrPath handPath,
    bool* pressed,
    bool* active)
{
    if (pressed == nullptr ||
        active == nullptr)
    {
        return false;
    }

    XrActionStateGetInfo getInfo{
        XR_TYPE_ACTION_STATE_GET_INFO
    };

    getInfo.action = action;
    getInfo.subactionPath = handPath;

    XrActionStateBoolean state{
        XR_TYPE_ACTION_STATE_BOOLEAN
    };

    const XrResult result =
        xrGetActionStateBoolean(
            g_vrSession,
            &getInfo,
            &state);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetActionStateBoolean",
            result);

        return false;
    }

    *active = state.isActive == XR_TRUE;
    *pressed =
        *active &&
        state.currentState == XR_TRUE;

    return true;
}

bool VR_LocateControllerSpace(
    const XrSpace controllerSpace,
    const XrTime displayTime,
    XrSpaceLocation* location,
    XrSpaceVelocity* velocity = nullptr)
{
    if (controllerSpace == XR_NULL_HANDLE ||
        location == nullptr)
    {
        return false;
    }

    *location =
        XrSpaceLocation{
            XR_TYPE_SPACE_LOCATION
        };

    if (velocity != nullptr)
    {
        *velocity =
            XrSpaceVelocity{
                XR_TYPE_SPACE_VELOCITY
            };

        location->next =
            velocity;
    }

    const XrResult result =
        xrLocateSpace(
            controllerSpace,
            g_vrAppSpace,
            displayTime,
            location);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrLocateSpace(controller)",
            result);

        return false;
    }

    const XrSpaceLocationFlags requiredFlags =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;

    return
        (location->locationFlags & requiredFlags) ==
        requiredFlags;
}

void VR_LogControllerPoseSnapshot(
    const std::uint32_t handIndex,
    const XrSpaceLocation& aimLocation,
    const float triggerValue,
    const float squeezeValue)
{
    const VrHeadVector aimForward =
        VR_RotateHeadVector(
            aimLocation.pose.orientation,
            {0.0f, 0.0f, -1.0f});
    const VrMeasurementUnitSystem units =
        VR_GetConfiguratorSettings().measurementUnits;

    Com_Printf(
        0,
        "[VR] Controller %s aim snapshot: "
        "pos %.2f %.2f %.2f %s, "
        "forward %.3f %.3f %.3f, "
        "trigger %.3f, squeeze %.3f.\n",
        VR_ControllerHandName(handIndex),
        VR_DisplayMeters(aimLocation.pose.position.x, units),
        VR_DisplayMeters(aimLocation.pose.position.y, units),
        VR_DisplayMeters(aimLocation.pose.position.z, units),
        VR_DisplayLengthUnit(units),
        aimForward.x,
        aimForward.y,
        aimForward.z,
        triggerValue,
        squeezeValue);
}

bool VR_GetNightVisionGestureHeadLocalPosition(
    const XrPosef& leftGripPose,
    VrGestures::HeadLocalPosition* const position)
{
    if (position == nullptr ||
        g_vrViews.size() < kVrStereoEyeCount)
    {
        return false;
    }

    const XrQuaternionf headOrientation =
        VR_NormalizeQuaternion(
            g_vrViews[0].pose.orientation);

    const XrVector3f headCenter = {
        (g_vrViews[0].pose.position.x +
         g_vrViews[1].pose.position.x) * 0.5f,
        (g_vrViews[0].pose.position.y +
         g_vrViews[1].pose.position.y) * 0.5f,
        (g_vrViews[0].pose.position.z +
         g_vrViews[1].pose.position.z) * 0.5f,
    };

    const VrHeadVector headLocal =
        VR_RotateHeadVector(
            VR_ConjugateQuaternion(
                headOrientation),
            {
                leftGripPose.position.x - headCenter.x,
                leftGripPose.position.y - headCenter.y,
                leftGripPose.position.z - headCenter.z,
            });

    position->x = headLocal.x;
    position->y = headLocal.y;
    position->z = headLocal.z;

    return VrGestures::IsFinite(*position);
}

VrGestures::NightVisionVisorUpdate
VR_UpdateNightVisionVisorGesture(
    const bool gripPoseValid,
    const XrPosef& leftGripPose,
    const bool gripAvailable,
    const bool gripHeld,
    const char* const backendLabel)
{
    VrGestures::HeadLocalPosition position = {};
    const bool positionValid =
        gripPoseValid &&
        VR_GetNightVisionGestureHeadLocalPosition(
            leftGripPose,
            &position);

    const bool gameplayAllowed =
        clientUIActives[0].connectionState == CA_ACTIVE &&
        !Key_IsCatcherActive(0, 0x10);

    const VrGestures::NightVisionVisorUpdate update =
        VrGestures::UpdateNightVisionVisorGesture(
            &g_vrNightVisionVisorGesture,
            gameplayAllowed,
            gripAvailable,
            gripHeld,
            positionValid,
            position,
            static_cast<std::uint32_t>(
                Sys_Milliseconds()));

    if (update.available &&
        !g_vrLoggedNightVisionVisorGesture)
    {
        Com_Printf(
            0,
            "[VR][GESTURE][NIGHT_VISION] V81 foregrip-safe left-grip "
            "visor gesture is active through %s: grip at the crown and "
            "pull down, or grip at the visor and pull up; release "
            "to toggle.\n",
            backendLabel != nullptr
                ? backendLabel
                : "the active runtime");
        g_vrLoggedNightVisionVisorGesture = true;
    }

    if (VR_VerboseDiagnosticsEnabled() &&
        update.armedThisFrame)
    {
        Com_Printf(
            0,
            "[VR][GESTURE][NIGHT_VISION] Armed visor-%s gesture "
            "at head-local %.3f %.3f %.3f m.\n",
            g_vrNightVisionVisorGesture.direction ==
                    VrGestures::NightVisionVisorDirection::Lower
                ? "lowering"
                : "raising",
            position.x,
            position.y,
            position.z);
    }

    if (update.toggledThisFrame)
    {
        Com_Printf(
            0,
            "[VR][GESTURE][NIGHT_VISION] Visor pulled %s and "
            "released; queued one night-vision toggle.\n",
            update.completedDirection ==
                    VrGestures::NightVisionVisorDirection::Lower
                ? "down"
                : "up");
    }
    else if (VR_VerboseDiagnosticsEnabled() &&
             update.cancelledThisFrame)
    {
        Com_Printf(
            0,
            "[VR][GESTURE][NIGHT_VISION] Cancelled incomplete "
            "visor gesture.\n");
    }

    return update;
}


void VR_PublishLeftControllerForegripPose(
    const XrPosef& controllerGripPose,
    const bool gripValid,
    const bool interactionGripPressed,
    const bool supportGripPressed,
    const XrVector3f& controllerLinearVelocity,
    const bool linearVelocityValid)
{
    if (!gripValid ||
        g_vrViews.size() < kVrStereoEyeCount)
    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        g_vrLeftControllerForegripPoseValid = false;
        g_vrLeftControllerForegripPressed = false;
        g_vrLeftControllerSqueezePressedRaw = false;
        g_vrLeftControllerLinearVelocityValid = false;
        g_vrLeftControllerPositionSampleValid = false;
        g_vrLeftControllerPositionSampleMilliseconds = 0u;
        return;
    }

    const XrQuaternionf headOrientation =
        VR_NormalizeQuaternion(
            g_vrViews[0].pose.orientation);

    const XrQuaternionf inverseHeadOrientation =
        VR_ConjugateQuaternion(
            headOrientation);

    const XrVector3f headCenter = {
        (g_vrViews[0].pose.position.x +
         g_vrViews[1].pose.position.x) * 0.5f,
        (g_vrViews[0].pose.position.y +
         g_vrViews[1].pose.position.y) * 0.5f,
        (g_vrViews[0].pose.position.z +
         g_vrViews[1].pose.position.z) * 0.5f,
    };

    const VrHeadVector controllerOffsetOpenXr = {
        controllerGripPose.position.x -
            headCenter.x,
        controllerGripPose.position.y -
            headCenter.y,
        controllerGripPose.position.z -
            headCenter.z,
    };

    const VrHeadVector controllerOffsetHeadLocal =
        VR_RotateHeadVector(
            inverseHeadOrientation,
            controllerOffsetOpenXr);

    const VrHeadVector controllerPositionCod =
        VR_OpenXrVectorToCod(
            controllerOffsetHeadLocal);

    const VrHeadVector controllerVelocityHeadLocal =
        VR_RotateHeadVector(
            inverseHeadOrientation,
            {
                controllerLinearVelocity.x,
                controllerLinearVelocity.y,
                controllerLinearVelocity.z,
            });

    const VrHeadVector controllerVelocityCod =
        VR_OpenXrVectorToCod(
            controllerVelocityHeadLocal);

    const XrQuaternionf controllerOrientation =
        VR_NormalizeQuaternion(
            controllerGripPose.orientation);

    const XrQuaternionf controllerRelativeToHead =
        VR_NormalizeQuaternion(
            VR_MultiplyQuaternion(
                inverseHeadOrientation,
                controllerOrientation));

    const VrHeadVector forwardCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                controllerRelativeToHead,
                {0.0f, 0.0f, -1.0f}));

    const VrHeadVector leftCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                controllerRelativeToHead,
                {-1.0f, 0.0f, 0.0f}));

    const VrHeadVector upCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                controllerRelativeToHead,
                {0.0f, 1.0f, 0.0f}));

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    const float controllerPositionGameUnits[3] = {
        controllerPositionCod.x *
            kVrGameUnitsPerMeter,
        controllerPositionCod.y *
            kVrGameUnitsPerMeter,
        controllerPositionCod.z *
            kVrGameUnitsPerMeter,
    };

    g_vrLeftControllerForegripPosition[0] =
        controllerPositionGameUnits[0];

    g_vrLeftControllerForegripPosition[1] =
        controllerPositionGameUnits[1];

    g_vrLeftControllerForegripPosition[2] =
        controllerPositionGameUnits[2];

    g_vrLeftControllerLinearVelocityValid =
        linearVelocityValid &&
        std::isfinite(controllerVelocityCod.x) &&
        std::isfinite(controllerVelocityCod.y) &&
        std::isfinite(controllerVelocityCod.z);

    if (g_vrLeftControllerLinearVelocityValid)
    {
        g_vrLeftControllerLinearVelocity[0] =
            controllerVelocityCod.x *
            kVrGameUnitsPerMeter;

        g_vrLeftControllerLinearVelocity[1] =
            controllerVelocityCod.y *
            kVrGameUnitsPerMeter;

        g_vrLeftControllerLinearVelocity[2] =
            controllerVelocityCod.z *
            kVrGameUnitsPerMeter;
    }
    else
    {
        // Some OpenXR runtimes omit XR_SPACE_VELOCITY_LINEAR_VALID_BIT.
        // Fall back to a short finite difference in the same HMD-local basis
        // so physical throwing remains available on those runtimes.
        const std::uint32_t elapsedMilliseconds =
            nowMilliseconds -
            g_vrLeftControllerPositionSampleMilliseconds;

        if (g_vrLeftControllerPositionSampleValid &&
            elapsedMilliseconds >= 4u &&
            elapsedMilliseconds <= 100u)
        {
            const float samplesPerSecond =
                1000.0f /
                static_cast<float>(elapsedMilliseconds);

            for (int component = 0;
                 component < 3;
                 ++component)
            {
                g_vrLeftControllerLinearVelocity[component] =
                    (controllerPositionGameUnits[component] -
                     g_vrLeftControllerPreviousPosition[component]) *
                    samplesPerSecond;
            }

            g_vrLeftControllerLinearVelocityValid =
                std::isfinite(
                    g_vrLeftControllerLinearVelocity[0]) &&
                std::isfinite(
                    g_vrLeftControllerLinearVelocity[1]) &&
                std::isfinite(
                    g_vrLeftControllerLinearVelocity[2]);
        }

        if (!g_vrLeftControllerLinearVelocityValid)
        {
            g_vrLeftControllerLinearVelocity[0] = 0.0f;
            g_vrLeftControllerLinearVelocity[1] = 0.0f;
            g_vrLeftControllerLinearVelocity[2] = 0.0f;
        }
    }

    std::memcpy(
        g_vrLeftControllerPreviousPosition,
        controllerPositionGameUnits,
        sizeof(
            g_vrLeftControllerPreviousPosition));

    g_vrLeftControllerPositionSampleMilliseconds =
        nowMilliseconds;

    g_vrLeftControllerPositionSampleValid = true;

    g_vrLeftControllerForegripAxis[0][0] =
        forwardCod.x;
    g_vrLeftControllerForegripAxis[0][1] =
        forwardCod.y;
    g_vrLeftControllerForegripAxis[0][2] =
        forwardCod.z;

    g_vrLeftControllerForegripAxis[1][0] =
        leftCod.x;
    g_vrLeftControllerForegripAxis[1][1] =
        leftCod.y;
    g_vrLeftControllerForegripAxis[1][2] =
        leftCod.z;

    g_vrLeftControllerForegripAxis[2][0] =
        upCod.x;
    g_vrLeftControllerForegripAxis[2][1] =
        upCod.y;
    g_vrLeftControllerForegripAxis[2][2] =
        upCod.z;

    g_vrLeftControllerGripOrientationHeadLocalOpenXr =
        controllerRelativeToHead;

    g_vrLeftControllerForegripPoseValid = true;

    g_vrLeftControllerSqueezePressedRaw =
        interactionGripPressed;

    g_vrLeftControllerForegripPressed =
        supportGripPressed &&
        g_vrManualMagazineReload.stage ==
            VrManualMagazineReloadStage::Ready &&
        g_vrManualGrenade.stage ==
            VrManualGrenadeStage::Ready;
}


void VR_PublishLeftControllerPalmPose(
    const XrPosef& controllerPalmPose,
    const bool palmValid)
{
    if (!palmValid ||
        g_vrViews.size() < kVrStereoEyeCount)
    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        g_vrLeftControllerPalmPoseValid = false;
        return;
    }

    const XrQuaternionf headOrientation =
        VR_NormalizeQuaternion(
            g_vrViews[0].pose.orientation);

    const XrQuaternionf inverseHeadOrientation =
        VR_ConjugateQuaternion(
            headOrientation);

    const XrVector3f headCenter = {
        (g_vrViews[0].pose.position.x +
         g_vrViews[1].pose.position.x) * 0.5f,
        (g_vrViews[0].pose.position.y +
         g_vrViews[1].pose.position.y) * 0.5f,
        (g_vrViews[0].pose.position.z +
         g_vrViews[1].pose.position.z) * 0.5f,
    };

    const VrHeadVector palmOffsetOpenXr = {
        controllerPalmPose.position.x -
            headCenter.x,
        controllerPalmPose.position.y -
            headCenter.y,
        controllerPalmPose.position.z -
            headCenter.z,
    };

    const VrHeadVector palmOffsetHeadLocal =
        VR_RotateHeadVector(
            inverseHeadOrientation,
            palmOffsetOpenXr);

    const VrHeadVector palmPositionCod =
        VR_OpenXrVectorToCod(
            palmOffsetHeadLocal);

    const XrQuaternionf palmOrientation =
        VR_NormalizeQuaternion(
            controllerPalmPose.orientation);

    const XrQuaternionf palmRelativeToHead =
        VR_NormalizeQuaternion(
            VR_MultiplyQuaternion(
                inverseHeadOrientation,
                palmOrientation));

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    g_vrLeftControllerPalmPosition[0] =
        palmPositionCod.x *
        kVrGameUnitsPerMeter;

    g_vrLeftControllerPalmPosition[1] =
        palmPositionCod.y *
        kVrGameUnitsPerMeter;

    g_vrLeftControllerPalmPosition[2] =
        palmPositionCod.z *
        kVrGameUnitsPerMeter;

    g_vrLeftControllerPalmOrientationHeadLocalOpenXr =
        palmRelativeToHead;

    g_vrLeftControllerPalmPoseValid = true;
}


void VR_PublishRightControllerWeaponPose(
    const XrPosef& controllerGripPose,
    const XrPosef& controllerAimPose,
    const XrVector3f& controllerLinearVelocity,
    const bool linearVelocityValid)
{
    if (g_vrViews.size() < kVrStereoEyeCount)
    {
        return;
    }

    XrVector3f filteredGripPosition = {};
    XrQuaternionf filteredAimOrientation = {};

    const XrQuaternionf normalizedAimOrientation =
        VR_NormalizeQuaternion(
            controllerAimPose.orientation);

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrRightControllerWeaponFilterValid)
        {
            g_vrRightControllerFilteredGripPosition =
                controllerGripPose.position;

            g_vrRightControllerFilteredAimOrientation =
                normalizedAimOrientation;

            g_vrRightControllerWeaponFilterValid = true;
        }
        else
        {
            // Modest smoothing removes visible tracking shimmer while
            // retaining responsive one-handed aiming at typical VR rates.
            const VrConfiguratorSettings& configurable =
                VR_GetConfiguratorSettings();

            const float positionBlend =
                configurable.weaponPositionResponse;

            const float orientationBlend =
                configurable.weaponOrientationResponse;

            g_vrRightControllerFilteredGripPosition.x +=
                (controllerGripPose.position.x -
                 g_vrRightControllerFilteredGripPosition.x) *
                positionBlend;

            g_vrRightControllerFilteredGripPosition.y +=
                (controllerGripPose.position.y -
                 g_vrRightControllerFilteredGripPosition.y) *
                positionBlend;

            g_vrRightControllerFilteredGripPosition.z +=
                (controllerGripPose.position.z -
                 g_vrRightControllerFilteredGripPosition.z) *
                positionBlend;

            XrQuaternionf targetOrientation =
                normalizedAimOrientation;

            const float orientationDot =
                g_vrRightControllerFilteredAimOrientation.x *
                    targetOrientation.x +
                g_vrRightControllerFilteredAimOrientation.y *
                    targetOrientation.y +
                g_vrRightControllerFilteredAimOrientation.z *
                    targetOrientation.z +
                g_vrRightControllerFilteredAimOrientation.w *
                    targetOrientation.w;

            if (orientationDot < 0.0f)
            {
                targetOrientation.x =
                    -targetOrientation.x;
                targetOrientation.y =
                    -targetOrientation.y;
                targetOrientation.z =
                    -targetOrientation.z;
                targetOrientation.w =
                    -targetOrientation.w;
            }

            XrQuaternionf blendedOrientation = {
                g_vrRightControllerFilteredAimOrientation.x +
                    (targetOrientation.x -
                     g_vrRightControllerFilteredAimOrientation.x) *
                    orientationBlend,
                g_vrRightControllerFilteredAimOrientation.y +
                    (targetOrientation.y -
                     g_vrRightControllerFilteredAimOrientation.y) *
                    orientationBlend,
                g_vrRightControllerFilteredAimOrientation.z +
                    (targetOrientation.z -
                     g_vrRightControllerFilteredAimOrientation.z) *
                    orientationBlend,
                g_vrRightControllerFilteredAimOrientation.w +
                    (targetOrientation.w -
                     g_vrRightControllerFilteredAimOrientation.w) *
                    orientationBlend,
            };

            g_vrRightControllerFilteredAimOrientation =
                VR_NormalizeQuaternion(
                    blendedOrientation);
        }

        filteredGripPosition =
            g_vrRightControllerFilteredGripPosition;

        filteredAimOrientation =
            g_vrRightControllerFilteredAimOrientation;
    }

    static bool loggedFilteredGripWeaponPose = false;

    if (!loggedFilteredGripWeaponPose)
    {
        Com_Printf(
            0,
            "[VR] Using filtered weapon-hand grip position and aim orientation "
            "for the weapon pose.\n");

        loggedFilteredGripWeaponPose = true;
    }

    const XrQuaternionf headOrientation =
        VR_NormalizeQuaternion(
            g_vrViews[0].pose.orientation);

    const XrQuaternionf inverseHeadOrientation =
        VR_ConjugateQuaternion(
            headOrientation);

    const XrVector3f headCenter = {
        (g_vrViews[0].pose.position.x +
         g_vrViews[1].pose.position.x) * 0.5f,
        (g_vrViews[0].pose.position.y +
         g_vrViews[1].pose.position.y) * 0.5f,
        (g_vrViews[0].pose.position.z +
         g_vrViews[1].pose.position.z) * 0.5f,
    };

    const VrHeadVector controllerOffsetOpenXr = {
        filteredGripPosition.x -
            headCenter.x,
        filteredGripPosition.y -
            headCenter.y,
        filteredGripPosition.z -
            headCenter.z,
    };

    const VrHeadVector controllerOffsetHeadLocal =
        VR_RotateHeadVector(
            inverseHeadOrientation,
            controllerOffsetOpenXr);

    const VrHeadVector controllerPositionCod =
        VR_OpenXrVectorToCod(
            controllerOffsetHeadLocal);

    const VrHeadVector controllerVelocityHeadLocal =
        VR_RotateHeadVector(
            inverseHeadOrientation,
            {
                controllerLinearVelocity.x,
                controllerLinearVelocity.y,
                controllerLinearVelocity.z,
            });

    const VrHeadVector controllerVelocityCod =
        VR_OpenXrVectorToCod(
            controllerVelocityHeadLocal);

    const XrQuaternionf controllerOrientation =
        filteredAimOrientation;

    const XrQuaternionf gripOrientation =
        VR_NormalizeQuaternion(
            controllerGripPose.orientation);

    const XrQuaternionf controllerRelativeToHead =
        VR_NormalizeQuaternion(
            VR_MultiplyQuaternion(
                inverseHeadOrientation,
                controllerOrientation));

    const XrQuaternionf gripRelativeToHead =
        VR_NormalizeQuaternion(
            VR_MultiplyQuaternion(
                inverseHeadOrientation,
                gripOrientation));

    const VrHeadVector forwardCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                controllerRelativeToHead,
                {0.0f, 0.0f, -1.0f}));

    const VrHeadVector leftCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                controllerRelativeToHead,
                {-1.0f, 0.0f, 0.0f}));

    const VrHeadVector upCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                controllerRelativeToHead,
                {0.0f, 1.0f, 0.0f}));

    const VrHeadVector gripForwardCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                gripRelativeToHead,
                {0.0f, 0.0f, -1.0f}));

    const VrHeadVector gripLeftCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                gripRelativeToHead,
                {-1.0f, 0.0f, 0.0f}));

    const VrHeadVector gripUpCod =
        VR_OpenXrVectorToCod(
            VR_RotateHeadVector(
                gripRelativeToHead,
                {0.0f, 1.0f, 0.0f}));

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    g_vrRightControllerWeaponPosition[0] =
        controllerPositionCod.x *
        kVrGameUnitsPerMeter;

    g_vrRightControllerWeaponPosition[1] =
        controllerPositionCod.y *
        kVrGameUnitsPerMeter;

    g_vrRightControllerWeaponPosition[2] =
        controllerPositionCod.z *
        kVrGameUnitsPerMeter;

    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(Sys_Milliseconds());
    const float controllerPositionGameUnits[3] = {
        g_vrRightControllerWeaponPosition[0],
        g_vrRightControllerWeaponPosition[1],
        g_vrRightControllerWeaponPosition[2],
    };

    g_vrRightControllerLinearVelocityValid =
        linearVelocityValid &&
        std::isfinite(controllerVelocityCod.x) &&
        std::isfinite(controllerVelocityCod.y) &&
        std::isfinite(controllerVelocityCod.z);
    if (g_vrRightControllerLinearVelocityValid)
    {
        g_vrRightControllerLinearVelocity[0] =
            controllerVelocityCod.x * kVrGameUnitsPerMeter;
        g_vrRightControllerLinearVelocity[1] =
            controllerVelocityCod.y * kVrGameUnitsPerMeter;
        g_vrRightControllerLinearVelocity[2] =
            controllerVelocityCod.z * kVrGameUnitsPerMeter;
    }
    else
    {
        const std::uint32_t elapsedMilliseconds =
            nowMilliseconds -
            g_vrRightControllerPositionSampleMilliseconds;
        if (g_vrRightControllerPositionSampleValid &&
            elapsedMilliseconds >= 4u &&
            elapsedMilliseconds <= 100u)
        {
            const float samplesPerSecond =
                1000.0f / static_cast<float>(elapsedMilliseconds);
            for (int component = 0; component < 3; ++component)
            {
                g_vrRightControllerLinearVelocity[component] =
                    (controllerPositionGameUnits[component] -
                     g_vrRightControllerPreviousPosition[component]) *
                    samplesPerSecond;
            }
            g_vrRightControllerLinearVelocityValid =
                std::isfinite(g_vrRightControllerLinearVelocity[0]) &&
                std::isfinite(g_vrRightControllerLinearVelocity[1]) &&
                std::isfinite(g_vrRightControllerLinearVelocity[2]);
        }
        if (!g_vrRightControllerLinearVelocityValid)
        {
            g_vrRightControllerLinearVelocity[0] = 0.0f;
            g_vrRightControllerLinearVelocity[1] = 0.0f;
            g_vrRightControllerLinearVelocity[2] = 0.0f;
        }
    }
    std::memcpy(
        g_vrRightControllerPreviousPosition,
        controllerPositionGameUnits,
        sizeof(g_vrRightControllerPreviousPosition));
    g_vrRightControllerPositionSampleMilliseconds = nowMilliseconds;
    g_vrRightControllerPositionSampleValid = true;

    g_vrRightControllerWeaponAxis[0][0] =
        forwardCod.x;
    g_vrRightControllerWeaponAxis[0][1] =
        forwardCod.y;
    g_vrRightControllerWeaponAxis[0][2] =
        forwardCod.z;

    g_vrRightControllerWeaponAxis[1][0] =
        leftCod.x;
    g_vrRightControllerWeaponAxis[1][1] =
        leftCod.y;
    g_vrRightControllerWeaponAxis[1][2] =
        leftCod.z;

    g_vrRightControllerWeaponAxis[2][0] =
        upCod.x;
    g_vrRightControllerWeaponAxis[2][1] =
        upCod.y;
    g_vrRightControllerWeaponAxis[2][2] =
        upCod.z;

    g_vrRightControllerGripAxis[0][0] =
        gripForwardCod.x;
    g_vrRightControllerGripAxis[0][1] =
        gripForwardCod.y;
    g_vrRightControllerGripAxis[0][2] =
        gripForwardCod.z;

    g_vrRightControllerGripAxis[1][0] =
        gripLeftCod.x;
    g_vrRightControllerGripAxis[1][1] =
        gripLeftCod.y;
    g_vrRightControllerGripAxis[1][2] =
        gripLeftCod.z;

    g_vrRightControllerGripAxis[2][0] =
        gripUpCod.x;
    g_vrRightControllerGripAxis[2][1] =
        gripUpCod.y;
    g_vrRightControllerGripAxis[2][2] =
        gripUpCod.z;

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();
    if (configurable.meleeMode != VrInteractions::MeleeMode::Button &&
        g_vrRightControllerLinearVelocityValid)
    {
        const float speed = std::sqrt(
            g_vrRightControllerLinearVelocity[0] *
                g_vrRightControllerLinearVelocity[0] +
            g_vrRightControllerLinearVelocity[1] *
                g_vrRightControllerLinearVelocity[1] +
            g_vrRightControllerLinearVelocity[2] *
                g_vrRightControllerLinearVelocity[2]);
        if (speed <= configurable.meleeSpeed * 0.40f)
        {
            g_vrPhysicalMeleeArmed = true;
        }

        const bool interactionHandsFree =
            g_vrManualMagazineReload.stage ==
                VrManualMagazineReloadStage::Ready &&
            g_vrManualGrenade.stage ==
                VrManualGrenadeStage::Ready;
        const bool cooldownComplete =
            g_vrLastPhysicalMeleeMilliseconds == 0u ||
            nowMilliseconds - g_vrLastPhysicalMeleeMilliseconds >=
                configurable.meleeCooldownMilliseconds;
        float forwardFraction = 0.0f;
        if (interactionHandsFree && cooldownComplete &&
            g_vrPhysicalMeleeArmed &&
            VrInteractions::MeleeGestureQualifies(
                g_vrRightControllerLinearVelocity,
                g_vrRightControllerWeaponAxis[0],
                configurable.meleeSpeed,
                configurable.meleeForwardBias,
                nullptr,
                &forwardFraction))
        {
            g_vrPhysicalMeleePulseUntilMilliseconds =
                nowMilliseconds + 120u;
            g_vrLastPhysicalMeleeMilliseconds = nowMilliseconds;
            g_vrPhysicalMeleeArmed = false;

            static bool loggedPhysicalMelee = false;
            if (!loggedPhysicalMelee || VR_VerboseDiagnosticsEnabled())
            {
                Com_Printf(
                    0,
                    "[VR][INTERACTIONS] Physical weapon-hand melee "
                    "recognized at %.1f %s/s with %.2f forward bias.\n",
                    VR_DisplayInches(
                        speed,
                        configurable.measurementUnits),
                    VR_DisplayLengthUnit(configurable.measurementUnits),
                    forwardFraction);
                loggedPhysicalMelee = true;
            }
        }
    }

    g_vrRightControllerWeaponPoseValid = true;
}

void VR_InvalidateRightControllerWeaponPose()
{
    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        g_vrRightControllerWeaponPoseValid = false;
        g_vrRightControllerWeaponFilterValid = false;
        g_vrRightControllerWeaponCalibrationValid = false;
        g_vrMountedWeaponCameraAxisWorldValid = false;
        g_vrRightControllerFinalWeaponAimValid = false;
        g_vrRightControllerFinalWeaponAxisCameraLocalValid = false;
        g_vrPhysicalSniperScopeOffsetWeaponLocalValid = false;
        g_vrPhysicalSniperScopePoseWorldValid = false;
        g_vrRightControllerFinalWeaponMuzzleValid = false;
        g_vrRightControllerFinalWeaponMuzzleBlocked = false;
    }
    {
        std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
        g_vrActiveWeaponCapturePoseValid = false;
    }
}

void VR_UpdatePoseFocusAimFromControllers()
{
    bool previousHeld = false;
    bool currentHeld = false;
    bool poseAvailable = false;

    float headAlignment = -1.0f;
    float eyeLineDistance = 999.0f;
    float rightHandForward = 0.0f;
    float rightHandHeight = 0.0f;

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        previousHeld =
            g_vrPoseFocusAimPoseHeld;

        poseAvailable =
            g_vrTwoHandWeaponTargetActive &&
            g_vrLeftControllerForegripPoseValid &&
            g_vrLeftControllerForegripPressed &&
            g_vrRightControllerWeaponPoseValid;

        bool engagePose = false;
        bool retainPose = false;

        if (poseAvailable)
        {
            float weaponForward[3] = {
                g_vrLeftControllerForegripPosition[0] -
                    g_vrRightControllerWeaponPosition[0],
                g_vrLeftControllerForegripPosition[1] -
                    g_vrRightControllerWeaponPosition[1],
                g_vrLeftControllerForegripPosition[2] -
                    g_vrRightControllerWeaponPosition[2],
            };

            const float weaponForwardLength =
                std::sqrt(
                    weaponForward[0] * weaponForward[0] +
                    weaponForward[1] * weaponForward[1] +
                    weaponForward[2] * weaponForward[2]);

            if (weaponForwardLength <= 0.0001f)
            {
                poseAvailable = false;
            }
            else
            {
                weaponForward[0] /= weaponForwardLength;
                weaponForward[1] /= weaponForwardLength;
                weaponForward[2] /= weaponForwardLength;

                // Controller positions are HMD-local CoD coordinates:
                // +X is gaze-forward, +Y is left, and +Z is up.  The
                // normalized hand-to-hand vector is the same forward axis
                // used by the existing two-hand weapon stabilization.
                headAlignment =
                    weaponForward[0];

                rightHandForward =
                    g_vrRightControllerWeaponPosition[0];

                rightHandHeight =
                    g_vrRightControllerWeaponPosition[2];

                const float positionAlongWeapon =
                    g_vrRightControllerWeaponPosition[0] *
                        weaponForward[0] +
                    g_vrRightControllerWeaponPosition[1] *
                        weaponForward[1] +
                    g_vrRightControllerWeaponPosition[2] *
                        weaponForward[2];

                const float perpendicularToEye[3] = {
                    g_vrRightControllerWeaponPosition[0] -
                        positionAlongWeapon *
                            weaponForward[0],
                    g_vrRightControllerWeaponPosition[1] -
                        positionAlongWeapon *
                            weaponForward[1],
                    g_vrRightControllerWeaponPosition[2] -
                        positionAlongWeapon *
                            weaponForward[2],
                };

                // This is the distance from the HMD center to the infinite
                // line through both hands.  It rejects hip/chest poses while
                // allowing the normal vertical offset between the hands and
                // a rifle's optic.
                eyeLineDistance =
                    std::sqrt(
                        perpendicularToEye[0] *
                            perpendicularToEye[0] +
                        perpendicularToEye[1] *
                            perpendicularToEye[1] +
                        perpendicularToEye[2] *
                            perpendicularToEye[2]);

                // Game units are inches.  Engage inside the tighter window;
                // once active, the wider window prevents boundary flicker.
                engagePose =
                    headAlignment >= 0.80f &&
                    eyeLineDistance <= 15.0f &&
                    rightHandForward >= 0.0f &&
                    rightHandForward <= 32.0f &&
                    rightHandHeight >= -17.0f &&
                    rightHandHeight <= 10.0f;

                retainPose =
                    headAlignment >= 0.68f &&
                    eyeLineDistance <= 20.0f &&
                    rightHandForward >= -4.0f &&
                    rightHandForward <= 38.0f &&
                    rightHandHeight >= -22.0f &&
                    rightHandHeight <= 14.0f;
            }
        }

        if (!poseAvailable)
        {
            // Releasing the left middle-finger grip should lower ADS
            // immediately; only pose-boundary exits are debounced.
            g_vrPoseFocusAimPoseHeld = false;
            g_vrPoseFocusAimEngageFrames = 0u;
            g_vrPoseFocusAimReleaseFrames = 0u;
        }
        else if (!g_vrPoseFocusAimPoseHeld)
        {
            g_vrPoseFocusAimReleaseFrames = 0u;

            if (engagePose)
            {
                if (g_vrPoseFocusAimEngageFrames < 3u)
                {
                    ++g_vrPoseFocusAimEngageFrames;
                }

                if (g_vrPoseFocusAimEngageFrames >= 3u)
                {
                    g_vrPoseFocusAimPoseHeld = true;
                    g_vrPoseFocusAimEngageFrames = 0u;
                }
            }
            else
            {
                g_vrPoseFocusAimEngageFrames = 0u;
            }
        }
        else
        {
            g_vrPoseFocusAimEngageFrames = 0u;

            if (retainPose)
            {
                g_vrPoseFocusAimReleaseFrames = 0u;
            }
            else
            {
                if (g_vrPoseFocusAimReleaseFrames < 5u)
                {
                    ++g_vrPoseFocusAimReleaseFrames;
                }

                if (g_vrPoseFocusAimReleaseFrames >= 5u)
                {
                    g_vrPoseFocusAimPoseHeld = false;
                    g_vrPoseFocusAimReleaseFrames = 0u;
                }
            }
        }

        currentHeld =
            g_vrPoseFocusAimPoseHeld;
    }

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        g_vrPoseFocusAimHeld =
            currentHeld || g_vrConfiguredAimHeld;
    }

    static bool previousPoseAvailable = false;

    const bool poseJustBecameAvailable =
        poseAvailable &&
        !previousPoseAvailable;

    previousPoseAvailable =
        poseAvailable;

    static bool loggedConfiguration = false;
    const VrMeasurementUnitSystem units =
        VR_GetConfiguratorSettings().measurementUnits;

    if (VR_VerboseDiagnosticsEnabled() &&
        !loggedConfiguration)
    {
        Com_Printf(
            0,
            "[VR][FOCUS] Pose ADS enabled: hold the off-hand squeeze "
            "and shoulder the two-handed weapon near the HMD sight "
            "line. The configured Jump and Reload actions remain "
            "available on their selected physical controllers.\n");

        loggedConfiguration = true;
    }

    if (VR_VerboseDiagnosticsEnabled() &&
        poseJustBecameAvailable &&
        !currentHeld)
    {
        Com_Printf(
            0,
            "[VR][FOCUS] Evaluated a new two-hand grip: "
            "alignment %.3f, eye-line %.2f, weapon grip forward "
            "%.2f, height %.2f %s.\n",
            headAlignment,
            VR_DisplayInches(eyeLineDistance, units),
            VR_DisplayInches(rightHandForward, units),
            VR_DisplayInches(rightHandHeight, units),
            VR_DisplayLengthUnit(units));
    }

    if (VR_VerboseDiagnosticsEnabled() &&
        currentHeld &&
        !previousHeld)
    {
        Com_Printf(
            0,
            "[VR][FOCUS] Engaged pose ADS: alignment %.3f, "
            "eye-line %.2f, weapon grip forward %.2f, height %.2f %s.\n",
            headAlignment,
            VR_DisplayInches(eyeLineDistance, units),
            VR_DisplayInches(rightHandForward, units),
            VR_DisplayInches(rightHandHeight, units),
            VR_DisplayLengthUnit(units));
    }
    else if (VR_VerboseDiagnosticsEnabled() &&
             !currentHeld &&
             previousHeld)
    {
        if (!poseAvailable)
        {
            Com_Printf(
                0,
                "[VR][FOCUS] Released pose ADS because the off-hand "
                "foregrip squeeze or tracked two-hand pose ended.\n");
        }
        else
        {
            Com_Printf(
                0,
                "[VR][FOCUS] Released pose ADS after leaving the "
                "eye-level window: alignment %.3f, eye-line %.2f, "
                "weapon grip forward %.2f, height %.2f %s.\n",
                headAlignment,
                VR_DisplayInches(eyeLineDistance, units),
                VR_DisplayInches(rightHandForward, units),
                VR_DisplayInches(rightHandHeight, units),
                VR_DisplayLengthUnit(units));
        }
    }
}

using VrInputHeldState =
    std::array<bool, VrInput::kActionCount>;
using VrInputVectorState =
    std::array<XrVector2f, VrInput::kActionCount>;
using VrInputActiveState =
    std::array<bool, VrInput::kActionCount>;

bool VR_ProjectRightControllerToHud(
    VrHud::Point* const pointer)
{
    const std::uint32_t weaponHandIndex =
        VrInteractions::WeaponControllerIndex(
            VR_GetConfiguratorSettings().dominantHand);
    if (pointer == nullptr ||
        g_vrViews.size() < kVrStereoEyeCount ||
        !g_vrControllerRenderPoses[weaponHandIndex].aimValid)
    {
        return false;
    }

    const XrPosef& controller =
        g_vrControllerRenderPoses[weaponHandIndex].aimPose;
    const XrQuaternionf headOrientation =
        VR_NormalizeQuaternion(
            g_vrViews[0].pose.orientation);
    const XrQuaternionf inverseHeadOrientation =
        VR_ConjugateQuaternion(headOrientation);
    const XrVector3f headCenter = {
        0.5f *
            (g_vrViews[0].pose.position.x +
             g_vrViews[1].pose.position.x),
        0.5f *
            (g_vrViews[0].pose.position.y +
             g_vrViews[1].pose.position.y),
        0.5f *
            (g_vrViews[0].pose.position.z +
             g_vrViews[1].pose.position.z),
    };

    const VrHeadVector origin =
        VR_RotateHeadVector(
            inverseHeadOrientation,
            {
                controller.position.x - headCenter.x,
                controller.position.y - headCenter.y,
                controller.position.z - headCenter.z,
            });
    const VrHeadVector directionWorld =
        VR_RotateHeadVector(
            VR_NormalizeQuaternion(controller.orientation),
            {0.0f, 0.0f, -1.0f});
    const VrHeadVector direction =
        VR_RotateHeadVector(
            inverseHeadOrientation,
            directionWorld);

    constexpr float planeDistanceMeters = 1.0f;
    if (direction.z >= -0.01f)
    {
        return false;
    }
    const float distance =
        (-planeDistanceMeters - origin.z) / direction.z;
    if (!std::isfinite(distance) || distance <= 0.0f)
    {
        return false;
    }

    const float intersectionX =
        origin.x + direction.x * distance;
    const float intersectionY =
        origin.y + direction.y * distance;

    float tanHalfFovX = 1.0f;
    float tanHalfFovY = 1.0f;
    {
        std::lock_guard<std::mutex> lock(
            g_vrProjectionMutex);
        if (g_vrEyeProjectionValid)
        {
            tanHalfFovX = (std::max)(
                -g_vrEyeProjectionTangents[0].left,
                g_vrEyeProjectionTangents[0].right);
            tanHalfFovY = (std::max)(
                -g_vrEyeProjectionTangents[0].down,
                g_vrEyeProjectionTangents[0].up);
        }
    }
    if (tanHalfFovX <= 0.01f || tanHalfFovY <= 0.01f)
    {
        return false;
    }

    const float x =
        (0.5f +
         intersectionX /
             (2.0f * planeDistanceMeters * tanHalfFovX)) *
        VrHud::kCanvasWidth;
    const float y =
        (0.5f -
         intersectionY /
             (2.0f * planeDistanceMeters * tanHalfFovY)) *
        VrHud::kCanvasHeight;
    if (!std::isfinite(x) || !std::isfinite(y) ||
        x < -32.0f || x > VrHud::kCanvasWidth + 32.0f ||
        y < -32.0f || y > VrHud::kCanvasHeight + 32.0f)
    {
        return false;
    }

    pointer->x = (std::clamp)(x, 0.0f, VrHud::kCanvasWidth);
    pointer->y = (std::clamp)(y, 0.0f, VrHud::kCanvasHeight);
    return true;
}

bool VR_UpdateHudEditorInput(
    const VrInputHeldState& inputHeld,
    const VrInputVectorState& inputVectors,
    const VrInputActiveState& inputVectorActive)
{
    {
        std::lock_guard<std::mutex> lock(g_vrHudEditorMutex);
        if (!g_vrHudEditorActive)
        {
            return false;
        }
    }

    const auto isHeld = [&inputHeld](const VrInput::Action action)
    {
        return inputHeld[static_cast<std::size_t>(action)];
    };

    VrHud::Point pointer = {};
    const bool pointerValid =
        VR_ProjectRightControllerToHud(&pointer);
    const bool triggerHeld =
        isHeld(VrInput::Action::Attack);
    const bool confirmHeld =
        isHeld(VrInput::Action::MenuConfirm);
    const bool backHeld =
        isHeld(VrInput::Action::MenuBack);
    const bool snapEnabled =
        !isHeld(VrInput::Action::SupportGrip);
    const bool previousHeld =
        isHeld(VrInput::Action::Use);
    const bool nextHeld =
        isHeld(VrInput::Action::NextWeapon);
    const bool centerHeld =
        isHeld(VrInput::Action::Sprint);
    const bool resetHeld =
        isHeld(VrInput::Action::Melee);

    const bool keyboardTabHeld =
        (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
    const bool keyboardShiftHeld =
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool keyboardCenterHeld =
        (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
    const bool keyboardResetHeld =
        (GetAsyncKeyState(VK_END) & 0x8000) != 0;

    const std::size_t turnIndex =
        static_cast<std::size_t>(VrInput::Action::Turn);
    const float scaleAxis =
        inputVectorActive[turnIndex]
            ? inputVectors[turnIndex].y
            : 0.0f;

    bool finish = false;
    bool save = false;
    bool layoutChanged = false;
    const char* recoveryCommand = nullptr;
    VrHud::Element recoveryElement = VrHud::Element::AmmoEquipment;
    VrHud::Point recoveryCenter = {};
    float recoveryScale = 1.0f;
    {
        std::lock_guard<std::mutex> lock(g_vrHudEditorMutex);
        if (!g_vrHudEditorActive)
        {
            return true;
        }

        g_vrHudEditorPointer = pointer;
        g_vrHudEditorPointerValid = pointerValid;
        g_vrHudEditorSnapEnabled = snapEnabled;

        const bool triggerPressed =
            triggerHeld && !g_vrHudEditorTriggerWasHeld;
        const bool confirmPressed =
            confirmHeld && !g_vrHudEditorConfirmWasHeld;
        const bool backPressed =
            backHeld && !g_vrHudEditorBackWasHeld;
        const bool keyboardTabPressed =
            keyboardTabHeld && !g_vrHudEditorKeyboardTabWasHeld;
        const bool previousPressed =
            (previousHeld && !g_vrHudEditorPreviousWasHeld) ||
            (keyboardTabPressed && keyboardShiftHeld);
        const bool nextPressed =
            (nextHeld && !g_vrHudEditorNextWasHeld) ||
            (keyboardTabPressed && !keyboardShiftHeld);
        const bool centerPressed =
            (centerHeld && !g_vrHudEditorCenterWasHeld) ||
            (keyboardCenterHeld &&
             !g_vrHudEditorKeyboardCenterWasHeld);
        const bool resetPressed =
            (resetHeld && !g_vrHudEditorResetWasHeld) ||
            (keyboardResetHeld &&
             !g_vrHudEditorKeyboardResetWasHeld);

        if (backPressed)
        {
            finish = true;
            save = false;
        }
        else if (confirmPressed)
        {
            finish = true;
            save = true;
        }
        else if (previousPressed || nextPressed)
        {
            g_vrHudEditorSelected = VrHud::CycleElement(
                g_vrHudEditorSelected,
                previousPressed ? -1 : 1);
            g_vrHudEditorDragging = false;
            recoveryCommand = previousPressed
                ? "Selected previous"
                : "Selected next";
            recoveryElement = g_vrHudEditorSelected;
            recoveryCenter = VrHud::ElementCenter(
                g_vrHudEditorLayout,
                recoveryElement);
            recoveryScale = VrHud::ElementScale(
                g_vrHudEditorLayout,
                recoveryElement);
        }
        else if (centerPressed)
        {
            VrHud::CenterElement(
                &g_vrHudEditorLayout,
                g_vrHudEditorSelected);
            g_vrHudEditorDragging = false;
            layoutChanged = true;
            recoveryCommand = "Centered";
            recoveryElement = g_vrHudEditorSelected;
            recoveryCenter = VrHud::ElementCenter(
                g_vrHudEditorLayout,
                recoveryElement);
            recoveryScale = VrHud::ElementScale(
                g_vrHudEditorLayout,
                recoveryElement);
        }
        else if (resetPressed)
        {
            VrHud::ResetElement(
                &g_vrHudEditorLayout,
                g_vrHudEditorSelected);
            g_vrHudEditorDragging = false;
            layoutChanged = true;
            recoveryCommand = "Reset";
            recoveryElement = g_vrHudEditorSelected;
            recoveryCenter = VrHud::ElementCenter(
                g_vrHudEditorLayout,
                recoveryElement);
            recoveryScale = VrHud::ElementScale(
                g_vrHudEditorLayout,
                recoveryElement);
        }
        else if (triggerPressed && pointerValid)
        {
            const bool overSave =
                pointer.x >= 500.0f && pointer.x <= 624.0f &&
                pointer.y >= 18.0f && pointer.y <= 58.0f;
            const bool overCancel =
                pointer.x >= 366.0f && pointer.x <= 490.0f &&
                pointer.y >= 18.0f && pointer.y <= 58.0f;
            if (overSave || overCancel)
            {
                finish = true;
                save = overSave;
            }
            else
            {
                VrHud::Element hit = g_vrHudEditorSelected;
                if (VrHud::HitTestElement(
                        g_vrHudEditorLayout,
                        pointer,
                        &hit))
                {
                    g_vrHudEditorSelected = hit;
                    g_vrHudEditorDragging = true;
                }
            }
        }

        if (g_vrHudEditorDragging && triggerHeld && pointerValid)
        {
            const VrHud::Point before =
                VrHud::ElementCenter(
                    g_vrHudEditorLayout,
                    g_vrHudEditorSelected);
            VrHud::MoveElement(
                &g_vrHudEditorLayout,
                g_vrHudEditorSelected,
                pointer,
                snapEnabled);
            const VrHud::Point after =
                VrHud::ElementCenter(
                    g_vrHudEditorLayout,
                    g_vrHudEditorSelected);
            layoutChanged =
                std::abs(before.x - after.x) > 0.01f ||
                std::abs(before.y - after.y) > 0.01f;
        }
        if (!triggerHeld)
        {
            g_vrHudEditorDragging = false;
        }

        if (std::abs(scaleAxis) < 0.30f)
        {
            g_vrHudEditorScaleArmed = true;
        }
        else if (g_vrHudEditorScaleArmed &&
                 std::abs(scaleAxis) >= 0.65f)
        {
            const float previousScale =
                VrHud::ElementScale(
                    g_vrHudEditorLayout,
                    g_vrHudEditorSelected);
            const float step = scaleAxis > 0.0f ? 0.05f : -0.05f;
            VrHud::SetElementScale(
                &g_vrHudEditorLayout,
                g_vrHudEditorSelected,
                previousScale + step);
            layoutChanged = true;
            g_vrHudEditorScaleArmed = false;
        }

        if (layoutChanged)
        {
            ++g_vrHudLayoutRevision;
        }
        g_vrHudEditorTriggerWasHeld = triggerHeld;
        g_vrHudEditorConfirmWasHeld = confirmHeld;
        g_vrHudEditorBackWasHeld = backHeld;
        g_vrHudEditorPreviousWasHeld = previousHeld;
        g_vrHudEditorNextWasHeld = nextHeld;
        g_vrHudEditorCenterWasHeld = centerHeld;
        g_vrHudEditorResetWasHeld = resetHeld;
        g_vrHudEditorKeyboardTabWasHeld = keyboardTabHeld;
        g_vrHudEditorKeyboardCenterWasHeld = keyboardCenterHeld;
        g_vrHudEditorKeyboardResetWasHeld = keyboardResetHeld;
    }

    if (recoveryCommand != nullptr)
    {
        Com_Printf(
            0,
            "[VR][HUD][EDITOR] %s group %u/%u: %s at %.0f %.0f, "
            "scale %.2f.\n",
            recoveryCommand,
            static_cast<unsigned int>(recoveryElement) + 1u,
            static_cast<unsigned int>(VrHud::kElementCount),
            VrHud::ElementId(recoveryElement),
            recoveryCenter.x,
            recoveryCenter.y,
            recoveryScale);
    }

    if (finish)
    {
        VR_FinishHudEditor(save);
    }
    return true;
}

void VR_ApplyControllerInputState(
    const VrInputHeldState& inputHeld,
    const VrInputVectorState& inputVectors,
    const VrInputActiveState& inputVectorActive,
    const bool missionMovementLockHeld,
    const char* const backendLabel)
{
    const auto isHeld = [&inputHeld](
        const VrInput::Action action)
    {
        return inputHeld[
            static_cast<std::size_t>(action)];
    };

    const auto vectorFor = [&inputVectors](
        const VrInput::Action action) -> const XrVector2f&
    {
        return inputVectors[
            static_cast<std::size_t>(action)];
    };

    const auto vectorActive = [&inputVectorActive](
        const VrInput::Action action)
    {
        return inputVectorActive[
            static_cast<std::size_t>(action)];
    };

    const XrVector2f& moveAxis =
        vectorFor(VrInput::Action::Move);
    const XrVector2f& turnAxis =
        vectorFor(VrInput::Action::Turn);
    const bool moveAxisValid =
        vectorActive(VrInput::Action::Move);
    const bool turnAxisValid =
        vectorActive(VrInput::Action::Turn);

    if (VR_UpdateHudEditorInput(
            inputHeld,
            inputVectors,
            inputVectorActive))
    {
        {
            std::lock_guard<std::mutex> lock(
                g_vrHeadOrientationMutex);
            g_vrLeftThumbstickValid = false;
            g_vrLeftThumbstick[0] = 0.0f;
            g_vrLeftThumbstick[1] = 0.0f;
            g_vrRightThumbstickValid = false;
            g_vrRightThumbstickX = 0.0f;
            g_vrRightThumbstickY = 0.0f;
            g_vrConfiguredAimHeld = false;
            g_vrLeftTriggerJumpHeld = false;
            g_vrLeftXUseHeld = false;
            g_vrLeftStickSprintHeld = false;
            g_vrLeftYNextWeaponHeld = false;
            g_vrRightAButtonHeld = false;
            g_vrRightStickMeleeHeld = false;
            g_vrRightBStanceHeld = false;
            g_vrLowerStanceHeld = false;
            g_vrNativeOffhandHeld = false;
            g_vrLeftMenuHeld = false;
            g_vrMenuConfirmHeld = false;
            g_vrMenuBackHeld = false;
        }
        {
            std::lock_guard<std::mutex> lock(
                g_vrWeaponControllerPoseMutex);
            g_vrRightControllerAttackPressed = false;
        }
        g_vrInputActionPreviousHeld = inputHeld;
        return;
    }

    bool openPauseMenu = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        g_vrLeftThumbstickValid = moveAxisValid;
        g_vrLeftThumbstick[0] = moveAxisValid
            ? moveAxis.x
            : 0.0f;
        g_vrLeftThumbstick[1] = moveAxisValid
            ? moveAxis.y
            : 0.0f;

        g_vrRightThumbstickValid = turnAxisValid;
        g_vrRightThumbstickX = turnAxisValid
            ? turnAxis.x
            : 0.0f;
        g_vrRightThumbstickY = turnAxisValid
            ? turnAxis.y
            : 0.0f;

        g_vrMenuNavigationAxis =
            vectorFor(VrInput::Action::MenuNavigate);
        g_vrMenuNavigationAxisValid =
            vectorActive(VrInput::Action::MenuNavigate);
        g_vrScopeZoomAxis =
            vectorFor(VrInput::Action::ScopeZoom);
        g_vrScopeZoomAxisValid =
            vectorActive(VrInput::Action::ScopeZoom);

        g_vrRightThumbrestTouched =
            missionMovementLockHeld;

        g_vrConfiguredAimHeld =
            isHeld(VrInput::Action::Aim);

        g_vrLeftTriggerJumpHeld =
            isHeld(VrInput::Action::Jump);
        g_vrLeftXUseHeld =
            isHeld(VrInput::Action::Use);
        g_vrLeftStickSprintHeld =
            isHeld(VrInput::Action::Sprint);
        g_vrLeftYNextWeaponHeld =
            isHeld(VrInput::Action::NextWeapon);

        g_vrRightAButtonHeld =
            isHeld(VrInput::Action::Reload);
        g_vrRightStickMeleeHeld =
            isHeld(VrInput::Action::Melee);
        g_vrRightBStanceHeld =
            isHeld(VrInput::Action::Stance);
        g_vrLowerStanceHeld =
            isHeld(VrInput::Action::LowerStance);
        g_vrNativeOffhandHeld =
            isHeld(VrInput::Action::Offhand);

        g_vrLeftMenuHeld =
            isHeld(VrInput::Action::PauseMenu);
        g_vrMenuConfirmHeld =
            isHeld(VrInput::Action::MenuConfirm);
        g_vrMenuBackHeld =
            isHeld(VrInput::Action::MenuBack);

        openPauseMenu =
            g_vrLeftMenuHeld &&
            !g_vrLeftMenuWasHeld;
        g_vrLeftMenuWasHeld =
            g_vrLeftMenuHeld;
    }

    if (openPauseMenu)
    {
        const std::uint32_t eventTime =
            static_cast<std::uint32_t>(
                Sys_Milliseconds());

        CL_KeyEvent(0, 27, 1, eventTime);
        CL_KeyEvent(0, 27, 0, eventTime);

        static bool loggedVrMenuButton = false;
        if (!loggedVrMenuButton)
        {
            Com_Printf(
                0,
                "[VR] Bound the configured pause action "
                "to Escape.\n");
            loggedVrMenuButton = true;
        }
    }

    const bool attackPressed =
        isHeld(VrInput::Action::Attack);

    bool logAttackInjection = false;
    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        logAttackInjection =
            attackPressed &&
            !g_vrRightControllerAttackPressed;
        g_vrRightControllerAttackPressed =
            attackPressed;
    }

    if (logAttackInjection &&
        !g_vrLoggedRightControllerAttackInjection)
    {
        Com_Printf(
            0,
            "[VR] Configured fire binding is ready to inject "
            "BUTTON_ATTACK.\n");
        g_vrLoggedRightControllerAttackInjection = true;
    }

    static bool loggedControllerInputV4 = false;
    if (!loggedControllerInputV4)
    {
        Com_Printf(
            0,
            "[VR][CONTROLS] Controller Input V4 is routing "
            "gameplay actions through %s independently of "
            "controller model.\n",
            backendLabel != nullptr ? backendLabel : "the active runtime");
        loggedControllerInputV4 = true;
    }

    struct DirectMissionBinding
    {
        VrInput::Action action;
        int key;
    };

    constexpr std::array<DirectMissionBinding, 4>
        directMissionBindings = {{
            {VrInput::Action::GrenadeLauncher, '5'},
            {VrInput::Action::NightVision, 'n'},
            {VrInput::Action::Airstrike, '6'},
            {VrInput::Action::C4, '7'},
        }};

    const bool gameplayInputAllowed =
        !Key_IsCatcherActive(0, 0x10);

    bool anyMissionShortcutHeld = false;
    bool firedMissionShortcut = false;

    for (const DirectMissionBinding& binding :
         directMissionBindings)
    {
        const std::size_t actionIndex =
            static_cast<std::size_t>(binding.action);

        anyMissionShortcutHeld =
            anyMissionShortcutHeld || inputHeld[actionIndex];

        if (gameplayInputAllowed &&
            g_vrMissionShortcutArmed &&
            inputHeld[actionIndex] &&
            !g_vrInputActionPreviousHeld[actionIndex])
        {
            const std::uint32_t eventTime =
                static_cast<std::uint32_t>(
                    Sys_Milliseconds());

            CL_KeyEvent(0, binding.key, 1, eventTime);
            CL_KeyEvent(0, binding.key, 0, eventTime);
            firedMissionShortcut = true;
            g_vrMissionShortcutArmed = false;
        }
    }

    if (!anyMissionShortcutHeld)
    {
        g_vrMissionShortcutArmed = true;
    }
    else if (firedMissionShortcut)
    {
        g_vrMissionShortcutArmed = false;
    }

    g_vrInputActionPreviousHeld = inputHeld;
}

void VR_ResolveOffhandGripModes(
    const bool bindingHeld,
    bool* const supportGripHeld,
    bool* const objectGripHeld)
{
    if (supportGripHeld == nullptr || objectGripHeld == nullptr)
    {
        return;
    }

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();
    const bool supportPressedEdge =
        bindingHeld && !g_vrSupportGripBindingWasHeld;
    const bool objectPressedEdge =
        bindingHeld && !g_vrObjectGripBindingWasHeld;

    if (supportPressedEdge &&
        configurable.supportGripMode ==
            VrInteractions::SupportGripMode::Toggle)
    {
        g_vrSupportGripToggleLatched =
            !g_vrSupportGripToggleLatched;
    }
    if (objectPressedEdge &&
        configurable.objectGripMode ==
            VrInteractions::ObjectGripMode::Toggle)
    {
        g_vrObjectGripToggleLatched =
            !g_vrObjectGripToggleLatched;
    }

    switch (configurable.supportGripMode)
    {
    case VrInteractions::SupportGripMode::Toggle:
        *supportGripHeld = g_vrSupportGripToggleLatched;
        break;
    case VrInteractions::SupportGripMode::Proximity:
        *supportGripHeld = true;
        break;
    case VrInteractions::SupportGripMode::Hold:
    default:
        *supportGripHeld = bindingHeld;
        break;
    }

    *objectGripHeld =
        configurable.objectGripMode ==
                VrInteractions::ObjectGripMode::Toggle
            ? g_vrObjectGripToggleLatched
            : bindingHeld;

    g_vrSupportGripBindingWasHeld = bindingHeld;
    g_vrObjectGripBindingWasHeld = bindingHeld;
}

// Both runtime backends publish the semantic weapon-hand and off-hand poses
// into the same guarded state. Keep the two-hand qualification and blend in a
// backend-neutral step so OpenXR and the legacy OpenVR fallback cannot drift.
// Caller must hold g_vrWeaponControllerPoseMutex.
bool VR_IsSupportGripCandidateLocked()
{
    if (!g_vrLeftControllerForegripPoseValid ||
        !g_vrRightControllerWeaponPoseValid)
    {
        return false;
    }

    const float handDelta[3] = {
        g_vrLeftControllerForegripPosition[0] -
            g_vrRightControllerWeaponPosition[0],
        g_vrLeftControllerForegripPosition[1] -
            g_vrRightControllerWeaponPosition[1],
        g_vrLeftControllerForegripPosition[2] -
            g_vrRightControllerWeaponPosition[2],
    };

    const float handDistance =
        std::sqrt(
            handDelta[0] * handDelta[0] +
            handDelta[1] * handDelta[1] +
            handDelta[2] * handDelta[2]);

    const float forwardDistance =
        handDelta[0] *
            g_vrRightControllerWeaponAxis[0][0] +
        handDelta[1] *
            g_vrRightControllerWeaponAxis[0][1] +
        handDelta[2] *
            g_vrRightControllerWeaponAxis[0][2];

    const float minimumDistance =
        g_vrTwoHandWeaponTargetActive
            ? 3.0f
            : 4.0f;

    const float maximumDistance =
        g_vrTwoHandWeaponTargetActive
            ? 36.0f
            : 32.0f;

    const float minimumForwardDistance =
        g_vrTwoHandWeaponTargetActive
            ? -1.0f
            : 1.0f;

    return
        handDistance >= minimumDistance &&
        handDistance <= maximumDistance &&
        forwardDistance >= minimumForwardDistance;
}

void VR_UpdateTwoHandWeaponTargetFromPublishedPoses()
{
    bool logTwoHandEngaged = false;
    bool logTwoHandReleased = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        bool targetActive = false;

        if (g_vrLeftControllerForegripPressed)
        {
            targetActive =
                VR_IsSupportGripCandidateLocked();
        }

        logTwoHandEngaged =
            targetActive &&
            !g_vrTwoHandWeaponTargetActive;

        logTwoHandReleased =
            !targetActive &&
            g_vrTwoHandWeaponTargetActive;

        g_vrTwoHandWeaponTargetActive =
            targetActive;

        const float targetBlend =
            targetActive
                ? VR_GetConfiguratorSettings().twoHandStrength
                : 0.0f;

        const float blendRate =
            targetActive
                ? 0.22f
                : 0.18f;

        g_vrTwoHandWeaponBlend +=
            (targetBlend -
             g_vrTwoHandWeaponBlend) *
            blendRate;

        if (g_vrTwoHandWeaponBlend < 0.001f)
        {
            g_vrTwoHandWeaponBlend = 0.0f;
        }
        else if (g_vrTwoHandWeaponBlend > 0.999f)
        {
            g_vrTwoHandWeaponBlend = 1.0f;
        }
    }

    if (VR_VerboseDiagnosticsEnabled() &&
        logTwoHandEngaged)
    {
        Com_Printf(
            0,
            "[VR] Off-hand controller engaged optional "
            "two-hand weapon stabilization.\n");
    }

    if (VR_VerboseDiagnosticsEnabled() &&
        logTwoHandReleased)
    {
        Com_Printf(
            0,
            "[VR] Off-hand controller released optional "
            "two-hand weapon stabilization.\n");
    }
}

void VR_UpdateControllerActions(
    const XrTime displayTime)
{
    if (!g_vrControllerActionsAttached ||
        !g_vrControllerSpacesCreated ||
        !g_vrSessionRunning)
    {
        return;
    }

    XrActiveActionSet activeActionSet = {};
    activeActionSet.actionSet =
        g_vrControllerActionSet;
    activeActionSet.subactionPath =
        XR_NULL_PATH;

    XrActionsSyncInfo syncInfo{
        XR_TYPE_ACTIONS_SYNC_INFO
    };

    syncInfo.countActiveActionSets = 1u;
    syncInfo.activeActionSets =
        &activeActionSet;

    const XrResult syncResult =
        xrSyncActions(
            g_vrSession,
            &syncInfo);

    if (XR_FAILED(syncResult))
    {
        VR_LogXrFailure(
            "xrSyncActions",
            syncResult);

        return;
    }

    ++g_vrControllerDiagnosticFrame;

    const bool logPeriodicSnapshot =
        VR_VerboseDiagnosticsEnabled() &&
        (g_vrControllerDiagnosticFrame % 180u) ==
        0u;

    VrInputHeldState inputHeld = {};
    VrInputVectorState inputVectors = {};
    VrInputActiveState inputVectorActive = {};
    bool missionMovementLockHeld = false;
    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    const XrAction missionTouchAction =
        VR_FindInputTermActionForPhysicalSource(
            VrInput::Source::RightThumbrestTouch);
    const XrAction leftPrimaryAxisAction =
        VR_FindInputTermActionForPhysicalSource(
            VrInput::Source::LeftPrimaryAxis);
    const XrAction rightPrimaryAxisAction =
        VR_FindInputTermActionForPhysicalSource(
            VrInput::Source::RightPrimaryAxis);

    bool missionTouchHeld = false;
    bool missionTouchActive = false;
    const bool missionTouchValid =
        missionTouchAction != XR_NULL_HANDLE &&
        VR_GetControllerBooleanState(
            missionTouchAction,
            XR_NULL_PATH,
            &missionTouchHeld,
            &missionTouchActive);

    XrVector2f leftPrimaryAxis = {};
    bool leftPrimaryAxisActive = false;
    const bool leftPrimaryAxisValid =
        leftPrimaryAxisAction != XR_NULL_HANDLE &&
        VR_GetControllerVector2State(
            leftPrimaryAxisAction,
            XR_NULL_PATH,
            &leftPrimaryAxis,
            &leftPrimaryAxisActive);

    XrVector2f rightPrimaryAxis = {};
    bool rightPrimaryAxisActive = false;
    const bool rightPrimaryAxisValid =
        rightPrimaryAxisAction != XR_NULL_HANDLE &&
        VR_GetControllerVector2State(
            rightPrimaryAxisAction,
            XR_NULL_PATH,
            &rightPrimaryAxis,
            &rightPrimaryAxisActive);

    const VrInput::OpenVrMissionSelectorUpdate missionSelector =
        VrInput::UpdateOpenVrMissionSelector(
            &g_vrOpenXrMissionSelector,
            missionTouchValid && missionTouchActive,
            missionTouchHeld,
            {leftPrimaryAxis.x, leftPrimaryAxis.y},
            leftPrimaryAxisValid && leftPrimaryAxisActive,
            {rightPrimaryAxis.x, rightPrimaryAxis.y},
            rightPrimaryAxisValid && rightPrimaryAxisActive);

    if (missionSelector.available &&
        !g_vrOpenXrLoggedMissionSelector)
    {
        Com_Printf(
            0,
            "[VR][OPENXR][CONTROLS] V103 guarded mission selector is "
            "active: start with both sticks centered, touch the right "
            "thumbrest, then move the left stick; right-stick movement "
            "cancels selection.\n");
        g_vrOpenXrLoggedMissionSelector = true;
    }

    bool nightVisionGestureGripPressed = false;
    bool nightVisionGestureGripActive = false;
    const bool nightVisionGestureGripValid =
        VR_GetControllerBooleanState(
            g_vrNightVisionGestureGripAction,
            g_vrControllerHandPaths[
                VR_CONTROLLER_LEFT],
            &nightVisionGestureGripPressed,
            &nightVisionGestureGripActive);

    const VrControllerRenderPose&
        nightVisionGesturePose =
            g_vrControllerRenderPoses[
                VR_CONTROLLER_LEFT];

    const VrGestures::NightVisionVisorUpdate
        nightVisionGesture =
            VR_UpdateNightVisionVisorGesture(
                nightVisionGesturePose.gripValid,
                nightVisionGesturePose.gripPose,
                nightVisionGestureGripValid &&
                    nightVisionGestureGripActive,
                nightVisionGestureGripPressed,
                "OpenXR");

    for (const VrInput::ActionDefinition& action :
         VrInput::ActionDefinitions())
    {
        const std::size_t actionIndex =
            static_cast<std::size_t>(action.action);

        if (action.valueType == VrInput::ValueType::Boolean)
        {
            bool held = false;

            for (std::size_t bindingIndex = 0u;
                 bindingIndex < 2u;
                 ++bindingIndex)
            {
                const VrInput::Binding& binding =
                    configurable.bindings[actionIndex][bindingIndex];
                if (binding.sourceCount == 0u)
                {
                    continue;
                }

                const bool missionShortcut =
                    action.action == VrInput::Action::GrenadeLauncher ||
                    action.action == VrInput::Action::NightVision ||
                    action.action == VrInput::Action::Airstrike ||
                    action.action == VrInput::Action::C4;
                const bool guardedMissionBinding =
                    missionShortcut &&
                    VrInput::UsesOpenVrMissionSelector(binding);

                bool chordHeld = true;
                for (std::size_t termIndex = 0u;
                     termIndex < binding.sourceCount;
                     ++termIndex)
                {
                    const VrInput::Source source =
                        binding.sources[termIndex];
                    const XrAction termAction =
                        VR_GetInputTermAction(
                            action.action,
                            bindingIndex,
                            termIndex);

                    bool termHeld = false;
                    if (VrInput::IsDirectionalSource(source))
                    {
                        XrVector2f value = {};
                        bool active = false;
                        const bool valid =
                            VR_GetControllerVector2State(
                                termAction,
                                XR_NULL_PATH,
                                &value,
                                &active);
                        bool& latched =
                            g_vrDirectionalTermLatched[actionIndex]
                                [bindingIndex][termIndex];

                        if (!valid || !active ||
                            VrInput::DirectionalSourceReleased(
                                source,
                                value.x,
                                value.y))
                        {
                            latched = false;
                        }
                        else if (!latched)
                        {
                            const bool deliberateDirection =
                                action.action ==
                                    VrInput::Action::Jump ||
                                action.action ==
                                    VrInput::Action::LowerStance;

                            latched =
                                VrInput::DirectionalSourcePressed(
                                    source,
                                    value.x,
                                    value.y,
                                    deliberateDirection ? 0.80f : 0.75f,
                                    deliberateDirection ? 0.15f : 0.12f);
                        }

                        termHeld = latched;
                    }
                    else
                    {
                        bool pressed = false;
                        bool active = false;
                        const bool valid =
                            VR_GetControllerBooleanState(
                                termAction,
                                XR_NULL_PATH,
                                &pressed,
                                &active);
                        termHeld = valid && active && pressed;
                    }

                    if (nightVisionGesture.consumeLeftGrip &&
                        source ==
                            VrInput::Source::LeftSqueeze)
                    {
                        termHeld = false;
                    }

                    if (guardedMissionBinding &&
                        source == VrInput::Source::RightThumbrestTouch)
                    {
                        termHeld = missionSelector.modifierHeld;
                    }

                    if (missionShortcut &&
                        source == VrInput::Source::RightThumbrestTouch &&
                        termHeld)
                    {
                        missionMovementLockHeld = true;
                    }

                    chordHeld = chordHeld && termHeld;
                }

                held = held || chordHeld;
            }

            inputHeld[actionIndex] = held;
            continue;
        }

        float selectedMagnitudeSquared = -1.0f;
        for (std::size_t bindingIndex = 0u;
             bindingIndex < 2u;
             ++bindingIndex)
        {
            const VrInput::Binding& binding =
                configurable.bindings[actionIndex][bindingIndex];
            if (binding.sourceCount != 1u)
            {
                continue;
            }

            XrVector2f candidate = {};
            bool active = false;
            const bool valid = VR_GetControllerVector2State(
                VR_GetInputTermAction(
                    action.action,
                    bindingIndex,
                    0u),
                XR_NULL_PATH,
                &candidate,
                &active);
            if (!valid || !active)
            {
                continue;
            }

            const float magnitudeSquared =
                candidate.x * candidate.x +
                candidate.y * candidate.y;
            if (!inputVectorActive[actionIndex] ||
                magnitudeSquared > selectedMagnitudeSquared)
            {
                inputVectors[actionIndex] = candidate;
                inputVectorActive[actionIndex] = true;
                selectedMagnitudeSquared = magnitudeSquared;
            }
        }
    }

    if (nightVisionGesture.toggledThisFrame)
    {
        inputHeld[
            static_cast<std::size_t>(
                VrInput::Action::NightVision)] = true;
    }

    const auto isHeld = [&inputHeld](
        const VrInput::Action action)
    {
        return inputHeld[
            static_cast<std::size_t>(action)];
    };

    VR_ApplyControllerInputState(
        inputHeld,
        inputVectors,
        inputVectorActive,
        missionMovementLockHeld,
        "OpenXR");

    const std::uint32_t weaponHandIndex =
        VrInteractions::WeaponControllerIndex(
            configurable.dominantHand);
    const std::uint32_t offHandIndex =
        VrInteractions::OffHandControllerIndex(
            configurable.dominantHand);
    const bool rawOffhandGripHeld =
        isHeld(VrInput::Action::SupportGrip);
    bool supportGripHeld = false;
    bool objectGripHeld = false;
    VR_ResolveOffhandGripModes(
        rawOffhandGripHeld,
        &supportGripHeld,
        &objectGripHeld);

    for (std::uint32_t handIndex = 0u;
         handIndex < kVrControllerCount;
         ++handIndex)
    {
        const XrPath handPath =
            g_vrControllerHandPaths[handIndex];

        bool gripActive = false;
        bool palmActive = false;
        bool aimActive = false;

        VR_GetControllerPoseState(
            g_vrGripPoseAction,
            handPath,
            &gripActive);

        VR_GetControllerPoseState(
            g_vrAimPoseAction,
            handPath,
            &aimActive);

        if (g_vrPalmPoseAction != XR_NULL_HANDLE)
        {
            VR_GetControllerPoseState(
                g_vrPalmPoseAction,
                handPath,
                &palmActive);
        }

        XrSpaceLocation gripLocation{
            XR_TYPE_SPACE_LOCATION
        };

        XrSpaceVelocity gripVelocity{
            XR_TYPE_SPACE_VELOCITY
        };

        XrSpaceLocation palmLocation{
            XR_TYPE_SPACE_LOCATION
        };

        XrSpaceLocation aimLocation{
            XR_TYPE_SPACE_LOCATION
        };

        const bool gripValid =
            gripActive &&
            VR_LocateControllerSpace(
                g_vrControllerGripSpaces[
                    handIndex],
                displayTime,
                &gripLocation,
                &gripVelocity);

        const bool palmValid =
            palmActive &&
            g_vrControllerPalmSpaces[handIndex] !=
                XR_NULL_HANDLE &&
            VR_LocateControllerSpace(
                g_vrControllerPalmSpaces[
                    handIndex],
                displayTime,
                &palmLocation);

        const bool aimValid =
            aimActive &&
            VR_LocateControllerSpace(
                g_vrControllerAimSpaces[
                    handIndex],
                displayTime,
                &aimLocation);

        VrControllerRenderPose& renderPose =
            g_vrControllerRenderPoses[handIndex];

        renderPose.gripValid = gripValid;
        renderPose.palmValid = palmValid;
        renderPose.aimValid = aimValid;

        if (gripValid)
        {
            renderPose.gripPose =
                gripLocation.pose;
        }

        if (palmValid)
        {
            renderPose.palmPose =
                palmLocation.pose;
        }

        if (aimValid)
        {
            renderPose.aimPose =
                aimLocation.pose;
        }

        if (handIndex == weaponHandIndex)
        {
            if (gripValid && aimValid)
            {
                VR_PublishRightControllerWeaponPose(
                    gripLocation.pose,
                    aimLocation.pose,
                    gripVelocity.linearVelocity,
                    (gripVelocity.velocityFlags &
                     XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0);
            }
            else
            {
                VR_InvalidateRightControllerWeaponPose();
            }
        }

        if (gripValid &&
            !g_vrLoggedFirstGripPose[handIndex])
        {
            Com_Printf(
                0,
                "[VR] Located first valid %s "
                "controller grip pose.\n",
                VR_ControllerHandName(handIndex));

            g_vrLoggedFirstGripPose[handIndex] =
                true;
        }

        if (palmValid &&
            !g_vrLoggedFirstPalmPose[handIndex])
        {
            Com_Printf(
                0,
                "[VR][HANDS] Located first valid %s controller "
                "XR_EXT_palm_pose surface.\n",
                VR_ControllerHandName(handIndex));

            g_vrLoggedFirstPalmPose[handIndex] =
                true;
        }

        if (aimValid &&
            !g_vrLoggedFirstAimPose[handIndex])
        {
            Com_Printf(
                0,
                "[VR] Located first valid %s "
                "controller aim pose.\n",
                VR_ControllerHandName(handIndex));

            g_vrLoggedFirstAimPose[handIndex] =
                true;
        }

        const bool triggerPressed =
            handIndex == weaponHandIndex
                ? isHeld(VrInput::Action::Attack)
                : (handIndex == offHandIndex
                       ? isHeld(VrInput::Action::Jump)
                       : false);

        const bool squeezePressed =
            handIndex == offHandIndex
                ? rawOffhandGripHeld
                : (handIndex == weaponHandIndex
                       ? isHeld(VrInput::Action::Offhand)
                       : false);

        const float triggerValue =
            triggerPressed ? 1.0f : 0.0f;

        const float squeezeValue =
            squeezePressed ? 1.0f : 0.0f;

        if (VR_VerboseDiagnosticsEnabled() &&
            triggerPressed &&
            !g_vrControllerTriggerPressed[
                handIndex])
        {
            Com_Printf(
                0,
                "[VR] Controller %s trigger "
                "crossed 0.75.\n",
                VR_ControllerHandName(handIndex));
        }

        if (VR_VerboseDiagnosticsEnabled() &&
            squeezePressed &&
            !g_vrControllerSqueezePressed[
                handIndex])
        {
            Com_Printf(
                0,
                "[VR] Controller %s squeeze "
                "crossed 0.75.\n",
                VR_ControllerHandName(handIndex));
        }

        g_vrControllerTriggerPressed[handIndex] =
            triggerPressed;

        g_vrControllerSqueezePressed[handIndex] =
            squeezePressed;

        if (handIndex == offHandIndex)
        {
            VR_PublishLeftControllerForegripPose(
                gripLocation.pose,
                gripValid,
                objectGripHeld,
                supportGripHeld,
                gripVelocity.linearVelocity,
                gripValid &&
                    (gripVelocity.velocityFlags &
                     XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0);

            VR_PublishLeftControllerPalmPose(
                palmLocation.pose,
                palmValid);
        }

        if (logPeriodicSnapshot &&
            aimValid)
        {
            VR_LogControllerPoseSnapshot(
                handIndex,
                aimLocation,
                triggerValue,
                squeezeValue);
        }
    }

    VR_UpdateTwoHandWeaponTargetFromPublishedPoses();

    VR_UpdatePoseFocusAimFromControllers();
}

bool VR_CreateSession()
{
    PFN_xrGetD3D11GraphicsRequirementsKHR
        getD3D11GraphicsRequirements = nullptr;

    XrResult result =
        xrGetInstanceProcAddr(
            g_vrInstance,
            "xrGetD3D11GraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(
                &getD3D11GraphicsRequirements));

    if (XR_FAILED(result) ||
        getD3D11GraphicsRequirements == nullptr)
    {
        VR_LogXrFailure(
            "xrGetInstanceProcAddr("
            "xrGetD3D11GraphicsRequirementsKHR)",
            result);

        return false;
    }

    XrGraphicsRequirementsD3D11KHR requirements{
        XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR
    };

    result =
        getD3D11GraphicsRequirements(
            g_vrInstance,
            g_vrSystemId,
            &requirements);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetD3D11GraphicsRequirementsKHR",
            result);

        return false;
    }

    if (!VR_CreateD3D11Device(requirements))
    {
        return false;
    }

    XrGraphicsBindingD3D11KHR graphicsBinding{
        XR_TYPE_GRAPHICS_BINDING_D3D11_KHR
    };

    graphicsBinding.device = g_vrD3dDevice.Get();

    XrSessionCreateInfo sessionCreateInfo{
        XR_TYPE_SESSION_CREATE_INFO
    };

    sessionCreateInfo.next = &graphicsBinding;
    sessionCreateInfo.systemId = g_vrSystemId;

    result =
        xrCreateSession(
            g_vrInstance,
            &sessionCreateInfo,
            &g_vrSession);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrCreateSession", result);
        return false;
    }

    XrReferenceSpaceCreateInfo spaceCreateInfo{
        XR_TYPE_REFERENCE_SPACE_CREATE_INFO
    };

    spaceCreateInfo.referenceSpaceType =
        XR_REFERENCE_SPACE_TYPE_LOCAL;

    spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;

    result =
        xrCreateReferenceSpace(
            g_vrSession,
            &spaceCreateInfo,
            &g_vrAppSpace);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrCreateReferenceSpace", result);
        return false;
    }

    // Keep gameplay in LOCAL space, but create a second floor-referenced
    // STAGE space when the runtime exposes it. The guided standing-height
    // measurement uses this space only for one sample and never changes the
    // compositor origin or the user's guardian/boundary configuration.
    std::uint32_t referenceSpaceCount = 0u;
    result = xrEnumerateReferenceSpaces(
        g_vrSession,
        0u,
        &referenceSpaceCount,
        nullptr);

    if (XR_SUCCEEDED(result) && referenceSpaceCount > 0u)
    {
        std::vector<XrReferenceSpaceType> referenceSpaces(
            referenceSpaceCount);

        result = xrEnumerateReferenceSpaces(
            g_vrSession,
            referenceSpaceCount,
            &referenceSpaceCount,
            referenceSpaces.data());

        const bool stageAvailable =
            XR_SUCCEEDED(result) &&
            std::find(
                referenceSpaces.begin(),
                referenceSpaces.end(),
                XR_REFERENCE_SPACE_TYPE_STAGE) !=
                    referenceSpaces.end();

        if (stageAvailable)
        {
            XrReferenceSpaceCreateInfo floorSpaceCreateInfo{
                XR_TYPE_REFERENCE_SPACE_CREATE_INFO
            };

            floorSpaceCreateInfo.referenceSpaceType =
                XR_REFERENCE_SPACE_TYPE_STAGE;
            floorSpaceCreateInfo.poseInReferenceSpace.orientation.w =
                1.0f;

            result = xrCreateReferenceSpace(
                g_vrSession,
                &floorSpaceCreateInfo,
                &g_vrCalibrationFloorSpace);

            g_vrCalibrationFloorSpaceAvailable =
                XR_SUCCEEDED(result) &&
                g_vrCalibrationFloorSpace != XR_NULL_HANDLE;
        }
    }

    Com_Printf(
        0,
        "[VR][CALIBRATION] OpenXR floor reference: %s.\n",
        g_vrCalibrationFloorSpaceAvailable
            ? "STAGE available"
            : "unavailable; manual height remains supported");

    if (!VR_SelectEnvironmentBlendMode())
    {
        return false;
    }

    if (g_vrControllerActionsCreated &&
        !VR_AttachControllerActions())
    {
        Com_PrintWarning(
            0,
            "[VR] Controller actions could not be "
            "attached. Rendering will continue "
            "without controller tracking.\n");
    }

    return true;
}

int64_t VR_SelectSwapchainFormat(
    const std::vector<int64_t>& runtimeFormats)
{
    constexpr std::array<DXGI_FORMAT, 4> preferredFormats = {
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM,
    };

    for (const DXGI_FORMAT preferredFormat : preferredFormats)
    {
        const int64_t candidate =
            static_cast<int64_t>(preferredFormat);

        if (std::find(
                runtimeFormats.begin(),
                runtimeFormats.end(),
                candidate) != runtimeFormats.end())
        {
            return candidate;
        }
    }

    return -1;
}

bool VR_CreateSwapchains()
{
    uint32_t viewCount = 0;

    XrResult result =
        xrEnumerateViewConfigurationViews(
            g_vrInstance,
            g_vrSystemId,
            kViewConfiguration,
            0,
            &viewCount,
            nullptr);

    if (XR_FAILED(result) || viewCount == 0)
    {
        VR_LogXrFailure(
            "xrEnumerateViewConfigurationViews(count)",
            result);

        return false;
    }

    g_vrViewConfigs.resize(viewCount);

    for (XrViewConfigurationView& viewConfig :
         g_vrViewConfigs)
    {
        viewConfig = XrViewConfigurationView{
            XR_TYPE_VIEW_CONFIGURATION_VIEW
        };
    }

    result =
        xrEnumerateViewConfigurationViews(
            g_vrInstance,
            g_vrSystemId,
            kViewConfiguration,
            viewCount,
            &viewCount,
            g_vrViewConfigs.data());

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrEnumerateViewConfigurationViews(list)",
            result);

        return false;
    }

    if (viewCount != 2)
    {
        Com_PrintWarning(
            0,
            "[VR] Expected two stereo views but runtime returned %u.\n",
            viewCount);

        return false;
    }

    uint32_t formatCount = 0;

    result =
        xrEnumerateSwapchainFormats(
            g_vrSession,
            0,
            &formatCount,
            nullptr);

    if (XR_FAILED(result) || formatCount == 0)
    {
        VR_LogXrFailure(
            "xrEnumerateSwapchainFormats(count)",
            result);

        return false;
    }

    std::vector<int64_t> runtimeFormats(formatCount);

    result =
        xrEnumerateSwapchainFormats(
            g_vrSession,
            formatCount,
            &formatCount,
            runtimeFormats.data());

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrEnumerateSwapchainFormats(list)",
            result);

        return false;
    }

    const int64_t selectedFormat =
        VR_SelectSwapchainFormat(runtimeFormats);

    if (selectedFormat < 0)
    {
        Com_PrintWarning(
            0,
            "[VR] Runtime did not expose a supported RGBA/BGRA "
            "swapchain format.\n");

        return false;
    }

    g_vrEyeSwapchains.resize(viewCount);
    g_vrViews.resize(viewCount);

    for (XrView& view : g_vrViews)
    {
        view = XrView{XR_TYPE_VIEW};
    }

    for (uint32_t eyeIndex = 0;
         eyeIndex < viewCount;
         ++eyeIndex)
    {
        const XrViewConfigurationView& viewConfig =
            g_vrViewConfigs[eyeIndex];

        VrEyeSwapchain& eyeSwapchain =
            g_vrEyeSwapchains[eyeIndex];

        // Preserve the runtime's rectangular per-eye shape.  Scale only
        // pixel density; OpenXR projection FOV remains unchanged.
        static const float outputScale = []() -> float
        {
            constexpr float defaultOutputScale = 1.00f;
            constexpr float minimumOutputScale = 0.50f;
            constexpr float maximumOutputScale = 1.00f;

            const char* requestedOutputScale =
                std::getenv(
                    "KISAK_VR_OUTPUT_SCALE");

            if (requestedOutputScale == nullptr ||
                requestedOutputScale[0] == '\0')
            {
                return defaultOutputScale;
            }

            char* parseEnd = nullptr;

            const float parsedOutputScale =
                std::strtof(
                    requestedOutputScale,
                    &parseEnd);

            if (parseEnd == requestedOutputScale ||
                parseEnd == nullptr ||
                parseEnd[0] != '\0' ||
                !std::isfinite(parsedOutputScale) ||
                parsedOutputScale < minimumOutputScale ||
                parsedOutputScale > maximumOutputScale)
            {
                Com_PrintWarning(
                    0,
                    "[VR] Ignoring invalid "
                    "KISAK_VR_OUTPUT_SCALE='%s'; using %.2f. "
                    "Valid range is %.2f through %.2f.\n",
                    requestedOutputScale,
                    defaultOutputScale,
                    minimumOutputScale,
                    maximumOutputScale);

                return defaultOutputScale;
            }

            return parsedOutputScale;
        }();

        g_vrOutputScale = outputScale;

        eyeSwapchain.width =
            static_cast<int32_t>(
                static_cast<float>(
                    viewConfig.recommendedImageRectWidth) *
                    outputScale +
                0.5f);

        eyeSwapchain.height =
            static_cast<int32_t>(
                static_cast<float>(
                    viewConfig.recommendedImageRectHeight) *
                    outputScale +
                0.5f);

        XrSwapchainCreateInfo swapchainCreateInfo{
            XR_TYPE_SWAPCHAIN_CREATE_INFO
        };

        swapchainCreateInfo.usageFlags =
            XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
            XR_SWAPCHAIN_USAGE_SAMPLED_BIT;

        swapchainCreateInfo.format = selectedFormat;

        // The captured D3D9 frame has already resolved r_aaSamples.
        // Multisampling this full-screen copy wastes swapchain memory.
        swapchainCreateInfo.sampleCount = 1u;
        swapchainCreateInfo.width =
            static_cast<uint32_t>(
                eyeSwapchain.width);

        swapchainCreateInfo.height =
            static_cast<uint32_t>(
                eyeSwapchain.height);
        swapchainCreateInfo.faceCount = 1;
        swapchainCreateInfo.arraySize = 1;
        swapchainCreateInfo.mipCount = 1;

        result =
            xrCreateSwapchain(
                g_vrSession,
                &swapchainCreateInfo,
                &eyeSwapchain.handle);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure("xrCreateSwapchain", result);
            return false;
        }

        uint32_t imageCount = 0;

        result =
            xrEnumerateSwapchainImages(
                eyeSwapchain.handle,
                0,
                &imageCount,
                nullptr);

        if (XR_FAILED(result) || imageCount == 0)
        {
            VR_LogXrFailure(
                "xrEnumerateSwapchainImages(count)",
                result);

            return false;
        }

        eyeSwapchain.images.resize(imageCount);

        for (XrSwapchainImageD3D11KHR& image :
             eyeSwapchain.images)
        {
            image = XrSwapchainImageD3D11KHR{
                XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR
            };
        }

        result =
            xrEnumerateSwapchainImages(
                eyeSwapchain.handle,
                imageCount,
                &imageCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(
                    eyeSwapchain.images.data()));

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrEnumerateSwapchainImages(list)",
                result);

            return false;
        }

        eyeSwapchain.renderTargetViews.resize(imageCount);

        for (uint32_t imageIndex = 0;
             imageIndex < imageCount;
             ++imageIndex)
        {
            ID3D11Texture2D* texture =
                eyeSwapchain.images[imageIndex].texture;

            D3D11_TEXTURE2D_DESC textureDescription = {};
            texture->GetDesc(&textureDescription);

            D3D11_RENDER_TARGET_VIEW_DESC
                renderTargetViewDescription = {};

            // OpenXR runtimes may expose the swapchain texture through a
            // typeless resource. A null RTV description then fails because
            // Direct3D cannot infer the typed view format. Use the exact
            // typed format selected through xrEnumerateSwapchainFormats.
            renderTargetViewDescription.Format =
                static_cast<DXGI_FORMAT>(selectedFormat);

            const bool isMultisampled =
                textureDescription.SampleDesc.Count > 1;

            const bool isArrayTexture =
                textureDescription.ArraySize > 1;

            if (isArrayTexture && isMultisampled)
            {
                renderTargetViewDescription.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;

                renderTargetViewDescription
                    .Texture2DMSArray.FirstArraySlice = 0;

                renderTargetViewDescription
                    .Texture2DMSArray.ArraySize = 1;
            }
            else if (isArrayTexture)
            {
                renderTargetViewDescription.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2DARRAY;

                renderTargetViewDescription
                    .Texture2DArray.MipSlice = 0;

                renderTargetViewDescription
                    .Texture2DArray.FirstArraySlice = 0;

                renderTargetViewDescription
                    .Texture2DArray.ArraySize = 1;
            }
            else if (isMultisampled)
            {
                renderTargetViewDescription.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2DMS;
            }
            else
            {
                renderTargetViewDescription.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2D;

                renderTargetViewDescription
                    .Texture2D.MipSlice = 0;
            }

            const HRESULT hr =
                g_vrD3dDevice->CreateRenderTargetView(
                    texture,
                    &renderTargetViewDescription,
                    eyeSwapchain
                        .renderTargetViews[imageIndex]
                        .GetAddressOf());

            if (FAILED(hr))
            {
                Com_PrintWarning(
                    0,
                    "[VR] RTV failure: eye %u image %u, "
                    "resource format %u, selected format %lld, "
                    "array size %u, sample count %u.\n",
                    eyeIndex,
                    imageIndex,
                    static_cast<unsigned int>(
                        textureDescription.Format),
                    static_cast<long long>(selectedFormat),
                    textureDescription.ArraySize,
                    textureDescription.SampleDesc.Count);

                VR_LogHrFailure(
                    "ID3D11Device::CreateRenderTargetView",
                    hr);

                return false;
            }

            // FSR output uses no per-swapchain depth image.
        }

        Com_Printf(
            0,
            "[VR] OpenXR eye %u swapchain: %d x %d with %u images; "
            "runtime recommended %u x %u.\n",
            eyeIndex,
            eyeSwapchain.width,
            eyeSwapchain.height,
            imageCount,
            viewConfig.recommendedImageRectWidth,
            viewConfig.recommendedImageRectHeight);

        if (eyeIndex == 0u)
        {
            Com_Printf(
                0,
                "[VR] OpenXR rectangular output scale is %.2f; "
                "swapchain copies use one sample because D3D9 MSAA "
                "is already resolved.\n",
                g_vrOutputScale);

            const char* legacyEyeSize =
                std::getenv("KISAK_VR_EYE_SIZE");

            if (legacyEyeSize != nullptr &&
                legacyEyeSize[0] != '\0')
            {
                Com_PrintWarning(
                    0,
                    "[VR] KISAK_VR_EYE_SIZE is obsolete and ignored. "
                    "Use KISAK_VR_OUTPUT_SCALE=0.50..1.00.\n");
            }
        }
    }

    Com_Printf(
        0,
        "[VR] Selected OpenXR swapchain DXGI format %lld.\n",
        static_cast<long long>(selectedFormat));

    return true;
}

void VR_HandleSessionStateChanged(
    const XrEventDataSessionStateChanged& stateChanged)
{
    g_vrSessionState = stateChanged.state;

    Com_Printf(
        0,
        "[VR] OpenXR session state: %s.\n",
        VR_SessionStateName(g_vrSessionState));

    if (g_vrSessionState == XR_SESSION_STATE_READY &&
        !g_vrSessionRunning)
    {
        XrSessionBeginInfo beginInfo{
            XR_TYPE_SESSION_BEGIN_INFO
        };

        beginInfo.primaryViewConfigurationType =
            kViewConfiguration;

        const XrResult result =
            xrBeginSession(g_vrSession, &beginInfo);

        if (XR_SUCCEEDED(result))
        {
            g_vrSessionRunning = true;

            Com_Printf(
                0,
                "[VR] OpenXR session started.\n");
        }
        else
        {
            VR_LogXrFailure("xrBeginSession", result);
        }
    }
    else if (
        g_vrSessionState == XR_SESSION_STATE_STOPPING &&
        g_vrSessionRunning)
    {
        const XrResult result =
            xrEndSession(g_vrSession);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure("xrEndSession", result);
        }

        g_vrSessionRunning = false;
    }
    else if (
        g_vrSessionState == XR_SESSION_STATE_EXITING ||
        g_vrSessionState == XR_SESSION_STATE_LOSS_PENDING)
    {
        g_vrSessionRunning = false;
        g_vrExitRequested = true;
    }

    KisakCrash_SetVrState(
        g_vrInitialized,
        g_vrSessionRunning,
        static_cast<int>(g_vrSessionState),
        static_cast<unsigned int>(
            g_vrUploadedStereoSerial & 0xFFFFFFFFu),
        g_vrCapturedStereoWidth,
        g_vrCapturedStereoHeight);
}

void VR_LogCurrentControllerProfiles()
{
    if (g_vrSession == XR_NULL_HANDLE)
    {
        return;
    }

    for (std::uint32_t handIndex = 0u;
         handIndex < kVrControllerCount;
         ++handIndex)
    {
        XrInteractionProfileState state{
            XR_TYPE_INTERACTION_PROFILE_STATE
        };

        const XrResult result =
            xrGetCurrentInteractionProfile(
                g_vrSession,
                g_vrControllerHandPaths[handIndex],
                &state);

        if (XR_FAILED(result) ||
            state.interactionProfile == XR_NULL_PATH)
        {
            Com_PrintWarning(
                0,
                "[VR][CONTROLS] No active %s controller profile.\n",
                VR_ControllerHandName(handIndex));
            continue;
        }

        std::array<char, XR_MAX_PATH_LENGTH> path = {};
        std::uint32_t requiredLength = 0u;

        const XrResult pathResult =
            xrPathToString(
                g_vrInstance,
                state.interactionProfile,
                static_cast<std::uint32_t>(path.size()),
                &requiredLength,
                path.data());

        const char* const activeProfile =
            XR_SUCCEEDED(pathResult) && path[0] != '\0'
                ? path.data()
                : "unknown";

        std::snprintf(
            g_vrCompatibilityControllerProfiles[handIndex].data(),
            g_vrCompatibilityControllerProfiles[handIndex].size(),
            "%s",
            activeProfile);

        Com_Printf(
            0,
            "[VR][CONTROLS] Active %s interaction profile: %s.\n",
            VR_ControllerHandName(handIndex),
            activeProfile);
    }

    VR_AppendCompatibilityRuntimeReceipt();
}

void VR_PollEvents()
{
    XrEventDataBuffer eventData{
        XR_TYPE_EVENT_DATA_BUFFER
    };

    while (true)
    {
        const XrResult result =
            xrPollEvent(g_vrInstance, &eventData);

        if (result == XR_EVENT_UNAVAILABLE)
        {
            return;
        }

        if (XR_FAILED(result))
        {
            VR_LogXrFailure("xrPollEvent", result);
            return;
        }

        switch (eventData.type)
        {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
            {
                const auto* stateChanged =
                    reinterpret_cast<
                        const XrEventDataSessionStateChanged*>(
                            &eventData);

                VR_HandleSessionStateChanged(*stateChanged);
                break;
            }

            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                Com_PrintWarning(
                    0,
                    "[VR] OpenXR runtime reported instance loss "
                    "pending.\n");
                g_vrExitRequested = true;
                break;

            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                VR_LogCurrentControllerProfiles();
                break;

            default:
                break;
        }

        eventData = XrEventDataBuffer{
            XR_TYPE_EVENT_DATA_BUFFER
        };
    }
}

int VR_MenuKeyNumber(
    const char* keyName,
    const int fallback)
{
    const int keyNumber =
        Key_StringToKeynum(
            keyName);

    return keyNumber >= 0
        ? keyNumber
        : fallback;
}

void VR_SendMenuKeyTap(
    const int keyNumber)
{
    const std::uint32_t eventTime =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    CL_KeyEvent(
        0,
        keyNumber,
        1,
        eventTime);

    CL_KeyEvent(
        0,
        keyNumber,
        0,
        eventTime);
}

void VR_UpdateMenuControllerNavigation()
{
    static bool menuWasActive = false;
    static bool eyeLocalMenuWasActive = false;
    static bool confirmWasHeld = false;
    static bool backWasHeld = false;
    static bool loggedMenuCursor = false;

    static float cursorX = 0.0f;
    static float cursorY = 0.0f;
    static std::uint32_t lastUpdateTime = 0u;

    const bool menuActive =
        Key_IsCatcherActive(
            0,
            0x10);

    const std::uint32_t currentTime =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    if (!menuActive)
    {
        menuWasActive = false;
        eyeLocalMenuWasActive = false;
        confirmWasHeld = false;
        backWasHeld = false;
        lastUpdateTime = currentTime;
        return;
    }

    float stickX = 0.0f;
    float stickY = 0.0f;
    bool stickValid = false;
    bool confirmHeld = false;
    bool backHeld = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        stickValid =
            g_vrMenuNavigationAxisValid;

        if (stickValid)
        {
            stickX =
                g_vrMenuNavigationAxis.x;

            stickY =
                g_vrMenuNavigationAxis.y;
        }

        confirmHeld =
            g_vrMenuConfirmHeld;

        backHeld =
            g_vrMenuBackHeld;
    }

    // KISAK_SP_VR_CENTERED_SCRIPT_MODAL_V75
    // These dialogs still need one-pass shared painting to prevent duplicate
    // stereo copies. V82/V88 author every menu, including shared modals, in
    // the same one-eye coordinate space for rendering and hit testing.
    const bool centeredModalMenu =
        VR_IsCenteredMonoscopicMenuActive();

    const bool activeGameplayMenu =
        clientUIActives[0].connectionState ==
            CA_ACTIVE &&
        !centeredModalMenu;

    // KISAK_SP_VR_EYE_LOCAL_SHARED_MODAL_V88
    // V82's ScreenPlacement is now authoritative for every UI menu. The
    // centered-modal classifier controls command-list ownership only; it no
    // longer changes cursor coordinates back to the packed two-eye canvas.
    const bool eyeLocalMenu =
        true;

    const std::uint32_t capturedWidth =
        g_vrCapturedStereoWidth > 0u
            ? VR_GetCapturedMainStereoWidth()
            : 640u;

    const std::uint32_t cursorWidth =
        eyeLocalMenu &&
        capturedWidth >= 2u
            ? capturedWidth / 2u
            : capturedWidth;

    const std::uint32_t cursorHeight =
        g_vrCapturedStereoHeight > 0u
            ? g_vrCapturedStereoHeight
            : 480u;

    static bool loggedEyeLocalMenuCursor = false;

    if (eyeLocalMenu &&
        !loggedEyeLocalMenuCursor)
    {
        Com_Printf(
            0,
            "[VR][UI] V83 routes ordinary frontend/pause cursor "
            "coordinates through one-eye ScreenPlacement space.\n");

        loggedEyeLocalMenuCursor = true;
    }

    static bool loggedCenteredModalCursor = false;

    if (centeredModalMenu &&
        !loggedCenteredModalCursor)
    {
        const char* topMenuName =
            UI_GetTopActiveMenuName(0);

        Com_Printf(
            0,
            "[VR][UI] V88 shared modal cursor uses the one-eye "
            "ScreenPlacement (top menu '%s').\n",
            topMenuName != nullptr
                ? topMenuName
                : "unknown");

        loggedCenteredModalCursor = true;
    }

    const bool cursorCoordinateModeChanged =
        menuWasActive &&
        eyeLocalMenu !=
            eyeLocalMenuWasActive;

    if (!menuWasActive ||
        cursorCoordinateModeChanged)
    {
        cursorX =
            (activeGameplayMenu
                ? 0.25f
                : 0.5f) *
            static_cast<float>(
                cursorWidth);

        cursorY =
            (activeGameplayMenu
                ? 0.26f
                : 0.5f) *
            static_cast<float>(
                cursorHeight);

        UI_MouseEvent(
            0,
            static_cast<int>(cursorX),
            static_cast<int>(cursorY));

        menuWasActive = true;
        lastUpdateTime = currentTime;
    }

    eyeLocalMenuWasActive =
        eyeLocalMenu;

    std::uint32_t elapsedMilliseconds =
        currentTime - lastUpdateTime;

    if (elapsedMilliseconds > 50u)
    {
        elapsedMilliseconds = 50u;
    }

    lastUpdateTime = currentTime;

    constexpr float deadzone = 0.20f;
    constexpr float cursorSpeedPixelsPerSecond =
        1100.0f;

    bool cursorMoved = false;

    if (stickValid)
    {
        if (std::fabs(stickX) < deadzone)
        {
            stickX = 0.0f;
        }

        if (std::fabs(stickY) < deadzone)
        {
            stickY = 0.0f;
        }

        if (stickX != 0.0f ||
            stickY != 0.0f)
        {
            const float elapsedSeconds =
                static_cast<float>(
                    elapsedMilliseconds) /
                1000.0f;

            cursorX +=
                stickX *
                cursorSpeedPixelsPerSecond *
                elapsedSeconds;

            // OpenXR stick +Y is up; desktop cursor +Y is down.
            cursorY -=
                stickY *
                cursorSpeedPixelsPerSecond *
                elapsedSeconds;

            const float maximumX =
                static_cast<float>(
                    cursorWidth - 1u);

            const float maximumY =
                static_cast<float>(
                    cursorHeight - 1u);

            if (cursorX < 0.0f)
            {
                cursorX = 0.0f;
            }
            else if (cursorX > maximumX)
            {
                cursorX = maximumX;
            }

            if (cursorY < 0.0f)
            {
                cursorY = 0.0f;
            }
            else if (cursorY > maximumY)
            {
                cursorY = maximumY;
            }

            cursorMoved = true;
        }
    }

    if (cursorMoved)
    {
        UI_MouseEvent(
            0,
            static_cast<int>(cursorX),
            static_cast<int>(cursorY));
    }

    if (confirmHeld &&
        !confirmWasHeld)
    {
        // K_MOUSE1. The normal UI path activates the item beneath
        // the virtual cursor and preserves menu scripts and sounds.
        VR_SendMenuKeyTap(
            200);
    }

    if (backHeld &&
        !backWasHeld)
    {
        VR_SendMenuKeyTap(
            27);
    }

    confirmWasHeld =
        confirmHeld;

    backWasHeld =
        backHeld;

    if (!loggedMenuCursor)
    {
        Com_Printf(
            0,
            "[VR] Enabled configurable VR menu cursor, confirm, "
            "and back actions.\n");

        loggedMenuCursor = true;
    }
}

bool VR_RenderSolidColorFrame(
    const XrFrameState& frameState,
    XrCompositionLayerProjection& projectionLayer,
    std::vector<XrCompositionLayerProjectionView>&
        projectionViews)
{
    XrViewLocateInfo viewLocateInfo{
        XR_TYPE_VIEW_LOCATE_INFO
    };

    viewLocateInfo.viewConfigurationType =
        kViewConfiguration;
    viewLocateInfo.displayTime =
        frameState.predictedDisplayTime;
    viewLocateInfo.space = g_vrAppSpace;

    XrViewState viewState{XR_TYPE_VIEW_STATE};

    uint32_t locatedViewCount = 0;

    XrResult result =
        xrLocateViews(
            g_vrSession,
            &viewLocateInfo,
            &viewState,
            static_cast<uint32_t>(g_vrViews.size()),
            &locatedViewCount,
            g_vrViews.data());

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrLocateViews", result);
        return false;
    }

    const XrViewStateFlags requiredFlags =
        XR_VIEW_STATE_ORIENTATION_VALID_BIT |
        XR_VIEW_STATE_POSITION_VALID_BIT;

    if ((viewState.viewStateFlags & requiredFlags) !=
        requiredFlags)
    {
        return false;
    }

    if (locatedViewCount != g_vrEyeSwapchains.size())
    {
        Com_PrintWarning(
            0,
            "[VR] xrLocateViews returned %u views; expected %u.\n",
            locatedViewCount,
            static_cast<unsigned int>(
                g_vrEyeSwapchains.size()));

        return false;
    }

    VR_UpdateControllerActions(
        frameState.predictedDisplayTime);

    VR_UpdateMenuControllerNavigation();

    projectionViews.resize(locatedViewCount);

    if (locatedViewCount >= kVrStereoEyeCount)
    {
        std::array<
            VrEyeProjectionTangents,
            kVrStereoEyeCount>
            publishedTangents = {};

        bool projectionValid = true;

        for (std::uint32_t eyeIndex = 0u;
             eyeIndex < kVrStereoEyeCount;
             ++eyeIndex)
        {
            const XrFovf& fov =
                g_vrViews[eyeIndex].fov;

            VrEyeProjectionTangents& tangents =
                publishedTangents[eyeIndex];

            tangents.left =
                std::tan(fov.angleLeft);

            tangents.right =
                std::tan(fov.angleRight);

            tangents.down =
                std::tan(fov.angleDown);

            tangents.up =
                std::tan(fov.angleUp);

            projectionValid =
                projectionValid &&
                tangents.left < tangents.right &&
                tangents.down < tangents.up &&
                tangents.left < 0.0f &&
                tangents.right > 0.0f &&
                tangents.down < 0.0f &&
                tangents.up > 0.0f;
        }

        {
            std::lock_guard<std::mutex> lock(
                g_vrProjectionMutex);

            if (projectionValid)
            {
                g_vrEyeProjectionTangents =
                    publishedTangents;

                g_vrEyeProjectionValid = true;
            }
        }

        if (projectionValid &&
            !g_vrLoggedProjectionPublish)
        {
            Com_Printf(
                0,
                "[VR] Published OpenXR per-eye "
                "projection tangents: "
                "L %.4f %.4f %.4f %.4f, "
                "R %.4f %.4f %.4f %.4f.\n",
                publishedTangents[0].left,
                publishedTangents[0].right,
                publishedTangents[0].down,
                publishedTangents[0].up,
                publishedTangents[1].left,
                publishedTangents[1].right,
                publishedTangents[1].down,
                publishedTangents[1].up);

            g_vrLoggedProjectionPublish = true;
        }
    }

    VR_UpdateCapturedStereoTexture();

    std::array<XrView, kVrStereoEyeCount>
        submissionViews = {};

    bool submissionViewsValid = false;

    if (g_vrCapturedStereoViewsValid)
    {
        submissionViews =
            g_vrCapturedStereoViews;

        submissionViewsValid =
            true;
    }
    else if (locatedViewCount >=
             kVrStereoEyeCount)
    {
        for (std::uint32_t eyeIndex = 0u;
             eyeIndex < kVrStereoEyeCount;
             ++eyeIndex)
        {
            submissionViews[eyeIndex] =
                g_vrViews[eyeIndex];
        }

        submissionViewsValid =
            true;
    }

    if (locatedViewCount > 0)
    {
        VR_PublishHeadOrientation(
            g_vrViews[0].pose.orientation);

        VR_ProcessCalibrationRequest(
            frameState.predictedDisplayTime);
        VR_ProcessHudEditorRequest();
        VR_ProcessWeaponCalibrationRequest();
    }

    if (locatedViewCount >=
        kVrStereoEyeCount)
    {
        std::lock_guard<std::mutex> lock(
            g_vrPublishedRenderViewsMutex);

        for (std::uint32_t eyeIndex = 0u;
             eyeIndex < kVrStereoEyeCount;
             ++eyeIndex)
        {
            g_vrPublishedRenderViews[eyeIndex] =
                g_vrViews[eyeIndex];
        }

        g_vrPublishedRenderViewsValid =
            true;

        g_vrPublishedRenderPoseNanoseconds =
            VR_OpenXrClockNanoseconds();
    }

    constexpr float clearColor[4] = {
        0.03f,
        0.08f,
        0.20f,
        1.0f,
    };

    const bool menuComfortMode =
        Key_IsCatcherActive(
            0,
            0x10);

    // KISAK_SP_VR_EYE_LOCAL_SHARED_MODAL_V88
    // Centered dialogs are still painted once in the shared list, but V82
    // authors that list in the left eye. The dedicated modal buffer therefore
    // samples the same left-eye region as frontend UI instead of 25%-75%.
    const bool centeredModalComfortMode =
        menuComfortMode &&
        VR_IsCenteredMonoscopicMenuActive();

    const bool activePauseComfortMode =
        menuComfortMode &&
        !centeredModalComfortMode &&
        clientUIActives[0].connectionState ==
            CA_ACTIVE;

    XrPosef menuComfortPose = {};
    XrFovf menuComfortFov = {};
    bool menuComfortProjectionValid = false;

    if (menuComfortMode &&
        locatedViewCount >=
            kVrStereoEyeCount)
    {
        std::array<
            XrView,
            kVrStereoEyeCount>
            menuSourceViews = {};

        if (submissionViewsValid)
        {
            menuSourceViews =
                submissionViews;
        }
        else if (g_vrPublishedRenderViewsValid)
        {
            menuSourceViews =
                g_vrPublishedRenderViews;
        }
        else
        {
            for (std::uint32_t eyeIndex = 0u;
                 eyeIndex < kVrStereoEyeCount;
                 ++eyeIndex)
            {
                menuSourceViews[eyeIndex] =
                    g_vrViews[eyeIndex];
            }
        }

        menuComfortPose =
            menuSourceViews[0].pose;

        menuComfortPose.position.x =
            0.5f *
            (menuSourceViews[0].pose.position.x +
             menuSourceViews[1].pose.position.x);

        menuComfortPose.position.y =
            0.5f *
            (menuSourceViews[0].pose.position.y +
             menuSourceViews[1].pose.position.y);

        menuComfortPose.position.z =
            0.5f *
            (menuSourceViews[0].pose.position.z +
             menuSourceViews[1].pose.position.z);

        // Give both eyes exactly the same centered optical frustum.
        // This creates zero binocular disparity for the copied 2D menu
        // and also keeps it smaller than the full gameplay FOV.
        menuComfortFov.angleLeft =
            -0.62f;

        menuComfortFov.angleRight =
            0.62f;

        menuComfortFov.angleUp =
            0.38f;

        menuComfortFov.angleDown =
            -0.38f;

        menuComfortProjectionValid =
            true;
    }

    if (menuComfortMode &&
        !g_vrLoggedMenuComfortScreen)
    {
        Com_Printf(
            0,
            "[VR] Presented the active menu as a monoscopic "
            "comfort screen.\n");

        g_vrLoggedMenuComfortScreen = true;
    }

    static bool loggedActivePauseCrop = false;

    if (activePauseComfortMode &&
        !loggedActivePauseCrop)
    {
        Com_Printf(
            0,
            "[VR] Cropped the active pause menu from the right "
            "stereo eye and presented it to both eyes.\n");

        loggedActivePauseCrop = true;
    }

    static bool loggedCenteredModalCrop = false;

    if (centeredModalComfortMode &&
        !loggedCenteredModalCrop)
    {
        const char* topMenuName =
            UI_GetTopActiveMenuName(0);

        Com_Printf(
            0,
            "[VR][UI] V88 shared modal mono: retained "
            "one-pass painting and sampled the authored "
            "left-eye region for "
            "both eyes (top menu '%s').\n",
            topMenuName != nullptr
                ? topMenuName
                : "unknown");

        loggedCenteredModalCrop = true;
    }

    static bool loggedConvergedMenuProjection = false;

    if (menuComfortProjectionValid &&
        !loggedConvergedMenuProjection)
    {
        Com_Printf(
            0,
            "[VR] Applied converged monoscopic menu projection "
            "to both eyes.\n");

        loggedConvergedMenuProjection = true;
    }

    for (uint32_t eyeIndex = 0;
         eyeIndex < locatedViewCount;
         ++eyeIndex)
    {
        VrEyeSwapchain& eyeSwapchain =
            g_vrEyeSwapchains[eyeIndex];

        uint32_t imageIndex = 0;

        XrSwapchainImageAcquireInfo acquireInfo{
            XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO
        };

        result =
            xrAcquireSwapchainImage(
                eyeSwapchain.handle,
                &acquireInfo,
                &imageIndex);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrAcquireSwapchainImage",
                result);

            return false;
        }

        XrSwapchainImageWaitInfo waitInfo{
            XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO
        };

        waitInfo.timeout = XR_INFINITE_DURATION;

        result =
            xrWaitSwapchainImage(
                eyeSwapchain.handle,
                &waitInfo);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrWaitSwapchainImage",
                result);

            XrSwapchainImageReleaseInfo releaseInfo{
                XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO
            };

            xrReleaseSwapchainImage(
                eyeSwapchain.handle,
                &releaseInfo);

            return false;
        }

        ID3D11RenderTargetView* renderTarget =
            eyeSwapchain
                .renderTargetViews[imageIndex]
                .Get();

        g_vrD3dContext->OMSetRenderTargets(
            1,
            &renderTarget,
            nullptr);

        g_vrD3dContext->ClearRenderTargetView(
            renderTarget,
            clearColor);

        D3D11_VIEWPORT viewport = {};
        viewport.Width =
            static_cast<float>(eyeSwapchain.width);
        viewport.Height =
            static_cast<float>(eyeSwapchain.height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        if (menuComfortMode &&
            g_vrCapturedStereoWidth > 0u &&
            g_vrCapturedStereoHeight > 0u)
        {
            // Every V82/V88-authored menu source occupies one eye. Shared
            // modals and frontend menus use the completed left eye; ordinary
            // active pause menus use the completed right eye.
            // V83_EYE_LOCAL_MENU_SOURCE_OPENXR
            const float sourceAspect =
                kVrCanonicalMenuAspect;

            if (!g_vrLoggedCanonicalMenuAspect)
            {
                Com_Printf(
                    0,
                    "[VR][UI][V101] Restored the canonical 4:3 menu "
                    "presentation in both eyes.\n");

                g_vrLoggedCanonicalMenuAspect = true;
            }

            const float targetAspect =
                static_cast<float>(
                    eyeSwapchain.width) /
                static_cast<float>(
                    eyeSwapchain.height);

            if (sourceAspect > targetAspect)
            {
                viewport.Height =
                    viewport.Width /
                    sourceAspect;

                viewport.TopLeftY =
                    0.5f *
                    (static_cast<float>(
                         eyeSwapchain.height) -
                     viewport.Height);
            }
            else
            {
                viewport.Width =
                    viewport.Height *
                    sourceAspect;

                viewport.TopLeftX =
                    0.5f *
                    (static_cast<float>(
                         eyeSwapchain.width) -
                     viewport.Width);
            }
        }

        if (!menuComfortMode &&
            submissionViewsValid &&
            eyeIndex < submissionViews.size())
        {
            VR_ConfigureHudConvergedCaptureViewport(
                submissionViews[eyeIndex].fov,
                eyeSwapchain.width,
                eyeSwapchain.height,
                &viewport);
        }

        // KISAK_SP_VR_FIXED_SCOPE_BINOCULAR_RETICLE_FIX_V2
        // The constant buffer must be refreshed after this eye's asymmetric
        // viewport is known and before either the direct or FSR blit.
        VR_UpdateCompositorConstantsForEye(
            viewport,
            eyeSwapchain.width,
            eyeSwapchain.height);

        g_vrD3dContext->RSSetViewports(1, &viewport);
        g_vrD3dContext->RSSetState(
            g_vrTestRasterizerState.Get());

        g_vrD3dContext->OMSetDepthStencilState(
            g_vrTestDepthStencilState.Get(),
            0);

        ID3D11ShaderResourceView* capturedView =
            g_vrCapturedStereoView.Get();

        bool fsrRendered = false;

        if (capturedView != nullptr &&
            !menuComfortMode)
        {
            fsrRendered =
                VR_RenderFsrUpscaledEye(
                    eyeIndex,
                    eyeSwapchain.width,
                    eyeSwapchain.height,
                    capturedView,
                    renderTarget,
                    viewport);
        }

        if (capturedView != nullptr &&
            !fsrRendered)
        {
            g_vrD3dContext->OMSetRenderTargets(
                1,
                &renderTarget,
                nullptr);

            g_vrD3dContext->OMSetDepthStencilState(
                nullptr,
                0);

            g_vrD3dContext->IASetInputLayout(
                g_vrBlitInputLayout.Get());

            const UINT stride =
                sizeof(VrBlitVertex);

            const UINT offset = 0;

            ID3D11Buffer* vertexBuffer =
                centeredModalComfortMode
                    ? g_vrCenteredModalBlitVertexBuffer.Get()
                    : activePauseComfortMode
                    ? g_vrPauseMenuBlitVertexBuffer.Get()
                    : menuComfortMode
                        ? g_vrMenuBlitVertexBuffer.Get()
                        : (
                            eyeIndex <
                                g_vrBlitVertexBuffers.size()
                                ? g_vrBlitVertexBuffers[eyeIndex]
                                    .Get()
                                : nullptr);

            g_vrD3dContext->IASetVertexBuffers(
                0,
                1,
                &vertexBuffer,
                &stride,
                &offset);

            g_vrD3dContext->IASetIndexBuffer(
                nullptr,
                DXGI_FORMAT_UNKNOWN,
                0);

            g_vrD3dContext->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

            g_vrD3dContext->VSSetShader(
                g_vrBlitVertexShader.Get(),
                nullptr,
                0);

            g_vrD3dContext->PSSetShader(
                g_vrBlitPixelShader.Get(),
                nullptr,
                0);

            ID3D11Buffer* compositorConstantBuffer =
                g_vrCompositorConstantBuffer.Get();

            g_vrD3dContext->PSSetConstantBuffers(
                1,
                1,
                &compositorConstantBuffer);

            g_vrD3dContext->PSSetShaderResources(
                0,
                1,
                &capturedView);

            ID3D11SamplerState* sampler =
                g_vrBlitSampler.Get();

            g_vrD3dContext->PSSetSamplers(
                0,
                1,
                &sampler);

            g_vrD3dContext->Draw(4, 0);

            ID3D11ShaderResourceView* nullView = nullptr;

            g_vrD3dContext->PSSetShaderResources(
                0,
                1,
                &nullView);

            // The captured frame used an oversized/offset viewport for the
            // symmetric-to-asymmetric projection mapping.  Direct OpenXR
            // overlays already project in destination-eye space.
            VR_SetFullEyeViewport(
                eyeSwapchain.width,
                eyeSwapchain.height);

            if (!menuComfortMode &&
                submissionViewsValid &&
                eyeIndex < submissionViews.size())
            {
                VR_RenderPhysicalSniperScope(
                    eyeIndex,
                    eyeSwapchain.width,
                    eyeSwapchain.height,
                    submissionViews[eyeIndex]);
            }
        }

        if (capturedView != nullptr &&
            fsrRendered)
        {
            VR_SetFullEyeViewport(
                eyeSwapchain.width,
                eyeSwapchain.height);

            if (submissionViewsValid &&
                eyeIndex < submissionViews.size())
            {
                VR_RenderPhysicalSniperScope(
                    eyeIndex,
                    eyeSwapchain.width,
                    eyeSwapchain.height,
                    submissionViews[eyeIndex]);
            }
        }

        if (!menuComfortMode)
        {
            // Also restore the direct-eye viewport when no captured texture
            // was available for this frame.
            VR_SetFullEyeViewport(
                eyeSwapchain.width,
                eyeSwapchain.height);

            // KISAK_SP_VR_TRACKED_HANDS_V28_REMOVE_CONTROLLER_AXIS_OVERLAY
            // V22's compositor-space origin/axis proxy was temporary
            // calibration instrumentation.  The tracked-hand diagnostics
            // remain available, but no colored proxy geometry is submitted.
            static bool loggedControllerAxisOverlayRemoval = false;

            if (!loggedControllerAxisOverlayRemoval)
            {
                Com_Printf(
                    0,
                    "[VR][HANDS] V28 removed the colored controller origin/axis overlay while preserving V27 hand tracking and grip behavior.\n");

                loggedControllerAxisOverlayRemoval = true;
            }
        }

        g_vrD3dContext->Flush();

        XrSwapchainImageReleaseInfo releaseInfo{
            XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO
        };

        result =
            xrReleaseSwapchainImage(
                eyeSwapchain.handle,
                &releaseInfo);

        if (XR_FAILED(result))
        {
            VR_LogXrFailure(
                "xrReleaseSwapchainImage",
                result);

            return false;
        }

        XrCompositionLayerProjectionView& projectionView =
            projectionViews[eyeIndex];

        projectionView =
            XrCompositionLayerProjectionView{
                XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW
            };

        if (menuComfortProjectionValid)
        {
            projectionView.pose =
                menuComfortPose;

            projectionView.fov =
                menuComfortFov;
        }
        else if (submissionViewsValid &&
                 eyeIndex < kVrStereoEyeCount)
        {
            projectionView.pose =
                submissionViews[eyeIndex].pose;

            projectionView.fov =
                submissionViews[eyeIndex].fov;
        }
        else
        {
            projectionView.pose =
                g_vrViews[eyeIndex].pose;

            projectionView.fov =
                g_vrViews[eyeIndex].fov;
        }

        projectionView.subImage.swapchain =
            eyeSwapchain.handle;

        projectionView.subImage.imageRect.offset = {0, 0};

        projectionView.subImage.imageRect.extent = {
            eyeSwapchain.width,
            eyeSwapchain.height,
        };

        projectionView.subImage.imageArrayIndex = 0;
    }

    projectionLayer =
        XrCompositionLayerProjection{
            XR_TYPE_COMPOSITION_LAYER_PROJECTION
        };

    projectionLayer.space = g_vrAppSpace;
    projectionLayer.viewCount =
        static_cast<uint32_t>(projectionViews.size());
    projectionLayer.views = projectionViews.data();

    if (!g_vrLoggedFirstTestFrame)
    {
        Com_Printf(
            0,
            "[VR] Submitted first OpenXR frame during D3D9 bridge.\n");

        g_vrLoggedFirstTestFrame = true;
    }

    return true;
}

// KISAK_SP_VR_OPENVR_MENU_OPTICAL_CENTER_V76
// OpenXR can submit the same centered pose/FOV for both eyes while the
// monoscopic menu is active. OpenVR owns its per-eye projection at Submit(),
// so place the copied menu into an equal tangent-space rectangle instead.
// This keeps every menu pixel on the same visual ray even when a runtime such
// as AVP/ALVR reports strongly asymmetric left/right optical centers.
bool VR_ConfigureOpenVrMenuComfortViewport(
    const std::uint32_t eyeIndex,
    const float sourceAspect,
    const int32_t targetWidth,
    const int32_t targetHeight,
    D3D11_VIEWPORT* viewport)
{
    if (eyeIndex >= kVrStereoEyeCount ||
        !std::isfinite(sourceAspect) ||
        sourceAspect <= 0.0f ||
        targetWidth <= 0 ||
        targetHeight <= 0 ||
        viewport == nullptr)
    {
        return false;
    }

    std::array<
        VrEyeProjectionTangents,
        kVrStereoEyeCount>
        projections = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrProjectionMutex);

        if (!g_vrEyeProjectionValid)
        {
            return false;
        }

        projections =
            g_vrEyeProjectionTangents;
    }

    float commonHorizontal =
        (std::min)(
            -projections[0].left,
            projections[0].right);

    float commonVertical =
        (std::min)(
            -projections[0].down,
            projections[0].up);

    for (std::uint32_t projectionIndex = 0u;
         projectionIndex < kVrStereoEyeCount;
         ++projectionIndex)
    {
        const VrEyeProjectionTangents& projection =
            projections[projectionIndex];

        const float horizontalSpan =
            projection.right - projection.left;

        const float verticalSpan =
            projection.up - projection.down;

        if (!std::isfinite(projection.left) ||
            !std::isfinite(projection.right) ||
            !std::isfinite(projection.down) ||
            !std::isfinite(projection.up) ||
            horizontalSpan <= 0.0f ||
            verticalSpan <= 0.0f ||
            projection.left >= 0.0f ||
            projection.right <= 0.0f ||
            projection.down >= 0.0f ||
            projection.up <= 0.0f)
        {
            return false;
        }

        commonHorizontal =
            (std::min)(
                commonHorizontal,
                (std::min)(
                    -projection.left,
                    projection.right));

        commonVertical =
            (std::min)(
                commonVertical,
                (std::min)(
                    -projection.down,
                    projection.up));
    }

    constexpr float projectionSafetyMargin =
        0.90f;

    const float maximumHorizontal =
        projectionSafetyMargin *
        commonHorizontal;

    const float maximumVertical =
        projectionSafetyMargin *
        commonVertical;

    // Match the centered OpenXR comfort projection whenever the runtime has
    // room for it, then shrink uniformly in tangent space on narrower HMDs.
    float halfTangentX =
        (std::min)(
            std::tan(0.62f),
            maximumHorizontal);

    const float requestedMaximumY =
        (std::min)(
            std::tan(0.38f),
            maximumVertical);

    float halfTangentY =
        halfTangentX /
        sourceAspect;

    if (halfTangentY >
        requestedMaximumY)
    {
        halfTangentY =
            requestedMaximumY;

        halfTangentX =
            halfTangentY *
            sourceAspect;
    }

    if (!std::isfinite(halfTangentX) ||
        !std::isfinite(halfTangentY) ||
        halfTangentX <= 0.0f ||
        halfTangentY <= 0.0f ||
        halfTangentX > maximumHorizontal ||
        halfTangentY > maximumVertical)
    {
        return false;
    }

    const VrEyeProjectionTangents& projection =
        projections[eyeIndex];

    const float horizontalSpan =
        projection.right - projection.left;

    const float verticalSpan =
        projection.up - projection.down;

    const float leftUv =
        (-halfTangentX - projection.left) /
        horizontalSpan;

    const float rightUv =
        (halfTangentX - projection.left) /
        horizontalSpan;

    const float topUv =
        (projection.up - halfTangentY) /
        verticalSpan;

    const float bottomUv =
        (projection.up + halfTangentY) /
        verticalSpan;

    if (leftUv < 0.0f ||
        rightUv > 1.0f ||
        topUv < 0.0f ||
        bottomUv > 1.0f ||
        leftUv >= rightUv ||
        topUv >= bottomUv)
    {
        return false;
    }

    viewport->TopLeftX =
        leftUv *
        static_cast<float>(targetWidth);

    viewport->TopLeftY =
        topUv *
        static_cast<float>(targetHeight);

    viewport->Width =
        (rightUv - leftUv) *
        static_cast<float>(targetWidth);

    viewport->Height =
        (bottomUv - topUv) *
        static_cast<float>(targetHeight);

    viewport->MinDepth = 0.0f;
    viewport->MaxDepth = 1.0f;

    static std::array<bool, kVrStereoEyeCount>
        loggedViewport = {};

    if (!loggedViewport[eyeIndex])
    {
        Com_Printf(
            0,
            "[VR][OPENVR][MENU] V76 optical-center comfort "
            "viewport eye %u: center %.4f %.4f, size %.4f %.4f, "
            "half tangents %.4f %.4f.\n",
            eyeIndex,
            0.5f * (leftUv + rightUv),
            0.5f * (topUv + bottomUv),
            rightUv - leftUv,
            bottomUv - topUv,
            halfTangentX,
            halfTangentY);

        loggedViewport[eyeIndex] = true;
    }

    return true;
}

bool VR_RenderOpenVrEye(
    const std::uint32_t eyeIndex,
    const std::array<XrView, kVrStereoEyeCount>&
        submissionViews,
    const bool submissionViewsValid,
    const bool menuComfortMode,
    const bool centeredModalComfortMode,
    const bool activePauseComfortMode)
{
    if (eyeIndex >= kVrStereoEyeCount ||
        g_vrD3dContext == nullptr)
    {
        return false;
    }

    VrOpenVrEyeTarget& eyeTarget =
        g_vrOpenVrEyeTargets[eyeIndex];

    ID3D11RenderTargetView* renderTarget =
        eyeTarget.renderTargetView.Get();

    if (renderTarget == nullptr ||
        eyeTarget.texture == nullptr ||
        eyeTarget.width <= 0 ||
        eyeTarget.height <= 0)
    {
        return false;
    }

    constexpr float clearColor[4] = {
        0.03f,
        0.08f,
        0.20f,
        1.0f,
    };

    g_vrD3dContext->OMSetRenderTargets(
        1,
        &renderTarget,
        nullptr);

    g_vrD3dContext->ClearRenderTargetView(
        renderTarget,
        clearColor);

    D3D11_VIEWPORT viewport = {};
    viewport.Width =
        static_cast<float>(eyeTarget.width);
    viewport.Height =
        static_cast<float>(eyeTarget.height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    if (menuComfortMode &&
        g_vrCapturedStereoWidth > 0u &&
        g_vrCapturedStereoHeight > 0u)
    {
        // V83_EYE_LOCAL_MENU_SOURCE_OPENVR
        const float sourceAspect =
            kVrCanonicalMenuAspect;

        if (!g_vrLoggedCanonicalMenuAspect)
        {
            Com_Printf(
                0,
                "[VR][UI][V101] Restored the canonical 4:3 menu "
                "presentation in both eyes.\n");

            g_vrLoggedCanonicalMenuAspect = true;
        }

        const bool opticalCenterViewport =
            VR_ConfigureOpenVrMenuComfortViewport(
                eyeIndex,
                sourceAspect,
                eyeTarget.width,
                eyeTarget.height,
                &viewport);

        // Retain the beta.11 centered-pixel fallback if SteamVR ever fails to
        // publish usable projection tangents. Normal OpenVR initialization
        // publishes them before the first frame.
        if (!opticalCenterViewport)
        {
            const float targetAspect =
                static_cast<float>(eyeTarget.width) /
                static_cast<float>(eyeTarget.height);

            if (sourceAspect > targetAspect)
            {
                viewport.Height =
                    viewport.Width /
                    sourceAspect;

                viewport.TopLeftY =
                    0.5f *
                    (static_cast<float>(
                         eyeTarget.height) -
                     viewport.Height);
            }
            else
            {
                viewport.Width =
                    viewport.Height *
                    sourceAspect;

                viewport.TopLeftX =
                    0.5f *
                    (static_cast<float>(
                         eyeTarget.width) -
                     viewport.Width);
            }
        }
    }

    if (!menuComfortMode &&
        submissionViewsValid)
    {
        VR_ConfigureHudConvergedCaptureViewport(
            submissionViews[eyeIndex].fov,
            eyeTarget.width,
            eyeTarget.height,
            &viewport);
    }

    VR_UpdateCompositorConstantsForEye(
        viewport,
        eyeTarget.width,
        eyeTarget.height);

    g_vrD3dContext->RSSetViewports(
        1,
        &viewport);

    g_vrD3dContext->RSSetState(
        g_vrTestRasterizerState.Get());

    g_vrD3dContext->OMSetDepthStencilState(
        g_vrTestDepthStencilState.Get(),
        0);

    ID3D11ShaderResourceView* capturedView =
        g_vrCapturedStereoView.Get();

    bool fsrRendered = false;

    if (capturedView != nullptr &&
        !menuComfortMode)
    {
        fsrRendered =
            VR_RenderFsrUpscaledEye(
                eyeIndex,
                eyeTarget.width,
                eyeTarget.height,
                capturedView,
                renderTarget,
                viewport);
    }

    if (capturedView != nullptr &&
        !fsrRendered)
    {
        g_vrD3dContext->OMSetRenderTargets(
            1,
            &renderTarget,
            nullptr);

        g_vrD3dContext->OMSetDepthStencilState(
            nullptr,
            0);

        g_vrD3dContext->IASetInputLayout(
            g_vrBlitInputLayout.Get());

        const UINT stride =
            sizeof(VrBlitVertex);

        const UINT offset = 0u;

        ID3D11Buffer* vertexBuffer =
            centeredModalComfortMode
                ? g_vrCenteredModalBlitVertexBuffer.Get()
                : activePauseComfortMode
                    ? g_vrPauseMenuBlitVertexBuffer.Get()
                    : menuComfortMode
                        ? g_vrMenuBlitVertexBuffer.Get()
                        : g_vrBlitVertexBuffers[eyeIndex]
                              .Get();

        g_vrD3dContext->IASetVertexBuffers(
            0,
            1,
            &vertexBuffer,
            &stride,
            &offset);

        g_vrD3dContext->IASetIndexBuffer(
            nullptr,
            DXGI_FORMAT_UNKNOWN,
            0);

        g_vrD3dContext->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        g_vrD3dContext->VSSetShader(
            g_vrBlitVertexShader.Get(),
            nullptr,
            0);

        g_vrD3dContext->PSSetShader(
            g_vrBlitPixelShader.Get(),
            nullptr,
            0);

        ID3D11Buffer* compositorConstantBuffer =
            g_vrCompositorConstantBuffer.Get();

        g_vrD3dContext->PSSetConstantBuffers(
            1,
            1,
            &compositorConstantBuffer);

        g_vrD3dContext->PSSetShaderResources(
            0,
            1,
            &capturedView);

        ID3D11SamplerState* sampler =
            g_vrBlitSampler.Get();

        g_vrD3dContext->PSSetSamplers(
            0,
            1,
            &sampler);

        g_vrD3dContext->Draw(4, 0);

        ID3D11ShaderResourceView* nullView =
            nullptr;

        g_vrD3dContext->PSSetShaderResources(
            0,
            1,
            &nullView);

        VR_SetFullEyeViewport(
            eyeTarget.width,
            eyeTarget.height);

        if (!menuComfortMode &&
            submissionViewsValid)
        {
            VR_RenderPhysicalSniperScope(
                eyeIndex,
                eyeTarget.width,
                eyeTarget.height,
                submissionViews[eyeIndex]);
        }
    }

    if (capturedView != nullptr &&
        fsrRendered)
    {
        VR_SetFullEyeViewport(
            eyeTarget.width,
            eyeTarget.height);

        if (submissionViewsValid)
        {
            VR_RenderPhysicalSniperScope(
                eyeIndex,
                eyeTarget.width,
                eyeTarget.height,
                submissionViews[eyeIndex]);
        }
    }

    if (!menuComfortMode)
    {
        VR_SetFullEyeViewport(
            eyeTarget.width,
            eyeTarget.height);
    }

    g_vrD3dContext->Flush();
    return true;
}

void VR_ResetState()
{
    g_vrRuntimeBackend =
        VrRuntimeBackend::None;
    g_vrInstance = XR_NULL_HANDLE;
    g_vrSystemId = XR_NULL_SYSTEM_ID;
    g_vrSession = XR_NULL_HANDLE;
    g_vrAppSpace = XR_NULL_HANDLE;
    g_vrCalibrationFloorSpace = XR_NULL_HANDLE;
    g_vrCalibrationFloorSpaceAvailable = false;
    g_vrSessionState = XR_SESSION_STATE_UNKNOWN;
    g_vrBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    g_vrInitialized = false;
    g_vrSessionRunning = false;
    g_vrExitRequested = false;
    g_vrPalmPoseExtensionEnabled = false;
    g_vrEnabledOpenXrProfiles.fill(false);
    g_vrOpenVrInitialized = false;
    g_vrOpenVrRenderModels = nullptr;
    g_vrOpenVrLoggedFirstPose = false;
    g_vrOpenVrLoggedFirstSubmit = false;
    g_vrOpenVrHands = {};
    g_vrOpenVrLoggedController.fill(false);
    g_vrOpenVrMissionSelector = {};
    g_vrOpenVrLoggedMissionSelector = false;
    g_vrOpenXrMissionSelector = {};
    g_vrOpenXrLoggedMissionSelector = false;
    g_vrOpenVrControllerPoseComponents = {};
    g_vrNightVisionVisorGesture = {};
    g_vrLoggedNightVisionVisorGesture = false;
    g_vrCompatibilityRuntimeName.fill('\0');
    g_vrCompatibilityHeadsetName.fill('\0');
    for (auto& profile : g_vrCompatibilityControllerProfiles)
    {
        profile.fill('\0');
    }

    {
        std::lock_guard<std::mutex> lock(
            g_vrScopeStateMutex);

        g_vrScopeActive = false;
        g_vrScopeAdsFraction = 0.0f;
        g_vrScopeAdsFovDegrees = 65.0f;
        g_vrFixedScopedTurretActive = false;
        g_vrLoggedFixedScopedTurretCompositor = false;

        // KISAK_SP_VR_FIXED_SCOPED_TURRET_RUNTIME_V4
        g_vrFixedScopedTurretZoomFovDegrees = 20.0f;
        g_vrFixedScopedTurretMaximumZoomFovDegrees = 20.0f;
        g_vrLoggedFixedScopedTurretVisibleZoom = false;
        g_vrLoggedFixedScopedTurretFsrBypass = false;
    }

    KisakCrash_SetVrState(
        false,
        false,
        static_cast<int>(XR_SESSION_STATE_UNKNOWN),
        0u,
        0u,
        0u);
}
}


bool VR_IsQuitConfirmationMenuActive()
{
    if (!Key_IsCatcherActive(0, 0x10))
    {
        return false;
    }

    const char* topMenuName =
        UI_GetTopActiveMenuName(0);

    if (topMenuName == nullptr ||
        topMenuName[0] == '\0')
    {
        return false;
    }

    // KISAK_SP_VR_ACTIVE_MISSION_QUIT_CONFIRMATION_MONO_V47
    // The frontend confirmation is quit_popmenu, but Pause -> Quit during an
    // active SP mission does not reuse that asset.  Depending on save/profile
    // state, objective_info.menu opens one of these save-game warnings after
    // closing pausedmenu.  Their names contain neither "quit" nor
    // "leavegame", so V45/V46 kept painting them in both the stereo gameplay
    // list and the final shared UI list.  The right-eye pause crop therefore
    // contained two displaced copies of the dialog.
    const bool activeMissionQuitConfirmation =
        clientUIActives[0].connectionState == CA_ACTIVE &&
        (
            I_stricmp(topMenuName, "savegame_warning") == 0 ||
            I_stricmp(topMenuName, "savegame_warning_noprofile") == 0 ||
            I_stricmp(topMenuName, "savegame_warning_arcade") == 0
        );

    if (activeMissionQuitConfirmation)
    {
        static bool loggedActiveMissionQuitConfirmation = false;

        if (!loggedActiveMissionQuitConfirmation)
        {
            Com_Printf(
                0,
                "[VR][UI] V88 active-mission Pause -> Quit "
                "confirmation mono: detected '%s'; suppressing "
                "the stereo-list UI copy and using the shared "
                "eye-local source for both eyes.\n",
                topMenuName);

            loggedActiveMissionQuitConfirmation = true;
        }

        return true;
    }

    // Frontend menu packs commonly use quit_popmenu, while inherited MP
    // assets use popup_leavegame.  Preserve V45/V46 for those separate
    // confirmation dialogs without confusing them with the SP mission path.
    return
        I_stristr(topMenuName, "quit") != nullptr ||
        I_stristr(topMenuName, "leavegame") != nullptr;
}


bool VR_IsCenteredMonoscopicMenuActive()
{
    if (!Key_IsCatcherActive(0, 0x10))
    {
        return false;
    }

    const char* topMenuName =
        UI_GetTopActiveMenuName(0);

    if (topMenuName == nullptr ||
        topMenuName[0] == '\0')
    {
        return false;
    }

    // KISAK_SP_VR_FNG_DIFFICULTY_MODAL_V75
    // COD4's original F.N.G. flow opens select_difficulty as a script popup.
    // Selecting a non-recommended level can nest one of four diff_con_*
    // confirmations. All five retain one shared paint pass, now authored in
    // V82/V88's eye-local ScreenPlacement rather than the packed canvas.
    const bool fngDifficultyModal =
        I_stricmp(topMenuName, "select_difficulty") == 0 ||
        I_stricmp(topMenuName, "diff_con_easy") == 0 ||
        I_stricmp(topMenuName, "diff_con_regular") == 0 ||
        I_stricmp(topMenuName, "diff_con_hardened") == 0 ||
        I_stricmp(topMenuName, "diff_con_veteran") == 0;

    if (fngDifficultyModal)
    {
        static bool loggedFngDifficultyModal = false;

        if (!loggedFngDifficultyModal)
        {
            Com_Printf(
                0,
                "[VR][UI] V75 detected F.N.G. difficulty "
                "modal '%s'; suppressing stereo-list duplicates "
                "and using centered mono rendering/input.\n",
                topMenuName);

            loggedFngDifficultyModal = true;
        }

        return true;
    }

    return VR_IsQuitConfirmationMenuActive();
}


void VR_RecordRenderFramePose(
    const unsigned int renderFrameId)
{
    if (renderFrameId == 0u)
    {
        return;
    }

    std::array<XrView, kVrStereoEyeCount>
        renderViews = {};
    std::uint64_t recordedNanoseconds = 0u;

    {
        std::lock_guard<std::mutex> lock(
            g_vrPublishedRenderViewsMutex);

        if (!g_vrPublishedRenderViewsValid)
        {
            return;
        }

        renderViews = g_vrPublishedRenderViews;
        recordedNanoseconds =
            g_vrPublishedRenderPoseNanoseconds != 0u
                ? g_vrPublishedRenderPoseNanoseconds
                : VR_OpenXrClockNanoseconds();
    }

    std::lock_guard<std::mutex> lock(
        g_vrRenderPoseHistoryMutex);

    VrRenderPoseHistoryEntry& entry =
        g_vrRenderPoseHistory[
            g_vrRenderPoseHistoryWriteIndex];

    entry.valid = true;
    entry.renderFrameId = renderFrameId;
    entry.recordedNanoseconds = recordedNanoseconds;
    entry.views = renderViews;

    g_vrRenderPoseHistoryWriteIndex =
        (g_vrRenderPoseHistoryWriteIndex + 1u) %
        kVrRenderPoseHistoryCount;
}

bool VR_VerboseDiagnosticsEnabled()
{
    static const bool enabled = []()
    {
        const char* requested =
            std::getenv(
                "KISAK_VR_VERBOSE_DIAGNOSTICS");

        return
            requested != nullptr &&
            requested[0] == '1' &&
            requested[1] == '\0';
    }();

    return enabled;
}


bool VR_TrackedHandDiagnosticsEnabled()
{
    return VR_TrackedHandDiagnosticsEnabledInternal();
}



bool VR_GetRightControllerWeaponGripWorld(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float gripWorld[3])
{
    if (cameraOrigin == nullptr ||
        cameraAxis == nullptr ||
        gripWorld == nullptr)
    {
        return false;
    }

    float gripCameraLocal[3] = {};
    float calibrationCameraLocal[3] = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrRightControllerWeaponPoseValid)
        {
            return false;
        }

        gripCameraLocal[0] =
            g_vrRightControllerWeaponPosition[0];

        gripCameraLocal[1] =
            g_vrRightControllerWeaponPosition[1];

        gripCameraLocal[2] =
            g_vrRightControllerWeaponPosition[2];

        if (g_vrRightControllerWeaponCalibrationValid)
        {
            std::memcpy(
                calibrationCameraLocal,
                g_vrRightControllerWeaponCalibrationCameraLocal,
                sizeof(calibrationCameraLocal));
        }
    }

    kisak::vr::weapon_calibration::CalibratedGripTargetWorld(
        cameraOrigin,
        cameraAxis,
        gripCameraLocal,
        calibrationCameraLocal,
        gripWorld);

    return true;
}


void VR_ReportRightControllerWeaponGripAlignment(
    const char* const gripTagName,
    const float calibratedTargetWorld[3],
    const float alignedTagWorld[3])
{
    if (gripTagName == nullptr ||
        calibratedTargetWorld == nullptr ||
        alignedTagWorld == nullptr)
    {
        return;
    }

    float calibrationCameraLocal[3] = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrRightControllerWeaponCalibrationValid ||
            g_vrReportedRightControllerWeaponCalibration)
        {
            return;
        }

        std::memcpy(
            calibrationCameraLocal,
            g_vrRightControllerWeaponCalibrationCameraLocal,
            sizeof(calibrationCameraLocal));

        g_vrReportedRightControllerWeaponCalibration = true;
    }

    const float alignmentError[3] = {
        alignedTagWorld[0] - calibratedTargetWorld[0],
        alignedTagWorld[1] - calibratedTargetWorld[1],
        alignedTagWorld[2] - calibratedTargetWorld[2],
    };

    const float alignmentErrorLength =
        std::sqrt(
            alignmentError[0] * alignmentError[0] +
            alignmentError[1] * alignmentError[1] +
            alignmentError[2] * alignmentError[2]);

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();
    VrWeaponProfiles::EffectiveCalibration effective;
    {
        std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
        effective = g_vrActiveEffectiveWeaponCalibration;
    }

    Com_Printf(
        0,
        "[VR][CONFIG][APPLY] Weapon position offset %.2f %.2f %.2f "
        "%s survived final grip-tag alignment '%s'; camera-local delta "
        "%.2f %.2f %.2f %s; residual error %.4f %s.\n",
        VR_DisplayInches(
            effective.pose.offset[0],
            configurable.measurementUnits),
        VR_DisplayInches(
            effective.pose.offset[1],
            configurable.measurementUnits),
        VR_DisplayInches(
            effective.pose.offset[2],
            configurable.measurementUnits),
        VR_DisplayLengthUnit(configurable.measurementUnits),
        gripTagName,
        VR_DisplayInches(
            calibrationCameraLocal[0],
            configurable.measurementUnits),
        VR_DisplayInches(
            calibrationCameraLocal[1],
            configurable.measurementUnits),
        VR_DisplayInches(
            calibrationCameraLocal[2],
            configurable.measurementUnits),
        VR_DisplayLengthUnit(configurable.measurementUnits),
        VR_DisplayInches(
            alignmentErrorLength,
            configurable.measurementUnits),
        VR_DisplayLengthUnit(configurable.measurementUnits));

    const char* const receiptPath =
        std::getenv("KISAK_VR_SETTINGS_RECEIPT_PATH");
    if (receiptPath == nullptr || receiptPath[0] == '\0')
    {
        return;
    }

    FILE* receipt = nullptr;
    if (fopen_s(&receipt, receiptPath, "ab") != 0 || receipt == nullptr)
    {
        Com_PrintWarning(
            0,
            "[VR][CONFIG][APPLY] The runtime could not append weapon "
            "pose verification to the settings receipt.\n");
        return;
    }

    std::fprintf(
        receipt,
        "\r\nSTATUS=RUNTIME_WEAPON_POSE_APPLIED\r\n"
        "RUNTIME_WEAPON_OFFSET_APPLIED=%.2f %.2f %.2f\r\n"
        "RUNTIME_WEAPON_OFFSET_CAMERA_DELTA=%.2f %.2f %.2f\r\n"
        "RUNTIME_WEAPON_ALIGNMENT_TAG=%s\r\n"
        "RUNTIME_WEAPON_ALIGNMENT_ERROR=%.4f\r\n"
        "RUNTIME_WEAPON_DISPLAY_UNITS=%s\r\n"
        "RUNTIME_WEAPON_OFFSET_APPLIED_DISPLAY=%.2f %.2f %.2f %s\r\n"
        "RUNTIME_WEAPON_ALIGNMENT_ERROR_DISPLAY=%.4f %s\r\n",
        effective.pose.offset[0],
        effective.pose.offset[1],
        effective.pose.offset[2],
        calibrationCameraLocal[0],
        calibrationCameraLocal[1],
        calibrationCameraLocal[2],
        gripTagName,
        alignmentErrorLength,
        VR_MeasurementUnitSystemId(configurable.measurementUnits),
        VR_DisplayInches(
            effective.pose.offset[0],
            configurable.measurementUnits),
        VR_DisplayInches(
            effective.pose.offset[1],
            configurable.measurementUnits),
        VR_DisplayInches(
            effective.pose.offset[2],
            configurable.measurementUnits),
        VR_DisplayLengthUnit(configurable.measurementUnits),
        VR_DisplayInches(
            alignmentErrorLength,
            configurable.measurementUnits),
        VR_DisplayLengthUnit(configurable.measurementUnits));

    const bool writeSucceeded =
        std::fflush(receipt) == 0 && std::ferror(receipt) == 0;
    std::fclose(receipt);

    if (!writeSucceeded)
    {
        Com_PrintWarning(
            0,
            "[VR][CONFIG][APPLY] Weapon pose verification could not "
            "be flushed completely to the settings receipt.\n");
    }
}


bool VR_GetTrackedControllerGripPoseWorld(
    const bool leftHand,
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float gripOrigin[3],
    float gripAxis[3][3])
{
    if (cameraOrigin == nullptr ||
        cameraAxis == nullptr ||
        gripOrigin == nullptr ||
        gripAxis == nullptr)
    {
        return false;
    }

    float gripCameraLocal[3] = {};
    float gripAxisCameraLocal[3][3] = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        const bool poseValid =
            leftHand
                ? g_vrLeftControllerForegripPoseValid
                : g_vrRightControllerWeaponPoseValid;

        if (!poseValid)
        {
            return false;
        }

        std::memcpy(
            gripCameraLocal,
            leftHand
                ? g_vrLeftControllerForegripPosition
                : g_vrRightControllerWeaponPosition,
            sizeof(gripCameraLocal));

        std::memcpy(
            gripAxisCameraLocal,
            leftHand
                ? g_vrLeftControllerForegripAxis
                : g_vrRightControllerGripAxis,
            sizeof(gripAxisCameraLocal));
    }

    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        gripOrigin[worldComponent] =
            cameraOrigin[worldComponent] +
            gripCameraLocal[0] *
                cameraAxis[0][worldComponent] +
            gripCameraLocal[1] *
                cameraAxis[1][worldComponent] +
            gripCameraLocal[2] *
                cameraAxis[2][worldComponent];
    }

    for (int axisRow = 0;
         axisRow < 3;
         ++axisRow)
    {
        for (int worldComponent = 0;
             worldComponent < 3;
             ++worldComponent)
        {
            gripAxis[axisRow][worldComponent] =
                gripAxisCameraLocal[axisRow][0] *
                    cameraAxis[0][worldComponent] +
                gripAxisCameraLocal[axisRow][1] *
                    cameraAxis[1][worldComponent] +
                gripAxisCameraLocal[axisRow][2] *
                    cameraAxis[2][worldComponent];
        }
    }

    return true;
}


bool VR_GetTrackedLeftControllerPalmQuaternionWorld(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float palmOrigin[3],
    float palmOrientationHeadLocalOpenXr[4])
{
    if (cameraOrigin == nullptr ||
        cameraAxis == nullptr ||
        palmOrigin == nullptr ||
        palmOrientationHeadLocalOpenXr == nullptr)
    {
        return false;
    }

    float palmCameraLocal[3] = {};
    XrQuaternionf orientation = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrLeftControllerPalmPoseValid)
        {
            return false;
        }

        std::memcpy(
            palmCameraLocal,
            g_vrLeftControllerPalmPosition,
            sizeof(palmCameraLocal));

        orientation =
            g_vrLeftControllerPalmOrientationHeadLocalOpenXr;
    }

    orientation =
        VR_NormalizeQuaternion(
            orientation);

    if (!std::isfinite(orientation.x) ||
        !std::isfinite(orientation.y) ||
        !std::isfinite(orientation.z) ||
        !std::isfinite(orientation.w))
    {
        return false;
    }

    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        palmOrigin[worldComponent] =
            cameraOrigin[worldComponent] +
            palmCameraLocal[0] *
                cameraAxis[0][worldComponent] +
            palmCameraLocal[1] *
                cameraAxis[1][worldComponent] +
            palmCameraLocal[2] *
                cameraAxis[2][worldComponent];
    }

    palmOrientationHeadLocalOpenXr[0] =
        orientation.x;
    palmOrientationHeadLocalOpenXr[1] =
        orientation.y;
    palmOrientationHeadLocalOpenXr[2] =
        orientation.z;
    palmOrientationHeadLocalOpenXr[3] =
        orientation.w;

    return true;
}


bool VR_GetTrackedLeftControllerGripQuaternionWorld(
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float gripOrigin[3],
    float gripOrientationHeadLocalOpenXr[4])
{
    if (cameraOrigin == nullptr ||
        cameraAxis == nullptr ||
        gripOrigin == nullptr ||
        gripOrientationHeadLocalOpenXr == nullptr)
    {
        return false;
    }

    float gripCameraLocal[3] = {};
    XrQuaternionf orientation = {
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrLeftControllerForegripPoseValid)
        {
            return false;
        }

        std::memcpy(
            gripCameraLocal,
            g_vrLeftControllerForegripPosition,
            sizeof(gripCameraLocal));

        orientation =
            g_vrLeftControllerGripOrientationHeadLocalOpenXr;
    }

    orientation =
        VR_NormalizeQuaternion(
            orientation);

    if (!std::isfinite(orientation.x) ||
        !std::isfinite(orientation.y) ||
        !std::isfinite(orientation.z) ||
        !std::isfinite(orientation.w))
    {
        return false;
    }

    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        gripOrigin[worldComponent] =
            cameraOrigin[worldComponent] +
            gripCameraLocal[0] *
                cameraAxis[0][worldComponent] +
            gripCameraLocal[1] *
                cameraAxis[1][worldComponent] +
            gripCameraLocal[2] *
                cameraAxis[2][worldComponent];
    }

    gripOrientationHeadLocalOpenXr[0] =
        orientation.x;
    gripOrientationHeadLocalOpenXr[1] =
        orientation.y;
    gripOrientationHeadLocalOpenXr[2] =
        orientation.z;
    gripOrientationHeadLocalOpenXr[3] =
        orientation.w;

    return true;
}


bool VR_UsesPimaxGripPoseFallback()
{
    if (g_vrRuntimeBackend !=
        VrRuntimeBackend::OpenXr)
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (g_vrLeftControllerPalmPoseValid)
        {
            return false;
        }
    }

    const auto containsCaseInsensitive = [](
        const char* const value,
        const char* const needle)
    {
        if (value == nullptr || needle == nullptr ||
            needle[0] == '\0')
        {
            return false;
        }

        const std::size_t needleLength =
            std::strlen(needle);
        for (const char* cursor = value;
             *cursor != '\0';
             ++cursor)
        {
            if (_strnicmp(
                    cursor,
                    needle,
                    needleLength) == 0)
            {
                return true;
            }
        }
        return false;
    };

    const bool pimaxIdentity =
        containsCaseInsensitive(
            g_vrCompatibilityRuntimeName.data(),
            "pimax") ||
        containsCaseInsensitive(
            g_vrCompatibilityRuntimeName.data(),
            "piopenxr") ||
        containsCaseInsensitive(
            g_vrCompatibilityHeadsetName.data(),
            "pimax");

    if (pimaxIdentity)
    {
        static bool loggedPimaxFreeHandBasis = false;
        if (!loggedPimaxFreeHandBasis)
        {
            Com_Printf(
                0,
                "[VR][HANDS][V86] Pimax grip/pose fallback uses "
                "the dedicated free-hand anatomical basis; weapon, "
                "support-grip, magazine, and grenade frames are unchanged.\n");
            loggedPimaxFreeHandBasis = true;
        }
    }

    return pimaxIdentity;
}


bool VR_GetLeftControllerSupportGripPressed(
    bool* supportGripPressed)
{
    if (supportGripPressed == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    if (!g_vrLeftControllerForegripPoseValid)
    {
        *supportGripPressed = false;
        return false;
    }

    // This state is already suppressed while physical magazine handling owns
    // the left squeeze, so the rendered hand cannot attach to the rifle during
    // a magazine draw or insertion.
    *supportGripPressed =
        g_vrLeftControllerForegripPressed;

    return true;
}


bool VR_SupportGripUsesAutomaticProximity()
{
    return
        VR_GetConfiguratorSettings().supportGripMode ==
        VrInteractions::SupportGripMode::Proximity;
}


void VR_UpdateManualMagazineReload(
    const int weaponIndex,
    const bool supported,
    const bool canReload,
    const float magazineWellOrigin[3],
    const float magazineWellAxis[3][3],
    const float cameraOrigin[3],
    const float cameraAxis[3][3])
{
    if (magazineWellOrigin == nullptr ||
        magazineWellAxis == nullptr ||
        cameraOrigin == nullptr ||
        cameraAxis == nullptr)
    {
        return;
    }

    if (!g_vrManualMagazineReload.settingRead)
    {
        g_vrManualMagazineReload.enabled =
            VR_GetConfiguratorSettings().manualReload;

        g_vrManualMagazineReload.settingRead = true;

        Com_Printf(
            0,
            "[VR][RELOAD] Physical detachable-magazine reloads "
            "are %s.\n",
            g_vrManualMagazineReload.enabled
                ? "enabled"
                : "disabled");
    }

    bool ejectButtonHeld = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        ejectButtonHeld =
            g_vrRightAButtonHeld;
    }

    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    bool logEjected = false;
    bool logGrabbedLoaded = false;
    bool logPullCanceled = false;
    bool ejectedByPull = false;
    bool logDrewFresh = false;
    bool logDroppedFresh = false;
    bool logInserted = false;
    bool applyInsertHaptic = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        const bool effectiveSupport =
            g_vrManualMagazineReload.enabled &&
            supported;

        const bool weaponChanged =
            g_vrManualMagazineReload.weaponIndex !=
                weaponIndex ||
            g_vrManualMagazineReload.supported !=
                effectiveSupport;

        if (weaponChanged)
        {
            g_vrManualMagazineReload.weaponIndex =
                weaponIndex;

            g_vrManualMagazineReload.supported =
                effectiveSupport;

            g_vrManualMagazineReload.stage =
                VrManualMagazineReloadStage::Ready;

            g_vrManualMagazineReload.ejectButtonWasHeld =
                ejectButtonHeld;

            g_vrManualMagazineReload.leftSqueezeWasHeld =
                g_vrLeftControllerSqueezePressedRaw;

            g_vrManualMagazineReload.heldNearMagazineWell =
                false;

            g_vrManualMagazineReload.heldPoseValid =
                false;

            g_vrManualMagazineReload.ejectedAtMilliseconds =
                0u;

            g_vrManualMagazineReload.commitUntilMilliseconds =
                0u;
        }

        g_vrManualMagazineReload.canReload =
            canReload;

        std::memcpy(
            g_vrManualMagazineReload.magazineWellOrigin,
            magazineWellOrigin,
            sizeof(
                g_vrManualMagazineReload.magazineWellOrigin));

        std::memcpy(
            g_vrManualMagazineReload.magazineWellAxis,
            magazineWellAxis,
            sizeof(
                g_vrManualMagazineReload.magazineWellAxis));

        const bool leftSqueezeHeld =
            g_vrLeftControllerSqueezePressedRaw;

        const bool ejectButtonPressedEdge =
            ejectButtonHeld &&
            !g_vrManualMagazineReload.ejectButtonWasHeld;

        const bool squeezePressedEdge =
            leftSqueezeHeld &&
            !g_vrManualMagazineReload.leftSqueezeWasHeld;

        const bool squeezeReleasedEdge =
            !leftSqueezeHeld &&
            g_vrManualMagazineReload.leftSqueezeWasHeld;

        g_vrManualMagazineReload.ejectButtonWasHeld =
            ejectButtonHeld;

        g_vrManualMagazineReload.leftSqueezeWasHeld =
            leftSqueezeHeld;

        if (!effectiveSupport)
        {
            return;
        }

        if (!canReload &&
            g_vrManualMagazineReload.stage !=
                VrManualMagazineReloadStage::Ready)
        {
            g_vrManualMagazineReload.stage =
                VrManualMagazineReloadStage::Ready;

            g_vrManualMagazineReload.heldNearMagazineWell =
                false;

            g_vrManualMagazineReload.heldPoseValid =
                false;
        }

        if (canReload &&
            ejectButtonPressedEdge &&
            VR_GetConfiguratorSettings().reloadEjectMode ==
                VrInteractions::ReloadEjectMode::Button &&
            g_vrManualMagazineReload.stage ==
                VrManualMagazineReloadStage::Ready)
        {
            g_vrManualMagazineReload.stage =
                VrManualMagazineReloadStage::Ejected;

            g_vrManualMagazineReload.ejectedAtMilliseconds =
                nowMilliseconds;

            std::memcpy(
                g_vrManualMagazineReload.ejectedOrigin,
                magazineWellOrigin,
                sizeof(
                    g_vrManualMagazineReload.ejectedOrigin));

            std::memcpy(
                g_vrManualMagazineReload.ejectedAxis,
                magazineWellAxis,
                sizeof(
                    g_vrManualMagazineReload.ejectedAxis));

            for (int component = 0;
                 component < 3;
                 ++component)
            {
                g_vrManualMagazineReload
                    .ejectedVelocity[component] =
                    magazineWellAxis[1][component] *
                        5.0f -
                    magazineWellAxis[2][component] *
                        10.0f;
            }

            g_vrManualMagazineReload.heldNearMagazineWell =
                false;

            g_vrManualMagazineReload.heldPoseValid =
                false;

            g_vrLeftControllerForegripPressed = false;
            g_vrTwoHandWeaponTargetActive = false;

            logEjected = true;
        }

        float leftGripWorld[3] = {};
        float leftGripWorldAxis[3][3] = {};

        if (g_vrLeftControllerForegripPoseValid)
        {
            for (int worldComponent = 0;
                 worldComponent < 3;
                 ++worldComponent)
            {
                leftGripWorld[worldComponent] =
                    cameraOrigin[worldComponent] +
                    g_vrLeftControllerForegripPosition[0] *
                        cameraAxis[0][worldComponent] +
                    g_vrLeftControllerForegripPosition[1] *
                        cameraAxis[1][worldComponent] +
                    g_vrLeftControllerForegripPosition[2] *
                        cameraAxis[2][worldComponent];
            }

            for (int axisRow = 0;
                 axisRow < 3;
                 ++axisRow)
            {
                for (int worldComponent = 0;
                     worldComponent < 3;
                     ++worldComponent)
                {
                    leftGripWorldAxis[axisRow][worldComponent] =
                        g_vrLeftControllerForegripAxis[axisRow][0] *
                            cameraAxis[0][worldComponent] +
                        g_vrLeftControllerForegripAxis[axisRow][1] *
                            cameraAxis[1][worldComponent] +
                        g_vrLeftControllerForegripAxis[axisRow][2] *
                            cameraAxis[2][worldComponent];
                }
            }
        }

        const VrConfiguratorSettings& configurable =
            VR_GetConfiguratorSettings();

        const float beltMinimumUp =
            configurable.beltHeight - 14.0f;

        const float beltMaximumUp =
            configurable.beltHeight + 14.0f;

        const float magazineHipCenter =
            VrInteractions::MagazineHipCenter(
                configurable.dominantHand,
                configurable.magazineHip,
                configurable.beltHipDistance);
        const float magazineHipMinimum =
            magazineHipCenter - configurable.beltGrabRadius;
        const float magazineHipMaximum =
            magazineHipCenter + configurable.beltGrabRadius;

        const bool offhandGripAtMagazineHip =
            g_vrLeftControllerForegripPoseValid &&
            g_vrLeftControllerForegripPosition[0] >=
                -18.0f + configurable.beltForwardOffset &&
            g_vrLeftControllerForegripPosition[0] <=
                24.0f + configurable.beltForwardOffset &&
            g_vrLeftControllerForegripPosition[1] >=
                magazineHipMinimum &&
            g_vrLeftControllerForegripPosition[1] <=
                magazineHipMaximum &&
            g_vrLeftControllerForegripPosition[2] <=
                beltMaximumUp &&
            g_vrLeftControllerForegripPosition[2] >=
                beltMinimumUp;

        const float loadedMagazineDelta[3] = {
            leftGripWorld[0] - magazineWellOrigin[0],
            leftGripWorld[1] - magazineWellOrigin[1],
            leftGripWorld[2] - magazineWellOrigin[2],
        };
        const float loadedMagazineDistance =
            std::sqrt(
                loadedMagazineDelta[0] * loadedMagazineDelta[0] +
                loadedMagazineDelta[1] * loadedMagazineDelta[1] +
                loadedMagazineDelta[2] * loadedMagazineDelta[2]);

        if (canReload &&
            configurable.reloadEjectMode ==
                VrInteractions::ReloadEjectMode::Pull &&
            g_vrManualMagazineReload.stage ==
                VrManualMagazineReloadStage::Ready &&
            squeezePressedEdge &&
            g_vrLeftControllerForegripPoseValid &&
            loadedMagazineDistance <= configurable.reloadInsertRadius)
        {
            g_vrManualMagazineReload.stage =
                VrManualMagazineReloadStage::HoldingLoaded;
            g_vrManualMagazineReload.heldPoseValid = true;
            std::memcpy(
                g_vrManualMagazineReload.heldOrigin,
                leftGripWorld,
                sizeof(g_vrManualMagazineReload.heldOrigin));
            std::memcpy(
                g_vrManualMagazineReload.heldAxis,
                magazineWellAxis,
                sizeof(g_vrManualMagazineReload.heldAxis));
            g_vrLeftControllerForegripPressed = false;
            g_vrTwoHandWeaponTargetActive = false;
            logGrabbedLoaded = true;
        }

        if (g_vrManualMagazineReload.stage ==
                VrManualMagazineReloadStage::HoldingLoaded)
        {
            if (g_vrLeftControllerForegripPoseValid)
            {
                std::memcpy(
                    g_vrManualMagazineReload.heldOrigin,
                    leftGripWorld,
                    sizeof(g_vrManualMagazineReload.heldOrigin));
                std::memcpy(
                    g_vrManualMagazineReload.heldAxis,
                    leftGripWorldAxis,
                    sizeof(g_vrManualMagazineReload.heldAxis));
            }

            if (loadedMagazineDistance >=
                configurable.reloadPullDistance)
            {
                g_vrManualMagazineReload.stage =
                    VrManualMagazineReloadStage::Ejected;
                g_vrManualMagazineReload.heldPoseValid = false;
                g_vrManualMagazineReload.ejectedAtMilliseconds =
                    nowMilliseconds;
                std::memcpy(
                    g_vrManualMagazineReload.ejectedOrigin,
                    leftGripWorld,
                    sizeof(g_vrManualMagazineReload.ejectedOrigin));
                std::memcpy(
                    g_vrManualMagazineReload.ejectedAxis,
                    leftGripWorldAxis,
                    sizeof(g_vrManualMagazineReload.ejectedAxis));
                for (int component = 0; component < 3; ++component)
                {
                    g_vrManualMagazineReload.ejectedVelocity[component] =
                        0.0f;
                }
                logEjected = true;
                ejectedByPull = true;
            }
            else if (squeezeReleasedEdge)
            {
                g_vrManualMagazineReload.stage =
                    VrManualMagazineReloadStage::Ready;
                g_vrManualMagazineReload.heldPoseValid = false;
                logPullCanceled = true;
            }
        }

        if (g_vrManualMagazineReload.stage ==
                VrManualMagazineReloadStage::Ejected &&
            !ejectedByPull &&
            squeezePressedEdge &&
            offhandGripAtMagazineHip)
        {
            g_vrManualMagazineReload.stage =
                VrManualMagazineReloadStage::HoldingFresh;

            g_vrManualMagazineReload.heldPoseValid =
                true;

            std::memcpy(
                g_vrManualMagazineReload.heldOrigin,
                leftGripWorld,
                sizeof(
                    g_vrManualMagazineReload.heldOrigin));

            std::memcpy(
                g_vrManualMagazineReload.heldAxis,
                leftGripWorldAxis,
                sizeof(
                    g_vrManualMagazineReload.heldAxis));

            logDrewFresh = true;
        }

        if (g_vrManualMagazineReload.stage ==
                VrManualMagazineReloadStage::HoldingFresh &&
            g_vrLeftControllerForegripPoseValid)
        {
            std::memcpy(
                g_vrManualMagazineReload.heldOrigin,
                leftGripWorld,
                sizeof(
                    g_vrManualMagazineReload.heldOrigin));

            std::memcpy(
                g_vrManualMagazineReload.heldAxis,
                leftGripWorldAxis,
                sizeof(
                    g_vrManualMagazineReload.heldAxis));

            g_vrManualMagazineReload.heldPoseValid =
                true;

            const float delta[3] = {
                leftGripWorld[0] -
                    magazineWellOrigin[0],
                leftGripWorld[1] -
                    magazineWellOrigin[1],
                leftGripWorld[2] -
                    magazineWellOrigin[2],
            };

            const float distanceToMagazineWell =
                std::sqrt(
                    delta[0] * delta[0] +
                    delta[1] * delta[1] +
                    delta[2] * delta[2]);

            const float insertionRadius =
                configurable.reloadInsertRadius;

            g_vrManualMagazineReload.heldNearMagazineWell =
                distanceToMagazineWell <=
                    insertionRadius;

            if (g_vrManualMagazineReload
                    .heldNearMagazineWell)
            {
                // Snap only the model's orientation while the controller
                // remains the positional authority.  This makes insertion
                // forgiving without teleporting the user's hand.
                std::memcpy(
                    g_vrManualMagazineReload.heldAxis,
                    magazineWellAxis,
                    sizeof(
                        g_vrManualMagazineReload.heldAxis));
            }

            const bool contactInsertion =
                configurable.reloadInsertMode ==
                    VrInteractions::ReloadInsertMode::Contact;
            if ((contactInsertion &&
                 g_vrManualMagazineReload.heldNearMagazineWell) ||
                squeezeReleasedEdge)
            {
                if (g_vrManualMagazineReload
                        .heldNearMagazineWell)
                {
                    g_vrManualMagazineReload.stage =
                        VrManualMagazineReloadStage::Ready;

                    g_vrManualMagazineReload.heldPoseValid =
                        false;

                    g_vrManualMagazineReload
                        .heldNearMagazineWell = false;

                    g_vrManualMagazineReload
                        .commitUntilMilliseconds =
                        nowMilliseconds + 400u;

                    logInserted = true;
                    applyInsertHaptic = true;
                }
                else
                {
                    g_vrManualMagazineReload.stage =
                        VrManualMagazineReloadStage::Ejected;

                    g_vrManualMagazineReload.heldPoseValid =
                        false;

                    g_vrManualMagazineReload
                        .heldNearMagazineWell = false;

                    g_vrManualMagazineReload
                        .ejectedAtMilliseconds =
                        nowMilliseconds;

                    std::memcpy(
                        g_vrManualMagazineReload.ejectedOrigin,
                        leftGripWorld,
                        sizeof(
                            g_vrManualMagazineReload
                                .ejectedOrigin));

                    std::memcpy(
                        g_vrManualMagazineReload.ejectedAxis,
                        leftGripWorldAxis,
                        sizeof(
                            g_vrManualMagazineReload
                                .ejectedAxis));

                    g_vrManualMagazineReload.ejectedVelocity[0] =
                        0.0f;

                    g_vrManualMagazineReload.ejectedVelocity[1] =
                        0.0f;

                    g_vrManualMagazineReload.ejectedVelocity[2] =
                        -6.0f;

                    logDroppedFresh = true;
                }
            }
        }

        if (g_vrManualMagazineReload.stage !=
            VrManualMagazineReloadStage::Ready)
        {
            g_vrLeftControllerForegripPressed = false;
            g_vrTwoHandWeaponTargetActive = false;
        }
    }

    if (logEjected)
    {
        Com_Printf(
            0,
            "[VR][RELOAD] %s weapon %d magazine. Release and grip "
            "at the configured fresh-magazine hip to draw a new one.\n",
            ejectedByPull ? "Physically pulled" : "Button-ejected",
            weaponIndex);
        if (ejectedByPull)
        {
            VR_ApplyOffhandControllerHaptic(0.30f, 0.035f);
        }
    }

    if (logGrabbedLoaded)
    {
        Com_Printf(
            0,
            "[VR][RELOAD] Off hand gripped weapon %d's loaded "
            "magazine; pull it %.1f %s from the well.\n",
            weaponIndex,
            VR_DisplayInches(
                VR_GetConfiguratorSettings().reloadPullDistance,
                VR_GetConfiguratorSettings().measurementUnits),
            VR_DisplayLengthUnit(
                VR_GetConfiguratorSettings().measurementUnits));
        VR_ApplyOffhandControllerHaptic(0.20f, 0.030f);
    }

    if (logPullCanceled)
    {
        Com_Printf(
            0,
            "[VR][RELOAD] Released the loaded magazine before the "
            "pull threshold; snapped it back into the weapon.\n");
    }

    if (logDrewFresh)
    {
        Com_Printf(
            0,
            "[VR][RELOAD] Drew a fresh weapon %d magazine. "
            "Release squeeze inside the magazine well.\n",
            weaponIndex);
    }

    if (logDroppedFresh)
    {
        Com_Printf(
            0,
            "[VR][RELOAD] Dropped the fresh magazine outside "
            "the magazine well.\n");
    }

    if (logInserted)
    {
        Com_Printf(
            0,
            "[VR][RELOAD] Inserted weapon %d magazine; "
            "committing ammo transfer.\n",
            weaponIndex);
    }

    if (applyInsertHaptic)
    {
        VR_ApplyOffhandControllerHaptic(
            0.35f,
            0.045f);
    }
}


bool VR_GetManualMagazineReloadRenderState(
    const int weaponIndex,
    bool* hideLoadedMagazine,
    bool* drawEjectedMagazine,
    float ejectedOrigin[3],
    float ejectedAxis[3][3],
    bool* drawHeldMagazine,
    float heldOrigin[3],
    float heldAxis[3][3])
{
    if (hideLoadedMagazine == nullptr ||
        drawEjectedMagazine == nullptr ||
        ejectedOrigin == nullptr ||
        ejectedAxis == nullptr ||
        drawHeldMagazine == nullptr ||
        heldOrigin == nullptr ||
        heldAxis == nullptr)
    {
        return false;
    }

    *hideLoadedMagazine = false;
    *drawEjectedMagazine = false;
    *drawHeldMagazine = false;

    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    if (!g_vrManualMagazineReload.enabled ||
        !g_vrManualMagazineReload.supported ||
        g_vrManualMagazineReload.weaponIndex !=
            weaponIndex)
    {
        return false;
    }

    *hideLoadedMagazine =
        g_vrManualMagazineReload.stage !=
        VrManualMagazineReloadStage::Ready;

    if (g_vrManualMagazineReload.ejectedAtMilliseconds !=
        0u)
    {
        const std::uint32_t ageMilliseconds =
            nowMilliseconds -
            g_vrManualMagazineReload
                .ejectedAtMilliseconds;

        constexpr std::uint32_t ejectedLifetimeMilliseconds =
            1400u;

        if (ageMilliseconds <=
            ejectedLifetimeMilliseconds)
        {
            const float ageSeconds =
                static_cast<float>(ageMilliseconds) /
                1000.0f;

            for (int component = 0;
                 component < 3;
                 ++component)
            {
                ejectedOrigin[component] =
                    g_vrManualMagazineReload
                        .ejectedOrigin[component] +
                    g_vrManualMagazineReload
                        .ejectedVelocity[component] *
                        ageSeconds;
            }

            // CoD world units are inches.  A deliberately reduced visual
            // gravity keeps the dropped model readable in the headset.
            ejectedOrigin[2] -=
                90.0f *
                ageSeconds *
                ageSeconds;

            std::memcpy(
                ejectedAxis,
                g_vrManualMagazineReload.ejectedAxis,
                sizeof(
                    g_vrManualMagazineReload.ejectedAxis));

            *drawEjectedMagazine = true;
        }
    }

    if ((g_vrManualMagazineReload.stage ==
             VrManualMagazineReloadStage::HoldingFresh ||
         g_vrManualMagazineReload.stage ==
             VrManualMagazineReloadStage::HoldingLoaded) &&
        g_vrManualMagazineReload.heldPoseValid)
    {
        std::memcpy(
            heldOrigin,
            g_vrManualMagazineReload.heldOrigin,
            sizeof(
                g_vrManualMagazineReload.heldOrigin));

        std::memcpy(
            heldAxis,
            g_vrManualMagazineReload.heldAxis,
            sizeof(
                g_vrManualMagazineReload.heldAxis));

        *drawHeldMagazine = true;
    }

    return true;
}


bool VR_ManualMagazineReloadSuppressesAutomaticReload(
    const int weaponIndex)
{
    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    return
        g_vrManualMagazineReload.enabled &&
        g_vrManualMagazineReload.supported &&
        g_vrManualMagazineReload.weaponIndex ==
            weaponIndex;
}


bool VR_IsManualMagazineReloadCommitActive(
    const int weaponIndex)
{
    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    if (!g_vrManualMagazineReload.enabled ||
        !g_vrManualMagazineReload.supported ||
        g_vrManualMagazineReload.weaponIndex !=
            weaponIndex ||
        g_vrManualMagazineReload
                .commitUntilMilliseconds == 0u)
    {
        return false;
    }

    return static_cast<std::int32_t>(
               g_vrManualMagazineReload
                   .commitUntilMilliseconds -
               nowMilliseconds) > 0;
}


// KISAK_SP_VR_MANUAL_GRENADE_RELEASE_CALIBRATION_V54
static void VR_ClearManualGrenadeVelocityHistoryLocked()
{
    for (VrManualGrenadeVelocitySample& sample :
         g_vrManualGrenade.velocitySamples)
    {
        sample = VrManualGrenadeVelocitySample{};
    }

    g_vrManualGrenade.velocitySampleWriteIndex = 0u;
    g_vrManualGrenade.releaseVelocitySampleAgeMilliseconds = 0u;
}


static void VR_RecordManualGrenadeVelocityLocked(
    const std::uint32_t nowMilliseconds,
    const float velocity[3])
{
    if (velocity == nullptr)
    {
        return;
    }

    VrManualGrenadeVelocitySample& sample =
        g_vrManualGrenade.velocitySamples[
            g_vrManualGrenade.velocitySampleWriteIndex];

    sample.valid = true;
    sample.recordedAtMilliseconds = nowMilliseconds;

    std::memcpy(
        sample.velocity,
        velocity,
        sizeof(sample.velocity));

    g_vrManualGrenade.velocitySampleWriteIndex =
        (g_vrManualGrenade.velocitySampleWriteIndex + 1u) %
        kVrManualGrenadeVelocitySampleCapacity;
}


static bool VR_SelectManualGrenadeReleaseVelocityLocked(
    const std::uint32_t nowMilliseconds,
    float velocity[3],
    std::uint32_t* sampleAgeMilliseconds)
{
    if (velocity == nullptr ||
        sampleAgeMilliseconds == nullptr)
    {
        return false;
    }

    constexpr std::uint32_t historyWindowMilliseconds = 140u;
    constexpr float historyAgePenalty = 0.35f;
    constexpr float maximumPhysicalHandSpeed = 500.0f;

    bool selected = false;
    float selectedScore = -1.0f;
    float selectedSpeed = 0.0f;
    std::uint32_t selectedAgeMilliseconds = 0u;

    for (const VrManualGrenadeVelocitySample& sample :
         g_vrManualGrenade.velocitySamples)
    {
        if (!sample.valid)
        {
            continue;
        }

        const std::uint32_t ageMilliseconds =
            nowMilliseconds -
            sample.recordedAtMilliseconds;

        if (ageMilliseconds > historyWindowMilliseconds)
        {
            continue;
        }

        const float speed =
            std::sqrt(
                sample.velocity[0] * sample.velocity[0] +
                sample.velocity[1] * sample.velocity[1] +
                sample.velocity[2] * sample.velocity[2]);

        if (!std::isfinite(speed))
        {
            continue;
        }

        const float normalizedAge =
            static_cast<float>(ageMilliseconds) /
            static_cast<float>(historyWindowMilliseconds);

        const float score =
            speed *
            (1.0f - historyAgePenalty * normalizedAge);

        if (!selected || score > selectedScore)
        {
            selected = true;
            selectedScore = score;
            selectedSpeed = speed;
            selectedAgeMilliseconds = ageMilliseconds;

            std::memcpy(
                velocity,
                sample.velocity,
                sizeof(sample.velocity));
        }
    }

    if (!selected)
    {
        velocity[0] = 0.0f;
        velocity[1] = 0.0f;
        velocity[2] = 0.0f;
        *sampleAgeMilliseconds = 0u;
        return false;
    }

    if (selectedSpeed > maximumPhysicalHandSpeed)
    {
        const float scale =
            maximumPhysicalHandSpeed /
            selectedSpeed;

        velocity[0] *= scale;
        velocity[1] *= scale;
        velocity[2] *= scale;
    }

    *sampleAgeMilliseconds =
        selectedAgeMilliseconds;

    return true;
}


// KISAK_SP_VR_MANUAL_GRENADE_THROW_V53
static void VR_ResetManualGrenadeInteractionLocked()
{
    g_vrManualGrenade.stage =
        VrManualGrenadeStage::Ready;

    g_vrManualGrenade.slot =
        VrManualGrenadeSlot::None;

    g_vrManualGrenade.weaponIndex = 0;
    g_vrManualGrenade.releasedAtMilliseconds = 0u;
    g_vrManualGrenade.pendingUntilMilliseconds = 0u;

    VR_ClearManualGrenadeVelocityHistoryLocked();
}


// KISAK_SP_VR_OFFHAND_INTERACTION_PRIORITY_V86
// Caller must hold g_vrWeaponControllerPoseMutex. Existing magazine ownership
// always wins. In the Ready stage, also reserve the same squeeze edge when it
// is already a valid pull-to-eject grab at the last published magazine well;
// this makes the decision independent of client/cgame update order.
static bool VR_ManualMagazineOwnsOrClaimsLeftGripLocked(
    const float cameraOrigin[3],
    const float cameraAxis[3][3])
{
    if (g_vrManualMagazineReload.stage !=
        VrManualMagazineReloadStage::Ready)
    {
        return true;
    }

    if (!g_vrManualMagazineReload.enabled ||
        !g_vrManualMagazineReload.supported ||
        !g_vrManualMagazineReload.canReload ||
        !g_vrLeftControllerForegripPoseValid ||
        VR_GetConfiguratorSettings().reloadEjectMode !=
            VrInteractions::ReloadEjectMode::Pull)
    {
        return false;
    }

    float leftGripWorld[3] = {};
    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        leftGripWorld[worldComponent] =
            cameraOrigin[worldComponent] +
            g_vrLeftControllerForegripPosition[0] *
                cameraAxis[0][worldComponent] +
            g_vrLeftControllerForegripPosition[1] *
                cameraAxis[1][worldComponent] +
            g_vrLeftControllerForegripPosition[2] *
                cameraAxis[2][worldComponent];
    }

    const float magazineDelta[3] = {
        leftGripWorld[0] -
            g_vrManualMagazineReload.magazineWellOrigin[0],
        leftGripWorld[1] -
            g_vrManualMagazineReload.magazineWellOrigin[1],
        leftGripWorld[2] -
            g_vrManualMagazineReload.magazineWellOrigin[2],
    };

    const float magazineDistance =
        std::sqrt(
            magazineDelta[0] * magazineDelta[0] +
            magazineDelta[1] * magazineDelta[1] +
            magazineDelta[2] * magazineDelta[2]);

    return magazineDistance <=
        VR_GetConfiguratorSettings().reloadInsertRadius;
}


bool VR_UpdateManualGrenadeInput(
    const int fragWeaponIndex,
    const int tacticalWeaponIndex,
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    bool* manualModeEnabled,
    bool* fragHeld,
    bool* tacticalHeld)
{
    if (cameraOrigin == nullptr ||
        cameraAxis == nullptr ||
        manualModeEnabled == nullptr ||
        fragHeld == nullptr ||
        tacticalHeld == nullptr)
    {
        return false;
    }

    *manualModeEnabled = false;
    *fragHeld = false;
    *tacticalHeld = false;

    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    bool logSetting = false;
    bool logGrab = false;
    bool logRelease = false;
    bool logEmptySlot = false;
    bool logPendingTimeout = false;
    bool logMagazinePriority = false;
    bool logSupportGripPriority = false;
    bool loggedUsedPalmPose = false;
    VrManualGrenadeSlot loggedSlot =
        VrManualGrenadeSlot::None;
    int loggedWeaponIndex = 0;
    std::uint32_t loggedVelocitySampleAgeMilliseconds = 0u;
    float loggedLocalPosition[3] = {};
    float loggedReleaseOrigin[3] = {};
    float loggedReleaseVelocity[3] = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrManualGrenade.settingRead)
        {
            g_vrManualGrenade.enabled =
                VR_GetConfiguratorSettings().manualGrenades;

            g_vrManualGrenade.settingRead = true;
            logSetting = true;
        }

        const bool leftSqueezeHeld =
            g_vrLeftControllerSqueezePressedRaw;

        if (!g_vrManualGrenade.inputInitialized)
        {
            // A grip already held when the map starts cannot grab a belt
            // object.  Releasing once arms the required press edge.
            g_vrManualGrenade.leftSqueezeWasHeld =
                leftSqueezeHeld;

            g_vrManualGrenade.inputInitialized = true;
        }

        if (!g_vrManualGrenade.enabled)
        {
            VR_ResetManualGrenadeInteractionLocked();
            g_vrManualGrenade.leftSqueezeWasHeld =
                leftSqueezeHeld;
        }
        else
        {
            *manualModeEnabled = true;

            if (g_vrManualGrenade.stage ==
                    VrManualGrenadeStage::ReleasedPending &&
                static_cast<std::int32_t>(
                    g_vrManualGrenade.pendingUntilMilliseconds -
                    nowMilliseconds) <= 0)
            {
                VR_ResetManualGrenadeInteractionLocked();
                logPendingTimeout = true;
            }

            const bool squeezePressedEdge =
                leftSqueezeHeld &&
                !g_vrManualGrenade.leftSqueezeWasHeld;

            const bool squeezeReleasedEdge =
                !leftSqueezeHeld &&
                g_vrManualGrenade.leftSqueezeWasHeld;

            const bool poseValid =
                g_vrLeftControllerForegripPoseValid;

            float currentOrigin[3] = {};
            float currentAxis[3][3] = {};
            float currentVelocity[3] = {};
            bool currentOriginUsesPalmPose = false;

            if (poseValid)
            {
                for (int worldComponent = 0;
                     worldComponent < 3;
                     ++worldComponent)
                {
                    currentOrigin[worldComponent] =
                        cameraOrigin[worldComponent] +
                        g_vrLeftControllerForegripPosition[0] *
                            cameraAxis[0][worldComponent] +
                        g_vrLeftControllerForegripPosition[1] *
                            cameraAxis[1][worldComponent] +
                        g_vrLeftControllerForegripPosition[2] *
                            cameraAxis[2][worldComponent];

                    currentVelocity[worldComponent] =
                        g_vrLeftControllerLinearVelocity[0] *
                            cameraAxis[0][worldComponent] +
                        g_vrLeftControllerLinearVelocity[1] *
                            cameraAxis[1][worldComponent] +
                        g_vrLeftControllerLinearVelocity[2] *
                            cameraAxis[2][worldComponent];
                }

                for (int axisRow = 0;
                     axisRow < 3;
                     ++axisRow)
                {
                    for (int worldComponent = 0;
                         worldComponent < 3;
                         ++worldComponent)
                    {
                        currentAxis[axisRow][worldComponent] =
                            g_vrLeftControllerForegripAxis[axisRow][0] *
                                cameraAxis[0][worldComponent] +
                            g_vrLeftControllerForegripAxis[axisRow][1] *
                                cameraAxis[1][worldComponent] +
                            g_vrLeftControllerForegripAxis[axisRow][2] *
                                cameraAxis[2][worldComponent];
                    }
                }

                // KISAK_SP_VR_MANUAL_GRENADE_PALM_FIT_V54
                // The floating glove is registered to grip_surface/palm_ext,
                // whereas V53 placed the grenade at grip/pose.  Use the same
                // palm pose for both objects and move the grenade center a
                // small distance into the left palm so it no longer hovers
                // outside the fingers while the grip is held.
                if (g_vrLeftControllerPalmPoseValid)
                {
                    const XrQuaternionf palmOrientation =
                        VR_NormalizeQuaternion(
                            g_vrLeftControllerPalmOrientationHeadLocalOpenXr);

                    const std::array<VrHeadVector, 3> palmAxisCameraLocal = {
                        VR_OpenXrVectorToCod(
                            VR_RotateHeadVector(
                                palmOrientation,
                                {0.0f, 0.0f, -1.0f})),
                        VR_OpenXrVectorToCod(
                            VR_RotateHeadVector(
                                palmOrientation,
                                {-1.0f, 0.0f, 0.0f})),
                        VR_OpenXrVectorToCod(
                            VR_RotateHeadVector(
                                palmOrientation,
                                {0.0f, 1.0f, 0.0f})),
                    };

                    for (int worldComponent = 0;
                         worldComponent < 3;
                         ++worldComponent)
                    {
                        currentOrigin[worldComponent] =
                            cameraOrigin[worldComponent] +
                            g_vrLeftControllerPalmPosition[0] *
                                cameraAxis[0][worldComponent] +
                            g_vrLeftControllerPalmPosition[1] *
                                cameraAxis[1][worldComponent] +
                            g_vrLeftControllerPalmPosition[2] *
                                cameraAxis[2][worldComponent];

                        for (int axisRow = 0;
                             axisRow < 3;
                             ++axisRow)
                        {
                            currentAxis[axisRow][worldComponent] =
                                palmAxisCameraLocal[axisRow].x *
                                    cameraAxis[0][worldComponent] +
                                palmAxisCameraLocal[axisRow].y *
                                    cameraAxis[1][worldComponent] +
                                palmAxisCameraLocal[axisRow].z *
                                    cameraAxis[2][worldComponent];
                        }
                    }

                    constexpr float grenadePalmInset = 0.60f;

                    for (int worldComponent = 0;
                         worldComponent < 3;
                         ++worldComponent)
                    {
                        currentOrigin[worldComponent] +=
                            currentAxis[1][worldComponent] *
                            grenadePalmInset;
                    }

                    currentOriginUsesPalmPose = true;
                }
            }

            const float localForward =
                g_vrLeftControllerForegripPosition[0];

            const float localLeft =
                g_vrLeftControllerForegripPosition[1];

            const float localUp =
                g_vrLeftControllerForegripPosition[2];

            const VrConfiguratorSettings& configurable =
                VR_GetConfiguratorSettings();

            const float beltMinimumUp =
                configurable.beltHeight - 14.0f;

            const float beltMaximumUp =
                configurable.beltHeight + 14.0f;

            const float hipMinimum =
                configurable.beltHipDistance -
                configurable.beltGrabRadius;

            const float hipMaximum =
                configurable.beltHipDistance +
                configurable.beltGrabRadius;

            const bool insideBeltHeight =
                poseValid &&
                localForward >=
                    -18.0f + configurable.beltForwardOffset &&
                localForward <=
                    18.0f + configurable.beltForwardOffset &&
                localUp >= beltMinimumUp &&
                localUp <= beltMaximumUp;

            const bool insideLeftHip =
                insideBeltHeight &&
                localLeft >= hipMinimum &&
                localLeft <= hipMaximum;

            const bool insideRightHip =
                insideBeltHeight &&
                localLeft <= -hipMinimum &&
                localLeft >= -hipMaximum;

            const bool magazineOwnsLeftGrip =
                VR_ManualMagazineOwnsOrClaimsLeftGripLocked(
                    cameraOrigin,
                    cameraAxis);

            const bool supportGripCandidate =
                g_vrLeftControllerForegripPressed &&
                VR_IsSupportGripCandidateLocked();

            const bool beltGrabPressed =
                g_vrManualGrenade.stage ==
                    VrManualGrenadeStage::Ready &&
                squeezePressedEdge &&
                (insideLeftHip || insideRightHip);

            if (beltGrabPressed &&
                (magazineOwnsLeftGrip ||
                 supportGripCandidate))
            {
                logMagazinePriority =
                    magazineOwnsLeftGrip;
                logSupportGripPriority =
                    !magazineOwnsLeftGrip &&
                    supportGripCandidate;
            }

            if (beltGrabPressed &&
                !magazineOwnsLeftGrip &&
                !supportGripCandidate)
            {
                const bool fragUsesLeftHip =
                    VrInteractions::FragUsesLeftHip(
                        configurable.dominantHand,
                        configurable.grenadeBeltLayout);
                const VrManualGrenadeSlot selectedSlot =
                    insideLeftHip == fragUsesLeftHip
                        ? VrManualGrenadeSlot::Frag
                        : VrManualGrenadeSlot::Tactical;

                const int selectedWeaponIndex =
                    selectedSlot ==
                            VrManualGrenadeSlot::Frag
                        ? fragWeaponIndex
                        : tacticalWeaponIndex;

                loggedSlot = selectedSlot;
                loggedWeaponIndex = selectedWeaponIndex;
                loggedLocalPosition[0] = localForward;
                loggedLocalPosition[1] = localLeft;
                loggedLocalPosition[2] = localUp;

                if (selectedWeaponIndex > 0)
                {
                    VR_ClearManualGrenadeVelocityHistoryLocked();

                    g_vrManualGrenade.stage =
                        VrManualGrenadeStage::Holding;

                    g_vrManualGrenade.slot =
                        selectedSlot;

                    g_vrManualGrenade.weaponIndex =
                        selectedWeaponIndex;

                    std::memcpy(
                        g_vrManualGrenade.heldOrigin,
                        currentOrigin,
                        sizeof(
                            g_vrManualGrenade.heldOrigin));

                    std::memcpy(
                        g_vrManualGrenade.heldAxis,
                        currentAxis,
                        sizeof(
                            g_vrManualGrenade.heldAxis));

                    g_vrLeftControllerForegripPressed = false;
                    g_vrTwoHandWeaponTargetActive = false;
                    loggedUsedPalmPose =
                        currentOriginUsesPalmPose;
                    logGrab = true;
                }
                else
                {
                    // Pulse the corresponding native button for one command
                    // so COD4 can show its existing no-grenade hint.
                    if (selectedSlot ==
                        VrManualGrenadeSlot::Frag)
                    {
                        *fragHeld = true;
                    }
                    else
                    {
                        *tacticalHeld = true;
                    }

                    logEmptySlot = true;
                }
            }

            if (g_vrManualGrenade.stage ==
                VrManualGrenadeStage::Holding)
            {
                if (poseValid)
                {
                    std::memcpy(
                        g_vrManualGrenade.heldOrigin,
                        currentOrigin,
                        sizeof(
                            g_vrManualGrenade.heldOrigin));

                    std::memcpy(
                        g_vrManualGrenade.heldAxis,
                        currentAxis,
                        sizeof(
                            g_vrManualGrenade.heldAxis));
                }

                if (poseValid &&
                    g_vrLeftControllerLinearVelocityValid)
                {
                    VR_RecordManualGrenadeVelocityLocked(
                        nowMilliseconds,
                        currentVelocity);
                }

                if (squeezeReleasedEdge ||
                    !leftSqueezeHeld)
                {
                    std::memcpy(
                        g_vrManualGrenade.releaseOrigin,
                        g_vrManualGrenade.heldOrigin,
                        sizeof(
                            g_vrManualGrenade.releaseOrigin));

                    if (!VR_SelectManualGrenadeReleaseVelocityLocked(
                            nowMilliseconds,
                            g_vrManualGrenade.releaseVelocity,
                            &g_vrManualGrenade
                                .releaseVelocitySampleAgeMilliseconds))
                    {
                        g_vrManualGrenade.releaseVelocity[0] = 0.0f;
                        g_vrManualGrenade.releaseVelocity[1] = 0.0f;
                        g_vrManualGrenade.releaseVelocity[2] = 0.0f;
                    }

                    // The server uses this only when the physical sample has
                    // too little horizontal motion to establish a stable
                    // direction.  It is the release-time view forward, not a
                    // later camera orientation.
                    std::memcpy(
                        g_vrManualGrenade.releaseFallbackForward,
                        cameraAxis[0],
                        sizeof(
                            g_vrManualGrenade.releaseFallbackForward));

                    g_vrManualGrenade.stage =
                        VrManualGrenadeStage::ReleasedPending;

                    g_vrManualGrenade.releasedAtMilliseconds =
                        nowMilliseconds;

                    g_vrManualGrenade.pendingUntilMilliseconds =
                        nowMilliseconds + 3000u;

                    g_vrManualGrenade.viewOverrideUntilMilliseconds =
                        nowMilliseconds + 3000u;

                    std::memcpy(
                        loggedReleaseOrigin,
                        g_vrManualGrenade.releaseOrigin,
                        sizeof(loggedReleaseOrigin));

                    std::memcpy(
                        loggedReleaseVelocity,
                        g_vrManualGrenade.releaseVelocity,
                        sizeof(loggedReleaseVelocity));

                    loggedVelocitySampleAgeMilliseconds =
                        g_vrManualGrenade
                            .releaseVelocitySampleAgeMilliseconds;

                    loggedSlot =
                        g_vrManualGrenade.slot;

                    loggedWeaponIndex =
                        g_vrManualGrenade.weaponIndex;

                    logRelease = true;
                }
                else if (g_vrManualGrenade.slot ==
                         VrManualGrenadeSlot::Frag)
                {
                    *fragHeld = true;
                }
                else
                {
                    *tacticalHeld = true;
                }

                g_vrLeftControllerForegripPressed = false;
                g_vrTwoHandWeaponTargetActive = false;
            }
            else if (g_vrManualGrenade.stage ==
                     VrManualGrenadeStage::ReleasedPending)
            {
                g_vrLeftControllerForegripPressed = false;
                g_vrTwoHandWeaponTargetActive = false;
            }

            g_vrManualGrenade.leftSqueezeWasHeld =
                leftSqueezeHeld;
        }
    }

    if (logSetting)
    {
        Com_Printf(
            0,
            "[VR][GRENADE] beta.13 handed manual hip grenades are %s. "
            "Belt layout %s; release/toggle the off-hand grip to throw.\n",
            *manualModeEnabled
                ? "enabled"
                : "disabled",
            VrInteractions::GrenadeBeltLayoutId(
                VR_GetConfiguratorSettings().grenadeBeltLayout));
    }

    if (logMagazinePriority ||
        logSupportGripPriority)
    {
        Com_Printf(
            0,
            "[VR][GRENADE][V86] Belt grab suppressed: the off-hand "
            "squeeze belongs to %s. Release before deliberately "
            "drawing a grenade.\n",
            logMagazinePriority
                ? "physical magazine/reload interaction"
                : "the valid weapon support-grip candidate");
    }

    const char* loggedSlotName =
        loggedSlot == VrManualGrenadeSlot::Frag
            ? "frag"
            : "tactical";

    if (logGrab)
    {
        Com_Printf(
            0,
            "[VR][GRENADE] Grabbed %s hip weapon %d at "
            "HMD-local %.2f %.2f %.2f; held-model anchor %s.\n",
            loggedSlotName,
            loggedWeaponIndex,
            loggedLocalPosition[0],
            loggedLocalPosition[1],
            loggedLocalPosition[2],
            loggedUsedPalmPose
                ? "palm_ext/pose"
                : "grip/pose fallback");
        VR_ApplyOffhandControllerHaptic(0.22f, 0.030f);
    }

    if (logEmptySlot)
    {
        Com_Printf(
            0,
            "[VR][GRENADE] %s hip grab had no available "
            "grenade; pulsed COD4's native empty hint.\n",
            loggedSlotName);
    }

    if (logRelease)
    {
        const float releaseSpeed =
            std::sqrt(
                loggedReleaseVelocity[0] *
                    loggedReleaseVelocity[0] +
                loggedReleaseVelocity[1] *
                    loggedReleaseVelocity[1] +
                loggedReleaseVelocity[2] *
                    loggedReleaseVelocity[2]);

        Com_Printf(
            0,
            "[VR][GRENADE] Released %s weapon %d at "
            "%.2f %.2f %.2f using recent hand velocity "
            "%.2f %.2f %.2f (speed %.2f; sample age %u ms).\n",
            loggedSlotName,
            loggedWeaponIndex,
            loggedReleaseOrigin[0],
            loggedReleaseOrigin[1],
            loggedReleaseOrigin[2],
            loggedReleaseVelocity[0],
            loggedReleaseVelocity[1],
            loggedReleaseVelocity[2],
            releaseSpeed,
            loggedVelocitySampleAgeMilliseconds);
        VR_ApplyOffhandControllerHaptic(0.16f, 0.020f);
    }

    if (logPendingTimeout)
    {
        Com_PrintWarning(
            0,
            "[VR][GRENADE] A released grenade did not reach "
            "EV_USE_OFFHAND within 3000 ms; cleared the stale "
            "physical throw sample.\n");
    }

    return true;
}


bool VR_GetManualGrenadeRenderState(
    int* weaponIndex,
    float heldOrigin[3],
    float heldAxis[3][3])
{
    if (weaponIndex == nullptr ||
        heldOrigin == nullptr ||
        heldAxis == nullptr)
    {
        return false;
    }

    *weaponIndex = 0;

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    if (!g_vrManualGrenade.enabled ||
        g_vrManualGrenade.stage !=
            VrManualGrenadeStage::Holding ||
        g_vrManualGrenade.weaponIndex <= 0)
    {
        return false;
    }

    *weaponIndex =
        g_vrManualGrenade.weaponIndex;

    std::memcpy(
        heldOrigin,
        g_vrManualGrenade.heldOrigin,
        sizeof(g_vrManualGrenade.heldOrigin));

    std::memcpy(
        heldAxis,
        g_vrManualGrenade.heldAxis,
        sizeof(g_vrManualGrenade.heldAxis));

    return true;
}


bool VR_IsManualGrenadeViewOverrideActive()
{
    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    const bool recoveryWindowActive =
        g_vrManualGrenade
            .viewOverrideUntilMilliseconds != 0u &&
        static_cast<std::int32_t>(
            g_vrManualGrenade
                .viewOverrideUntilMilliseconds -
            nowMilliseconds) > 0;

    return
        g_vrManualGrenade.enabled &&
        (g_vrManualGrenade.stage !=
             VrManualGrenadeStage::Ready ||
         recoveryWindowActive);
}


bool VR_IsManualGrenadeReleasePending(
    const int weaponIndex)
{
    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    return
        g_vrManualGrenade.enabled &&
        g_vrManualGrenade.stage ==
            VrManualGrenadeStage::ReleasedPending &&
        g_vrManualGrenade.weaponIndex ==
            weaponIndex &&
        static_cast<std::int32_t>(
            g_vrManualGrenade.pendingUntilMilliseconds -
            nowMilliseconds) > 0;
}


bool VR_ConsumeManualGrenadeThrow(
    const int weaponIndex,
    float releaseOrigin[3],
    float releaseVelocity[3],
    float releaseFallbackForward[3],
    unsigned int* velocitySampleAgeMilliseconds,
    unsigned int* releaseAgeMilliseconds)
{
    if (releaseOrigin == nullptr ||
        releaseVelocity == nullptr ||
        releaseFallbackForward == nullptr ||
        velocitySampleAgeMilliseconds == nullptr ||
        releaseAgeMilliseconds == nullptr)
    {
        return false;
    }

    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    if (!g_vrManualGrenade.enabled ||
        g_vrManualGrenade.stage !=
            VrManualGrenadeStage::ReleasedPending ||
        g_vrManualGrenade.weaponIndex !=
            weaponIndex ||
        static_cast<std::int32_t>(
            g_vrManualGrenade.pendingUntilMilliseconds -
            nowMilliseconds) <= 0)
    {
        return false;
    }

    std::memcpy(
        releaseOrigin,
        g_vrManualGrenade.releaseOrigin,
        sizeof(g_vrManualGrenade.releaseOrigin));

    std::memcpy(
        releaseVelocity,
        g_vrManualGrenade.releaseVelocity,
        sizeof(g_vrManualGrenade.releaseVelocity));

    std::memcpy(
        releaseFallbackForward,
        g_vrManualGrenade.releaseFallbackForward,
        sizeof(g_vrManualGrenade.releaseFallbackForward));

    *velocitySampleAgeMilliseconds =
        g_vrManualGrenade
            .releaseVelocitySampleAgeMilliseconds;

    *releaseAgeMilliseconds =
        nowMilliseconds -
        g_vrManualGrenade.releasedAtMilliseconds;

    VR_ResetManualGrenadeInteractionLocked();

    // Keep the primary firearm selected visually through the native offhand
    // recovery frames after the projectile has been committed.
    g_vrManualGrenade.viewOverrideUntilMilliseconds =
        nowMilliseconds + 750u;

    return true;
}


bool VR_LoadWeaponProfilesFromDisk(
    VrWeaponProfiles::Document* const document,
    std::string* const revision,
    std::string* const error)
{
    if (document == nullptr || revision == nullptr)
    {
        return false;
    }

    const char* const path =
        std::getenv("KISAK_VR_WEAPON_PROFILES_PATH");
    if (path == nullptr || path[0] == '\0')
    {
        *document = VrWeaponProfiles::DefaultDocument();
        *revision = VrWeaponProfiles::DocumentRevision(*document);
        if (error != nullptr)
        {
            *error = "no profile path supplied; using zeroed generic profile";
        }
        return true;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        *document = VrWeaponProfiles::DefaultDocument();
        *revision = VrWeaponProfiles::DocumentRevision(*document);
        if (error != nullptr)
        {
            error->clear();
        }
        return true;
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    std::string parseError;
    VrWeaponProfiles::Document parsed;
    if (!VrWeaponProfiles::ParseDocument(
            contents.str(),
            &parsed,
            &parseError))
    {
        if (error != nullptr)
        {
            *error = parseError;
        }
        return false;
    }

    *revision = VrWeaponProfiles::DocumentRevision(parsed);
    *document = std::move(parsed);
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool VR_ReloadWeaponProfiles()
{
    VrWeaponProfiles::Document loaded;
    std::string revision;
    std::string error;
    const bool success = VR_LoadWeaponProfilesFromDisk(
        &loaded,
        &revision,
        &error);

    std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
    g_vrWeaponProfilesLoaded = true;
    g_vrWeaponProfilesLoadError = error;
    if (success)
    {
        g_vrWeaponProfiles = std::move(loaded);
        g_vrWeaponProfilesRevision = revision;
        g_vrLastWeaponStatusSignature.clear();
    }
    return success;
}

void VR_EnsureWeaponProfilesLoaded()
{
    {
        std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
        if (g_vrWeaponProfilesLoaded)
        {
            return;
        }
    }
    const bool success = VR_ReloadWeaponProfiles();
    std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
    if (success)
    {
        Com_Printf(
            0,
                "[VR][WEAPON PROFILE] beta.13 loaded revision %s; %u weapon "
            "override(s), %u gunstock profile(s), active '%s'.\n",
            g_vrWeaponProfilesRevision.c_str(),
            static_cast<unsigned int>(g_vrWeaponProfiles.weapons.size()),
            static_cast<unsigned int>(g_vrWeaponProfiles.gunstocks.size()),
            g_vrWeaponProfiles.activeGunstockId.c_str());
    }
    else
    {
        Com_PrintWarning(
            0,
            "[VR][WEAPON PROFILE] Rejected profile file: %s. "
            "Global weapon calibration remains active.\n",
            g_vrWeaponProfilesLoadError.c_str());
    }
}

VrWeaponProfiles::EffectiveCalibration
VR_ResolveEffectiveWeaponCalibration(
    const char* const weaponId,
    const float shoulderedBlend)
{
    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();
    VrWeaponProfiles::Pose global;
    for (std::size_t component = 0u; component < 3u; ++component)
    {
        global.offset[component] = configurable.weaponOffset[component];
        global.angles[component] = configurable.weaponAngles[component];
    }

    VR_EnsureWeaponProfilesLoaded();
    std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
    return VrWeaponProfiles::Resolve(
        g_vrWeaponProfiles,
        configurable.weaponProfilesEnabled &&
            g_vrWeaponProfilesLoadError.empty(),
        global,
        weaponId != nullptr ? weaponId : "unknown",
        shoulderedBlend);
}

void VR_WriteWeaponCalibrationStatusAtomic(
    const VrWeaponProfiles::RuntimeStatus& status)
{
    const char* const path =
        std::getenv("KISAK_VR_WEAPON_CALIBRATION_STATUS_PATH");
    if (path == nullptr || path[0] == '\0')
    {
        return;
    }
    const std::string temporaryPath = std::string(path) + ".tmp";
    {
        std::ofstream output(
            temporaryPath,
            std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return;
        }
        output << VrWeaponProfiles::SerializeRuntimeStatus(status);
        output.flush();
        if (!output)
        {
            return;
        }
    }
    if (!MoveFileExA(
            temporaryPath.c_str(),
            path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::remove(temporaryPath.c_str());
    }
}

void VR_AppendWeaponCalibrationReceipt(
    const VrWeaponProfiles::RuntimeStatus& status)
{
    const char* const receiptPath =
        std::getenv("KISAK_VR_SETTINGS_RECEIPT_PATH");
    if (receiptPath == nullptr || receiptPath[0] == '\0')
    {
        return;
    }
    FILE* receipt = nullptr;
    if (fopen_s(&receipt, receiptPath, "ab") != 0 || receipt == nullptr)
    {
        return;
    }
    const VrMeasurementUnitSystem units =
        VR_GetConfiguratorSettings().measurementUnits;
    std::fprintf(
        receipt,
        "\r\nSTATUS=RUNTIME_WEAPON_PROFILE_APPLIED\r\n"
        "RUNTIME_WEAPON_INDEX=%d\r\n"
        "RUNTIME_WEAPON_ID=%s\r\n"
        "RUNTIME_WEAPON_NAME=%s\r\n"
        "RUNTIME_WEAPON_PROFILE_REVISION=%s\r\n"
        "RUNTIME_WEAPON_GUNSTOCK=%s\r\n"
        "RUNTIME_WEAPON_OVERRIDE=%d\r\n"
        "RUNTIME_WEAPON_SHOULDER_MODE=%d\r\n"
        "RUNTIME_WEAPON_EFFECTIVE_OFFSET=%.2f %.2f %.2f\r\n"
        "RUNTIME_WEAPON_EFFECTIVE_OFFSET_DISPLAY=%.2f %.2f %.2f %s\r\n"
        "RUNTIME_WEAPON_EFFECTIVE_ANGLES=%.1f %.1f %.1f\r\n",
        status.weaponIndex,
        status.weaponId.c_str(),
        status.weaponName.c_str(),
        status.profileRevision.c_str(),
        status.activeGunstockId.c_str(),
        status.effective.weaponOverrideApplied ? 1 : 0,
        status.effective.shoulderedBlend >= 0.5f ? 1 : 0,
        status.effective.pose.offset[0],
        status.effective.pose.offset[1],
        status.effective.pose.offset[2],
        VR_DisplayInches(status.effective.pose.offset[0], units),
        VR_DisplayInches(status.effective.pose.offset[1], units),
        VR_DisplayInches(status.effective.pose.offset[2], units),
        VR_DisplayLengthUnit(units),
        status.effective.pose.angles[0],
        status.effective.pose.angles[1],
        status.effective.pose.angles[2]);
    std::fflush(receipt);
    std::fclose(receipt);
}

void VR_PublishWeaponCalibrationStatus(
    const int weaponIndex,
    const char* const weaponId,
    const char* const weaponName,
    const bool shouldered)
{
    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(Sys_Milliseconds());
    if (static_cast<std::int32_t>(
            g_vrWeaponStatusHoldUntilMilliseconds - nowMilliseconds) > 0)
    {
        return;
    }

    VrWeaponProfiles::RuntimeStatus status;
    status.status = "active";
    status.message = shouldered
        ? "Shouldered/ADS calibration active"
        : "Hip-fire calibration active";
    status.weaponIndex = weaponIndex;
    status.weaponId = weaponId != nullptr ? weaponId : "unknown";
    status.weaponName = weaponName != nullptr ? weaponName : status.weaponId;
    status.effective = VR_ResolveEffectiveWeaponCalibration(
        status.weaponId.c_str(),
        shouldered ? 1.0f : 0.0f);

    std::string signature;
    {
        std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
        status.activeGunstockId = g_vrWeaponProfiles.activeGunstockId;
        status.profileRevision = g_vrWeaponProfilesRevision;
        signature = status.weaponId + "|" + status.activeGunstockId + "|" +
            status.profileRevision + "|" + (shouldered ? "1" : "0");
        if (signature == g_vrLastWeaponStatusSignature)
        {
            return;
        }
        g_vrLastWeaponStatusSignature = signature;
    }

    {
        std::lock_guard<std::mutex> lock(g_vrWeaponControllerPoseMutex);
        g_vrReportedRightControllerWeaponCalibration = false;
    }

    VR_WriteWeaponCalibrationStatusAtomic(status);
    VR_AppendWeaponCalibrationReceipt(status);
    Com_Printf(
        0,
        "[VR][WEAPON PROFILE][APPLY] %s (%d): %s; gunstock '%s'; "
        "offset %.2f %.2f %.2f; angles %.1f %.1f %.1f.\n",
        status.weaponId.c_str(),
        status.weaponIndex,
        shouldered ? "shouldered/ADS" : "hip-fire",
        status.activeGunstockId.c_str(),
        status.effective.pose.offset[0],
        status.effective.pose.offset[1],
        status.effective.pose.offset[2],
        status.effective.pose.angles[0],
        status.effective.pose.angles[1],
        status.effective.pose.angles[2]);
}

bool VR_ApplyRightControllerToWeaponPlacement(
    const int weaponIndex,
    const char* const weaponId,
    const char* const weaponName,
    const float adsFraction,
    const float cameraOrigin[3],
    const float cameraAxis[3][3],
    float weaponOrigin[3],
    float weaponAxis[3][3])
{
    if (weaponIndex <= 0 || weaponIndex >= 128 ||
        cameraOrigin == nullptr ||
        cameraAxis == nullptr ||
        weaponOrigin == nullptr ||
        weaponAxis == nullptr)
    {
        return false;
    }

    float currentPosition[3] = {};
    float currentAxis[3][3] = {};
    float attachmentPosition[3] = {};
    float attachmentAxis[3][3] = {};
    float baseAttachmentAxis[3][3] = {};
    bool attachmentValid = false;

    float leftForegripPosition[3] = {};
    float twoHandBlend = 0.0f;
    std::string safeWeaponId = VrWeaponProfiles::NormalizeId(
        weaponId != nullptr ? weaponId : "");
    if (!VrWeaponProfiles::IsSafeId(safeWeaponId))
    {
        safeWeaponId = "weapon_" + std::to_string(weaponIndex);
    }

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrRightControllerWeaponPoseValid)
        {
            return false;
        }

        memcpy(
            currentPosition,
            g_vrRightControllerWeaponPosition,
            sizeof(currentPosition));

        memcpy(
            currentAxis,
            g_vrRightControllerWeaponAxis,
            sizeof(currentAxis));

        memcpy(
            leftForegripPosition,
            g_vrLeftControllerForegripPosition,
            sizeof(leftForegripPosition));

        twoHandBlend =
            g_vrTwoHandWeaponBlend;

        VrWeaponAttachmentBaseline& baseline =
            g_vrWeaponAttachmentBaselines[
                static_cast<std::size_t>(weaponIndex)];
        if (baseline.valid && baseline.weaponId != safeWeaponId)
        {
            baseline = {};
        }
        attachmentValid = baseline.valid;

        if (attachmentValid)
        {
            memcpy(
                attachmentPosition,
                baseline.position,
                sizeof(attachmentPosition));

            memcpy(
                attachmentAxis,
                baseline.axis,
                sizeof(attachmentAxis));
        }
    }

    if (!attachmentValid)
    {
        float weaponOriginCameraLocal[3] = {};
        float weaponAxisCameraLocal[3][3] = {};

        const float originDeltaWorld[3] = {
            weaponOrigin[0] - cameraOrigin[0],
            weaponOrigin[1] - cameraOrigin[1],
            weaponOrigin[2] - cameraOrigin[2],
        };

        for (int cameraComponent = 0;
             cameraComponent < 3;
             ++cameraComponent)
        {
            weaponOriginCameraLocal[cameraComponent] =
                originDeltaWorld[0] *
                    cameraAxis[cameraComponent][0] +
                originDeltaWorld[1] *
                    cameraAxis[cameraComponent][1] +
                originDeltaWorld[2] *
                    cameraAxis[cameraComponent][2];
        }

        for (int weaponAxisRow = 0;
             weaponAxisRow < 3;
             ++weaponAxisRow)
        {
            for (int cameraComponent = 0;
                 cameraComponent < 3;
                 ++cameraComponent)
            {
                weaponAxisCameraLocal[
                    weaponAxisRow][cameraComponent] =
                    weaponAxis[weaponAxisRow][0] *
                        cameraAxis[cameraComponent][0] +
                    weaponAxis[weaponAxisRow][1] *
                        cameraAxis[cameraComponent][1] +
                    weaponAxis[weaponAxisRow][2] *
                        cameraAxis[cameraComponent][2];
            }
        }

        const float controllerToWeaponCameraLocal[3] = {
            weaponOriginCameraLocal[0] -
                currentPosition[0],
            weaponOriginCameraLocal[1] -
                currentPosition[1],
            weaponOriginCameraLocal[2] -
                currentPosition[2],
        };

        // Calibrate against a canonical controller basis:
        // forward = camera forward, left = camera left, up = camera up.
        //
        // The old code projected these offsets through the controller's
        // orientation on the first rendered frame. That made the first
        // controller pose become a permanent neutral pose. A controller
        // pointed upward during startup therefore produced a lasting
        // orientation offset.
        //
        // Keeping the default viewmodel placement directly in canonical
        // controller-local coordinates makes later weapon orientation an
        // absolute function of the current controller pose instead.
        memcpy(
            attachmentPosition,
            controllerToWeaponCameraLocal,
            sizeof(attachmentPosition));

        memcpy(
            attachmentAxis,
            weaponAxisCameraLocal,
            sizeof(attachmentAxis));

        {
            std::lock_guard<std::mutex> lock(
                g_vrWeaponControllerPoseMutex);

            VrWeaponAttachmentBaseline& baseline =
                g_vrWeaponAttachmentBaselines[
                    static_cast<std::size_t>(weaponIndex)];
            if (!baseline.valid || baseline.weaponId != safeWeaponId)
            {
                memcpy(
                    baseline.position,
                    attachmentPosition,
                    sizeof(attachmentPosition));

                memcpy(
                    baseline.axis,
                    attachmentAxis,
                    sizeof(attachmentAxis));

                baseline.weaponId = safeWeaponId;
                baseline.valid = true;
            }
            else
            {
                memcpy(
                    attachmentPosition,
                    baseline.position,
                    sizeof(attachmentPosition));

                memcpy(
                    attachmentAxis,
                    baseline.axis,
                    sizeof(attachmentAxis));
            }
        }

        if (!g_vrLoggedRightControllerWeaponCalibration)
        {
            Com_Printf(
                0,
                "[VR] Calibrated controller-independent per-weapon "
                "viewmodel attachment for %s (%d).\n",
                safeWeaponId.c_str(),
                weaponIndex);

            g_vrLoggedRightControllerWeaponCalibration = true;
        }
    }

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    std::memcpy(
        baseAttachmentAxis,
        attachmentAxis,
        sizeof(baseAttachmentAxis));

    const float shoulderedBlend = std::clamp(
        (std::max)(twoHandBlend, adsFraction),
        0.0f,
        1.0f);
    const VrWeaponProfiles::EffectiveCalibration effective =
        VR_ResolveEffectiveWeaponCalibration(
            safeWeaponId.c_str(),
            shoulderedBlend);

    for (int component = 0; component < 3; ++component)
    {
        attachmentPosition[component] +=
            effective.pose.offset[static_cast<std::size_t>(component)];
    }

    float configurableRotation[3][3] = {};
    AnglesToAxis(
        effective.pose.angles.data(),
        configurableRotation);

    float adjustedAttachmentAxis[3][3] = {};
    for (int weaponAxisRow = 0; weaponAxisRow < 3; ++weaponAxisRow)
    {
        for (int controllerComponent = 0;
             controllerComponent < 3;
             ++controllerComponent)
        {
            adjustedAttachmentAxis[weaponAxisRow][controllerComponent] =
                attachmentAxis[weaponAxisRow][0] *
                    configurableRotation[0][controllerComponent] +
                attachmentAxis[weaponAxisRow][1] *
                    configurableRotation[1][controllerComponent] +
                attachmentAxis[weaponAxisRow][2] *
                    configurableRotation[2][controllerComponent];
        }
    }

    std::memcpy(
        attachmentAxis,
        adjustedAttachmentAxis,
        sizeof(adjustedAttachmentAxis));

    static bool loggedConfiguratorWeaponCalibration = false;
    if (!loggedConfiguratorWeaponCalibration)
    {
        Com_Printf(
            0,
            "[VR][CONFIG] Right weapon calibration: offset %.2f %.2f "
            "%.2f %s; pitch/yaw/roll %.1f %.1f %.1f; response %.2f/%.2f.\n",
            VR_DisplayInches(
                effective.pose.offset[0],
                configurable.measurementUnits),
            VR_DisplayInches(
                effective.pose.offset[1],
                configurable.measurementUnits),
            VR_DisplayInches(
                effective.pose.offset[2],
                configurable.measurementUnits),
            VR_DisplayLengthUnit(configurable.measurementUnits),
            effective.pose.angles[0],
            effective.pose.angles[1],
            effective.pose.angles[2],
            configurable.weaponPositionResponse,
            configurable.weaponOrientationResponse);

        loggedConfiguratorWeaponCalibration = true;
    }

    if (twoHandBlend > 0.001f)
    {
        float twoHandForward[3] = {
            leftForegripPosition[0] -
                currentPosition[0],
            leftForegripPosition[1] -
                currentPosition[1],
            leftForegripPosition[2] -
                currentPosition[2],
        };

        const float forwardLength =
            std::sqrt(
                twoHandForward[0] *
                    twoHandForward[0] +
                twoHandForward[1] *
                    twoHandForward[1] +
                twoHandForward[2] *
                    twoHandForward[2]);

        if (forwardLength > 0.0001f)
        {
            twoHandForward[0] /= forwardLength;
            twoHandForward[1] /= forwardLength;
            twoHandForward[2] /= forwardLength;

            const float upAlongForward =
                currentAxis[2][0] *
                    twoHandForward[0] +
                currentAxis[2][1] *
                    twoHandForward[1] +
                currentAxis[2][2] *
                    twoHandForward[2];

            float twoHandUp[3] = {
                currentAxis[2][0] -
                    upAlongForward *
                    twoHandForward[0],
                currentAxis[2][1] -
                    upAlongForward *
                    twoHandForward[1],
                currentAxis[2][2] -
                    upAlongForward *
                    twoHandForward[2],
            };

            float upLength =
                std::sqrt(
                    twoHandUp[0] * twoHandUp[0] +
                    twoHandUp[1] * twoHandUp[1] +
                    twoHandUp[2] * twoHandUp[2]);

            if (upLength <= 0.0001f)
            {
                twoHandUp[0] = currentAxis[1][1] *
                                   twoHandForward[2] -
                               currentAxis[1][2] *
                                   twoHandForward[1];

                twoHandUp[1] = currentAxis[1][2] *
                                   twoHandForward[0] -
                               currentAxis[1][0] *
                                   twoHandForward[2];

                twoHandUp[2] = currentAxis[1][0] *
                                   twoHandForward[1] -
                               currentAxis[1][1] *
                                   twoHandForward[0];

                upLength =
                    std::sqrt(
                        twoHandUp[0] *
                            twoHandUp[0] +
                        twoHandUp[1] *
                            twoHandUp[1] +
                        twoHandUp[2] *
                            twoHandUp[2]);
            }

            if (upLength > 0.0001f)
            {
                twoHandUp[0] /= upLength;
                twoHandUp[1] /= upLength;
                twoHandUp[2] /= upLength;

                float twoHandLeft[3] = {
                    twoHandUp[1] *
                        twoHandForward[2] -
                    twoHandUp[2] *
                        twoHandForward[1],
                    twoHandUp[2] *
                        twoHandForward[0] -
                    twoHandUp[0] *
                        twoHandForward[2],
                    twoHandUp[0] *
                        twoHandForward[1] -
                    twoHandUp[1] *
                        twoHandForward[0],
                };

                const float leftLength =
                    std::sqrt(
                        twoHandLeft[0] *
                            twoHandLeft[0] +
                        twoHandLeft[1] *
                            twoHandLeft[1] +
                        twoHandLeft[2] *
                            twoHandLeft[2]);

                if (leftLength > 0.0001f)
                {
                    twoHandLeft[0] /= leftLength;
                    twoHandLeft[1] /= leftLength;
                    twoHandLeft[2] /= leftLength;

                    const float blend =
                        std::clamp(
                            twoHandBlend,
                            0.0f,
                            1.0f);

                    float blendedForward[3] = {
                        currentAxis[0][0] *
                                (1.0f - blend) +
                            twoHandForward[0] *
                                blend,
                        currentAxis[0][1] *
                                (1.0f - blend) +
                            twoHandForward[1] *
                                blend,
                        currentAxis[0][2] *
                                (1.0f - blend) +
                            twoHandForward[2] *
                                blend,
                    };

                    float blendedUpHint[3] = {
                        currentAxis[2][0] *
                                (1.0f - blend) +
                            twoHandUp[0] *
                                blend,
                        currentAxis[2][1] *
                                (1.0f - blend) +
                            twoHandUp[1] *
                                blend,
                        currentAxis[2][2] *
                                (1.0f - blend) +
                            twoHandUp[2] *
                                blend,
                    };

                    const float blendedForwardLength =
                        std::sqrt(
                            blendedForward[0] *
                                blendedForward[0] +
                            blendedForward[1] *
                                blendedForward[1] +
                            blendedForward[2] *
                                blendedForward[2]);

                    if (blendedForwardLength > 0.0001f)
                    {
                        blendedForward[0] /=
                            blendedForwardLength;

                        blendedForward[1] /=
                            blendedForwardLength;

                        blendedForward[2] /=
                            blendedForwardLength;

                        const float blendedUpAlongForward =
                            blendedUpHint[0] *
                                blendedForward[0] +
                            blendedUpHint[1] *
                                blendedForward[1] +
                            blendedUpHint[2] *
                                blendedForward[2];

                        blendedUpHint[0] -=
                            blendedUpAlongForward *
                            blendedForward[0];

                        blendedUpHint[1] -=
                            blendedUpAlongForward *
                            blendedForward[1];

                        blendedUpHint[2] -=
                            blendedUpAlongForward *
                            blendedForward[2];

                        const float blendedUpLength =
                            std::sqrt(
                                blendedUpHint[0] *
                                    blendedUpHint[0] +
                                blendedUpHint[1] *
                                    blendedUpHint[1] +
                                blendedUpHint[2] *
                                    blendedUpHint[2]);

                        if (blendedUpLength > 0.0001f)
                        {
                            blendedUpHint[0] /=
                                blendedUpLength;

                            blendedUpHint[1] /=
                                blendedUpLength;

                            blendedUpHint[2] /=
                                blendedUpLength;

                            float blendedLeft[3] = {
                                blendedUpHint[1] *
                                    blendedForward[2] -
                                blendedUpHint[2] *
                                    blendedForward[1],
                                blendedUpHint[2] *
                                    blendedForward[0] -
                                blendedUpHint[0] *
                                    blendedForward[2],
                                blendedUpHint[0] *
                                    blendedForward[1] -
                                blendedUpHint[1] *
                                    blendedForward[0],
                            };

                            const float blendedLeftLength =
                                std::sqrt(
                                    blendedLeft[0] *
                                        blendedLeft[0] +
                                    blendedLeft[1] *
                                        blendedLeft[1] +
                                    blendedLeft[2] *
                                        blendedLeft[2]);

                            if (blendedLeftLength > 0.0001f)
                            {
                                blendedLeft[0] /=
                                    blendedLeftLength;

                                blendedLeft[1] /=
                                    blendedLeftLength;

                                blendedLeft[2] /=
                                    blendedLeftLength;

                                float blendedUp[3] = {
                                    blendedForward[1] *
                                        blendedLeft[2] -
                                    blendedForward[2] *
                                        blendedLeft[1],
                                    blendedForward[2] *
                                        blendedLeft[0] -
                                    blendedForward[0] *
                                        blendedLeft[2],
                                    blendedForward[0] *
                                        blendedLeft[1] -
                                    blendedForward[1] *
                                        blendedLeft[0],
                                };

                                memcpy(
                                    currentAxis[0],
                                    blendedForward,
                                    sizeof(blendedForward));

                                memcpy(
                                    currentAxis[1],
                                    blendedLeft,
                                    sizeof(blendedLeft));

                                memcpy(
                                    currentAxis[2],
                                    blendedUp,
                                    sizeof(blendedUp));

                                static bool
                                    loggedTwoHandWeaponApply =
                                        false;

                                if (!loggedTwoHandWeaponApply)
                                {
                                    Com_Printf(
                                        0,
                                        "[VR] Applied optional two-hand "
                                        "foregrip stabilization.\n");

                                    loggedTwoHandWeaponApply =
                                        true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    float calibrationCameraLocal[3] = {};
    kisak::vr::weapon_calibration::ControllerLocalOffsetToCameraLocal(
        currentAxis,
        effective.pose.offset.data(),
        calibrationCameraLocal);

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        std::memcpy(
            g_vrRightControllerWeaponCalibrationCameraLocal,
            calibrationCameraLocal,
            sizeof(calibrationCameraLocal));

        g_vrRightControllerWeaponCalibrationValid = true;
    }

    float weaponOriginCameraLocal[3] = {};

    for (int cameraComponent = 0;
         cameraComponent < 3;
         ++cameraComponent)
    {
        weaponOriginCameraLocal[cameraComponent] =
            currentPosition[cameraComponent] +
            attachmentPosition[0] *
                currentAxis[0][cameraComponent] +
            attachmentPosition[1] *
                currentAxis[1][cameraComponent] +
            attachmentPosition[2] *
                currentAxis[2][cameraComponent];
    }

    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        weaponOrigin[worldComponent] =
            cameraOrigin[worldComponent] +
            weaponOriginCameraLocal[0] *
                cameraAxis[0][worldComponent] +
            weaponOriginCameraLocal[1] *
                cameraAxis[1][worldComponent] +
            weaponOriginCameraLocal[2] *
                cameraAxis[2][worldComponent];
    }

    float finalWeaponAxisCameraLocal[3][3] = {};

    for (int weaponAxisRow = 0;
         weaponAxisRow < 3;
         ++weaponAxisRow)
    {
        float weaponRowCameraLocal[3] = {};

        for (int cameraComponent = 0;
             cameraComponent < 3;
             ++cameraComponent)
        {
            weaponRowCameraLocal[cameraComponent] =
                attachmentAxis[weaponAxisRow][0] *
                    currentAxis[0][cameraComponent] +
                attachmentAxis[weaponAxisRow][1] *
                    currentAxis[1][cameraComponent] +
                attachmentAxis[weaponAxisRow][2] *
                    currentAxis[2][cameraComponent];
        }

        std::memcpy(
            finalWeaponAxisCameraLocal[weaponAxisRow],
            weaponRowCameraLocal,
            sizeof(weaponRowCameraLocal));

        for (int worldComponent = 0;
             worldComponent < 3;
             ++worldComponent)
        {
            weaponAxis[weaponAxisRow][worldComponent] =
                weaponRowCameraLocal[0] *
                    cameraAxis[0][worldComponent] +
                weaponRowCameraLocal[1] *
                    cameraAxis[1][worldComponent] +
                weaponRowCameraLocal[2] *
                    cameraAxis[2][worldComponent];
        }
    }

    const float forwardLength =
        std::sqrt(
            weaponAxis[0][0] * weaponAxis[0][0] +
            weaponAxis[0][1] * weaponAxis[0][1] +
            weaponAxis[0][2] * weaponAxis[0][2]);

    if (forwardLength > 0.0001f)
    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        g_vrRightControllerFinalWeaponForward[0] =
            weaponAxis[0][0] / forwardLength;

        g_vrRightControllerFinalWeaponForward[1] =
            weaponAxis[0][1] / forwardLength;

        g_vrRightControllerFinalWeaponForward[2] =
            weaponAxis[0][2] / forwardLength;

        std::memcpy(
            g_vrRightControllerFinalWeaponAxisCameraLocal,
            finalWeaponAxisCameraLocal,
            sizeof(finalWeaponAxisCameraLocal));

        g_vrRightControllerFinalWeaponAxisCameraLocalValid = true;
        g_vrRightControllerFinalWeaponAimValid = true;
    }

    {
        std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
        g_vrActiveCalibrationWeaponIndex = weaponIndex;
        g_vrActiveCalibrationWeaponId = safeWeaponId;
        g_vrActiveCalibrationWeaponName =
            weaponName != nullptr && weaponName[0] != '\0'
                ? weaponName
                : safeWeaponId;
        g_vrActiveEffectiveWeaponCalibration = effective;
        std::memcpy(
            g_vrActiveWeaponBaseAttachmentAxis,
            baseAttachmentAxis,
            sizeof(baseAttachmentAxis));
        std::memcpy(
            g_vrActiveWeaponControllerAxis,
            currentAxis,
            sizeof(currentAxis));
        g_vrActiveWeaponCapturePoseValid = true;
    }

    VR_PublishWeaponCalibrationStatus(
        weaponIndex,
        safeWeaponId.c_str(),
        weaponName,
        shoulderedBlend >= 0.5f);

    if (!g_vrLoggedRightControllerWeaponApply)
    {
        Com_Printf(
            0,
            "[VR] Applied absolute rigid weapon-controller "
            "weapon transform.\n");

        g_vrLoggedRightControllerWeaponApply = true;
    }

    return true;
}


void VR_PublishRightControllerWeaponMuzzleWorld(
    const float muzzleOrigin[3])
{
    if (muzzleOrigin == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    g_vrRightControllerFinalWeaponMuzzleWorld[0] =
        muzzleOrigin[0];

    g_vrRightControllerFinalWeaponMuzzleWorld[1] =
        muzzleOrigin[1];

    g_vrRightControllerFinalWeaponMuzzleWorld[2] =
        muzzleOrigin[2];

    g_vrRightControllerFinalWeaponMuzzleValid = true;
}

void VR_SetRightControllerWeaponMuzzleBlocked(
    const bool blocked)
{
    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    g_vrRightControllerFinalWeaponMuzzleBlocked =
        blocked;
}

bool VR_ShouldSuppressRightControllerBlockedMuzzleShot()
{
    if (!VR_GetConfiguratorSettings().muzzleObstruction)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    return
        g_vrRightControllerFinalWeaponAimValid &&
        g_vrRightControllerAttackPressed &&
        g_vrRightControllerFinalWeaponMuzzleBlocked;
}


bool VR_GetRightControllerWeaponMuzzleWorld(
    float muzzleOrigin[3])
{
    if (muzzleOrigin == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    if (!g_vrRightControllerFinalWeaponMuzzleValid)
    {
        return false;
    }

    muzzleOrigin[0] =
        g_vrRightControllerFinalWeaponMuzzleWorld[0];

    muzzleOrigin[1] =
        g_vrRightControllerFinalWeaponMuzzleWorld[1];

    muzzleOrigin[2] =
        g_vrRightControllerFinalWeaponMuzzleWorld[2];

    return true;
}


bool VR_ApplyControllerRoleHaptic(
    const bool weaponHand,
    const float amplitude,
    const float durationSeconds)
{
    if (!g_vrSessionRunning)
    {
        return false;
    }

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();
    const float clampedAmplitude =
        VrInteractions::EffectiveHapticAmplitude(
            configurable.hapticsEnabled,
            amplitude,
            configurable.hapticStrength);

    if (clampedAmplitude <= 0.0f ||
        durationSeconds <= 0.0f)
    {
        return false;
    }

    const std::uint32_t controllerIndex =
        weaponHand
            ? VrInteractions::WeaponControllerIndex(
                  configurable.dominantHand)
            : VrInteractions::OffHandControllerIndex(
                  configurable.dominantHand);

    if (g_vrRuntimeBackend == VrRuntimeBackend::OpenVr)
    {
        const VrInput::OpenVrHandState& hand =
            g_vrOpenVrHands[controllerIndex];

        if (g_vrOpenVrSystem == nullptr ||
            !hand.stateValid ||
            hand.deviceIndex ==
                vr::k_unTrackedDeviceIndexInvalid)
        {
            return false;
        }

        const float requestedMicroseconds =
            durationSeconds * 1000000.0f *
            clampedAmplitude;
        const unsigned short pulseMicroseconds =
            static_cast<unsigned short>(
                (std::max)(
                    1.0f,
                    (std::min)(3999.0f, requestedMicroseconds)));

        g_vrOpenVrSystem->TriggerHapticPulse(
            hand.deviceIndex,
            0u,
            pulseMicroseconds);

        return true;
    }

    if (!g_vrControllerActionsAttached ||
        g_vrSession == XR_NULL_HANDLE ||
        g_vrHapticOutputAction == XR_NULL_HANDLE ||
        g_vrControllerHandPaths[controllerIndex] ==
            XR_NULL_PATH)
    {
        return false;
    }

    XrHapticActionInfo actionInfo{
        XR_TYPE_HAPTIC_ACTION_INFO
    };

    actionInfo.action =
        g_vrHapticOutputAction;

    actionInfo.subactionPath =
        g_vrControllerHandPaths[controllerIndex];

    XrHapticVibration vibration{
        XR_TYPE_HAPTIC_VIBRATION
    };

    vibration.amplitude =
        clampedAmplitude;

    vibration.duration =
        static_cast<XrDuration>(
            static_cast<double>(durationSeconds) *
            1000000000.0);

    vibration.frequency =
        XR_FREQUENCY_UNSPECIFIED;

    const XrResult result =
        xrApplyHapticFeedback(
            g_vrSession,
            &actionInfo,
            reinterpret_cast<
                const XrHapticBaseHeader*>(
                    &vibration));

    if (XR_FAILED(result))
    {
        static bool loggedHapticFailure = false;

        if (!loggedHapticFailure)
        {
            VR_LogXrFailure(
                "xrApplyHapticFeedback",
                result);

            loggedHapticFailure = true;
        }

        return false;
    }

    static bool loggedFirstWeaponHaptic = false;
    static bool loggedFirstOffhandHaptic = false;
    bool& logged = weaponHand
        ? loggedFirstWeaponHaptic
        : loggedFirstOffhandHaptic;

    if (!logged)
    {
        Com_Printf(
            0,
            "[VR][INTERACTIONS] Applied %s-controller haptic to "
            "the physical %s hand.\n",
            weaponHand ? "weapon" : "off-hand",
            controllerIndex == VR_CONTROLLER_LEFT ? "left" : "right");

        logged = true;
    }

    return true;
}

bool VR_ApplyRightControllerWeaponHaptic(
    const float amplitude,
    const float durationSeconds)
{
    return VR_ApplyControllerRoleHaptic(
        true,
        amplitude,
        durationSeconds);
}

bool VR_ApplyOffhandControllerHaptic(
    const float amplitude,
    const float durationSeconds)
{
    return VR_ApplyControllerRoleHaptic(
        false,
        amplitude,
        durationSeconds);
}


bool VR_GetCampaignAdsHeld(
    bool* const adsHeld)
{
    if (adsHeld == nullptr)
    {
        return false;
    }

    *adsHeld = false;

    if (!g_vrInitialized)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_vrHeadOrientationMutex);

    *adsHeld =
        g_vrPoseFocusAimHeld;

    return true;
}

bool VR_GetBasicGameplayButtons(
    bool* adsHeld,
    bool* jumpHeld,
    bool* useHeld,
    bool* reloadHeld)
{
    if (adsHeld == nullptr ||
        jumpHeld == nullptr ||
        useHeld == nullptr ||
        reloadHeld == nullptr)
    {
        return false;
    }

    bool rawReloadButtonHeld = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        *adsHeld =
            g_vrPoseFocusAimHeld;

        *jumpHeld =
            g_vrLeftTriggerJumpHeld;

        *useHeld =
            g_vrLeftXUseHeld;

        rawReloadButtonHeld =
            g_vrRightAButtonHeld;
    }

    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(
            Sys_Milliseconds());

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        const bool manualReloadActive =
            g_vrManualMagazineReload.enabled &&
            g_vrManualMagazineReload.supported;

        const bool manualCommitActive =
            manualReloadActive &&
            g_vrManualMagazineReload
                    .commitUntilMilliseconds != 0u &&
            static_cast<std::int32_t>(
                g_vrManualMagazineReload
                    .commitUntilMilliseconds -
                nowMilliseconds) > 0;

        // Unsupported weapons use right A for the original native reload.
        // Supported magazines emit BUTTON_RELOAD only after insertion.
        *reloadHeld =
            manualReloadActive
                ? manualCommitActive
                : rawReloadButtonHeld;
    }

    return
        *adsHeld ||
        *jumpHeld ||
        *useHeld ||
        *reloadHeld;
}

bool VR_GetLocomotionCombatButtons(
    bool* sprintHeld,
    bool* meleeHeld,
    bool* stanceHeld)
{
    if (sprintHeld == nullptr ||
        meleeHeld == nullptr ||
        stanceHeld == nullptr)
    {
        return false;
    }

    bool configuredMeleeHeld = false;
    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        *sprintHeld = g_vrLeftStickSprintHeld;
        configuredMeleeHeld = g_vrRightStickMeleeHeld;
        *stanceHeld = g_vrRightBStanceHeld;
    }

    const VrInteractions::MeleeMode mode =
        VR_GetConfiguratorSettings().meleeMode;
    bool physicalMeleeHeld = false;
    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);
        const std::uint32_t nowMilliseconds =
            static_cast<std::uint32_t>(Sys_Milliseconds());
        physicalMeleeHeld =
            static_cast<std::int32_t>(
                g_vrPhysicalMeleePulseUntilMilliseconds -
                nowMilliseconds) > 0;
    }

    *meleeHeld =
        mode == VrInteractions::MeleeMode::Button
            ? configuredMeleeHeld
            : (mode == VrInteractions::MeleeMode::Gesture
                   ? physicalMeleeHeld
                   : configuredMeleeHeld || physicalMeleeHeld);

    return true;
}

bool VR_GetLowerStanceButton(
    bool* lowerHeld)
{
    if (lowerHeld == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_vrHeadOrientationMutex);

    *lowerHeld = g_vrLowerStanceHeld;

    return true;
}

bool VR_GetWeaponUtilityButtons(
    bool* offhandHeld,
    bool* leftYHeld)
{
    if (offhandHeld == nullptr ||
        leftYHeld == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_vrHeadOrientationMutex);

    *offhandHeld =
        g_vrNativeOffhandHeld;

    *leftYHeld =
        g_vrLeftYNextWeaponHeld;

    return true;
}

bool VR_GetTurnYawDelta(
    const float elapsedSeconds,
    float* yawDeltaDegrees)
{
    if (yawDeltaDegrees == nullptr)
    {
        return false;
    }

    *yawDeltaDegrees = 0.0f;

    std::lock_guard<std::mutex> lock(
        g_vrHeadOrientationMutex);

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    // KISAK_SP_VR_SMOOTH_TURN_OPTION_V50
    // VR-Settings.bat is loaded before process creation, so turning settings
    // are immutable for this run. Read them once on the gameplay thread.
    if (!g_vrTurnSettingsLoaded)
    {
        constexpr float defaultSmoothSpeed =
            120.0f;

        constexpr float minimumSmoothSpeed =
            30.0f;

        constexpr float maximumSmoothSpeed =
            360.0f;

        const char* requestedMode =
            std::getenv("KISAK_VR_TURN_MODE");

        if (requestedMode != nullptr &&
            requestedMode[0] != '\0')
        {
            if (_stricmp(requestedMode, "smooth") == 0 ||
                std::strcmp(requestedMode, "1") == 0)
            {
                g_vrTurnMode =
                    VrTurnMode::Smooth;
            }
            else if (_stricmp(requestedMode, "snap") != 0 &&
                     std::strcmp(requestedMode, "0") != 0)
            {
                Com_PrintWarning(
                    0,
                    "[VR][CONTROLS] Ignoring invalid "
                    "KISAK_VR_TURN_MODE='%s'; using snap. "
                    "Valid values are snap and smooth.\n",
                    requestedMode);
            }
        }

        g_vrSmoothTurnSpeedDegreesPerSecond =
            defaultSmoothSpeed;

        const char* requestedSmoothSpeed =
            std::getenv(
                "KISAK_VR_SMOOTH_TURN_SPEED");

        if (requestedSmoothSpeed != nullptr &&
            requestedSmoothSpeed[0] != '\0')
        {
            char* parseEnd = nullptr;

            const float parsedSmoothSpeed =
                std::strtof(
                    requestedSmoothSpeed,
                    &parseEnd);

            if (parseEnd == requestedSmoothSpeed ||
                parseEnd == nullptr ||
                parseEnd[0] != '\0' ||
                !std::isfinite(parsedSmoothSpeed) ||
                parsedSmoothSpeed < minimumSmoothSpeed ||
                parsedSmoothSpeed > maximumSmoothSpeed)
            {
                Com_PrintWarning(
                    0,
                    "[VR][CONTROLS] Ignoring invalid "
                    "KISAK_VR_SMOOTH_TURN_SPEED='%s'; "
                    "using %.0f. Valid range is %.0f through "
                    "%.0f degrees per second.\n",
                    requestedSmoothSpeed,
                    defaultSmoothSpeed,
                    minimumSmoothSpeed,
                    maximumSmoothSpeed);
            }
            else
            {
                g_vrSmoothTurnSpeedDegreesPerSecond =
                    parsedSmoothSpeed;
            }
        }

        if (g_vrTurnMode ==
            VrTurnMode::Smooth)
        {
            Com_Printf(
                0,
                "[VR][CONTROLS] V50 turn mode: smooth analog "
                "at %.0f degrees/second; right-stick deadzone "
                "%.2f.\n",
                g_vrSmoothTurnSpeedDegreesPerSecond,
                configurable.turnDeadzone);
        }
        else
        {
            Com_Printf(
                0,
                "[VR][CONTROLS] V56 turn mode: %.0f-degree "
                "snap with neutral latch rearm.\n",
                configurable.snapTurnAngleDegrees);
        }

        g_vrTurnSettingsLoaded = true;
    }

    if (!g_vrRightThumbstickValid)
    {
        g_vrSnapTurnArmed = true;
        return false;
    }

    // KISAK_SP_VR_SNAP_TURN_REARM_V52
    // Re-arm snap turning before classifying horizontal versus vertical
    // intent.  At neutral, both axes are zero, so the dominance margin below
    // otherwise classifies the centered stick as vertical and returns before
    // the latch can reset.  That regression made another snap possible only
    // through the narrow horizontal band between the dominance margin and
    // this release threshold.
    const float snapReleaseThreshold =
        (std::min)(0.65f, configurable.turnDeadzone + 0.10f);

    if (g_vrTurnMode ==
            VrTurnMode::Snap &&
        g_vrRightThumbstickX >
            -snapReleaseThreshold &&
        g_vrRightThumbstickX <
            snapReleaseThreshold)
    {
        g_vrSnapTurnArmed = true;
        return false;
    }

    // Turning consumes only deliberate horizontal axis motion. Vertical
    // movement is ignored here; Controller Input V4 routes the default
    // right-axis up gesture through Jump and right-axis down through Lower
    // stance step.
    if (std::abs(g_vrRightThumbstickX) <
        std::abs(g_vrRightThumbstickY) + 0.15f)
    {
        return false;
    }

    if (g_vrTurnMode ==
        VrTurnMode::Smooth)
    {
        g_vrSnapTurnArmed = true;

        const float smoothDeadzone =
            configurable.turnDeadzone;

        const float absoluteStickX =
            std::abs(g_vrRightThumbstickX);

        if (absoluteStickX <=
                smoothDeadzone ||
            !std::isfinite(elapsedSeconds) ||
            elapsedSeconds <= 0.0f)
        {
            return false;
        }

        // Linearly remap the usable stick range so motion starts at zero just
        // outside the deadzone and reaches the configured speed at full tilt.
        const float normalizedMagnitude =
            (std::min)(
                1.0f,
                (absoluteStickX - smoothDeadzone) /
                    (1.0f - smoothDeadzone));

        const float turnDirection =
            g_vrRightThumbstickX > 0.0f
                ? -1.0f
                : 1.0f;

        *yawDeltaDegrees =
            turnDirection *
            normalizedMagnitude *
            g_vrSmoothTurnSpeedDegreesPerSecond *
            (std::min)(elapsedSeconds, 0.05f);

        return
            std::abs(*yawDeltaDegrees) >
            0.0001f;
    }

    const float engageThreshold =
        (std::max)(
            0.65f,
            (std::min)(
                0.90f,
                configurable.turnDeadzone + 0.50f));

    const float snapAngleDegrees =
        configurable.snapTurnAngleDegrees;

    if (!g_vrSnapTurnArmed)
    {
        return false;
    }

    if (g_vrRightThumbstickX >=
        engageThreshold)
    {
        // Positive CoD yaw turns left.
        *yawDeltaDegrees =
            -snapAngleDegrees;

        g_vrSnapTurnArmed = false;
        return true;
    }

    if (g_vrRightThumbstickX <=
        -engageThreshold)
    {
        *yawDeltaDegrees =
            snapAngleDegrees;

        g_vrSnapTurnArmed = false;
        return true;
    }

    return false;
}

bool VR_TransferHmdYawToBody(
    float* bodyYawDeltaDegrees)
{
    if (bodyYawDeltaDegrees == nullptr)
    {
        return false;
    }

    *bodyYawDeltaDegrees = 0.0f;

    std::lock_guard<std::mutex> lock(
        g_vrHeadOrientationMutex);

    if (!g_vrHeadOrientationValid)
    {
        return false;
    }

    const float horizontalForward =
        g_vrHeadOrientationAxis[0][0];

    const float horizontalLeft =
        g_vrHeadOrientationAxis[0][1];

    const float horizontalLength =
        std::sqrt(
            horizontalForward *
                horizontalForward +
            horizontalLeft *
                horizontalLeft);

    if (horizontalLength <= 0.0001f)
    {
        return false;
    }

    constexpr float radiansToDegrees =
        57.295779513082320876f;

    const float yawDeltaDegrees =
        std::atan2(
            horizontalLeft,
            horizontalForward) *
        radiansToDegrees;

    // Ignore sub-degree tracking noise, but transfer meaningful physical
    // turns continuously while sprint is held.
    if (std::fabs(yawDeltaDegrees) < 0.25f)
    {
        return false;
    }

    g_vrTransferredBodyYawDegrees +=
        yawDeltaDegrees;

    g_vrHeadPositionBodyYawDegrees +=
        yawDeltaDegrees;

    while (g_vrTransferredBodyYawDegrees >=
           180.0f)
    {
        g_vrTransferredBodyYawDegrees -=
            360.0f;
    }

    while (g_vrTransferredBodyYawDegrees <
           -180.0f)
    {
        g_vrTransferredBodyYawDegrees +=
            360.0f;
    }

    while (g_vrHeadPositionBodyYawDegrees >=
           180.0f)
    {
        g_vrHeadPositionBodyYawDegrees -=
            360.0f;
    }

    while (g_vrHeadPositionBodyYawDegrees <
           -180.0f)
    {
        g_vrHeadPositionBodyYawDegrees +=
            360.0f;
    }

    constexpr float degreesToRadians =
        0.01745329251994329577f;

    const float yawDeltaRadians =
        yawDeltaDegrees *
        degreesToRadians;

    const float yawCos =
        std::cos(
            yawDeltaRadians);

    const float yawSin =
        std::sin(
            yawDeltaRadians);

    for (int axisIndex = 0;
         axisIndex < 3;
         ++axisIndex)
    {
        const float originalForward =
            g_vrHeadOrientationAxis[
                axisIndex][0];

        const float originalLeft =
            g_vrHeadOrientationAxis[
                axisIndex][1];

        g_vrHeadOrientationAxis[
            axisIndex][0] =
                yawCos *
                    originalForward +
                yawSin *
                    originalLeft;

        g_vrHeadOrientationAxis[
            axisIndex][1] =
                -yawSin *
                    originalForward +
                yawCos *
                    originalLeft;
    }

    if (g_vrHeadPositionValid)
    {
        const float originalForward =
            g_vrHeadPositionLocal[0];

        const float originalLeft =
            g_vrHeadPositionLocal[1];

        g_vrHeadPositionLocal[0] =
            yawCos *
                originalForward +
            yawSin *
                originalLeft;

        g_vrHeadPositionLocal[1] =
            -yawSin *
                originalForward +
            yawCos *
                originalLeft;
    }

    *bodyYawDeltaDegrees =
        yawDeltaDegrees;

    return true;
}

bool VR_GetHmdOrientedMovement(
    float* forward,
    float* right)
{
    if (forward == nullptr ||
        right == nullptr)
    {
        return false;
    }

    *forward = 0.0f;
    *right = 0.0f;

    float stickX = 0.0f;
    float stickY = 0.0f;
    float headForward = 1.0f;
    float headLeft = 0.0f;

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        if (!g_vrLeftThumbstickValid ||
            !g_vrHeadOrientationValid ||
            g_vrRightThumbrestTouched)
        {
            return false;
        }

        stickX = g_vrLeftThumbstick[0];
        stickY = g_vrLeftThumbstick[1];

        headForward =
            g_vrHeadOrientationAxis[0][0];

        headLeft =
            g_vrHeadOrientationAxis[0][1];
    }

    if (configurable.movementDirection ==
        VrMovementDirection::Body)
    {
        headForward = 1.0f;
        headLeft = 0.0f;
    }
    else if (configurable.movementDirection !=
             VrMovementDirection::Head)
    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        bool useOffHand =
            configurable.movementDirection ==
                VrMovementDirection::OffHand;
        if (configurable.movementDirection ==
            VrMovementDirection::PhysicalLeft)
        {
            useOffHand =
                configurable.dominantHand ==
                    VrInteractions::DominantHand::Right;
        }
        else if (configurable.movementDirection ==
                 VrMovementDirection::PhysicalRight)
        {
            useOffHand =
                configurable.dominantHand ==
                    VrInteractions::DominantHand::Left;
        }

        if (useOffHand && g_vrLeftControllerForegripPoseValid)
        {
            headForward =
                g_vrLeftControllerForegripAxis[0][0];
            headLeft =
                g_vrLeftControllerForegripAxis[0][1];
        }
        else if (!useOffHand && g_vrRightControllerWeaponPoseValid)
        {
            headForward =
                g_vrRightControllerWeaponAxis[0][0];
            headLeft =
                g_vrRightControllerWeaponAxis[0][1];
        }
    }

    const float rawMagnitude =
        std::sqrt(
            stickX * stickX +
            stickY * stickY);

    const float deadzone =
        configurable.movementDeadzone;

    if (rawMagnitude <= deadzone)
    {
        return false;
    }

    const float clampedMagnitude =
        rawMagnitude < 1.0f
            ? rawMagnitude
            : 1.0f;

    const float remappedMagnitude =
        (clampedMagnitude - deadzone) /
        (1.0f - deadzone);

    const float inverseRawMagnitude =
        1.0f / rawMagnitude;

    stickX *=
        inverseRawMagnitude *
        remappedMagnitude;

    stickY *=
        inverseRawMagnitude *
        remappedMagnitude;

    const float horizontalHeadLength =
        std::sqrt(
            headForward * headForward +
            headLeft * headLeft);

    if (horizontalHeadLength <= 0.0001f)
    {
        return false;
    }

    headForward /= horizontalHeadLength;
    headLeft /= horizontalHeadLength;

    // CoD's local camera basis is +forward, +left. OpenXR stick X is
    // positive right, so command-right is the negative local-left result.
    *forward =
        stickY * headForward +
        stickX * headLeft;

    *right =
        stickX * headForward -
        stickY * headLeft;

    return true;
}

// KISAK_SP_VR_FIXED_SCOPED_TURRET_CONTROLS_V3_OPENXR
bool VR_GetFixedScopedTurretAim(
    float* gunPitch,
    float* gunYaw)
{
    if (gunPitch == nullptr ||
        gunYaw == nullptr)
    {
        return false;
    }

    float forwardWorld[3] = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrMountedWeaponCameraAxisWorldValid)
        {
            return false;
        }

        memcpy(
            forwardWorld,
            g_vrMountedWeaponCameraAxisWorld[0],
            sizeof(forwardWorld));
    }

    const float forwardLength =
        std::sqrt(
            forwardWorld[0] * forwardWorld[0] +
            forwardWorld[1] * forwardWorld[1] +
            forwardWorld[2] * forwardWorld[2]);

    if (forwardLength <= 0.0001f)
    {
        return false;
    }

    forwardWorld[0] /= forwardLength;
    forwardWorld[1] /= forwardLength;
    forwardWorld[2] /= forwardLength;

    constexpr float radiansToDegrees =
        57.29577951308232f;

    const float horizontalLength =
        std::sqrt(
            forwardWorld[0] * forwardWorld[0] +
            forwardWorld[1] * forwardWorld[1]);

    float pitch =
        std::atan2(
            -forwardWorld[2],
            horizontalLength) *
        radiansToDegrees;

    float yaw =
        std::atan2(
            forwardWorld[1],
            forwardWorld[0]) *
        radiansToDegrees;

    pitch = std::fmod(pitch, 360.0f);
    yaw = std::fmod(yaw, 360.0f);

    if (pitch < 0.0f)
    {
        pitch += 360.0f;
    }

    if (yaw < 0.0f)
    {
        yaw += 360.0f;
    }

    *gunPitch = pitch;
    *gunYaw = yaw;

    return true;
}

bool VR_GetFixedScopedTurretZoomAxis(
    float* zoomAxis)
{
    if (zoomAxis == nullptr)
    {
        return false;
    }

    *zoomAxis = 0.0f;

    float rawZoomAxis = 0.0f;

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        if (!g_vrScopeZoomAxisValid)
        {
            return false;
        }

        rawZoomAxis =
            g_vrScopeZoomAxis.y;
    }

    constexpr float deadzone = 0.18f;

    const float absoluteAxis =
        std::abs(rawZoomAxis);

    // Returning true here is intentional.  It prevents HMD-oriented
    // locomotion from becoming a false zoom input while the raw stick is
    // available but vertically centered.
    if (absoluteAxis <= deadzone)
    {
        return true;
    }

    const float clampedAxis =
        absoluteAxis < 1.0f
            ? absoluteAxis
            : 1.0f;

    const float remappedAxis =
        (clampedAxis - deadzone) /
        (1.0f - deadzone);

    *zoomAxis =
        rawZoomAxis < 0.0f
            ? -remappedAxis
            : remappedAxis;

    return true;
}

bool VR_GetRightControllerMountedWeaponAim(
    float* gunPitch,
    float* gunYaw)
{
    if (gunPitch == nullptr ||
        gunYaw == nullptr)
    {
        return false;
    }

    float controllerForwardCameraLocal[3] = {};
    float cameraAxisWorld[3][3] = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrRightControllerWeaponPoseValid ||
            !g_vrMountedWeaponCameraAxisWorldValid)
        {
            return false;
        }

        memcpy(
            controllerForwardCameraLocal,
            g_vrRightControllerWeaponAxis[0],
            sizeof(controllerForwardCameraLocal));

        memcpy(
            cameraAxisWorld,
            g_vrMountedWeaponCameraAxisWorld,
            sizeof(cameraAxisWorld));
    }

    float forwardWorld[3] = {};

    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        forwardWorld[worldComponent] =
            controllerForwardCameraLocal[0] *
                cameraAxisWorld[0][worldComponent] +
            controllerForwardCameraLocal[1] *
                cameraAxisWorld[1][worldComponent] +
            controllerForwardCameraLocal[2] *
                cameraAxisWorld[2][worldComponent];
    }

    const float forwardLength =
        std::sqrt(
            forwardWorld[0] * forwardWorld[0] +
            forwardWorld[1] * forwardWorld[1] +
            forwardWorld[2] * forwardWorld[2]);

    if (forwardLength <= 0.0001f)
    {
        return false;
    }

    forwardWorld[0] /= forwardLength;
    forwardWorld[1] /= forwardLength;
    forwardWorld[2] /= forwardLength;

    constexpr float radiansToDegrees =
        57.29577951308232f;

    const float horizontalLength =
        std::sqrt(
            forwardWorld[0] * forwardWorld[0] +
            forwardWorld[1] * forwardWorld[1]);

    float pitch =
        std::atan2(
            -forwardWorld[2],
            horizontalLength) *
        radiansToDegrees;

    float yaw =
        std::atan2(
            forwardWorld[1],
            forwardWorld[0]) *
        radiansToDegrees;

    pitch = std::fmod(pitch, 360.0f);
    yaw = std::fmod(yaw, 360.0f);

    if (pitch < 0.0f)
    {
        pitch += 360.0f;
    }

    if (yaw < 0.0f)
    {
        yaw += 360.0f;
    }

    *gunPitch = pitch;
    *gunYaw = yaw;

    return true;
}

// KISAK_SP_VR_SCRIPTED_DETONATOR_TRIGGER_REPAIR_V68
bool VR_GetConfiguredAttackButton(
    bool* attackPressed)
{
    if (attackPressed == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    *attackPressed =
        g_vrRightControllerAttackPressed;

    return g_vrInitialized;
}


// KISAK_SP_VR_MOUNTED_WEAPON_TRIGGER_BOOTSTRAP_V1
bool VR_GetRightControllerMountedWeaponTrigger(
    bool* attackPressed)
{
    if (attackPressed == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    const bool mountedWeaponPoseAvailable =
        g_vrRightControllerWeaponPoseValid &&
        g_vrMountedWeaponCameraAxisWorldValid;

    *attackPressed =
        mountedWeaponPoseAvailable &&
        g_vrRightControllerAttackPressed;

    return mountedWeaponPoseAvailable;
}

bool VR_GetRightControllerWeaponCommand(
    float* gunPitch,
    float* gunYaw,
    bool* attackPressed)
{
    if (gunPitch == nullptr ||
        gunYaw == nullptr ||
        attackPressed == nullptr)
    {
        return false;
    }

    float forward[3] = {};
    bool aimValid = false;
    bool currentAttackPressed = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        aimValid =
            g_vrRightControllerFinalWeaponAimValid;

        const bool manualMagazineIsOut =
            g_vrManualMagazineReload.enabled &&
            g_vrManualMagazineReload.supported &&
            g_vrManualMagazineReload.stage !=
                VrManualMagazineReloadStage::Ready;

        currentAttackPressed =
            g_vrRightControllerAttackPressed &&
            !manualMagazineIsOut;

        forward[0] =
            g_vrRightControllerFinalWeaponForward[0];

        forward[1] =
            g_vrRightControllerFinalWeaponForward[1];

        forward[2] =
            g_vrRightControllerFinalWeaponForward[2];
    }

    *attackPressed =
        aimValid && currentAttackPressed;

    if (!aimValid)
    {
        return false;
    }

    constexpr float radiansToDegrees =
        57.29577951308232f;

    const float horizontalLength =
        std::sqrt(
            forward[0] * forward[0] +
            forward[1] * forward[1]);

    float pitch =
        std::atan2(
            -forward[2],
            horizontalLength) *
        radiansToDegrees;

    float yaw =
        std::atan2(
            forward[1],
            forward[0]) *
        radiansToDegrees;

    pitch = std::fmod(pitch, 360.0f);
    yaw = std::fmod(yaw, 360.0f);

    if (pitch < 0.0f)
    {
        pitch += 360.0f;
    }

    if (yaw < 0.0f)
    {
        yaw += 360.0f;
    }

    *gunPitch = pitch;
    *gunYaw = yaw;

    if (!g_vrLoggedRightControllerUsercmdAim)
    {
        Com_Printf(
            0,
            "[VR] Supplied weapon-controller "
            "aim to the multiplayer usercmd.\n");

        g_vrLoggedRightControllerUsercmdAim = true;
    }

    return true;
}

static bool VR_RecenterHeadForMode(
    const VrCalibration::RecenterMode mode)
{
    std::lock_guard<std::mutex> lock(
        g_vrHeadOrientationMutex);

    if (mode == VrCalibration::RecenterMode::Disabled)
    {
        return true;
    }

    const bool needsPosition =
        mode == VrCalibration::RecenterMode::PositionOnly ||
        mode == VrCalibration::RecenterMode::Full;
    const bool needsDirectionLevel =
        mode == VrCalibration::RecenterMode::DirectionLevelOnly ||
        mode == VrCalibration::RecenterMode::Full;

    // Validate every requested component before mutating either one. A full
    // recenter therefore stays atomic if tracking temporarily loses only
    // position or only orientation.
    if ((needsPosition && !g_vrLatestHeadPositionValid) ||
        (needsDirectionLevel && !g_vrLatestHeadOrientationValid))
    {
        return false;
    }

    XrQuaternionf requestedLevelBaseOrientation = {};

    if (needsDirectionLevel &&
        !VR_TryGetLevelYawOnlyOrientation(
            g_vrLatestHeadOrientation,
            &requestedLevelBaseOrientation))
    {
        // Preserve recenter atomicity: a vertical pose cannot provide a
        // stable yaw, so do not commit position or orientation changes.
        return false;
    }

    if (needsDirectionLevel)
    {
        g_vrHeadBaseOrientation =
            requestedLevelBaseOrientation;
        g_vrHeadBaseOrientationValid = true;
        VR_LogLevelSafeHeadBaseOnce();

        g_vrHeadOrientationAxis[0][0] = 1.0f;
        g_vrHeadOrientationAxis[0][1] = 0.0f;
        g_vrHeadOrientationAxis[0][2] = 0.0f;
        g_vrHeadOrientationAxis[1][0] = 0.0f;
        g_vrHeadOrientationAxis[1][1] = 1.0f;
        g_vrHeadOrientationAxis[1][2] = 0.0f;
        g_vrHeadOrientationAxis[2][0] = 0.0f;
        g_vrHeadOrientationAxis[2][1] = 0.0f;
        g_vrHeadOrientationAxis[2][2] = 1.0f;
        g_vrHeadOrientationValid = true;
        g_vrTransferredBodyYawDegrees = 0.0f;
    }

    if (needsPosition)
    {
        g_vrHeadPositionOrigin =
            g_vrLatestHeadPosition;
        g_vrHeadPositionOriginValid = true;
        g_vrHeadPositionLocal[0] = 0.0f;
        g_vrHeadPositionLocal[1] = 0.0f;
        g_vrHeadPositionLocal[2] = 0.0f;
        g_vrHeadPositionValid = true;
    }

    if (mode == VrCalibration::RecenterMode::Full)
    {
        g_vrHeadPositionBodyYawDegrees = 0.0f;
    }

    Com_Printf(
        0,
        "[VR][CALIBRATION][RECENTER] backend %s; mode %s; "
        "position %s; direction/level %s.\n",
        VR_RuntimeBackendName(),
        VrCalibration::RecenterModeId(mode),
        needsPosition ? "updated" : "preserved",
        needsDirectionLevel ? "updated" : "preserved");

    return true;
}

bool VR_RecenterHeadPosition()
{
    return VR_RecenterHeadForMode(
        VrCalibration::RecenterMode::PositionOnly);
}

bool VR_RecenterHeadDirectionLevel()
{
    return VR_RecenterHeadForMode(
        VrCalibration::RecenterMode::DirectionLevelOnly);
}

bool VR_RecenterHeadPose()
{
    return VR_RecenterHeadForMode(
        VrCalibration::RecenterMode::Full);
}

bool VR_RecenterAtFirstGameplayCamera()
{
    return VR_RecenterHeadForMode(
        VR_GetConfiguratorSettings().firstGameplayRecenterMode);
}

const char* VR_GetFirstGameplayRecenterModeName()
{
    return VrCalibration::RecenterModeId(
        VR_GetConfiguratorSettings().firstGameplayRecenterMode);
}

namespace
{

bool VR_ReadCalibrationRequestFile(
    const char* const path,
    std::string* const text)
{
    if (path == nullptr || path[0] == '\0' || text == nullptr)
    {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return false;
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    *text = contents.str();
    return !text->empty() && text->size() <= 4096u;
}

bool VR_MeasureFloorReferencedEyeHeight(
    const XrTime predictedDisplayTime,
    float* const eyeHeightInches)
{
    if (eyeHeightInches == nullptr)
    {
        return false;
    }

    float meters = 0.0f;

    if (g_vrRuntimeBackend == VrRuntimeBackend::OpenVr)
    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        if (!g_vrLatestHeadPositionValid)
        {
            return false;
        }

        // OpenVR is configured with TrackingUniverseStanding, whose Y=0
        // plane is the SteamVR floor.
        meters = g_vrLatestHeadPosition.y;
    }
    else if (g_vrRuntimeBackend == VrRuntimeBackend::OpenXr &&
             g_vrCalibrationFloorSpaceAvailable &&
             g_vrCalibrationFloorSpace != XR_NULL_HANDLE &&
             predictedDisplayTime != 0)
    {
        XrViewLocateInfo locateInfo{
            XR_TYPE_VIEW_LOCATE_INFO
        };
        locateInfo.viewConfigurationType =
            kViewConfiguration;
        locateInfo.displayTime =
            predictedDisplayTime;
        locateInfo.space =
            g_vrCalibrationFloorSpace;

        XrViewState viewState{
            XR_TYPE_VIEW_STATE
        };

        std::array<XrView, kVrStereoEyeCount> floorViews = {{
            XrView{XR_TYPE_VIEW},
            XrView{XR_TYPE_VIEW},
        }};

        std::uint32_t locatedCount = 0u;
        const XrResult result = xrLocateViews(
            g_vrSession,
            &locateInfo,
            &viewState,
            kVrStereoEyeCount,
            &locatedCount,
            floorViews.data());

        const XrViewStateFlags requiredFlags =
            XR_VIEW_STATE_ORIENTATION_VALID_BIT |
            XR_VIEW_STATE_POSITION_VALID_BIT;

        if (XR_FAILED(result) ||
            locatedCount < kVrStereoEyeCount ||
            (viewState.viewStateFlags & requiredFlags) !=
                requiredFlags)
        {
            return false;
        }

        meters = 0.5f *
            (floorViews[0].pose.position.y +
             floorViews[1].pose.position.y);
    }
    else
    {
        return false;
    }

    const float inches =
        meters * kVrGameUnitsPerMeter;

    if (!std::isfinite(inches) ||
        inches < VrCalibration::kMinimumEyeHeightInches ||
        inches > VrCalibration::kMaximumEyeHeightInches)
    {
        return false;
    }

    *eyeHeightInches = inches;
    return true;
}

void VR_WriteCalibrationStatus(
    const VrCalibration::Request& request,
    const char* const status,
    const bool floorAvailable,
    const float measuredEyeHeightInches,
    const float appliedEyeHeightInches,
    const VrCalibration::RecenterMode recenterMode,
    const bool recentered)
{
    const char* const statusPath =
        std::getenv("KISAK_VR_CALIBRATION_STATUS_PATH");

    if (statusPath == nullptr || statusPath[0] == '\0')
    {
        return;
    }

    const std::string temporaryPath =
        std::string(statusPath) + ".tmp";

    {
        std::ofstream output(
            temporaryPath,
            std::ios::binary | std::ios::trunc);

        if (!output)
        {
            return;
        }

        output.setf(std::ios::fixed, std::ios::floatfield);
        output.precision(2);
        output << "VERSION=1\r\n";
        output << "STATUS=" << status << "\r\n";
        output << "REQUEST_ID=" << request.requestId << "\r\n";
        output << "COMMAND="
               << VrCalibration::CommandId(request.command)
               << "\r\n";
        output << "PLAY_MODE="
               << VrCalibration::PlayModeId(request.playMode)
               << "\r\n";
        output << "BACKEND=" << VR_RuntimeBackendName() << "\r\n";
        output << "FLOOR_AVAILABLE="
               << (floorAvailable ? 1 : 0) << "\r\n";
        output << "MEASURED_EYE_HEIGHT_INCHES=";
        if (floorAvailable)
        {
            output << measuredEyeHeightInches;
        }
        output << "\r\n";
        output << "APPLIED_EYE_HEIGHT_INCHES="
               << appliedEyeHeightInches << "\r\n";
        output << "HEIGHT_CORRECTION_INCHES="
               << VrCalibration::EyeHeightCorrectionInches(
                      appliedEyeHeightInches)
               << "\r\n";
        output << "RECENTER_MODE="
               << VrCalibration::RecenterModeId(recenterMode)
               << "\r\n";
        output << "RECENTERED=" << (recentered ? 1 : 0) << "\r\n";
        output.flush();

        if (!output)
        {
            return;
        }
    }

    if (!MoveFileExA(
            temporaryPath.c_str(),
            statusPath,
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH))
    {
        std::remove(temporaryPath.c_str());
    }
}

void VR_AppendCalibrationReceipt(
    const VrCalibration::Request& request,
    const bool floorAvailable,
    const float measuredEyeHeightInches,
    const float appliedEyeHeightInches,
    const VrCalibration::RecenterMode recenterMode,
    const bool recentered)
{
    const char* const receiptPath =
        std::getenv("KISAK_VR_SETTINGS_RECEIPT_PATH");
    if (receiptPath == nullptr || receiptPath[0] == '\0')
    {
        return;
    }

    FILE* receipt = nullptr;
    if (fopen_s(&receipt, receiptPath, "ab") != 0 ||
        receipt == nullptr)
    {
        return;
    }

    const VrMeasurementUnitSystem units =
        VR_GetConfiguratorSettings().measurementUnits;

    std::fprintf(
        receipt,
        "\r\nSTATUS=RUNTIME_CALIBRATION_APPLIED\r\n"
        "RUNTIME_CALIBRATION_REQUEST=%s\r\n"
        "RUNTIME_CALIBRATION_COMMAND=%s\r\n"
        "RUNTIME_CALIBRATION_PLAY_MODE=%s\r\n"
        "RUNTIME_CALIBRATION_BACKEND=%s\r\n"
        "RUNTIME_CALIBRATION_FLOOR_AVAILABLE=%d\r\n"
        "RUNTIME_CALIBRATION_MEASURED_EYE_HEIGHT=%.2f\r\n"
        "RUNTIME_CALIBRATION_APPLIED_EYE_HEIGHT=%.2f\r\n"
        "RUNTIME_CALIBRATION_HEIGHT_CORRECTION=%+.2f\r\n"
        "RUNTIME_CALIBRATION_DISPLAY_UNITS=%s\r\n"
        "RUNTIME_CALIBRATION_APPLIED_DISPLAY=%.1f %s\r\n"
        "RUNTIME_CALIBRATION_RECENTER_MODE=%s\r\n"
        "RUNTIME_CALIBRATION_RECENTERED=%d\r\n",
        request.requestId.c_str(),
        VrCalibration::CommandId(request.command),
        VrCalibration::PlayModeId(request.playMode),
        VR_RuntimeBackendName(),
        floorAvailable ? 1 : 0,
        floorAvailable ? measuredEyeHeightInches : 0.0f,
        appliedEyeHeightInches,
        VrCalibration::EyeHeightCorrectionInches(
            appliedEyeHeightInches),
        VR_MeasurementUnitSystemId(units),
        VR_DisplayInches(appliedEyeHeightInches, units),
        VR_DisplayLengthUnit(units),
        VrCalibration::RecenterModeId(recenterMode),
        recentered ? 1 : 0);

    std::fflush(receipt);
    std::fclose(receipt);
}

} // namespace

bool VR_GetActiveHudLayout(VrHud::Layout* const layout)
{
    if (layout == nullptr)
    {
        return false;
    }
    VR_EnsureHudLayoutInitialized();
    std::lock_guard<std::mutex> lock(g_vrHudEditorMutex);
    *layout = g_vrHudEditorLayout;
    return true;
}

std::uint64_t VR_GetHudLayoutRevision()
{
    VR_EnsureHudLayoutInitialized();
    std::lock_guard<std::mutex> lock(g_vrHudEditorMutex);
    return g_vrHudLayoutRevision;
}

bool VR_GetHudEditorSnapshot(
    VrHud::EditorSnapshot* const snapshot)
{
    if (snapshot == nullptr)
    {
        return false;
    }
    VR_EnsureHudLayoutInitialized();
    std::lock_guard<std::mutex> lock(g_vrHudEditorMutex);
    snapshot->active = g_vrHudEditorActive;
    snapshot->layout = g_vrHudEditorLayout;
    snapshot->selected = g_vrHudEditorSelected;
    snapshot->pointer = g_vrHudEditorPointer;
    snapshot->pointerValid = g_vrHudEditorPointerValid;
    snapshot->dragging = g_vrHudEditorDragging;
    snapshot->snapEnabled = g_vrHudEditorSnapEnabled;
    return snapshot->active;
}

bool VR_HudEditorConsumesGameplayInput()
{
    std::lock_guard<std::mutex> lock(g_vrHudEditorMutex);
    return g_vrHudEditorActive;
}

void VR_ProcessHudEditorRequest()
{
    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(Sys_Milliseconds());
    if (nowMilliseconds - g_vrLastHudEditorPollMilliseconds < 150u)
    {
        return;
    }
    g_vrLastHudEditorPollMilliseconds = nowMilliseconds;

    const char* const requestPath =
        std::getenv("KISAK_VR_HUD_EDITOR_REQUEST_PATH");
    std::string text;
    if (!VR_ReadCalibrationRequestFile(requestPath, &text))
    {
        return;
    }

    VrHud::Request request;
    std::string parseError;
    if (!VrHud::ParseRequest(text, &request, &parseError))
    {
        Com_PrintWarning(
            0,
            "[VR][HUD][EDITOR] Ignored invalid request: %s.\n",
            parseError.c_str());
        if (requestPath != nullptr)
        {
            std::remove(requestPath);
        }
        return;
    }

    if (request.requestId == g_vrLastHudEditorRequestId)
    {
        if (requestPath != nullptr)
        {
            std::remove(requestPath);
        }
        return;
    }
    g_vrLastHudEditorRequestId = request.requestId;
    VR_EnsureHudLayoutInitialized();

    {
        std::lock_guard<std::mutex> lock(g_vrHudEditorMutex);
        g_vrHudEditorOriginalLayout = request.layout;
        g_vrHudEditorLayout = request.layout;
        g_vrHudEditorActive = true;
        g_vrHudEditorDragging = false;
        g_vrHudEditorPointerValid = false;
        g_vrHudEditorSnapEnabled = true;
        g_vrHudEditorTriggerWasHeld = false;
        g_vrHudEditorConfirmWasHeld = false;
        g_vrHudEditorBackWasHeld = false;
        g_vrHudEditorScaleArmed = true;
        g_vrHudEditorPreviousWasHeld = false;
        g_vrHudEditorNextWasHeld = false;
        g_vrHudEditorCenterWasHeld = false;
        g_vrHudEditorResetWasHeld = false;
        g_vrHudEditorKeyboardTabWasHeld = false;
        g_vrHudEditorKeyboardCenterWasHeld = false;
        g_vrHudEditorKeyboardResetWasHeld = false;
        g_vrHudEditorSelected = VrHud::Element::AmmoEquipment;
        g_vrHudEditorRequestId = request.requestId;
        ++g_vrHudLayoutRevision;
    }

    VrHud::Response response;
    response.requestId = request.requestId;
    response.status = VrHud::ResponseStatus::Active;
    response.layout = request.layout;
    response.message =
        "Use/Next selects; Sprint/Melee centers or resets; trigger drags";
    VR_WriteHudEditorResponse(response);
    if (requestPath != nullptr)
    {
        std::remove(requestPath);
    }

    Com_Printf(
        0,
        "[VR][HUD][EDITOR] Request %s active with %u draggable "
        "HUD groups; gameplay input is suppressed until Save or Cancel.\n",
        request.requestId.c_str(),
        static_cast<unsigned int>(VrHud::kElementCount));
}

void VR_ProcessCalibrationRequest(
    const XrTime predictedDisplayTime)
{
    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(Sys_Milliseconds());

    if (nowMilliseconds -
            g_vrLastCalibrationPollMilliseconds <
        150u)
    {
        return;
    }

    g_vrLastCalibrationPollMilliseconds =
        nowMilliseconds;

    const char* const requestPath =
        std::getenv("KISAK_VR_CALIBRATION_REQUEST_PATH");

    std::string text;
    if (!VR_ReadCalibrationRequestFile(requestPath, &text))
    {
        return;
    }

    VrCalibration::Request request;
    std::string parseError;
    if (!VrCalibration::ParseRequest(
            text,
            &request,
            &parseError))
    {
        static bool loggedInvalidRequest = false;
        if (!loggedInvalidRequest)
        {
            Com_PrintWarning(
                0,
                "[VR][CALIBRATION] Ignored invalid request: %s\n",
                parseError.c_str());
            loggedInvalidRequest = true;
        }
        return;
    }

    if (request.requestId ==
        g_vrLastCalibrationRequestId)
    {
        return;
    }

    g_vrLastCalibrationRequestId =
        request.requestId;

    float measuredEyeHeightInches = 0.0f;
    bool floorAvailable = false;
    float appliedEyeHeightInches =
        request.targetEyeHeightInches;

    if (request.command ==
        VrCalibration::Command::MeasureStanding)
    {
        floorAvailable =
            VR_MeasureFloorReferencedEyeHeight(
                predictedDisplayTime,
                &measuredEyeHeightInches);

        if (floorAvailable)
        {
            appliedEyeHeightInches =
                measuredEyeHeightInches;
        }
    }

    const VrCalibration::RecenterMode recenterMode =
        VrCalibration::CommandRecenterMode(request.command);

    const bool recentered =
        recenterMode == VrCalibration::RecenterMode::Disabled ||
        VR_RecenterHeadForMode(recenterMode);

    if (!recentered)
    {
        VR_WriteCalibrationStatus(
            request,
            "NO_TRACKED_POSE",
            floorAvailable,
            measuredEyeHeightInches,
            appliedEyeHeightInches,
            recenterMode,
            false);
        return;
    }

    // Commit the requested height only after every required pose operation
    // succeeds. A lost-tracking recenter is therefore all-or-nothing instead
    // of silently changing the player's height while reporting failure.
    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        g_vrLiveTargetEyeHeightInches =
            appliedEyeHeightInches;
        g_vrLiveTargetEyeHeightValid = true;
        g_vrLoggedFirstHeightApply.store(false);
    }

    const char* const status =
        request.command ==
                    VrCalibration::Command::MeasureStanding &&
                !floorAvailable
            ? "APPLIED_MANUAL_HEIGHT"
            : "APPLIED";

    VR_WriteCalibrationStatus(
        request,
        status,
        floorAvailable,
        measuredEyeHeightInches,
        appliedEyeHeightInches,
        recenterMode,
        recenterMode != VrCalibration::RecenterMode::Disabled);

    VR_AppendCalibrationReceipt(
        request,
        floorAvailable,
        measuredEyeHeightInches,
        appliedEyeHeightInches,
        recenterMode,
        recenterMode != VrCalibration::RecenterMode::Disabled);

    const VrMeasurementUnitSystem units =
        VR_GetConfiguratorSettings().measurementUnits;
    Com_Printf(
        0,
        "[VR][CALIBRATION][APPLY] Request %s: %s, posture %s, "
        "target %.1f %s, correction %+.1f %s; floor %s; "
        "recenter %s.\n",
        request.requestId.c_str(),
        VrCalibration::CommandId(request.command),
        VrCalibration::PlayModeId(request.playMode),
        VR_DisplayInches(appliedEyeHeightInches, units),
        VR_DisplayLengthUnit(units),
        VR_DisplayInches(
            VrCalibration::EyeHeightCorrectionInches(
                appliedEyeHeightInches),
            units),
        VR_DisplayLengthUnit(units),
        floorAvailable ? "measured" : "not measured",
        VrCalibration::RecenterModeId(recenterMode));
}

void VR_ProcessWeaponCalibrationRequest()
{
    const std::uint32_t nowMilliseconds =
        static_cast<std::uint32_t>(Sys_Milliseconds());
    if (nowMilliseconds - g_vrLastWeaponCalibrationPollMilliseconds < 150u)
    {
        return;
    }
    g_vrLastWeaponCalibrationPollMilliseconds = nowMilliseconds;

    const char* const requestPath =
        std::getenv("KISAK_VR_WEAPON_CALIBRATION_REQUEST_PATH");
    std::string text;
    if (!VR_ReadCalibrationRequestFile(requestPath, &text))
    {
        return;
    }

    VrWeaponProfiles::Request request;
    std::string parseError;
    if (!VrWeaponProfiles::ParseRequest(text, &request, &parseError))
    {
        Com_PrintWarning(
            0,
            "[VR][WEAPON CALIBRATION] Ignored invalid request: %s\n",
            parseError.c_str());
        if (requestPath != nullptr)
        {
            std::remove(requestPath);
        }
        return;
    }
    if (request.requestId == g_vrLastWeaponCalibrationRequestId)
    {
        if (requestPath != nullptr)
        {
            std::remove(requestPath);
        }
        return;
    }
    g_vrLastWeaponCalibrationRequestId = request.requestId;

    VrWeaponProfiles::RuntimeStatus status;
    status.requestId = request.requestId;
    const bool reloaded = VR_ReloadWeaponProfiles();
    std::string profileLoadError;

    {
        std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
        status.weaponIndex = g_vrActiveCalibrationWeaponIndex;
        status.weaponId = g_vrActiveCalibrationWeaponId;
        status.weaponName = g_vrActiveCalibrationWeaponName;
        status.activeGunstockId = g_vrWeaponProfiles.activeGunstockId;
        status.profileRevision = g_vrWeaponProfilesRevision;
        status.effective = g_vrActiveEffectiveWeaponCalibration;
        profileLoadError = g_vrWeaponProfilesLoadError;
    }

    if (!reloaded)
    {
        status.status = "invalid_profiles";
        status.message = profileLoadError;
    }
    else if (request.command == VrWeaponProfiles::Command::Reload)
    {
        status.status = "reloaded";
        status.message = "Weapon and gunstock profiles applied live";
        status.effective = VR_ResolveEffectiveWeaponCalibration(
            status.weaponId.c_str(),
            status.effective.shoulderedBlend);
    }
    else
    {
        float baseAttachmentAxis[3][3] = {};
        float controllerAxis[3][3] = {};
        bool captureValid = false;
        {
            std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
            captureValid = g_vrActiveWeaponCapturePoseValid &&
                !g_vrActiveCalibrationWeaponId.empty() &&
                request.weaponId == g_vrActiveCalibrationWeaponId &&
                request.gunstockId ==
                    g_vrWeaponProfiles.activeGunstockId;
            if (captureValid)
            {
                std::memcpy(
                    baseAttachmentAxis,
                    g_vrActiveWeaponBaseAttachmentAxis,
                    sizeof(baseAttachmentAxis));
                std::memcpy(
                    controllerAxis,
                    g_vrActiveWeaponControllerAxis,
                    sizeof(controllerAxis));
            }
        }

        if (!captureValid)
        {
            status.status = "no_matching_weapon";
            status.message =
                "Equip the selected weapon in an active mission and retry";
        }
        else
        {
            float effectiveRotationAxis[3][3] = {};
            kisak::vr::weapon_calibration::AimAlignedEffectiveRotation(
                baseAttachmentAxis,
                controllerAxis,
                effectiveRotationAxis);
            float capturedAngles[3] = {};
            AxisToAngles(
                reinterpret_cast<const mat3x3&>(effectiveRotationAxis),
                capturedAngles);

            for (std::size_t component = 0u; component < 3u; ++component)
            {
                float angle = std::fmod(capturedAngles[component], 360.0f);
                if (angle > 180.0f)
                {
                    angle -= 360.0f;
                }
                else if (angle < -180.0f)
                {
                    angle += 360.0f;
                }
                status.capturedEffectiveAngles[component] = angle;
            }

            const bool rangeValid = std::all_of(
                status.capturedEffectiveAngles.begin(),
                status.capturedEffectiveAngles.end(),
                [](const float angle)
                {
                    return std::isfinite(angle) &&
                        std::abs(angle) <=
                            VrWeaponProfiles::kMaximumAngleDegrees;
                });
            status.capturedAnglesValid = rangeValid;
            status.status = rangeValid ? "captured" : "capture_out_of_range";
            status.message = rangeValid
                ? "Aim alignment captured; save the returned correction"
                : "Captured mount rotation exceeds the guarded 90-degree range";

            if (rangeValid)
            {
                const char* const receiptPath =
                    std::getenv("KISAK_VR_SETTINGS_RECEIPT_PATH");
                FILE* receipt = nullptr;
                if (receiptPath != nullptr && receiptPath[0] != '\0' &&
                    fopen_s(&receipt, receiptPath, "ab") == 0 &&
                    receipt != nullptr)
                {
                    std::fprintf(
                        receipt,
                        "\r\nSTATUS=RUNTIME_WEAPON_AIM_CAPTURED\r\n"
                        "RUNTIME_WEAPON_CAPTURE_REQUEST=%s\r\n"
                        "RUNTIME_WEAPON_CAPTURE_TARGET=%s\r\n"
                        "RUNTIME_WEAPON_CAPTURE_ID=%s\r\n"
                        "RUNTIME_WEAPON_CAPTURE_EFFECTIVE_ANGLES=%.2f %.2f %.2f\r\n",
                        request.requestId.c_str(),
                        VrWeaponProfiles::CaptureTargetId(request.target),
                        status.weaponId.c_str(),
                        status.capturedEffectiveAngles[0],
                        status.capturedEffectiveAngles[1],
                        status.capturedEffectiveAngles[2]);
                    std::fflush(receipt);
                    std::fclose(receipt);
                }
            }
        }
    }

    g_vrWeaponStatusHoldUntilMilliseconds = nowMilliseconds + 5000u;
    {
        std::lock_guard<std::mutex> lock(g_vrWeaponProfilesMutex);
        g_vrLastWeaponStatusSignature.clear();
    }
    VR_WriteWeaponCalibrationStatusAtomic(status);
    if (requestPath != nullptr)
    {
        std::remove(requestPath);
    }
    Com_Printf(
        0,
        "[VR][WEAPON CALIBRATION] Request %s (%s): %s.\n",
        request.requestId.c_str(),
        VrWeaponProfiles::CommandId(request.command),
        status.status.c_str());
}

bool VR_ApplyHeadPosition(
    float viewOrigin[3],
    const float viewAxis[3][3],
    const float nativeViewHeightCurrent)
{
    if (viewOrigin == nullptr ||
        viewAxis == nullptr)
    {
        return false;
    }

    float localPosition[3] = {};
    float targetEyeHeightInches =
        VrCalibration::kNativeStandingEyeHeightInches;

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    targetEyeHeightInches =
        configurable.playMode == VrCalibration::PlayMode::Seated
            ? configurable.seatedEyeHeightInches
            : configurable.standingEyeHeightInches;

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        if (!g_vrHeadPositionValid)
        {
            return false;
        }

        localPosition[0] =
            g_vrHeadPositionLocal[0];

        localPosition[1] =
            g_vrHeadPositionLocal[1];

        localPosition[2] =
            g_vrHeadPositionLocal[2];

        if (g_vrLiveTargetEyeHeightValid)
        {
            targetEyeHeightInches =
                g_vrLiveTargetEyeHeightInches;
        }
    }

    const bool playerHeightAvailable =
        std::isfinite(nativeViewHeightCurrent) &&
        nativeViewHeightCurrent >= 1.0f &&
        nativeViewHeightCurrent <= 84.0f;

    const float heightCorrectionInches =
        playerHeightAvailable
            ? VrCalibration::EyeHeightCorrectionInches(
                  targetEyeHeightInches)
            : 0.0f;

    localPosition[2] +=
        heightCorrectionInches;

    // Use the normal player-camera basis. The MP callsite invokes this
    // before applying the headset's rotational offset.
    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        viewOrigin[worldComponent] +=
            localPosition[0] *
                viewAxis[0][worldComponent] +
            localPosition[1] *
                viewAxis[1][worldComponent] +
            localPosition[2] *
                viewAxis[2][worldComponent];
    }

    if (!g_vrLoggedFirstPositionApply)
    {
        Com_Printf(
            0,
            "[VR] Applied OpenXR headset position "
            "to the CoD4 camera.\n");

        g_vrLoggedFirstPositionApply = true;
    }

    if (playerHeightAvailable &&
        !g_vrLoggedFirstHeightApply.exchange(true))
    {
        const VrMeasurementUnitSystem units =
            configurable.measurementUnits;
        Com_Printf(
            0,
            "[VR][CALIBRATION][APPLY] Native standing eye height %.1f; "
            "target %.1f; persistent camera correction %+.1f; current "
            "stance height %.1f %s.\n",
            VR_DisplayInches(
                VrCalibration::kNativeStandingEyeHeightInches,
                units),
            VR_DisplayInches(targetEyeHeightInches, units),
            VR_DisplayInches(heightCorrectionInches, units),
            VR_DisplayInches(nativeViewHeightCurrent, units),
            VR_DisplayLengthUnit(units));

        const char* const receiptPath =
            std::getenv("KISAK_VR_SETTINGS_RECEIPT_PATH");

        if (receiptPath != nullptr && receiptPath[0] != '\0')
        {
            FILE* receipt = nullptr;
            if (fopen_s(&receipt, receiptPath, "ab") == 0 &&
                receipt != nullptr)
            {
                std::fprintf(
                    receipt,
                    "\r\nSTATUS=RUNTIME_HEIGHT_APPLIED\r\n"
                    "RUNTIME_NATIVE_STANDING_EYE_HEIGHT=%.1f\r\n"
                    "RUNTIME_TARGET_EYE_HEIGHT=%.1f\r\n"
                    "RUNTIME_HEIGHT_CORRECTION=%+.1f\r\n"
                    "RUNTIME_CURRENT_STANCE_HEIGHT=%.1f\r\n"
                    "RUNTIME_HEIGHT_DISPLAY_UNITS=%s\r\n"
                    "RUNTIME_TARGET_EYE_HEIGHT_DISPLAY=%.1f %s\r\n",
                    VrCalibration::kNativeStandingEyeHeightInches,
                    targetEyeHeightInches,
                    heightCorrectionInches,
                    nativeViewHeightCurrent,
                    VR_MeasurementUnitSystemId(units),
                    VR_DisplayInches(targetEyeHeightInches, units),
                    VR_DisplayLengthUnit(units));

                std::fflush(receipt);
                std::fclose(receipt);
            }
        }

    }

    return true;
}



void VR_BeginStereoEyeRender(
    const unsigned int eyeIndex)
{
    g_vrCurrentRenderEye =
        eyeIndex < kVrStereoEyeCount
            ? static_cast<int>(eyeIndex)
            : -1;
}

void VR_EndStereoEyeRender()
{
    g_vrCurrentRenderEye = -1;
}

bool VR_GetStereoEyeFovBounds(
    const unsigned int eyeIndex,
    float* tanHalfFovX,
    float* tanHalfFovY)
{
    if (eyeIndex >= kVrStereoEyeCount ||
        tanHalfFovX == nullptr ||
        tanHalfFovY == nullptr)
    {
        return false;
    }

    VrEyeProjectionTangents tangents = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrProjectionMutex);

        if (!g_vrEyeProjectionValid)
        {
            return false;
        }

        tangents =
            g_vrEyeProjectionTangents[eyeIndex];
    }

    const float leftMagnitude =
        -tangents.left;

    const float downMagnitude =
        -tangents.down;

    *tanHalfFovX =
        leftMagnitude > tangents.right
            ? leftMagnitude
            : tangents.right;

    *tanHalfFovY =
        downMagnitude > tangents.up
            ? downMagnitude
            : tangents.up;

    return
        *tanHalfFovX > 0.0f &&
        *tanHalfFovY > 0.0f;
}

bool VR_GetCurrentRenderEyeProjection(
    float* tanLeft,
    float* tanRight,
    float* tanDown,
    float* tanUp)
{
    if (tanLeft == nullptr ||
        tanRight == nullptr ||
        tanDown == nullptr ||
        tanUp == nullptr ||
        g_vrCurrentRenderEye < 0 ||
        g_vrCurrentRenderEye >=
            static_cast<int>(
                kVrStereoEyeCount))
    {
        return false;
    }

    const unsigned int eyeIndex =
        static_cast<unsigned int>(
            g_vrCurrentRenderEye);

    VrEyeProjectionTangents tangents = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrProjectionMutex);

        if (!g_vrEyeProjectionValid)
        {
            return false;
        }

        tangents =
            g_vrEyeProjectionTangents[eyeIndex];
    }

    // KISAK_SP_VR_HUD_CONVERGENCE_V1
    // The captured D3D9 source must be centered so that the one shared 2D
    // HUD command list lands on the same visual ray in both eyes.  The
    // OpenXR compositor remaps this symmetric source back into the runtime's
    // exact asymmetric frustum before submission.
    const float symmetricHorizontal =
        (std::max)(-tangents.left, tangents.right);

    const float symmetricVertical =
        (std::max)(-tangents.down, tangents.up);

    if (symmetricHorizontal <= 0.0f ||
        symmetricVertical <= 0.0f)
    {
        return false;
    }

    *tanLeft = -symmetricHorizontal;
    *tanRight = symmetricHorizontal;
    *tanDown = -symmetricVertical;
    *tanUp = symmetricVertical;

    if (!g_vrLoggedProjectionApply[eyeIndex])
    {
        Com_Printf(
            0,
            "[VR] Supplying centered capture projection "
            "for HUD-converged eye %u.\n",
            eyeIndex);

        g_vrLoggedProjectionApply[eyeIndex] = true;
    }

    return true;
}

bool VR_ApplyStereoEyeOffsetForEye(
    float viewOrigin[3],
    const float viewAxis[3][3],
    const unsigned int eyeIndex)
{
    if (viewOrigin == nullptr ||
        viewAxis == nullptr ||
        eyeIndex >= kVrStereoEyeCount)
    {
        return false;
    }

    float halfIpdGameUnits =
        kVrDefaultHalfIpdGameUnits;

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        if (!g_vrHeadOrientationValid)
        {
            return false;
        }

        halfIpdGameUnits =
            g_vrHalfIpdGameUnits;
    }

    // CoD's camera basis uses row 1 as left. Left eye therefore moves in
    // +left, while right eye moves in -left.
    const float signedEyeOffset =
        eyeIndex == 0u
            ? halfIpdGameUnits
            : -halfIpdGameUnits;

    for (int worldComponent = 0;
         worldComponent < 3;
         ++worldComponent)
    {
        viewOrigin[worldComponent] +=
            signedEyeOffset *
            viewAxis[1][worldComponent];
    }

    if (!g_vrLoggedFirstStereoEyeOffset)
    {
        const VrMeasurementUnitSystem units =
            VR_GetConfiguratorSettings().measurementUnits;
        Com_Printf(
            0,
            "[VR] Applied same-frame stereo eye "
            "offsets; half IPD is %.3f %s.\n",
            VR_DisplayInches(halfIpdGameUnits, units),
            VR_DisplayLengthUnit(units));

        g_vrLoggedFirstStereoEyeOffset = true;
    }

    return true;
}


bool VR_ApplyHeadOrientation(
    float viewAxis[3][3])
{
    if (viewAxis == nullptr)
    {
        return false;
    }

    float headAxis[3][3] = {};

    {
        std::lock_guard<std::mutex> lock(
            g_vrHeadOrientationMutex);

        if (!g_vrHeadOrientationValid)
        {
            return false;
        }

        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0;
                 column < 3;
                 ++column)
            {
                headAxis[row][column] =
                    g_vrHeadOrientationAxis[row][column];
            }
        }
    }

    float originalAxis[3][3] = {};

    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0;
             column < 3;
             ++column)
        {
            originalAxis[row][column] =
                viewAxis[row][column];
        }
    }

    // The headset matrix is expressed in the camera-local
    // forward/left/up basis. Compose it with CoD4's completed
    // world-space render-camera axis.
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0;
             column < 3;
             ++column)
        {
            viewAxis[row][column] =
                headAxis[row][0] *
                    originalAxis[0][column] +
                headAxis[row][1] *
                    originalAxis[1][column] +
                headAxis[row][2] *
                    originalAxis[2][column];
        }
    }

    {
        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        memcpy(
            g_vrMountedWeaponCameraAxisWorld,
            viewAxis,
            sizeof(g_vrMountedWeaponCameraAxisWorld));

        g_vrMountedWeaponCameraAxisWorldValid = true;
    }

    if (!g_vrLoggedFirstCameraApply)
    {
        Com_Printf(
            0,
            "[VR] Applied OpenXR headset orientation "
            "to the CoD4 camera.\n");

        g_vrLoggedFirstCameraApply = true;
    }

    return true;
}

// KISAK_SP_VR_EYE_LOCAL_HUD_ALIGNMENT_V82
void VR_UpdatePackedUiScreenPlacement()
{
#ifdef KISAK_SP
    const int displayWidth =
        cls.vidConfig.displayWidth;

    const int displayHeight =
        cls.vidConfig.displayHeight;

    if (displayWidth <= 0 ||
        displayHeight <= 0)
    {
        return;
    }

    int mainStereoWidth = displayWidth;
    int scopePanelSize = 0;

    const bool packedScopeLayout =
        VR_GetPhysicalSniperScopeCaptureLayout(
            displayWidth,
            displayHeight,
            &mainStereoWidth,
            nullptr,
            nullptr,
            &scopePanelSize);

    // One 2D command list is replayed in each eye's viewport.  It therefore
    // must be authored in one-eye pixels.  Beta.12 used the combined stereo
    // width here, placing logical x=320 near an eye edge and making the live
    // editor disagree with the image seen through the headset.
    if (mainStereoWidth < 2)
    {
        return;
    }

    const int uiEyeWidth =
        mainStereoWidth / 2;

    if (uiEyeWidth <= 0)
    {
        return;
    }

    const float uiWidth =
        static_cast<float>(uiEyeWidth);

    const float uiHeight =
        static_cast<float>(
            displayHeight);

    const std::uint64_t hudLayoutRevision =
        VR_GetHudLayoutRevision();
    static std::uint64_t appliedHudLayoutRevision = 0u;
    static bool placementConfigured = false;
    static bool appliedMenuActive = false;

    const bool menuActive =
        Key_IsCatcherActive(0, 0x10);

    const bool alreadyConfigured =
        placementConfigured &&
        appliedMenuActive == menuActive &&
        scrPlaceFull.realViewportSize[0] ==
            uiWidth &&
        scrPlaceFull.realViewportSize[1] ==
            uiHeight &&
        scrPlaceFullUnsafe.realViewportSize[0] ==
            uiWidth &&
        scrPlaceFullUnsafe.realViewportSize[1] ==
            uiHeight &&
        scrPlaceView[0].realViewportSize[0] ==
            uiWidth &&
        scrPlaceView[0].realViewportSize[1] ==
            uiHeight &&
        appliedHudLayoutRevision == hudLayoutRevision;

    if (alreadyConfigured)
    {
        return;
    }

    ScrPlace_SetupUnsafeViewport(
        &scrPlaceFullUnsafe,
        0,
        0,
        uiEyeWidth,
        displayHeight);

    ScrPlace_SetupViewport(
        &scrPlaceFull,
        0,
        0,
        uiEyeWidth,
        displayHeight);

    ScrPlace_SetupViewport(
        &scrPlaceView[0],
        0,
        0,
        uiEyeWidth,
        displayHeight);

    appliedHudLayoutRevision = hudLayoutRevision;
    appliedMenuActive = menuActive;
    placementConfigured = true;

    static bool loggedGameplayPlacement = false;
    static bool loggedMenuPlacement = false;

    if (menuActive)
    {
        if (!loggedMenuPlacement)
        {
            Com_Printf(
                0,
                "[VR][UI] V88 isolated menu ScreenPlacement from "
                "the configurable gameplay HUD safe area in one "
                "%d x %d eye.\n",
                uiEyeWidth,
                displayHeight);

            loggedMenuPlacement = true;
        }
    }
    else if (!loggedGameplayPlacement)
    {
        Com_Printf(
            0,
            "[VR][HUD] V82 authored the shared 2D command list in "
            "one %d x %d eye; it is replayed identically in both eyes. "
            "Companion eye: %d px. Dedicated scope panel: %s (%d px). "
            "Neither is part of the eye-local HUD layout.\n",
            uiEyeWidth,
            displayHeight,
            mainStereoWidth - uiEyeWidth,
            packedScopeLayout ? "active" : "inactive",
            scopePanelSize);

        loggedGameplayPlacement = true;
    }
#endif
}

float VR_ReadOpenVrOutputScale()
{
    constexpr float defaultOutputScale = 1.00f;
    constexpr float minimumOutputScale = 0.50f;
    constexpr float maximumOutputScale = 1.00f;

    const char* requestedOutputScale =
        std::getenv(
            "KISAK_VR_OUTPUT_SCALE");

    if (requestedOutputScale == nullptr ||
        requestedOutputScale[0] == '\0')
    {
        return defaultOutputScale;
    }

    char* parseEnd = nullptr;

    const float parsedOutputScale =
        std::strtof(
            requestedOutputScale,
            &parseEnd);

    if (parseEnd == requestedOutputScale ||
        parseEnd == nullptr ||
        parseEnd[0] != '\0' ||
        !std::isfinite(parsedOutputScale) ||
        parsedOutputScale < minimumOutputScale ||
        parsedOutputScale > maximumOutputScale)
    {
        Com_PrintWarning(
            0,
            "[VR][OPENVR] Ignoring invalid "
            "KISAK_VR_OUTPUT_SCALE='%s'; using %.2f. "
            "Valid range is %.2f through %.2f.\n",
            requestedOutputScale,
            defaultOutputScale,
            minimumOutputScale,
            maximumOutputScale);

        return defaultOutputScale;
    }

    return parsedOutputScale;
}

bool VR_ConfigureOpenVrViews()
{
    if (g_vrOpenVrSystem == nullptr)
    {
        return false;
    }

    g_vrViews.resize(kVrStereoEyeCount);

    std::array<
        VrEyeProjectionTangents,
        kVrStereoEyeCount>
        publishedTangents = {};

    for (std::uint32_t eyeIndex = 0u;
         eyeIndex < kVrStereoEyeCount;
         ++eyeIndex)
    {
        const vr::EVREye eye =
            eyeIndex == 0u
                ? vr::Eye_Left
                : vr::Eye_Right;

        float left = 0.0f;
        float right = 0.0f;
        float top = 0.0f;
        float bottom = 0.0f;

        g_vrOpenVrSystem->GetProjectionRaw(
            eye,
            &left,
            &right,
            &top,
            &bottom);

        VrEyeProjectionTangents& tangents =
            publishedTangents[eyeIndex];

        // KISAK_SP_VR_OPENVR_PROJECTION_CONVENTION_V77
        // GetProjectionRaw reports negative top and positive bottom in
        // OpenVR's texture convention. Those values map directly to the
        // negative down and positive up tangents consumed by KisakCOD's
        // OpenXR-style renderer. Negating and exchanging them mirrored the
        // vertical optical asymmetry and warped SteamVR gameplay.
        tangents.left = left;
        tangents.right = right;
        tangents.down = top;
        tangents.up = bottom;

        if (!std::isfinite(tangents.left) ||
            !std::isfinite(tangents.right) ||
            !std::isfinite(tangents.down) ||
            !std::isfinite(tangents.up) ||
            tangents.left >= 0.0f ||
            tangents.right <= 0.0f ||
            tangents.down >= 0.0f ||
            tangents.up <= 0.0f ||
            tangents.left >= tangents.right ||
            tangents.down >= tangents.up)
        {
            Com_PrintWarning(
                0,
                "[VR][OPENVR] Rejected invalid raw projection "
                "for eye %u: %.4f %.4f %.4f %.4f.\n",
                eyeIndex,
                left,
                right,
                top,
                bottom);

            return false;
        }

        XrView& view =
            g_vrViews[eyeIndex];

        view = XrView{XR_TYPE_VIEW};
        view.pose.orientation.w = 1.0f;
        view.fov.angleLeft =
            std::atan(tangents.left);
        view.fov.angleRight =
            std::atan(tangents.right);
        view.fov.angleDown =
            std::atan(tangents.down);
        view.fov.angleUp =
            std::atan(tangents.up);
    }

    {
        std::lock_guard<std::mutex> lock(
            g_vrProjectionMutex);

        g_vrEyeProjectionTangents =
            publishedTangents;

        g_vrEyeProjectionValid = true;
    }

    Com_Printf(
        0,
        "[VR][OPENVR] Published per-eye projection tangents: "
        "L %.4f %.4f %.4f %.4f, "
        "R %.4f %.4f %.4f %.4f.\n",
        publishedTangents[0].left,
        publishedTangents[0].right,
        publishedTangents[0].down,
        publishedTangents[0].up,
        publishedTangents[1].left,
        publishedTangents[1].right,
        publishedTangents[1].down,
        publishedTangents[1].up);

    g_vrLoggedProjectionPublish = true;
    return true;
}

bool VR_CreateOpenVrEyeTargets()
{
    if (g_vrOpenVrSystem == nullptr ||
        g_vrD3dDevice == nullptr)
    {
        return false;
    }

    std::uint32_t recommendedWidth = 0u;
    std::uint32_t recommendedHeight = 0u;

    g_vrOpenVrSystem->GetRecommendedRenderTargetSize(
        &recommendedWidth,
        &recommendedHeight);

    if (recommendedWidth == 0u ||
        recommendedHeight == 0u)
    {
        Com_PrintWarning(
            0,
            "[VR][OPENVR] SteamVR returned an empty recommended "
            "render-target size.\n");
        return false;
    }

    g_vrOutputScale =
        VR_ReadOpenVrOutputScale();

    const std::uint32_t targetWidth =
        (std::min)(
            16384u,
            (std::max)(
                256u,
                static_cast<std::uint32_t>(
                    static_cast<float>(recommendedWidth) *
                        g_vrOutputScale +
                    0.5f)));

    const std::uint32_t targetHeight =
        (std::min)(
            16384u,
            (std::max)(
                256u,
                static_cast<std::uint32_t>(
                    static_cast<float>(recommendedHeight) *
                        g_vrOutputScale +
                    0.5f)));

    for (std::uint32_t eyeIndex = 0u;
         eyeIndex < kVrStereoEyeCount;
         ++eyeIndex)
    {
        VrOpenVrEyeTarget& target =
            g_vrOpenVrEyeTargets[eyeIndex];

        target.renderTargetView.Reset();
        target.texture.Reset();

        D3D11_TEXTURE2D_DESC description = {};
        description.Width = targetWidth;
        description.Height = targetHeight;
        description.MipLevels = 1u;
        description.ArraySize = 1u;
        description.Format =
            DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1u;
        description.Usage =
            D3D11_USAGE_DEFAULT;
        description.BindFlags =
            D3D11_BIND_RENDER_TARGET |
            D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags =
            D3D11_RESOURCE_MISC_SHARED;

        HRESULT hr =
            g_vrD3dDevice->CreateTexture2D(
                &description,
                nullptr,
                target.texture.GetAddressOf());

        if (FAILED(hr) || target.texture == nullptr)
        {
            VR_LogHrFailure(
                "CreateTexture2D(OpenVR eye target)",
                hr);
            return false;
        }

        hr =
            g_vrD3dDevice->CreateRenderTargetView(
                target.texture.Get(),
                nullptr,
                target.renderTargetView.GetAddressOf());

        if (FAILED(hr) ||
            target.renderTargetView == nullptr)
        {
            VR_LogHrFailure(
                "CreateRenderTargetView(OpenVR eye target)",
                hr);
            return false;
        }

        target.width =
            static_cast<int32_t>(targetWidth);
        target.height =
            static_cast<int32_t>(targetHeight);
    }

    Com_Printf(
        0,
        "[VR][OPENVR] Eye targets are %u x %u at output scale "
        "%.2f; SteamVR recommended %u x %u.\n",
        targetWidth,
        targetHeight,
        g_vrOutputScale,
        recommendedWidth,
        recommendedHeight);

    return true;
}

void VR_LogOpenVrIdentity()
{
    if (g_vrOpenVrSystem == nullptr)
    {
        return;
    }

    char runtimePath[1024] = {};
    std::uint32_t requiredRuntimePath = 0u;

    if (vr::VR_GetRuntimePath(
            runtimePath,
            static_cast<std::uint32_t>(
                sizeof(runtimePath)),
            &requiredRuntimePath))
    {
        Com_Printf(
            0,
            "[VR][OPENVR] Runtime path: %s.\n",
            runtimePath);
    }

    char trackingSystem[256] = {};
    char modelNumber[256] = {};

    vr::ETrackedPropertyError propertyError =
        vr::TrackedProp_Success;

    g_vrOpenVrSystem->GetStringTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd,
        vr::Prop_TrackingSystemName_String,
        trackingSystem,
        static_cast<std::uint32_t>(
            sizeof(trackingSystem)),
        &propertyError);

    propertyError =
        vr::TrackedProp_Success;

    g_vrOpenVrSystem->GetStringTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd,
        vr::Prop_ModelNumber_String,
        modelNumber,
        static_cast<std::uint32_t>(
            sizeof(modelNumber)),
        &propertyError);

    Com_Printf(
        0,
        "[VR][OPENVR] Runtime/headset: %s / %s; "
        "SDK 2.15.6; process x86.\n",
        trackingSystem[0] != '\0'
            ? trackingSystem
            : "SteamVR",
        modelNumber[0] != '\0'
            ? modelNumber
            : "unknown HMD");

    std::snprintf(
        g_vrCompatibilityRuntimeName.data(),
        g_vrCompatibilityRuntimeName.size(),
        "%s",
        trackingSystem[0] != '\0' ? trackingSystem : "SteamVR");
    std::snprintf(
        g_vrCompatibilityHeadsetName.data(),
        g_vrCompatibilityHeadsetName.size(),
        "%s",
        modelNumber[0] != '\0' ? modelNumber : "unknown HMD");
}

bool VR_InitOpenVrFallback(
    const char* openXrFailure)
{
    std::array<char, 1024> preservedOpenXrFailure = {};

    std::snprintf(
        preservedOpenXrFailure.data(),
        preservedOpenXrFailure.size(),
        "%s",
        openXrFailure != nullptr &&
                openXrFailure[0] != '\0'
            ? openXrFailure
            : "OpenXR was skipped or unavailable.");

    Com_Printf(
        0,
        "[VR][OPENVR] V49 attempting the 32-bit SteamVR "
        "fallback after: %s\n",
        preservedOpenXrFailure.data());

    vr::EVRInitError initError =
        vr::VRInitError_None;

    g_vrOpenVrSystem =
        vr::VR_Init(
            &initError,
            vr::VRApplication_Scene,
            "KisakCOD VR V49");

    if (initError != vr::VRInitError_None ||
        g_vrOpenVrSystem == nullptr)
    {
        VR_RecordOpenVrStartupFailure(
            preservedOpenXrFailure.data(),
            "initialization failed",
            vr::VR_GetVRInitErrorAsSymbol(
                initError),
            static_cast<int>(initError));

        Com_PrintWarning(
            0,
            "[VR][OPENVR] %s\n",
            g_vrLastStartupError.data());

        g_vrOpenVrSystem = nullptr;
        return false;
    }

    g_vrRuntimeBackend =
        VrRuntimeBackend::OpenVr;
    g_vrOpenVrInitialized = true;

    g_vrOpenVrCompositor =
        vr::VRCompositor();

    if (g_vrOpenVrCompositor == nullptr)
    {
        VR_RecordOpenVrStartupFailure(
            preservedOpenXrFailure.data(),
            "could not acquire IVRCompositor",
            "VRInitError_Init_InterfaceNotFound",
            static_cast<int>(
                vr::VRInitError_Init_InterfaceNotFound));

        vr::VR_Shutdown();
        g_vrOpenVrSystem = nullptr;
        g_vrOpenVrInitialized = false;
        g_vrRuntimeBackend =
            VrRuntimeBackend::None;
        return false;
    }

    g_vrOpenVrCompositor->SetTrackingSpace(
        vr::TrackingUniverseStanding);

    g_vrOpenVrRenderModels =
        vr::VRRenderModels();

    if (g_vrOpenVrRenderModels == nullptr)
    {
        Com_PrintWarning(
            0,
            "[VR][OPENVR][POSE] IVRRenderModels is unavailable; "
            "controller poses will retain the raw-device fallback.\n");
    }
    else
    {
        Com_Printf(
            0,
            "[VR][OPENVR][POSE] V77 semantic grip/aim component "
            "discovery is available.\n");
    }

    VR_LogOpenVrIdentity();

    if (!VR_CreateOpenVrD3D11Device() ||
        !VR_ConfigureOpenVrViews() ||
        !VR_CreateOpenVrEyeTargets() ||
        !VR_CreateHeadTrackedScene() ||
        !VR_CreateCapturedFrameBlitResources())
    {
        std::array<char, 1024> openVrDetail = {};

        std::snprintf(
            openVrDetail.data(),
            openVrDetail.size(),
            "%s",
            g_vrLastStartupError[0] != '\0'
                ? g_vrLastStartupError.data()
                : "OpenVR graphics initialization failed.");

        vr::VR_Shutdown();
        g_vrOpenVrRenderModels = nullptr;
        g_vrOpenVrCompositor = nullptr;
        g_vrOpenVrSystem = nullptr;
        g_vrOpenVrInitialized = false;
        g_vrRuntimeBackend =
            VrRuntimeBackend::None;

        std::snprintf(
            g_vrLastStartupError.data(),
            g_vrLastStartupError.size(),
            "%s OpenVR fallback graphics setup failed: %s",
            preservedOpenXrFailure.data(),
            openVrDetail.data());

        return false;
    }

    VR_D3D9CaptureSetEnabled(true);

    g_vrInitialized = true;
    g_vrSessionRunning = true;
    g_vrSessionState =
        XR_SESSION_STATE_FOCUSED;
    g_vrLastStartupError[0] = '\0';

    KisakCrash_SetVrState(
        true,
        true,
        static_cast<int>(
            g_vrSessionState),
        0u,
        0u,
        0u);

    VR_UpdatePackedUiScreenPlacement();

    Com_Printf(
        0,
        "[VR][OPENVR] V57 x86 SteamVR fallback is ready: "
        "head/controller poses, Controller Input V4, and stereo "
        "submission enabled.\n");

    VR_AppendCompatibilityRuntimeReceipt();

    return true;
}

bool VR_Init()
{
    g_vrLastStartupError[0] = '\0';

    VR_ResetHeadOrientation();

    {
        std::lock_guard<std::mutex> lock(
            g_vrProjectionMutex);

        g_vrEyeProjectionTangents = {};
        g_vrEyeProjectionValid = false;
    }

    g_vrCurrentRenderEye = -1;
    g_vrLoggedProjectionApply.fill(false);
    g_vrLoggedProjectionPublish = false;

    g_vrCapturedStereoViews = {};
    g_vrCapturedStereoViewsValid = false;
    g_vrLoggedCapturedPoseMatch = false;
    g_vrLoggedCapturedPoseMiss = false;
    g_vrCapturedRenderPoseNanoseconds = 0u;
    g_vrCapturedStereoMetadata = {};
    g_vrCapturedStereoPoseMatched = false;

    {
        std::lock_guard<std::mutex> lock(
            g_vrPublishedRenderViewsMutex);

        g_vrPublishedRenderViews = {};
        g_vrPublishedRenderViewsValid = false;
        g_vrPublishedRenderPoseNanoseconds = 0u;
    }

    {
        std::lock_guard<std::mutex> lock(
            g_vrRenderPoseHistoryMutex);

        g_vrRenderPoseHistory = {};
        g_vrRenderPoseHistoryWriteIndex = 0u;
    }
    if (g_vrInitialized)
    {
        return true;
    }

    const char* requestedBackend =
        std::getenv("KISAK_VR_BACKEND");

    bool forceOpenXr = false;
    bool forceOpenVr = false;

    if (requestedBackend != nullptr &&
        requestedBackend[0] != '\0' &&
        _stricmp(requestedBackend, "auto") != 0)
    {
        forceOpenXr =
            _stricmp(requestedBackend, "openxr") == 0;

        forceOpenVr =
            _stricmp(requestedBackend, "openvr") == 0;

        if (!forceOpenXr && !forceOpenVr)
        {
            Com_PrintWarning(
                0,
                "[VR][STARTUP] Ignoring unknown "
                "KISAK_VR_BACKEND='%s'; valid values are "
                "auto, openxr, and openvr.\n",
                requestedBackend);
        }
    }

    Com_Printf(
        0,
        "[VR][STARTUP] V49 backend policy: %s; OpenXR "
        "remains primary and x86 OpenVR is the fallback.\n",
        forceOpenVr
            ? "forced OpenVR"
            : forceOpenXr
                ? "forced OpenXR"
                : "automatic");

    if (forceOpenVr)
    {
        return VR_InitOpenVrFallback(
            "OpenXR was skipped by KISAK_VR_BACKEND=openvr.");
    }

    Com_Printf(
        0,
        "[VR] Initializing OpenXR head-rotation frame bridge...\n");

    Com_Printf(
        0,
        "[VR][STARTUP] V31 fail-fast OpenXR diagnostics enabled.\n");

    if (!VR_HasInstanceExtension(
            XR_KHR_D3D11_ENABLE_EXTENSION_NAME))
    {
        Com_PrintWarning(
            0,
            "[VR] Runtime does not expose "
            "XR_KHR_D3D11_enable.\n");

        if (g_vrLastStartupError[0] == '\0')
        {
            VR_RecordStartupFailure(
                "OpenXR extension check",
                "XR_KHR_D3D11_enable is unavailable",
                -9,
                "Select a Windows OpenXR runtime that supports D3D11.");
        }

        if (!forceOpenXr)
        {
            return VR_InitOpenVrFallback(
                g_vrLastStartupError.data());
        }

        return false;
    }

    const bool palmPoseExtensionAvailable =
        VR_HasInstanceExtension(
            "XR_EXT_palm_pose");

    std::vector<const char*> enabledExtensions = {
        XR_KHR_D3D11_ENABLE_EXTENSION_NAME,
    };

    enabledExtensions.reserve(
        2u + VrInput::kOpenXrProfileCount);

    if (palmPoseExtensionAvailable)
    {
        enabledExtensions.push_back(
            "XR_EXT_palm_pose");
    }

    g_vrEnabledOpenXrProfiles.fill(false);

    for (const VrInput::OpenXrProfileDefinition& profile :
         VrInput::OpenXrProfileDefinitions())
    {
        bool enabled = profile.requiredExtension == nullptr;

        if (!enabled)
        {
            const auto alreadyEnabled =
                std::find_if(
                    enabledExtensions.begin(),
                    enabledExtensions.end(),
                    [&](const char* const extension)
                    {
                        return std::strcmp(
                                   extension,
                                   profile.requiredExtension) == 0;
                    });

            if (alreadyEnabled != enabledExtensions.end())
            {
                enabled = true;
            }
            else if (VR_HasInstanceExtension(
                         profile.requiredExtension))
            {
                enabledExtensions.push_back(
                    profile.requiredExtension);
                enabled = true;
            }
        }

        g_vrEnabledOpenXrProfiles[
            static_cast<std::size_t>(profile.profile)] =
            enabled;
    }

    XrInstanceCreateInfo createInfo{
        XR_TYPE_INSTANCE_CREATE_INFO
    };

    std::snprintf(
        createInfo.applicationInfo.applicationName,
        XR_MAX_APPLICATION_NAME_SIZE,
        "%s",
        "KisakCOD VR");

    createInfo.applicationInfo.applicationVersion = 1;

    std::snprintf(
        createInfo.applicationInfo.engineName,
        XR_MAX_ENGINE_NAME_SIZE,
        "%s",
        "IW3 / KisakCOD");

    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion =
        XR_MAKE_VERSION(1, 0, 0);

    createInfo.enabledExtensionCount =
        static_cast<std::uint32_t>(
            enabledExtensions.size());
    createInfo.enabledExtensionNames =
        enabledExtensions.data();

    XrResult result =
        xrCreateInstance(&createInfo, &g_vrInstance);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrCreateInstance", result);
        VR_ResetState();
        return false;
    }

    g_vrPalmPoseExtensionEnabled =
        palmPoseExtensionAvailable;

    Com_Printf(
        0,
        "[VR][HANDS] XR_EXT_palm_pose %s; the free off hand will %s.\n",
        g_vrPalmPoseExtensionEnabled
            ? "is enabled"
            : "is unavailable",
        g_vrPalmPoseExtensionEnabled
            ? "use the runtime's controller-specific palm surface pose"
            : "fall back to V24's rigid grip pose");

    XrInstanceProperties runtimeProperties{
        XR_TYPE_INSTANCE_PROPERTIES
    };

    result =
        xrGetInstanceProperties(
            g_vrInstance,
            &runtimeProperties);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetInstanceProperties",
            result);

        VR_Shutdown();
        return false;
    }

    Com_Printf(
        0,
        "[VR] OpenXR runtime: %s.\n",
        runtimeProperties.runtimeName);

    std::snprintf(
        g_vrCompatibilityRuntimeName.data(),
        g_vrCompatibilityRuntimeName.size(),
        "%s",
        runtimeProperties.runtimeName);

    XrSystemGetInfo systemInfo{
        XR_TYPE_SYSTEM_GET_INFO
    };

    systemInfo.formFactor =
        XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    result =
        xrGetSystem(
            g_vrInstance,
            &systemInfo,
            &g_vrSystemId);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrGetSystem", result);
        VR_Shutdown();
        return false;
    }

    XrSystemProperties systemProperties{
        XR_TYPE_SYSTEM_PROPERTIES
    };

    result =
        xrGetSystemProperties(
            g_vrInstance,
            g_vrSystemId,
            &systemProperties);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure(
            "xrGetSystemProperties",
            result);

        VR_Shutdown();
        return false;
    }

    Com_Printf(
        0,
        "[VR] OpenXR system: %s.\n",
        systemProperties.systemName);

    std::snprintf(
        g_vrCompatibilityHeadsetName.data(),
        g_vrCompatibilityHeadsetName.size(),
        "%s",
        systemProperties.systemName);

    if (!VR_CreateControllerActions())
    {
        Com_PrintWarning(
            0,
            "[VR] Controller action setup failed. "
            "Rendering will continue without "
            "controller tracking.\n");
    }

    if (!VR_CreateSession())
    {
        VR_Shutdown();
        return false;
    }

    if (!VR_CreateSwapchains())
    {
        VR_Shutdown();
        return false;
    }

    if (!VR_CreateHeadTrackedScene())
    {
        VR_Shutdown();
        return false;
    }

    if (!VR_CreateCapturedFrameBlitResources())
    {
        VR_Shutdown();
        return false;
    }

    if (!VR_CreateControllerProxyResources())
    {
        Com_PrintWarning(
            0,
            "[VR] Controller proxy renderer could "
            "not be created. Controller tracking "
            "will continue without visible markers.\n");
    }

    VR_D3D9CaptureSetEnabled(true);

    g_vrRuntimeBackend =
        VrRuntimeBackend::OpenXr;
    g_vrInitialized = true;

    KisakCrash_SetVrState(
        g_vrInitialized,
        g_vrSessionRunning,
        static_cast<int>(g_vrSessionState),
        static_cast<unsigned int>(
            g_vrUploadedStereoSerial & 0xFFFFFFFFu),
        g_vrCapturedStereoWidth,
        g_vrCapturedStereoHeight);

    VR_UpdatePackedUiScreenPlacement();

    Com_Printf(
        0,
        "[VR] OpenXR D3D11 session and swapchains are ready.\n");

    VR_AppendCompatibilityRuntimeReceipt();

    return true;
}

void VR_PollOpenVrEvents()
{
    if (g_vrOpenVrSystem == nullptr)
    {
        return;
    }

    vr::VREvent_t event = {};

    while (g_vrOpenVrSystem->PollNextEvent(
               &event,
               sizeof(event)))
    {
        if (event.eventType ==
            vr::VREvent_Quit)
        {
            Com_PrintWarning(
                0,
                "[VR][OPENVR] SteamVR requested application "
                "shutdown.\n");

            g_vrOpenVrSystem->AcknowledgeQuit_Exiting();
            g_vrExitRequested = true;
        }
    }
}

bool VR_UpdateOpenVrHeadPose()
{
    if (g_vrOpenVrSystem == nullptr ||
        g_vrOpenVrCompositor == nullptr ||
        g_vrViews.size() < kVrStereoEyeCount)
    {
        return false;
    }

    const vr::EVRCompositorError waitResult =
        g_vrOpenVrCompositor->WaitGetPoses(
            g_vrOpenVrRenderPoses.data(),
            static_cast<std::uint32_t>(
                g_vrOpenVrRenderPoses.size()),
            nullptr,
            0u);

    if (waitResult !=
        vr::VRCompositorError_None)
    {
        Com_PrintWarning(
            0,
            "[VR][OPENVR] WaitGetPoses failed with "
            "EVRCompositorError %d.\n",
            static_cast<int>(waitResult));
        return false;
    }

    const vr::TrackedDevicePose_t& hmdPose =
        g_vrOpenVrRenderPoses[
            vr::k_unTrackedDeviceIndex_Hmd];

    if (!hmdPose.bDeviceIsConnected ||
        !hmdPose.bPoseIsValid)
    {
        return false;
    }

    const XrPosef headPose =
        VR_OpenVrMatrixToPose(
            hmdPose.mDeviceToAbsoluteTracking);

    for (std::uint32_t eyeIndex = 0u;
         eyeIndex < kVrStereoEyeCount;
         ++eyeIndex)
    {
        const vr::EVREye eye =
            eyeIndex == 0u
                ? vr::Eye_Left
                : vr::Eye_Right;

        const XrPosef eyeToHeadPose =
            VR_OpenVrMatrixToPose(
                g_vrOpenVrSystem
                    ->GetEyeToHeadTransform(eye));

        g_vrViews[eyeIndex].pose =
            VR_ComposePose(
                headPose,
                eyeToHeadPose);
    }

    if (!g_vrOpenVrLoggedFirstPose)
    {
        Com_Printf(
            0,
            "[VR][OPENVR] Received the first valid predicted "
            "HMD pose from SteamVR.\n");

        g_vrOpenVrLoggedFirstPose = true;
    }

    return true;
}

const char* VR_FindOpenVrControllerComponent(
    const char* renderModelName,
    const std::array<const char*, 2>& candidates)
{
    if (g_vrOpenVrRenderModels == nullptr ||
        renderModelName == nullptr ||
        renderModelName[0] == '\0')
    {
        return nullptr;
    }

    for (const char* candidate : candidates)
    {
        if (candidate != nullptr &&
            g_vrOpenVrRenderModels->RenderModelHasComponent(
                renderModelName,
                candidate))
        {
            return candidate;
        }
    }

    return nullptr;
}

bool VR_ResolveOpenVrControllerPoseComponents(
    const std::size_t handIndex,
    const VrInput::OpenVrHandState& hand)
{
    if (handIndex >=
            g_vrOpenVrControllerPoseComponents.size() ||
        g_vrOpenVrSystem == nullptr ||
        g_vrOpenVrRenderModels == nullptr ||
        hand.deviceIndex ==
            vr::k_unTrackedDeviceIndexInvalid)
    {
        return false;
    }

    VrOpenVrControllerPoseComponents& components =
        g_vrOpenVrControllerPoseComponents[handIndex];

    if (components.deviceIndex != hand.deviceIndex)
    {
        components = {};
        components.deviceIndex = hand.deviceIndex;
    }

    if (components.resolved)
    {
        return components.gripComponent != nullptr ||
            components.palmComponent != nullptr ||
            components.aimComponent != nullptr;
    }

    vr::ETrackedPropertyError propertyError =
        vr::TrackedProp_Success;

    g_vrOpenVrSystem->GetStringTrackedDeviceProperty(
        hand.deviceIndex,
        vr::Prop_RenderModelName_String,
        components.renderModelName.data(),
        static_cast<std::uint32_t>(
            components.renderModelName.size()),
        &propertyError);

    components.resolved = true;

    if (propertyError != vr::TrackedProp_Success ||
        components.renderModelName[0] == '\0')
    {
        Com_PrintWarning(
            0,
            "[VR][OPENVR][POSE] SteamVR did not publish a "
            "render-model name for the %s controller; using its "
            "raw device pose.\n",
            VR_ControllerHandName(
                static_cast<std::uint32_t>(handIndex)));
        return false;
    }

    const std::array<const char*, 2> gripCandidates = {
        vr::k_pch_Controller_Component_OpenXR_Grip,
        vr::k_pch_Controller_Component_HandGrip,
    };

    const std::array<const char*, 2> aimCandidates = {
        vr::k_pch_Controller_Component_OpenXR_Aim,
        vr::k_pch_Controller_Component_Tip,
    };

    const std::array<const char*, 2> palmCandidates = {
        vr::k_pch_Controller_Component_OpenXR_HandModel,
        nullptr,
    };

    components.gripComponent =
        VR_FindOpenVrControllerComponent(
            components.renderModelName.data(),
            gripCandidates);

    components.palmComponent =
        VR_FindOpenVrControllerComponent(
            components.renderModelName.data(),
            palmCandidates);

    components.aimComponent =
        VR_FindOpenVrControllerComponent(
            components.renderModelName.data(),
            aimCandidates);

    Com_Printf(
        0,
        "[VR][OPENVR][POSE] V77 %s controller render model "
        "'%s': grip component %s; hand-model component %s; "
        "aim component %s.\n",
        VR_ControllerHandName(
            static_cast<std::uint32_t>(handIndex)),
        components.renderModelName.data(),
        components.gripComponent != nullptr
            ? components.gripComponent
            : "unavailable (raw-device fallback)",
        components.palmComponent != nullptr
            ? components.palmComponent
            : "unavailable",
        components.aimComponent != nullptr
            ? components.aimComponent
            : "unavailable (raw-device fallback)");

    if (components.aimComponent == nullptr)
    {
        Com_PrintWarning(
            0,
            "[VR][OPENVR][POSE] The %s controller has no "
            "openxr_aim or tip component; SteamVR cannot supply "
            "a portable weapon-aim basis for this device.\n",
            VR_ControllerHandName(
                static_cast<std::uint32_t>(handIndex)));
    }

    return components.gripComponent != nullptr ||
        components.palmComponent != nullptr ||
        components.aimComponent != nullptr;
}

bool VR_LocateOpenVrControllerComponentPose(
    const VrInput::OpenVrHandState& hand,
    const VrOpenVrControllerPoseComponents& components,
    const char* componentName,
    const XrPosef& devicePose,
    XrPosef* componentPose)
{
    if (g_vrOpenVrRenderModels == nullptr ||
        !components.resolved ||
        componentName == nullptr ||
        components.renderModelName[0] == '\0' ||
        componentPose == nullptr)
    {
        return false;
    }

    vr::RenderModel_ControllerMode_State_t modeState = {};
    vr::RenderModel_ComponentState_t componentState = {};

    if (!g_vrOpenVrRenderModels->GetComponentState(
            components.renderModelName.data(),
            componentName,
            &hand.controllerState,
            &modeState,
            &componentState))
    {
        return false;
    }

    // mTrackingToComponentLocal is the portable attachment coordinate system
    // and defines -Z as pointing out from the component surface. Compose it
    // after the absolute tracked-device transform, matching OpenXR grip/aim
    // action-space semantics without a controller-specific Euler correction.
    *componentPose =
        VR_ComposePose(
            devicePose,
            VR_OpenVrMatrixToPose(
                componentState
                    .mTrackingToComponentLocal));

    return true;
}

bool VR_UpdateOpenVrControllerActions()
{
    if (g_vrOpenVrSystem == nullptr)
    {
        return false;
    }

    const std::array<VrInput::Hand, 2> handTypes = {
        VrInput::Hand::Left,
        VrInput::Hand::Right,
    };

    bool anyController = false;

    for (std::size_t handIndex = 0u;
         handIndex < handTypes.size();
         ++handIndex)
    {
        const bool stateValid =
            VrInput::RefreshOpenVrHandState(
                g_vrOpenVrSystem,
                handTypes[handIndex],
                &g_vrOpenVrHands[handIndex]);

        anyController = anyController || stateValid;

        if (stateValid &&
            !g_vrOpenVrLoggedController[handIndex])
        {
            const std::string description =
                VrInput::OpenVrHandDescription(
                    g_vrOpenVrHands[handIndex]);

            Com_Printf(
                0,
                "[VR][OPENVR][CONTROLS] Connected %s. Legacy "
                "SteamVR component discovery is active.\n",
                description.c_str());

            if (VrInput::IsOpenVrIndexController(
                    g_vrOpenVrHands[handIndex]))
            {
                Com_Printf(
                    0,
                    "[VR][OPENVR][CONTROLS] V98 Index physical "
                    "squeeze accepts either analog Axis2 or its "
                    "digital press bit.\n");
            }

            std::snprintf(
                g_vrCompatibilityControllerProfiles[handIndex].data(),
                g_vrCompatibilityControllerProfiles[handIndex].size(),
                "%s",
                description.c_str());

            g_vrOpenVrLoggedController[handIndex] = true;
            VR_AppendCompatibilityRuntimeReceipt();
        }
        else if (!stateValid)
        {
            g_vrOpenVrLoggedController[handIndex] = false;
            g_vrOpenVrControllerPoseComponents[handIndex] = {};
        }
    }

    VrInputHeldState inputHeld = {};
    VrInputVectorState inputVectors = {};
    VrInputActiveState inputVectorActive = {};
    bool missionMovementLockHeld = false;

    bool missionTouchActive = false;
    const bool missionTouchHeld =
        VrInput::GetOpenVrBooleanSourceState(
            g_vrOpenVrHands,
            VrInput::Source::RightThumbrestTouch,
            &missionTouchActive);
    bool leftPrimaryAxisActive = false;
    const VrInput::OpenVrVector2 leftPrimaryAxis =
        VrInput::GetOpenVrVector2SourceState(
            g_vrOpenVrHands,
            VrInput::Source::LeftPrimaryAxis,
            &leftPrimaryAxisActive);
    bool rightPrimaryAxisActive = false;
    const VrInput::OpenVrVector2 rightPrimaryAxis =
        VrInput::GetOpenVrVector2SourceState(
            g_vrOpenVrHands,
            VrInput::Source::RightPrimaryAxis,
            &rightPrimaryAxisActive);
    const VrInput::OpenVrMissionSelectorUpdate missionSelector =
        VrInput::UpdateOpenVrMissionSelector(
            &g_vrOpenVrMissionSelector,
            missionTouchActive,
            missionTouchHeld,
            leftPrimaryAxis,
            leftPrimaryAxisActive,
            rightPrimaryAxis,
            rightPrimaryAxisActive);

    if (missionSelector.available &&
        !g_vrOpenVrLoggedMissionSelector)
    {
        Com_Printf(
            0,
            "[VR][OPENVR][CONTROLS] V79 guarded mission selector is "
            "active: start with both sticks centered, touch the centered "
            "right stick, then move the left stick; right-stick movement "
            "cancels selection.\n");
        g_vrOpenVrLoggedMissionSelector = true;
    }

    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    bool nightVisionGestureGripActive = false;
    const bool nightVisionGestureGripPressed =
        VrInput::GetOpenVrBooleanSourceState(
            g_vrOpenVrHands,
            VrInput::Source::LeftSqueeze,
            &nightVisionGestureGripActive);

    const VrControllerRenderPose&
        nightVisionGesturePose =
            g_vrControllerRenderPoses[
                VR_CONTROLLER_LEFT];

    const VrGestures::NightVisionVisorUpdate
        nightVisionGesture =
            VR_UpdateNightVisionVisorGesture(
                nightVisionGesturePose.gripValid,
                nightVisionGesturePose.gripPose,
                nightVisionGestureGripActive,
                nightVisionGestureGripPressed,
                "OpenVR/SteamVR");

    for (const VrInput::ActionDefinition& action :
         VrInput::ActionDefinitions())
    {
        if (action.valueType != VrInput::ValueType::Boolean)
        {
            continue;
        }

        bool held = false;
        const std::size_t actionIndex =
            static_cast<std::size_t>(action.action);

        for (std::size_t bindingIndex = 0u;
             bindingIndex < 2u;
             ++bindingIndex)
        {
            const VrInput::Binding& binding =
                configurable.bindings[actionIndex][bindingIndex];
            if (binding.sourceCount == 0u)
            {
                continue;
            }

            const bool missionShortcut =
                action.action == VrInput::Action::GrenadeLauncher ||
                action.action == VrInput::Action::NightVision ||
                action.action == VrInput::Action::Airstrike ||
                action.action == VrInput::Action::C4;
            const bool guardedMissionBinding =
                missionShortcut &&
                VrInput::UsesOpenVrMissionSelector(binding);

            bool chordHeld = true;
            for (std::size_t termIndex = 0u;
                 termIndex < binding.sourceCount;
                 ++termIndex)
            {
                const VrInput::Source source =
                    binding.sources[termIndex];
                bool sourceActive = false;
                bool sourceHeld = false;

                if (VrInput::IsDirectionalSource(source))
                {
                    const VrInput::OpenVrVector2 value =
                        VrInput::GetOpenVrVector2SourceState(
                            g_vrOpenVrHands,
                            VrInput::PhysicalSource(source),
                            &sourceActive);
                    bool& latched =
                        g_vrDirectionalTermLatched[actionIndex]
                            [bindingIndex][termIndex];

                    if (!sourceActive ||
                        VrInput::DirectionalSourceReleased(
                            source,
                            value.x,
                            value.y))
                    {
                        latched = false;
                    }
                    else if (!latched)
                    {
                        const bool deliberateDirection =
                            action.action ==
                                VrInput::Action::Jump ||
                            action.action ==
                                VrInput::Action::LowerStance;

                        latched =
                            VrInput::DirectionalSourcePressed(
                                source,
                                value.x,
                                value.y,
                                deliberateDirection ? 0.80f : 0.75f,
                                deliberateDirection ? 0.15f : 0.12f);
                    }

                    sourceHeld = latched;
                }
                else
                {
                    sourceHeld =
                        VrInput::GetOpenVrBooleanSourceState(
                            g_vrOpenVrHands,
                            source,
                            &sourceActive);
                }

                if (nightVisionGesture.consumeLeftGrip &&
                    source ==
                        VrInput::Source::LeftSqueeze)
                {
                    sourceHeld = false;
                }

                if (guardedMissionBinding &&
                    source == VrInput::Source::RightThumbrestTouch)
                {
                    sourceActive = missionSelector.available;
                    sourceHeld = missionSelector.modifierHeld;
                }

                if (missionShortcut &&
                    source == VrInput::Source::RightThumbrestTouch &&
                    sourceActive && sourceHeld)
                {
                    missionMovementLockHeld = true;
                }
                chordHeld = chordHeld &&
                    sourceActive && sourceHeld;
            }

            held = held || chordHeld;
        }

        inputHeld[actionIndex] = held;
    }

    if (nightVisionGesture.toggledThisFrame)
    {
        inputHeld[
            static_cast<std::size_t>(
                VrInput::Action::NightVision)] = true;
    }

    const auto readVectorAction = [&](
        const VrInput::Action action)
    {
        const std::size_t actionIndex =
            static_cast<std::size_t>(action);
        XrVector2f& value = inputVectors[actionIndex];
        bool& active = inputVectorActive[actionIndex];
        value = {};
        active = false;
        float selectedMagnitudeSquared = -1.0f;

        for (std::size_t bindingIndex = 0u;
             bindingIndex < 2u;
             ++bindingIndex)
        {
            const VrInput::Binding& binding =
                configurable.bindings[actionIndex][bindingIndex];
            if (binding.sourceCount != 1u)
            {
                continue;
            }

            bool sourceActive = false;
            const VrInput::OpenVrVector2 candidate =
                VrInput::GetOpenVrVector2SourceState(
                    g_vrOpenVrHands,
                    binding.sources[0],
                    &sourceActive);

            if (!sourceActive)
            {
                continue;
            }

            const float magnitudeSquared =
                candidate.x * candidate.x +
                candidate.y * candidate.y;

            if (!active ||
                magnitudeSquared > selectedMagnitudeSquared)
            {
                value.x = candidate.x;
                value.y = candidate.y;
                selectedMagnitudeSquared = magnitudeSquared;
                active = true;
            }
        }
    };

    for (const VrInput::ActionDefinition& action :
         VrInput::ActionDefinitions())
    {
        if (action.valueType == VrInput::ValueType::Vector2)
        {
            readVectorAction(action.action);
        }
    }

    VR_ApplyControllerInputState(
        inputHeld,
        inputVectors,
        inputVectorActive,
        missionMovementLockHeld,
        "OpenVR/SteamVR");

    const auto isHeld = [&inputHeld](
        const VrInput::Action action)
    {
        return inputHeld[
            static_cast<std::size_t>(action)];
    };

    const std::uint32_t weaponHandIndex =
        VrInteractions::WeaponControllerIndex(
            configurable.dominantHand);
    const std::uint32_t offHandIndex =
        VrInteractions::OffHandControllerIndex(
            configurable.dominantHand);
    const bool rawOffhandGripHeld =
        isHeld(VrInput::Action::SupportGrip);
    bool supportGripHeld = false;
    bool objectGripHeld = false;
    VR_ResolveOffhandGripModes(
        rawOffhandGripHeld,
        &supportGripHeld,
        &objectGripHeld);

    for (std::size_t handIndex = 0u;
         handIndex < g_vrOpenVrHands.size();
         ++handIndex)
    {
        const VrInput::OpenVrHandState& hand =
            g_vrOpenVrHands[handIndex];

        const bool deviceIndexValid =
            hand.deviceIndex !=
                vr::k_unTrackedDeviceIndexInvalid &&
            hand.deviceIndex <
                g_vrOpenVrRenderPoses.size();

        const vr::TrackedDevicePose_t* trackedPose =
            deviceIndexValid
                ? &g_vrOpenVrRenderPoses[hand.deviceIndex]
                : nullptr;

        const bool poseValid =
            trackedPose != nullptr &&
            trackedPose->bDeviceIsConnected &&
            trackedPose->bPoseIsValid;

        XrPosef controllerPose = {
            {0.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 0.0f},
        };

        if (poseValid)
        {
            controllerPose = VR_OpenVrMatrixToPose(
                trackedPose->mDeviceToAbsoluteTracking);
        }

        XrPosef controllerGripPose =
            controllerPose;
        XrPosef controllerPalmPose =
            controllerPose;
        XrPosef controllerAimPose =
            controllerPose;
        bool semanticGripPose = false;
        bool semanticPalmPose = false;
        bool semanticAimPose = false;
        const bool openVrIndexOffHand =
            handIndex == offHandIndex &&
            VrInput::IsOpenVrIndexController(hand);

        if (poseValid)
        {
            VR_ResolveOpenVrControllerPoseComponents(
                handIndex,
                hand);

            VrOpenVrControllerPoseComponents& components =
                g_vrOpenVrControllerPoseComponents[handIndex];

            semanticGripPose =
                VR_LocateOpenVrControllerComponentPose(
                    hand,
                    components,
                    components.gripComponent,
                    controllerPose,
                    &controllerGripPose);

            // KISAK_SP_VR_INDEX_LEFT_HANDMODEL_BASIS_V102
            // Valve's Index render model exposes three distinct semantic
            // transforms: handgrip is the neutral held-controller grip,
            // tip is the pointing pose, and openxr_handmodel is the pose
            // explicitly intended to place visual hands. V98 sent handgrip
            // through the generic grip fallback and the glove faced the
            // player. Use openxr_handmodel only for the standalone Index
            // off-hand visual; support grip, reload, gestures, weapon aim,
            // the right hand, and every non-Index path retain their existing
            // grip/aim poses.
            if (openVrIndexOffHand)
            {
                semanticPalmPose =
                    VR_LocateOpenVrControllerComponentPose(
                        hand,
                        components,
                        components.palmComponent,
                        controllerPose,
                        &controllerPalmPose);

                if (!components.indexHandModelBasisLogged)
                {
                    if (semanticPalmPose)
                    {
                        Com_Printf(
                            0,
                            "[VR][OPENVR][POSE] V102 Index off-hand "
                            "uses SteamVR openxr_handmodel as the "
                            "visual palm basis; handgrip remains "
                            "unchanged for support and reload.\n");
                    }
                    else
                    {
                        Com_PrintWarning(
                            0,
                            "[VR][OPENVR][POSE] V102 Index off-hand "
                            "could not locate SteamVR openxr_handmodel; "
                            "the visual glove is using the unchanged "
                            "grip fallback.\n");
                    }

                    components.indexHandModelBasisLogged = true;
                }
            }

            semanticAimPose =
                VR_LocateOpenVrControllerComponentPose(
                    hand,
                    components,
                    components.aimComponent,
                    controllerPose,
                    &controllerAimPose);

            if ((!semanticGripPose &&
                 components.gripComponent != nullptr) ||
                (openVrIndexOffHand &&
                 !semanticPalmPose &&
                 components.palmComponent != nullptr) ||
                (!semanticAimPose &&
                 components.aimComponent != nullptr))
            {
                if (!components.stateFailureLogged)
                {
                    Com_PrintWarning(
                        0,
                        "[VR][OPENVR][POSE] SteamVR rejected a "
                        "semantic component-state query for the %s "
                        "controller; the affected pose is using the "
                        "raw-device fallback.\n",
                        VR_ControllerHandName(
                            static_cast<std::uint32_t>(handIndex)));

                    components.stateFailureLogged = true;
                }
            }
        }

        VrControllerRenderPose& renderPose =
            g_vrControllerRenderPoses[handIndex];
        renderPose.gripValid = poseValid;
        renderPose.palmValid =
            poseValid &&
            (!openVrIndexOffHand || semanticPalmPose);
        renderPose.aimValid = poseValid;

        if (poseValid)
        {
            renderPose.gripPose =
                controllerGripPose;
            if (renderPose.palmValid)
            {
                renderPose.palmPose =
                    openVrIndexOffHand
                        ? controllerPalmPose
                        : controllerGripPose;
            }
            renderPose.aimPose =
                controllerAimPose;
        }

        XrVector3f linearVelocity = {};
        if (poseValid)
        {
            linearVelocity.x = trackedPose->vVelocity.v[0];
            linearVelocity.y = trackedPose->vVelocity.v[1];
            linearVelocity.z = trackedPose->vVelocity.v[2];
        }

        if (handIndex == weaponHandIndex)
        {
            if (poseValid)
            {
                VR_PublishRightControllerWeaponPose(
                    controllerGripPose,
                    controllerAimPose,
                    linearVelocity,
                    poseValid);
            }
            else
            {
                VR_InvalidateRightControllerWeaponPose();
            }
        }
        else if (handIndex == offHandIndex)
        {
            VR_PublishLeftControllerForegripPose(
                controllerGripPose,
                poseValid,
                objectGripHeld,
                supportGripHeld,
                linearVelocity,
                poseValid);

            VR_PublishLeftControllerPalmPose(
                openVrIndexOffHand
                    ? controllerPalmPose
                    : controllerGripPose,
                poseValid &&
                    (!openVrIndexOffHand || semanticPalmPose));
        }

        if (poseValid &&
            !g_vrLoggedFirstGripPose[handIndex])
        {
            Com_Printf(
                0,
                "[VR][OPENVR] Located first valid %s controller "
                "poses; grip %s and aim %s.\n",
                VR_ControllerHandName(
                    static_cast<std::uint32_t>(handIndex)),
                semanticGripPose
                    ? "uses a semantic component"
                    : "uses the raw-device fallback",
                semanticAimPose
                    ? "uses a semantic component"
                    : "uses the raw-device fallback");
            g_vrLoggedFirstGripPose[handIndex] = true;
            g_vrLoggedFirstPalmPose[handIndex] = true;
            g_vrLoggedFirstAimPose[handIndex] = true;
        }
    }

    // OpenXR updates these after locating both controller action spaces. The
    // legacy SteamVR adapter publishes equivalent device poses above, so run
    // the same two-hand weapon blend and physical-ADS detector here as well.
    static bool loggedOpenVrTwoHandRepair = false;
    if (!loggedOpenVrTwoHandRepair)
    {
        Com_Printf(
            0,
            "[VR][OPENVR] V66 backend-shared two-hand weapon "
            "target update is active.\n");
        loggedOpenVrTwoHandRepair = true;
    }
    VR_UpdateTwoHandWeaponTargetFromPublishedPoses();
    VR_UpdatePoseFocusAimFromControllers();

    return anyController;
}

void VR_FrameOpenVr()
{
    if (!g_vrInitialized ||
        !g_vrOpenVrInitialized ||
        g_vrRuntimeBackend !=
            VrRuntimeBackend::OpenVr ||
        g_vrOpenVrSystem == nullptr ||
        g_vrOpenVrCompositor == nullptr)
    {
        return;
    }

    KisakCrash_SetStage(
        "VR_Frame: OpenVR update packed UI placement");
    VR_UpdatePackedUiScreenPlacement();

    KisakCrash_SetStage(
        "VR_Frame: OpenVR poll events");
    VR_PollOpenVrEvents();

    if (g_vrExitRequested)
    {
        return;
    }

    KisakCrash_SetStage(
        "VR_Frame: OpenVR WaitGetPoses");

    if (!VR_UpdateOpenVrHeadPose())
    {
        return;
    }

    KisakCrash_SetStage(
        "VR_Frame: OpenVR controller input");
    VR_UpdateOpenVrControllerActions();

    // KISAK_SP_VR_OPENVR_MENU_NAVIGATION_V76
    // Keep legacy SteamVR controller state on the same menu-dispatch path as
    // OpenXR. Without this call OpenVR gameplay actions update, but frontend
    // cursor movement, confirm, and back are never sent to COD4's UI.
    KisakCrash_SetStage(
        "VR_Frame: OpenVR menu controller navigation");
    VR_UpdateMenuControllerNavigation();

    KisakCrash_SetStage(
        "VR_Frame: OpenVR acquire captured frame");
    VR_UpdateCapturedStereoTexture();

    std::array<XrView, kVrStereoEyeCount>
        submissionViews = {};

    bool submissionViewsValid = false;

    if (g_vrCapturedStereoViewsValid)
    {
        submissionViews =
            g_vrCapturedStereoViews;
        submissionViewsValid = true;
    }
    else
    {
        for (std::uint32_t eyeIndex = 0u;
             eyeIndex < kVrStereoEyeCount;
             ++eyeIndex)
        {
            submissionViews[eyeIndex] =
                g_vrViews[eyeIndex];
        }

        submissionViewsValid = true;
    }

    std::array<XrView, kVrStereoEyeCount>
        currentOpenVrViews = {};

    for (std::uint32_t eyeIndex = 0u;
         eyeIndex < kVrStereoEyeCount;
         ++eyeIndex)
    {
        currentOpenVrViews[eyeIndex] =
            g_vrViews[eyeIndex];
    }

    const XrPosef currentOpenVrHeadPose =
        VR_OpenVrHeadPoseFromViews(
            currentOpenVrViews);

    VR_PublishHeadOrientation(
        currentOpenVrHeadPose.orientation);

    VR_ProcessCalibrationRequest(0);
    VR_ProcessHudEditorRequest();
    VR_ProcessWeaponCalibrationRequest();

    {
        std::lock_guard<std::mutex> lock(
            g_vrPublishedRenderViewsMutex);

        for (std::uint32_t eyeIndex = 0u;
             eyeIndex < kVrStereoEyeCount;
             ++eyeIndex)
        {
            g_vrPublishedRenderViews[eyeIndex] =
                g_vrViews[eyeIndex];
        }

        g_vrPublishedRenderViewsValid = true;
        g_vrPublishedRenderPoseNanoseconds =
            VR_OpenXrClockNanoseconds();
    }

    const bool menuComfortMode =
        Key_IsCatcherActive(
            0,
            0x10);

    const bool centeredModalComfortMode =
        menuComfortMode &&
        VR_IsCenteredMonoscopicMenuActive();

    const bool activePauseComfortMode =
        menuComfortMode &&
        !centeredModalComfortMode &&
        clientUIActives[0].connectionState ==
            CA_ACTIVE;

    KisakCrash_SetStage(
        "VR_Frame: OpenVR render eye targets");

    for (std::uint32_t eyeIndex = 0u;
         eyeIndex < kVrStereoEyeCount;
         ++eyeIndex)
    {
        if (!VR_RenderOpenVrEye(
                eyeIndex,
                submissionViews,
                submissionViewsValid,
                menuComfortMode,
                centeredModalComfortMode,
                activePauseComfortMode))
        {
            return;
        }
    }

    const vr::HmdMatrix34_t submissionHeadPose =
        VR_OpenVrHeadMatrixFromViews(
            submissionViews);

    vr::VRTextureBounds_t bounds = {};
    bounds.uMin = 0.0f;
    bounds.vMin = 0.0f;
    bounds.uMax = 1.0f;
    bounds.vMax = 1.0f;

    KisakCrash_SetStage(
        "VR_Frame: OpenVR submit stereo textures");

    bool submittedBothEyes = true;

    for (std::uint32_t eyeIndex = 0u;
         eyeIndex < kVrStereoEyeCount;
         ++eyeIndex)
    {
        VrOpenVrEyeTarget& target =
            g_vrOpenVrEyeTargets[eyeIndex];

        vr::VRTextureWithPose_t texture = {};
        texture.handle = target.texture.Get();
        texture.eType =
            vr::TextureType_DirectX;
        // KISAK_SP_VR_OPENVR_GAMMA_SUBMISSION_V78
        // Oculus/SteamVR rejects this legacy D3D11 BGRA-to-RGBA path when it
        // is tagged ColorSpace_Linear (EVRCompositorError 105). OpenVR now
        // preserves the D3D9 display-referred values during sampling, so the
        // previously compatible 8-bit Auto/gamma compositor path is correct.
        texture.eColorSpace =
            vr::ColorSpace_Auto;
        texture.mDeviceToAbsoluteTracking =
            submissionHeadPose;

        const vr::EVRCompositorError submitResult =
            g_vrOpenVrCompositor->Submit(
                eyeIndex == 0u
                    ? vr::Eye_Left
                    : vr::Eye_Right,
                &texture,
                &bounds,
                vr::Submit_TextureWithPose);

        if (submitResult !=
            vr::VRCompositorError_None)
        {
            Com_PrintWarning(
                0,
                "[VR][OPENVR] Submit failed for eye %u with "
                "EVRCompositorError %d.\n",
                eyeIndex,
                static_cast<int>(submitResult));

            submittedBothEyes = false;
        }
    }

    g_vrOpenVrCompositor->PostPresentHandoff();

    KisakCrash_SetStage(
        "VR_Frame: OpenVR retire captured frame");
    VR_PollRetiredSharedFrames();

    if (submittedBothEyes &&
        !g_vrOpenVrLoggedFirstSubmit)
    {
        Com_Printf(
            0,
            "[VR][OPENVR] V78 submitted the first stereo D3D11 "
            "frame to SteamVR as gamma-encoded BGRA8 with "
            "ColorSpace_Auto and explicit captured-pose metadata.\n");

        g_vrOpenVrLoggedFirstSubmit = true;
    }

    KisakCrash_SetVrState(
        true,
        true,
        static_cast<int>(
            g_vrSessionState),
        static_cast<unsigned int>(
            g_vrUploadedStereoSerial &
            0xFFFFFFFFu),
        g_vrCapturedStereoWidth,
        g_vrCapturedStereoHeight);
}

void VR_Frame()
{
    KisakCrash_SetVrState(
        g_vrInitialized,
        g_vrSessionRunning,
        static_cast<int>(g_vrSessionState),
        static_cast<unsigned int>(
            g_vrUploadedStereoSerial & 0xFFFFFFFFu),
        g_vrCapturedStereoWidth,
        g_vrCapturedStereoHeight);

    if (g_vrRuntimeBackend ==
        VrRuntimeBackend::OpenVr)
    {
        VR_FrameOpenVr();
        return;
    }

    if (!g_vrInitialized ||
        g_vrInstance == XR_NULL_HANDLE ||
        g_vrSession == XR_NULL_HANDLE)
    {
        return;
    }

    // CL_InitRenderer rebuilds ScreenPlacement during a vid_restart.
    // Reassert the stereo-only UI width before the next game frame.
    KisakCrash_SetStage("VR_Frame: update packed UI placement");
    VR_UpdatePackedUiScreenPlacement();

    KisakCrash_SetStage("VR_Frame: xrPollEvent");
    VR_PollEvents();

    if (!g_vrSessionRunning ||
        g_vrExitRequested)
    {
        return;
    }

    // KISAK_SP_VR_PERFORMANCE_DIAGNOSTICS_V1
    using VrPerfClock =
        std::chrono::steady_clock;

    const auto vrPerfFrameStart =
        VrPerfClock::now();

    static bool vrPerfPreviousFrameValid = false;
    static VrPerfClock::time_point
        vrPerfPreviousFrameStart = {};

    const bool vrPerfHasInterval =
        vrPerfPreviousFrameValid;

    const bool vrPerfGameplayActive =
        clientUIActives[0].connectionState ==
            CA_ACTIVE &&
        !Key_IsCatcherActive(0, 0x10);

    static bool vrPerfPreviousGameplayActive = false;

    const bool vrPerfContinuousGameplay =
        vrPerfGameplayActive &&
        vrPerfPreviousGameplayActive;

    vrPerfPreviousGameplayActive =
        vrPerfGameplayActive;

    const double vrPerfIntervalMilliseconds =
        vrPerfHasInterval
            ? std::chrono::duration<double, std::milli>(
                  vrPerfFrameStart -
                  vrPerfPreviousFrameStart).count()
            : 0.0;

    vrPerfPreviousFrameStart =
        vrPerfFrameStart;
    vrPerfPreviousFrameValid = true;

    const auto vrPerfWaitStart =
        VrPerfClock::now();

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};

    KisakCrash_SetStage("VR_Frame: xrWaitFrame");
    XrResult result =
        xrWaitFrame(
            g_vrSession,
            &waitInfo,
            &frameState);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrWaitFrame", result);
        return;
    }

    const auto vrPerfWaitEnd =
        VrPerfClock::now();

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};

    KisakCrash_SetStage("VR_Frame: xrBeginFrame");
    result =
        xrBeginFrame(g_vrSession, &beginInfo);

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrBeginFrame", result);
        return;
    }

    XrCompositionLayerProjection projectionLayer{
        XR_TYPE_COMPOSITION_LAYER_PROJECTION
    };

    std::vector<XrCompositionLayerProjectionView>
        projectionViews;

    bool submittedProjectionLayer = false;

    const std::uint64_t vrPerfCaptureSerialBefore =
        g_vrUploadedStereoSerial;

    const auto vrPerfRenderStart =
        VrPerfClock::now();

    KisakCrash_SetStage("VR_Frame: render and composite eyes");
    if (frameState.shouldRender)
    {
        submittedProjectionLayer =
            VR_RenderSolidColorFrame(
                frameState,
                projectionLayer,
                projectionViews);
    }

    const auto vrPerfRenderEnd =
        VrPerfClock::now();

    const bool vrPerfFreshCapture =
        frameState.shouldRender &&
        g_vrUploadedStereoSerial !=
            vrPerfCaptureSerialBefore;

    const std::uint64_t vrPerfNowNanoseconds =
        VR_OpenXrClockNanoseconds();

    const std::uint64_t vrPerfCaptureReferenceNanoseconds =
        g_vrCapturedStereoMetadata
                    .producerReadyNanoseconds != 0u
            ? g_vrCapturedStereoMetadata
                  .producerReadyNanoseconds
            : g_vrCapturedStereoMetadata
                  .captureSubmittedNanoseconds;

    const double vrPerfCaptureAgeMilliseconds =
        vrPerfCaptureReferenceNanoseconds != 0u &&
                vrPerfNowNanoseconds >=
                    vrPerfCaptureReferenceNanoseconds
            ? static_cast<double>(
                  vrPerfNowNanoseconds -
                  vrPerfCaptureReferenceNanoseconds) /
                  1000000.0
            : 0.0;

    const double vrPerfPoseAgeMilliseconds =
        g_vrCapturedRenderPoseNanoseconds != 0u &&
                vrPerfNowNanoseconds >=
                    g_vrCapturedRenderPoseNanoseconds
            ? static_cast<double>(
                  vrPerfNowNanoseconds -
                  g_vrCapturedRenderPoseNanoseconds) /
                  1000000.0
            : 0.0;

    const XrCompositionLayerBaseHeader* layers[1] = {};

    uint32_t layerCount = 0;

    if (submittedProjectionLayer)
    {
        layers[0] =
            reinterpret_cast<
                const XrCompositionLayerBaseHeader*>(
                    &projectionLayer);

        layerCount = 1;
    }

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};

    endInfo.displayTime =
        frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = g_vrBlendMode;
    endInfo.layerCount = layerCount;
    endInfo.layers =
        layerCount > 0
            ? layers
            : nullptr;

    const auto vrPerfEndStart =
        VrPerfClock::now();

    KisakCrash_SetStage("VR_Frame: xrEndFrame");
    result =
        xrEndFrame(g_vrSession, &endInfo);

    const auto vrPerfEndEnd =
        VrPerfClock::now();

    if (XR_FAILED(result))
    {
        VR_LogXrFailure("xrEndFrame", result);
    }

    // KISAK_SP_VR_SEQUENTIAL_CAPTURE_QUEUE_V33
    // The current frame's D3D11 work has now been submitted. Poll retirement
    // fences again so the D3D9 producer sees a free slot before the next
    // Com_Frame handoff. This lets a single queued capture survive a temporary
    // producer/consumer phase crossing instead of being overwritten.
    KisakCrash_SetStage("VR_Frame: retire shared capture frames");
    VR_PollRetiredSharedFrames();

    KisakCrash_SetStage("VR_Frame: performance diagnostics");

    const double vrPerfWaitMilliseconds =
        std::chrono::duration<double, std::milli>(
            vrPerfWaitEnd -
            vrPerfWaitStart).count();

    const double vrPerfRenderMilliseconds =
        std::chrono::duration<double, std::milli>(
            vrPerfRenderEnd -
            vrPerfRenderStart).count();

    const double vrPerfEndMilliseconds =
        std::chrono::duration<double, std::milli>(
            vrPerfEndEnd -
            vrPerfEndStart).count();

    const double vrPerfCallMilliseconds =
        std::chrono::duration<double, std::milli>(
            vrPerfEndEnd -
            vrPerfFrameStart).count();

    static unsigned int vrPerfSampleCount = 0u;
    static unsigned int vrPerfIntervalSampleCount = 0u;
    static unsigned int vrPerfFreshCaptureCount = 0u;
    static unsigned int vrPerfReusedCaptureCount = 0u;
    static unsigned int vrPerfNoRenderCount = 0u;

    static unsigned int vrPerfGameplaySampleCount = 0u;
    static unsigned int vrPerfGameplayIntervalCount = 0u;
    static unsigned int vrPerfGameplayFreshCount = 0u;
    static unsigned int vrPerfGameplayReusedCount = 0u;
    static unsigned int vrPerfGameplayReuseStreak = 0u;
    static unsigned int vrPerfGameplayReuseStreakMaximum = 0u;
    static unsigned int vrPerfGameplaySpike20Count = 0u;
    static unsigned int vrPerfGameplaySpike30Count = 0u;
    static unsigned int vrPerfGameplayPoseMatchedCount = 0u;
    static unsigned int vrPerfGameplayPoseFallbackCount = 0u;
    static unsigned int vrPerfGameplayCaptureAgeCount = 0u;
    static unsigned int vrPerfGameplayProducerIntervalCount = 0u;

    static std::uint64_t vrPerfPreviousGameplayCaptureSerial = 0u;
    static std::uint64_t vrPerfPreviousGameplayProducerSequence = 0u;
    static std::uint64_t vrPerfPreviousGameplayRenderFrameId = 0u;
    static std::uint64_t vrPerfPreviousGameplayCaptureSubmitted = 0u;
    static std::uint64_t vrPerfGameplayBridgeMissedCount = 0u;
    static std::uint64_t vrPerfGameplayConsumerSkippedCount = 0u;
    static std::uint64_t vrPerfGameplayRenderGapCount = 0u;

    static double vrPerfIntervalTotal = 0.0;
    static double vrPerfWaitTotal = 0.0;
    static double vrPerfRenderTotal = 0.0;
    static double vrPerfEndTotal = 0.0;
    static double vrPerfCallTotal = 0.0;

    static double vrPerfGameplayIntervalTotal = 0.0;
    static double vrPerfGameplayCaptureAgeTotal = 0.0;
    static double vrPerfGameplayPoseAgeTotal = 0.0;
    static double vrPerfGameplayProducerIntervalTotal = 0.0;

    static double vrPerfIntervalMaximum = 0.0;
    static double vrPerfWaitMaximum = 0.0;
    static double vrPerfRenderMaximum = 0.0;
    static double vrPerfEndMaximum = 0.0;
    static double vrPerfCallMaximum = 0.0;

    static double vrPerfGameplayIntervalMaximum = 0.0;
    static double vrPerfGameplayCaptureAgeMaximum = 0.0;
    static double vrPerfGameplayPoseAgeMaximum = 0.0;
    static double vrPerfGameplayProducerIntervalMaximum = 0.0;

    ++vrPerfSampleCount;

    if (vrPerfHasInterval)
    {
        ++vrPerfIntervalSampleCount;
        vrPerfIntervalTotal +=
            vrPerfIntervalMilliseconds;

        if (vrPerfIntervalMilliseconds >
            vrPerfIntervalMaximum)
        {
            vrPerfIntervalMaximum =
                vrPerfIntervalMilliseconds;
        }
    }

    vrPerfWaitTotal +=
        vrPerfWaitMilliseconds;
    vrPerfRenderTotal +=
        vrPerfRenderMilliseconds;
    vrPerfEndTotal +=
        vrPerfEndMilliseconds;
    vrPerfCallTotal +=
        vrPerfCallMilliseconds;

    if (vrPerfWaitMilliseconds >
        vrPerfWaitMaximum)
    {
        vrPerfWaitMaximum =
            vrPerfWaitMilliseconds;
    }

    if (vrPerfRenderMilliseconds >
        vrPerfRenderMaximum)
    {
        vrPerfRenderMaximum =
            vrPerfRenderMilliseconds;
    }

    if (vrPerfEndMilliseconds >
        vrPerfEndMaximum)
    {
        vrPerfEndMaximum =
            vrPerfEndMilliseconds;
    }

    if (vrPerfCallMilliseconds >
        vrPerfCallMaximum)
    {
        vrPerfCallMaximum =
            vrPerfCallMilliseconds;
    }

    if (!frameState.shouldRender)
    {
        ++vrPerfNoRenderCount;
    }
    else if (vrPerfFreshCapture)
    {
        ++vrPerfFreshCaptureCount;
    }
    else
    {
        ++vrPerfReusedCaptureCount;
    }

    if (!vrPerfGameplayActive)
    {
        vrPerfGameplayReuseStreak = 0u;
        vrPerfPreviousGameplayCaptureSerial = 0u;
        vrPerfPreviousGameplayProducerSequence = 0u;
        vrPerfPreviousGameplayRenderFrameId = 0u;
        vrPerfPreviousGameplayCaptureSubmitted = 0u;
    }
    else if (frameState.shouldRender)
    {
        ++vrPerfGameplaySampleCount;

        if (vrPerfContinuousGameplay &&
            vrPerfHasInterval)
        {
            ++vrPerfGameplayIntervalCount;
            vrPerfGameplayIntervalTotal +=
                vrPerfIntervalMilliseconds;

            if (vrPerfIntervalMilliseconds >
                vrPerfGameplayIntervalMaximum)
            {
                vrPerfGameplayIntervalMaximum =
                    vrPerfIntervalMilliseconds;
            }

            if (vrPerfIntervalMilliseconds > 20.0)
            {
                ++vrPerfGameplaySpike20Count;
            }

            if (vrPerfIntervalMilliseconds > 30.0)
            {
                ++vrPerfGameplaySpike30Count;
            }
        }

        if (g_vrUploadedStereoSerial != 0u)
        {
            ++vrPerfGameplayCaptureAgeCount;
            vrPerfGameplayCaptureAgeTotal +=
                vrPerfCaptureAgeMilliseconds;
            vrPerfGameplayPoseAgeTotal +=
                vrPerfPoseAgeMilliseconds;

            if (vrPerfCaptureAgeMilliseconds >
                vrPerfGameplayCaptureAgeMaximum)
            {
                vrPerfGameplayCaptureAgeMaximum =
                    vrPerfCaptureAgeMilliseconds;
            }

            if (vrPerfPoseAgeMilliseconds >
                vrPerfGameplayPoseAgeMaximum)
            {
                vrPerfGameplayPoseAgeMaximum =
                    vrPerfPoseAgeMilliseconds;
            }
        }

        if (vrPerfFreshCapture)
        {
            ++vrPerfGameplayFreshCount;
            vrPerfGameplayReuseStreak = 0u;

            const std::uint64_t currentSerial =
                g_vrUploadedStereoSerial;

            const std::uint64_t currentProducerSequence =
                g_vrCapturedStereoMetadata
                    .producerSequence;

            const std::uint64_t currentRenderFrameId =
                g_vrCapturedStereoMetadata
                    .renderFrameId;

            const std::uint64_t currentCaptureSubmitted =
                g_vrCapturedStereoMetadata
                    .captureSubmittedNanoseconds;

            const bool sequenceContinues =
                vrPerfPreviousGameplayCaptureSerial != 0u &&
                currentSerial >
                    vrPerfPreviousGameplayCaptureSerial &&
                currentProducerSequence >=
                    vrPerfPreviousGameplayProducerSequence &&
                currentRenderFrameId >=
                    vrPerfPreviousGameplayRenderFrameId;

            if (sequenceContinues)
            {
                const std::uint64_t serialDelta =
                    currentSerial -
                    vrPerfPreviousGameplayCaptureSerial;

                const std::uint64_t producerDelta =
                    currentProducerSequence -
                    vrPerfPreviousGameplayProducerSequence;

                const std::uint64_t renderFrameDelta =
                    currentRenderFrameId -
                    vrPerfPreviousGameplayRenderFrameId;

                if (producerDelta > serialDelta)
                {
                    vrPerfGameplayBridgeMissedCount +=
                        producerDelta - serialDelta;
                }

                if (serialDelta > 1u)
                {
                    vrPerfGameplayConsumerSkippedCount +=
                        serialDelta - 1u;
                }

                if (renderFrameDelta > producerDelta)
                {
                    vrPerfGameplayRenderGapCount +=
                        renderFrameDelta - producerDelta;
                }

                if (currentCaptureSubmitted >
                    vrPerfPreviousGameplayCaptureSubmitted &&
                    vrPerfPreviousGameplayCaptureSubmitted != 0u)
                {
                    const std::uint64_t producerIntervalDivisor =
                        producerDelta > 0u
                            ? producerDelta
                            : 1u;

                    const double producerIntervalMilliseconds =
                        static_cast<double>(
                            currentCaptureSubmitted -
                            vrPerfPreviousGameplayCaptureSubmitted) /
                        (1000000.0 *
                         static_cast<double>(
                             producerIntervalDivisor));

                    ++vrPerfGameplayProducerIntervalCount;
                    vrPerfGameplayProducerIntervalTotal +=
                        producerIntervalMilliseconds;

                    if (producerIntervalMilliseconds >
                        vrPerfGameplayProducerIntervalMaximum)
                    {
                        vrPerfGameplayProducerIntervalMaximum =
                            producerIntervalMilliseconds;
                    }
                }
            }

            vrPerfPreviousGameplayCaptureSerial =
                currentSerial;
            vrPerfPreviousGameplayProducerSequence =
                currentProducerSequence;
            vrPerfPreviousGameplayRenderFrameId =
                currentRenderFrameId;
            vrPerfPreviousGameplayCaptureSubmitted =
                currentCaptureSubmitted;

            if (g_vrCapturedStereoPoseMatched)
            {
                ++vrPerfGameplayPoseMatchedCount;
            }
            else
            {
                ++vrPerfGameplayPoseFallbackCount;
            }
        }
        else
        {
            ++vrPerfGameplayReusedCount;
            ++vrPerfGameplayReuseStreak;

            if (vrPerfGameplayReuseStreak >
                vrPerfGameplayReuseStreakMaximum)
            {
                vrPerfGameplayReuseStreakMaximum =
                    vrPerfGameplayReuseStreak;
            }
        }
    }

    if (vrPerfSampleCount >= 120u)
    {
        const double vrPerfSampleScale =
            1.0 /
            static_cast<double>(
                vrPerfSampleCount);

        const double vrPerfAverageInterval =
            vrPerfIntervalSampleCount > 0u
                ? vrPerfIntervalTotal /
                      static_cast<double>(
                          vrPerfIntervalSampleCount)
                : 0.0;

        const double vrPerfFramesPerSecond =
            vrPerfAverageInterval > 0.0
                ? 1000.0 /
                      vrPerfAverageInterval
                : 0.0;

        const unsigned int vrPerfRenderedSampleCount =
            vrPerfFreshCaptureCount +
            vrPerfReusedCaptureCount;

        const double vrPerfFreshCaptureFramesPerSecond =
            vrPerfRenderedSampleCount > 0u
                ? vrPerfFramesPerSecond *
                      static_cast<double>(
                          vrPerfFreshCaptureCount) /
                      static_cast<double>(
                          vrPerfRenderedSampleCount)
                : 0.0;

        const double vrPerfGameplayAverageInterval =
            vrPerfGameplayIntervalCount > 0u
                ? vrPerfGameplayIntervalTotal /
                      static_cast<double>(
                          vrPerfGameplayIntervalCount)
                : 0.0;

        const double vrPerfGameplayAverageCaptureAge =
            vrPerfGameplayCaptureAgeCount > 0u
                ? vrPerfGameplayCaptureAgeTotal /
                      static_cast<double>(
                          vrPerfGameplayCaptureAgeCount)
                : 0.0;

        const double vrPerfGameplayAveragePoseAge =
            vrPerfGameplayCaptureAgeCount > 0u
                ? vrPerfGameplayPoseAgeTotal /
                      static_cast<double>(
                          vrPerfGameplayCaptureAgeCount)
                : 0.0;

        const double vrPerfGameplayAverageProducerInterval =
            vrPerfGameplayProducerIntervalCount > 0u
                ? vrPerfGameplayProducerIntervalTotal /
                      static_cast<double>(
                          vrPerfGameplayProducerIntervalCount)
                : 0.0;

        Com_Printf(
            0,
            "[VR][PERF] OpenXR loop: %.1f FPS; "
            "fresh capture %.1f FPS; "
            "interval %.2f avg / %.2f max ms; "
            "wait %.2f / %.2f; "
            "render %.2f / %.2f; "
            "end %.2f / %.2f; "
            "call %.2f / %.2f; "
            "capture fresh %u, reused %u, "
            "no-render %u of %u.\n",
            vrPerfFramesPerSecond,
            vrPerfFreshCaptureFramesPerSecond,
            vrPerfAverageInterval,
            vrPerfIntervalMaximum,
            vrPerfWaitTotal *
                vrPerfSampleScale,
            vrPerfWaitMaximum,
            vrPerfRenderTotal *
                vrPerfSampleScale,
            vrPerfRenderMaximum,
            vrPerfEndTotal *
                vrPerfSampleScale,
            vrPerfEndMaximum,
            vrPerfCallTotal *
                vrPerfSampleScale,
            vrPerfCallMaximum,
            vrPerfFreshCaptureCount,
            vrPerfReusedCaptureCount,
            vrPerfNoRenderCount,
            vrPerfSampleCount);

        Com_Printf(
            0,
            "[VR][PERF] Gameplay: %u samples; interval %.2f avg / "
            "%.2f max ms; spikes >20 %u, >30 %u; capture fresh %u, "
            "reused %u, reuse streak max %u; producer %.2f avg / "
            "%.2f max ms; bridge missed %llu, consumer skipped %llu, "
            "render gaps %llu; capture age %.2f avg / %.2f max ms; "
            "pose age %.2f avg / %.2f max ms; exact pose %u, "
            "fallback %u.\n",
            vrPerfGameplaySampleCount,
            vrPerfGameplayAverageInterval,
            vrPerfGameplayIntervalMaximum,
            vrPerfGameplaySpike20Count,
            vrPerfGameplaySpike30Count,
            vrPerfGameplayFreshCount,
            vrPerfGameplayReusedCount,
            vrPerfGameplayReuseStreakMaximum,
            vrPerfGameplayAverageProducerInterval,
            vrPerfGameplayProducerIntervalMaximum,
            static_cast<unsigned long long>(
                vrPerfGameplayBridgeMissedCount),
            static_cast<unsigned long long>(
                vrPerfGameplayConsumerSkippedCount),
            static_cast<unsigned long long>(
                vrPerfGameplayRenderGapCount),
            vrPerfGameplayAverageCaptureAge,
            vrPerfGameplayCaptureAgeMaximum,
            vrPerfGameplayAveragePoseAge,
            vrPerfGameplayPoseAgeMaximum,
            vrPerfGameplayPoseMatchedCount,
            vrPerfGameplayPoseFallbackCount);

        vrPerfSampleCount = 0u;
        vrPerfIntervalSampleCount = 0u;
        vrPerfFreshCaptureCount = 0u;
        vrPerfReusedCaptureCount = 0u;
        vrPerfNoRenderCount = 0u;

        vrPerfGameplaySampleCount = 0u;
        vrPerfGameplayIntervalCount = 0u;
        vrPerfGameplayFreshCount = 0u;
        vrPerfGameplayReusedCount = 0u;
        vrPerfGameplayReuseStreakMaximum = 0u;
        vrPerfGameplaySpike20Count = 0u;
        vrPerfGameplaySpike30Count = 0u;
        vrPerfGameplayPoseMatchedCount = 0u;
        vrPerfGameplayPoseFallbackCount = 0u;
        vrPerfGameplayCaptureAgeCount = 0u;
        vrPerfGameplayProducerIntervalCount = 0u;
        vrPerfGameplayBridgeMissedCount = 0u;
        vrPerfGameplayConsumerSkippedCount = 0u;
        vrPerfGameplayRenderGapCount = 0u;

        vrPerfIntervalTotal = 0.0;
        vrPerfWaitTotal = 0.0;
        vrPerfRenderTotal = 0.0;
        vrPerfEndTotal = 0.0;
        vrPerfCallTotal = 0.0;

        vrPerfGameplayIntervalTotal = 0.0;
        vrPerfGameplayCaptureAgeTotal = 0.0;
        vrPerfGameplayPoseAgeTotal = 0.0;
        vrPerfGameplayProducerIntervalTotal = 0.0;

        vrPerfIntervalMaximum = 0.0;
        vrPerfWaitMaximum = 0.0;
        vrPerfRenderMaximum = 0.0;
        vrPerfEndMaximum = 0.0;
        vrPerfCallMaximum = 0.0;

        vrPerfGameplayIntervalMaximum = 0.0;
        vrPerfGameplayCaptureAgeMaximum = 0.0;
        vrPerfGameplayPoseAgeMaximum = 0.0;
        vrPerfGameplayProducerIntervalMaximum = 0.0;
    }

    KisakCrash_SetVrState(
        g_vrInitialized,
        g_vrSessionRunning,
        static_cast<int>(g_vrSessionState),
        static_cast<unsigned int>(
            g_vrUploadedStereoSerial & 0xFFFFFFFFu),
        g_vrCapturedStereoWidth,
        g_vrCapturedStereoHeight);
}

void VR_Shutdown()
{
    KisakCrash_SetStage("shutdown: VR runtime resources");
    VR_ResetHeadOrientation();
    if (g_vrInstance == XR_NULL_HANDLE &&
        g_vrSession == XR_NULL_HANDLE &&
        !g_vrOpenVrInitialized)
    {
        VR_ResetState();
        return;
    }

    Com_Printf(
        0,
        "[VR] Shutting down %s D3D11 subsystem...\n",
        VR_RuntimeBackendName());

    VR_D3D9CaptureSetEnabled(false);

    g_vrSessionRunning = false;

    if (g_vrOpenVrInitialized)
    {
        if (g_vrOpenVrCompositor != nullptr)
        {
            g_vrOpenVrCompositor
                ->ClearLastSubmittedFrame();
        }

        vr::VR_Shutdown();
        g_vrOpenVrRenderModels = nullptr;
        g_vrOpenVrCompositor = nullptr;
        g_vrOpenVrSystem = nullptr;
        g_vrOpenVrInitialized = false;
        g_vrOpenVrRenderPoses = {};
        g_vrOpenVrMissionSelector = {};
        g_vrOpenVrControllerPoseComponents = {};
    }

    VR_DestroyControllerInput();

    for (VrEyeSwapchain& eyeSwapchain :
         g_vrEyeSwapchains)
    {
        eyeSwapchain.renderTargetViews.clear();
        eyeSwapchain.images.clear();

        if (eyeSwapchain.handle != XR_NULL_HANDLE)
        {
            xrDestroySwapchain(eyeSwapchain.handle);
            eyeSwapchain.handle = XR_NULL_HANDLE;
        }
    }

    g_vrEyeSwapchains.clear();
    g_vrViews.clear();
    g_vrViewConfigs.clear();

    if (g_vrCalibrationFloorSpace != XR_NULL_HANDLE)
    {
        xrDestroySpace(g_vrCalibrationFloorSpace);
        g_vrCalibrationFloorSpace = XR_NULL_HANDLE;
    }

    g_vrCalibrationFloorSpaceAvailable = false;

    if (g_vrAppSpace != XR_NULL_HANDLE)
    {
        xrDestroySpace(g_vrAppSpace);
        g_vrAppSpace = XR_NULL_HANDLE;
    }

    if (g_vrSession != XR_NULL_HANDLE)
    {
        xrDestroySession(g_vrSession);
        g_vrSession = XR_NULL_HANDLE;
    }

    if (g_vrD3dContext)
    {
        g_vrD3dContext->ClearState();
        g_vrD3dContext->Flush();
    }

    for (VrOpenVrEyeTarget& target :
         g_vrOpenVrEyeTargets)
    {
        target.renderTargetView.Reset();
        target.texture.Reset();
        target.width = 0;
        target.height = 0;
    }

    g_vrCapturedStereoView.Reset();
    g_vrCapturedStereoTexture.Reset();

    for (auto& decodedView :
         g_vrCapturedSharedViews)
    {
        decodedView.Reset();
    }

    for (auto& decodedTexture :
         g_vrCapturedSharedTextures)
    {
        decodedTexture.Reset();
    }

    g_vrLoggedCaptureColorTransfer = false;

    for (VrRetiredSharedFrame& retired :
         g_vrRetiredSharedFrames)
    {
        retired.active = false;
        retired.slotIndex = 0u;
        retired.serial = 0u;
        retired.completionQuery.Reset();
        retired.view.Reset();
        retired.texture.Reset();
    }

    g_vrBlitSampler.Reset();
    g_vrMenuBlitVertexBuffer.Reset();
    g_vrPauseMenuBlitVertexBuffer.Reset();
    g_vrCenteredModalBlitVertexBuffer.Reset();
    g_vrLoggedMenuComfortScreen = false;
    g_vrLoggedCanonicalMenuAspect = false;

    for (auto& vertexBuffer :
         g_vrBlitVertexBuffers)
    {
        vertexBuffer.Reset();
    }

    g_vrBlitInputLayout.Reset();
    g_vrFsrIntermediateView.Reset();
    g_vrFsrIntermediateTarget.Reset();
    g_vrFsrIntermediateTexture.Reset();
    g_vrFsrConstantBuffer.Reset();
    g_vrFsrRcasPixelShader.Reset();
    g_vrFsrEasuPixelShader.Reset();
    g_vrCompositorConstantBuffer.Reset();
    g_vrCompositorBrightness = 1.0f;
    g_vrFsrEnabled = true;
    g_vrFsrShadersAvailable = false;
    g_vrFsrSharpness = 0.60f;
    g_vrOutputScale = 1.0f;
    g_vrLoggedFirstFsrFrame = false;
    g_vrLoggedFsrFallback = false;
    g_vrScopeConstantBuffer.Reset();
    g_vrScopePixelShader.Reset();
    g_vrBlitPixelShader.Reset();
    g_vrBlitVertexShader.Reset();
    g_vrLoggedFirstPhysicalScopeDraw = false;
    g_vrLoggedDedicatedScopeLayout = false;
    g_vrLoggedDedicatedScopeLayoutMissing = false;
    g_vrLoggedDedicatedScopeSample = false;
    g_vrScopeCaptureSizePixels = 1024;

    g_vrCapturedStereoWidth = 0u;
    g_vrCapturedStereoHeight = 0u;
    g_vrUploadedStereoSerial = 0u;
    g_vrCapturedStereoMetadata = {};
    g_vrCapturedRenderPoseNanoseconds = 0u;
    g_vrCapturedStereoPoseMatched = false;
    g_vrLoggedFirstStereoUpload = false;
    g_vrCurrentSharedFrameActive = false;
    g_vrCurrentSharedSlot = 0u;
    g_vrCurrentSharedGeneration = 0u;
    g_vrCurrentSharedSerial = 0u;
    g_vrLoggedFirstSharedFrameOpen = false;
    g_vrLoggedSharedConsumerFailure = false;

    g_vrControllerProxyVertexBuffer.Reset();
    g_vrControllerProxyInputLayout.Reset();
    g_vrControllerProxyPixelShader.Reset();
    g_vrControllerProxyVertexShader.Reset();
    g_vrControllerProxyResourcesReady = false;
    g_vrLoggedFirstControllerProxyDraw = false;
    g_vrControllerRenderPoses = {};

    g_vrTestDepthStencilState.Reset();
    g_vrTestRasterizerState.Reset();
    g_vrTestConstantBuffer.Reset();
    g_vrGridVertexBuffer.Reset();
    g_vrTestIndexBuffer.Reset();
    g_vrTestVertexBuffer.Reset();
    g_vrTestInputLayout.Reset();
    g_vrTestPixelShader.Reset();
    g_vrTestVertexShader.Reset();

    g_vrTestIndexCount = 0;
    g_vrGridVertexCount = 0;
    g_vrLoggedFirstTestFrame = false;

    g_vrD3dContext.Reset();
    g_vrD3dDevice.Reset();

    if (g_vrInstance != XR_NULL_HANDLE)
    {
        xrDestroyInstance(g_vrInstance);
        g_vrInstance = XR_NULL_HANDLE;
    }

    VR_ResetState();
}

bool VR_IsInitialized()
{
    return g_vrInitialized;
}

int VR_GetPromptBindingLabels(
    const char* const command,
    char (*const bindingNames)[128])
{
    if (bindingNames == nullptr)
    {
        return 0;
    }

    bindingNames[0][0] = '\0';
    bindingNames[1][0] = '\0';

    if (!g_vrInitialized || command == nullptr || command[0] == '\0')
    {
        return 0;
    }

    const VrInput::ActionDefinition* const action =
        VrPrompts::FindPromptAction(command);
    if (action == nullptr)
    {
        return 0;
    }

    const std::size_t actionIndex =
        static_cast<std::size_t>(action->action);
    const VrConfiguratorSettings& configurable =
        VR_GetConfiguratorSettings();

    const std::array<std::string_view, 2> profiles = {{
        g_vrCompatibilityControllerProfiles[VR_CONTROLLER_LEFT].data(),
        g_vrCompatibilityControllerProfiles[VR_CONTROLLER_RIGHT].data(),
    }};

    const VrPrompts::Backend backend =
        g_vrRuntimeBackend == VrRuntimeBackend::OpenVr
            ? VrPrompts::Backend::OpenVr
            : VrPrompts::Backend::OpenXr;

    const VrPrompts::BindingLabels labels =
        VrPrompts::BuildBindingLabels(
            configurable.bindings[actionIndex],
            profiles,
            backend,
            command);

    for (std::size_t index = 0u; index < labels.count; ++index)
    {
        std::snprintf(
            bindingNames[index],
            128u,
            "%s",
            labels.values[index].c_str());
    }

    if (labels.count > 0u)
    {
        static std::array<std::string, VrInput::kActionCount>
            loggedLabels = {};
        std::string signature = labels.values[0];
        if (labels.count == 2u)
        {
            signature += " OR ";
            signature += labels.values[1];
        }

        if (loggedLabels[actionIndex] != signature)
        {
            Com_Printf(
                0,
                "[VR][PROMPTS] V71 %s uses configured %s label%s: "
                "%s%s%s.\n",
                command,
                action->label,
                labels.count == 1u ? "" : "s",
                labels.values[0].c_str(),
                labels.count == 2u ? " OR " : "",
                labels.count == 2u ? labels.values[1].c_str() : "");
            loggedLabels[actionIndex] = signature;
        }
    }

    return static_cast<int>(labels.count);
}

const char* VR_GetActiveBackendName()
{
    return VR_RuntimeBackendName();
}

const char* VR_GetLastStartupError()
{
    if (g_vrLastStartupError[0] == '\0')
    {
        return
            "VR initialization failed without a runtime error code. "
            "Check OpenXR-Startup.log and main\\console.log.";
    }

    return g_vrLastStartupError.data();
}

void VR_SetFixedScopedTurretState(
    const bool active)
{
    std::lock_guard<std::mutex> lock(
        g_vrScopeStateMutex);

    g_vrFixedScopedTurretActive =
        g_vrInitialized &&
        active;
}

// KISAK_SP_VR_FIXED_SCOPED_TURRET_RUNTIME_V4
void VR_SetFixedScopedTurretZoomFov(
    const float currentFovDegrees,
    const float maximumFovDegrees)
{
    if (!std::isfinite(currentFovDegrees) ||
        !std::isfinite(maximumFovDegrees) ||
        currentFovDegrees <= 0.01f ||
        maximumFovDegrees <= 0.01f)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(
        g_vrScopeStateMutex);

    g_vrFixedScopedTurretZoomFovDegrees =
        (std::min)(
            currentFovDegrees,
            maximumFovDegrees);

    g_vrFixedScopedTurretMaximumZoomFovDegrees =
        maximumFovDegrees;
}

void VR_SetPhysicalSniperScopeState(
    const bool active,
    const float adsFraction,
    const float adsFovDegrees)
{
    std::lock_guard<std::mutex> lock(
        g_vrScopeStateMutex);

    g_vrScopeActive =
        active &&
        adsFraction > 0.01f &&
        adsFovDegrees > 1.0f;

    g_vrScopeAdsFraction =
        (std::max)(
            0.0f,
            (std::min)(1.0f, adsFraction));

    g_vrScopeAdsFovDegrees =
        (std::max)(
            1.0f,
            (std::min)(120.0f, adsFovDegrees));
}

bool VR_IsPhysicalSniperScopeAimActive()
{
    std::lock_guard<std::mutex> lock(
        g_vrScopeStateMutex);

    return g_vrScopeActive;
}

bool VR_GetPhysicalSniperScopeCaptureLayout(
    const int backbufferWidth,
    const int backbufferHeight,
    int* mainStereoWidth,
    int* scopePanelX,
    int* scopePanelY,
    int* scopePanelSize)
{
    if (backbufferWidth < 2 ||
        backbufferHeight < 1 ||
        g_vrEyeSwapchains.size() <
            kVrStereoEyeCount)
    {
        return false;
    }

    const int leftEyeWidth =
        g_vrEyeSwapchains[0].width;

    const int rightEyeWidth =
        g_vrEyeSwapchains[1].width;

    if (leftEyeWidth <= 0 ||
        rightEyeWidth <= 0)
    {
        return false;
    }

    const int requestedScopeSize =
        (std::max)(
            512,
            (std::min)(
                g_vrScopeCaptureSizePixels,
                backbufferHeight));

    const int requiredMainStereoWidth =
        leftEyeWidth +
        rightEyeWidth;

    if (requestedScopeSize > backbufferHeight ||
        requiredMainStereoWidth >
            backbufferWidth - requestedScopeSize)
    {
        return false;
    }

    if (mainStereoWidth != nullptr)
    {
        *mainStereoWidth =
            requiredMainStereoWidth;
    }

    if (scopePanelX != nullptr)
    {
        *scopePanelX =
            requiredMainStereoWidth;
    }

    if (scopePanelY != nullptr)
    {
        *scopePanelY = 0;
    }

    if (scopePanelSize != nullptr)
    {
        *scopePanelSize =
            requestedScopeSize;
    }

    if (!g_vrLoggedDedicatedScopeLayout)
    {
        Com_Printf(
            0,
            "[VR] Packed dedicated scope layout active: "
            "%d px stereo region plus %d x %d scope source "
            "inside the %d x %d backbuffer.\n",
            requiredMainStereoWidth,
            requestedScopeSize,
            requestedScopeSize,
            backbufferWidth,
            backbufferHeight);

        g_vrLoggedDedicatedScopeLayout = true;
    }

    return true;
}

bool VR_GetPhysicalSniperScopeRenderView(
    float scopeOrigin[3],
    float scopeAxis[3][3],
    float* tanHalfFovX,
    float* tanHalfFovY)
{
    if (scopeOrigin == nullptr ||
        scopeAxis == nullptr ||
        tanHalfFovX == nullptr ||
        tanHalfFovY == nullptr)
    {
        return false;
    }

    bool scopeActive = false;
    float currentAdsFraction = 0.0f;
    float adsFovDegrees = 65.0f;

    // KISAK_SP_VR_FIXED_SCOPE_SHARP_VIEW_AND_TRACER_V5
    bool fixedScopedTurretActive = false;
    float fixedScopedTurretFovDegrees = 20.0f;

    {
        std::lock_guard<std::mutex> lock(
            g_vrScopeStateMutex);

        scopeActive =
            g_vrScopeActive;

        currentAdsFraction =
            g_vrScopeAdsFraction;

        adsFovDegrees =
            g_vrScopeAdsFovDegrees;

        fixedScopedTurretActive =
            g_vrFixedScopedTurretActive;

        fixedScopedTurretFovDegrees =
            g_vrFixedScopedTurretZoomFovDegrees;
    }

    if (fixedScopedTurretActive)
    {
        // The caller initialized scopeOrigin from the current refdef, which
        // is the correct fixed-rifle eye position. Replace only its axis with
        // the same fused HMD-center basis used by the authoritative shot.
        {
            std::lock_guard<std::mutex> lock(
                g_vrWeaponControllerPoseMutex);

            if (!g_vrMountedWeaponCameraAxisWorldValid)
            {
                return false;
            }

            std::memcpy(
                scopeAxis,
                g_vrMountedWeaponCameraAxisWorld,
                sizeof(g_vrMountedWeaponCameraAxisWorld));
        }

        adsFovDegrees =
            fixedScopedTurretFovDegrees;
    }
    else
    {
        if (!scopeActive ||
            currentAdsFraction <= 0.01f)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(
            g_vrWeaponControllerPoseMutex);

        if (!g_vrPhysicalSniperScopePoseWorldValid)
        {
            return false;
        }

        std::memcpy(
            scopeOrigin,
            g_vrPhysicalSniperScopeOriginWorld,
            sizeof(g_vrPhysicalSniperScopeOriginWorld));

        std::memcpy(
            scopeAxis,
            g_vrPhysicalSniperScopeAxisWorld,
            sizeof(g_vrPhysicalSniperScopeAxisWorld));
    }

    constexpr float kPi =
        3.14159265358979323846f;

    const float squareScopeTanHalfFov =
        std::tan(
            0.5f *
            adsFovDegrees *
            (kPi / 180.0f)) *
        0.75f;

    if (!std::isfinite(squareScopeTanHalfFov) ||
        squareScopeTanHalfFov <= 0.0001f)
    {
        return false;
    }

    *tanHalfFovX =
        squareScopeTanHalfFov;

    *tanHalfFovY =
        squareScopeTanHalfFov;

    return true;
}

void VR_PublishPhysicalSniperScopePoseWorld(
    const float scopeOrigin[3],
    const float scopeAxis[3][3],
    const float cameraOrigin[3],
    const float cameraAxis[3][3])
{
    if (scopeOrigin == nullptr ||
        scopeAxis == nullptr ||
        cameraOrigin == nullptr ||
        cameraAxis == nullptr)
    {
        return;
    }

    const float calibratedScopeOrigin[3] = {
        scopeOrigin[0] +
            scopeAxis[0][0] *
                g_vrScopeForwardCalibrationMeters *
                kVrGameUnitsPerMeter +
            scopeAxis[1][0] *
                g_vrScopeLeftCalibrationMeters *
                kVrGameUnitsPerMeter +
            scopeAxis[2][0] *
                g_vrScopeUpCalibrationMeters *
                kVrGameUnitsPerMeter,
        scopeOrigin[1] +
            scopeAxis[0][1] *
                g_vrScopeForwardCalibrationMeters *
                kVrGameUnitsPerMeter +
            scopeAxis[1][1] *
                g_vrScopeLeftCalibrationMeters *
                kVrGameUnitsPerMeter +
            scopeAxis[2][1] *
                g_vrScopeUpCalibrationMeters *
                kVrGameUnitsPerMeter,
        scopeOrigin[2] +
            scopeAxis[0][2] *
                g_vrScopeForwardCalibrationMeters *
                kVrGameUnitsPerMeter +
            scopeAxis[1][2] *
                g_vrScopeLeftCalibrationMeters *
                kVrGameUnitsPerMeter +
            scopeAxis[2][2] *
                g_vrScopeUpCalibrationMeters *
                kVrGameUnitsPerMeter,
    };

    const float forwardLength =
        std::sqrt(
            scopeAxis[0][0] * scopeAxis[0][0] +
            scopeAxis[0][1] * scopeAxis[0][1] +
            scopeAxis[0][2] * scopeAxis[0][2]);

    if (forwardLength <= 0.0001f)
    {
        return;
    }

    const float scopeForwardWorld[3] = {
        scopeAxis[0][0] / forwardLength,
        scopeAxis[0][1] / forwardLength,
        scopeAxis[0][2] / forwardLength,
    };

    const float originDeltaWorld[3] = {
        calibratedScopeOrigin[0] - cameraOrigin[0],
        calibratedScopeOrigin[1] - cameraOrigin[1],
        calibratedScopeOrigin[2] - cameraOrigin[2],
    };

    float scopeOriginCameraLocal[3] = {};
    float scopeAxisCameraLocal[3][3] = {};

    for (int cameraComponent = 0;
         cameraComponent < 3;
         ++cameraComponent)
    {
        scopeOriginCameraLocal[cameraComponent] =
            originDeltaWorld[0] *
                cameraAxis[cameraComponent][0] +
            originDeltaWorld[1] *
                cameraAxis[cameraComponent][1] +
            originDeltaWorld[2] *
                cameraAxis[cameraComponent][2];

        for (int scopeAxisRow = 0;
             scopeAxisRow < 3;
             ++scopeAxisRow)
        {
            scopeAxisCameraLocal[
                scopeAxisRow][cameraComponent] =
                scopeAxis[scopeAxisRow][0] *
                    cameraAxis[cameraComponent][0] +
                scopeAxis[scopeAxisRow][1] *
                    cameraAxis[cameraComponent][1] +
                scopeAxis[scopeAxisRow][2] *
                    cameraAxis[cameraComponent][2];
        }
    }

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    std::memcpy(
        g_vrPhysicalSniperScopeOriginWorld,
        calibratedScopeOrigin,
        sizeof(calibratedScopeOrigin));

    std::memcpy(
        g_vrPhysicalSniperScopeForwardWorld,
        scopeForwardWorld,
        sizeof(scopeForwardWorld));

    std::memcpy(
        g_vrPhysicalSniperScopeAxisWorld,
        scopeAxis,
        sizeof(g_vrPhysicalSniperScopeAxisWorld));

    g_vrPhysicalSniperScopePoseWorldValid = true;

    if (!g_vrRightControllerWeaponPoseValid)
    {
        g_vrPhysicalSniperScopeOffsetWeaponLocalValid = false;
        return;
    }

    const float controllerToScopeCameraLocal[3] = {
        scopeOriginCameraLocal[0] -
            g_vrRightControllerWeaponPosition[0],
        scopeOriginCameraLocal[1] -
            g_vrRightControllerWeaponPosition[1],
        scopeOriginCameraLocal[2] -
            g_vrRightControllerWeaponPosition[2],
    };

    for (int scopeAxisRow = 0;
         scopeAxisRow < 3;
         ++scopeAxisRow)
    {
        g_vrPhysicalSniperScopeOffsetWeaponLocal[
            scopeAxisRow] =
            controllerToScopeCameraLocal[0] *
                scopeAxisCameraLocal[scopeAxisRow][0] +
            controllerToScopeCameraLocal[1] *
                scopeAxisCameraLocal[scopeAxisRow][1] +
            controllerToScopeCameraLocal[2] *
                scopeAxisCameraLocal[scopeAxisRow][2];
    }

    g_vrPhysicalSniperScopeOffsetWeaponLocalValid = true;
}

bool VR_GetPhysicalSniperScopeAimWorld(
    float scopeOrigin[3],
    float scopeForward[3])
{
    if (scopeOrigin == nullptr ||
        scopeForward == nullptr ||
        !VR_IsPhysicalSniperScopeAimActive())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        g_vrWeaponControllerPoseMutex);

    if (!g_vrPhysicalSniperScopePoseWorldValid)
    {
        return false;
    }

    std::memcpy(
        scopeOrigin,
        g_vrPhysicalSniperScopeOriginWorld,
        sizeof(g_vrPhysicalSniperScopeOriginWorld));

    std::memcpy(
        scopeForward,
        g_vrPhysicalSniperScopeForwardWorld,
        sizeof(g_vrPhysicalSniperScopeForwardWorld));

    return true;
}
