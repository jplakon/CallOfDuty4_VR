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

enum class Command
{
    Invalid,
    Recenter,
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

inline const char* CommandId(const Command command)
{
    switch (command)
    {
    case Command::Recenter:
        return "recenter";
    case Command::MeasureStanding:
        return "measure_standing";
    case Command::ApplyHeight:
        return "apply_height";
    default:
        return "invalid";
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
            if (value == "recenter")
            {
                parsed.command = Command::Recenter;
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
