#include "../tools/configurator/settings_core.h"
#include "vr/vr_hud_layout.h"
#include "vr/vr_gestures.h"
#include "vr/vr_input_bindings.h"
#include "vr/vr_interactions.h"
#include "vr/vr_calibration.h"
#include "vr/vr_compatibility.h"
#include "vr/vr_openvr_input.h"
#include "vr/vr_openxr_profiles.h"
#include "vr/vr_prompt_labels.h"
#include "vr/vr_weapon_calibration.h"
#include "vr/vr_weapon_profiles.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

namespace kc = kisak::configurator;
namespace vg = kisak::vr::gestures;
namespace vi = kisak::vr::input;
namespace vint = kisak::vr::interactions;
namespace vc = kisak::vr::calibration;
namespace vrc = kisak::vr::compatibility;
namespace vh = kisak::vr::hud;
namespace vp = kisak::vr::prompts;
namespace vwp = kisak::vr::weapon_profiles;

namespace
{

int failures = 0;

void Check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool HasError(
    const std::vector<kc::ValidationMessage>& messages,
    const std::string& key)
{
    for (const kc::ValidationMessage& message : messages)
    {
        if (message.severity == kc::ValidationMessage::Severity::Error &&
            message.key == key)
        {
            return true;
        }
    }
    return false;
}

bool HasWarning(
    const std::vector<kc::ValidationMessage>& messages,
    const std::string& key)
{
    for (const kc::ValidationMessage& message : messages)
    {
        if (message.severity == kc::ValidationMessage::Severity::Warning &&
            message.key == key)
        {
            return true;
        }
    }
    return false;
}

bool NearlyEqual(const float left, const float right)
{
    return std::abs(left - right) < 0.001f;
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::size_t CountOccurrences(
    const std::string& text,
    const std::string& needle)
{
    if (needle.empty())
    {
        return 0u;
    }

    std::size_t count = 0u;
    std::size_t position = 0u;
    while ((position = text.find(needle, position)) !=
           std::string::npos)
    {
        ++count;
        position += needle.size();
    }
    return count;
}

} // namespace

int main(const int argumentCount, char** arguments)
{
    {
        const vg::HeadLocalPosition crown = {
            -0.15f,
            0.17f,
            -0.02f,
        };
        const vg::HeadLocalPosition visor = {
            -0.14f,
            -0.04f,
            -0.14f,
        };
        const vg::HeadLocalPosition ordinaryGrip = {
            -0.30f,
            -0.40f,
            -0.25f,
        };
        // This is intentionally inside V80's forgiving visor destination
        // box, but represents a normal foregrip below and farther forward
        // than a hand touching the visor.
        const vg::HeadLocalPosition rifleForegrip = {
            -0.20f,
            -0.16f,
            -0.30f,
        };

        Check(
            vg::IsNightVisionCrownZone(crown) &&
                !vg::IsNightVisionVisorZone(crown) &&
                vg::IsNightVisionVisorZone(visor) &&
                vg::IsNightVisionVisorStartZone(visor) &&
                !vg::IsNightVisionCrownZone(visor) &&
                vg::IsNightVisionVisorZone(rifleForegrip) &&
                !vg::IsNightVisionVisorStartZone(rifleForegrip),
            "V81 should keep visor arming close to the face while retaining a forgiving lowering destination");

        vg::NightVisionVisorState visorState;
        vg::NightVisionVisorUpdate visorUpdate =
            vg::UpdateNightVisionVisorGesture(
                &visorState,
                true,
                true,
                true,
                true,
                ordinaryGrip,
                1000u);
        Check(
            !visorUpdate.armedThisFrame &&
                !visorUpdate.consumeLeftGrip,
            "V80 should not steal an ordinary left-grip press away from the weapon interactions");

        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            true,
            true,
            crown,
            1050u);
        Check(
            !visorUpdate.armedThisFrame,
            "V80 should require grip release before arming after a press that began outside a gesture zone");

        vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            false,
            true,
            crown,
            1100u);

        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            true,
            true,
            rifleForegrip,
            1120u);
        Check(
            !visorUpdate.armedThisFrame &&
                !visorUpdate.consumeLeftGrip &&
                visorState.direction ==
                    vg::NightVisionVisorDirection::None,
            "V81 should not reserve left grip when a new press begins at the normal rifle foregrip");

        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            true,
            true,
            visor,
            1130u);
        Check(
            !visorUpdate.armedThisFrame &&
                !visorUpdate.consumeLeftGrip,
            "V81 should not convert a rifle grip into a visor gesture after the held hand moves toward the face");

        vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            false,
            true,
            visor,
            1140u);

        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            true,
            true,
            crown,
            1150u);
        Check(
            visorUpdate.armedThisFrame &&
                visorUpdate.consumeLeftGrip &&
                visorState.direction ==
                    vg::NightVisionVisorDirection::Lower,
            "V80 should arm a visor-lowering gesture only on a new crown-zone grip press");

        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            true,
            true,
            visor,
            1400u);
        Check(
            !visorUpdate.toggledThisFrame &&
                visorUpdate.consumeLeftGrip &&
                visorState.destinationReached,
            "V80 should consume the left grip while the visor is pulled down without toggling before release");

        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            false,
            true,
            visor,
            1450u);
        Check(
            visorUpdate.toggledThisFrame &&
                visorUpdate.completedDirection ==
                    vg::NightVisionVisorDirection::Lower &&
                !visorUpdate.consumeLeftGrip,
            "V80 should emit exactly one night-vision toggle when a completed downward visor pull is released");

        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            false,
            true,
            visor,
            1500u);
        Check(
            !visorUpdate.toggledThisFrame,
            "V80 should not repeat the night-vision toggle after the release frame");

        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            true,
            true,
            visor,
            2000u);
        Check(
            visorUpdate.armedThisFrame &&
                visorState.direction ==
                    vg::NightVisionVisorDirection::Raise,
            "V80 should arm a visor-raising gesture from the front-of-face zone");

        vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            true,
            true,
            crown,
            2250u);
        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            false,
            true,
            crown,
            2300u);
        Check(
            visorUpdate.toggledThisFrame &&
                visorUpdate.completedDirection ==
                    vg::NightVisionVisorDirection::Raise,
            "V80 should toggle once when the user grips the visor and pulls it up to the crown");

        const vg::HeadLocalPosition shortPull = {
            -0.15f,
            0.10f,
            -0.10f,
        };
        vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            true,
            true,
            crown,
            3000u);
        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            false,
            true,
            shortPull,
            3150u);
        Check(
            !visorUpdate.toggledThisFrame &&
                visorUpdate.cancelledThisFrame,
            "V80 should reject a short head-area grip motion that does not complete the 12 cm visor travel");

        vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            true,
            true,
            crown,
            4000u);
        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            false,
            true,
            true,
            true,
            crown,
            4100u);
        Check(
            visorUpdate.cancelledThisFrame &&
                visorUpdate.consumeLeftGrip,
            "V80 should cancel when a menu opens but keep consuming the in-progress grip until release");
        visorUpdate = vg::UpdateNightVisionVisorGesture(
            &visorState,
            true,
            true,
            false,
            true,
            visor,
            4150u);
        Check(
            !visorUpdate.toggledThisFrame &&
                !visorUpdate.consumeLeftGrip,
            "V80 should require a completely new grip after menu or tracking cancellation");
    }

    {
        vi::Binding guardedBinding;
        vi::Binding unrelatedBinding;
        Check(
            vi::ParseBinding(
                vi::Action::NightVision,
                "right.thumbrest_touch+left.primary_axis.down",
                &guardedBinding) &&
                vi::UsesOpenVrMissionSelector(guardedBinding),
            "V79 should guard the default OpenVR mission-selector chord");
        Check(
            vi::ParseBinding(
                vi::Action::NightVision,
                "right.thumbrest_touch+right.primary_axis.down",
                &unrelatedBinding) &&
                !vi::UsesOpenVrMissionSelector(unrelatedBinding),
            "V79 should not rewrite a custom non-left-axis chord");

        const vi::OpenVrVector2 neutral = {};
        const vi::OpenVrVector2 leftUp = {0.0f, 0.9f};
        const vi::OpenVrVector2 rightTurn = {0.8f, 0.0f};
        vi::OpenVrMissionSelectorState selector;

        vi::OpenVrMissionSelectorUpdate update =
            vi::UpdateOpenVrMissionSelector(
                &selector,
                true,
                true,
                leftUp,
                true,
                neutral,
                true);
        Check(
            !update.modifierHeld && !selector.armed,
            "V79 should not arm while the movement stick is already deflected");

        update = vi::UpdateOpenVrMissionSelector(
            &selector,
            true,
            true,
            neutral,
            true,
            neutral,
            true);
        Check(
            !update.modifierHeld && !selector.armed,
            "V79 should require release and a new centered contact after walking");

        vi::UpdateOpenVrMissionSelector(
            &selector,
            true,
            false,
            neutral,
            true,
            neutral,
            true);
        update = vi::UpdateOpenVrMissionSelector(
            &selector,
            true,
            true,
            neutral,
            true,
            neutral,
            true);
        Check(
            update.available && update.modifierHeld &&
                update.armedThisFrame && selector.armed,
            "V79 should arm when right-stick contact begins with both sticks centered");

        update = vi::UpdateOpenVrMissionSelector(
            &selector,
            true,
            true,
            leftUp,
            true,
            neutral,
            true);
        Check(
            update.modifierHeld && selector.armed,
            "V79 should keep the modifier armed while the left stick selects a mission action");

        update = vi::UpdateOpenVrMissionSelector(
            &selector,
            true,
            true,
            neutral,
            true,
            rightTurn,
            true);
        Check(
            !update.modifierHeld && update.cancelledThisFrame &&
                !selector.armed,
            "V79 should cancel mission selection as soon as the right stick turns");

        update = vi::UpdateOpenVrMissionSelector(
            &selector,
            true,
            true,
            neutral,
            true,
            neutral,
            true);
        Check(
            !update.modifierHeld && !selector.armed,
            "V79 should not rearm after turning until right-stick contact is released");
    }

    {
        const vi::ActionDefinition* useAction =
            vp::FindPromptAction("  +ACTIVATE  ");
        Check(
            useAction != nullptr &&
                useAction->action == vi::Action::Use &&
                vp::FindPromptAction("+reload")->action ==
                    vi::Action::Reload &&
                vp::FindPromptAction("+gostand")->action ==
                    vi::Action::Jump &&
                vp::FindPromptAction("+moveup")->action ==
                    vi::Action::Jump &&
                vp::FindPromptAction("+melee")->action ==
                    vi::Action::Melee &&
                vp::FindPromptAction("+forward")->action ==
                    vi::Action::Move &&
                vp::FindPromptAction("+right")->action ==
                    vi::Action::Turn,
            "V71 should map common COD4 prompt commands to semantic VR actions");
        Check(
            vp::FindPromptAction("+usereload") == nullptr &&
                vp::FindPromptAction("+melee_breath") == nullptr &&
                vp::FindPromptAction("weapprev") == nullptr &&
                vp::FindPromptAction("totally_unknown") == nullptr,
            "V71 should leave ambiguous and unknown commands on COD4's keyboard fallback");

        const std::array<std::string_view, 2> touchProfiles = {{
            "/interaction_profiles/meta/touch_controller_plus",
            "/interaction_profiles/meta/touch_controller_plus",
        }};

        std::array<vi::Binding, 2> useBindings = {};
        Check(
            vi::ParseBinding(
                vi::Action::Use,
                "left.primary",
                &useBindings[0]) &&
                vi::ParseBinding(
                    vi::Action::Use,
                    "left.trigger",
                    &useBindings[1]),
            "V71 prompt fixtures should parse normal primary/alternate bindings");
        vp::BindingLabels labels = vp::BuildBindingLabels(
            useBindings,
            touchProfiles,
            vp::Backend::OpenXr);
        Check(
            labels.count == 2u &&
                labels.values[0] == "X" &&
                labels.values[1] == "Left trigger",
            "V71 should display both actual Touch bindings instead of the PC key");

        std::array<vi::Binding, 2> remappedUse = {};
        Check(
            vi::ParseBinding(
                vi::Action::Use,
                "right.secondary",
                &remappedUse[0]),
            "V71 prompt fixtures should accept a remapped Use action");
        labels = vp::BuildBindingLabels(
            remappedUse,
            touchProfiles,
            vp::Backend::OpenXr);
        Check(
            labels.count == 1u && labels.values[0] == "B",
            "V71 should read the user's configured VR binding instead of assuming Quest X");

        std::array<vi::Binding, 2> jumpBindings = {};
        Check(
            vi::ParseBinding(
                vi::Action::Jump,
                "right.primary_axis.up",
                &jumpBindings[0]) &&
                vi::ParseBinding(
                    vi::Action::Jump,
                    "left.trigger",
                    &jumpBindings[1]),
            "V71 prompt fixtures should parse directional Jump alternatives");
        labels = vp::BuildBindingLabels(
            jumpBindings,
            touchProfiles,
            vp::Backend::OpenXr);
        Check(
            labels.count == 2u &&
                labels.values[0] == "Right stick up" &&
                labels.values[1] == "Left trigger",
            "V71 mantle prompts should expose both configured Jump bindings");

        std::array<vi::Binding, 2> moveBindings = {};
        Check(
            vi::ParseBinding(
                vi::Action::Move,
                "left.primary_axis",
                &moveBindings[0]),
            "V71 prompt fixtures should parse the configured movement axis");
        labels = vp::BuildBindingLabels(
            moveBindings,
            touchProfiles,
            vp::Backend::OpenXr,
            "+forward");
        Check(
            labels.count == 1u &&
                labels.values[0] == "Left stick up",
            "V71 should turn keyboard movement prompts into directional VR-axis text");

        std::array<vi::Binding, 2> duplicateBindings = {};
        Check(
            vi::ParseBinding(
                vi::Action::Use,
                "left.primary",
                &duplicateBindings[0]) &&
                vi::ParseBinding(
                    vi::Action::Use,
                    "left.primary",
                    &duplicateBindings[1]),
            "V71 prompt fixtures should permit intentional duplicate slots");
        labels = vp::BuildBindingLabels(
            duplicateBindings,
            touchProfiles,
            vp::Backend::OpenXr);
        Check(
            labels.count == 1u && labels.values[0] == "X",
            "V71 should collapse duplicate primary and alternate prompt text");

        Check(
            vp::SourcePromptLabel(
                vi::Source::RightPrimary,
                "/interaction_profiles/valve/index_controller",
                vp::Backend::OpenXr) == "Right A" &&
                vp::SourcePromptLabel(
                    vi::Source::RightPrimary,
                    "right knuckles [/input/knuckles_profile.json]",
                    vp::Backend::OpenVr) == "Right grip",
            "V71 should describe the different OpenXR and legacy OpenVR Index primary routes accurately");
        Check(
            vp::SourcePromptLabel(
                vi::Source::LeftPrimary,
                "/interaction_profiles/htc/vive_controller",
                vp::Backend::OpenXr) == "Left trackpad press" &&
                vp::SourcePromptLabel(
                    vi::Source::LeftSecondary,
                    "/interaction_profiles/microsoft/motion_controller",
                    vp::Backend::OpenXr) == "Left stick click",
            "V71 should provide profile-aware Vive and Mixed Reality text labels");
        Check(
            vp::SourcePromptLabel(
                vi::Source::LeftPrimary,
                "/interaction_profiles/hp/mixed_reality_controller",
                vp::Backend::OpenXr) == "X",
            "V71 should match the HP profile's Touch-family OpenXR component mapping");

        std::array<vi::Binding, 2> chordBindings = {};
        Check(
            vi::ParseBinding(
                vi::Action::NightVision,
                "right.thumbrest_touch+left.primary_axis.down",
                &chordBindings[0]),
            "V71 prompt fixtures should parse cross-hand input chords");
        labels = vp::BuildBindingLabels(
            chordBindings,
            touchProfiles,
            vp::Backend::OpenXr);
        Check(
            labels.count == 1u &&
                labels.values[0] ==
                    "Right thumbrest + Left stick down",
            "V71 should preserve every term of a configured prompt chord");

        const std::array<std::string_view, 2> unknownProfiles = {{
            "",
            "unknown experimental controller",
        }};
        labels = vp::BuildBindingLabels(
            remappedUse,
            unknownProfiles,
            vp::Backend::OpenXr);
        Check(
            labels.count == 1u &&
                labels.values[0] == "Right secondary",
            "V71 should fall back to controller-neutral text when identity is uncertain");

        const std::array<vi::Binding, 2> unboundBindings = {};
        labels = vp::BuildBindingLabels(
            unboundBindings,
            touchProfiles,
            vp::Backend::OpenXr);
        Check(
            labels.count == 0u,
            "V71 should let unbound VR actions fall back to keyboard labels");
    }

    {
        Check(
            vint::WeaponControllerIndex(vint::DominantHand::Right) == 1u &&
                vint::OffHandControllerIndex(vint::DominantHand::Right) == 0u &&
                vint::WeaponControllerIndex(vint::DominantHand::Left) == 0u &&
                vint::OffHandControllerIndex(vint::DominantHand::Left) == 1u,
            "V64 handedness should swap the actual weapon/off-hand controller roles");
        Check(
            vint::MirrorBindingHands(
                "right.thumbrest_touch+left.primary_axis.up") ==
                "left.thumbrest_touch+right.primary_axis.up" &&
                vint::MirrorBindingHands("unbound") == "unbound",
            "V64 should mirror every physical hand term while preserving chords and unbound slots");
        Check(
            NearlyEqual(
                vint::MagazineHipCenter(
                    vint::DominantHand::Right,
                    vint::MagazineHip::OffHand,
                    13.0f),
                13.0f) &&
                NearlyEqual(
                    vint::MagazineHipCenter(
                        vint::DominantHand::Left,
                        vint::MagazineHip::OffHand,
                        13.0f),
                    -13.0f) &&
                vint::FragUsesLeftHip(
                    vint::DominantHand::Right,
                    vint::GrenadeBeltLayout::Handed) &&
                !vint::FragUsesLeftHip(
                    vint::DominantHand::Left,
                    vint::GrenadeBeltLayout::Handed),
            "V64 handed belt layouts should place magazine and grenade zones on the intended physical side");

        const float forwardThrust[3] = {120.0f, 10.0f, 0.0f};
        const float sidewaysSwing[3] = {20.0f, 120.0f, 0.0f};
        const float weaponForward[3] = {1.0f, 0.0f, 0.0f};
        Check(
            vint::MeleeGestureQualifies(
                forwardThrust,
                weaponForward,
                95.0f,
                0.55f) &&
                !vint::MeleeGestureQualifies(
                    sidewaysSwing,
                    weaponForward,
                    95.0f,
                    0.55f),
            "V64 physical melee should accept deliberate forward thrusts and reject sideways controller motion");
        Check(
            NearlyEqual(
                vint::EffectiveHapticAmplitude(true, 0.8f, 1.5f),
                1.0f) &&
                NearlyEqual(
                    vint::EffectiveHapticAmplitude(false, 0.8f, 1.0f),
                    0.0f),
            "V64 haptic scaling should clamp safely and honor the accessibility disable switch");
        Check(
            vint::SupportGripProximityQualifies(
                14.0f,
                14.0f,
                false) &&
                !vint::SupportGripProximityQualifies(
                    14.01f,
                    14.0f,
                    false) &&
                vint::SupportGripProximityQualifies(
                    18.0f,
                    14.0f,
                    true) &&
                !vint::SupportGripProximityQualifies(
                    18.01f,
                    14.0f,
                    true) &&
                !vint::SupportGripProximityQualifies(
                    -1.0f,
                    14.0f,
                    true),
            "issue #26 V67 automatic proximity should enter at the configured radius and release four units farther out");
        Check(
            vint::WeaponRequiresPoseIndependentAttack(
                true,
                true) &&
                !vint::WeaponRequiresPoseIndependentAttack(
                    true,
                    false) &&
                !vint::WeaponRequiresPoseIndependentAttack(
                    false,
                    true) &&
                !vint::WeaponRequiresPoseIndependentAttack(
                    false,
                    false),
            "issue #18 V68 must bypass weapon-pose gating only for grenade-class weapons with hasDetonator");
    }

    {
        const float controllerAxisCameraLocal[3][3] = {
            {0.0f, 1.0f, 0.0f},
            {-1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
        const float requestedOffset[3] = {3.0f, -2.0f, 1.0f};
        float cameraLocalDelta[3] = {};

        kisak::vr::weapon_calibration::
            ControllerLocalOffsetToCameraLocal(
                controllerAxisCameraLocal,
                requestedOffset,
                cameraLocalDelta);

        Check(
            cameraLocalDelta[0] == 2.0f &&
                cameraLocalDelta[1] == 3.0f &&
                cameraLocalDelta[2] == 1.0f,
            "weapon offsets should follow controller-local forward/left/up axes");

        const float cameraOrigin[3] = {100.0f, 200.0f, 300.0f};
        const float cameraAxis[3][3] = {
            {0.0f, 1.0f, 0.0f},
            {-1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
        const float gripCameraLocal[3] = {10.0f, 20.0f, 30.0f};
        float calibratedTargetWorld[3] = {};

        kisak::vr::weapon_calibration::CalibratedGripTargetWorld(
            cameraOrigin,
            cameraAxis,
            gripCameraLocal,
            cameraLocalDelta,
            calibratedTargetWorld);

        Check(
            calibratedTargetWorld[0] == 77.0f &&
                calibratedTargetWorld[1] == 212.0f &&
                calibratedTargetWorld[2] == 331.0f,
            "final grip target should preserve the configured weapon translation");
    }

    {
        vc::Request requested;
        requested.requestId = "cfg-test-123";
        requested.command = vc::Command::MeasureStanding;
        requested.playMode = vc::PlayMode::Standing;
        requested.targetEyeHeightInches = 67.5f;

        vc::Request parsed;
        std::string parseError;
        Check(
            vc::ParseRequest(
                vc::SerializeRequest(requested),
                &parsed,
                &parseError) &&
                parsed.requestId == requested.requestId &&
                parsed.command == requested.command &&
                parsed.playMode == requested.playMode &&
                parsed.targetEyeHeightInches == 67.5f,
            "V60 calibration requests should round-trip through the shared protocol");

        for (const vc::Command command : {
                 vc::Command::RecenterPosition,
                 vc::Command::RecenterDirectionLevel,
                 vc::Command::RecenterFull})
        {
            requested.command = command;
            Check(
                vc::ParseRequest(
                    vc::SerializeRequest(requested),
                    &parsed,
                    &parseError) &&
                    parsed.command == command,
                "V70 separated recenter commands should round-trip without changing meaning");
        }
        Check(
            vc::ParseRequest(
                "VERSION=1\nREQUEST_ID=legacy-full\nCOMMAND=recenter\n"
                "PLAY_MODE=standing\nTARGET_EYE_HEIGHT_INCHES=60\n",
                &parsed,
                &parseError) &&
                parsed.command == vc::Command::RecenterFull,
            "V70 should treat beta.8-beta.10 recenter requests as full recenter requests");

        vc::RecenterMode mode = vc::RecenterMode::Disabled;
        Check(
            vc::ParseRecenterMode("position_only", &mode) &&
                mode == vc::RecenterMode::PositionOnly &&
                vc::CommandRecenterMode(
                    vc::Command::RecenterPosition) == mode &&
                std::string(vc::RecenterModeId(mode)) ==
                    "position_only",
            "V70 position-only recenter should have one stable settings/protocol identity");
        Check(
            vc::ParseRecenterMode("direction_level_only", &mode) &&
                mode == vc::RecenterMode::DirectionLevelOnly &&
                vc::CommandRecenterMode(
                    vc::Command::RecenterDirectionLevel) == mode,
            "V70 direction/level-only recenter should preserve the positional origin");
        Check(
            vc::ParseRecenterMode("1", &mode) &&
                mode == vc::RecenterMode::Full &&
                vc::ParseRecenterMode("0", &mode) &&
                mode == vc::RecenterMode::Disabled &&
                vc::CommandRecenterMode(
                    vc::Command::MeasureStanding) ==
                    vc::RecenterMode::Disabled &&
                vc::CommandRecenterMode(
                    vc::Command::ApplyHeight) ==
                    vc::RecenterMode::Disabled,
            "V70 should preserve beta.10 first-gameplay 0/1 profile behavior");
        Check(
            !vc::ParseRequest(
                "VERSION=1\nREQUEST_ID=bad&id\nCOMMAND=recenter\n"
                "PLAY_MODE=standing\nTARGET_EYE_HEIGHT_INCHES=60\n",
                &parsed,
                &parseError),
            "V60 should reject unsafe calibration request identifiers");
        Check(
            !vc::ParseRequest(
                "VERSION=1\nREQUEST_ID=too-short\nCOMMAND=apply_height\n"
                "PLAY_MODE=seated\nTARGET_EYE_HEIGHT_INCHES=41.9\n",
                &parsed,
                &parseError) &&
                !vc::ParseRequest(
                    "VERSION=1\nREQUEST_ID=too-tall\nCOMMAND=apply_height\n"
                    "PLAY_MODE=standing\nTARGET_EYE_HEIGHT_INCHES=84.1\n",
                    &parsed,
                    &parseError),
            "V60 should reject calibration heights outside the guarded 42-84 inch range");
        Check(
            vc::EyeHeightCorrectionInches(67.5f) == 7.5f &&
                vc::EyeHeightCorrectionInches(60.0f) == 0.0f,
            "height correction should be relative to COD4's native 60-inch standing camera");
    }

    {
        vwp::Document profiles = vwp::DefaultDocument();
        profiles.gunstocks.front().shouldered.offset = {1.0f, 2.0f, 3.0f};
        profiles.gunstocks.front().shouldered.angles = {4.0f, 5.0f, 6.0f};
        vwp::WeaponProfile mp5;
        mp5.id = "mp5";
        mp5.name = "MP5";
        mp5.hip.offset = {0.5f, -0.5f, 1.0f};
        mp5.hip.angles = {1.0f, -2.0f, 3.0f};
        mp5.shouldered.offset = {2.0f, 0.0f, -1.0f};
        mp5.shouldered.angles = {10.0f, 0.0f, -4.0f};
        profiles.weapons.push_back(mp5);

        vwp::Document parsed;
        std::string profileError;
        const std::string serialized = vwp::SerializeDocument(profiles);
        Check(
            vwp::ParseDocument(serialized, &parsed, &profileError) &&
                vwp::SerializeDocument(parsed) == serialized &&
                vwp::DocumentRevision(parsed) ==
                    vwp::DocumentRevision(profiles),
            "V63 weapon/gunstock profiles should round-trip deterministically");

        vwp::Pose global;
        global.offset = {3.0f, 0.0f, 0.0f};
        const vwp::EffectiveCalibration hip =
            vwp::Resolve(parsed, true, global, "MP5", 0.0f);
        const vwp::EffectiveCalibration shoulder =
            vwp::Resolve(parsed, true, global, "mp5", 1.0f);
        const vwp::EffectiveCalibration halfway =
            vwp::Resolve(parsed, true, global, "mp5", 0.5f);
        Check(
            NearlyEqual(hip.pose.offset[0], 3.5f) &&
                NearlyEqual(hip.pose.offset[1], -0.5f) &&
                NearlyEqual(hip.pose.angles[0], 1.0f) &&
                hip.weaponOverrideApplied && !hip.gunstockApplied,
            "V63 hip calibration should layer the weapon override over the global baseline");
        Check(
            NearlyEqual(shoulder.pose.offset[0], 6.5f) &&
                NearlyEqual(shoulder.pose.offset[1], 1.5f) &&
                NearlyEqual(shoulder.pose.offset[2], 3.0f) &&
                NearlyEqual(shoulder.pose.angles[0], 15.0f) &&
                shoulder.weaponOverrideApplied && shoulder.gunstockApplied,
            "V63 shouldered calibration should add gunstock and per-weapon shoulder deltas");
        Check(
            NearlyEqual(halfway.pose.offset[0], 5.0f) &&
                NearlyEqual(halfway.pose.angles[0], 8.0f),
            "V63 shoulder calibration should blend smoothly instead of snapping");
        Check(
            NearlyEqual(
                vwp::Resolve(parsed, false, global, "mp5", 1.0f)
                    .pose.offset[0],
                3.0f),
            "disabling V63 profiles should preserve the proven global baseline alone");

        vwp::GunstockProfile importedStock;
        Check(
            vwp::ParseGunstock(
                vwp::SerializeGunstock(profiles.gunstocks.front()),
                &importedStock,
                &profileError) &&
                importedStock.id == "generic" &&
                importedStock.shouldered.offset[2] == 3.0f,
            "V63 shareable .vrstock profiles should round-trip independently");
        Check(
            !vwp::ParseDocument(
                "VERSION=1\nACTIVE_GUNSTOCK=bad&id\n",
                &parsed,
                &profileError),
            "V63 should reject unsafe profile identifiers");

        vwp::Request request;
        request.requestId = "weapon-capture-123";
        request.command = vwp::Command::CaptureAim;
        request.target = vwp::CaptureTarget::WeaponShouldered;
        request.weaponId = "mp5";
        request.gunstockId = "generic";
        vwp::Request parsedRequest;
        Check(
            vwp::ParseRequest(
                vwp::SerializeRequest(request),
                &parsedRequest,
                &profileError) &&
                parsedRequest.requestId == request.requestId &&
                parsedRequest.command == request.command &&
                parsedRequest.target == request.target,
            "V63 live reload/capture requests should round-trip through one shared protocol");
        Check(
            !vwp::ParseRequest(
                "VERSION=1\nREQUEST_ID=weapon-capture-123\n"
                "COMMAND=capture_aim\nTARGET=surprise\n"
                "WEAPON_ID=mp5\nGUNSTOCK_ID=generic\n",
                &parsedRequest,
                &profileError),
            "V63 should reject unknown live-capture targets");

        vwp::RuntimeStatus runtimeStatus;
        runtimeStatus.status = "captured";
        runtimeStatus.requestId = request.requestId;
        runtimeStatus.weaponIndex = 19;
        runtimeStatus.weaponId = "mp5";
        runtimeStatus.weaponName = "MP5";
        runtimeStatus.activeGunstockId = "generic";
        runtimeStatus.profileRevision = vwp::DocumentRevision(profiles);
        runtimeStatus.effective = shoulder;
        runtimeStatus.capturedAnglesValid = true;
        runtimeStatus.capturedEffectiveAngles = {11.0f, -2.0f, 3.0f};
        vwp::RuntimeStatus parsedStatus;
        Check(
            vwp::ParseRuntimeStatus(
                vwp::SerializeRuntimeStatus(runtimeStatus),
                &parsedStatus,
                &profileError) &&
                parsedStatus.status == "captured" &&
                parsedStatus.weaponId == "mp5" &&
                parsedStatus.capturedAnglesValid &&
                parsedStatus.capturedEffectiveAngles[0] == 11.0f,
            "V63 runtime weapon identity, effective pose, and capture result should round-trip");

        const float identity[3][3] = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
        const float yawedController[3][3] = {
            {0.0f, 1.0f, 0.0f},
            {-1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        };
        float capturedRotation[3][3] = {};
        kisak::vr::weapon_calibration::AimAlignedEffectiveRotation(
            identity,
            yawedController,
            capturedRotation);
        float aligned[3][3] = {};
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                aligned[row][column] =
                    capturedRotation[row][0] * yawedController[0][column] +
                    capturedRotation[row][1] * yawedController[1][column] +
                    capturedRotation[row][2] * yawedController[2][column];
            }
        }
        Check(
            NearlyEqual(aligned[0][0], 1.0f) &&
                NearlyEqual(aligned[1][1], 1.0f) &&
                NearlyEqual(aligned[2][2], 1.0f),
            "V63 deliberate aim capture should solve an absolute correction without startup-pose calibration");
    }

    {
        vh::Layout layout = vh::DefaultLayout();
        Check(
            vh::kElementCount == 5u,
            "V61 should expose five independently movable HUD groups");
        Check(
            NearlyEqual(
                vh::ElementCenter(
                    layout,
                    vh::Element::AmmoEquipment).x,
                195.0f) &&
                NearlyEqual(
                vh::ElementCenter(
                    layout,
                    vh::Element::Compass).y,
                    360.0f),
            "the visual canvas should reproduce the tested ammo and V82 compass defaults");

        vh::MoveElement(
            &layout,
            vh::Element::Notifications,
            {329.0f, 246.0f},
            true);
        const vh::Point snapped = vh::ElementCenter(
            layout,
            vh::Element::Notifications);
        Check(
            NearlyEqual(snapped.x, 320.0f) &&
                NearlyEqual(snapped.y, 240.0f),
            "dragging near the visual center should snap to its anchor");

        vh::SetElementScale(
            &layout,
            vh::Element::Subtitles,
            1.25f);
        Check(
            NearlyEqual(
                vh::ElementScale(
                    layout,
                    vh::Element::Subtitles),
                1.25f),
            "the visual editor should resize an individual HUD group");

        const vh::Point compassBeforeResize = vh::ElementCenter(
            layout,
            vh::Element::Compass);
        vh::SetElementScale(
            &layout,
            vh::Element::Compass,
            1.40f);
        const vh::Point compassAfterResize = vh::ElementCenter(
            layout,
            vh::Element::Compass);
        Check(
            NearlyEqual(compassBeforeResize.x, compassAfterResize.x) &&
                NearlyEqual(compassBeforeResize.y, compassAfterResize.y),
            "resizing an edge HUD group should preserve its visual center");

        vh::Element hit = vh::Element::AmmoEquipment;
        Check(
            vh::HitTestElement(
                layout,
                vh::ElementCenter(
                    layout,
                    vh::Element::Subtitles),
                &hit) &&
                hit == vh::Element::Subtitles,
            "the visual canvas should select HUD groups by their real bounds");

        Check(
            vh::CycleElement(vh::Element::AmmoEquipment, -1) ==
                    vh::Element::Subtitles &&
                vh::CycleElement(vh::Element::Subtitles, 1) ==
                    vh::Element::AmmoEquipment &&
                vh::CycleElement(vh::Element::AmmoEquipment, 1) ==
                    vh::Element::Compass,
            "V69 selection cycling should reach every HUD group without hit-testing its rectangle");

        vh::Layout recovery = vh::DefaultLayout();
        recovery.compassScale = 2.0f;
        recovery.compassInsetX = 600.0f;
        recovery.compassInsetY = 440.0f;
        recovery.notificationOffsetX = 123.0f;
        vh::ClampLayout(&recovery);
        const vh::Point unreachableCompass = vh::ElementCenter(
            recovery,
            vh::Element::Compass);
        const vh::Size unreachableCompassSize = vh::ElementSize(
            recovery,
            vh::Element::Compass);
        Check(
            unreachableCompass.x -
                    unreachableCompassSize.width * 0.5f < 0.0f &&
                unreachableCompass.y -
                    unreachableCompassSize.height * 0.5f < 0.0f,
            "the regression fixture should reproduce a partially off-canvas compass group");

        vh::CenterElement(&recovery, vh::Element::Compass);
        const vh::Point centeredCompass = vh::ElementCenter(
            recovery,
            vh::Element::Compass);
        Check(
            NearlyEqual(centeredCompass.x, 320.0f) &&
                NearlyEqual(centeredCompass.y, 240.0f) &&
                NearlyEqual(recovery.compassScale, 2.0f) &&
                NearlyEqual(recovery.notificationOffsetX, 123.0f),
            "V69 center-selected should recover an off-screen group without changing its scale or another group");

        recovery.compassEnabled = false;
        vh::ResetElement(&recovery, vh::Element::Compass);
        const vh::Layout defaults = vh::DefaultLayout();
        const vh::Point resetCompass = vh::ElementCenter(
            recovery,
            vh::Element::Compass);
        const vh::Point defaultCompass = vh::ElementCenter(
            defaults,
            vh::Element::Compass);
        Check(
            NearlyEqual(resetCompass.x, defaultCompass.x) &&
                NearlyEqual(resetCompass.y, defaultCompass.y) &&
                NearlyEqual(
                    recovery.compassScale,
                    defaults.compassScale) &&
                recovery.compassEnabled &&
                NearlyEqual(recovery.notificationOffsetX, 123.0f),
            "V69 reset-selected should restore only the selected group to tested defaults");

        vh::Request requested;
        requested.requestId = "hud-test-123";
        requested.layout = layout;
        vh::Request parsed;
        std::string parseError;
        Check(
            vh::ParseRequest(
                vh::SerializeRequest(requested),
                &parsed,
                &parseError) &&
                parsed.requestId == requested.requestId &&
                NearlyEqual(
                    parsed.layout.subtitleScale,
                    1.25f),
            "V61 headset editor requests should round-trip through the shared protocol");
        Check(
            !vh::ParseRequest(
                "VERSION=1\nREQUEST_ID=bad&id\nCOMMAND=START\n",
                &parsed,
                &parseError),
            "V61 should reject unsafe headset editor request identifiers");

        vh::Response response;
        response.requestId = requested.requestId;
        response.status = vh::ResponseStatus::Saved;
        response.layout = layout;
        response.message = "saved";
        vh::Response parsedResponse;
        Check(
            vh::ParseResponse(
                vh::SerializeResponse(response),
                &parsedResponse,
                &parseError) &&
                parsedResponse.status ==
                    vh::ResponseStatus::Saved &&
                parsedResponse.requestId == requested.requestId,
            "the runtime's saved layout response should round-trip exactly");
    }

    {
        vrc::Probe vd;
        vd.is64BitWindows = true;
        vd.gameExecutablePresent = true;
        vd.modExecutablePresent = true;
        vd.configuratorPresent = true;
        vd.settingsPresent = true;
        vd.launcherPresent = true;
        vd.d3dx9Present = true;
        vd.openXr32Registered = true;
        vd.openXr32ManifestPresent = true;
        vd.openXr32ManifestPath = "C:/Program Files/Virtual Desktop/VDXR/OpenXR.json";
        vd.openXr32ManifestText = "{\"runtime\":{\"library_path\":\"VirtualDesktop.OpenXR-32.dll\"}}";
        vd.openVrInstalled = true;
        vd.gpuName = "NVIDIA GeForce RTX 3080 Ti";
        vd.dedicatedVideoMemoryBytes = 12ull * 1024ull * 1024ull * 1024ull;
        vd.backendPolicy = "auto";
        const vrc::Report report = vrc::Evaluate(vd);
        Check(
            report.readyForLaunch &&
                report.status == vrc::Status::Warning &&
                report.runtimeFamily == vrc::RuntimeFamily::VirtualDesktop &&
                report.recommendedBackend == "openxr" &&
                report.recommendedGraphicsProfile == "native" &&
                report.headsetTestRequired,
            "V65 should recommend the primary tested VDXR/native path while requiring one live headset test");

        vd.lastRuntimeStatus = "READY";
        vd.lastRuntimeBackend = "OpenXR";
        vd.lastRuntimeName = "VirtualDesktopXR";
        vd.lastHeadsetName = "Meta Quest 3";
        vd.lastLeftControllerProfile = "/interaction_profiles/meta/touch_controller_plus";
        vd.lastRightControllerProfile = vd.lastLeftControllerProfile;
        const vrc::Report proven = vrc::Evaluate(vd);
        Check(
            proven.status == vrc::Status::Ready &&
                !proven.headsetTestRequired,
            "V65 should become fully ready after a successful runtime/controller receipt");

        const std::string serialized = vrc::SerializeReport(
            vd,
            proven,
            "2026-08-10T20:00:00-0300");
        Check(
            serialized.find("STATUS=READY") != std::string::npos &&
                serialized.find("RUNTIME_FAMILY=virtual_desktop") != std::string::npos &&
                serialized.find("LAST_HEADSET=Meta Quest 3") != std::string::npos &&
                serialized.find("RECOMMENDED_BACKEND=openxr") != std::string::npos &&
                serialized.find("CHECK_runtime=READY") != std::string::npos,
            "V65 support reports should preserve machine, runtime, recommendation, and per-check evidence");

        vrc::Probe pimax = vd;
        pimax.openXr32ManifestPath =
            "C:/Program Files/Pimax/Runtime/PiOpenXR_32.json";
        pimax.openXr32ManifestText =
            "{\"runtime\":{\"library_path\":\"PimaxOpenXR.dll\"}}";
        pimax.lastRuntimeName = "Pimax OpenXR";
        pimax.lastHeadsetName = "Pimax Crystal Light";
        pimax.currentPackedMode = "6016x2688";
        pimax.currentOutputScale = "1.00";

        const vrc::Report pimaxNeedsLayout =
            vrc::Evaluate(pimax);
        Check(
            pimaxNeedsLayout.readyForLaunch &&
                pimaxNeedsLayout.status == vrc::Status::Warning &&
                pimaxNeedsLayout.runtimeFamily ==
                    vrc::RuntimeFamily::Pimax &&
                pimaxNeedsLayout.recommendedGraphicsProfile ==
                    "pimax_crystal_light",
            "issue #41 V89 should recommend the full-FOV scope-safe packed mode after a Pimax Crystal Light runtime receipt");

        pimax.currentPackedMode = "7684x3128";
        const vrc::Report pimaxCroppedLayout =
            vrc::Evaluate(pimax);
        Check(
            pimaxCroppedLayout.status == vrc::Status::Warning &&
                pimaxCroppedLayout.recommendedGraphicsProfile ==
                    "pimax_crystal_light" &&
                vrc::SerializeReport(
                    pimax,
                    pimaxCroppedLayout,
                    "2026-08-13T22:00:00-0300")
                        .find(
                            "CHECK_pimax_scope_layout=WARNING") !=
                    std::string::npos,
            "issue #41 V89 should identify the old 7684 x 3128 mode as cropped-FOV-only");

        pimax.currentPackedMode = "7924x4082";
        pimax.currentOutputScale = "0.80";
        const vrc::Report pimaxFullFovScopeReady =
            vrc::Evaluate(pimax);
        Check(
            pimaxFullFovScopeReady.status == vrc::Status::Ready &&
                pimaxFullFovScopeReady.recommendedGraphicsProfile ==
                    "pimax_crystal_light" &&
                vrc::SerializeReport(
                    pimax,
                    pimaxFullFovScopeReady,
                    "2026-08-14T15:00:00-0300")
                        .find(
                            "CHECK_pimax_scope_layout=READY") !=
                    std::string::npos,
            "issue #41 V89 should reserve 3450 + 3450 + 1024 pixels for uncropped Pimax eyes and the dedicated scope camera");
    }

    {
        vrc::Probe mismatch;
        mismatch.is64BitWindows = true;
        mismatch.gameExecutablePresent = true;
        mismatch.modExecutablePresent = true;
        mismatch.configuratorPresent = true;
        mismatch.settingsPresent = true;
        mismatch.launcherPresent = true;
        mismatch.d3dx9Present = true;
        mismatch.openXr64Registered = true;
        mismatch.openXr64ManifestPresent = true;
        mismatch.openXr64ManifestPath = "C:/Runtime/openxr64.json";
        mismatch.gpuName = "8 GiB GPU";
        mismatch.dedicatedVideoMemoryBytes = 8ull * 1024ull * 1024ull * 1024ull;
        mismatch.backendPolicy = "auto";
        const vrc::Report blocked = vrc::Evaluate(mismatch);
        Check(
            blocked.status == vrc::Status::Blocked &&
                !blocked.readyForLaunch,
            "V65 should block a 32-bit launch when only a 64-bit OpenXR runtime is available and OpenVR is absent");

        mismatch.openVrInstalled = true;
        mismatch.openVrEvidence = "openvrpaths.vrpath";
        const vrc::Report fallback = vrc::Evaluate(mismatch);
        Check(
            fallback.readyForLaunch &&
                fallback.status == vrc::Status::Warning &&
                fallback.recommendedBackend == "openvr" &&
                fallback.recommendedGraphicsProfile == "performance",
            "V65 should identify the x86 OpenVR escape path and recommend Performance for an 8 GiB adapter");

        mismatch.backendPolicy = "openxr";
        const vrc::Report forced = vrc::Evaluate(mismatch);
        Check(
            forced.status == vrc::Status::Blocked,
            "V65 should honor an explicit OpenXR-only policy instead of silently changing backends");
    }

    const auto& catalog = kc::SettingsCatalog();
    Check(catalog.size() == 142u, "V65 should retain all 142 verified settings");
    Check(vi::kActionCount == 23u, "V57 input V4 should expose 23 actions");

    std::set<std::string> keys;
    for (const kc::SettingDefinition& definition : catalog)
    {
        Check(keys.insert(definition.key).second, "duplicate key: " + definition.key);
    }

    kc::SettingsMap values = kc::BuiltInDefaults();
    auto messages = kc::ValidateSettings(values);
    Check(!HasError(messages, "VR_CUSTOM_MODE"), "built-in defaults should validate");
    Check(messages.empty(), "built-in defaults should have no warnings or errors");
    Check(
        values["KISAK_VR_UNIT_SYSTEM"] == "metric" &&
            kc::MeasurementUnitsFromSettings(values) ==
                kc::MeasurementUnitSystem::Metric,
        "V62 should default the configurator to metric presentation");
    Check(
        values["KISAK_VR_WEAPON_PROFILES_ENABLED"] == "1",
        "V63 should enable per-weapon and gunstock layering by default");
    Check(
        values["KISAK_VR_CROSSHAIR"] == "0",
        "V83 should default the normal COD4 weapon crosshair to off");
    Check(
        values["KISAK_VR_DOMINANT_HAND"] == "right" &&
            values["KISAK_VR_SUPPORT_GRIP_MODE"] == "hold" &&
            values["KISAK_VR_OBJECT_GRIP_MODE"] == "hold" &&
            values["KISAK_VR_MELEE_MODE"] == "both" &&
            values["KISAK_VR_GRENADE_BELT_LAYOUT"] == "handed",
        "V64 should preserve the tested right-handed behavior while enabling optional physical interactions");

    {
        kc::SettingsMap handed = values;
        const std::string originalAttack = handed["KISAK_VR_BIND_ATTACK"];
        const std::string originalMove = handed["KISAK_VR_BIND_MOVE_AXIS"];
        Check(
            kc::ApplyDominantHand("left", &handed) &&
                handed["KISAK_VR_DOMINANT_HAND"] == "left" &&
                handed["KISAK_VR_BIND_ATTACK"] == "left.trigger" &&
                handed["KISAK_VR_BIND_MOVE_AXIS"] == "right.primary_axis" &&
                kc::ValidateSettings(handed).empty(),
            "V64 left-handed selection should mirror bindings and remain fully valid");
        Check(
            kc::ApplyDominantHand("right", &handed) &&
                handed["KISAK_VR_BIND_ATTACK"] == originalAttack &&
                handed["KISAK_VR_BIND_MOVE_AXIS"] == originalMove,
            "V64 handedness should round-trip custom controller sources without drift");
    }

    std::size_t physicalSettingCount = 0u;
    for (const kc::SettingDefinition& definition : catalog)
    {
        if (definition.measurementKind == kc::MeasurementKind::None)
        {
            continue;
        }

        ++physicalSettingCount;
        for (const kc::MeasurementUnitSystem units : {
                 kc::MeasurementUnitSystem::Metric,
                 kc::MeasurementUnitSystem::Imperial})
        {
            std::string displayed;
            std::string canonical;
            Check(
                kc::CanonicalValueToDisplay(
                    definition,
                    units,
                    definition.defaultValue,
                    &displayed) &&
                    kc::DisplayValueToCanonical(
                        definition,
                        units,
                        displayed,
                        &canonical),
                "physical setting should convert in both directions: " +
                    definition.key);

            const double original = std::stod(definition.defaultValue);
            const double recovered = std::stod(canonical);
            const double tolerance =
                0.5001 * std::pow(10.0, -definition.decimalPlaces);
            Check(
                std::abs(original - recovered) <= tolerance,
                "unit display round-trip should preserve the canonical calibration: " +
                    definition.key);
        }
    }
    Check(
        physicalSettingCount == 22u,
        "V64 should classify all 22 physical calibration/interaction fields and no HUD pixel fields");

    const kc::SettingDefinition* const standingHeight =
        kc::FindSetting("KISAK_VR_STANDING_EYE_HEIGHT");
    const kc::SettingDefinition* const weaponForward =
        kc::FindSetting("KISAK_VR_WEAPON_OFFSET_FORWARD");
    const kc::SettingDefinition* const grenadeDrop =
        kc::FindSetting("KISAK_VR_GRENADE_DROP_SPEED");
    const kc::SettingDefinition* const scopeForward =
        kc::FindSetting("KISAK_VR_SCOPE_FORWARD_METERS");
    std::string converted;
    std::string canonical;
    Check(
        standingHeight != nullptr &&
            kc::CanonicalValueToDisplay(
                *standingHeight,
                kc::MeasurementUnitSystem::Metric,
                "60.0",
                &converted) &&
            converted == "152.4" &&
            kc::DisplaySettingLabel(
                *standingHeight,
                kc::MeasurementUnitSystem::Metric) ==
                "Standing virtual eye height (cm)",
        "60 game inches should be presented as 152.4 cm");
    Check(
        standingHeight != nullptr &&
            kc::DisplayValueToCanonical(
                *standingHeight,
                kc::MeasurementUnitSystem::Metric,
                "106.7",
                &canonical) &&
            canonical == "42.00" &&
            kc::DisplayValueToCanonical(
                *standingHeight,
                kc::MeasurementUnitSystem::Metric,
                "213.4",
                &canonical) &&
            canonical == "84.00",
        "rounded metric range endpoints should snap to the exact canonical limits");
    Check(
        weaponForward != nullptr &&
            kc::CanonicalValueToDisplay(
                *weaponForward,
                kc::MeasurementUnitSystem::Metric,
                "3.00",
                &converted) &&
            converted == "7.62" &&
            kc::DisplayValueToCanonical(
                *weaponForward,
                kc::MeasurementUnitSystem::Metric,
                converted,
                &canonical) &&
            canonical == "3.00",
        "weapon offsets should round-trip between game inches and centimeters exactly");
    Check(
        grenadeDrop != nullptr &&
            kc::CanonicalValueToDisplay(
                *grenadeDrop,
                kc::MeasurementUnitSystem::Metric,
                "35",
                &converted) &&
            converted == "89" &&
            kc::DisplaySettingLabel(
                *grenadeDrop,
                kc::MeasurementUnitSystem::Metric) ==
                "Grenade drop threshold (cm/s)",
        "physical hand speeds should use centimeters per second in metric mode");
    Check(
        scopeForward != nullptr &&
            kc::CanonicalValueToDisplay(
                *scopeForward,
                kc::MeasurementUnitSystem::Metric,
                "-0.100",
                &converted) &&
            converted == "-10.0" &&
            kc::CanonicalValueToDisplay(
                *scopeForward,
                kc::MeasurementUnitSystem::Imperial,
                "-0.100",
                &canonical) &&
            canonical == "-3.94",
        "scope meters should be presented as centimeters or inches without changing runtime storage");
    values["KISAK_VR_STANDING_EYE_HEIGHT"] = "90.0";
    messages = kc::ValidateSettings(values);
    Check(
        std::any_of(
            messages.begin(),
            messages.end(),
            [](const kc::ValidationMessage& message)
            {
                return message.key == "KISAK_VR_STANDING_EYE_HEIGHT" &&
                    message.message.find("106.7 through 213.4 cm") !=
                        std::string::npos;
            }),
        "metric range errors should be reported in the units shown by the editor");
    values = kc::BuiltInDefaults();
    {
        vh::Layout edited = kc::HudLayoutFromSettings(values);
        const vh::Point defaultCompassCenter =
            vh::ElementCenter(edited, vh::Element::Compass);
        const vh::Size defaultCompassSize =
            vh::ElementSize(edited, vh::Element::Compass);
        const vh::Rect transformedCompassTicker =
            vh::TransformCompassRect(
                edited,
                {-62.0f, -34.0f, 124.0f, 13.0f});
        Check(
            NearlyEqual(defaultCompassCenter.x, 348.0f) &&
                NearlyEqual(defaultCompassCenter.y, 360.0f) &&
                NearlyEqual(defaultCompassSize.width, 144.0f) &&
                NearlyEqual(defaultCompassSize.height, 40.0f) &&
                NearlyEqual(
                    transformedCompassTicker.x +
                        transformedCompassTicker.width * 0.5f,
                    28.0f) &&
                NearlyEqual(
                    transformedCompassTicker.y +
                        transformedCompassTicker.height * 0.5f,
                    -120.0f),
            "V82 should preserve beta.12's saved compass rectangle and use one transform for its real ticker and editor bounds");
        const vh::Point defaultNotificationCenter =
            vh::ElementCenter(
                edited,
                vh::Element::Notifications);
        Check(
            NearlyEqual(defaultNotificationCenter.x, 301.0f) &&
                NearlyEqual(defaultNotificationCenter.y, 109.0f),
            "V82 notification bounds should start from the real safe-left 6/10 message anchor and include the configured offset");
        vh::MoveElement(
            &edited,
            vh::Element::AmmoEquipment,
            {250.0f, 400.0f},
            false);
        vh::MoveElement(
            &edited,
            vh::Element::ObjectiveBanner,
            {300.0f, 220.0f},
            false);
        vh::SetElementScale(
            &edited,
            vh::Element::ObjectiveBanner,
            1.35f);
        kc::ApplyHudLayoutToSettings(edited, &values);
        const vh::Layout roundTripped =
            kc::HudLayoutFromSettings(values);
        Check(
            NearlyEqual(
                vh::ElementCenter(
                    roundTripped,
                    vh::Element::AmmoEquipment).x,
                250.0f) &&
                NearlyEqual(
                    vh::ElementCenter(
                        roundTripped,
                        vh::Element::ObjectiveBanner).y,
                    220.0f) &&
                NearlyEqual(
                    roundTripped.objectiveScale,
                    1.35f),
            "desktop HUD edits should map to settings and back without losing placement");
        values = kc::BuiltInDefaults();
    }
    Check(
        values["KISAK_VR_INPUT_BINDINGS_VERSION"] == "4",
        "built-in defaults should use Controller Input V4");
    Check(
        values["KISAK_VR_BIND_ATTACK"] == "right.trigger",
        "fire should retain the beta.7 right-trigger default");
    Check(
        values["KISAK_VR_BIND_JUMP"] ==
                "right.primary_axis.up" &&
            values["KISAK_VR_BIND_JUMP_ALT"] ==
                "left.trigger" &&
            values["KISAK_VR_BIND_LOWER_STANCE"] ==
                "right.primary_axis.down",
        "right-stick up should be Jump, right-stick down should lower stance, and left trigger should remain an alternate Jump binding");
    Check(
        std::string(vi::GetActionDefinition(vi::Action::Jump).label) ==
                "Jump" &&
            std::string(
                vi::GetActionDefinition(
                    vi::Action::LowerStance).label) ==
                "Lower stance",
        "the menu should use the real Jump and Lower stance rows instead of duplicate raise/lower-step labels");
    Check(
        values.count("KISAK_VR_BIND_RAISE_STANCE") == 0u,
        "the confusing separate Raise stance action should be retired");

    const kc::SettingDefinition* const jumpSetting =
        kc::FindSetting("KISAK_VR_BIND_JUMP");
    const kc::SettingDefinition* const lowerStanceSetting =
        kc::FindSetting("KISAK_VR_BIND_LOWER_STANCE");
    const auto hasChoice = [](
        const kc::SettingDefinition* const setting,
        const std::string& value,
        const std::string& label)
    {
        return setting != nullptr &&
            std::any_of(
                setting->choices.begin(),
                setting->choices.end(),
                [&](const kc::SettingChoice& choice)
                {
                    return choice.value == value &&
                        choice.label == label;
                });
    };
    Check(
        hasChoice(
            jumpSetting,
            "right.primary_axis.up",
            "Right primary stick / trackpad up"),
        "the Jump menu must visibly offer Right primary stick / trackpad up");
    Check(
        hasChoice(
            lowerStanceSetting,
            "right.primary_axis.down",
            "Right primary stick / trackpad down"),
        "the Lower stance menu must visibly offer Right primary stick / trackpad down");
    Check(
        values["KISAK_VR_BIND_MOVE_AXIS"] == "left.primary_axis",
        "movement should use the controller-neutral primary axis");
    Check(
        values["KISAK_VR_BIND_MENU_AXIS"] == "left.primary_axis" &&
            values["KISAK_VR_BIND_SCOPE_ZOOM_AXIS"] ==
                "left.primary_axis",
        "formerly shared auxiliary axes should have independent bindings");
    Check(
        values["KISAK_VR_BIND_NIGHT_VISION"] ==
            "right.thumbrest_touch+left.primary_axis.down",
        "night vision should expose the proven beta.7 modifier chord");
    Check(
        values["KISAK_VR_BIND_GRENADE_LAUNCHER"] ==
                "right.squeeze",
        "grenade launcher should use the physical right grip by default");
    Check(
        values["KISAK_VR_BIND_AIRSTRIKE"] ==
                "right.thumbrest_touch+left.primary_axis.left" &&
            values["KISAK_VR_BIND_C4"] ==
                "right.thumbrest_touch+left.primary_axis.right",
        "the remaining directional mission shortcuts should retain their ordinary chords");
    Check(
        values["KISAK_VR_BIND_OFFHAND"] == "unbound",
        "the redundant native off-hand action should remain unbound by default");
    Check(
        values["KISAK_VR_PLAY_MODE"] == "standing" &&
            values["KISAK_VR_STANDING_EYE_HEIGHT"] == "60.0" &&
            values["KISAK_VR_SEATED_EYE_HEIGHT"] == "60.0" &&
            values["KISAK_VR_RECENTER_ON_START"] == "full",
        "V70 calibration defaults should preserve COD4's native height and make beta.10 full startup recenter behavior explicit");

    const kc::SettingsMap legacyRecenterEnabled =
        kc::ParseBatchSettings(
            "set \"KISAK_VR_RECENTER_ON_START=1\"\n");
    const kc::SettingsMap legacyRecenterDisabled =
        kc::ParseBatchSettings(
            "set \"KISAK_VR_RECENTER_ON_START=0\"\n");
    Check(
        legacyRecenterEnabled.at("KISAK_VR_RECENTER_ON_START") ==
                "full" &&
            legacyRecenterDisabled.at("KISAK_VR_RECENTER_ON_START") ==
                "off",
        "V70 should migrate legacy startup recenter toggles without changing behavior");

    vi::Binding jumpDefault;
    vi::Binding lowerStanceDefault;
    vi::Binding nightVisionDefault;
    Check(
        vi::ParseBinding(
            vi::Action::Jump,
            values["KISAK_VR_BIND_JUMP"],
            &jumpDefault) &&
            jumpDefault.sourceCount == 1u &&
            jumpDefault.sources[0] ==
                vi::Source::RightPrimaryAxisUp,
        "Jump should evaluate right-stick up as one directional input");
    Check(
        vi::ParseBinding(
            vi::Action::LowerStance,
            values["KISAK_VR_BIND_LOWER_STANCE"],
            &lowerStanceDefault) &&
            lowerStanceDefault.sourceCount == 1u &&
            lowerStanceDefault.sources[0] ==
                vi::Source::RightPrimaryAxisDown,
        "Lower stance should evaluate right-stick down as one directional input");
    Check(
        vi::ParseBinding(
            vi::Action::NightVision,
            values["KISAK_VR_BIND_NIGHT_VISION"],
            &nightVisionDefault) &&
            nightVisionDefault.sourceCount == 2u &&
            nightVisionDefault.sources[0] ==
                vi::Source::RightThumbrestTouch &&
            nightVisionDefault.sources[1] ==
                vi::Source::LeftPrimaryAxisDown,
        "Night vision should evaluate as right-thumbrest AND left-stick down");
    Check(
        vi::DirectionalSourcePressed(
            nightVisionDefault.sources[1],
            0.0f,
            -0.80f) &&
            !vi::DirectionalSourcePressed(
                nightVisionDefault.sources[1],
                0.0f,
                0.80f),
        "the night-vision chord should accept left-stick down and reject left-stick up");

    if (argumentCount >= 2)
    {
        const std::filesystem::path releaseDefaults = arguments[1];
        const kc::LoadResult packaged = kc::LoadSettings(
            releaseDefaults,
            releaseDefaults.parent_path() / "__missing-v56-user-settings.bat");
        Check(packaged.messages.empty(), "packaged release defaults should validate cleanly");
        Check(packaged.values == values, "packaged release defaults should match the configurator catalog");
        Check(
            packaged.profileName == "Tested Quest 3" &&
                packaged.revision == "beta11-hud-recenter-prompts-fng-defaults",
            "packaged defaults should identify their active profile and revision");
        Check(
            packaged.activePath == releaseDefaults,
            "packaged defaults should report their exact active path");
    }

    if (argumentCount >= 4)
    {
        const std::string launcher = Read(arguments[2]);
        const std::string runtime = Read(arguments[3]);
        Check(
            launcher.find("STATUS=LAUNCHER_VERIFIED") != std::string::npos &&
                launcher.find("Active-VR-Settings.txt") != std::string::npos &&
                launcher.find("Calibration-Request.txt") != std::string::npos &&
                launcher.find("Calibration-Status.txt") != std::string::npos &&
                launcher.find("HUD-Editor-Request.txt") != std::string::npos &&
                launcher.find("HUD-Editor-Status.txt") != std::string::npos &&
                launcher.find("VR-Weapon-Profiles.ini") !=
                    std::string::npos &&
                launcher.find("Weapon-Calibration-Request.txt") !=
                    std::string::npos &&
                launcher.find("Weapon-Calibration-Status.txt") !=
                    std::string::npos &&
                launcher.find("Compatibility-Report.txt") !=
                    std::string::npos &&
                launcher.find("--compatibility-report") !=
                    std::string::npos &&
                launcher.find("compatibility preflight found a launch blocker") !=
                    std::string::npos &&
                launcher.find("--validate") != std::string::npos,
            "the launcher should validate overrides, run beta.14 preflight, and publish every guarded state path");
        Check(
            launcher.find(
                "KISAK_SP_VR_PIMAX_X86_RUNTIME_V86") !=
                    std::string::npos &&
                launcher.find(
                    "Explicit XR_RUNTIME_JSON was preserved") !=
                    std::string::npos &&
                launcher.find("PiOpenXR_32.json") !=
                    std::string::npos &&
                launcher.find(
                    "if not defined XR_RUNTIME_JSON if defined VR_ACTIVE_RUNTIME_32") !=
                    std::string::npos &&
                launcher.find(
                    "if not defined XR_RUNTIME_JSON if not defined VR_ACTIVE_RUNTIME_32 if defined VR_ACTIVE_RUNTIME_64") !=
                    std::string::npos &&
                launcher.find(
                    "if /I not \"%KISAK_VR_BACKEND%\"==\"openvr\"") !=
                    std::string::npos &&
                launcher.find(
                    "KISAK_SP_VR_PIMAX_FULL_FOV_SCOPE_LAUNCH_PREFLIGHT_V89") !=
                    std::string::npos &&
                launcher.find("7924x4082") !=
                    std::string::npos &&
                launcher.find(
                    "7924x4082 requires KISAK_VR_OUTPUT_SCALE=0.80") !=
                    std::string::npos &&
                launcher.find("7684x3128") !=
                    std::string::npos &&
                launcher.find(
                    "7684x3128 requires KISAK_VR_OUTPUT_SCALE=1.00") !=
                    std::string::npos,
            "issues #11/#41 V89 launcher must preserve explicit/non-Pimax runtime choices, select Pimax's x86 manifest only for an active Pimax runtime, and guard both full-FOV and cropped-FOV Crystal Light packed modes");
        Check(
            runtime.find("STATUS=RUNTIME_ACCEPTED") != std::string::npos &&
                runtime.find("RUNTIME_MEASUREMENT_UNITS") !=
                    std::string::npos &&
                runtime.find("RUNTIME_ACTIVE_EYE_HEIGHT_DISPLAY") !=
                    std::string::npos &&
                runtime.find("RUNTIME_WEAPON_OFFSET") != std::string::npos &&
                runtime.find("RUNTIME_MANUAL_RELOAD") != std::string::npos &&
                runtime.find("RUNTIME_DOMINANT_HAND") !=
                    std::string::npos &&
                runtime.find("RUNTIME_SUPPORT_GRIP_MODE") !=
                    std::string::npos &&
                runtime.find("RUNTIME_RELOAD_EJECT_MODE") !=
                    std::string::npos &&
                runtime.find("RUNTIME_GRENADE_BELT_LAYOUT") !=
                    std::string::npos &&
                runtime.find("RUNTIME_MELEE_MODE") !=
                    std::string::npos &&
                runtime.find("WeaponControllerIndex") !=
                    std::string::npos &&
                runtime.find("OffHandControllerIndex") !=
                    std::string::npos &&
                runtime.find("MeleeGestureQualifies") !=
                    std::string::npos &&
                runtime.find("VrManualMagazineReloadStage::HoldingLoaded") !=
                    std::string::npos &&
                runtime.find("VR_ApplyOffhandControllerHaptic") !=
                    std::string::npos &&
                runtime.find("STATUS=RUNTIME_WEAPON_POSE_APPLIED") !=
                    std::string::npos &&
                runtime.find("RUNTIME_WEAPON_ALIGNMENT_ERROR") !=
                    std::string::npos &&
                runtime.find("RUNTIME_WEAPON_DISPLAY_UNITS") !=
                    std::string::npos &&
                runtime.find("RUNTIME_WEAPON_OFFSET_APPLIED_DISPLAY") !=
                    std::string::npos &&
                runtime.find("CalibratedGripTargetWorld") !=
                    std::string::npos &&
                runtime.find("STATUS=RUNTIME_HEIGHT_APPLIED") !=
                    std::string::npos &&
                runtime.find("STATUS=RUNTIME_CALIBRATION_APPLIED") !=
                    std::string::npos &&
                runtime.find("RUNTIME_CALIBRATION_DISPLAY_UNITS") !=
                    std::string::npos &&
                runtime.find("RUNTIME_CALIBRATION_RECENTER_MODE") !=
                    std::string::npos &&
                runtime.find("RUNTIME_RECENTER_ON_START_MODE") !=
                    std::string::npos &&
                runtime.find("VR_RecenterHeadPosition") !=
                    std::string::npos &&
                runtime.find("VR_RecenterHeadDirectionLevel") !=
                    std::string::npos &&
                runtime.find("VR_RecenterHeadPose") !=
                    std::string::npos &&
                runtime.find("g_vrHeadPositionBodyYawDegrees") !=
                    std::string::npos &&
                runtime.find("Full recenter clears both histories") !=
                    std::string::npos &&
                runtime.find("VR_RecenterAtFirstGameplayCamera") !=
                    std::string::npos &&
                runtime.find("[VR][CALIBRATION][RECENTER]") !=
                    std::string::npos &&
                runtime.find("XR_REFERENCE_SPACE_TYPE_STAGE") !=
                    std::string::npos &&
                runtime.find("RUNTIME_HUD_EDITOR_ELEMENTS") !=
                    std::string::npos &&
                runtime.find("STATUS=RUNTIME_HUD_EDITOR_%s") !=
                    std::string::npos &&
                runtime.find("VR_ProcessHudEditorRequest") !=
                    std::string::npos &&
                runtime.find("VR_HudEditorConsumesGameplayInput") !=
                    std::string::npos &&
                runtime.find("g_vrWeaponAttachmentBaselines") !=
                    std::string::npos &&
                runtime.find("VR_ProcessWeaponCalibrationRequest") !=
                    std::string::npos &&
                runtime.find("STATUS=RUNTIME_WEAPON_PROFILE_APPLIED") !=
                    std::string::npos &&
                runtime.find("effective.pose.offset.data()") !=
                    std::string::npos &&
                runtime.find("(std::max)(twoHandBlend, adsFraction)") !=
                    std::string::npos &&
                runtime.find("AimAlignedEffectiveRotation") !=
                    std::string::npos &&
                runtime.find("STATUS=RUNTIME_COMPATIBILITY_READY") !=
                    std::string::npos &&
                runtime.find("RUNTIME_COMPATIBILITY_RUNTIME") !=
                    std::string::npos &&
                runtime.find("RUNTIME_COMPATIBILITY_HEADSET") !=
                    std::string::npos &&
                runtime.find("RUNTIME_COMPATIBILITY_LEFT_CONTROLLER") !=
                    std::string::npos,
            "the game should acknowledge settings, calibration, compatibility, live HUD editing, and per-weapon/gunstock lifecycle receipts");
        const std::size_t openVrControllerUpdate =
            runtime.find("bool VR_UpdateOpenVrControllerActions()");
        const std::size_t openVrTwoHandUpdate =
            runtime.find(
                "VR_UpdateTwoHandWeaponTargetFromPublishedPoses();",
                openVrControllerUpdate);
        const std::size_t openVrFocusUpdate =
            runtime.find(
                "VR_UpdatePoseFocusAimFromControllers();",
                openVrTwoHandUpdate);
        Check(
            runtime.find(
                "void VR_UpdateTwoHandWeaponTargetFromPublishedPoses()") !=
                    std::string::npos &&
                CountOccurrences(
                    runtime,
                    "VR_UpdateTwoHandWeaponTargetFromPublishedPoses();") ==
                    2u &&
                openVrControllerUpdate != std::string::npos &&
                openVrTwoHandUpdate != std::string::npos &&
                openVrFocusUpdate != std::string::npos &&
                runtime.find(
                    "V66 backend-shared two-hand weapon") !=
                    std::string::npos &&
                openVrControllerUpdate < openVrTwoHandUpdate &&
                openVrTwoHandUpdate < openVrFocusUpdate,
            "issue #26: OpenXR and OpenVR must both update the shared two-hand weapon target after publishing controller poses");
        Check(
                runtime.find(
                    "KISAK_SP_VR_NIGHT_VISION_VISOR_GESTURE_V80") !=
                    std::string::npos &&
                runtime.find(
                    "KISAK_SP_VR_NIGHT_VISION_VISOR_FOREGRIP_GUARD_V81") !=
                    std::string::npos &&
                runtime.find(
                    "night_vision_gesture_grip") !=
                    std::string::npos &&
                runtime.find(
                    "g_vrNightVisionGestureGripAction") !=
                    std::string::npos &&
                runtime.find(
                    "VrInput::Source::LeftSqueeze") !=
                    std::string::npos &&
                runtime.find(
                    "nightVisionGesture.consumeLeftGrip") !=
                    std::string::npos &&
                runtime.find(
                    "nightVisionGesture.toggledThisFrame") !=
                    std::string::npos &&
                CountOccurrences(
                    runtime,
                    "VR_UpdateNightVisionVisorGesture(") == 3u &&
                runtime.find(
                    "queued one night-vision toggle") !=
                    std::string::npos &&
                runtime.find(
                    "V81 foregrip-safe left-grip") !=
                    std::string::npos,
            "V81 should route the foregrip-safe physical left-grip visor gesture through one shared OpenXR/OpenVR night-vision pulse path");
        const std::size_t lostPoseStatus =
            runtime.find("\"NO_TRACKED_POSE\"");
        const std::size_t heightCommit =
            runtime.find("Commit the requested height only");
        Check(
            lostPoseStatus != std::string::npos &&
                heightCommit != std::string::npos &&
                lostPoseStatus < heightCommit,
            "a failed tracked-pose recenter must be rejected before live height is committed");
        Check(
            runtime.find("const bool needsPosition") !=
                    std::string::npos &&
                runtime.find("const bool needsDirectionLevel") !=
                    std::string::npos &&
                runtime.find(
                    "mode == VrCalibration::RecenterMode::PositionOnly") !=
                    std::string::npos &&
                runtime.find(
                    "mode == VrCalibration::RecenterMode::DirectionLevelOnly") !=
                    std::string::npos &&
                runtime.find(
                    "VR_RecenterHeadForMode(recenterMode)") !=
                    std::string::npos,
            "issue #29 V70 must route OpenXR and OpenVR through one component-selective recenter transaction");

        const std::filesystem::path runtimePath = arguments[3];
        const std::filesystem::path root =
            runtimePath.parent_path().parent_path().parent_path();
        const std::string screenPlacement = Read(
            root / "src/client/screen_placement.cpp");
        const std::string messages = Read(
            root / "src/client/cl_console.cpp");
        const std::string compass = Read(
            root / "src/cgame/cg_compass.cpp");
        const std::string rendererScene = Read(
            root / "src/gfx_d3d/r_scene.cpp");
        const std::string draw = Read(
            root / "src/cgame/cg_draw.cpp");
        const std::string reticles = Read(
            root / "src/cgame/cg_draw_reticles.cpp");
        const std::string cgameMain = Read(
            root / "src/cgame/cg_main.cpp");
        const std::string cgameView = Read(
            root / "src/cgame/cg_view.cpp");
        Check(
            cgameView.find("VR_RecenterAtFirstGameplayCamera()") !=
                    std::string::npos &&
                cgameView.find(
                    "VR_GetFirstGameplayRecenterModeName()") !=
                    std::string::npos,
            "V70 first-gameplay capture should execute and report the explicit selected recenter mode");
        const std::string debugDraw = Read(
            root / "src/cgame/cg_draw_debug.cpp");
        const std::string weapons = Read(
            root / "src/cgame/cg_weapons.cpp");
        const std::string clientInput = Read(
            root / "src/client/cl_input.cpp");
        const std::string clientScreen = Read(
            root / "src/client/cl_scrn.cpp");
        const std::string promptLabels = Read(
            root / "src/vr/vr_prompt_labels.cpp");
        const std::string uiExpressions = Read(
            root / "src/ui/ui_expressions.cpp");
        const std::string gameScript = Read(
            root / "src/game/g_scr_main.cpp");
        const std::string gameClientScript = Read(
            root / "src/game/g_client_script_cmd.cpp");
        const std::string gamePlayerUse = Read(
            root / "src/game/player_use.cpp");
        const std::string qcommonCommandHeader = Read(
            root / "src/qcommon/cmd.h");
        const std::string qcommonCommands = Read(
            root / "src/qcommon/cmd.cpp");
        const std::string runtimeHeader = Read(
            root / "src/vr/vr_openxr.h");
        const std::string compatibilityProbe = Read(
            root / "tools/configurator/compatibility_probe_win32.cpp");
        const std::string configuratorBuild = Read(
            root / "tools/configurator/CMakeLists.txt");
        const std::size_t interactionPriority =
            runtime.find(
                "KISAK_SP_VR_OFFHAND_INTERACTION_PRIORITY_V86");
        const std::size_t magazinePriority =
            runtime.find(
                "const bool magazineOwnsLeftGrip",
                interactionPriority);
        const std::size_t supportPriority =
            runtime.find(
                "const bool supportGripCandidate",
                interactionPriority);
        const std::size_t grenadeGate =
            runtime.find(
                "if (beltGrabPressed",
                interactionPriority);
        Check(
            compatibilityProbe.find(
                "KISAK_SP_VR_PIMAX_X86_RUNTIME_V86") !=
                    std::string::npos &&
                compatibilityProbe.find(
                    "PiOpenXR_32.json") !=
                    std::string::npos &&
                compatibilityProbe.find(
                    "!probe.openXr32Registered") !=
                    std::string::npos &&
                compatibilityProbe.find(
                    "A valid non-Pimax x86 registration") !=
                    std::string::npos,
            "issue #11 V86 compatibility preflight must mirror the launcher's active-Pimax-only x86 manifest repair");
        Check(
            runtimeHeader.find(
                "bool VR_UsesPimaxGripPoseFallback();") !=
                    std::string::npos &&
                weapons.find(
                    "KISAK_SP_VR_PIMAX_FREE_HAND_BASIS_V86") !=
                    std::string::npos &&
                runtime.find(
                    "bool VR_UsesPimaxGripPoseFallback()") !=
                    std::string::npos &&
                runtime.find(
                    "VrRuntimeBackend::OpenXr") !=
                    std::string::npos &&
                runtime.find(
                    "g_vrLeftControllerPalmPoseValid") !=
                    std::string::npos &&
                weapons.find(
                    "VrFreeLeftHandPoseBasis::PimaxGripFallback") !=
                    std::string::npos &&
                weapons.find(
                    "usingPimaxGripPoseFallback") !=
                    std::string::npos &&
                weapons.find(
                    "weapon/support/reload consumers retain the original grip axis") !=
                    std::string::npos,
            "issue #32 V86 must apply the Pimax grip fallback basis only to the standalone free hand when OpenXR has no palm pose");
        Check(
            interactionPriority != std::string::npos &&
                magazinePriority != std::string::npos &&
                supportPriority != std::string::npos &&
                grenadeGate != std::string::npos &&
                magazinePriority < supportPriority &&
                supportPriority < grenadeGate &&
                runtime.find(
                    "VR_ManualMagazineOwnsOrClaimsLeftGripLocked") !=
                    std::string::npos &&
                runtime.find(
                    "VR_IsSupportGripCandidateLocked") !=
                    std::string::npos &&
                runtime.find(
                    "[VR][GRENADE][V86] Belt grab suppressed") !=
                    std::string::npos,
            "issues #11/#32 V86 must assign the squeeze edge to magazine/reload, then a valid support grip, before allowing a belt grenade grab");
        Check(
            runtime.find(
                "int VR_GetPromptBindingLabels(") !=
                    std::string::npos &&
                runtime.find(
                    "VrPrompts::BuildBindingLabels(") !=
                    std::string::npos &&
                runtime.find("[VR][PROMPTS] V71") !=
                    std::string::npos &&
                promptLabels.find(
                    "{\"+activate\", VrInput::Action::Use}") !=
                    std::string::npos &&
                promptLabels.find(
                    "{\"+reload\", VrInput::Action::Reload}") !=
                    std::string::npos &&
                promptLabels.find(
                    "{\"+gostand\", VrInput::Action::Jump}") !=
                    std::string::npos &&
                promptLabels.find(
                    "{\"+melee\", VrInput::Action::Melee}") !=
                    std::string::npos &&
                uiExpressions.find(
                    "VR_GetPromptBindingLabels(command, bindings)") !=
                    std::string::npos &&
                uiExpressions.find(
                    "bindCount = CL_GetKeyBinding(localClientNum, command, bindings)") !=
                    std::string::npos &&
                gameScript.find(
                    "bindCount = VR_GetPromptBindingLabels(command, bindings)") !=
                    std::string::npos &&
                gameScript.find(
                    "bindCount = CL_GetKeyBinding(0, command, bindings)") !=
                    std::string::npos,
            "V71 must route both HUD and script prompt lookups through configured VR labels while preserving keyboard fallback");
        Check(
            CountOccurrences(
                configuratorBuild,
                "../../src/vr/vr_prompt_labels.cpp") == 2u &&
                CountOccurrences(
                    configuratorBuild,
                    "../../src/vr/vr_prompt_labels.h") == 2u,
            "V71 must link the prompt resolver into both the configurator and its settings-test target");
        Check(
            runtime.find(
                "bool VR_GetCampaignAdsHeld(") !=
                    std::string::npos &&
                runtime.find(
                    "g_vrPoseFocusAimHeld;") !=
                    std::string::npos &&
                gameClientScript.find(
                    "KISAK_SP_VR_FNG_CAMPAIGN_INPUT_BRIDGE_V72") !=
                    std::string::npos &&
                gameClientScript.find(
                    "VR_GetCampaignAdsHeld(&vrAdsHeld)") !=
                    std::string::npos &&
                gameClientScript.find(
                    "scriptAdsFraction <= 0.5f") !=
                    std::string::npos &&
                gameClientScript.find(
                    "scriptAdsFraction = 1.0f") !=
                    std::string::npos &&
                clientInput.find(
                    "KISAK_SP_VR_FNG_CAMPAIGN_INPUT_BRIDGE_V72") !=
                    std::string::npos &&
                clientInput.find(
                    "KISAK_SP_VR_FNG_NATIVE_ADS_COMMAND_BRIDGE_V73") !=
                    std::string::npos &&
                clientInput.find(
                    "vrAdsNativeCommandHeld") !=
                    std::string::npos &&
                clientInput.find(
                    "+speed 253 0") !=
                    std::string::npos &&
                clientInput.find(
                    "-speed 253 0") !=
                    std::string::npos &&
                clientInput.find(
                    "[VR][CAMPAIGN] V73 native +speed DOWN") !=
                    std::string::npos &&
                clientInput.find(
                    "vrSprintNativeCommandHeld") !=
                    std::string::npos &&
                clientInput.find(
                    "+sprint 254 0") !=
                    std::string::npos &&
                clientInput.find(
                    "-sprint 254 0") !=
                    std::string::npos &&
                clientInput.find(
                    "!Key_IsCatcherActive(0, 0x33)") !=
                    std::string::npos &&
                clientInput.find(
                    "[VR][CAMPAIGN] V72") !=
                    std::string::npos,
            "V73 must mirror physical/configured ADS and Sprint through their native held command paths while retaining the V72 playerADS bridge");
        const std::size_t acceptedVrShot =
            clientInput.find("if (!vrMuzzleBlocked)");
        const std::size_t virtualAttackNotify =
            clientInput.find(
                "Cmd_NotifyVirtualCommand(\"+attack\")");
        Check(
            qcommonCommandHeader.find(
                "int Cmd_NotifyVirtualCommand(") !=
                    std::string::npos &&
                qcommonCommands.find(
                    "int Cmd_NotifyVirtualCommand(") !=
                    std::string::npos &&
                qcommonCommands.find(
                    "G_AddCommandNotify(cmd_notify[i].notify)") !=
                    std::string::npos &&
                qcommonCommands.find(
                    "return matchedNotifications;") !=
                    std::string::npos &&
                qcommonCommands.find(
                    "Cmd_NotifyVirtualCommand(Cmd_Argv(0))") !=
                    std::string::npos &&
                clientInput.find(
                    "KISAK_SP_VR_FNG_SCRIPT_ACTION_NOTIFY_BRIDGE_V74") !=
                    std::string::npos &&
                acceptedVrShot != std::string::npos &&
                virtualAttackNotify != std::string::npos &&
                acceptedVrShot < virtualAttackNotify &&
                clientInput.find(
                    "!kb[KEY_ATTACK].active") !=
                    std::string::npos &&
                clientInput.find(
                    "[VR][CAMPAIGN] V74 VR +attack notification") !=
                    std::string::npos,
            "V74 must release F.N.G.'s blocking pc_hip_attack keyHint through the registered +attack script-notify path without mutating mouse kbutton state or bypassing muzzle acceptance");
        Check(
            runtimeHeader.find(
                "bool VR_IsCenteredMonoscopicMenuActive();") !=
                    std::string::npos &&
                runtime.find(
                    "bool VR_IsCenteredMonoscopicMenuActive()") !=
                    std::string::npos &&
                runtime.find(
                    "KISAK_SP_VR_FNG_DIFFICULTY_MODAL_V75") !=
                    std::string::npos &&
                runtime.find("\"select_difficulty\"") !=
                    std::string::npos &&
                runtime.find("\"diff_con_easy\"") !=
                    std::string::npos &&
                runtime.find("\"diff_con_regular\"") !=
                    std::string::npos &&
                runtime.find("\"diff_con_hardened\"") !=
                    std::string::npos &&
                runtime.find("\"diff_con_veteran\"") !=
                    std::string::npos &&
                clientScreen.find(
                    "!VR_IsCenteredMonoscopicMenuActive()") !=
                    std::string::npos &&
                runtime.find(
                    "g_vrCenteredModalBlitVertexBuffer") !=
                    std::string::npos &&
                CountOccurrences(
                    runtime,
                    "VR_IsCenteredMonoscopicMenuActive();") == 3u,
            "V75 must classify F.N.G.'s recommendation and confirmation menus as one-pass centered modals in the stereo command-list, OpenXR, and OpenVR paths");
        Check(
            runtime.find(
                "const bool centeredModalMenu") !=
                    std::string::npos &&
                runtime.find(
                    "const bool activeGameplayMenu") !=
                    std::string::npos &&
                runtime.find("!centeredModalMenu;") !=
                    std::string::npos &&
                runtime.find(
                    "const bool cursorCoordinateModeChanged") !=
                    std::string::npos &&
                runtime.find(
                    "eyeLocalMenuWasActive") !=
                    std::string::npos &&
                runtime.find(
                    "V88 shared modal cursor uses the one-eye") !=
                    std::string::npos,
            "V88 must retain one-pass modal ownership while routing every menu through the same eye-local cursor space");
        Check(
            runtime.find(
                "KISAK_SP_VR_EYE_LOCAL_MENU_AND_CURSOR_V83") !=
                    std::string::npos &&
                runtime.find(
                    "const bool eyeLocalMenu") !=
                    std::string::npos &&
                runtime.find(
                    "eyeLocalMenuWasActive") !=
                    std::string::npos &&
                runtime.find(
                    "static_cast<int>(cursorX)") !=
                    std::string::npos &&
                runtime.find("cursorOriginX") ==
                    std::string::npos &&
                runtime.find(
                    "V83_EYE_LOCAL_MENU_SOURCE_OPENXR") !=
                    std::string::npos &&
                runtime.find(
                    "V83_EYE_LOCAL_MENU_SOURCE_OPENVR") !=
                    std::string::npos &&
                runtime.find(
                    "{{ 1.0f,  1.0f}, {0.5f, 0.0f}}") !=
                    std::string::npos &&
                runtime.find(
                    "V83 routes ordinary frontend/pause cursor") !=
                    std::string::npos,
            "V83 must sample one completed eye for ordinary menus and keep frontend/pause cursor hit testing in eye-local ScreenPlacement coordinates");
        Check(
            screenPlacement.find(
                "KISAK_SP_VR_MENU_SAFE_AREA_ISOLATION_V88") !=
                    std::string::npos &&
                screenPlacement.find(
                    "Key_IsCatcherActive(0, 0x10)") !=
                    std::string::npos &&
                runtime.find(
                    "KISAK_SP_VR_EYE_LOCAL_SHARED_MODAL_V88") !=
                    std::string::npos &&
                runtime.find(
                    "const bool eyeLocalMenu =\n        true;") !=
                    std::string::npos &&
                runtime.find(
                    "{{-1.0f,  1.0f}, {0.25f, 0.0f}}") ==
                    std::string::npos &&
                runtime.find(
                    "{{ 1.0f,  1.0f}, {0.75f, 0.0f}}") ==
                    std::string::npos &&
                runtime.find(
                    "V88 shared modal mono: retained") !=
                    std::string::npos &&
                runtime.find(
                    "V88 isolated menu ScreenPlacement") !=
                    std::string::npos &&
                runtime.find(
                    "appliedMenuActive == menuActive") !=
                    std::string::npos &&
                clientScreen.find(
                    "KISAK_SP_VR_EYE_LOCAL_SHARED_MODAL_V88") !=
                    std::string::npos,
            "issue #51 V88 must isolate menus from HUD safe-area transforms and sample shared dialogs from their eye-local source");
        Check(
            reticles.find(
                "KISAK_SP_VR_FLAT_CROSSHAIR_SUPPRESSION_V88") !=
                    std::string::npos &&
                reticles.find(
                    "if (VR_IsInitialized())") !=
                    std::string::npos &&
                reticles.find(
                    "V88 suppressed the legacy flat weapon") !=
                    std::string::npos &&
                reticles.find(
                    "return true;",
                    reticles.find(
                        "KISAK_SP_VR_FLAT_CROSSHAIR_SUPPRESSION_V88")) !=
                    std::string::npos,
            "issue #51 V88 must suppress the legacy center-screen weapon crosshair whenever VR is active, including old profiles");
        Check(
            gameClientScript.find(
                "KISAK_SP_VR_AIR_SUPPORT_WEAPON_IDENTITY_V91") !=
                    std::string::npos &&
                gameClientScript.find(
                    "KISAK_SP_VR_AIR_SUPPORT_SCRIPT_AIM_V91") !=
                    std::string::npos &&
                gameClientScript.find(
                    "\"cobra_air_support\"") !=
                    std::string::npos &&
                gameClientScript.find(
                    "\"airstrike_support\"") !=
                    std::string::npos &&
                gameClientScript.find(
                    "G_VR_IsStockAirSupportDesignator(") !=
                    std::string::npos &&
                gameClientScript.find(
                    "VR_GetRightControllerWeaponCommand(") !=
                    std::string::npos &&
                gameClientScript.find(
                    "Scr_AddVector(vrAirSupportAngles);") !=
                    std::string::npos &&
                gameClientScript.find(
                    "[VR][ISSUE45][AIR SUPPORT]") !=
                    std::string::npos,
            "issue #45 V91 must identify Safehouse/Heat's exact stock non-binocular support assets and expose the final tracked designator ray through getplayerangles");
        Check(
            gamePlayerUse.find(
                "KISAK_SP_VR_AIR_SUPPORT_WEAPON_IDENTITY_V91") !=
                    std::string::npos &&
                gamePlayerUse.find(
                    "KISAK_SP_VR_AIR_SUPPORT_LOOKAT_AIM_V91") !=
                    std::string::npos &&
                gamePlayerUse.find(
                    "\"cobra_air_support\"") !=
                    std::string::npos &&
                gamePlayerUse.find(
                    "\"airstrike_support\"") !=
                    std::string::npos &&
                gamePlayerUse.find(
                    "G_VR_IsStockAirSupportDesignator(") !=
                    std::string::npos &&
                gamePlayerUse.find(
                    "VR_GetRightControllerWeaponCommand(") !=
                    std::string::npos &&
                gamePlayerUse.find(
                    "AngleVectors(") !=
                    std::string::npos &&
                gamePlayerUse.find(
                    "Native look-at now") !=
                    std::string::npos,
            "issue #45 V91 must keep the native look-at fallback on the same exact support assets and tracked designator direction");
        Check(
            weapons.find(
                "KISAK_SP_VR_AIR_SUPPORT_HAND_GEOMETRY_V91") !=
                    std::string::npos &&
                weapons.find(
                    "VR_BuildHiddenWeaponHandModel(") !=
                    std::string::npos &&
                weapons.find(
                    "VR_CreateDegenerateSurfaceIndexList(") !=
                    std::string::npos &&
                weapons.find(
                    "\"cobra_air_support\"") !=
                    std::string::npos &&
                weapons.find(
                    "\"airstrike_support\"") !=
                    std::string::npos &&
                CountOccurrences(
                    weapons,
                    "VR_GetViewmodelHandModel(") == 3u &&
                weapons.find(
                    "return VR_GetRightOnlyWeaponHandModel(") !=
                    std::string::npos,
            "issue #45 V91 must hide the canned hand surfaces only for Safehouse/Heat's exact support assets at both DObj creation sites while retaining ordinary tracked-hand fallback for every other weapon");
        Check(
            weapons.find(
                "KISAK_SP_VR_AIR_SUPPORT_RIGHT_HAND_V92") !=
                    std::string::npos &&
                CountOccurrences(
                    weapons,
                    "VR_IsStockAirSupportDesignator(") == 4u &&
                weapons.find(
                    "VR_BuildAirSupportRightHandModel(") !=
                    std::string::npos &&
                weapons.find(
                    "\"j_shoulder_ri\"") !=
                    std::string::npos &&
                weapons.find(
                    "\"j_wristtwist_ri\"") !=
                    std::string::npos &&
                weapons.find(
                    "VR_WeaponUsesHiddenAirSupportHandModel(") !=
                    std::string::npos &&
                weapons.find(
                    "CG_DObjGetWorldTagMatrix(",
                    weapons.find(
                        "VR_AddAirSupportRightHandToScene(")) !=
                    std::string::npos &&
                weapons.find(
                    "(void)airSupportRightHandRendered;") !=
                    std::string::npos,
            "issue #45 V92/V93/V94/V95 must preserve the exact V91 designator classifier, right-glove extraction, and live right-wrist submission scaffolding");
        const std::size_t v93AirSupportSceneStart =
            weapons.find(
                "static bool VR_AddAirSupportRightHandToScene(");
        const std::size_t v93AirSupportSceneEnd =
            weapons.find(
                "static void VR_HandQuaternionToAxis(",
                v93AirSupportSceneStart);
        const std::string v93AirSupportScene =
            v93AirSupportSceneStart != std::string::npos &&
                    v93AirSupportSceneEnd > v93AirSupportSceneStart
                ? weapons.substr(
                      v93AirSupportSceneStart,
                      v93AirSupportSceneEnd -
                          v93AirSupportSceneStart)
                : std::string();
        Check(
            weapons.find(
                "KISAK_SP_VR_AIR_SUPPORT_STABLE_GLOVE_V93") !=
                    std::string::npos &&
                weapons.find(
                    "s_vrAirSupportStableRightHandDonorModel") !=
                    std::string::npos &&
                weapons.find(
                    "weaponDef->weapType == WEAPTYPE_BULLET") !=
                    std::string::npos &&
                weapons.find(
                    "KISAK_SP_VR_AIR_SUPPORT_NO_POSE_BAKE_V93") !=
                    std::string::npos &&
                CountOccurrences(
                    weapons,
                    "VR_BakeAirSupportRightHandGripPose(") == 1u &&
                v93AirSupportScene.find(
                    "VR_BakeAirSupportRightHandGripPose(") ==
                    std::string::npos &&
                v93AirSupportScene.find(
                    "R_SkinXSurfaceWeight(") ==
                    std::string::npos &&
                v93AirSupportScene.find(
                    "stableGloveSourceModel") !=
                    std::string::npos &&
                v93AirSupportScene.find(
                    "V93 submitted an unskinned") !=
                    std::string::npos &&
                v93AirSupportScene.find(
                    "no support hand skinning or canned arm geometry") !=
                    std::string::npos,
            "issue #45 V93 must use an unskinned bind-pose right-glove donor at the proven live wrist and must never invoke V92's corrupt support-animation bake");
        const std::size_t v94AirSupportDeviceStart =
            weapons.find(
                "static bool VR_AddAirSupportDeviceToScene(");
        const std::size_t v94AirSupportDeviceEnd =
            weapons.find(
                "static bool VR_CreateAirSupportRightHandObject(",
                v94AirSupportDeviceStart);
        const std::string v94AirSupportDeviceScene =
            v94AirSupportDeviceStart != std::string::npos &&
                    v94AirSupportDeviceEnd > v94AirSupportDeviceStart
                ? weapons.substr(
                      v94AirSupportDeviceStart,
                      v94AirSupportDeviceEnd -
                          v94AirSupportDeviceStart)
                : std::string();
        Check(
            weapons.find(
                "KISAK_SP_VR_AIR_SUPPORT_DEVICE_RESTORE_V94") !=
                    std::string::npos &&
                weapons.find(
                    "struct VrAirSupportDeviceAsset") !=
                    std::string::npos &&
                weapons.find(
                    "s_vrAirSupportDeviceAssets") !=
                    std::string::npos &&
                v94AirSupportDeviceScene.find(
                    "VR_IsStockAirSupportDesignator(") !=
                    std::string::npos &&
                v94AirSupportDeviceScene.find(
                    "DObjGetNumModels(") !=
                    std::string::npos &&
                v94AirSupportDeviceScene.find(
                    "DObjGetModel(") !=
                    std::string::npos &&
                v94AirSupportDeviceScene.find(
                    "scr_const.tag_weapon") !=
                    std::string::npos &&
                v94AirSupportDeviceScene.find(
                    "CG_DObjGetWorldTagMatrix(") !=
                    std::string::npos &&
                v94AirSupportDeviceScene.find(
                    "R_AddDObjToSceneUntracked(") !=
                    std::string::npos &&
                v94AirSupportDeviceScene.find(
                    "V94 restored standalone") !=
                    std::string::npos &&
                weapons.find(
                    "const bool airSupportDeviceRendered =") !=
                    std::string::npos &&
                weapons.find(
                    "(void)airSupportDeviceRendered;") !=
                    std::string::npos &&
                weapons.find(
                    "for (VrAirSupportDeviceAsset& asset") !=
                    std::string::npos,
            "issue #45 V94 must preserve its model-slot-1 tag_weapon fallback without changing V93's stable glove or tracked targeting");
        const std::size_t v95EmbeddedDeviceStart =
            weapons.find(
                "static bool VR_BuildHiddenWeaponHandModel(");
        const std::size_t v95EmbeddedDeviceEnd =
            weapons.find(
                "static XModel* VR_GetHiddenWeaponHandModel(",
                v95EmbeddedDeviceStart);
        const std::string v95EmbeddedDeviceBuilder =
            v95EmbeddedDeviceStart != std::string::npos &&
                    v95EmbeddedDeviceEnd > v95EmbeddedDeviceStart
                ? weapons.substr(
                      v95EmbeddedDeviceStart,
                      v95EmbeddedDeviceEnd -
                          v95EmbeddedDeviceStart)
                : std::string();
        Check(
            weapons.find(
                "KISAK_SP_VR_AIR_SUPPORT_EMBEDDED_DEVICE_V95") !=
                    std::string::npos &&
                weapons.find(
                    "VR_SelectNonArmDeviceTriangle(") !=
                    std::string::npos &&
                weapons.find(
                    "combinedArmWeightSum < 0.50f") !=
                    std::string::npos &&
                weapons.find(
                    "VR_CreateNonArmDeviceIndexList(") !=
                    std::string::npos &&
                v95EmbeddedDeviceBuilder.find(
                    "\"j_shoulder_le\"") !=
                    std::string::npos &&
                v95EmbeddedDeviceBuilder.find(
                    "\"j_shoulder_ri\"") !=
                    std::string::npos &&
                v95EmbeddedDeviceBuilder.find(
                    "if (!sourceSurface.deformed)") !=
                    std::string::npos &&
                CountOccurrences(
                    v95EmbeddedDeviceBuilder,
                    "VR_CalculateSplitHandVertexWeights(") == 2u &&
                v95EmbeddedDeviceBuilder.find(
                    "VR_CreateNonArmDeviceIndexList(") !=
                    std::string::npos &&
                v95EmbeddedDeviceBuilder.find(
                    "preservedRigidTriangleCount") !=
                    std::string::npos &&
                v95EmbeddedDeviceBuilder.find(
                    "preservedWeightedTriangleCount") !=
                    std::string::npos &&
                v95EmbeddedDeviceBuilder.find(
                    "suppressedArmTriangleCount") !=
                    std::string::npos &&
                v95EmbeddedDeviceBuilder.find(
                    "V95 preserved %u embedded") !=
                    std::string::npos &&
                weapons.find(
                    "R_AddDObjToScene(weapInfo->viewModelDObj") !=
                    std::string::npos,
            "issue #45 V95 must preserve rigid and non-arm device triangles embedded in composite slot 0 while suppressing both canned arms and retaining the authored viewmodel submission");
        Check(
            runtime.find(
                "KISAK_SP_VR_OPENXR_DXGI_1_1_FACTORY_V84") !=
                    std::string::npos &&
                runtime.find(
                    "ComPtr<IDXGIFactory1> factory;") !=
                    std::string::npos &&
                runtime.find(
                    "CreateDXGIFactory1(") !=
                    std::string::npos &&
                runtime.find(
                    "ComPtr<IDXGIAdapter1> selectedAdapter;") !=
                    std::string::npos &&
                runtime.find(
                    "factory->EnumAdapters1(") !=
                    std::string::npos &&
                runtime.find(
                    "DXGI_ADAPTER_DESC1 description") !=
                    std::string::npos &&
                runtime.find(
                    "candidateAdapter->GetDesc1(") !=
                    std::string::npos &&
                runtime.find(
                    "CreateDXGIFactory(") ==
                    std::string::npos &&
                runtime.find(
                    "V84 created the D3D11 device through") !=
                    std::string::npos,
            "issues #4/#20 V84 must create the OpenXR D3D11 device through DXGI 1.1 so SteamVR can import submitted eye textures");
        const std::size_t rawAttackGetter =
            runtime.find(
                "bool VR_GetConfiguredAttackButton(");
        const std::size_t normalWeaponGetter =
            runtime.find(
                "bool VR_GetRightControllerWeaponCommand(");
        Check(
            rawAttackGetter != std::string::npos &&
                normalWeaponGetter != std::string::npos &&
                rawAttackGetter < normalWeaponGetter &&
                runtime.find(
                    "g_vrRightControllerAttackPressed;",
                    rawAttackGetter) != std::string::npos &&
                clientInput.find(
                    "KISAK_SP_VR_SCRIPTED_DETONATOR_TRIGGER_REPAIR_V68") !=
                    std::string::npos &&
                clientInput.find(
                    "WeaponRequiresPoseIndependentAttack") !=
                    std::string::npos &&
                clientInput.find(
                    "activeWeaponDef->hasDetonator != 0") !=
                    std::string::npos &&
                clientInput.find(
                    "[VR][DETONATOR] Routed configured Attack") !=
                    std::string::npos &&
                clientInput.find(
                    "else if (VR_GetRightControllerWeaponCommand(") !=
                    std::string::npos,
            "issue #18 V68 must route raw Attack only for semantic detonators before preserving the normal aimed-weapon path");
        Check(
            runtime.find(
                "bool VR_SupportGripUsesAutomaticProximity()") !=
                    std::string::npos &&
                runtime.find(
                    "VrInteractions::SupportGripMode::Proximity") !=
                    std::string::npos &&
                weapons.find(
                    "automaticProximity &&") !=
                    std::string::npos &&
                weapons.find(
                    "SupportGripProximityQualifies") !=
                    std::string::npos &&
                weapons.find(
                    "automaticProximity\n            ? insideGripRadius") !=
                    std::string::npos,
            "issue #26 V67: only automatic proximity must re-evaluate the finite support-hand release radius every frame");
        Check(
            runtime.find(
                "KISAK_SP_VR_EYE_LOCAL_HUD_ALIGNMENT_V82") !=
                    std::string::npos &&
                runtime.find(
                    "const int uiEyeWidth") !=
                    std::string::npos &&
                runtime.find(
                    "int mainStereoWidth = displayWidth") !=
                    std::string::npos &&
                runtime.find(
                    "mainStereoWidth / 2") !=
                    std::string::npos &&
                CountOccurrences(
                    runtime,
                    "uiEyeWidth,\n        displayHeight);") == 3u &&
                CountOccurrences(
                    runtime,
                    "VR_UpdatePackedUiScreenPlacement();") == 4u &&
                runtime.find(
                    "one %d x %d eye") !=
                    std::string::npos &&
                rendererScene.find(
                    "viewInfoIndex == 1") !=
                    std::string::npos &&
                rendererScene.find(
                    "frontEndDataOut->viewInfo[0].cmds") !=
                    std::string::npos &&
                compass.find(
                    "TransformCompassRect") !=
                    std::string::npos &&
                compass.find(
                    "SP compass and editor share center") !=
                    std::string::npos,
            "issue #22 V82 must author shared HUD commands in one-eye space and route the real compass through the editor's canonical transform");
        Check(
            screenPlacement.find("layout.ammoOffsetX") !=
                    std::string::npos &&
                screenPlacement.find("layout.ammoScale") !=
                    std::string::npos &&
                compass.find("TransformCompassRect") !=
                    std::string::npos &&
                messages.find("layout.objectiveOffsetX") !=
                    std::string::npos &&
                messages.find("layout.subtitleScale") !=
                    std::string::npos &&
                draw.find("VR_DrawHudEditorOverlay") !=
                    std::string::npos &&
                draw.find("CANCEL (B)") != std::string::npos &&
                draw.find("SAVE (A)") != std::string::npos &&
                runtime.find(
                    "KISAK_SP_VR_HUD_EDITOR_RECOVERY_V69") !=
                    std::string::npos &&
                runtime.find("VrInput::Action::Use") !=
                    std::string::npos &&
                runtime.find("VrInput::Action::NextWeapon") !=
                    std::string::npos &&
                runtime.find("VrHud::CycleElement") !=
                    std::string::npos &&
                runtime.find("VrHud::CenterElement") !=
                    std::string::npos &&
                runtime.find("VrHud::ResetElement") !=
                    std::string::npos &&
                runtime.find("GetAsyncKeyState(VK_TAB)") !=
                    std::string::npos &&
                runtime.find("GetAsyncKeyState(VK_HOME)") !=
                    std::string::npos &&
                runtime.find("GetAsyncKeyState(VK_END)") !=
                    std::string::npos &&
                draw.find("SELECTED HUD GROUP") !=
                    std::string::npos &&
                draw.find("Home centers") != std::string::npos &&
                draw.find("End resets selected only") !=
                    std::string::npos,
            "all five visual boxes should drive the corresponding live mission HUD paths");
        Check(
            cgameMain.find(
                "Dvar_RegisterEnum(\"cg_drawFPS\", cg_drawFpsNames, 0") !=
                    std::string::npos &&
                cgameMain.find("Dvar_SetInt(cg_drawFPS, 0)") !=
                    std::string::npos &&
                debugDraw.find("if (!com_statmon->current.enabled)") !=
                    std::string::npos &&
                debugDraw.find(
                    "if (!cg_drawPerformanceWarnings->current.enabled)") !=
                    std::string::npos,
            "V61 should keep the diagnostic FPS/stat overlays opt-in");
        Check(
            weapons.find("vrCalibrationWeapon->szInternalName") !=
                    std::string::npos &&
                weapons.find("vrCalibrationWeapon->szDisplayName") !=
                    std::string::npos &&
                weapons.find("ps->fWeaponPosFrac") !=
                    std::string::npos,
            "V63 should pass stable weapon identity and ADS blend into the runtime calibration path");
    }

    if (argumentCount >= 5)
    {
        const std::string configurator = Read(arguments[4]);
        const std::size_t v96MainWindowStart =
            configurator.find("LRESULT CALLBACK MainWindowProc(");
        const std::size_t v96MainWindowSwitch =
            configurator.find("switch (message)", v96MainWindowStart);
        const std::string v96MainWindowSetup =
            v96MainWindowStart != std::string::npos &&
                    v96MainWindowSwitch > v96MainWindowStart
                ? configurator.substr(
                      v96MainWindowStart,
                      v96MainWindowSwitch - v96MainWindowStart)
                : std::string();
        Check(
            configurator.find(
                "KISAK_VR_CONFIGURATOR_RESIZABLE_WINDOW_V96") !=
                    std::string::npos &&
                configurator.find(
                    "KisakCODVrConfiguratorResizableV96") !=
                    std::string::npos &&
                configurator.find(
                    "constexpr int kWindowClientWidth = 1160;") !=
                    std::string::npos &&
                configurator.find(
                    "constexpr int kWindowClientHeight = 750;") !=
                    std::string::npos &&
                configurator.find("WS_THICKFRAME") !=
                    std::string::npos &&
                configurator.find("WS_MAXIMIZEBOX") !=
                    std::string::npos &&
                configurator.find("AdjustWindowRectEx(") !=
                    std::string::npos &&
                CountOccurrences(
                    configurator,
                    "WM_GETMINMAXINFO") == 1u &&
                v96MainWindowSetup.find("WM_GETMINMAXINFO") !=
                    std::string::npos &&
                v96MainWindowSetup.find("ptMinTrackSize.x") !=
                    std::string::npos &&
                v96MainWindowSetup.find("ptMinTrackSize.y") !=
                    std::string::npos &&
                configurator.find("mainWindowSize.cx") !=
                    std::string::npos &&
                configurator.find("mainWindowSize.cy") !=
                    std::string::npos,
            "issue #53 V96 must make the Configurator resizable and guarantee enough initial/minimum client area for every rightmost and bottom control");
        Check(
            configurator.find("Setup & Compatibility") !=
                    std::string::npos &&
                configurator.find("v0.10.0-beta.14") !=
                    std::string::npos &&
                configurator.find("Rescan system") !=
                    std::string::npos &&
                configurator.find("Apply recommended") !=
                    std::string::npos &&
                configurator.find("pimax_crystal_light") !=
                    std::string::npos &&
                configurator.find("7924x4082") !=
                    std::string::npos &&
                configurator.find("7684x3128") !=
                    std::string::npos &&
                configurator.find("Copy support report") !=
                    std::string::npos &&
                configurator.find("--compatibility-report") !=
                    std::string::npos &&
                configurator.find("Handedness, units, comfort, controls, HUD, weapon profiles, and calibration will not change") !=
                    std::string::npos &&
                configurator.find("Height & Recenter") != std::string::npos &&
                configurator.find("Beta.11 HUD recovery") !=
                    std::string::npos &&
                configurator.find("recenter, VR prompts, and F.N.G. repair") !=
                    std::string::npos &&
                configurator.find("Recenter position only") !=
                    std::string::npos &&
                configurator.find("Recenter direction / level only") !=
                    std::string::npos &&
                configurator.find("Full recenter") !=
                    std::string::npos &&
                configurator.find("Measure standing height") !=
                    std::string::npos &&
                configurator.find("Apply seated + recenter position") !=
                    std::string::npos &&
                configurator.find("direction and level were preserved") !=
                    std::string::npos &&
                configurator.find("the positional origin was preserved") !=
                    std::string::npos &&
                configurator.find("1 cm shorter") != std::string::npos &&
                configurator.find("1 cm taller") != std::string::npos &&
                configurator.find("1 in shorter") != std::string::npos &&
                configurator.find("1 in taller") != std::string::npos &&
                configurator.find("lastRenderedCanonicalValue") !=
                    std::string::npos &&
                configurator.find("DisplayValueToCanonical") !=
                    std::string::npos &&
                configurator.find("displayDelta / 2.54") !=
                    std::string::npos &&
                configurator.find("Open desktop visual editor") !=
                    std::string::npos &&
                configurator.find("Edit live in headset") !=
                    std::string::npos &&
                configurator.find("Apply layout") !=
                    std::string::npos &&
                configurator.find("Center selected element") !=
                    std::string::npos &&
                configurator.find("Previous") !=
                    std::string::npos &&
                configurator.find("Next") !=
                    std::string::npos &&
                configurator.find("Snap anchors: ON") !=
                    std::string::npos &&
                configurator.find("Open calibration editor") !=
                    std::string::npos &&
                configurator.find("Use equipped weapon") !=
                    std::string::npos &&
                configurator.find("per-weapon shouldered/ADS delta") !=
                    std::string::npos &&
                configurator.find("Apply live") !=
                    std::string::npos &&
                configurator.find("Guided aim capture") !=
                    std::string::npos &&
                configurator.find("*.vrstock") !=
                    std::string::npos,
            "the beta.14 menu should retain compatibility, handed interactions, weapon/gunstock, metric, calibration, and both visual HUD workflows");
    }

    if (argumentCount >= 6)
    {
        const std::string mountedTurretPose = Read(arguments[5]);
        Check(
            mountedTurretPose.find(
                "KISAK_SP_VR_MOUNTED_TURRET_MODEL_AIM_V87") !=
                    std::string::npos &&
                mountedTurretPose.find(
                    "VR_GetRightControllerMountedWeaponAim(") !=
                    std::string::npos &&
                mountedTurretPose.find(
                    "CG_PlayerUsingScopedTurret(0)") !=
                    std::string::npos,
            "V87 mounted-gun model aim must use the live right-controller ray while explicitly excluding the HMD-centered fixed scope");
        Check(
            mountedTurretPose.find("viewAngleClampBase[0]") !=
                    std::string::npos &&
                mountedTurretPose.find("viewAngleClampBase[1]") !=
                    std::string::npos &&
                mountedTurretPose.find("viewAngleClampRange[0]") !=
                    std::string::npos &&
                mountedTurretPose.find("viewAngleClampRange[1]") !=
                    std::string::npos &&
                mountedTurretPose.find(
                    "AngleDelta(clampedPitch, pose->angles[0])") !=
                    std::string::npos &&
                mountedTurretPose.find(
                    "AngleDelta(clampedYaw, pose->angles[1])") !=
                    std::string::npos,
            "V87 visible mounted-gun pitch/yaw must share the replicated mechanical clamp and convert back to model-local angles");
        Check(
            mountedTurretPose.find(
                "[VR][TURRET][V87] Visible mounted-gun tag_aim follows") !=
                    std::string::npos &&
                mountedTurretPose.find(
                    "pose->turret.viewAngles") !=
                    std::string::npos,
            "V87 must expose a diagnostic marker and retain the native camera-angle fallback when VR aim is unavailable");
    }

    if (argumentCount >= 9)
    {
        const std::string installer = Read(arguments[6]);
        const std::string packager = Read(arguments[7]);
        const std::string installerBuilder = Read(arguments[8]);
        Check(
            installer.find(
                "KISAKCOD_VR_GUARDED_CLASSIC_INSTALLER_V97") !=
                    std::string::npos &&
                installer.find(
                    "#define MyAppId \"{{8A7413D7-9D08-4C56-8E75-9C2E6F4D1701}\"") !=
                    std::string::npos &&
                installer.find("AppId={#MyAppId}") !=
                    std::string::npos &&
                installer.find("PrivilegesRequired=admin") !=
                    std::string::npos &&
                installer.find(
                    "PrivilegesRequiredOverridesAllowed=commandline") !=
                    std::string::npos &&
                installer.find("UsePreviousAppDir=yes") !=
                    std::string::npos &&
                installer.find("FindGameInSteamLibraryFile") !=
                    std::string::npos &&
                installer.find("libraryfolders.vdf") !=
                    std::string::npos &&
                installer.find("Steam App 7940") !=
                    std::string::npos,
            "V97 installer must retain one stable upgrade identity, elevate for Program Files, and detect both the registered and additional Steam libraries");
        Check(
            installer.find("IsValidClassicInstall") !=
                    std::string::npos &&
                installer.find("iw3sp.exe") != std::string::npos &&
                installer.find("localization.txt") !=
                    std::string::npos &&
                installer.find("main\\*.iwd") !=
                    std::string::npos &&
                installer.find("\\code_post_gfx.ff") !=
                    std::string::npos &&
                installer.find(
                    "Microsoft/Xbox automatic raw-layout") !=
                    std::string::npos &&
                installer.find("No game or mod files were changed.") !=
                    std::string::npos,
            "V97 installer must reject incomplete, wrong-language, and not-yet-mapped Microsoft layouts before changing the game folder");
        Check(
            installer.find("PreparePayloadBackups") !=
                    std::string::npos &&
                installer.find("original-files.txt") !=
                    std::string::npos &&
                installer.find("managed-files.txt") !=
                    std::string::npos &&
                installer.find("backup-complete.txt") !=
                    std::string::npos &&
                installer.find("GetSHA256OfFile(TargetPath)") !=
                    std::string::npos &&
                installer.find("GetSHA256OfFile(TemporaryBackup)") !=
                    std::string::npos &&
                installer.find("RestoreOriginalFiles") !=
                    std::string::npos &&
                installer.find("usPostUninstall") !=
                    std::string::npos &&
                installer.find("install-receipt.txt") !=
                    std::string::npos &&
                installer.find("[InstallDelete]") ==
                    std::string::npos,
            "V97 install, update/repair, and uninstall must preserve every pre-existing managed path through verified incremental backups without broad deletion");
        Check(
            packager.find(
                "KISAKCOD_VR_SHARED_ZIP_INSTALLER_PAYLOAD_V97") !=
                    std::string::npos &&
                packager.find("--portable-only") !=
                    std::string::npos &&
                packager.find("tools\" / \"build_installer.py") !=
                    std::string::npos &&
                packager.find("-Setup.exe") !=
                    std::string::npos &&
                packager.find("installer builder did not produce") !=
                    std::string::npos &&
                packager.find(
                    "Path(name).name.lower() == \"iw3sp.exe\"") !=
                    std::string::npos,
            "V97 publisher must build the portable ZIP and Setup from the exact same allowlisted payload while continuing to forbid original COD4 data");
        Check(
            installerBuilder.find("safe_payload_inventory") !=
                    std::string::npos &&
                installerBuilder.find("case-insensitive payload collision") !=
                    std::string::npos &&
                installerBuilder.find("INNO_SETUP_COMPILER") !=
                    std::string::npos &&
                installerBuilder.find("Inno Setup 7") !=
                    std::string::npos &&
                installerBuilder.find("Inno Setup 6") !=
                    std::string::npos &&
                installerBuilder.find("payload-manifest.txt") !=
                    std::string::npos &&
                installerBuilder.find("/DMyManifestSha256=") !=
                    std::string::npos &&
                installerBuilder.find("executable.read(2) != b\"MZ\"") !=
                    std::string::npos &&
                installerBuilder.find(".sha256") !=
                    std::string::npos,
            "V97 installer builder must validate the payload namespace, compile through a discovered ISCC, and verify the resulting Windows executable and checksum sidecar");
    }

    const std::string mixed =
        "@echo off\r\n"
        "set \"KISAK_VR_TURN_MODE=smooth\"\n"
        " set KISAK_VR_SMOOTH_TURN_SPEED=180\r\n"
        "set \"UNKNOWN_SETTING=ignored\"\r\n";
    const kc::SettingsMap parsed = kc::ParseBatchSettings(mixed);
    Check(parsed.at("KISAK_VR_TURN_MODE") == "smooth", "quoted CRLF assignment should parse");
    Check(parsed.at("KISAK_VR_SMOOTH_TURN_SPEED") == "180", "unquoted assignment should parse");
    Check(parsed.count("UNKNOWN_SETTING") == 0u, "unknown settings should be ignored");

    const kc::SettingsMap legacyBindings = kc::ParseBatchSettings(
        "set \"KISAK_VR_BIND_USE=x\"\r\n"
        "set \"KISAK_VR_BIND_SPRINT=stick\"\r\n"
        "set \"KISAK_VR_BIND_NEXT_WEAPON=y\"\r\n"
        "set \"KISAK_VR_BIND_RELOAD=a\"\r\n"
        "set \"KISAK_VR_BIND_MELEE=stick\"\r\n"
        "set \"KISAK_VR_BIND_STANCE=b\"\r\n");
    Check(
        legacyBindings.at("KISAK_VR_BIND_USE") == "left.primary",
        "beta.7 X binding should migrate to left.primary");
    Check(
        legacyBindings.at("KISAK_VR_BIND_SPRINT") ==
            "left.thumbstick_click",
        "beta.7 left stick binding should migrate");
    Check(
        legacyBindings.at("KISAK_VR_BIND_RELOAD") == "right.primary",
        "beta.7 A binding should migrate to right.primary");
    Check(
        legacyBindings.at("KISAK_VR_BIND_MELEE") ==
            "right.thumbstick_click",
        "beta.7 right stick binding should migrate");

    std::vector<kc::ValidationMessage> unsafeMessages;
    const kc::SettingsMap unsafe = kc::ParseBatchSettings(
        "set \"KISAK_VR_TURN_MODE=smooth&calc\"\r\n",
        &unsafeMessages);
    Check(unsafe.empty(), "unsafe batch values should not be accepted");
    Check(HasError(unsafeMessages, "KISAK_VR_TURN_MODE"), "unsafe value should produce an error");

    values["VR_CUSTOM_MODE"] = "4768x2016";
    messages = kc::ValidateSettings(values);
    Check(HasError(messages, "KISAK_VR_OUTPUT_SCALE"), "lower packed mode must require 0.75 scale");
    values["KISAK_VR_OUTPUT_SCALE"] = "0.75";
    messages = kc::ValidateSettings(values);
    Check(!HasError(messages, "KISAK_VR_OUTPUT_SCALE"), "verified lower packed pair should validate");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_OUTPUT_SCALE"] = "0.75";
    messages = kc::ValidateSettings(values);
    Check(HasError(messages, "KISAK_VR_OUTPUT_SCALE"), "native packed mode must require 1.00 scale");

    values = kc::BuiltInDefaults();
    values["VR_CUSTOM_MODE"] = "7684x3128";
    messages = kc::ValidateSettings(values);
    Check(
        messages.empty(),
        "issue #41 cropped-FOV Pimax mode should remain available with 1.00 output and a 1024 px scope panel");
    values["KISAK_VR_OUTPUT_SCALE"] = "0.75";
    messages = kc::ValidateSettings(values);
    Check(
        HasError(messages, "KISAK_VR_OUTPUT_SCALE"),
        "issue #41 cropped-FOV Pimax mode must reject the 0.75 output pair");
    values["KISAK_VR_OUTPUT_SCALE"] = "1.00";
    values["KISAK_VR_SCOPE_CAPTURE_SIZE"] = "1280";
    messages = kc::ValidateSettings(values);
    Check(
        HasError(messages, "KISAK_VR_SCOPE_CAPTURE_SIZE"),
        "issue #41 cropped-FOV Pimax mode must keep the scope panel within its reserved 1024 px region");

    values = kc::BuiltInDefaults();
    values["VR_CUSTOM_MODE"] = "7924x4082";
    values["KISAK_VR_OUTPUT_SCALE"] = "0.80";
    messages = kc::ValidateSettings(values);
    Check(
        messages.empty(),
        "issue #41 V89 Pimax full-FOV mode should validate with 0.80 output and a 1024 px scope panel");
    values["KISAK_VR_OUTPUT_SCALE"] = "1.00";
    messages = kc::ValidateSettings(values);
    Check(
        HasError(messages, "KISAK_VR_OUTPUT_SCALE"),
        "issue #41 V89 Pimax full-FOV mode must reject the 1.00 output pair");
    values["KISAK_VR_OUTPUT_SCALE"] = "0.80";
    values["KISAK_VR_SCOPE_CAPTURE_SIZE"] = "1280";
    messages = kc::ValidateSettings(values);
    Check(
        HasError(messages, "KISAK_VR_SCOPE_CAPTURE_SIZE"),
        "issue #41 V89 Pimax full-FOV mode must keep the scope panel within its reserved 1024 px region");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_BIND_USE"] = "left.secondary";
    messages = kc::ValidateSettings(values);
    Check(
        !HasError(messages, "KISAK_VR_BIND_USE"),
        "duplicate bindings should no longer be rejected");
    Check(
        HasWarning(messages, "KISAK_VR_BIND_USE"),
        "duplicate gameplay bindings should produce a warning");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_BIND_USE"] = "right.auxiliary";
    messages = kc::ValidateSettings(values);
    Check(
        !HasError(messages, "KISAK_VR_BIND_USE"),
        "an action should accept a compatible input from either hand");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_BIND_MOVE_AXIS"] = "left.trigger";
    messages = kc::ValidateSettings(values);
    Check(
        HasError(messages, "KISAK_VR_BIND_MOVE_AXIS"),
        "a button source should not validate as a movement axis");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_BIND_USE"] =
        "right.thumbrest_touch+left.primary_axis.down";
    messages = kc::ValidateSettings(values);
    Check(
        !HasError(messages, "KISAK_VR_BIND_USE"),
        "a Boolean action should accept a cross-controller directional chord");

    values["KISAK_VR_BIND_USE"] =
        "right.thumbrest_touch+right.thumbrest_touch";
    messages = kc::ValidateSettings(values);
    Check(
        HasError(messages, "KISAK_VR_BIND_USE"),
        "a chord should reject duplicate terms");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_BIND_USE"] =
        "left.primary+left.secondary+right.primary+right.secondary";
    messages = kc::ValidateSettings(values);
    Check(
        !HasError(messages, "KISAK_VR_BIND_USE"),
        "a four-input chord should validate");
    values["KISAK_VR_BIND_USE"] += "+left.trigger";
    messages = kc::ValidateSettings(values);
    Check(
        HasError(messages, "KISAK_VR_BIND_USE"),
        "a chord should reject a fifth input");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_BIND_MOVE_AXIS"] =
        "left.primary_axis+right.primary_axis";
    messages = kc::ValidateSettings(values);
    Check(
        HasError(messages, "KISAK_VR_BIND_MOVE_AXIS"),
        "an analog slot should reject multi-axis chords");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_BELT_HIP_DISTANCE"] = "8.0";
    values["KISAK_VR_BELT_GRAB_RADIUS"] = "8.0";
    messages = kc::ValidateSettings(values);
    Check(HasError(messages, "KISAK_VR_BELT_GRAB_RADIUS"), "overlapping belt zones should be rejected");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_RELOAD_EJECT_MODE"] = "pull";
    values["KISAK_VR_RELOAD_PULL_DISTANCE"] = "6.0";
    messages = kc::ValidateSettings(values);
    Check(
        HasError(messages, "KISAK_VR_RELOAD_PULL_DISTANCE"),
        "physical pull distance should remain outside the magazine-well insertion radius");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_GRENADE_MIN_STRENGTH"] = "1.00";
    values["KISAK_VR_GRENADE_MAX_STRENGTH"] = "0.80";
    messages = kc::ValidateSettings(values);
    Check(HasError(messages, "KISAK_VR_GRENADE_MAX_STRENGTH"), "inverted grenade strength range should be rejected");

    values = kc::BuiltInDefaults();
    Check(kc::ApplyPreset("Performance", &values), "performance preset should exist");
    Check(values["VR_CUSTOM_MODE"] == "4768x2016", "performance preset should use lower packed mode");
    Check(values["KISAK_VR_OUTPUT_SCALE"] == "0.75", "performance preset should use 0.75 output scale");
    Check(values["KISAK_VR_FSR"] == "1", "performance preset should enable FSR");
    Check(kc::ValidateSettings(values).empty(), "performance preset should validate cleanly");

    values = kc::BuiltInDefaults();
    values["KISAK_VR_HUD_SAFE_X"] = "0.75";
    Check(
        kc::ApplyPreset("Pimax Crystal Light", &values),
        "issue #41 V89 Pimax Crystal Light preset should exist");
    Check(
        values["VR_CUSTOM_MODE"] == "7924x4082" &&
            values["KISAK_VR_OUTPUT_SCALE"] == "0.80" &&
            values["KISAK_VR_FSR"] == "0" &&
            values["KISAK_VR_SCOPE_CAPTURE_SIZE"] == "1024" &&
            values["KISAK_VR_HUD_SAFE_X"] == "0.75",
        "issue #41 V89 Pimax preset should reserve 3450 + 3450 + 1024 full-FOV pixels without replacing personal HUD settings");
    Check(
        kc::ValidateSettings(values).empty(),
        "issue #41 V89 Pimax Crystal Light preset should validate cleanly");

    values = kc::BuiltInDefaults();
    Check(kc::ApplyPreset("Seated", &values), "seated preset should exist");
    Check(
        values["KISAK_VR_PLAY_MODE"] == "seated" &&
            values["KISAK_VR_SEATED_EYE_HEIGHT"] == "60.0",
        "seated preset should select seated calibration while preserving native virtual stature");
    Check(
        kc::ValidateSettings(values).empty(),
        "seated calibration preset should validate cleanly");

    for (const vi::SourceDefinition& source : vi::SourceDefinitions())
    {
        vi::Source parsedSource = vi::Source::Count;
        Check(
            vi::ParseSource(source.id, &parsedSource) &&
                parsedSource == source.source,
            std::string("controller source should round-trip: ") + source.id);
    }

    for (const vi::ActionDefinition& action : vi::ActionDefinitions())
    {
        const kc::SettingDefinition* primary =
            kc::FindSetting(action.settingKey);
        const kc::SettingDefinition* alternate =
            kc::FindSetting(action.alternateSettingKey);

        Check(
            primary != nullptr &&
                primary->type == kc::SettingType::Binding,
            std::string("primary binding should exist: ") +
                action.settingKey);
        Check(
            alternate != nullptr &&
                alternate->type == kc::SettingType::Binding,
            std::string("alternate binding should exist: ") +
                action.alternateSettingKey);
        vi::Binding defaultBinding;
        Check(
            vi::ParseBinding(
                action.action,
                action.defaultBinding,
                &defaultBinding),
            std::string("default binding should be compatible: ") +
                action.settingKey);
        vi::Binding alternateBinding;
        Check(
            vi::ParseBinding(
                action.action,
                action.defaultAlternateBinding,
                &alternateBinding),
            std::string("alternate default should be compatible: ") +
                action.alternateSettingKey);
    }

    Check(
        std::string(
            vi::ResolveOpenXrComponent(
                vi::OpenXrProfile::OculusTouch,
                vi::Source::LeftPrimary)) == "/input/x/click",
        "Touch left primary should resolve to X");
    Check(
        std::string(
            vi::ResolveOpenXrComponent(
                vi::OpenXrProfile::OculusTouch,
                vi::Source::RightPrimary)) == "/input/a/click",
        "Touch right primary should resolve to A");
    Check(
        std::string(
            vi::OpenXrProfileDefinitions()[
                static_cast<std::size_t>(
                    vi::OpenXrProfile::MetaTouchPro)].path) ==
            "/interaction_profiles/facebook/touch_controller_pro",
        "Touch Pro should use the OpenXR 1.0 extension profile path");
    Check(
        std::string(
            vi::ResolveOpenXrComponent(
                vi::OpenXrProfile::KhronosGeneric,
                vi::Source::LeftPrimary)) == "/input/primary/click",
        "generic controller should expose a controller-neutral primary");
    Check(
        vi::ResolveOpenXrComponent(
            vi::OpenXrProfile::HtcVive,
            vi::Source::LeftThumbstick) == nullptr,
        "Vive wand should not claim a nonexistent thumbstick");
    Check(
        std::string(
            vi::ResolveOpenXrComponent(
                vi::OpenXrProfile::HtcVive,
                vi::Source::LeftPrimaryAxis)) == "/input/trackpad",
        "Vive wand primary axis should portably fall back to its trackpad");
    Check(
        std::string(
            vi::ResolveOpenXrComponent(
                vi::OpenXrProfile::HtcVive,
                vi::Source::LeftTrackpad)) == "/input/trackpad",
        "Vive wand should expose its trackpad axis");
    Check(
        std::string(
            vi::ResolveOpenXrComponent(
                vi::OpenXrProfile::OculusTouch,
                vi::Source::LeftPrimaryAxisDown)) ==
            "/input/thumbstick",
        "Touch directional bindings should derive from the thumbstick axis");
    Check(
        std::string(
            vi::ResolveOpenXrComponent(
                vi::OpenXrProfile::HtcVive,
                vi::Source::LeftPrimaryAxisLeft)) ==
            "/input/trackpad",
        "Vive directional bindings should derive from the trackpad axis");
    Check(
        vi::DirectionalSourcePressed(
            vi::Source::RightPrimaryAxisUp,
            0.05f,
            0.81f,
            0.80f,
            0.15f),
        "the restored stance-up default should engage at its beta.7 threshold");
    Check(
        !vi::DirectionalSourceReleased(
            vi::Source::RightPrimaryAxisUp,
            0.0f,
            0.40f) &&
            vi::DirectionalSourceReleased(
                vi::Source::RightPrimaryAxisUp,
                0.0f,
                0.35f),
        "directional actions should require the beta.7 neutral release threshold");

    for (const vi::OpenXrProfileDefinition& profile :
         vi::OpenXrProfileDefinitions())
    {
        Check(
            vi::OpenXrProfileHasPalmPose(profile.profile),
            std::string("supported profile should expose XR_EXT_palm_pose: ") +
                profile.path);
    }

    std::array<vi::OpenVrHandState, 2> openVrHands = {};
    vi::OpenVrHandState& openVrLeft = openVrHands[0];
    openVrLeft.hand = vi::Hand::Left;
    openVrLeft.connected = true;
    openVrLeft.stateValid = true;
    openVrLeft.supportedButtonsKnown = true;
    openVrLeft.axisTypes.fill(vr::k_eControllerAxis_None);
    openVrLeft.axisTypes[0] = vr::k_eControllerAxis_Joystick;
    openVrLeft.axisTypes[1] = vr::k_eControllerAxis_Trigger;
    openVrLeft.supportedButtons =
        vr::ButtonMaskFromId(vr::k_EButton_A) |
        vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu) |
        vr::ButtonMaskFromId(vr::k_EButton_Grip) |
        vr::ButtonMaskFromId(vr::k_EButton_Axis0) |
        vr::ButtonMaskFromId(vr::k_EButton_Axis1);
    openVrLeft.controllerState.ulButtonPressed =
        vr::ButtonMaskFromId(vr::k_EButton_A) |
        vr::ButtonMaskFromId(vr::k_EButton_Axis0);
    openVrLeft.controllerState.ulButtonTouched =
        vr::ButtonMaskFromId(vr::k_EButton_Axis0);
    openVrLeft.controllerState.rAxis[0] = {0.25f, -0.75f};
    openVrLeft.controllerState.rAxis[1].x = 0.80f;

    bool openVrActive = false;
    Check(
        vi::GetOpenVrBooleanSourceState(
            openVrHands,
            vi::Source::LeftPrimary,
            &openVrActive) && openVrActive,
        "OpenVR A/X should resolve as the controller-neutral primary");
    Check(
        vi::GetOpenVrBooleanSourceState(
            openVrHands,
            vi::Source::LeftTrigger,
            &openVrActive) && openVrActive,
        "OpenVR analog trigger should convert to a boolean binding");
    Check(
        vi::GetOpenVrBooleanSourceState(
            openVrHands,
            vi::Source::LeftThumbstickClick,
            &openVrActive) && openVrActive,
        "OpenVR joystick click should resolve from its discovered axis");
    Check(
        vi::GetOpenVrBooleanSourceState(
            openVrHands,
            vi::Source::LeftThumbrestTouch,
            &openVrActive) && openVrActive,
        "OpenVR joystick touch should back the thumb-contact source");
    Check(
        vi::GetOpenVrBooleanSourceState(
            openVrHands,
            vi::Source::LeftPrimaryAxisDown,
            &openVrActive) && openVrActive,
        "OpenVR should expose a downward stick direction as a Boolean source");

    const vi::OpenVrVector2 openVrStick =
        vi::GetOpenVrVector2SourceState(
            openVrHands,
            vi::Source::LeftThumbstick,
            &openVrActive);
    Check(
        openVrActive && openVrStick.x == 0.25f &&
            openVrStick.y == -0.75f,
        "OpenVR joystick axis should preserve both components");

    vi::OpenVrHandState& openVrRight = openVrHands[1];
    openVrRight.hand = vi::Hand::Right;
    openVrRight.connected = true;
    openVrRight.stateValid = true;
    openVrRight.controllerType = "knuckles";
    openVrRight.supportedButtonsKnown = true;
    openVrRight.axisTypes.fill(vr::k_eControllerAxis_None);
    openVrRight.axisTypes[1] = vr::k_eControllerAxis_Trigger;
    openVrRight.axisTypes[2] = vr::k_eControllerAxis_Trigger;
    openVrRight.supportedButtons =
        vr::ButtonMaskFromId(vr::k_EButton_Grip) |
        vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu);
    openVrRight.controllerState.ulButtonPressed =
        vr::ButtonMaskFromId(vr::k_EButton_Grip);
    openVrRight.controllerState.rAxis[2].x = 0.70f;

    Check(
        vi::GetOpenVrBooleanSourceState(
            openVrHands,
            vi::Source::RightPrimary,
            &openVrActive) && openVrActive,
        "Index A should resolve through the legacy knuckles grip bit");
    Check(
        vi::GetOpenVrBooleanSourceState(
            openVrHands,
            vi::Source::RightSqueeze,
            &openVrActive) && openVrActive,
        "Index squeeze should prefer the second trigger-style axis");

    std::array<vi::OpenVrHandState, 2> viveHands = {};
    vi::OpenVrHandState& viveLeft = viveHands[0];
    viveLeft.hand = vi::Hand::Left;
    viveLeft.connected = true;
    viveLeft.stateValid = true;
    viveLeft.controllerType = "vive_controller";
    viveLeft.supportedButtonsKnown = true;
    viveLeft.axisTypes.fill(vr::k_eControllerAxis_None);
    viveLeft.axisTypes[0] = vr::k_eControllerAxis_TrackPad;
    viveLeft.supportedButtons =
        vr::ButtonMaskFromId(vr::k_EButton_Axis0) |
        vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu);
    viveLeft.controllerState.ulButtonPressed =
        vr::ButtonMaskFromId(vr::k_EButton_Axis0);
    viveLeft.controllerState.rAxis[0] = {-0.5f, 0.25f};

    Check(
        vi::GetOpenVrBooleanSourceState(
            viveHands,
            vi::Source::LeftPrimary,
            &openVrActive) && openVrActive,
        "Vive trackpad press should resolve as the portable primary action");

    const vi::OpenVrVector2 vivePrimaryAxis =
        vi::GetOpenVrVector2SourceState(
            viveHands,
            vi::Source::LeftPrimaryAxis,
            &openVrActive);
    Check(
        openVrActive && vivePrimaryAxis.x == -0.5f &&
            vivePrimaryAxis.y == 0.25f,
        "Vive trackpad should resolve as the portable primary axis");

    const std::filesystem::path temp =
        std::filesystem::temp_directory_path() / "kisakcod-configurator-core-test";
    std::error_code error;
    std::filesystem::remove_all(temp, error);
    std::filesystem::create_directories(temp, error);
    Check(!error, "temporary test directory should be created");

    const std::filesystem::path legacyUserFile =
        temp / "VR-User-Settings-V2.bat";
    {
        std::ofstream legacy(legacyUserFile, std::ios::binary);
        legacy << "@echo off\r\n"
               << "set \"KISAK_VR_INPUT_BINDINGS_VERSION=2\"\r\n"
               << "set \"KISAK_VR_BIND_OFFHAND=right.squeeze\"\r\n"
               << "set \"KISAK_VR_BIND_GRENADE_LAUNCHER=unbound\"\r\n"
               << "set \"KISAK_VR_BIND_NIGHT_VISION=unbound\"\r\n"
               << "set \"KISAK_VR_BIND_AIRSTRIKE=unbound\"\r\n"
               << "set \"KISAK_VR_BIND_C4=unbound\"\r\n";
    }

    kc::LoadResult migrated = kc::LoadSettings(
        temp / "missing-defaults.bat",
        legacyUserFile);
    Check(
        migrated.values["KISAK_VR_INPUT_BINDINGS_VERSION"] == "4",
        "V2 user profiles should migrate to binding schema V4");
    Check(
        migrated.values["KISAK_VR_BIND_GRENADE_LAUNCHER"] ==
            "right.squeeze",
        "V2 profiles with an unbound launcher shortcut should recover the new right-grip default");
    Check(
        migrated.values["KISAK_VR_BIND_NIGHT_VISION"] ==
            "right.thumbrest_touch+left.primary_axis.down",
        "V2 profiles should recover the visible night-vision chord");
    Check(
        migrated.values["KISAK_VR_BIND_OFFHAND"] == "unbound",
        "V2 right-grip test default should migrate to unbound");
    Check(
        migrated.messages.empty(),
        "migrated V2 settings should validate cleanly");

    const std::filesystem::path v3UserFile =
        temp / "VR-User-Settings-V3.bat";
    {
        std::ofstream legacy(v3UserFile, std::ios::binary);
        legacy << "@echo off\r\n"
               << "set \"KISAK_VR_INPUT_BINDINGS_VERSION=3\"\r\n"
               << "set \"KISAK_VR_BIND_JUMP=left.trigger\"\r\n"
               << "set \"KISAK_VR_BIND_JUMP_ALT=unbound\"\r\n"
               << "set \"KISAK_VR_BIND_RAISE_STANCE=right.primary_axis.up\"\r\n"
               << "set \"KISAK_VR_BIND_RAISE_STANCE_ALT=unbound\"\r\n"
               << "set \"KISAK_VR_BIND_LOWER_STANCE=right.primary_axis.down\"\r\n"
               << "set \"KISAK_VR_BIND_GRENADE_LAUNCHER=right.thumbrest_touch+left.primary_axis.up\"\r\n"
               << "set \"KISAK_VR_BIND_NIGHT_VISION=right.thumbrest_touch+left.primary_axis.down\"\r\n"
               << "set \"KISAK_VR_BIND_AIRSTRIKE=right.thumbrest_touch+left.primary_axis.left\"\r\n"
               << "set \"KISAK_VR_BIND_C4=right.thumbrest_touch+left.primary_axis.right\"\r\n";
    }

    migrated = kc::LoadSettings(
        temp / "missing-defaults.bat",
        v3UserFile);
    Check(
        migrated.values["KISAK_VR_INPUT_BINDINGS_VERSION"] == "4",
        "V3 user profiles should migrate to binding schema V4");
    Check(
        migrated.values["KISAK_VR_BIND_JUMP"] ==
                "right.primary_axis.up" &&
            migrated.values["KISAK_VR_BIND_JUMP_ALT"] ==
                "left.trigger",
        "V3's separate upward stance action should become the primary Jump binding while left trigger remains alternate");
    Check(
        migrated.values.count("KISAK_VR_BIND_RAISE_STANCE") == 0u &&
            migrated.values.count("KISAK_VR_BIND_RAISE_STANCE_ALT") == 0u,
        "retired V3 Raise stance keys should not survive migration");
    Check(
        migrated.values["KISAK_VR_BIND_LOWER_STANCE"] ==
                "right.primary_axis.down" &&
            migrated.values["KISAK_VR_BIND_GRENADE_LAUNCHER"] ==
                "right.thumbrest_touch+left.primary_axis.up" &&
            migrated.values["KISAK_VR_BIND_NIGHT_VISION"] ==
                "right.thumbrest_touch+left.primary_axis.down" &&
            migrated.values["KISAK_VR_BIND_AIRSTRIKE"] ==
                "right.thumbrest_touch+left.primary_axis.left" &&
            migrated.values["KISAK_VR_BIND_C4"] ==
                "right.thumbrest_touch+left.primary_axis.right",
        "V3-to-V4 migration should preserve every requested directional default and thumbrest chord");
    Check(
        migrated.values["KISAK_VR_BIND_OFFHAND"] == "unbound",
        "the native off-hand action should remain unbound after V3-to-V4 migration");
    Check(
        migrated.messages.empty(),
        "migrated V3 settings should validate cleanly");

    const std::filesystem::path v4UserFile =
        temp / "VR-User-Settings-V4.bat";
    {
        std::ofstream current(v4UserFile, std::ios::binary);
        current << "@echo off\r\n"
                << "set \"KISAK_VR_INPUT_BINDINGS_VERSION=4\"\r\n"
                << "set \"KISAK_VR_BIND_GRENADE_LAUNCHER=right.thumbrest_touch+left.primary_axis.up\"\r\n";
    }

    const kc::LoadResult preservedV4 = kc::LoadSettings(
        temp / "missing-defaults.bat",
        v4UserFile);
    Check(
        preservedV4.values.at("KISAK_VR_BIND_GRENADE_LAUNCHER") ==
            "right.thumbrest_touch+left.primary_axis.up",
        "beta.14 should not overwrite an existing V4 grenade-launcher binding");
    Check(
        preservedV4.messages.empty(),
        "an existing V4 grenade-launcher binding should remain valid");

    const std::filesystem::path userFile = temp / "VR-User-Settings.bat";
    values = kc::BuiltInDefaults();
    values["KISAK_VR_SNAP_TURN_ANGLE"] = "30";
    values["KISAK_VR_WEAPON_OFFSET_FORWARD"] = "3.25";
    values["KISAK_VR_WEAPON_OFFSET_LEFT"] = "-2.50";
    values["KISAK_VR_WEAPON_OFFSET_UP"] = "4.75";
    values["KISAK_VR_MANUAL_RELOAD"] = "0";
    values["KISAK_VR_UNIT_SYSTEM"] = "imperial";
    kc::SaveResult saved = kc::SaveUserSettingsAtomic(userFile, values, "Test profile");
    Check(saved.success, "first atomic save should succeed: " + saved.error);
    Check(saved.readBackVerified, "a successful save must include disk read-back verification");
    Check(
        saved.verifiedSettingCount == catalog.size(),
        "read-back should compare every catalog setting");
    Check(
        !saved.revision.empty() && !saved.savedAt.empty(),
        "a verified save should expose its revision and save time");
    Check(saved.backupPath.empty(), "first save should not create a backup");
    Check(Read(userFile).find("\r\n") != std::string::npos, "saved batch file should use CRLF");
    Check(
        Read(userFile).find("generated by beta.14 Configurator (Unified Setup/Compatibility)") !=
            std::string::npos,
        "saved settings should identify the beta.14 unified-compatibility schema");
    Check(
        Read(userFile).find("KISAK_VR_SETTINGS_REVISION=" + saved.revision) !=
            std::string::npos,
        "the exact verified revision should be inherited by the launcher");

    kc::LoadResult loaded = kc::LoadSettings(temp / "missing-defaults.bat", userFile);
    Check(loaded.values["KISAK_VR_SNAP_TURN_ANGLE"] == "30", "saved settings should round-trip");
    Check(
        loaded.values["KISAK_VR_WEAPON_OFFSET_FORWARD"] == "3.25" &&
            loaded.values["KISAK_VR_WEAPON_OFFSET_LEFT"] == "-2.50" &&
            loaded.values["KISAK_VR_WEAPON_OFFSET_UP"] == "4.75",
        "all three weapon positional offsets should round-trip exactly");
    Check(
        loaded.values["KISAK_VR_MANUAL_RELOAD"] == "0",
        "automatic/native reload mode should round-trip exactly");
    Check(
        loaded.values["KISAK_VR_UNIT_SYSTEM"] == "imperial",
        "the selected measurement presentation should round-trip exactly");
    Check(
        loaded.profileName == "Test profile" &&
            loaded.revision == saved.revision &&
            loaded.activePath == userFile,
        "reload should preserve the active profile, revision, and path");
    Check(loaded.messages.empty(), "round-tripped settings should validate");

    kc::VerificationResult verified = kc::VerifyUserSettingsFile(
        userFile,
        values,
        "Test profile",
        saved.revision);
    Check(
        verified.success && verified.verifiedSettingCount == catalog.size(),
        "explicit verification should confirm all saved values");

    {
        std::ofstream tamper(userFile, std::ios::binary | std::ios::app);
        tamper << "rem simulated post-save tamper\r\n";
    }
    verified = kc::VerifyUserSettingsFile(
        userFile,
        values,
        "Test profile",
        saved.revision);
    Check(
        !verified.success,
        "byte-level verification should reject a file altered after save");

    values["KISAK_VR_SNAP_TURN_ANGLE"] = "60";
    saved = kc::SaveUserSettingsAtomic(userFile, values, "Second profile");
    Check(saved.success, "second atomic save should succeed: " + saved.error);
    Check(!saved.backupPath.empty(), "second save should create a backup");
    Check(std::filesystem::is_regular_file(saved.backupPath), "backup file should exist");
    loaded = kc::LoadSettings(temp / "missing-defaults.bat", userFile);
    Check(loaded.values["KISAK_VR_SNAP_TURN_ANGLE"] == "60", "replacement settings should become active");
    Check(
        saved.readBackVerified && saved.verifiedSettingCount == catalog.size(),
        "replacement saves should also complete mandatory read-back verification");

    std::filesystem::remove_all(temp, error);

    if (failures != 0)
    {
        std::cerr << failures << " configurator settings test(s) failed.\n";
        return 1;
    }

    std::cout << "All configurator settings tests passed (" << catalog.size()
              << " settings).\n";
    return 0;
}
