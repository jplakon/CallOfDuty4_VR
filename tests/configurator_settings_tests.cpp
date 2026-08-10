#include "../tools/configurator/settings_core.h"
#include "vr/vr_hud_layout.h"
#include "vr/vr_input_bindings.h"
#include "vr/vr_interactions.h"
#include "vr/vr_calibration.h"
#include "vr/vr_compatibility.h"
#include "vr/vr_openvr_input.h"
#include "vr/vr_openxr_profiles.h"
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
namespace vi = kisak::vr::input;
namespace vint = kisak::vr::interactions;
namespace vc = kisak::vr::calibration;
namespace vrc = kisak::vr::compatibility;
namespace vh = kisak::vr::hud;
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

} // namespace

int main(const int argumentCount, char** arguments)
{
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
                packaged.revision == "beta9-unified-calibration-interactions-defaults",
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
            "the launcher should validate overrides, run beta.9 preflight, and publish every guarded state path");
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
        const std::string weapons = Read(
            root / "src/cgame/cg_weapons.cpp");
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
        Check(
            configurator.find("Setup & Compatibility") !=
                    std::string::npos &&
                configurator.find("v0.10.0-beta.9") !=
                    std::string::npos &&
                configurator.find("Rescan system") !=
                    std::string::npos &&
                configurator.find("Apply recommended") !=
                    std::string::npos &&
                configurator.find("Copy support report") !=
                    std::string::npos &&
                configurator.find("--compatibility-report") !=
                    std::string::npos &&
                configurator.find("Handedness, units, comfort, controls, HUD, weapon profiles, and calibration will not change") !=
                    std::string::npos &&
                configurator.find("Height & Recenter") != std::string::npos &&
                configurator.find("Beta.9 unified setup") !=
                    std::string::npos &&
                configurator.find("Recenter now") != std::string::npos &&
                configurator.find("Measure standing height") !=
                    std::string::npos &&
                configurator.find("Apply seated calibration") !=
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
            "the beta.9 menu should unify compatibility with handed interactions, weapon/gunstock, metric, calibration, and both visual HUD workflows");
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
        Read(userFile).find("generated by beta.9 Configurator (Unified Setup/Compatibility)") !=
            std::string::npos,
        "saved settings should identify the beta.9 unified-compatibility schema");
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
