#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace kisak::configurator
{

enum class SettingType
{
    Toggle,
    Integer,
    Decimal,
    Choice,
};

enum class SettingPage
{
    Quick,
    Hud,
    Weapons,
    Interactions,
    Scope,
    Graphics,
    Controls,
    Advanced,
};

struct SettingChoice
{
    std::string value;
    std::string label;
};

struct SettingDefinition
{
    std::string key;
    std::string label;
    std::string description;
    SettingType type = SettingType::Choice;
    SettingPage page = SettingPage::Quick;
    std::string defaultValue;
    double minimumValue = 0.0;
    double maximumValue = 0.0;
    int decimalPlaces = 0;
    bool advanced = false;
    std::vector<SettingChoice> choices;
};

struct ValidationMessage
{
    enum class Severity
    {
        Warning,
        Error,
    };

    Severity severity = Severity::Error;
    std::string key;
    std::string message;
};

using SettingsMap = std::map<std::string, std::string>;

struct LoadResult
{
    SettingsMap values;
    std::vector<ValidationMessage> messages;
    bool userFileFound = false;
};

struct SaveResult
{
    bool success = false;
    std::filesystem::path settingsPath;
    std::filesystem::path backupPath;
    std::string error;
};

const std::vector<SettingDefinition>& SettingsCatalog();
const SettingDefinition* FindSetting(const std::string& key);
SettingsMap BuiltInDefaults();

SettingsMap ParseBatchSettings(
    const std::string& text,
    std::vector<ValidationMessage>* messages = nullptr);

std::vector<ValidationMessage> ValidateSettings(
    const SettingsMap& values);

LoadResult LoadSettings(
    const std::filesystem::path& defaultsPath,
    const std::filesystem::path& userPath);

std::string SerializeUserSettings(
    const SettingsMap& values,
    const std::string& profileName = "Custom");

SaveResult SaveUserSettingsAtomic(
    const std::filesystem::path& userPath,
    const SettingsMap& values,
    const std::string& profileName = "Custom");

bool ApplyPreset(
    const std::string& presetName,
    SettingsMap* values);

std::vector<std::string> PresetNames();

} // namespace kisak::configurator
