#include "../tools/configurator/settings_core.h"
#include "vr/vr_hud_layout.h"
#include "vr/vr_input_bindings.h"
#include "vr/vr_calibration.h"
#include "vr/vr_openvr_input.h"
#include "vr/vr_openxr_profiles.h"
#include "vr/vr_weapon_calibration.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

namespace kc = kisak::configurator;
namespace vi = kisak::vr::input;
namespace vc = kisak::vr::calibration;
namespace vh = kisak::vr::hud;

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

} // namespace

int main(const int argumentCount, char** arguments)
{
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
            "the visual canvas should reproduce the tested ammo and compass defaults");

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

    const auto& catalog = kc::SettingsCatalog();
    Check(catalog.size() == 125u, "V61 should expose 125 verified settings");
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
    {
        vh::Layout edited = kc::HudLayoutFromSettings(values);
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
                "right.thumbrest_touch+left.primary_axis.up" &&
            values["KISAK_VR_BIND_AIRSTRIKE"] ==
                "right.thumbrest_touch+left.primary_axis.left" &&
            values["KISAK_VR_BIND_C4"] ==
                "right.thumbrest_touch+left.primary_axis.right",
        "all four beta.7 mission D-pad defaults should be ordinary chords");
    Check(
        values["KISAK_VR_BIND_OFFHAND"] == "unbound",
        "right grip should be unbound by default");
    Check(
        values["KISAK_VR_PLAY_MODE"] == "standing" &&
            values["KISAK_VR_STANDING_EYE_HEIGHT"] == "60.0" &&
            values["KISAK_VR_SEATED_EYE_HEIGHT"] == "60.0" &&
            values["KISAK_VR_RECENTER_ON_START"] == "1",
        "V60 calibration defaults should preserve COD4's native height and recenter at gameplay start");

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
                packaged.revision == "v61-release-defaults",
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
                launcher.find("--validate") != std::string::npos,
            "the launcher should validate overrides and write an effective-settings receipt");
        Check(
            runtime.find("STATUS=RUNTIME_ACCEPTED") != std::string::npos &&
                runtime.find("RUNTIME_WEAPON_OFFSET") != std::string::npos &&
                runtime.find("RUNTIME_MANUAL_RELOAD") != std::string::npos &&
                runtime.find("STATUS=RUNTIME_WEAPON_POSE_APPLIED") !=
                    std::string::npos &&
                runtime.find("RUNTIME_WEAPON_ALIGNMENT_ERROR") !=
                    std::string::npos &&
                runtime.find("CalibratedGripTargetWorld") !=
                    std::string::npos &&
                runtime.find("STATUS=RUNTIME_HEIGHT_APPLIED") !=
                    std::string::npos &&
                runtime.find("STATUS=RUNTIME_CALIBRATION_APPLIED") !=
                    std::string::npos &&
                runtime.find("VR_RecenterHeadPose") !=
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
                    std::string::npos,
            "the game should acknowledge settings, calibration, and the live HUD editor lifecycle");
        const std::size_t lostPoseStatus =
            runtime.find("\"NO_TRACKED_POSE\"");
        const std::size_t heightCommit =
            runtime.find("Commit the requested height only");
        Check(
            lostPoseStatus != std::string::npos &&
                heightCommit != std::string::npos &&
                lostPoseStatus < heightCommit,
            "a failed tracked-pose recenter must be rejected before live height is committed");

        const std::filesystem::path runtimePath = arguments[3];
        const std::filesystem::path root =
            runtimePath.parent_path().parent_path().parent_path();
        const std::string screenPlacement = Read(
            root / "src/client/screen_placement.cpp");
        const std::string messages = Read(
            root / "src/client/cl_console.cpp");
        const std::string compass = Read(
            root / "src/cgame/cg_compass.cpp");
        const std::string draw = Read(
            root / "src/cgame/cg_draw.cpp");
        const std::string cgameMain = Read(
            root / "src/cgame/cg_main.cpp");
        const std::string debugDraw = Read(
            root / "src/cgame/cg_draw_debug.cpp");
        Check(
            screenPlacement.find("layout.ammoOffsetX") !=
                    std::string::npos &&
                screenPlacement.find("layout.ammoScale") !=
                    std::string::npos &&
                compass.find("layout.compassInsetX") !=
                    std::string::npos &&
                messages.find("layout.objectiveOffsetX") !=
                    std::string::npos &&
                messages.find("layout.subtitleScale") !=
                    std::string::npos &&
                draw.find("VR_DrawHudEditorOverlay") !=
                    std::string::npos &&
                draw.find("CANCEL (B)") != std::string::npos &&
                draw.find("SAVE (A)") != std::string::npos,
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
    }

    if (argumentCount >= 5)
    {
        const std::string configurator = Read(arguments[4]);
        Check(
            configurator.find("Height & Recenter") != std::string::npos &&
                configurator.find("Recenter now") != std::string::npos &&
                configurator.find("Measure standing height") !=
                    std::string::npos &&
                configurator.find("Apply seated calibration") !=
                    std::string::npos &&
                configurator.find("1 in shorter") != std::string::npos &&
                configurator.find("1 in taller") != std::string::npos &&
                configurator.find("Open desktop visual editor") !=
                    std::string::npos &&
                configurator.find("Edit live in headset") !=
                    std::string::npos &&
                configurator.find("Apply layout") !=
                    std::string::npos &&
                configurator.find("Snap anchors: ON") !=
                    std::string::npos,
            "the V61 menu should expose both complete visual HUD workflows");
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
        "right grip should remain unbound after V3-to-V4 migration");
    Check(
        migrated.messages.empty(),
        "migrated V3 settings should validate cleanly");

    const std::filesystem::path userFile = temp / "VR-User-Settings.bat";
    values = kc::BuiltInDefaults();
    values["KISAK_VR_SNAP_TURN_ANGLE"] = "30";
    values["KISAK_VR_WEAPON_OFFSET_FORWARD"] = "3.25";
    values["KISAK_VR_WEAPON_OFFSET_LEFT"] = "-2.50";
    values["KISAK_VR_WEAPON_OFFSET_UP"] = "4.75";
    values["KISAK_VR_MANUAL_RELOAD"] = "0";
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
        Read(userFile).find("generated by v0.10.0-beta.8 Configurator (Visual HUD, Input V4)") !=
            std::string::npos,
        "saved settings should identify the beta.8 visual-HUD schema");
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
