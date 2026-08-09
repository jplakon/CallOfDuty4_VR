#include "../tools/configurator/settings_core.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

namespace kc = kisak::configurator;

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
    const auto& catalog = kc::SettingsCatalog();
    Check(catalog.size() >= 55u, "catalog should expose at least 55 settings");

    std::set<std::string> keys;
    for (const kc::SettingDefinition& definition : catalog)
    {
        Check(keys.insert(definition.key).second, "duplicate key: " + definition.key);
    }

    kc::SettingsMap values = kc::BuiltInDefaults();
    auto messages = kc::ValidateSettings(values);
    Check(!HasError(messages, "VR_CUSTOM_MODE"), "built-in defaults should validate");
    Check(messages.empty(), "built-in defaults should have no warnings or errors");

    if (argumentCount >= 2)
    {
        const std::filesystem::path releaseDefaults = arguments[1];
        const kc::LoadResult packaged = kc::LoadSettings(
            releaseDefaults,
            releaseDefaults.parent_path() / "__missing-v56-user-settings.bat");
        Check(packaged.messages.empty(), "packaged release defaults should validate cleanly");
        Check(packaged.values == values, "packaged release defaults should match the configurator catalog");
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
    values["KISAK_VR_BIND_USE"] = "y";
    messages = kc::ValidateSettings(values);
    Check(HasError(messages, "KISAK_VR_BIND_USE"), "left-button conflicts should be rejected");

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

    const std::filesystem::path temp =
        std::filesystem::temp_directory_path() / "kisakcod-configurator-core-test";
    std::error_code error;
    std::filesystem::remove_all(temp, error);
    std::filesystem::create_directories(temp, error);
    Check(!error, "temporary test directory should be created");

    const std::filesystem::path userFile = temp / "VR-User-Settings.bat";
    values = kc::BuiltInDefaults();
    values["KISAK_VR_SNAP_TURN_ANGLE"] = "30";
    kc::SaveResult saved = kc::SaveUserSettingsAtomic(userFile, values, "Test profile");
    Check(saved.success, "first atomic save should succeed: " + saved.error);
    Check(saved.backupPath.empty(), "first save should not create a backup");
    Check(Read(userFile).find("\r\n") != std::string::npos, "saved batch file should use CRLF");

    kc::LoadResult loaded = kc::LoadSettings(temp / "missing-defaults.bat", userFile);
    Check(loaded.values["KISAK_VR_SNAP_TURN_ANGLE"] == "30", "saved settings should round-trip");
    Check(loaded.messages.empty(), "round-tripped settings should validate");

    values["KISAK_VR_SNAP_TURN_ANGLE"] = "60";
    saved = kc::SaveUserSettingsAtomic(userFile, values, "Second profile");
    Check(saved.success, "second atomic save should succeed: " + saved.error);
    Check(!saved.backupPath.empty(), "second save should create a backup");
    Check(std::filesystem::is_regular_file(saved.backupPath), "backup file should exist");
    loaded = kc::LoadSettings(temp / "missing-defaults.bat", userFile);
    Check(loaded.values["KISAK_VR_SNAP_TURN_ANGLE"] == "60", "replacement settings should become active");

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
