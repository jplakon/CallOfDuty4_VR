#include "vr_weapon_profiles.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace kisak::vr::weapon_profiles
{
namespace
{

std::string Trim(std::string_view value)
{
    while (!value.empty() &&
           (value.front() == ' ' || value.front() == '\t' ||
            value.front() == '\r' || value.front() == '\n'))
    {
        value.remove_prefix(1u);
    }
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\t' ||
            value.back() == '\r' || value.back() == '\n'))
    {
        value.remove_suffix(1u);
    }
    return std::string(value);
}

bool SafeDisplayName(const std::string& value)
{
    if (value.empty() || value.size() > 96u)
    {
        return false;
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [](const unsigned char character)
        {
            return character >= 0x20u && character != 0x7fu &&
                character != '\r' && character != '\n';
        });
}

bool ParseFloat(const std::string& value, float* parsed)
{
    if (parsed == nullptr || value.empty())
    {
        return false;
    }
    char* end = nullptr;
    const float number = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || end == nullptr || end[0] != '\0' ||
        !std::isfinite(number))
    {
        return false;
    }
    *parsed = number;
    return true;
}

bool ParseInteger(const std::string& value, int* parsed)
{
    if (parsed == nullptr || value.empty())
    {
        return false;
    }
    int number = 0;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto result = std::from_chars(begin, end, number);
    if (result.ec != std::errc() || result.ptr != end)
    {
        return false;
    }
    *parsed = number;
    return true;
}

bool ParseToggle(const std::string& value, bool* parsed)
{
    if (parsed == nullptr || (value != "0" && value != "1"))
    {
        return false;
    }
    *parsed = value == "1";
    return true;
}

bool PoseValid(const Pose& pose)
{
    for (const float value : pose.offset)
    {
        if (!std::isfinite(value) ||
            value < -kMaximumOffsetInches ||
            value > kMaximumOffsetInches)
        {
            return false;
        }
    }
    for (const float value : pose.angles)
    {
        if (!std::isfinite(value) ||
            value < -kMaximumAngleDegrees ||
            value > kMaximumAngleDegrees)
        {
            return false;
        }
    }
    return true;
}

bool SetPoseValue(
    Pose* const pose,
    const std::string& key,
    const std::string& value,
    const std::string& prefix)
{
    if (pose == nullptr || key.rfind(prefix, 0u) != 0u)
    {
        return false;
    }
    const std::string suffix = key.substr(prefix.size());
    float parsed = 0.0f;
    if (!ParseFloat(value, &parsed))
    {
        return false;
    }
    if (suffix == "OFFSET_FORWARD")
    {
        pose->offset[0] = parsed;
    }
    else if (suffix == "OFFSET_LEFT")
    {
        pose->offset[1] = parsed;
    }
    else if (suffix == "OFFSET_UP")
    {
        pose->offset[2] = parsed;
    }
    else if (suffix == "PITCH")
    {
        pose->angles[0] = parsed;
    }
    else if (suffix == "YAW")
    {
        pose->angles[1] = parsed;
    }
    else if (suffix == "ROLL")
    {
        pose->angles[2] = parsed;
    }
    else
    {
        return false;
    }
    return true;
}

void WritePose(
    std::ostringstream& output,
    const char* const prefix,
    const Pose& pose)
{
    output << prefix << "OFFSET_FORWARD=" << pose.offset[0] << "\r\n";
    output << prefix << "OFFSET_LEFT=" << pose.offset[1] << "\r\n";
    output << prefix << "OFFSET_UP=" << pose.offset[2] << "\r\n";
    output << prefix << "PITCH=" << pose.angles[0] << "\r\n";
    output << prefix << "YAW=" << pose.angles[1] << "\r\n";
    output << prefix << "ROLL=" << pose.angles[2] << "\r\n";
}

std::map<std::string, std::string> ParseKeyValues(
    const std::string& text)
{
    std::map<std::string, std::string> values;
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
        values[Trim(std::string_view(line).substr(0u, separator))] =
            Trim(std::string_view(line).substr(separator + 1u));
    }
    return values;
}

void AddScaled(Pose* const target, const Pose& source, const float scale)
{
    for (std::size_t component = 0u; component < 3u; ++component)
    {
        target->offset[component] += source.offset[component] * scale;
        target->angles[component] += source.angles[component] * scale;
    }
}

std::string SanitizeStatusText(std::string value)
{
    for (char& character : value)
    {
        if (character == '\r' || character == '\n')
        {
            character = ' ';
        }
    }
    if (value.size() > 160u)
    {
        value.resize(160u);
    }
    return value;
}

} // namespace

Document DefaultDocument()
{
    Document document;
    GunstockProfile generic;
    generic.id = "generic";
    generic.name = "Generic / no additional mount correction";
    document.gunstocks.push_back(generic);
    return document;
}

bool IsSafeId(const std::string_view value)
{
    if (value.empty() || value.size() > 64u)
    {
        return false;
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [](const unsigned char character)
        {
            return (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '.' || character == '_' || character == '-';
        });
}

std::string NormalizeId(const std::string_view value)
{
    std::string normalized = Trim(value);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return normalized;
}

bool ValidateDocument(const Document& document, std::string* const error)
{
    const auto fail = [error](const std::string& message)
    {
        if (error != nullptr)
        {
            *error = message;
        }
        return false;
    };

    if (document.gunstocks.empty() ||
        document.gunstocks.size() > kMaximumGunstockProfiles ||
        document.weapons.size() > kMaximumWeaponProfiles)
    {
        return fail("Profile count is outside the guarded limit.");
    }
    if (!IsSafeId(document.activeGunstockId))
    {
        return fail("The active gunstock id is invalid.");
    }

    std::vector<std::string> gunstockIds;
    for (const GunstockProfile& profile : document.gunstocks)
    {
        const std::string id = NormalizeId(profile.id);
        if (!IsSafeId(id) || !SafeDisplayName(profile.name) ||
            !PoseValid(profile.shouldered) ||
            std::find(gunstockIds.begin(), gunstockIds.end(), id) !=
                gunstockIds.end())
        {
            return fail("A gunstock profile is invalid or duplicated.");
        }
        gunstockIds.push_back(id);
    }
    if (std::find(
            gunstockIds.begin(),
            gunstockIds.end(),
            NormalizeId(document.activeGunstockId)) == gunstockIds.end())
    {
        return fail("The selected gunstock profile does not exist.");
    }

    std::vector<std::string> weaponIds;
    for (const WeaponProfile& profile : document.weapons)
    {
        const std::string id = NormalizeId(profile.id);
        if (!IsSafeId(id) || !SafeDisplayName(profile.name) ||
            !PoseValid(profile.hip) || !PoseValid(profile.shouldered) ||
            std::find(weaponIds.begin(), weaponIds.end(), id) !=
                weaponIds.end())
        {
            return fail("A weapon profile is invalid or duplicated.");
        }
        weaponIds.push_back(id);
    }
    return true;
}

bool ParseDocument(
    const std::string& text,
    Document* const document,
    std::string* const error)
{
    if (document == nullptr || text.empty() || text.size() > 262144u)
    {
        if (error != nullptr)
        {
            *error = "The weapon-profile document is empty or too large.";
        }
        return false;
    }

    Document parsed;
    parsed.activeGunstockId.clear();
    int version = 0;
    GunstockProfile* currentGunstock = nullptr;
    WeaponProfile* currentWeapon = nullptr;

    std::istringstream input(text);
    std::string line;
    std::size_t lineNumber = 0u;
    while (std::getline(input, line))
    {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
        {
            continue;
        }
        if (line.front() == '[' && line.back() == ']')
        {
            const std::string section = line.substr(1u, line.size() - 2u);
            const std::size_t separator = section.find(' ');
            if (separator == std::string::npos)
            {
                if (error != nullptr)
                {
                    *error = "Invalid section on line " +
                        std::to_string(lineNumber) + ".";
                }
                return false;
            }
            const std::string kind = section.substr(0u, separator);
            const std::string id = NormalizeId(section.substr(separator + 1u));
            if (!IsSafeId(id))
            {
                if (error != nullptr)
                {
                    *error = "Unsafe profile id on line " +
                        std::to_string(lineNumber) + ".";
                }
                return false;
            }
            currentGunstock = nullptr;
            currentWeapon = nullptr;
            if (kind == "GUNSTOCK")
            {
                parsed.gunstocks.push_back({});
                currentGunstock = &parsed.gunstocks.back();
                currentGunstock->id = id;
                currentGunstock->name = id;
            }
            else if (kind == "WEAPON")
            {
                parsed.weapons.push_back({});
                currentWeapon = &parsed.weapons.back();
                currentWeapon->id = id;
                currentWeapon->name = id;
            }
            else
            {
                if (error != nullptr)
                {
                    *error = "Unknown profile section on line " +
                        std::to_string(lineNumber) + ".";
                }
                return false;
            }
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos)
        {
            if (error != nullptr)
            {
                *error = "Expected key=value on line " +
                    std::to_string(lineNumber) + ".";
            }
            return false;
        }
        const std::string key = Trim(std::string_view(line).substr(0u, separator));
        const std::string value = Trim(std::string_view(line).substr(separator + 1u));
        bool accepted = false;

        if (currentGunstock != nullptr)
        {
            if (key == "NAME")
            {
                currentGunstock->name = value;
                accepted = true;
            }
            else if (key == "ENABLED")
            {
                accepted = ParseToggle(value, &currentGunstock->enabled);
            }
            else
            {
                accepted = SetPoseValue(
                    &currentGunstock->shouldered,
                    key,
                    value,
                    "SHOULDER_");
            }
        }
        else if (currentWeapon != nullptr)
        {
            if (key == "NAME")
            {
                currentWeapon->name = value;
                accepted = true;
            }
            else if (key == "ENABLED")
            {
                accepted = ParseToggle(value, &currentWeapon->enabled);
            }
            else
            {
                accepted = SetPoseValue(&currentWeapon->hip, key, value, "HIP_") ||
                    SetPoseValue(
                        &currentWeapon->shouldered,
                        key,
                        value,
                        "SHOULDER_");
            }
        }
        else if (key == "VERSION")
        {
            accepted = ParseInteger(value, &version);
        }
        else if (key == "ACTIVE_GUNSTOCK")
        {
            parsed.activeGunstockId = NormalizeId(value);
            accepted = IsSafeId(parsed.activeGunstockId);
        }

        if (!accepted)
        {
            if (error != nullptr)
            {
                *error = "Unknown or invalid value on line " +
                    std::to_string(lineNumber) + ".";
            }
            return false;
        }
    }

    if (version != 1)
    {
        if (error != nullptr)
        {
            *error = "Weapon profiles require VERSION=1.";
        }
        return false;
    }
    if (!ValidateDocument(parsed, error))
    {
        return false;
    }
    *document = std::move(parsed);
    return true;
}

std::string SerializeDocument(const Document& document)
{
    std::ostringstream output;
    output.setf(std::ios::fixed, std::ios::floatfield);
    output.precision(2);
    output << "# KisakCOD VR per-weapon and gunstock calibration\r\n";
    output << "VERSION=1\r\n";
    output << "ACTIVE_GUNSTOCK=" << NormalizeId(document.activeGunstockId)
           << "\r\n";

    std::vector<GunstockProfile> gunstocks = document.gunstocks;
    std::sort(gunstocks.begin(), gunstocks.end(), [](const auto& left, const auto& right)
    {
        return NormalizeId(left.id) < NormalizeId(right.id);
    });
    for (const GunstockProfile& profile : gunstocks)
    {
        output << "\r\n[GUNSTOCK " << NormalizeId(profile.id) << "]\r\n";
        output << "NAME=" << profile.name << "\r\n";
        output << "ENABLED=" << (profile.enabled ? 1 : 0) << "\r\n";
        WritePose(output, "SHOULDER_", profile.shouldered);
    }

    std::vector<WeaponProfile> weapons = document.weapons;
    std::sort(weapons.begin(), weapons.end(), [](const auto& left, const auto& right)
    {
        return NormalizeId(left.id) < NormalizeId(right.id);
    });
    for (const WeaponProfile& profile : weapons)
    {
        output << "\r\n[WEAPON " << NormalizeId(profile.id) << "]\r\n";
        output << "NAME=" << profile.name << "\r\n";
        output << "ENABLED=" << (profile.enabled ? 1 : 0) << "\r\n";
        WritePose(output, "HIP_", profile.hip);
        WritePose(output, "SHOULDER_", profile.shouldered);
    }
    return output.str();
}

std::string DocumentRevision(const Document& document)
{
    const std::string serialized = SerializeDocument(document);
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : serialized)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

const GunstockProfile* FindGunstock(
    const Document& document,
    const std::string_view id)
{
    const std::string normalized = NormalizeId(id);
    const auto found = std::find_if(
        document.gunstocks.begin(),
        document.gunstocks.end(),
        [&normalized](const GunstockProfile& profile)
        {
            return NormalizeId(profile.id) == normalized;
        });
    return found == document.gunstocks.end() ? nullptr : &*found;
}

GunstockProfile* FindGunstock(Document& document, const std::string_view id)
{
    return const_cast<GunstockProfile*>(
        FindGunstock(static_cast<const Document&>(document), id));
}

const WeaponProfile* FindWeapon(
    const Document& document,
    const std::string_view id)
{
    const std::string normalized = NormalizeId(id);
    const auto found = std::find_if(
        document.weapons.begin(),
        document.weapons.end(),
        [&normalized](const WeaponProfile& profile)
        {
            return NormalizeId(profile.id) == normalized;
        });
    return found == document.weapons.end() ? nullptr : &*found;
}

WeaponProfile* FindWeapon(Document& document, const std::string_view id)
{
    return const_cast<WeaponProfile*>(
        FindWeapon(static_cast<const Document&>(document), id));
}

EffectiveCalibration Resolve(
    const Document& document,
    const bool profilesEnabled,
    const Pose& globalBaseline,
    const std::string_view weaponId,
    const float shoulderedBlend)
{
    EffectiveCalibration result;
    result.pose = globalBaseline;
    result.weaponId = NormalizeId(weaponId);
    result.gunstockId = NormalizeId(document.activeGunstockId);
    result.shoulderedBlend = std::clamp(shoulderedBlend, 0.0f, 1.0f);
    if (!profilesEnabled)
    {
        result.gunstockId.clear();
        result.shoulderedBlend = 0.0f;
        return result;
    }

    const WeaponProfile* const weapon = FindWeapon(document, result.weaponId);
    if (weapon != nullptr && weapon->enabled)
    {
        AddScaled(&result.pose, weapon->hip, 1.0f);
        AddScaled(&result.pose, weapon->shouldered, result.shoulderedBlend);
        result.weaponOverrideApplied = true;
    }

    const GunstockProfile* const gunstock =
        FindGunstock(document, result.gunstockId);
    if (gunstock != nullptr && gunstock->enabled && result.shoulderedBlend > 0.0f)
    {
        AddScaled(&result.pose, gunstock->shouldered, result.shoulderedBlend);
        result.gunstockApplied = true;
    }
    return result;
}

std::string SerializeGunstock(const GunstockProfile& profile)
{
    Document document;
    document.activeGunstockId = NormalizeId(profile.id);
    document.gunstocks.push_back(profile);
    return SerializeDocument(document);
}

bool ParseGunstock(
    const std::string& text,
    GunstockProfile* const profile,
    std::string* const error)
{
    Document document;
    if (!ParseDocument(text, &document, error) ||
        document.gunstocks.size() != 1u || !document.weapons.empty())
    {
        if (error != nullptr && error->empty())
        {
            *error = "A .vrstock file must contain exactly one gunstock profile.";
        }
        return false;
    }
    *profile = document.gunstocks.front();
    return true;
}

const char* CommandId(const Command command)
{
    switch (command)
    {
    case Command::Reload:
        return "reload";
    case Command::CaptureAim:
        return "capture_aim";
    default:
        return "invalid";
    }
}

const char* CaptureTargetId(const CaptureTarget target)
{
    return target == CaptureTarget::WeaponShouldered
        ? "weapon_shouldered"
        : "gunstock";
}

std::string SerializeRequest(const Request& request)
{
    std::ostringstream output;
    output << "VERSION=1\r\n";
    output << "REQUEST_ID=" << request.requestId << "\r\n";
    output << "COMMAND=" << CommandId(request.command) << "\r\n";
    output << "TARGET=" << CaptureTargetId(request.target) << "\r\n";
    output << "WEAPON_ID=" << NormalizeId(request.weaponId) << "\r\n";
    output << "GUNSTOCK_ID=" << NormalizeId(request.gunstockId) << "\r\n";
    return output.str();
}

bool ParseRequest(
    const std::string& text,
    Request* const request,
    std::string* const error)
{
    if (request == nullptr || text.empty() || text.size() > 4096u)
    {
        return false;
    }
    const auto values = ParseKeyValues(text);
    const auto get = [&values](const char* key) -> std::string
    {
        const auto found = values.find(key);
        return found == values.end() ? std::string() : found->second;
    };

    Request parsed;
    int version = 0;
    ParseInteger(get("VERSION"), &version);
    parsed.requestId = get("REQUEST_ID");
    const std::string command = get("COMMAND");
    parsed.command = command == "reload"
        ? Command::Reload
        : (command == "capture_aim" ? Command::CaptureAim : Command::Invalid);
    const std::string target = get("TARGET");
    const bool knownTarget = target == "gunstock" ||
        target == "weapon_shouldered";
    parsed.target = target == "weapon_shouldered"
        ? CaptureTarget::WeaponShouldered
        : CaptureTarget::Gunstock;
    parsed.weaponId = NormalizeId(get("WEAPON_ID"));
    parsed.gunstockId = NormalizeId(get("GUNSTOCK_ID"));

    const bool valid = version == 1 && IsSafeId(parsed.requestId) &&
        parsed.command != Command::Invalid &&
        knownTarget &&
        (parsed.weaponId.empty() || IsSafeId(parsed.weaponId)) &&
        (parsed.gunstockId.empty() || IsSafeId(parsed.gunstockId)) &&
        (parsed.command == Command::Reload ||
         (!parsed.weaponId.empty() && !parsed.gunstockId.empty()));
    if (!valid)
    {
        if (error != nullptr)
        {
            *error = "Invalid beta.12 weapon-calibration request.";
        }
        return false;
    }
    *request = std::move(parsed);
    return true;
}

std::string SerializeRuntimeStatus(const RuntimeStatus& status)
{
    std::ostringstream output;
    output.setf(std::ios::fixed, std::ios::floatfield);
    output.precision(2);
    output << "VERSION=1\r\n";
    output << "STATUS=" << SanitizeStatusText(status.status) << "\r\n";
    output << "REQUEST_ID=" << status.requestId << "\r\n";
    output << "MESSAGE=" << SanitizeStatusText(status.message) << "\r\n";
    output << "WEAPON_INDEX=" << status.weaponIndex << "\r\n";
    output << "WEAPON_ID=" << NormalizeId(status.weaponId) << "\r\n";
    output << "WEAPON_NAME=" << SanitizeStatusText(status.weaponName) << "\r\n";
    output << "ACTIVE_GUNSTOCK=" << NormalizeId(status.activeGunstockId) << "\r\n";
    output << "PROFILE_REVISION=" << status.profileRevision << "\r\n";
    output << "WEAPON_OVERRIDE=" <<
        (status.effective.weaponOverrideApplied ? 1 : 0) << "\r\n";
    output << "GUNSTOCK_APPLIED=" <<
        (status.effective.gunstockApplied ? 1 : 0) << "\r\n";
    output << "SHOULDER_BLEND=" << status.effective.shoulderedBlend << "\r\n";
    output << "EFFECTIVE_OFFSET=" << status.effective.pose.offset[0] << ' '
           << status.effective.pose.offset[1] << ' '
           << status.effective.pose.offset[2] << "\r\n";
    output << "EFFECTIVE_ANGLES=" << status.effective.pose.angles[0] << ' '
           << status.effective.pose.angles[1] << ' '
           << status.effective.pose.angles[2] << "\r\n";
    output << "CAPTURED_ANGLES_VALID=" <<
        (status.capturedAnglesValid ? 1 : 0) << "\r\n";
    output << "CAPTURED_EFFECTIVE_ANGLES="
           << status.capturedEffectiveAngles[0] << ' '
           << status.capturedEffectiveAngles[1] << ' '
           << status.capturedEffectiveAngles[2] << "\r\n";
    return output.str();
}

bool ParseRuntimeStatus(
    const std::string& text,
    RuntimeStatus* const status,
    std::string* const error)
{
    if (status == nullptr || text.empty() || text.size() > 16384u)
    {
        return false;
    }
    const auto values = ParseKeyValues(text);
    const auto get = [&values](const char* key) -> std::string
    {
        const auto found = values.find(key);
        return found == values.end() ? std::string() : found->second;
    };
    const auto parseVector = [](const std::string& value, std::array<float, 3>* vector)
    {
        std::istringstream input(value);
        return static_cast<bool>(input >> (*vector)[0] >> (*vector)[1] >> (*vector)[2]) &&
            input.peek() == std::char_traits<char>::eof();
    };

    RuntimeStatus parsed;
    int version = 0;
    int weaponOverride = 0;
    int gunstockApplied = 0;
    int capturedValid = 0;
    ParseInteger(get("VERSION"), &version);
    ParseInteger(get("WEAPON_INDEX"), &parsed.weaponIndex);
    ParseInteger(get("WEAPON_OVERRIDE"), &weaponOverride);
    ParseInteger(get("GUNSTOCK_APPLIED"), &gunstockApplied);
    ParseInteger(get("CAPTURED_ANGLES_VALID"), &capturedValid);
    parsed.status = get("STATUS");
    parsed.requestId = get("REQUEST_ID");
    parsed.message = get("MESSAGE");
    parsed.weaponId = NormalizeId(get("WEAPON_ID"));
    parsed.weaponName = get("WEAPON_NAME");
    parsed.activeGunstockId = NormalizeId(get("ACTIVE_GUNSTOCK"));
    parsed.profileRevision = get("PROFILE_REVISION");
    parsed.effective.weaponId = parsed.weaponId;
    parsed.effective.gunstockId = parsed.activeGunstockId;
    parsed.effective.weaponOverrideApplied = weaponOverride == 1;
    parsed.effective.gunstockApplied = gunstockApplied == 1;
    parsed.capturedAnglesValid = capturedValid == 1;
    const bool valid = version == 1 && !parsed.status.empty() &&
        ParseFloat(get("SHOULDER_BLEND"), &parsed.effective.shoulderedBlend) &&
        parseVector(get("EFFECTIVE_OFFSET"), &parsed.effective.pose.offset) &&
        parseVector(get("EFFECTIVE_ANGLES"), &parsed.effective.pose.angles) &&
        parseVector(
            get("CAPTURED_EFFECTIVE_ANGLES"),
            &parsed.capturedEffectiveAngles) &&
        (parsed.weaponId.empty() || IsSafeId(parsed.weaponId)) &&
        (parsed.activeGunstockId.empty() || IsSafeId(parsed.activeGunstockId));
    if (!valid)
    {
        if (error != nullptr)
        {
            *error = "Invalid beta.12 weapon-calibration runtime status.";
        }
        return false;
    }
    *status = std::move(parsed);
    return true;
}

} // namespace kisak::vr::weapon_profiles
