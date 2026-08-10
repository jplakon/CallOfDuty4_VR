#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kisak::vr::weapon_profiles
{

constexpr std::size_t kMaximumWeaponProfiles = 128u;
constexpr std::size_t kMaximumGunstockProfiles = 32u;
constexpr float kMaximumOffsetInches = 12.0f;
constexpr float kMaximumAngleDegrees = 90.0f;

struct Pose
{
    std::array<float, 3> offset = {};
    std::array<float, 3> angles = {};
};

struct GunstockProfile
{
    std::string id;
    std::string name;
    bool enabled = true;
    Pose shouldered;
};

struct WeaponProfile
{
    std::string id;
    std::string name;
    bool enabled = true;
    Pose hip;
    Pose shouldered;
};

struct Document
{
    std::string activeGunstockId = "generic";
    std::vector<GunstockProfile> gunstocks;
    std::vector<WeaponProfile> weapons;
};

struct EffectiveCalibration
{
    Pose pose;
    std::string weaponId;
    std::string gunstockId;
    bool weaponOverrideApplied = false;
    bool gunstockApplied = false;
    float shoulderedBlend = 0.0f;
};

Document DefaultDocument();

bool IsSafeId(std::string_view value);
std::string NormalizeId(std::string_view value);
bool ValidateDocument(const Document& document, std::string* error = nullptr);
bool ParseDocument(
    const std::string& text,
    Document* document,
    std::string* error = nullptr);
std::string SerializeDocument(const Document& document);
std::string DocumentRevision(const Document& document);

const GunstockProfile* FindGunstock(
    const Document& document,
    std::string_view id);
GunstockProfile* FindGunstock(
    Document& document,
    std::string_view id);
const WeaponProfile* FindWeapon(
    const Document& document,
    std::string_view id);
WeaponProfile* FindWeapon(
    Document& document,
    std::string_view id);

EffectiveCalibration Resolve(
    const Document& document,
    bool profilesEnabled,
    const Pose& globalBaseline,
    std::string_view weaponId,
    float shoulderedBlend);

std::string SerializeGunstock(const GunstockProfile& profile);
bool ParseGunstock(
    const std::string& text,
    GunstockProfile* profile,
    std::string* error = nullptr);

enum class Command
{
    Invalid,
    Reload,
    CaptureAim,
};

enum class CaptureTarget
{
    Gunstock,
    WeaponShouldered,
};

struct Request
{
    std::string requestId;
    Command command = Command::Invalid;
    CaptureTarget target = CaptureTarget::Gunstock;
    std::string weaponId;
    std::string gunstockId;
};

const char* CommandId(Command command);
const char* CaptureTargetId(CaptureTarget target);
std::string SerializeRequest(const Request& request);
bool ParseRequest(
    const std::string& text,
    Request* request,
    std::string* error = nullptr);

struct RuntimeStatus
{
    std::string status = "idle";
    std::string requestId;
    std::string message;
    int weaponIndex = 0;
    std::string weaponId;
    std::string weaponName;
    std::string activeGunstockId;
    std::string profileRevision;
    EffectiveCalibration effective;
    bool capturedAnglesValid = false;
    std::array<float, 3> capturedEffectiveAngles = {};
};

std::string SerializeRuntimeStatus(const RuntimeStatus& status);
bool ParseRuntimeStatus(
    const std::string& text,
    RuntimeStatus* status,
    std::string* error = nullptr);

} // namespace kisak::vr::weapon_profiles
