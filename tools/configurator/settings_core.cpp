#include "settings_core.h"
#include "vr/vr_input_bindings.h"
#include "vr/vr_hud_layout.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <system_error>

namespace kisak::configurator
{
namespace
{

SettingDefinition Toggle(
    const char* key,
    const char* label,
    const char* description,
    const SettingPage page,
    const bool defaultValue,
    const bool advanced = false)
{
    return {
        key,
        label,
        description,
        SettingType::Toggle,
        page,
        defaultValue ? "1" : "0",
        0.0,
        1.0,
        0,
        advanced,
        {{"0", "Off"}, {"1", "On"}},
    };
}

SettingDefinition Integer(
    const char* key,
    const char* label,
    const char* description,
    const SettingPage page,
    const int defaultValue,
    const int minimumValue,
    const int maximumValue,
    const bool advanced = false)
{
    return {
        key,
        label,
        description,
        SettingType::Integer,
        page,
        std::to_string(defaultValue),
        static_cast<double>(minimumValue),
        static_cast<double>(maximumValue),
        0,
        advanced,
        {},
    };
}

SettingDefinition Decimal(
    const char* key,
    const char* label,
    const char* description,
    const SettingPage page,
    const char* defaultValue,
    const double minimumValue,
    const double maximumValue,
    const int decimalPlaces,
    const bool advanced = false)
{
    return {
        key,
        label,
        description,
        SettingType::Decimal,
        page,
        defaultValue,
        minimumValue,
        maximumValue,
        decimalPlaces,
        advanced,
        {},
    };
}

SettingDefinition Choice(
    const char* key,
    const char* label,
    const char* description,
    const SettingPage page,
    const char* defaultValue,
    std::initializer_list<SettingChoice> choices,
    const bool advanced = false)
{
    return {
        key,
        label,
        description,
        SettingType::Choice,
        page,
        defaultValue,
        0.0,
        0.0,
        0,
        advanced,
        choices,
    };
}

SettingDefinition Binding(
    const kisak::vr::input::Action action,
    const bool alternate)
{
    namespace input = kisak::vr::input;

    const input::ActionDefinition& actionDefinition =
        input::GetActionDefinition(action);

    SettingDefinition definition;
    definition.key = alternate
        ? actionDefinition.alternateSettingKey
        : actionDefinition.settingKey;
    definition.label = actionDefinition.label;
    if (alternate)
    {
        definition.label += " (alternate)";
    }
    definition.description = actionDefinition.description;
    if (alternate)
    {
        definition.description +=
            " This optional second binding may be left unbound.";
    }
    definition.type = SettingType::Binding;
    definition.page = SettingPage::Controls;
    definition.defaultValue = alternate
        ? actionDefinition.defaultAlternateBinding
        : actionDefinition.defaultBinding;
    definition.advanced = alternate;

    for (const input::SourceDefinition& sourceDefinition :
         input::SourceDefinitions())
    {
        if (sourceDefinition.source == input::Source::Unbound ||
            sourceDefinition.valueType == actionDefinition.valueType)
        {
            definition.choices.push_back({
                sourceDefinition.id,
                sourceDefinition.label,
            });
        }
    }

    return definition;
}

const std::vector<SettingDefinition> kCatalog = {
    Choice(
        "KISAK_VR_BACKEND",
        "Runtime backend",
        "Auto tries OpenXR first and falls back to the experimental 32-bit OpenVR path.",
        SettingPage::Quick,
        "auto",
        {{"auto", "Automatic"}, {"openxr", "OpenXR only"}, {"openvr", "OpenVR fallback"}}),
    Choice(
        "KISAK_VR_TURN_MODE",
        "Turning",
        "Choose comfort snap turning or continuous analog smooth turning.",
        SettingPage::Quick,
        "snap",
        {{"snap", "Snap"}, {"smooth", "Smooth"}}),
    Decimal(
        "KISAK_VR_SNAP_TURN_ANGLE",
        "Snap angle (degrees)",
        "Angle applied by each deliberate horizontal right-stick flick.",
        SettingPage::Quick,
        "45",
        15.0,
        90.0,
        0),
    Decimal(
        "KISAK_VR_SMOOTH_TURN_SPEED",
        "Smooth speed (deg/sec)",
        "Maximum smooth-turn speed at full right-stick deflection.",
        SettingPage::Quick,
        "120",
        30.0,
        360.0,
        0),
    Decimal(
        "KISAK_VR_TURN_DEADZONE",
        "Turn deadzone",
        "Horizontal right-stick deadzone. Larger values reduce accidental turning.",
        SettingPage::Quick,
        "0.25",
        0.10,
        0.50,
        2),
    Choice(
        "KISAK_VR_MOVEMENT_DIRECTION",
        "Movement direction",
        "Head uses gaze yaw, Body uses the game body, and Left hand uses controller pointing direction.",
        SettingPage::Quick,
        "head",
        {{"head", "Head-relative"}, {"body", "Body-relative"}, {"left_hand", "Left-hand-relative"}}),
    Decimal(
        "KISAK_VR_MOVEMENT_DEADZONE",
        "Movement deadzone",
        "Radial deadzone for the left movement stick.",
        SettingPage::Quick,
        "0.18",
        0.05,
        0.40,
        2),

    Choice(
        "KISAK_VR_PLAY_MODE",
        "Play posture",
        "Standing can measure your physical eye height from a floor-aware runtime. Seated keeps a full-height virtual player while recentering around your chair.",
        SettingPage::Calibration,
        "standing",
        {{"standing", "Standing"}, {"seated", "Seated"}}),
    Decimal(
        "KISAK_VR_STANDING_EYE_HEIGHT",
        "Standing virtual eye height (inches)",
        "Virtual eye height used in standing mode. The guided measurement button can fill this from the headset's floor reference.",
        SettingPage::Calibration,
        "60.0",
        42.0,
        84.0,
        1),
    Decimal(
        "KISAK_VR_SEATED_EYE_HEIGHT",
        "Seated virtual eye height (inches)",
        "Virtual player eye height while you remain physically seated. Keep 60 for COD4's native full standing stature.",
        SettingPage::Calibration,
        "60.0",
        42.0,
        84.0,
        1),
    Toggle(
        "KISAK_VR_RECENTER_ON_START",
        "Recenter at first gameplay camera",
        "Capture position and forward/level orientation when a mission first reaches the player camera. You can still recenter live afterward.",
        SettingPage::Calibration,
        true),

    Decimal(
        "KISAK_VR_HUD_SAFE_X",
        "HUD horizontal safe area",
        "Smaller values pull edge-aligned HUD elements toward the center. The visual editor is the recommended way to change this.",
        SettingPage::Hud,
        "0.50",
        0.50,
        1.00,
        2,
        true),
    Decimal(
        "KISAK_VR_HUD_SAFE_Y",
        "HUD vertical safe area",
        "Smaller values pull top and bottom HUD elements toward the center. The visual editor is the recommended way to change this.",
        SettingPage::Hud,
        "1.00",
        0.50,
        1.00,
        2,
        true),
    Integer(
        "KISAK_VR_HUD_BOTTOM_LEFT_X_OFFSET",
        "Ammo/equipment horizontal offset",
        "Move the bottom-left ammo and equipment cluster right in virtual HUD pixels. The visual editor writes this value.",
        SettingPage::Hud,
        0,
        -320,
        640,
        true),
    Integer(
        "KISAK_VR_HUD_BOTTOM_LEFT_Y_OFFSET",
        "Ammo/equipment vertical offset",
        "Move the bottom-left ammo and equipment cluster up in virtual HUD pixels. The visual editor writes this value.",
        SettingPage::Hud,
        0,
        -240,
        480,
        true),
    Decimal(
        "KISAK_VR_HUD_BOTTOM_LEFT_SCALE",
        "Ammo/action HUD scale",
        "Scale for the bottom-left weapon, ammunition, and action-slot cluster. Drag its resize handle in the visual editor.",
        SettingPage::Hud,
        "0.50",
        0.50,
        2.00,
        2,
        true),
    Toggle(
        "KISAK_VR_COMPASS_ENABLED",
        "Compass",
        "Show or hide COD4's normal compass.",
        SettingPage::Hud,
        true),
    Decimal(
        "KISAK_VR_COMPASS_SIZE",
        "Compass size",
        "Scale for the compass and its objective icons.",
        SettingPage::Hud,
        "1.00",
        0.50,
        2.00,
        2,
        true),
    Toggle(
        "KISAK_VR_COMPASS_ROTATION",
        "Rotating compass",
        "On rotates the compass with the player; Off keeps north fixed.",
        SettingPage::Hud,
        true),
    Integer(
        "KISAK_VR_COMPASS_INSET_X",
        "Compass inset left",
        "Move the lower-right compass left in virtual HUD pixels.",
        SettingPage::Hud,
        220,
        -80,
        600,
        true),
    Integer(
        "KISAK_VR_COMPASS_INSET_Y",
        "Compass inset up",
        "Move the lower-right compass upward in virtual HUD pixels.",
        SettingPage::Hud,
        48,
        -80,
        440,
        true),
    Integer(
        "KISAK_VR_GAME_MESSAGE_X_OFFSET",
        "Game-text horizontal offset",
        "Move mission notifications right (positive) or left (negative).",
        SettingPage::Hud,
        0,
        -300,
        300,
        true),
    Integer(
        "KISAK_VR_GAME_MESSAGE_Y_OFFSET",
        "Game-text vertical offset",
        "Move normal mission notifications down (positive) or up (negative).",
        SettingPage::Hud,
        72,
        -240,
        400,
        true),
    Decimal(
        "KISAK_VR_GAME_MESSAGE_SCALE",
        "Game-text scale",
        "Scale mission notifications and status messages.",
        SettingPage::Hud,
        "1.00",
        0.50,
        2.00,
        2,
        true),
    Integer(
        "KISAK_VR_OBJECTIVE_MESSAGE_X_OFFSET",
        "Objective/banner horizontal offset",
        "Move bold objective and mission-status banners right (positive) or left (negative).",
        SettingPage::Hud,
        0,
        -300,
        300,
        true),
    Integer(
        "KISAK_VR_OBJECTIVE_MESSAGE_Y_OFFSET",
        "Objective/banner vertical offset",
        "Move bold objective and mission-status banners down (positive) or up (negative).",
        SettingPage::Hud,
        0,
        -180,
        270,
        true),
    Decimal(
        "KISAK_VR_OBJECTIVE_MESSAGE_SCALE",
        "Objective/banner scale",
        "Scale bold objective and mission-status banners.",
        SettingPage::Hud,
        "1.00",
        0.50,
        2.00,
        2,
        true),
    Toggle(
        "KISAK_VR_CROSSHAIR",
        "Crosshair",
        "Show COD4's normal weapon crosshair when the mission permits it.",
        SettingPage::Hud,
        true),
    Toggle(
        "KISAK_VR_SUBTITLES",
        "Subtitles",
        "Show spoken-dialogue subtitles.",
        SettingPage::Hud,
        true),
    Integer(
        "KISAK_VR_SUBTITLE_X_OFFSET",
        "Subtitle horizontal offset",
        "Move spoken-dialogue subtitles right (positive) or left (negative).",
        SettingPage::Hud,
        0,
        -300,
        300,
        true),
    Integer(
        "KISAK_VR_SUBTITLE_Y_OFFSET",
        "Subtitle vertical offset",
        "Move spoken-dialogue subtitles down (positive) or up (negative).",
        SettingPage::Hud,
        0,
        -400,
        80,
        true),
    Decimal(
        "KISAK_VR_SUBTITLE_SCALE",
        "Subtitle scale",
        "Scale spoken-dialogue subtitles without changing notification text.",
        SettingPage::Hud,
        "1.00",
        0.50,
        2.00,
        2,
        true),

    Decimal(
        "KISAK_VR_WEAPON_OFFSET_FORWARD",
        "Weapon forward offset",
        "Move the right-hand weapon along the controller's forward axis, in game inches.",
        SettingPage::Weapons,
        "0.00",
        -8.0,
        8.0,
        2),
    Decimal(
        "KISAK_VR_WEAPON_OFFSET_LEFT",
        "Weapon left offset",
        "Move the right-hand weapon along the controller's left axis, in game inches.",
        SettingPage::Weapons,
        "0.00",
        -8.0,
        8.0,
        2),
    Decimal(
        "KISAK_VR_WEAPON_OFFSET_UP",
        "Weapon up offset",
        "Move the right-hand weapon along the controller's up axis, in game inches.",
        SettingPage::Weapons,
        "0.00",
        -8.0,
        8.0,
        2),
    Decimal(
        "KISAK_VR_WEAPON_PITCH",
        "Weapon pitch",
        "Rotate the right-hand weapon around controller-local pitch.",
        SettingPage::Weapons,
        "0.0",
        -45.0,
        45.0,
        1),
    Decimal(
        "KISAK_VR_WEAPON_YAW",
        "Weapon yaw",
        "Rotate the right-hand weapon around controller-local yaw.",
        SettingPage::Weapons,
        "0.0",
        -45.0,
        45.0,
        1),
    Decimal(
        "KISAK_VR_WEAPON_ROLL",
        "Weapon roll",
        "Rotate the right-hand weapon around controller-local roll.",
        SettingPage::Weapons,
        "0.0",
        -45.0,
        45.0,
        1),
    Decimal(
        "KISAK_VR_LEFT_HAND_OFFSET_FORWARD",
        "Left hand forward offset",
        "Move the floating left glove along controller forward, in game inches.",
        SettingPage::Weapons,
        "0.00",
        -8.0,
        8.0,
        2),
    Decimal(
        "KISAK_VR_LEFT_HAND_OFFSET_LEFT",
        "Left hand left offset",
        "Move the floating left glove along controller left, in game inches.",
        SettingPage::Weapons,
        "0.00",
        -8.0,
        8.0,
        2),
    Decimal(
        "KISAK_VR_LEFT_HAND_OFFSET_UP",
        "Left hand up offset",
        "Move the floating left glove along controller up, in game inches.",
        SettingPage::Weapons,
        "0.00",
        -8.0,
        8.0,
        2),
    Decimal(
        "KISAK_VR_LEFT_HAND_PITCH",
        "Left hand pitch",
        "Rotate the floating left glove around controller-local pitch.",
        SettingPage::Weapons,
        "0.0",
        -180.0,
        180.0,
        1),
    Decimal(
        "KISAK_VR_LEFT_HAND_YAW",
        "Left hand yaw",
        "Rotate the floating left glove around controller-local yaw.",
        SettingPage::Weapons,
        "0.0",
        -180.0,
        180.0,
        1),
    Decimal(
        "KISAK_VR_LEFT_HAND_ROLL",
        "Left hand roll",
        "Rotate the floating left glove around controller-local roll.",
        SettingPage::Weapons,
        "0.0",
        -180.0,
        180.0,
        1),
    Decimal(
        "KISAK_VR_LEFT_HAND_GRIP_RADIUS",
        "Support-hand grip radius",
        "Maximum distance from the weapon wrist anchor at which squeeze attaches the support hand.",
        SettingPage::Weapons,
        "14.0",
        3.0,
        24.0,
        1),
    Decimal(
        "KISAK_VR_TWO_HAND_STRENGTH",
        "Two-hand stabilization",
        "How strongly the weapon aligns with the line between both controllers.",
        SettingPage::Weapons,
        "1.00",
        0.0,
        1.0,
        2),
    Decimal(
        "KISAK_VR_WEAPON_POSITION_RESPONSE",
        "Weapon position response",
        "Higher values follow controller position more immediately; lower values smooth tracking shimmer.",
        SettingPage::Weapons,
        "0.45",
        0.10,
        1.0,
        2,
        true),
    Decimal(
        "KISAK_VR_WEAPON_ORIENTATION_RESPONSE",
        "Weapon rotation response",
        "Higher values follow controller rotation more immediately; lower values add smoothing.",
        SettingPage::Weapons,
        "0.55",
        0.10,
        1.0,
        2,
        true),

    Toggle(
        "KISAK_VR_MANUAL_RELOAD",
        "Physical magazine reload",
        "Eject, draw, and insert supported magazines physically. Off uses COD4's automatic reload.",
        SettingPage::Interactions,
        true),
    Toggle(
        "KISAK_VR_MANUAL_GRENADES",
        "Physical hip grenades",
        "Draw grenades from the belt and throw with tracked left-hand motion.",
        SettingPage::Interactions,
        true),
    Toggle(
        "KISAK_VR_TRACKED_HANDS",
        "Tracked left hand",
        "Render the independent IK left glove at its OpenXR palm pose.",
        SettingPage::Interactions,
        true),
    Decimal(
        "KISAK_VR_BELT_FORWARD_OFFSET",
        "Belt forward offset",
        "Move all hip grab zones forward or backward relative to the headset, in inches.",
        SettingPage::Interactions,
        "0.0",
        -12.0,
        12.0,
        1),
    Decimal(
        "KISAK_VR_BELT_HEIGHT",
        "Belt height",
        "Vertical center of the hip grab zones relative to the headset, in inches.",
        SettingPage::Interactions,
        "-28.0",
        -48.0,
        -8.0,
        1),
    Decimal(
        "KISAK_VR_BELT_HIP_DISTANCE",
        "Hip distance from center",
        "Left/right distance from body center to each grenade or magazine grab zone.",
        SettingPage::Interactions,
        "13.0",
        4.0,
        24.0,
        1),
    Decimal(
        "KISAK_VR_BELT_GRAB_RADIUS",
        "Hip grab radius",
        "Lateral half-width of each hip grab zone. Keep it smaller than hip distance.",
        SettingPage::Interactions,
        "11.0",
        3.0,
        18.0,
        1),
    Decimal(
        "KISAK_VR_RELOAD_INSERT_RADIUS",
        "Magazine insertion radius",
        "Distance from the magazine well that counts as a successful insertion.",
        SettingPage::Interactions,
        "6.5",
        3.0,
        12.0,
        1),
    Decimal(
        "KISAK_VR_GRENADE_DROP_SPEED",
        "Grenade drop threshold",
        "Hand speed below this value is treated as a deliberate drop rather than a throw.",
        SettingPage::Interactions,
        "35",
        10.0,
        100.0,
        0,
        true),
    Decimal(
        "KISAK_VR_GRENADE_FULL_THROW_SPEED",
        "Full-strength hand speed",
        "Hand speed that maps to the strongest configured grenade throw.",
        SettingPage::Interactions,
        "260",
        100.0,
        500.0,
        0,
        true),
    Decimal(
        "KISAK_VR_GRENADE_MIN_STRENGTH",
        "Minimum throw strength",
        "Fraction of the grenade asset's native horizontal speed for a weak throw.",
        SettingPage::Interactions,
        "0.70",
        0.30,
        1.00,
        2,
        true),
    Decimal(
        "KISAK_VR_GRENADE_MAX_STRENGTH",
        "Maximum throw strength",
        "Fraction of native horizontal speed used at full physical strength.",
        SettingPage::Interactions,
        "1.15",
        0.80,
        1.50,
        2,
        true),
    Decimal(
        "KISAK_VR_GRENADE_VERTICAL_SCALE",
        "Vertical throw influence",
        "How strongly physical upward hand velocity contributes to the grenade arc.",
        SettingPage::Interactions,
        "0.65",
        0.0,
        2.0,
        2,
        true),

    Decimal(
        "KISAK_VR_SCOPE_FORWARD_METERS",
        "Scope forward offset (m)",
        "Fine adjustment along the final rendered weapon's forward axis.",
        SettingPage::Scope,
        "-0.10",
        -0.25,
        0.25,
        3),
    Decimal(
        "KISAK_VR_SCOPE_LEFT_METERS",
        "Scope left offset (m)",
        "Fine adjustment along the final rendered weapon's left axis.",
        SettingPage::Scope,
        "0.000",
        -0.25,
        0.25,
        3),
    Decimal(
        "KISAK_VR_SCOPE_UP_METERS",
        "Scope up offset (m)",
        "Fine adjustment along the final rendered weapon's up axis.",
        SettingPage::Scope,
        "0.000",
        -0.25,
        0.25,
        3),
    Decimal(
        "KISAK_VR_SCOPE_RADIUS_METERS",
        "Scope lens radius (m)",
        "Physical radius of the circular lens rendered on the rifle optic.",
        SettingPage::Scope,
        "0.024",
        0.015,
        0.080,
        3),
    Choice(
        "KISAK_VR_SCOPE_CAPTURE_SIZE",
        "Scope capture quality",
        "Resolution of the dedicated square weapon-free scope camera.",
        SettingPage::Scope,
        "1024",
        {{"512", "512 px (fast)"}, {"768", "768 px"}, {"1024", "1024 px (tested)"}, {"1280", "1280 px"}, {"1536", "1536 px (high)"}}),

    Choice(
        "VR_CUSTOM_MODE",
        "Packed rendering mode",
        "Native preserves the full Quest 3 eye and scope layout. Performance uses the verified lower packed mode.",
        SettingPage::Graphics,
        "6016x2688",
        {{"6016x2688", "Native 6016 x 2688"}, {"4768x2016", "Performance 4768 x 2016"}}),
    Choice(
        "KISAK_VR_OUTPUT_SCALE",
        "Eye output scale",
        "Verified eye scale paired with the selected packed rendering layout.",
        SettingPage::Graphics,
        "1.00",
        {{"1.00", "Native 1.00"}, {"0.75", "Performance 0.75"}}),
    Toggle(
        "KISAK_VR_FSR",
        "FSR upscaling",
        "Use FSR 1 EASU/RCAS when rendering below native eye resolution.",
        SettingPage::Graphics,
        false),
    Decimal(
        "KISAK_VR_FSR_SHARPNESS",
        "FSR sharpness",
        "RCAS sharpening strength when FSR is enabled.",
        SettingPage::Graphics,
        "0.60",
        0.0,
        1.0,
        2),
    Decimal(
        "KISAK_VR_BRIGHTNESS",
        "Compositor brightness",
        "Brightness scale applied before submitting to the headset.",
        SettingPage::Graphics,
        "1.00",
        0.20,
        1.00,
        2),
    Toggle(
        "KISAK_VR_SHADOWS",
        "Synchronized shadows",
        "Render the VR shadow path. Disable only for corruption or a significant performance problem.",
        SettingPage::Graphics,
        true),
    Toggle(
        "KISAK_VR_GPU_BRIDGE",
        "GPU shared-texture bridge",
        "Use the fast D3D9Ex-to-D3D11 shared GPU bridge.",
        SettingPage::Graphics,
        true,
        true),
    Toggle(
        "KISAK_VR_ALLOW_OVERSIZED_WINDOW",
        "Allow oversized game window",
        "Permit the packed renderer's window to exceed the desktop bounds.",
        SettingPage::Graphics,
        true,
        true),

    Integer(
        "KISAK_VR_INPUT_BINDINGS_VERSION",
        "Controller binding format",
        "Internal schema version for controller-neutral bindings.",
        SettingPage::Controls,
        4,
        4,
        4,
        true),

    Binding(kisak::vr::input::Action::Attack, false),
    Binding(kisak::vr::input::Action::Attack, true),
    Binding(kisak::vr::input::Action::Aim, false),
    Binding(kisak::vr::input::Action::Aim, true),
    Binding(kisak::vr::input::Action::Jump, false),
    Binding(kisak::vr::input::Action::Jump, true),
    Binding(kisak::vr::input::Action::Use, false),
    Binding(kisak::vr::input::Action::Use, true),
    Binding(kisak::vr::input::Action::Reload, false),
    Binding(kisak::vr::input::Action::Reload, true),
    Binding(kisak::vr::input::Action::Sprint, false),
    Binding(kisak::vr::input::Action::Sprint, true),
    Binding(kisak::vr::input::Action::Melee, false),
    Binding(kisak::vr::input::Action::Melee, true),
    Binding(kisak::vr::input::Action::Stance, false),
    Binding(kisak::vr::input::Action::Stance, true),
    Binding(kisak::vr::input::Action::LowerStance, false),
    Binding(kisak::vr::input::Action::LowerStance, true),
    Binding(kisak::vr::input::Action::NextWeapon, false),
    Binding(kisak::vr::input::Action::NextWeapon, true),
    Binding(kisak::vr::input::Action::Offhand, false),
    Binding(kisak::vr::input::Action::Offhand, true),
    Binding(kisak::vr::input::Action::SupportGrip, false),
    Binding(kisak::vr::input::Action::SupportGrip, true),
    Binding(kisak::vr::input::Action::PauseMenu, false),
    Binding(kisak::vr::input::Action::PauseMenu, true),
    Binding(kisak::vr::input::Action::MenuConfirm, false),
    Binding(kisak::vr::input::Action::MenuConfirm, true),
    Binding(kisak::vr::input::Action::MenuBack, false),
    Binding(kisak::vr::input::Action::MenuBack, true),
    Binding(kisak::vr::input::Action::MenuNavigate, false),
    Binding(kisak::vr::input::Action::MenuNavigate, true),
    Binding(kisak::vr::input::Action::GrenadeLauncher, false),
    Binding(kisak::vr::input::Action::GrenadeLauncher, true),
    Binding(kisak::vr::input::Action::NightVision, false),
    Binding(kisak::vr::input::Action::NightVision, true),
    Binding(kisak::vr::input::Action::Airstrike, false),
    Binding(kisak::vr::input::Action::Airstrike, true),
    Binding(kisak::vr::input::Action::C4, false),
    Binding(kisak::vr::input::Action::C4, true),
    Binding(kisak::vr::input::Action::ScopeZoom, false),
    Binding(kisak::vr::input::Action::ScopeZoom, true),
    Binding(kisak::vr::input::Action::Move, false),
    Binding(kisak::vr::input::Action::Move, true),
    Binding(kisak::vr::input::Action::Turn, false),
    Binding(kisak::vr::input::Action::Turn, true),

    Toggle(
        "KISAK_VR_UNLOCK_MISSIONS",
        "Unlock all missions",
        "Enable COD4's built-in Mission Select unlock without developer mode.",
        SettingPage::Advanced,
        true),
    Toggle(
        "KISAK_VR_VERBOSE_DIAGNOSTICS",
        "Verbose VR diagnostics",
        "Enable high-volume controller, pose, and retired mission traces in console.log.",
        SettingPage::Advanced,
        false),
    Toggle(
        "KISAK_VR_CAMERA_SHAKE",
        "Scripted camera shake",
        "Allow COD4 earthquake and scripted camera-shake effects.",
        SettingPage::Advanced,
        true),
    Decimal(
        "KISAK_VR_WEAPON_BOB_AMPLITUDE",
        "Weapon bob amplitude",
        "COD4 viewmodel bob amplitude. Zero removes weapon bob.",
        SettingPage::Advanced,
        "0.16",
        0.0,
        1.0,
        2),
};

std::string Trim(std::string value)
{
    const auto isSpace = [](const unsigned char character)
    {
        return character == ' ' || character == '\t' ||
               character == '\r' || character == '\n';
    };

    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            [&](const char character)
            {
                return !isSpace(static_cast<unsigned char>(character));
            }));

    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            [&](const char character)
            {
                return !isSpace(static_cast<unsigned char>(character));
            }).base(),
        value.end());

    return value;
}

bool ParseFiniteNumber(const std::string& text, double* value)
{
    if (value == nullptr || text.empty())
    {
        return false;
    }

    char* parseEnd = nullptr;
    const double parsed = std::strtod(text.c_str(), &parseEnd);

    if (parseEnd == text.c_str() || parseEnd == nullptr ||
        *parseEnd != '\0' || !std::isfinite(parsed))
    {
        return false;
    }

    *value = parsed;
    return true;
}

bool IsSafeBatchValue(const std::string& value)
{
    return value.find_first_of("\r\n\"%!&|<>^") == std::string::npos;
}

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return {};
    }

    return std::string(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
}

std::string TimestampForFileName()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local = {};

#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif

    std::ostringstream output;
    output << std::put_time(&local, "%Y%m%d-%H%M%S");
    return output.str();
}

std::string TimestampForDisplay()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local = {};

#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif

    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

std::string SafeMetadataValue(
    const std::string& value,
    const char* const fallback)
{
    std::string safe = Trim(value);
    for (char& character : safe)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20u || character == '"' || character == '%' ||
            character == '!' || character == '&' || character == '|' ||
            character == '<' || character == '>' || character == '^')
        {
            character = ' ';
        }
    }
    safe = Trim(safe);
    return safe.empty() ? std::string(fallback) : safe;
}

std::string MetadataValue(
    const std::string& text,
    const char* const name)
{
    const std::regex metadata(
        std::string(R"regex(^\s*rem\s+)regex") + name +
            R"regex(\s*:\s*(.*?)\s*$)regex",
        std::regex::icase);
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        std::smatch match;
        if (std::regex_match(line, match, metadata))
        {
            return Trim(match[1].str());
        }
    }
    return {};
}

std::string BuildSettingsRevision(const SettingsMap& values)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto hashByte = [&hash](const unsigned char byte)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    };

    for (const SettingDefinition& definition : kCatalog)
    {
        const auto found = values.find(definition.key);
        const std::string& value = found == values.end()
            ? definition.defaultValue
            : found->second;
        for (const char character : definition.key)
        {
            hashByte(static_cast<unsigned char>(character));
        }
        hashByte(static_cast<unsigned char>('='));
        for (const char character : value)
        {
            hashByte(static_cast<unsigned char>(character));
        }
        hashByte(static_cast<unsigned char>('\n'));
    }

    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream revision;
    revision << TimestampForFileName() << '-'
             << std::setw(3) << std::setfill('0') << milliseconds.count()
             << '-' << std::hex << std::setw(16) << std::setfill('0') << hash;
    return revision.str();
}

bool RestoreAfterVerificationFailure(
    const std::filesystem::path& userPath,
    const std::filesystem::path& backupPath,
    std::string* const error)
{
    std::error_code filesystemError;
    if (backupPath.empty())
    {
        std::filesystem::remove(userPath, filesystemError);
    }
    else
    {
        std::filesystem::copy_file(
            backupPath,
            userPath,
            std::filesystem::copy_options::overwrite_existing,
            filesystemError);
    }

    if (filesystemError)
    {
        if (error != nullptr)
        {
            *error += " The previous settings could not be restored: " +
                filesystemError.message();
        }
        return false;
    }
    return true;
}

void Set(SettingsMap* values, const char* key, const char* value)
{
    if (values != nullptr)
    {
        (*values)[key] = value;
    }
}

void UpgradeControllerBindings(SettingsMap* const values)
{
    if (values == nullptr)
    {
        return;
    }

    const auto versionValue = values->find(
        "KISAK_VR_INPUT_BINDINGS_VERSION");
    const int version = versionValue == values->end()
        ? 2
        : std::atoi(versionValue->second.c_str());

    namespace input = kisak::vr::input;
    if (version >= 4)
    {
        return;
    }

    if (version < 3)
    {
        for (const input::Action action : {
                 input::Action::GrenadeLauncher,
                 input::Action::NightVision,
                 input::Action::Airstrike,
                 input::Action::C4})
        {
            const input::ActionDefinition& definition =
                input::GetActionDefinition(action);
            const auto current = values->find(definition.settingKey);
            if (current == values->end() ||
                current->second.empty() ||
                current->second == "unbound")
            {
                (*values)[definition.settingKey] =
                    definition.defaultBinding;
            }
        }

        // V57's first test build assigned the right grip to a redundant native
        // off-hand action. Physical grenades already use the support-hand grip,
        // so migrate that former test default to the restored unbound default.
        const input::ActionDefinition& offhand =
            input::GetActionDefinition(input::Action::Offhand);
        const auto offhandValue = values->find(offhand.settingKey);
        if (offhandValue != values->end() &&
            offhandValue->second == "right.squeeze")
        {
            offhandValue->second = "unbound";
        }
    }

    if (version < 4)
    {
        const input::ActionDefinition& jump =
            input::GetActionDefinition(input::Action::Jump);

        const auto valueOr = [values](
            const char* const key,
            const char* const fallback) -> std::string
        {
            const auto found = values->find(key);
            return found == values->end() || found->second.empty()
                ? std::string(fallback)
                : found->second;
        };

        const std::string oldJump =
            valueOr(jump.settingKey, "left.trigger");
        const std::string oldJumpAlternate =
            valueOr(jump.alternateSettingKey, "unbound");
        const std::string oldRaise =
            valueOr(
                "KISAK_VR_BIND_RAISE_STANCE",
                "right.primary_axis.up");
        const std::string oldRaiseAlternate =
            valueOr(
                "KISAK_VR_BIND_RAISE_STANCE_ALT",
                "unbound");

        const bool standardV3Layout =
            oldJump == "left.trigger" &&
            oldJumpAlternate == "unbound" &&
            oldRaise == "right.primary_axis.up" &&
            oldRaiseAlternate == "unbound";

        if (standardV3Layout)
        {
            (*values)[jump.settingKey] =
                jump.defaultBinding;
            (*values)[jump.alternateSettingKey] =
                jump.defaultAlternateBinding;
        }
        else
        {
            // Fold a customized V3 Raise stance / jump slot into any free
            // Jump slot so upgrading does not silently discard it.
            for (const std::string& oldRaiseBinding :
                 {oldRaise, oldRaiseAlternate})
            {
                if (oldRaiseBinding.empty() ||
                    oldRaiseBinding == "unbound" ||
                    oldRaiseBinding == (*values)[jump.settingKey] ||
                    oldRaiseBinding == (*values)[jump.alternateSettingKey])
                {
                    continue;
                }

                if ((*values)[jump.settingKey] == "unbound")
                {
                    (*values)[jump.settingKey] = oldRaiseBinding;
                }
                else if ((*values)[jump.alternateSettingKey] == "unbound")
                {
                    (*values)[jump.alternateSettingKey] = oldRaiseBinding;
                }
            }
        }

        values->erase("KISAK_VR_BIND_RAISE_STANCE");
        values->erase("KISAK_VR_BIND_RAISE_STANCE_ALT");
    }

    (*values)["KISAK_VR_INPUT_BINDINGS_VERSION"] = "4";
}

} // namespace

const std::vector<SettingDefinition>& SettingsCatalog()
{
    return kCatalog;
}

const SettingDefinition* FindSetting(const std::string& key)
{
    const auto found = std::find_if(
        kCatalog.begin(),
        kCatalog.end(),
        [&](const SettingDefinition& definition)
        {
            return definition.key == key;
        });

    return found == kCatalog.end() ? nullptr : &*found;
}

SettingsMap BuiltInDefaults()
{
    SettingsMap values;
    for (const SettingDefinition& definition : kCatalog)
    {
        values[definition.key] = definition.defaultValue;
    }
    return values;
}

kisak::vr::hud::Layout HudLayoutFromSettings(
    const SettingsMap& values)
{
    namespace hud = kisak::vr::hud;

    hud::Layout layout = hud::DefaultLayout();
    const auto number = [&](
        const char* const key,
        const float fallback)
    {
        const auto found = values.find(key);
        double parsed = fallback;
        if (found != values.end() &&
            ParseFiniteNumber(found->second, &parsed))
        {
            return static_cast<float>(parsed);
        }
        return fallback;
    };

    layout.safeX = number("KISAK_VR_HUD_SAFE_X", layout.safeX);
    layout.safeY = number("KISAK_VR_HUD_SAFE_Y", layout.safeY);
    layout.ammoOffsetX = number(
        "KISAK_VR_HUD_BOTTOM_LEFT_X_OFFSET",
        layout.ammoOffsetX);
    layout.ammoOffsetY = number(
        "KISAK_VR_HUD_BOTTOM_LEFT_Y_OFFSET",
        layout.ammoOffsetY);
    layout.ammoScale = number(
        "KISAK_VR_HUD_BOTTOM_LEFT_SCALE",
        layout.ammoScale);
    layout.compassEnabled =
        values.count("KISAK_VR_COMPASS_ENABLED") == 0u ||
        values.at("KISAK_VR_COMPASS_ENABLED") != "0";
    layout.compassInsetX = number(
        "KISAK_VR_COMPASS_INSET_X",
        layout.compassInsetX);
    layout.compassInsetY = number(
        "KISAK_VR_COMPASS_INSET_Y",
        layout.compassInsetY);
    layout.compassScale = number(
        "KISAK_VR_COMPASS_SIZE",
        layout.compassScale);
    layout.notificationOffsetX = number(
        "KISAK_VR_GAME_MESSAGE_X_OFFSET",
        layout.notificationOffsetX);
    layout.notificationOffsetY = number(
        "KISAK_VR_GAME_MESSAGE_Y_OFFSET",
        layout.notificationOffsetY);
    layout.notificationScale = number(
        "KISAK_VR_GAME_MESSAGE_SCALE",
        layout.notificationScale);
    layout.objectiveOffsetX = number(
        "KISAK_VR_OBJECTIVE_MESSAGE_X_OFFSET",
        layout.objectiveOffsetX);
    layout.objectiveOffsetY = number(
        "KISAK_VR_OBJECTIVE_MESSAGE_Y_OFFSET",
        layout.objectiveOffsetY);
    layout.objectiveScale = number(
        "KISAK_VR_OBJECTIVE_MESSAGE_SCALE",
        layout.objectiveScale);
    layout.subtitleOffsetX = number(
        "KISAK_VR_SUBTITLE_X_OFFSET",
        layout.subtitleOffsetX);
    layout.subtitleOffsetY = number(
        "KISAK_VR_SUBTITLE_Y_OFFSET",
        layout.subtitleOffsetY);
    layout.subtitleScale = number(
        "KISAK_VR_SUBTITLE_SCALE",
        layout.subtitleScale);

    hud::ClampLayout(&layout);
    return layout;
}

void ApplyHudLayoutToSettings(
    const kisak::vr::hud::Layout& requestedLayout,
    SettingsMap* const values)
{
    if (values == nullptr)
    {
        return;
    }

    kisak::vr::hud::Layout layout = requestedLayout;
    kisak::vr::hud::ClampLayout(&layout);

    const auto integer = [](const float value)
    {
        return std::to_string(
            static_cast<int>(std::lround(value)));
    };
    const auto decimal = [](const float value)
    {
        std::ostringstream output;
        output.setf(std::ios::fixed, std::ios::floatfield);
        output.precision(2);
        output << value;
        return output.str();
    };

    (*values)["KISAK_VR_HUD_SAFE_X"] = decimal(layout.safeX);
    (*values)["KISAK_VR_HUD_SAFE_Y"] = decimal(layout.safeY);
    (*values)["KISAK_VR_HUD_BOTTOM_LEFT_X_OFFSET"] =
        integer(layout.ammoOffsetX);
    (*values)["KISAK_VR_HUD_BOTTOM_LEFT_Y_OFFSET"] =
        integer(layout.ammoOffsetY);
    (*values)["KISAK_VR_HUD_BOTTOM_LEFT_SCALE"] =
        decimal(layout.ammoScale);
    (*values)["KISAK_VR_COMPASS_ENABLED"] =
        layout.compassEnabled ? "1" : "0";
    (*values)["KISAK_VR_COMPASS_INSET_X"] =
        integer(layout.compassInsetX);
    (*values)["KISAK_VR_COMPASS_INSET_Y"] =
        integer(layout.compassInsetY);
    (*values)["KISAK_VR_COMPASS_SIZE"] =
        decimal(layout.compassScale);
    (*values)["KISAK_VR_GAME_MESSAGE_X_OFFSET"] =
        integer(layout.notificationOffsetX);
    (*values)["KISAK_VR_GAME_MESSAGE_Y_OFFSET"] =
        integer(layout.notificationOffsetY);
    (*values)["KISAK_VR_GAME_MESSAGE_SCALE"] =
        decimal(layout.notificationScale);
    (*values)["KISAK_VR_OBJECTIVE_MESSAGE_X_OFFSET"] =
        integer(layout.objectiveOffsetX);
    (*values)["KISAK_VR_OBJECTIVE_MESSAGE_Y_OFFSET"] =
        integer(layout.objectiveOffsetY);
    (*values)["KISAK_VR_OBJECTIVE_MESSAGE_SCALE"] =
        decimal(layout.objectiveScale);
    (*values)["KISAK_VR_SUBTITLE_X_OFFSET"] =
        integer(layout.subtitleOffsetX);
    (*values)["KISAK_VR_SUBTITLE_Y_OFFSET"] =
        integer(layout.subtitleOffsetY);
    (*values)["KISAK_VR_SUBTITLE_SCALE"] =
        decimal(layout.subtitleScale);
}

SettingsMap ParseBatchSettings(
    const std::string& text,
    std::vector<ValidationMessage>* messages)
{
    SettingsMap values;
    std::istringstream input(text);
    std::string line;
    std::size_t lineNumber = 0;

    const std::regex assignment(
        R"regex(^\s*set\s+"?([A-Za-z_][A-Za-z0-9_]*)=([^"\r\n]*)"?\s*$)regex",
        std::regex::icase);

    while (std::getline(input, line))
    {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        std::smatch match;
        if (!std::regex_match(line, match, assignment))
        {
            continue;
        }

        std::string key = match[1].str();
        std::transform(
            key.begin(),
            key.end(),
            key.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(std::toupper(character));
            });

        std::string value = Trim(match[2].str());
        const SettingDefinition* const definition = FindSetting(key);
        const bool legacyRaiseStanceSetting =
            key == "KISAK_VR_BIND_RAISE_STANCE" ||
            key == "KISAK_VR_BIND_RAISE_STANCE_ALT";
        if (definition == nullptr && !legacyRaiseStanceSetting)
        {
            continue;
        }

        if (definition != nullptr &&
            definition->type == SettingType::Binding)
        {
            value = kisak::vr::input::CanonicalizeLegacyValue(key, value);
        }

        if (!IsSafeBatchValue(value))
        {
            if (messages != nullptr)
            {
                messages->push_back({
                    ValidationMessage::Severity::Error,
                    key,
                    "Unsafe characters were ignored on line " +
                        std::to_string(lineNumber) + ".",
                });
            }
            continue;
        }

        values[key] = value;
    }

    return values;
}

std::vector<ValidationMessage> ValidateSettings(
    const SettingsMap& values)
{
    std::vector<ValidationMessage> messages;

    for (const SettingDefinition& definition : kCatalog)
    {
        const auto found = values.find(definition.key);
        if (found == values.end())
        {
            messages.push_back({
                ValidationMessage::Severity::Error,
                definition.key,
                "A required setting is missing.",
            });
            continue;
        }

        const std::string& value = found->second;
        if (!IsSafeBatchValue(value))
        {
            messages.push_back({
                ValidationMessage::Severity::Error,
                definition.key,
                "The value contains characters that are unsafe in a Windows batch settings file.",
            });
            continue;
        }

        if (definition.type == SettingType::Choice ||
            definition.type == SettingType::Toggle)
        {
            const bool allowed = std::any_of(
                definition.choices.begin(),
                definition.choices.end(),
                [&](const SettingChoice& choice)
                {
                    return choice.value == value;
                });

            if (!allowed)
            {
                messages.push_back({
                    ValidationMessage::Severity::Error,
                    definition.key,
                    "The selected value is not supported.",
                });
            }
            continue;
        }

        if (definition.type == SettingType::Binding)
        {
            namespace input = kisak::vr::input;

            const input::ActionDefinition* const action =
                input::FindActionDefinition(definition.key);
            input::Binding binding;
            std::string bindingError;

            if (action == nullptr ||
                !input::ParseBinding(
                    action->action,
                    value,
                    &binding,
                    &bindingError))
            {
                messages.push_back({
                    ValidationMessage::Severity::Error,
                    definition.key,
                    bindingError.empty()
                        ? "The selected controller binding is not compatible with this action."
                        : bindingError,
                });
            }
            continue;
        }

        double number = 0.0;
        if (!ParseFiniteNumber(value, &number))
        {
            messages.push_back({
                ValidationMessage::Severity::Error,
                definition.key,
                "Enter a finite numeric value.",
            });
            continue;
        }

        if (definition.type == SettingType::Integer &&
            std::floor(number) != number)
        {
            messages.push_back({
                ValidationMessage::Severity::Error,
                definition.key,
                "Enter a whole number.",
            });
        }

        if (number < definition.minimumValue ||
            number > definition.maximumValue)
        {
            std::ostringstream range;
            range << "Enter a value from " << definition.minimumValue
                  << " through " << definition.maximumValue << ".";
            messages.push_back({
                ValidationMessage::Severity::Error,
                definition.key,
                range.str(),
            });
        }
    }

    const auto valueOf = [&](const char* key) -> std::string
    {
        const auto found = values.find(key);
        return found == values.end() ? std::string() : found->second;
    };

    if (valueOf("VR_CUSTOM_MODE") == "4768x2016" &&
        valueOf("KISAK_VR_OUTPUT_SCALE") != "0.75")
    {
        messages.push_back({
            ValidationMessage::Severity::Error,
            "KISAK_VR_OUTPUT_SCALE",
            "Performance packed mode requires an eye output scale of exactly 0.75.",
        });
    }

    if (valueOf("VR_CUSTOM_MODE") == "6016x2688" &&
        valueOf("KISAK_VR_OUTPUT_SCALE") != "1.00")
    {
        messages.push_back({
            ValidationMessage::Severity::Error,
            "KISAK_VR_OUTPUT_SCALE",
            "Native packed mode requires an eye output scale of exactly 1.00.",
        });
    }

    double beltDistance = 0.0;
    double beltRadius = 0.0;
    if (ParseFiniteNumber(valueOf("KISAK_VR_BELT_HIP_DISTANCE"), &beltDistance) &&
        ParseFiniteNumber(valueOf("KISAK_VR_BELT_GRAB_RADIUS"), &beltRadius) &&
        beltRadius >= beltDistance)
    {
        messages.push_back({
            ValidationMessage::Severity::Error,
            "KISAK_VR_BELT_GRAB_RADIUS",
            "Hip grab radius must be smaller than hip distance so left and right belt zones cannot overlap.",
        });
    }

    double dropSpeed = 0.0;
    double fullSpeed = 0.0;
    if (ParseFiniteNumber(valueOf("KISAK_VR_GRENADE_DROP_SPEED"), &dropSpeed) &&
        ParseFiniteNumber(valueOf("KISAK_VR_GRENADE_FULL_THROW_SPEED"), &fullSpeed) &&
        fullSpeed <= dropSpeed + 25.0)
    {
        messages.push_back({
            ValidationMessage::Severity::Error,
            "KISAK_VR_GRENADE_FULL_THROW_SPEED",
            "Full-strength hand speed must be at least 25 units above the deliberate-drop threshold.",
        });
    }

    double minimumGrenadeStrength = 0.0;
    double maximumGrenadeStrength = 0.0;
    if (ParseFiniteNumber(
            valueOf("KISAK_VR_GRENADE_MIN_STRENGTH"),
            &minimumGrenadeStrength) &&
        ParseFiniteNumber(
            valueOf("KISAK_VR_GRENADE_MAX_STRENGTH"),
            &maximumGrenadeStrength) &&
        maximumGrenadeStrength < minimumGrenadeStrength)
    {
        messages.push_back({
            ValidationMessage::Severity::Error,
            "KISAK_VR_GRENADE_MAX_STRENGTH",
            "Maximum grenade strength cannot be lower than minimum grenade strength.",
        });
    }

    namespace input = kisak::vr::input;
    std::map<std::string, std::vector<const input::ActionDefinition*>>
        gameplayAssignments;

    for (const input::ActionDefinition& action : input::ActionDefinitions())
    {
        if (!action.gameplayConflictGroup)
        {
            continue;
        }

        for (const char* const key :
             {action.settingKey, action.alternateSettingKey})
        {
            input::Binding binding;
            if (input::ParseBinding(
                    action.action,
                    valueOf(key),
                    &binding) &&
                binding.sourceCount != 0u)
            {
                std::sort(
                    binding.sources.begin(),
                    binding.sources.begin() + binding.sourceCount);
                gameplayAssignments[
                    input::BindingId(binding)].push_back(&action);
            }
        }
    }

    for (const auto& [bindingValue, actions] : gameplayAssignments)
    {
        if (actions.size() <= 1u)
        {
            continue;
        }

        std::ostringstream conflict;
        input::Binding binding;
        input::ParseBinding(
            actions.front()->action,
            bindingValue,
            &binding);
        conflict << input::BindingLabel(binding)
                 << " is assigned to multiple gameplay actions (";

        for (std::size_t index = 0u; index < actions.size(); ++index)
        {
            if (index != 0u)
            {
                conflict << ", ";
            }
            conflict << actions[index]->label;
        }
        conflict << "). This is allowed, but both actions will activate together.";

        messages.push_back({
            ValidationMessage::Severity::Warning,
            actions.front()->settingKey,
            conflict.str(),
        });
    }

    if (valueOf("KISAK_VR_FSR") == "1" &&
        valueOf("KISAK_VR_OUTPUT_SCALE") == "1.00")
    {
        messages.push_back({
            ValidationMessage::Severity::Warning,
            "KISAK_VR_FSR",
            "FSR is enabled at native output scale; it normally helps only below 1.00.",
        });
    }

    return messages;
}

LoadResult LoadSettings(
    const std::filesystem::path& defaultsPath,
    const std::filesystem::path& userPath)
{
    LoadResult result;
    result.values = BuiltInDefaults();
    result.activePath = defaultsPath;

    if (std::filesystem::is_regular_file(defaultsPath))
    {
        const std::string defaultsText = ReadTextFile(defaultsPath);
        const SettingsMap defaults =
            ParseBatchSettings(defaultsText, &result.messages);
        for (const auto& [key, value] : defaults)
        {
            result.values[key] = value;
        }

        const std::string profile = MetadataValue(defaultsText, "Profile");
        const std::string revision = MetadataValue(defaultsText, "Revision");
        if (!profile.empty())
        {
            result.profileName = profile;
        }
        result.revision = revision;
    }

    if (std::filesystem::is_regular_file(userPath))
    {
        result.userFileFound = true;
        result.activePath = userPath;
        const std::string userText = ReadTextFile(userPath);
        const SettingsMap overrides =
            ParseBatchSettings(userText, &result.messages);
        for (const auto& [key, value] : overrides)
        {
            result.values[key] = value;
        }

        const std::string profile = MetadataValue(userText, "Profile");
        const std::string revision = MetadataValue(userText, "Revision");
        result.profileName = profile.empty() ? "Custom" : profile;
        result.revision = revision;
    }

    UpgradeControllerBindings(&result.values);

    const std::vector<ValidationMessage> validation =
        ValidateSettings(result.values);
    result.messages.insert(
        result.messages.end(),
        validation.begin(),
        validation.end());

    return result;
}

std::string SerializeUserSettings(
    const SettingsMap& values,
    const std::string& profileName,
    const std::string& revision)
{
    const std::string safeProfile =
        SafeMetadataValue(profileName, "Custom");
    const std::string safeRevision =
        SafeMetadataValue(revision, "unspecified");

    std::ostringstream output;
    output << "@echo off\r\n";
    output << "rem KisakCOD VR user settings - generated by v0.10.0-beta.8 Configurator (Visual HUD, Input V4)\r\n";
    output << "rem Stored separately so extracting a future release cannot erase preferences.\r\n";
    output << "rem Profile: " << safeProfile << "\r\n";
    output << "rem Revision: " << safeRevision << "\r\n";
    output << "set \"KISAK_VR_SETTINGS_PROFILE=" << safeProfile << "\"\r\n";
    output << "set \"KISAK_VR_SETTINGS_REVISION=" << safeRevision << "\"\r\n\r\n";

    SettingPage previousPage = SettingPage::Quick;
    bool first = true;
    for (const SettingDefinition& definition : kCatalog)
    {
        if (!first && definition.page != previousPage)
        {
            output << "\r\n";
        }
        first = false;
        previousPage = definition.page;

        const auto found = values.find(definition.key);
        const std::string value =
            found == values.end() ? definition.defaultValue : found->second;
        output << "set \"" << definition.key << '=' << value << "\"\r\n";
    }

    return output.str();
}

VerificationResult VerifyUserSettingsFile(
    const std::filesystem::path& userPath,
    const SettingsMap& expectedValues,
    const std::string& expectedProfileName,
    const std::string& expectedRevision)
{
    VerificationResult result;
    result.profileName = SafeMetadataValue(expectedProfileName, "Custom");
    result.revision = SafeMetadataValue(expectedRevision, "unspecified");

    if (!std::filesystem::is_regular_file(userPath))
    {
        result.error = "The saved settings file does not exist after writing.";
        return result;
    }

    const std::string savedText = ReadTextFile(userPath);
    const std::string expectedText = SerializeUserSettings(
        expectedValues,
        result.profileName,
        result.revision);
    if (savedText != expectedText)
    {
        result.error = "The bytes read back from disk did not match the settings that were written.";
        return result;
    }

    std::vector<ValidationMessage> parseMessages;
    const SettingsMap parsed = ParseBatchSettings(savedText, &parseMessages);
    const auto parseError = std::find_if(
        parseMessages.begin(),
        parseMessages.end(),
        [](const ValidationMessage& message)
        {
            return message.severity == ValidationMessage::Severity::Error;
        });
    if (parseError != parseMessages.end())
    {
        result.error = "The saved file could not be parsed after writing: " +
            parseError->message;
        return result;
    }

    for (const SettingDefinition& definition : kCatalog)
    {
        const auto expected = expectedValues.find(definition.key);
        const std::string expectedValue = expected == expectedValues.end()
            ? definition.defaultValue
            : expected->second;
        const auto actual = parsed.find(definition.key);
        if (actual == parsed.end() || actual->second != expectedValue)
        {
            result.error = "Read-back verification failed for " +
                definition.key + ".";
            return result;
        }
        ++result.verifiedSettingCount;
    }

    if (MetadataValue(savedText, "Profile") != result.profileName ||
        MetadataValue(savedText, "Revision") != result.revision)
    {
        result.error = "The saved profile metadata did not survive read-back.";
        return result;
    }

    const std::vector<ValidationMessage> validation =
        ValidateSettings(parsed);
    const auto validationError = std::find_if(
        validation.begin(),
        validation.end(),
        [](const ValidationMessage& message)
        {
            return message.severity == ValidationMessage::Severity::Error;
        });
    if (validationError != validation.end())
    {
        result.error = "The saved file failed validation after read-back: " +
            validationError->message;
        return result;
    }

    result.success = true;
    return result;
}

SaveResult SaveUserSettingsAtomic(
    const std::filesystem::path& userPath,
    const SettingsMap& values,
    const std::string& profileName)
{
    SaveResult result;
    result.settingsPath = userPath;
    result.profileName = SafeMetadataValue(profileName, "Custom");
    result.revision = BuildSettingsRevision(values);
    result.savedAt = TimestampForDisplay();

    const std::vector<ValidationMessage> validation = ValidateSettings(values);
    const auto error = std::find_if(
        validation.begin(),
        validation.end(),
        [](const ValidationMessage& message)
        {
            return message.severity == ValidationMessage::Severity::Error;
        });

    if (error != validation.end())
    {
        result.error = error->message;
        return result;
    }

    std::error_code filesystemError;
    std::filesystem::create_directories(userPath.parent_path(), filesystemError);
    if (filesystemError)
    {
        result.error = "Could not create the settings directory: " +
            filesystemError.message();
        return result;
    }

    if (std::filesystem::is_regular_file(userPath))
    {
        const std::filesystem::path backupDirectory =
            userPath.parent_path() / "Backups";
        std::filesystem::create_directories(backupDirectory, filesystemError);
        if (filesystemError)
        {
            result.error = "Could not create the backup directory: " +
                filesystemError.message();
            return result;
        }

        result.backupPath = backupDirectory /
            ("VR-User-Settings-" + TimestampForFileName() + ".bat");
        std::filesystem::copy_file(
            userPath,
            result.backupPath,
            std::filesystem::copy_options::overwrite_existing,
            filesystemError);
        if (filesystemError)
        {
            result.error = "Could not back up the previous settings: " +
                filesystemError.message();
            return result;
        }
    }

    const std::filesystem::path temporaryPath =
        userPath.parent_path() /
        (userPath.filename().string() + ".new-" + result.revision);
    const std::string serialized = SerializeUserSettings(
        values,
        result.profileName,
        result.revision);
    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            result.error = "Could not create the temporary settings file.";
            return result;
        }

        stream.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        stream.flush();
        if (!stream)
        {
            result.error = "Could not finish writing the temporary settings file.";
            return result;
        }
    }

    std::filesystem::rename(temporaryPath, userPath, filesystemError);
    if (filesystemError)
    {
        // Windows rename cannot replace an existing destination. The old file
        // has already been backed up, so replace it explicitly and recover the
        // backup if the second rename fails.
        filesystemError.clear();
        std::filesystem::remove(userPath, filesystemError);
        if (!filesystemError)
        {
            std::filesystem::rename(temporaryPath, userPath, filesystemError);
        }

        if (filesystemError)
        {
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
            if (!result.backupPath.empty())
            {
                std::filesystem::copy_file(
                    result.backupPath,
                    userPath,
                    std::filesystem::copy_options::overwrite_existing,
                    cleanupError);
            }
            result.error = "Could not replace the active settings file: " +
                filesystemError.message();
            return result;
        }
    }

    const VerificationResult verification = VerifyUserSettingsFile(
        userPath,
        values,
        result.profileName,
        result.revision);
    if (!verification.success)
    {
        result.error = "Settings were written but failed mandatory read-back verification: " +
            verification.error;
        RestoreAfterVerificationFailure(
            userPath,
            result.backupPath,
            &result.error);
        return result;
    }

    result.readBackVerified = true;
    result.verifiedSettingCount = verification.verifiedSettingCount;
    result.success = true;
    return result;
}

bool ApplyPreset(
    const std::string& presetName,
    SettingsMap* values)
{
    if (values == nullptr)
    {
        return false;
    }

    if (presetName == "Tested Quest 3")
    {
        *values = BuiltInDefaults();
        return true;
    }

    if (presetName == "Performance")
    {
        *values = BuiltInDefaults();
        Set(values, "VR_CUSTOM_MODE", "4768x2016");
        Set(values, "KISAK_VR_OUTPUT_SCALE", "0.75");
        Set(values, "KISAK_VR_FSR", "1");
        Set(values, "KISAK_VR_FSR_SHARPNESS", "0.60");
        Set(values, "KISAK_VR_SCOPE_CAPTURE_SIZE", "768");
        return true;
    }

    if (presetName == "Comfort Snap")
    {
        Set(values, "KISAK_VR_TURN_MODE", "snap");
        Set(values, "KISAK_VR_SNAP_TURN_ANGLE", "30");
        Set(values, "KISAK_VR_TURN_DEADZONE", "0.30");
        Set(values, "KISAK_VR_CAMERA_SHAKE", "0");
        Set(values, "KISAK_VR_WEAPON_BOB_AMPLITUDE", "0.00");
        return true;
    }

    if (presetName == "Smooth Turn")
    {
        Set(values, "KISAK_VR_TURN_MODE", "smooth");
        Set(values, "KISAK_VR_SMOOTH_TURN_SPEED", "120");
        Set(values, "KISAK_VR_TURN_DEADZONE", "0.25");
        return true;
    }

    if (presetName == "Seated")
    {
        Set(values, "KISAK_VR_PLAY_MODE", "seated");
        Set(values, "KISAK_VR_TURN_MODE", "snap");
        Set(values, "KISAK_VR_SNAP_TURN_ANGLE", "30");
        Set(values, "KISAK_VR_BELT_HEIGHT", "-20.0");
        Set(values, "KISAK_VR_BELT_FORWARD_OFFSET", "4.0");
        Set(values, "KISAK_VR_CAMERA_SHAKE", "0");
        Set(values, "KISAK_VR_WEAPON_BOB_AMPLITUDE", "0.00");
        return true;
    }

    if (presetName == "Minimal HUD")
    {
        Set(values, "KISAK_VR_COMPASS_ENABLED", "0");
        Set(values, "KISAK_VR_CROSSHAIR", "0");
        Set(values, "KISAK_VR_HUD_SAFE_X", "0.65");
        Set(values, "KISAK_VR_HUD_SAFE_Y", "0.80");
        Set(values, "KISAK_VR_HUD_BOTTOM_LEFT_SCALE", "0.50");
        return true;
    }

    return false;
}

std::vector<std::string> PresetNames()
{
    return {
        "Tested Quest 3",
        "Performance",
        "Comfort Snap",
        "Smooth Turn",
        "Seated",
        "Minimal HUD",
    };
}

} // namespace kisak::configurator
