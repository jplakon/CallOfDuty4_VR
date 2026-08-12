#pragma once

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

namespace kisak::vr::calibration
{

constexpr float kNativeStandingEyeHeightInches = 60.0f;
constexpr float kMinimumEyeHeightInches = 42.0f;
constexpr float kMaximumEyeHeightInches = 84.0f;

enum class PlayMode
{
    Standing,
    Seated,
};

// V70 separates translation from the forward/level orientation baseline.
// Disabled is also used by the explicit first-gameplay setting and by
// calibration commands that change height without recentering either pose.
enum class RecenterMode
{
    Disabled,
    PositionOnly,
    DirectionLevelOnly,
    Full,
};

enum class Command
{
    Invalid,
    RecenterPosition,
    RecenterDirectionLevel,
    RecenterFull,
    MeasureStanding,
    ApplyHeight,
};

struct Request
{
    std::string requestId;
    Command command = Command::Invalid;
    PlayMode playMode = PlayMode::Standing;
    float targetEyeHeightInches =
        kNativeStandingEyeHeightInches;
};

inline const char* PlayModeId(const PlayMode mode)
{
    return mode == PlayMode::Seated ? "seated" : "standing";
}

inline const char* RecenterModeId(const RecenterMode mode)
{
    switch (mode)
    {
    case RecenterMode::PositionOnly:
        return "position_only";
    case RecenterMode::DirectionLevelOnly:
        return "direction_level_only";
    case RecenterMode::Full:
        return "full";
    default:
        return "off";
    }
}

inline bool ParseRecenterMode(
    const std::string& value,
    RecenterMode* const mode)
{
    if (mode == nullptr)
    {
        return false;
    }

    if (value == "off" || value == "0")
    {
        *mode = RecenterMode::Disabled;
        return true;
    }
    if (value == "position_only")
    {
        *mode = RecenterMode::PositionOnly;
        return true;
    }
    if (value == "direction_level_only")
    {
        *mode = RecenterMode::DirectionLevelOnly;
        return true;
    }
    if (value == "full" || value == "1")
    {
        *mode = RecenterMode::Full;
        return true;
    }

    return false;
}

inline const char* CommandId(const Command command)
{
    switch (command)
    {
    case Command::RecenterPosition:
        return "recenter_position";
    case Command::RecenterDirectionLevel:
        return "recenter_direction_level";
    case Command::RecenterFull:
        return "recenter_full";
    case Command::MeasureStanding:
        return "measure_standing";
    case Command::ApplyHeight:
        return "apply_height";
    default:
        return "invalid";
    }
}

inline RecenterMode CommandRecenterMode(const Command command)
{
    switch (command)
    {
    case Command::RecenterPosition:
        return RecenterMode::PositionOnly;
    case Command::RecenterDirectionLevel:
        return RecenterMode::DirectionLevelOnly;
    case Command::RecenterFull:
        return RecenterMode::Full;
    default:
        return RecenterMode::Disabled;
    }
}

inline bool IsSafeRequestId(const std::string& value)
{
    if (value.empty() || value.size() > 80u)
    {
        return false;
    }

    for (const unsigned char character : value)
    {
        const bool safe =
            (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' ||
            character == '_' ||
            character == '.';

        if (!safe)
        {
            return false;
        }
    }

    return true;
}

inline bool ParseFiniteFloat(
    const std::string& value,
    float* const parsed)
{
    if (parsed == nullptr || value.empty())
    {
        return false;
    }

    char* end = nullptr;
    const float number = std::strtof(value.c_str(), &end);
    if (end == value.c_str() ||
        end == nullptr ||
        end[0] != '\0' ||
        !std::isfinite(number))
    {
        return false;
    }

    *parsed = number;
    return true;
}

inline bool ParseRequest(
    const std::string& text,
    Request* const request,
    std::string* const error = nullptr)
{
    if (request == nullptr)
    {
        return false;
    }

    Request parsed;
    bool versionFound = false;
    bool requestFound = false;
    bool commandFound = false;
    bool modeFound = false;
    bool heightFound = false;

    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos)
        {
            continue;
        }

        const std::string key = line.substr(0u, separator);
        const std::string value = line.substr(separator + 1u);

        if (key == "VERSION")
        {
            versionFound = value == "1";
        }
        else if (key == "REQUEST_ID")
        {
            parsed.requestId = value;
            requestFound = IsSafeRequestId(value);
        }
        else if (key == "COMMAND")
        {
            commandFound = true;
            if (value == "recenter_position")
            {
                parsed.command = Command::RecenterPosition;
            }
            else if (value == "recenter_direction_level")
            {
                parsed.command = Command::RecenterDirectionLevel;
            }
            else if (value == "recenter_full" || value == "recenter")
            {
                // "recenter" is the beta.8-beta.10 protocol spelling. Keep
                // accepting it as a full recenter so a stale request or older
                // configurator cannot silently change meaning.
                parsed.command = Command::RecenterFull;
            }
            else if (value == "measure_standing")
            {
                parsed.command = Command::MeasureStanding;
            }
            else if (value == "apply_height")
            {
                parsed.command = Command::ApplyHeight;
            }
            else
            {
                parsed.command = Command::Invalid;
            }
        }
        else if (key == "PLAY_MODE")
        {
            modeFound = true;
            if (value == "standing")
            {
                parsed.playMode = PlayMode::Standing;
            }
            else if (value == "seated")
            {
                parsed.playMode = PlayMode::Seated;
            }
            else
            {
                modeFound = false;
            }
        }
        else if (key == "TARGET_EYE_HEIGHT_INCHES")
        {
            heightFound = ParseFiniteFloat(
                value,
                &parsed.targetEyeHeightInches);
        }
    }

    const bool valid =
        versionFound &&
        requestFound &&
        commandFound &&
        parsed.command != Command::Invalid &&
        modeFound &&
        heightFound &&
        parsed.targetEyeHeightInches >=
            kMinimumEyeHeightInches &&
        parsed.targetEyeHeightInches <=
            kMaximumEyeHeightInches;

    if (!valid)
    {
        if (error != nullptr)
        {
            *error =
                "Calibration request must contain VERSION=1, a safe "
                "REQUEST_ID, a supported COMMAND and PLAY_MODE, and an "
                "eye height from 42 through 84 inches.";
        }
        return false;
    }

    *request = parsed;
    return true;
}

inline std::string SerializeRequest(const Request& request)
{
    std::ostringstream output;
    output.setf(std::ios::fixed, std::ios::floatfield);
    output.precision(2);
    output << "VERSION=1\r\n";
    output << "REQUEST_ID=" << request.requestId << "\r\n";
    output << "COMMAND=" << CommandId(request.command) << "\r\n";
    output << "PLAY_MODE=" << PlayModeId(request.playMode) << "\r\n";
    output << "TARGET_EYE_HEIGHT_INCHES="
           << request.targetEyeHeightInches << "\r\n";
    return output.str();
}

inline float EyeHeightCorrectionInches(
    const float targetEyeHeightInches)
{
    return targetEyeHeightInches -
        kNativeStandingEyeHeightInches;
}

} // namespace kisak::vr::calibration
