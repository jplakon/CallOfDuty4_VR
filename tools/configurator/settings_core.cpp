#include "settings_core.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
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

    Decimal(
        "KISAK_VR_HUD_SAFE_X",
        "HUD horizontal safe area",
        "Smaller values pull edge-aligned HUD elements toward the center.",
        SettingPage::Hud,
        "0.50",
        0.50,
        1.00,
        2),
    Decimal(
        "KISAK_VR_HUD_SAFE_Y",
        "HUD vertical safe area",
        "Smaller values pull top and bottom HUD elements toward the center.",
        SettingPage::Hud,
        "1.00",
        0.50,
        1.00,
        2),
    Decimal(
        "KISAK_VR_HUD_BOTTOM_LEFT_SCALE",
        "Ammo/action HUD scale",
        "Scale for the bottom-left weapon, ammunition, and action-slot cluster.",
        SettingPage::Hud,
        "0.50",
        0.50,
        1.00,
        2),
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
        2),
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
        0,
        320),
    Integer(
        "KISAK_VR_COMPASS_INSET_Y",
        "Compass inset up",
        "Move the lower-right compass upward in virtual HUD pixels.",
        SettingPage::Hud,
        48,
        0,
        180),
    Integer(
        "KISAK_VR_GAME_MESSAGE_X_OFFSET",
        "Game-text horizontal offset",
        "Move mission notifications right (positive) or left (negative).",
        SettingPage::Hud,
        0,
        -300,
        300),
    Integer(
        "KISAK_VR_GAME_MESSAGE_Y_OFFSET",
        "Game-text vertical offset",
        "Move mission notifications down from COD4's original position.",
        SettingPage::Hud,
        72,
        0,
        200),
    Decimal(
        "KISAK_VR_GAME_MESSAGE_SCALE",
        "Game-text scale",
        "Scale mission notifications and status messages.",
        SettingPage::Hud,
        "1.00",
        0.50,
        1.50,
        2),
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

    Choice(
        "KISAK_VR_BIND_USE",
        "Use / interact",
        "Physical left-controller button used for COD4's use/interact action.",
        SettingPage::Controls,
        "x",
        {{"x", "Left X"}, {"y", "Left Y"}, {"stick", "Left stick click"}}),
    Choice(
        "KISAK_VR_BIND_SPRINT",
        "Sprint",
        "Physical left-controller button used for sprint.",
        SettingPage::Controls,
        "stick",
        {{"x", "Left X"}, {"y", "Left Y"}, {"stick", "Left stick click"}}),
    Choice(
        "KISAK_VR_BIND_NEXT_WEAPON",
        "Next weapon",
        "Physical left-controller button used to switch weapons.",
        SettingPage::Controls,
        "y",
        {{"x", "Left X"}, {"y", "Left Y"}, {"stick", "Left stick click"}}),
    Choice(
        "KISAK_VR_BIND_RELOAD",
        "Reload / eject magazine",
        "Physical right-controller button used for reload or manual magazine ejection.",
        SettingPage::Controls,
        "a",
        {{"a", "Right A"}, {"b", "Right B"}, {"stick", "Right stick click"}}),
    Choice(
        "KISAK_VR_BIND_MELEE",
        "Melee",
        "Physical right-controller button used for melee.",
        SettingPage::Controls,
        "stick",
        {{"a", "Right A"}, {"b", "Right B"}, {"stick", "Right stick click"}}),
    Choice(
        "KISAK_VR_BIND_STANCE",
        "Stance",
        "Physical right-controller button used for the stance/crouch action.",
        SettingPage::Controls,
        "b",
        {{"a", "Right A"}, {"b", "Right B"}, {"stick", "Right stick click"}}),

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

void Set(SettingsMap* values, const char* key, const char* value)
{
    if (values != nullptr)
    {
        (*values)[key] = value;
    }
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

        const std::string value = Trim(match[2].str());
        if (FindSetting(key) == nullptr)
        {
            continue;
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

    const std::vector<std::string> leftBindings = {
        valueOf("KISAK_VR_BIND_USE"),
        valueOf("KISAK_VR_BIND_SPRINT"),
        valueOf("KISAK_VR_BIND_NEXT_WEAPON"),
    };
    const std::vector<std::string> rightBindings = {
        valueOf("KISAK_VR_BIND_RELOAD"),
        valueOf("KISAK_VR_BIND_MELEE"),
        valueOf("KISAK_VR_BIND_STANCE"),
    };

    const auto hasDuplicate = [](std::vector<std::string> bindings)
    {
        std::sort(bindings.begin(), bindings.end());
        return std::adjacent_find(bindings.begin(), bindings.end()) !=
               bindings.end();
    };

    if (hasDuplicate(leftBindings))
    {
        messages.push_back({
            ValidationMessage::Severity::Error,
            "KISAK_VR_BIND_USE",
            "Use, Sprint, and Next weapon must use three different left-controller buttons.",
        });
    }

    if (hasDuplicate(rightBindings))
    {
        messages.push_back({
            ValidationMessage::Severity::Error,
            "KISAK_VR_BIND_RELOAD",
            "Reload, Melee, and Stance must use three different right-controller buttons.",
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

    if (std::filesystem::is_regular_file(defaultsPath))
    {
        const SettingsMap defaults =
            ParseBatchSettings(ReadTextFile(defaultsPath), &result.messages);
        for (const auto& [key, value] : defaults)
        {
            result.values[key] = value;
        }
    }

    if (std::filesystem::is_regular_file(userPath))
    {
        result.userFileFound = true;
        const SettingsMap overrides =
            ParseBatchSettings(ReadTextFile(userPath), &result.messages);
        for (const auto& [key, value] : overrides)
        {
            result.values[key] = value;
        }
    }

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
    const std::string& profileName)
{
    std::ostringstream output;
    output << "@echo off\r\n";
    output << "rem KisakCOD VR user settings - generated by V56 Configurator\r\n";
    output << "rem Stored separately so extracting a future release cannot erase preferences.\r\n";
    output << "rem Profile: " << profileName << "\r\n\r\n";

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

SaveResult SaveUserSettingsAtomic(
    const std::filesystem::path& userPath,
    const SettingsMap& values,
    const std::string& profileName)
{
    SaveResult result;
    result.settingsPath = userPath;

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
        userPath.parent_path() / (userPath.filename().string() + ".new");
    {
        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            result.error = "Could not create the temporary settings file.";
            return result;
        }

        const std::string serialized = SerializeUserSettings(values, profileName);
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
