#include "settings_core.h"
#include "compatibility_probe_win32.h"
#include "vr/vr_calibration.h"
#include "vr/vr_compatibility.h"
#include "vr/vr_input_bindings.h"
#include "vr/vr_weapon_profiles.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace kc = kisak::configurator;
namespace vi = kisak::vr::input;
namespace vc = kisak::vr::calibration;
namespace vrc = kisak::vr::compatibility;
namespace vh = kisak::vr::hud;
namespace vwp = kisak::vr::weapon_profiles;
namespace wc = kisak::configurator::win32_compatibility;

namespace
{

constexpr wchar_t kWindowClass[] = L"KisakCODVrConfiguratorV65";
constexpr wchar_t kPreviewClass[] = L"KisakCODVrPreviewV65";
constexpr wchar_t kChordEditorClass[] =
    L"KisakCODVrBindingChordEditorV65";
constexpr wchar_t kHudEditorClass[] =
    L"KisakCODVrVisualHudEditorV65";
constexpr wchar_t kWeaponEditorClass[] =
    L"KisakCODVrWeaponCalibrationEditorV65";
constexpr wchar_t kWindowTitle[] =
    L"KisakCOD VR Configurator - v0.10.0-beta.11";

constexpr int kWindowWidth = 1160;
constexpr int kWindowHeight = 790;
constexpr int kTabLeft = 18;
constexpr int kTabTop = 108;
constexpr int kTabWidth = 744;
constexpr int kTabHeight = 590;
constexpr int kPreviewLeft = 780;
constexpr int kPreviewTop = 108;
constexpr int kPreviewWidth = 356;
constexpr int kPreviewHeight = 446;

constexpr int kIdTabs = 100;
constexpr int kIdPreset = 101;
constexpr int kIdApplyPreset = 102;
constexpr int kIdAdvanced = 103;
constexpr int kIdRestoreDefaults = 104;
constexpr int kIdImport = 105;
constexpr int kIdExport = 106;
constexpr int kIdSave = 107;
constexpr int kIdSaveLaunch = 108;
constexpr int kIdDiagnostics = 109;
constexpr int kIdBindingActionList = 110;
constexpr int kIdBindingPrimary = 111;
constexpr int kIdBindingAlternate = 112;
constexpr int kIdCapturePrimary = 113;
constexpr int kIdCaptureAlternate = 114;
constexpr int kIdClearBinding = 115;
constexpr int kIdChordPrimary = 116;
constexpr int kIdChordAlternate = 117;
constexpr int kIdCalibrationRecenterFull = 118;
constexpr int kIdCalibrationMeasureStanding = 119;
constexpr int kIdCalibrationApplySeated = 120;
constexpr int kIdCalibrationShorter = 121;
constexpr int kIdCalibrationResetHeight = 122;
constexpr int kIdCalibrationTaller = 123;
constexpr int kIdHudVisualEditor = 124;
constexpr int kIdHudHeadsetEditor = 125;
constexpr int kIdHudPollTimer = 126;
constexpr int kIdWeaponEditor = 127;
constexpr int kIdWeaponRefresh = 128;
constexpr int kIdWeaponPollTimer = 129;
constexpr int kIdSetupRescan = 130;
constexpr int kIdSetupApplyRecommended = 131;
constexpr int kIdSetupCopyReport = 132;
constexpr int kIdSetupOpenReport = 133;
constexpr int kIdCalibrationRecenterPosition = 134;
constexpr int kIdCalibrationRecenterDirectionLevel = 135;
constexpr int kIdSettingBase = 2000;

constexpr int kIdWeaponEditWeapon = 500;
constexpr int kIdWeaponEditUseCurrent = 501;
constexpr int kIdWeaponEditDeleteWeapon = 502;
constexpr int kIdWeaponEditGunstock = 503;
constexpr int kIdWeaponEditNewGunstock = 504;
constexpr int kIdWeaponEditDeleteGunstock = 505;
constexpr int kIdWeaponEditImportGunstock = 506;
constexpr int kIdWeaponEditExportGunstock = 507;
constexpr int kIdWeaponEditLayer = 508;
constexpr int kIdWeaponEditEnabled = 509;
constexpr int kIdWeaponEditName = 510;
constexpr int kIdWeaponEditValueBase = 520;
constexpr int kIdWeaponEditReset = 526;
constexpr int kIdWeaponEditApplyLive = 527;
constexpr int kIdWeaponEditCapture = 528;

const std::array<kc::SettingPage, 9> kPageOrder = {
    kc::SettingPage::Quick,
    kc::SettingPage::Calibration,
    kc::SettingPage::Hud,
    kc::SettingPage::Weapons,
    kc::SettingPage::Interactions,
    kc::SettingPage::Scope,
    kc::SettingPage::Graphics,
    kc::SettingPage::Controls,
    kc::SettingPage::Advanced,
};

const std::array<const wchar_t*, 9> kPageNames = {
    L"Setup & Compatibility",
    L"Height & Recenter",
    L"HUD & Text",
    L"Weapons & Hands",
    L"Interactions",
    L"Scope",
    L"Graphics",
    L"Controls",
    L"Advanced",
};

std::wstring ToWide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);

    if (length <= 0)
    {
        return std::wstring(value.begin(), value.end());
    }

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        length);
    return result;
}

std::string ToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int length = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (length <= 0)
    {
        return std::string(value.begin(), value.end());
    }

    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        length,
        nullptr,
        nullptr);
    return result;
}

std::wstring WindowText(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    std::wstring result(static_cast<std::size_t>(length) + 1u, L'\0');
    if (length > 0)
    {
        GetWindowTextW(window, result.data(), length + 1);
    }
    result.resize(static_cast<std::size_t>(length));
    return result;
}

std::filesystem::path ExecutableDirectory()
{
    std::wstring path(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr,
        path.data(),
        static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path UserSettingsPath()
{
    std::wstring localAppData(32768u, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        localAppData.data(),
        static_cast<DWORD>(localAppData.size()));

    if (length > 0 && length < localAppData.size())
    {
        localAppData.resize(length);
        return std::filesystem::path(localAppData) /
            L"KisakCOD-VR" /
            L"VR-User-Settings.bat";
    }

    return ExecutableDirectory() /
        L"UserSettings" /
        L"VR-User-Settings.bat";
}

std::filesystem::path UserStateDirectory()
{
    return UserSettingsPath().parent_path();
}

std::filesystem::path UserWeaponProfilesPath()
{
    return UserStateDirectory() / L"VR-Weapon-Profiles.ini";
}

std::filesystem::path UserWeaponCalibrationRequestPath()
{
    return UserStateDirectory() / L"Weapon-Calibration-Request.txt";
}

std::filesystem::path UserWeaponCalibrationStatusPath()
{
    return UserStateDirectory() / L"Weapon-Calibration-Status.txt";
}

std::filesystem::path UserRuntimeReceiptPath()
{
    return UserStateDirectory() / L"Active-VR-Settings.txt";
}

std::filesystem::path UserCompatibilityReportPath()
{
    return UserStateDirectory() / L"Compatibility-Report.txt";
}

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

bool WriteTextFileAtomic(
    const std::filesystem::path& path,
    const std::string& text,
    std::string* const error)
{
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError)
    {
        if (error != nullptr)
        {
            *error = "Could not create the calibration settings folder.";
        }
        return false;
    }

    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(
            temporary,
            std::ios::binary | std::ios::trunc);
        if (!output)
        {
            if (error != nullptr)
            {
                *error = "Could not open the temporary calibration file.";
            }
            return false;
        }
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.flush();
        if (!output)
        {
            std::filesystem::remove(temporary, filesystemError);
            if (error != nullptr)
            {
                *error = "The calibration file could not be flushed completely.";
            }
            return false;
        }
    }

    if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary, filesystemError);
        if (error != nullptr)
        {
            *error = "Windows could not atomically replace the calibration file.";
        }
        return false;
    }
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

bool SaveWeaponProfiles(
    const std::filesystem::path& path,
    const vwp::Document& document,
    std::string* const error)
{
    if (!vwp::ValidateDocument(document, error))
    {
        return false;
    }
    const std::string serialized = vwp::SerializeDocument(document);
    if (!WriteTextFileAtomic(path, serialized, error))
    {
        return false;
    }
    vwp::Document verified;
    std::string verifyError;
    if (!vwp::ParseDocument(ReadTextFile(path), &verified, &verifyError) ||
        vwp::SerializeDocument(verified) != serialized)
    {
        if (error != nullptr)
        {
            *error = "Weapon profiles failed mandatory disk read-back: " +
                verifyError;
        }
        return false;
    }
    return true;
}

vwp::Document LoadWeaponProfiles(
    const std::filesystem::path& path,
    std::string* const error)
{
    if (!std::filesystem::is_regular_file(path))
    {
        if (error != nullptr)
        {
            error->clear();
        }
        return vwp::DefaultDocument();
    }
    vwp::Document document;
    if (!vwp::ParseDocument(ReadTextFile(path), &document, error))
    {
        return vwp::DefaultDocument();
    }
    return document;
}

double NumberValue(
    const kc::SettingsMap& values,
    const char* key,
    const double fallback)
{
    const auto found = values.find(key);
    if (found == values.end())
    {
        return fallback;
    }

    char* end = nullptr;
    const double parsed = std::strtod(found->second.c_str(), &end);
    return end != found->second.c_str() && end != nullptr && *end == '\0' &&
                   std::isfinite(parsed)
        ? parsed
        : fallback;
}

std::string StringValue(
    const kc::SettingsMap& values,
    const char* key,
    const char* fallback)
{
    const auto found = values.find(key);
    return found == values.end() ? fallback : found->second;
}

kc::MeasurementUnitSystem ActiveMeasurementUnits(
    const kc::SettingsMap& values)
{
    return kc::MeasurementUnitsFromSettings(values);
}

std::wstring DisplayMeasurement(
    const kc::SettingsMap& values,
    const char* const key,
    const std::string& canonicalValue)
{
    const kc::SettingDefinition* const definition =
        kc::FindSetting(key);
    if (definition == nullptr)
    {
        return ToWide(canonicalValue);
    }

    const kc::MeasurementUnitSystem units =
        ActiveMeasurementUnits(values);
    std::string displayValue;
    if (!kc::CanonicalValueToDisplay(
            *definition,
            units,
            canonicalValue,
            &displayValue))
    {
        displayValue = canonicalValue;
    }

    const std::string suffix =
        kc::MeasurementUnitSuffix(*definition, units);
    return ToWide(
        suffix.empty()
            ? displayValue
            : displayValue + " " + suffix);
}

struct ControlBinding
{
    const kc::SettingDefinition* definition = nullptr;
    HWND label = nullptr;
    HWND control = nullptr;
    std::wstring description;
    std::string lastRenderedDisplayValue;
    std::string lastRenderedCanonicalValue;
};

struct AppState
{
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HWND tabs = nullptr;
    HWND preview = nullptr;
    HWND preset = nullptr;
    HWND advanced = nullptr;
    HWND status = nullptr;
    HWND hint = nullptr;
    HWND settingsPath = nullptr;
    HWND tooltip = nullptr;
    HWND bindingActionList = nullptr;
    HWND bindingPrimary = nullptr;
    HWND bindingAlternate = nullptr;
    HWND capturePrimary = nullptr;
    HWND captureAlternate = nullptr;
    HWND chordPrimary = nullptr;
    HWND chordAlternate = nullptr;
    HWND clearBinding = nullptr;
    std::vector<HWND> bindingEditorControls;
    HWND calibrationStatus = nullptr;
    HWND calibrationShorter = nullptr;
    HWND calibrationResetHeight = nullptr;
    HWND calibrationTaller = nullptr;
    std::vector<HWND> calibrationControls;
    HWND hudStatus = nullptr;
    std::vector<HWND> hudControls;
    HWND weaponStatus = nullptr;
    std::vector<HWND> weaponControls;
    HWND setupStatus = nullptr;
    HWND setupDetails = nullptr;
    HWND setupRecommendation = nullptr;
    HWND setupApplyRecommended = nullptr;
    std::vector<HWND> setupControls;
    HFONT font = nullptr;
    HFONT titleFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH previewBrush = nullptr;

    std::filesystem::path gameDirectory;
    std::filesystem::path defaultsPath;
    std::filesystem::path userPath;
    std::filesystem::path activePath;
    std::filesystem::path weaponProfilesPath;
    std::filesystem::path weaponCalibrationRequestPath;
    std::filesystem::path weaponCalibrationStatusPath;
    std::filesystem::path runtimeReceiptPath;
    std::filesystem::path compatibilityReportPath;
    kc::SettingsMap values;
    std::vector<ControlBinding> bindings;
    std::vector<kc::ValidationMessage> validation;
    int selectedPage = 0;
    bool advancedMode = false;
    bool dirty = false;
    bool suppressEvents = false;
    std::string profileName = "Custom";
    std::string revision;
    std::string lastSavedAt;
    std::size_t verifiedSettingCount = 0u;
    bool readBackVerified = false;
    std::string pendingHudEditorRequestId;
    bool pendingHudEditorLaunched = false;
    ULONGLONG pendingHudEditorStartedAt = 0u;
    vwp::Document weaponProfiles = vwp::DefaultDocument();
    vwp::RuntimeStatus weaponRuntimeStatus;
    std::string weaponProfilesLoadError;
    std::string pendingWeaponRequestId;
    ULONGLONG pendingWeaponRequestStartedAt = 0u;
    vrc::Probe compatibilityProbe;
    vrc::Report compatibilityReport;
    std::string compatibilityReportError;
};

struct ChordEditorState
{
    AppState* app = nullptr;
    vi::Action action = vi::Action::Attack;
    HWND window = nullptr;
    HWND sourceList = nullptr;
    bool accepted = false;
    std::string result = "unbound";
};

struct HudEditorState
{
    AppState* app = nullptr;
    HWND window = nullptr;
    vh::Layout layout;
    vh::Element selected = vh::Element::AmmoEquipment;
    bool accepted = false;
    bool dragging = false;
    bool resizing = false;
    bool snapEnabled = true;
    POINT dragOffset = {};
    POINT dragStart = {};
    float resizeStartScale = 1.0f;
};

enum class WeaponEditorLayer
{
    WeaponHip,
    WeaponShouldered,
    Gunstock,
};

struct WeaponEditorState
{
    AppState* app = nullptr;
    HWND window = nullptr;
    HWND weaponCombo = nullptr;
    HWND gunstockCombo = nullptr;
    HWND layerCombo = nullptr;
    HWND enabled = nullptr;
    std::array<HWND, 6> valueEdits = {};
    HWND status = nullptr;
    vwp::Document document;
    vwp::RuntimeStatus runtimeStatus;
    std::string selectedWeaponId;
    std::string selectedGunstockId;
    WeaponEditorLayer layer = WeaponEditorLayer::WeaponHip;
    bool accepted = false;
    bool suppressEvents = false;
};

void UpdateControllerBindingEditor(AppState& state);
void ShowControllerBindingEditor(AppState& state, bool show);
void ShowCalibrationPanel(AppState& state, bool show);
void ShowHudPanel(AppState& state, bool show);
void ShowWeaponPanel(AppState& state, bool show);
void ShowSetupPanel(AppState& state, bool show);
void RefreshCompatibility(AppState& state, bool showWriteError);
LRESULT CALLBACK ChordEditorWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);
LRESULT CALLBACK HudEditorWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);
LRESULT CALLBACK WeaponEditorWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam);

void SetFont(HWND control, HFONT font)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND CreateControl(
    AppState& state,
    const wchar_t* className,
    const wchar_t* text,
    const DWORD style,
    const int x,
    const int y,
    const int width,
    const int height,
    const int identifier = 0,
    const DWORD extendedStyle = 0)
{
    HWND control = CreateWindowExW(
        extendedStyle,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
        state.instance,
        nullptr);
    SetFont(control, state.font);
    return control;
}

int PageIndex(const kc::SettingPage page)
{
    const auto found = std::find(kPageOrder.begin(), kPageOrder.end(), page);
    return found == kPageOrder.end()
        ? 0
        : static_cast<int>(std::distance(kPageOrder.begin(), found));
}

void AddTooltip(AppState& state, HWND control, std::wstring* text)
{
    TOOLINFOW tool = {};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = state.window;
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    tool.lpszText = text->data();
    SendMessageW(
        state.tooltip,
        TTM_ADDTOOLW,
        0,
        reinterpret_cast<LPARAM>(&tool));
}

void SetControlFromValue(
    ControlBinding& binding,
    const std::string& value,
    const kc::MeasurementUnitSystem units)
{
    const kc::SettingDefinition& definition = *binding.definition;
    if (definition.type == kc::SettingType::Choice ||
        definition.type == kc::SettingType::Toggle ||
        definition.type == kc::SettingType::Binding)
    {
        int selected = 0;
        for (std::size_t index = 0; index < definition.choices.size(); ++index)
        {
            if (definition.choices[index].value == value)
            {
                selected = static_cast<int>(index);
                break;
            }
        }
        SendMessageW(binding.control, CB_SETCURSEL, selected, 0);
    }
    else
    {
        std::string displayValue = value;
        kc::CanonicalValueToDisplay(
            definition,
            units,
            value,
            &displayValue);
        binding.lastRenderedDisplayValue = displayValue;
        binding.lastRenderedCanonicalValue = value;
        SetWindowTextW(
            binding.control,
            ToWide(displayValue).c_str());
    }
}

std::string ValueFromControl(
    const ControlBinding& binding,
    const kc::MeasurementUnitSystem units)
{
    const kc::SettingDefinition& definition = *binding.definition;
    if (definition.type == kc::SettingType::Choice ||
        definition.type == kc::SettingType::Toggle ||
        definition.type == kc::SettingType::Binding)
    {
        const LRESULT selected =
            SendMessageW(binding.control, CB_GETCURSEL, 0, 0);
        if (selected >= 0 &&
            static_cast<std::size_t>(selected) < definition.choices.size())
        {
            return definition.choices[static_cast<std::size_t>(selected)].value;
        }
        return definition.defaultValue;
    }

    const std::string displayValue =
        ToUtf8(WindowText(binding.control));
    if (definition.measurementKind == kc::MeasurementKind::None)
    {
        return displayValue;
    }

    if (displayValue == binding.lastRenderedDisplayValue)
    {
        return binding.lastRenderedCanonicalValue;
    }

    std::string canonicalValue;
    return kc::DisplayValueToCanonical(
               definition,
               units,
               displayValue,
               &canonicalValue)
        ? canonicalValue
        : displayValue;
}

void UpdateMeasurementPresentation(AppState& state)
{
    const kc::MeasurementUnitSystem units =
        ActiveMeasurementUnits(state.values);

    for (ControlBinding& binding : state.bindings)
    {
        SetWindowTextW(
            binding.label,
            ToWide(kc::DisplaySettingLabel(
                *binding.definition,
                units)).c_str());
    }

    if (state.calibrationShorter != nullptr)
    {
        SetWindowTextW(
            state.calibrationShorter,
            units == kc::MeasurementUnitSystem::Metric
                ? L"1 cm shorter"
                : L"1 in shorter");
    }
    if (state.calibrationResetHeight != nullptr)
    {
        SetWindowTextW(
            state.calibrationResetHeight,
            units == kc::MeasurementUnitSystem::Metric
                ? L"Reset current mode to 152.4 cm"
                : L"Reset current mode to 60 in");
    }
    if (state.calibrationTaller != nullptr)
    {
        SetWindowTextW(
            state.calibrationTaller,
            units == kc::MeasurementUnitSystem::Metric
                ? L"1 cm taller"
                : L"1 in taller");
    }

    if (state.preview != nullptr)
    {
        InvalidateRect(state.preview, nullptr, TRUE);
    }
}

void UpdateAllControls(AppState& state)
{
    state.suppressEvents = true;
    const kc::MeasurementUnitSystem units =
        ActiveMeasurementUnits(state.values);
    for (ControlBinding& binding : state.bindings)
    {
        const auto found = state.values.find(binding.definition->key);
        SetControlFromValue(
            binding,
            found == state.values.end()
                ? binding.definition->defaultValue
                : found->second,
            units);
    }
    state.suppressEvents = false;
    UpdateMeasurementPresentation(state);
    UpdateControllerBindingEditor(state);
    if (state.preview != nullptr)
    {
        InvalidateRect(state.preview, nullptr, TRUE);
    }
}

void ReadAllControls(AppState& state)
{
    const kc::MeasurementUnitSystem units =
        ActiveMeasurementUnits(state.values);
    for (const ControlBinding& binding : state.bindings)
    {
        state.values[binding.definition->key] =
            ValueFromControl(binding, units);
    }
}

void UpdateSettingsIdentity(AppState& state)
{
    if (state.settingsPath == nullptr)
    {
        return;
    }

    const std::filesystem::path& activePath =
        state.activePath.empty() ? state.defaultsPath : state.activePath;
    std::wstring identity =
        L"Active profile: " + ToWide(state.profileName) +
        L"  |  File: " + activePath.wstring();
    if (!state.revision.empty())
    {
        identity += L"  |  Revision: " + ToWide(state.revision);
    }
    SetWindowTextW(state.settingsPath, identity.c_str());
}

void MarkSettingsDirty(
    AppState& state,
    const std::string& profileName)
{
    state.dirty = true;
    state.profileName = profileName;
    state.revision.clear();
    state.lastSavedAt.clear();
    state.verifiedSettingCount = 0u;
    state.readBackVerified = false;
}

void UpdateValidation(AppState& state)
{
    ReadAllControls(state);
    state.validation = kc::ValidateSettings(state.values);

    int errors = 0;
    int warnings = 0;
    for (const kc::ValidationMessage& message : state.validation)
    {
        if (message.severity == kc::ValidationMessage::Severity::Error)
        {
            ++errors;
        }
        else
        {
            ++warnings;
        }
    }

    std::wostringstream status;
    if (errors == 0 && warnings == 0)
    {
        if (state.dirty)
        {
            status << L"Unsaved changes - all settings are valid.";
        }
        else if (state.readBackVerified)
        {
            status << L"Saved and read-back verified: "
                   << state.verifiedSettingCount << L"/"
                   << kc::SettingsCatalog().size() << L" settings";
            if (!state.lastSavedAt.empty())
            {
                status << L" at " << ToWide(state.lastSavedAt);
            }
            status << L".";
        }
        else
        {
            status << L"Settings loaded and valid. Save once to create a verified revision.";
        }
    }
    else
    {
        status << errors << L" error" << (errors == 1 ? L"" : L"s")
               << L", " << warnings << L" warning"
               << (warnings == 1 ? L"" : L"s");
        if (!state.validation.empty())
        {
            status << L" - " << ToWide(state.validation.front().message);
        }
    }

    SetWindowTextW(state.status, status.str().c_str());
    UpdateSettingsIdentity(state);
    InvalidateRect(state.preview, nullptr, TRUE);
}

std::wstring CompatibilityStatusLabel(const vrc::Status status)
{
    switch (status)
    {
        case vrc::Status::Ready:
            return L"READY";
        case vrc::Status::Warning:
            return L"READY WITH WARNINGS";
        case vrc::Status::Blocked:
            return L"BLOCKED";
        default:
            return L"UNKNOWN";
    }
}

std::wstring CompatibilityCheckPrefix(const vrc::Status status)
{
    switch (status)
    {
        case vrc::Status::Ready:
            return L"[PASS] ";
        case vrc::Status::Warning:
            return L"[WARN] ";
        case vrc::Status::Blocked:
            return L"[BLOCK] ";
        default:
            return L"[?] ";
    }
}

std::wstring CompatibilityDetailsText(const vrc::Report& report)
{
    std::wostringstream output;
    for (const vrc::Check& check : report.checks)
    {
        output << CompatibilityCheckPrefix(check.status)
               << ToWide(check.label) << L": "
               << ToWide(check.detail);
        if (!check.action.empty())
        {
            output << L"  Action: " << ToWide(check.action);
        }
        output << L"\r\n";
    }
    return output.str();
}

void RefreshCompatibility(
    AppState& state,
    const bool showWriteError)
{
    ReadAllControls(state);
    state.compatibilityProbe = wc::ProbeSystem(
        state.gameDirectory,
        state.runtimeReceiptPath,
        StringValue(state.values, "KISAK_VR_BACKEND", "auto"),
        StringValue(state.values, "VR_CUSTOM_MODE", "6016x2688"),
        StringValue(state.values, "KISAK_VR_OUTPUT_SCALE", "1.00"));
    state.compatibilityReport = vrc::Evaluate(state.compatibilityProbe);

    const std::string reportText = vrc::SerializeReport(
        state.compatibilityProbe,
        state.compatibilityReport,
        wc::LocalTimestamp());
    state.compatibilityReportError.clear();
    const bool reportWritten = wc::WriteReportAtomic(
        state.compatibilityReportPath,
        reportText,
        &state.compatibilityReportError);

    if (state.setupStatus != nullptr)
    {
        std::wstring status = L"Compatibility: " +
            CompatibilityStatusLabel(state.compatibilityReport.status);
        status += state.compatibilityReport.headsetTestRequired
            ? L"  |  Headset test still required"
            : L"  |  Prior headset test recorded";
        SetWindowTextW(state.setupStatus, status.c_str());
    }
    if (state.setupDetails != nullptr)
    {
        const std::wstring details =
            CompatibilityDetailsText(state.compatibilityReport);
        SetWindowTextW(state.setupDetails, details.c_str());
    }
    if (state.setupRecommendation != nullptr)
    {
        std::wstring recommendation = L"Recommended: " +
            ToWide(state.compatibilityReport.recommendationSummary);
        if (!reportWritten)
        {
            recommendation += L"  Report write failed: " +
                ToWide(state.compatibilityReportError);
        }
        SetWindowTextW(
            state.setupRecommendation,
            recommendation.c_str());
    }

    const bool backendChange =
        !state.compatibilityReport.recommendedBackend.empty() &&
        state.compatibilityReport.recommendedBackend !=
            StringValue(state.values, "KISAK_VR_BACKEND", "auto");
    const bool graphicsChange =
        (state.compatibilityReport.recommendedGraphicsProfile == "native" &&
         StringValue(state.values, "VR_CUSTOM_MODE", "6016x2688") !=
             "6016x2688") ||
        (state.compatibilityReport.recommendedGraphicsProfile == "performance" &&
         StringValue(state.values, "VR_CUSTOM_MODE", "6016x2688") !=
             "4768x2016");
    if (state.setupApplyRecommended != nullptr)
    {
        EnableWindow(
            state.setupApplyRecommended,
            backendChange || graphicsChange);
    }

    if (!reportWritten && showWriteError)
    {
        MessageBoxW(
            state.window,
            ToWide(state.compatibilityReportError).c_str(),
            L"Compatibility report could not be saved",
            MB_OK | MB_ICONWARNING);
    }
    if (state.preview != nullptr)
    {
        InvalidateRect(state.preview, nullptr, TRUE);
    }
}

void ApplyRecommendedCompatibility(AppState& state)
{
    ReadAllControls(state);
    const std::string currentBackend =
        StringValue(state.values, "KISAK_VR_BACKEND", "auto");
    const std::string currentMode =
        StringValue(state.values, "VR_CUSTOM_MODE", "6016x2688");
    const std::string recommendedBackend =
        state.compatibilityReport.recommendedBackend;
    const std::string recommendedGraphics =
        state.compatibilityReport.recommendedGraphicsProfile;

    std::wostringstream changes;
    if (!recommendedBackend.empty() &&
        recommendedBackend != currentBackend)
    {
        changes << L"Runtime backend: " << ToWide(currentBackend)
                << L" -> " << ToWide(recommendedBackend) << L"\r\n";
    }

    const std::string recommendedMode =
        recommendedGraphics == "performance"
            ? "4768x2016"
            : "6016x2688";
    if (recommendedMode != currentMode)
    {
        changes << L"Graphics profile: "
                << (currentMode == "4768x2016" ? L"Performance" : L"Native")
                << L" -> "
                << (recommendedGraphics == "performance" ? L"Performance" : L"Native")
                << L"\r\n";
    }

    if (changes.str().empty())
    {
        MessageBoxW(
            state.window,
            L"The recommended backend and graphics profile are already active.",
            L"Recommended setup",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring prompt =
        L"Apply only these detected compatibility changes?\r\n\r\n" +
        changes.str() +
        L"\r\nHandedness, units, comfort, controls, HUD, weapon profiles, and calibration will not change.";
    if (MessageBoxW(
            state.window,
            prompt.c_str(),
            L"Apply recommended compatibility setup",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    if (!recommendedBackend.empty())
    {
        state.values["KISAK_VR_BACKEND"] = recommendedBackend;
    }
    if (recommendedGraphics == "performance")
    {
        state.values["VR_CUSTOM_MODE"] = "4768x2016";
        state.values["KISAK_VR_OUTPUT_SCALE"] = "0.75";
        state.values["KISAK_VR_FSR"] = "1";
        state.values["KISAK_VR_FSR_SHARPNESS"] = "0.60";
        state.values["KISAK_VR_SCOPE_CAPTURE_SIZE"] = "768";
    }
    else if (recommendedGraphics == "native")
    {
        state.values["VR_CUSTOM_MODE"] = "6016x2688";
        state.values["KISAK_VR_OUTPUT_SCALE"] = "1.00";
        state.values["KISAK_VR_FSR"] = "0";
        state.values["KISAK_VR_SCOPE_CAPTURE_SIZE"] = "1024";
    }

    MarkSettingsDirty(state, "Compatibility recommendation");
    UpdateAllControls(state);
    UpdateValidation(state);
    RefreshCompatibility(state, true);
}

void CopyCompatibilityReport(AppState& state)
{
    const std::string report = vrc::SerializeReport(
        state.compatibilityProbe,
        state.compatibilityReport,
        wc::LocalTimestamp());
    const std::wstring wideReport = ToWide(report);
    if (!OpenClipboard(state.window))
    {
        MessageBoxW(
            state.window,
            L"Windows could not open the clipboard.",
            L"Copy compatibility report",
            MB_OK | MB_ICONWARNING);
        return;
    }

    EmptyClipboard();
    const SIZE_T bytes =
        (wideReport.size() + 1u) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr)
    {
        void* destination = GlobalLock(memory);
        if (destination != nullptr)
        {
            std::memcpy(destination, wideReport.c_str(), bytes);
            GlobalUnlock(memory);
            if (SetClipboardData(CF_UNICODETEXT, memory) != nullptr)
            {
                memory = nullptr;
            }
        }
    }
    if (memory != nullptr)
    {
        GlobalFree(memory);
    }
    CloseClipboard();
}

void OpenCompatibilityReport(AppState& state)
{
    RefreshCompatibility(state, true);
    if (!std::filesystem::is_regular_file(state.compatibilityReportPath))
    {
        return;
    }
    const HINSTANCE result = ShellExecuteW(
        state.window,
        L"open",
        state.compatibilityReportPath.c_str(),
        nullptr,
        state.compatibilityReportPath.parent_path().c_str(),
        SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        MessageBoxW(
            state.window,
            L"Windows could not open Compatibility-Report.txt.",
            L"Open compatibility report",
            MB_OK | MB_ICONWARNING);
    }
}

void LayoutSettings(AppState& state)
{
    const bool setupPage =
        state.selectedPage == PageIndex(kc::SettingPage::Quick);
    const bool controllerPage =
        state.selectedPage == PageIndex(kc::SettingPage::Controls);
    const bool calibrationPage =
        state.selectedPage == PageIndex(kc::SettingPage::Calibration);
    const bool hudPage =
        state.selectedPage == PageIndex(kc::SettingPage::Hud);
    const bool weaponPage =
        state.selectedPage == PageIndex(kc::SettingPage::Weapons);

    std::vector<ControlBinding*> visible;
    for (ControlBinding& binding : state.bindings)
    {
        const bool onPage =
            PageIndex(binding.definition->page) == state.selectedPage;
        const bool permitted =
            state.advancedMode || !binding.definition->advanced;
        const bool show =
            !controllerPage && onPage && permitted;
        ShowWindow(binding.label, show ? SW_SHOW : SW_HIDE);
        ShowWindow(binding.control, show ? SW_SHOW : SW_HIDE);
        if (show)
        {
            visible.push_back(&binding);
        }
    }

    ShowControllerBindingEditor(state, controllerPage);
    ShowCalibrationPanel(state, calibrationPage);
    ShowHudPanel(state, hudPage);
    ShowWeaponPanel(state, weaponPage);
    ShowSetupPanel(state, setupPage);

    const int pageX = kTabLeft + 18;
    const int pageY = setupPage
        ? kTabTop + 330
        : (hudPage
            ? kTabTop + 140
            : (weaponPage ? kTabTop + 170 : kTabTop + 54));
    const int count = static_cast<int>(visible.size());
    const int columns = setupPage
        ? 3
        : (count > 20 ? 3 : (count > 8 ? 2 : 1));
    const int rows = (count + columns - 1) / columns;
    const int columnWidth =
        columns == 1 ? 520 : (columns == 2 ? 344 : 230);
    const int rowHeight = setupPage
        ? 55
        : ((hudPage || weaponPage) && rows > 7
        ? 43
        : (rows > 7 ? 54 : 62));

    for (int index = 0; index < count; ++index)
    {
        const int column = columns == 1 ? 0 : index / rows;
        const int row = columns == 1 ? index : index % rows;
        const int x = pageX + column * columnWidth;
        const int y = pageY + row * rowHeight;
        const int controlWidth =
            columns == 1 ? 300 : (columns == 2 ? 292 : 214);

        MoveWindow(visible[index]->label, x, y, controlWidth, 18, TRUE);
        MoveWindow(
            visible[index]->control,
            x,
            y + 20,
            controlWidth,
            visible[index]->definition->type == kc::SettingType::Choice ||
                    visible[index]->definition->type == kc::SettingType::Toggle ||
                    visible[index]->definition->type == kc::SettingType::Binding
                ? 220
                : 26,
            TRUE);
    }

    InvalidateRect(state.preview, nullptr, TRUE);
}

void SelectPage(AppState& state, const int page)
{
    state.selectedPage = std::clamp(
        page,
        0,
        static_cast<int>(kPageOrder.size()) - 1);
    LayoutSettings(state);
}

void UpdateHint(AppState& state, const int identifier)
{
    const int index = identifier - kIdSettingBase;
    if (index < 0 || static_cast<std::size_t>(index) >= state.bindings.size())
    {
        return;
    }

    const ControlBinding& binding = state.bindings[static_cast<std::size_t>(index)];
    const kc::MeasurementUnitSystem units =
        ActiveMeasurementUnits(state.values);
    const std::wstring hint =
        ToWide(kc::DisplaySettingLabel(
            *binding.definition,
            units)) + L"\r\n" + binding.description;
    SetWindowTextW(state.hint, hint.c_str());
}

void SynchronizePackedMode(AppState& state, const std::string& changedKey)
{
    if (changedKey != "VR_CUSTOM_MODE" &&
        changedKey != "KISAK_VR_OUTPUT_SCALE")
    {
        return;
    }

    if (changedKey == "KISAK_VR_OUTPUT_SCALE")
    {
        const bool performance = StringValue(
            state.values,
            "KISAK_VR_OUTPUT_SCALE",
            "1.00") == "0.75";
        state.values["VR_CUSTOM_MODE"] = performance
            ? "4768x2016"
            : "6016x2688";
        state.values["KISAK_VR_FSR"] = performance ? "1" : "0";
        UpdateAllControls(state);
        return;
    }

    const std::string mode =
        StringValue(state.values, "VR_CUSTOM_MODE", "6016x2688");

    if (mode == "4768x2016")
    {
        state.values["KISAK_VR_OUTPUT_SCALE"] = "0.75";
        state.values["KISAK_VR_FSR"] = "1";
    }
    else
    {
        state.values["KISAK_VR_OUTPUT_SCALE"] = "1.00";
        state.values["KISAK_VR_FSR"] = "0";
    }

    UpdateAllControls(state);
}

void OnSettingChanged(AppState& state, const int identifier)
{
    if (state.suppressEvents)
    {
        return;
    }

    const int index = identifier - kIdSettingBase;
    if (index < 0 || static_cast<std::size_t>(index) >= state.bindings.size())
    {
        return;
    }

    ControlBinding& binding = state.bindings[static_cast<std::size_t>(index)];
    const kc::MeasurementUnitSystem units =
        ActiveMeasurementUnits(state.values);
    const std::string changedValue =
        ValueFromControl(binding, units);
    if (binding.definition->key == "KISAK_VR_DOMINANT_HAND")
    {
        kc::ApplyDominantHand(changedValue, &state.values);
        UpdateAllControls(state);
    }
    else
    {
        state.values[binding.definition->key] = changedValue;
    }
    MarkSettingsDirty(state, "Custom");
    if (binding.definition->key == "KISAK_VR_UNIT_SYSTEM")
    {
        UpdateAllControls(state);
    }
    SynchronizePackedMode(state, binding.definition->key);
    UpdateValidation(state);
    if (binding.definition->key == "KISAK_VR_BACKEND" ||
        binding.definition->key == "VR_CUSTOM_MODE" ||
        binding.definition->key == "KISAK_VR_OUTPUT_SCALE")
    {
        RefreshCompatibility(state, false);
    }
}

const vi::ActionDefinition* SelectedControllerAction(
    const AppState& state)
{
    if (state.bindingActionList == nullptr)
    {
        return nullptr;
    }

    const LRESULT selected =
        SendMessageW(
            state.bindingActionList,
            LB_GETCURSEL,
            0,
            0);

    if (selected < 0 ||
        static_cast<std::size_t>(selected) >= vi::kActionCount)
    {
        return nullptr;
    }

    return &vi::ActionDefinitions()[
        static_cast<std::size_t>(selected)];
}

std::wstring ControllerSourceLabel(const std::string& value)
{
    vi::Source source = vi::Source::Unbound;
    if (!vi::ParseSource(value, &source))
    {
        return ToWide(value);
    }

    return ToWide(
        vi::GetSourceDefinition(source).label);
}

std::wstring ControllerBindingLabel(
    const std::string& value,
    const vi::Action action)
{
    vi::Binding binding;
    if (!vi::ParseBinding(action, value, &binding))
    {
        return ToWide(value);
    }

    return ToWide(vi::BindingLabel(binding));
}

void SizeBindingDropDownToContents(HWND combo)
{
    if (combo == nullptr)
    {
        return;
    }

    HDC const deviceContext = GetDC(combo);
    if (deviceContext == nullptr)
    {
        SendMessageW(combo, CB_SETDROPPEDWIDTH, 380, 0);
        return;
    }

    const HFONT font = reinterpret_cast<HFONT>(
        SendMessageW(combo, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = font != nullptr
        ? SelectObject(deviceContext, font)
        : nullptr;

    int widest = 0;
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index)
    {
        const LRESULT length = SendMessageW(
            combo,
            CB_GETLBTEXTLEN,
            static_cast<WPARAM>(index),
            0);
        if (length <= 0)
        {
            continue;
        }

        std::wstring label(
            static_cast<std::size_t>(length) + 1u,
            L'\0');
        SendMessageW(
            combo,
            CB_GETLBTEXT,
            static_cast<WPARAM>(index),
            reinterpret_cast<LPARAM>(label.data()));

        SIZE textSize = {};
        if (GetTextExtentPoint32W(
                deviceContext,
                label.c_str(),
                static_cast<int>(length),
                &textSize))
        {
            widest = std::max(
                widest,
                static_cast<int>(textSize.cx));
        }
    }

    if (previousFont != nullptr)
    {
        SelectObject(deviceContext, previousFont);
    }
    ReleaseDC(combo, deviceContext);

    const int requestedWidth = std::clamp(
        widest + GetSystemMetrics(SM_CXVSCROLL) + 28,
        320,
        520);
    SendMessageW(
        combo,
        CB_SETDROPPEDWIDTH,
        static_cast<WPARAM>(requestedWidth),
        0);
}

void PopulateBindingCombo(
    AppState& state,
    HWND combo,
    const char* key)
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    const kc::SettingDefinition* const setting =
        kc::FindSetting(key);

    if (setting == nullptr)
    {
        return;
    }

    const std::string current =
        StringValue(
            state.values,
            key,
            setting->defaultValue.c_str());

    int selected = 0;

    const vi::ActionDefinition* const action =
        vi::FindActionDefinition(key);
    vi::Binding currentBinding;
    const bool chord =
        action != nullptr &&
        vi::ParseBinding(
            action->action,
            current,
            &currentBinding) &&
        currentBinding.sourceCount > 1u;

    if (chord)
    {
        const std::wstring label =
            L"Chord: " +
            ToWide(vi::BindingLabel(currentBinding));
        const LRESULT item = SendMessageW(
            combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(combo, CB_SETITEMDATA, item, -1);
    }

    for (std::size_t index = 0u;
         index < setting->choices.size();
         ++index)
    {
        const kc::SettingChoice& choice =
            setting->choices[index];

        const std::wstring label =
            ToWide(choice.label);

        const LRESULT item = SendMessageW(
            combo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(label.c_str()));

        vi::Source source = vi::Source::Unbound;
        vi::ParseSource(choice.value, &source);
        SendMessageW(
            combo,
            CB_SETITEMDATA,
            item,
            static_cast<LPARAM>(
                static_cast<std::size_t>(source)));

        if (choice.value == current)
        {
            selected = static_cast<int>(item);
        }
    }

    SendMessageW(
        combo,
        CB_SETCURSEL,
        selected,
        0);

    // Keep the compact editor layout, but widen its open list so the final
    // up/down/left/right word is always visible. Without this, four distinct
    // directional sources look like four identical truncated choices.
    SizeBindingDropDownToContents(combo);
}

void UpdateControllerBindingEditor(AppState& state)
{
    if (state.bindingActionList == nullptr ||
        state.bindingPrimary == nullptr ||
        state.bindingAlternate == nullptr)
    {
        return;
    }

    const LRESULT oldSelection =
        SendMessageW(
            state.bindingActionList,
            LB_GETCURSEL,
            0,
            0);

    const int selected =
        oldSelection >= 0 &&
        static_cast<std::size_t>(oldSelection) < vi::kActionCount
            ? static_cast<int>(oldSelection)
            : 0;

    const bool oldSuppression = state.suppressEvents;
    state.suppressEvents = true;

    SendMessageW(
        state.bindingActionList,
        LB_RESETCONTENT,
        0,
        0);

    for (const vi::ActionDefinition& action :
         vi::ActionDefinitions())
    {
        const std::string primary =
            StringValue(
                state.values,
                action.settingKey,
                action.defaultBinding);

        const std::string alternate =
            StringValue(
                state.values,
                action.alternateSettingKey,
                action.defaultAlternateBinding);

        std::wstring row =
            ToWide(action.label) + L"  —  " +
            ControllerBindingLabel(primary, action.action);

        if (alternate != "unbound")
        {
            row += L"  /  " +
                ControllerBindingLabel(alternate, action.action);
        }

        SendMessageW(
            state.bindingActionList,
            LB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(row.c_str()));
    }

    SendMessageW(
        state.bindingActionList,
        LB_SETCURSEL,
        selected,
        0);

    const vi::ActionDefinition& action =
        vi::ActionDefinitions()[
            static_cast<std::size_t>(selected)];

    PopulateBindingCombo(
        state,
        state.bindingPrimary,
        action.settingKey);

    PopulateBindingCombo(
        state,
        state.bindingAlternate,
        action.alternateSettingKey);

    SetWindowTextW(
        state.hint,
        (ToWide(action.label) + L"\r\n" +
         ToWide(action.description)).c_str());

    state.suppressEvents = oldSuppression;
}

void ShowControllerBindingEditor(
    AppState& state,
    const bool show)
{
    for (HWND control : state.bindingEditorControls)
    {
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    }
}

void OnControllerBindingChoice(
    AppState& state,
    const bool alternate)
{
    if (state.suppressEvents)
    {
        return;
    }

    const vi::ActionDefinition* const action =
        SelectedControllerAction(state);

    if (action == nullptr)
    {
        return;
    }

    const char* const key = alternate
        ? action->alternateSettingKey
        : action->settingKey;

    const kc::SettingDefinition* const setting =
        kc::FindSetting(key);

    HWND combo = alternate
        ? state.bindingAlternate
        : state.bindingPrimary;

    const LRESULT selected =
        SendMessageW(combo, CB_GETCURSEL, 0, 0);

    if (setting == nullptr || selected < 0)
    {
        return;
    }

    const LRESULT itemData =
        SendMessageW(combo, CB_GETITEMDATA, selected, 0);
    if (itemData == CB_ERR || itemData < 0 ||
        static_cast<std::size_t>(itemData) >= vi::kSourceCount)
    {
        return;
    }

    state.values[key] = std::string(
        vi::SourceId(static_cast<vi::Source>(itemData)));

    MarkSettingsDirty(state, "Custom");
    UpdateControllerBindingEditor(state);
    UpdateValidation(state);
}

void ClearControllerAlternateBinding(AppState& state)
{
    const vi::ActionDefinition* const action =
        SelectedControllerAction(state);

    if (action == nullptr)
    {
        return;
    }

    state.values[action->alternateSettingKey] = "unbound";
    MarkSettingsDirty(state, "Custom");
    UpdateControllerBindingEditor(state);
    UpdateValidation(state);
}

HWND CreateChordControl(
    ChordEditorState& state,
    const wchar_t* const className,
    const wchar_t* const text,
    const DWORD style,
    const int x,
    const int y,
    const int width,
    const int height,
    const int identifier,
    const DWORD extendedStyle = 0u)
{
    HWND control = CreateWindowExW(
        extendedStyle,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        state.window,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(identifier)),
        state.app->instance,
        nullptr);

    if (control != nullptr)
    {
        SetFont(control, state.app->font);
    }
    return control;
}

void AcceptChordEditor(ChordEditorState& state)
{
    if (state.sourceList == nullptr)
    {
        return;
    }

    const LRESULT selectedCount = SendMessageW(
        state.sourceList,
        LB_GETSELCOUNT,
        0,
        0);
    if (selectedCount >
        static_cast<LRESULT>(vi::kMaxBindingSources))
    {
        MessageBoxW(
            state.window,
            L"A binding may combine at most four inputs. Clear one or more selections and try again.",
            L"Too many chord inputs",
            MB_OK | MB_ICONWARNING);
        return;
    }

    vi::Binding binding;
    const LRESULT itemCount = SendMessageW(
        state.sourceList,
        LB_GETCOUNT,
        0,
        0);
    for (LRESULT item = 0;
         item < itemCount;
         ++item)
    {
        if (SendMessageW(
                state.sourceList,
                LB_GETSEL,
                item,
                0) <= 0)
        {
            continue;
        }

        const LRESULT itemData = SendMessageW(
            state.sourceList,
            LB_GETITEMDATA,
            item,
            0);
        if (itemData < 0 ||
            static_cast<std::size_t>(itemData) >= vi::kSourceCount)
        {
            continue;
        }

        binding.sources[binding.sourceCount++] =
            static_cast<vi::Source>(itemData);
    }

    state.result = vi::BindingId(binding);
    state.accepted = true;
    DestroyWindow(state.window);
}

LRESULT CALLBACK ChordEditorWindowProc(
    HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    ChordEditorState* state =
        reinterpret_cast<ChordEditorState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto* create =
            reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<ChordEditorState*>(
            create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }

    if (state == nullptr)
    {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    if (message == WM_COMMAND)
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
            AcceptChordEditor(*state);
            return 0;
        case IDCANCEL:
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
    }
    else if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

void EditControllerChord(
    AppState& state,
    const bool alternate)
{
    const vi::ActionDefinition* const action =
        SelectedControllerAction(state);
    if (action == nullptr)
    {
        return;
    }

    if (action->valueType != vi::ValueType::Boolean)
    {
        MessageBoxW(
            state.window,
            L"Analog actions use one axis per primary or alternate slot. Select the desired stick or trackpad from the dropdown.",
            L"Analog binding",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    const char* const key = alternate
        ? action->alternateSettingKey
        : action->settingKey;
    const char* const fallback = alternate
        ? action->defaultAlternateBinding
        : action->defaultBinding;
    const std::string current = StringValue(
        state.values,
        key,
        fallback);

    ChordEditorState editor;
    editor.app = &state;
    editor.action = action->action;

    editor.window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kChordEditorClass,
        (ToWide(action->label) + L" chord").c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        620,
        610,
        state.window,
        nullptr,
        state.instance,
        &editor);
    if (editor.window == nullptr)
    {
        MessageBoxW(
            state.window,
            L"Windows could not open the chord editor.",
            L"Chord editor failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    RECT parentRect = {};
    GetWindowRect(state.window, &parentRect);
    SetWindowPos(
        editor.window,
        HWND_TOP,
        parentRect.left +
            ((parentRect.right - parentRect.left) - 620) / 2,
        parentRect.top +
            ((parentRect.bottom - parentRect.top) - 610) / 2,
        0,
        0,
        SWP_NOSIZE);

    CreateChordControl(
        editor,
        L"STATIC",
        L"Click to select every input that must be held at the same time. Click a selected input again to remove it. Directional stick/trackpad choices are included.",
        SS_LEFT,
        18,
        16,
        568,
        54,
        0);

    editor.sourceList = CreateChordControl(
        editor,
        L"LISTBOX",
        L"",
        LBS_MULTIPLESEL | LBS_NOINTEGRALHEIGHT |
            WS_TABSTOP | WS_VSCROLL,
        18,
        76,
        568,
        430,
        1,
        WS_EX_CLIENTEDGE);

    vi::Binding currentBinding;
    vi::ParseBinding(
        action->action,
        current,
        &currentBinding);

    for (const vi::SourceDefinition& source :
         vi::SourceDefinitions())
    {
        if (source.source == vi::Source::Unbound ||
            !vi::IsSourceCompatible(action->action, source.source))
        {
            continue;
        }

        const std::wstring label = ToWide(source.label);
        const LRESULT item = SendMessageW(
            editor.sourceList,
            LB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(
            editor.sourceList,
            LB_SETITEMDATA,
            item,
            static_cast<LPARAM>(
                static_cast<std::size_t>(source.source)));

        if (std::find(
                currentBinding.sources.begin(),
                currentBinding.sources.begin() +
                    currentBinding.sourceCount,
                source.source) !=
            currentBinding.sources.begin() +
                currentBinding.sourceCount)
        {
            SendMessageW(editor.sourceList, LB_SETSEL, TRUE, item);
        }
    }

    CreateChordControl(
        editor,
        L"BUTTON",
        L"Save chord",
        BS_DEFPUSHBUTTON | WS_TABSTOP,
        372,
        520,
        102,
        30,
        IDOK);
    CreateChordControl(
        editor,
        L"BUTTON",
        L"Cancel",
        BS_PUSHBUTTON | WS_TABSTOP,
        484,
        520,
        102,
        30,
        IDCANCEL);

    EnableWindow(state.window, FALSE);
    ShowWindow(editor.window, SW_SHOW);
    UpdateWindow(editor.window);

    MSG message = {};
    bool sawQuit = false;
    WPARAM quitCode = 0;
    while (IsWindow(editor.window))
    {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
        {
            sawQuit = result == 0;
            quitCode = message.wParam;
            break;
        }

        if (!IsDialogMessageW(editor.window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(state.window, TRUE);
    SetForegroundWindow(state.window);

    if (sawQuit)
    {
        PostQuitMessage(static_cast<int>(quitCode));
        return;
    }

    if (!editor.accepted)
    {
        return;
    }

    state.values[key] = editor.result;
    MarkSettingsDirty(state, "Custom");
    UpdateControllerBindingEditor(state);
    UpdateValidation(state);
}

constexpr RECT kHudEditorCanvas = {32, 76, 832, 676};
constexpr RECT kHudEditorPreviousButton = {858, 310, 966, 350};
constexpr RECT kHudEditorNextButton = {974, 310, 1082, 350};
constexpr RECT kHudEditorSnapButton = {858, 360, 1082, 400};
constexpr RECT kHudEditorCenterButton = {858, 410, 1082, 450};
constexpr RECT kHudEditorResetButton = {858, 460, 1082, 500};
constexpr RECT kHudEditorDefaultsButton = {858, 510, 1082, 550};
constexpr RECT kHudEditorCancelButton = {858, 650, 964, 694};
constexpr RECT kHudEditorApplyButton = {976, 650, 1082, 694};

bool PointInRect(const RECT& rect, const POINT point)
{
    return point.x >= rect.left && point.x <= rect.right &&
        point.y >= rect.top && point.y <= rect.bottom;
}

POINT HudVirtualToClient(const vh::Point point)
{
    const float width = static_cast<float>(
        kHudEditorCanvas.right - kHudEditorCanvas.left);
    const float height = static_cast<float>(
        kHudEditorCanvas.bottom - kHudEditorCanvas.top);
    return {
        kHudEditorCanvas.left + static_cast<LONG>(
            std::lround(point.x / vh::kCanvasWidth * width)),
        kHudEditorCanvas.top + static_cast<LONG>(
            std::lround(point.y / vh::kCanvasHeight * height)),
    };
}

vh::Point HudClientToVirtual(const POINT point)
{
    const float width = static_cast<float>(
        kHudEditorCanvas.right - kHudEditorCanvas.left);
    const float height = static_cast<float>(
        kHudEditorCanvas.bottom - kHudEditorCanvas.top);
    return {
        (point.x - kHudEditorCanvas.left) / width * vh::kCanvasWidth,
        (point.y - kHudEditorCanvas.top) / height * vh::kCanvasHeight,
    };
}

RECT HudElementClientRect(
    const vh::Layout& layout,
    const vh::Element element)
{
    const POINT center = HudVirtualToClient(
        vh::ElementCenter(layout, element));
    const vh::Size size = vh::ElementSize(layout, element);
    const float scaleX =
        static_cast<float>(kHudEditorCanvas.right - kHudEditorCanvas.left) /
        vh::kCanvasWidth;
    const float scaleY =
        static_cast<float>(kHudEditorCanvas.bottom - kHudEditorCanvas.top) /
        vh::kCanvasHeight;
    const LONG halfWidth = static_cast<LONG>(
        std::lround(size.width * scaleX * 0.5f));
    const LONG halfHeight = static_cast<LONG>(
        std::lround(size.height * scaleY * 0.5f));
    return {
        center.x - halfWidth,
        center.y - halfHeight,
        center.x + halfWidth,
        center.y + halfHeight,
    };
}

RECT HudResizeHandleRect(
    const vh::Layout& layout,
    const vh::Element element)
{
    const RECT elementRect = HudElementClientRect(layout, element);
    return {
        elementRect.right - 9,
        elementRect.bottom - 9,
        elementRect.right + 9,
        elementRect.bottom + 9,
    };
}

void DrawHudEditorButton(
    HDC dc,
    const RECT& rect,
    const std::wstring& text,
    const COLORREF fill,
    const COLORREF foreground)
{
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(85, 99, 121));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    SetTextColor(dc, foreground);
    SetBkMode(dc, TRANSPARENT);
    RECT label = rect;
    DrawTextW(
        dc,
        text.c_str(),
        -1,
        &label,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawHudEditorElement(
    HDC dc,
    const HudEditorState& editor,
    const vh::Element element)
{
    const std::size_t index = static_cast<std::size_t>(element);
    constexpr std::array<COLORREF, vh::kElementCount> fills = {{
        RGB(49, 139, 89),
        RGB(191, 126, 44),
        RGB(49, 105, 181),
        RGB(130, 70, 173),
        RGB(94, 105, 123),
    }};

    RECT rect = HudElementClientRect(editor.layout, element);
    HBRUSH brush = CreateSolidBrush(fills[index]);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);

    HPEN pen = CreatePen(
        PS_SOLID,
        element == editor.selected ? 4 : 2,
        element == editor.selected
            ? RGB(255, 213, 58)
            : RGB(226, 234, 245));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    RECT label = rect;
    label.left += 9;
    label.right -= 9;
    const std::wstring elementLabel =
        ToWide(vh::ElementLabel(element));
    DrawTextW(
        dc,
        elementLabel.c_str(),
        -1,
        &label,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (element == vh::Element::AmmoEquipment)
    {
        RECT icon = {rect.left + 12, rect.top + 12, rect.left + 42, rect.bottom - 12};
        HBRUSH iconBrush = CreateSolidBrush(RGB(223, 236, 226));
        FillRect(dc, &icon, iconBrush);
        DeleteObject(iconBrush);
    }
    else if (element == vh::Element::Compass)
    {
        HPEN compassPen = CreatePen(PS_SOLID, 2, RGB(255, 238, 176));
        HGDIOBJ previousPen = SelectObject(dc, compassPen);
        HGDIOBJ previousBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Ellipse(dc, rect.left + 10, rect.top + 10, rect.right - 10, rect.bottom - 10);
        MoveToEx(dc, (rect.left + rect.right) / 2, rect.top + 13, nullptr);
        LineTo(dc, (rect.left + rect.right) / 2, rect.bottom - 13);
        SelectObject(dc, previousBrush);
        SelectObject(dc, previousPen);
        DeleteObject(compassPen);
    }

    if (element == editor.selected)
    {
        const RECT handle = HudResizeHandleRect(editor.layout, element);
        HBRUSH handleBrush = CreateSolidBrush(RGB(255, 213, 58));
        FillRect(dc, &handle, handleBrush);
        DeleteObject(handleBrush);
    }
}

void DrawHudEditorSurface(
    HDC dc,
    const RECT& client,
    const HudEditorState& editor)
{
    HBRUSH background = CreateSolidBrush(RGB(238, 242, 248));
    FillRect(dc, &client, background);
    DeleteObject(background);

    SelectObject(dc, editor.app->titleFont);
    SetTextColor(dc, RGB(27, 37, 53));
    SetBkMode(dc, TRANSPARENT);
    RECT title = {32, 18, 1084, 52};
    DrawTextW(
        dc,
        L"Visual HUD editor — drag the real runtime groups",
        -1,
        &title,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    HBRUSH sky = CreateSolidBrush(RGB(42, 65, 91));
    FillRect(dc, &kHudEditorCanvas, sky);
    DeleteObject(sky);
    RECT ground = kHudEditorCanvas;
    ground.top = kHudEditorCanvas.top + 330;
    HBRUSH groundBrush = CreateSolidBrush(RGB(42, 48, 44));
    FillRect(dc, &ground, groundBrush);
    DeleteObject(groundBrush);

    HPEN gridPen = CreatePen(PS_DOT, 1, RGB(112, 151, 183));
    HGDIOBJ oldPen = SelectObject(dc, gridPen);
    for (const float x : {64.0f, 320.0f, 576.0f})
    {
        const POINT point = HudVirtualToClient({x, 0.0f});
        MoveToEx(dc, point.x, kHudEditorCanvas.top, nullptr);
        LineTo(dc, point.x, kHudEditorCanvas.bottom);
    }
    for (const float y : {48.0f, 240.0f, 432.0f})
    {
        const POINT point = HudVirtualToClient({0.0f, y});
        MoveToEx(dc, kHudEditorCanvas.left, point.y, nullptr);
        LineTo(dc, kHudEditorCanvas.right, point.y);
    }
    SelectObject(dc, oldPen);
    DeleteObject(gridPen);

    for (const float x : {64.0f, 320.0f, 576.0f})
    {
        for (const float y : {48.0f, 240.0f, 432.0f})
        {
            const POINT anchor = HudVirtualToClient({x, y});
            HBRUSH anchorBrush = CreateSolidBrush(RGB(117, 184, 230));
            HGDIOBJ previousBrush = SelectObject(dc, anchorBrush);
            Ellipse(dc, anchor.x - 5, anchor.y - 5, anchor.x + 6, anchor.y + 6);
            SelectObject(dc, previousBrush);
            DeleteObject(anchorBrush);
        }
    }

    const POINT safeMinimum = HudVirtualToClient(
        vh::SafeAreaMinimum(editor.layout));
    const POINT safeMaximum = HudVirtualToClient(
        vh::SafeAreaMaximum(editor.layout));
    HPEN safePen = CreatePen(PS_SOLID, 2, RGB(53, 211, 255));
    oldPen = SelectObject(dc, safePen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(
        dc,
        safeMinimum.x,
        safeMinimum.y,
        safeMaximum.x,
        safeMaximum.y);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(safePen);

    // A muted weapon silhouette keeps the desktop surface spatially legible
    // without pretending to be a captured gameplay frame.
    HPEN weaponPen = CreatePen(PS_SOLID, 14, RGB(27, 31, 33));
    oldPen = SelectObject(dc, weaponPen);
    MoveToEx(dc, 550, 655, nullptr);
    LineTo(dc, 710, 525);
    SelectObject(dc, oldPen);
    DeleteObject(weaponPen);

    for (std::size_t index = 0u; index < vh::kElementCount; ++index)
    {
        DrawHudEditorElement(
            dc,
            editor,
            static_cast<vh::Element>(index));
    }

    const POINT opticalCenter = HudVirtualToClient({320.0f, 240.0f});
    HPEN crosshairPen = CreatePen(PS_SOLID, 2, RGB(255, 86, 86));
    oldPen = SelectObject(dc, crosshairPen);
    MoveToEx(dc, opticalCenter.x - 12, opticalCenter.y, nullptr);
    LineTo(dc, opticalCenter.x + 13, opticalCenter.y);
    MoveToEx(dc, opticalCenter.x, opticalCenter.y - 12, nullptr);
    LineTo(dc, opticalCenter.x, opticalCenter.y + 13);
    SelectObject(dc, oldPen);
    DeleteObject(crosshairPen);

    SelectObject(dc, editor.app->font);
    SetTextColor(dc, RGB(45, 57, 75));
    RECT sideTitle = {858, 76, 1082, 116};
    const std::wstring selected =
        L"Selected: " + ToWide(vh::ElementLabel(editor.selected));
    DrawTextW(dc, selected.c_str(), -1, &sideTitle, DT_LEFT | DT_WORDBREAK);

    const vh::Point center =
        vh::ElementCenter(editor.layout, editor.selected);
    std::wostringstream details;
    details.setf(std::ios::fixed, std::ios::floatfield);
    details.precision(1);
    details << L"Snap point: " << center.x << L", " << center.y
            << L"\r\nScale: ";
    details.precision(2);
    details << vh::ElementScale(editor.layout, editor.selected)
            << L"x\r\n\r\n"
            << L"Drag the box to move it. Drag the yellow corner or use the mouse wheel to resize. Blue dots are snap anchors. The crosshair stays at optical center.";
    RECT detailRect = {858, 126, 1082, 300};
    DrawTextW(
        dc,
        details.str().c_str(),
        -1,
        &detailRect,
        DT_LEFT | DT_WORDBREAK);

    DrawHudEditorButton(
        dc,
        kHudEditorPreviousButton,
        L"Previous",
        RGB(231, 235, 242),
        RGB(42, 52, 68));
    DrawHudEditorButton(
        dc,
        kHudEditorNextButton,
        L"Next",
        RGB(231, 235, 242),
        RGB(42, 52, 68));

    DrawHudEditorButton(
        dc,
        kHudEditorSnapButton,
        editor.snapEnabled
            ? L"Snap anchors: ON"
            : L"Snap anchors: OFF",
        editor.snapEnabled
            ? RGB(211, 237, 249)
            : RGB(225, 228, 233),
        RGB(33, 54, 72));
    DrawHudEditorButton(
        dc,
        kHudEditorCenterButton,
        L"Center selected element",
        RGB(231, 235, 242),
        RGB(42, 52, 68));
    DrawHudEditorButton(
        dc,
        kHudEditorResetButton,
        L"Reset selected element",
        RGB(231, 235, 242),
        RGB(42, 52, 68));
    DrawHudEditorButton(
        dc,
        kHudEditorDefaultsButton,
        L"Restore tested HUD defaults",
        RGB(231, 235, 242),
        RGB(42, 52, 68));

    RECT liveNote = {858, 558, 1082, 630};
    DrawTextW(
        dc,
        L"Apply updates the configurator values. Use “Edit live in headset” afterward to verify placement against the real mission HUD.",
        -1,
        &liveNote,
        DT_LEFT | DT_WORDBREAK);

    DrawHudEditorButton(
        dc,
        kHudEditorCancelButton,
        L"Cancel",
        RGB(235, 220, 222),
        RGB(90, 34, 39));
    DrawHudEditorButton(
        dc,
        kHudEditorApplyButton,
        L"Apply layout",
        RGB(200, 231, 211),
        RGB(25, 80, 46));
}

void ResetSelectedHudElement(HudEditorState& editor)
{
    vh::ResetElement(&editor.layout, editor.selected);
}

void AcceptHudEditor(HudEditorState& editor)
{
    editor.accepted = true;
    DestroyWindow(editor.window);
}

LRESULT CALLBACK HudEditorWindowProc(
    HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    HudEditorState* editor = reinterpret_cast<HudEditorState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        editor = static_cast<HudEditorState*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(editor));
    }
    if (editor == nullptr)
    {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT paint = {};
        HDC destination = BeginPaint(window, &paint);
        RECT client = {};
        GetClientRect(window, &client);
        HDC memory = CreateCompatibleDC(destination);
        HBITMAP bitmap = CreateCompatibleBitmap(
            destination,
            client.right,
            client.bottom);
        HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
        DrawHudEditorSurface(memory, client, *editor);
        BitBlt(
            destination,
            0,
            0,
            client.right,
            client.bottom,
            memory,
            0,
            0,
            SRCCOPY);
        SelectObject(memory, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        const POINT point = {
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam),
        };
        if (PointInRect(kHudEditorApplyButton, point))
        {
            AcceptHudEditor(*editor);
            return 0;
        }
        if (PointInRect(kHudEditorCancelButton, point))
        {
            DestroyWindow(window);
            return 0;
        }
        if (PointInRect(kHudEditorSnapButton, point))
        {
            editor->snapEnabled = !editor->snapEnabled;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (PointInRect(kHudEditorPreviousButton, point))
        {
            editor->selected = vh::CycleElement(editor->selected, -1);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (PointInRect(kHudEditorNextButton, point))
        {
            editor->selected = vh::CycleElement(editor->selected, 1);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (PointInRect(kHudEditorCenterButton, point))
        {
            vh::CenterElement(&editor->layout, editor->selected);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (PointInRect(kHudEditorResetButton, point))
        {
            ResetSelectedHudElement(*editor);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (PointInRect(kHudEditorDefaultsButton, point))
        {
            editor->layout = vh::DefaultLayout();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (!PointInRect(kHudEditorCanvas, point))
        {
            return 0;
        }

        if (PointInRect(
                HudResizeHandleRect(editor->layout, editor->selected),
                point))
        {
            editor->resizing = true;
            editor->dragStart = point;
            editor->resizeStartScale =
                vh::ElementScale(editor->layout, editor->selected);
            SetCapture(window);
            return 0;
        }

        vh::Element hit = editor->selected;
        const vh::Point virtualPoint = HudClientToVirtual(point);
        if (vh::HitTestElement(editor->layout, virtualPoint, &hit))
        {
            editor->selected = hit;
            editor->dragging = true;
            const POINT center = HudVirtualToClient(
                vh::ElementCenter(editor->layout, hit));
            editor->dragOffset = {
                point.x - center.x,
                point.y - center.y,
            };
            SetCapture(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (editor->dragging || editor->resizing)
        {
            const POINT point = {
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam),
            };
            if (editor->dragging)
            {
                const POINT adjusted = {
                    point.x - editor->dragOffset.x,
                    point.y - editor->dragOffset.y,
                };
                const bool shiftHeld =
                    (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                vh::MoveElement(
                    &editor->layout,
                    editor->selected,
                    HudClientToVirtual(adjusted),
                    editor->snapEnabled && !shiftHeld);
            }
            else
            {
                const float delta =
                    static_cast<float>(
                        point.x - editor->dragStart.x +
                        point.y - editor->dragStart.y);
                vh::SetElementScale(
                    &editor->layout,
                    editor->selected,
                    editor->resizeStartScale + delta / 240.0f);
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (editor->dragging || editor->resizing)
        {
            editor->dragging = false;
            editor->resizing = false;
            ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
    {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        const float scale = vh::ElementScale(
            editor->layout,
            editor->selected);
        vh::SetElementScale(
            &editor->layout,
            editor->selected,
            scale + (delta > 0 ? 0.05f : -0.05f));
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_RETURN)
        {
            AcceptHudEditor(*editor);
            return 0;
        }
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(window);
            return 0;
        }
        if (wParam == 'S')
        {
            editor->snapEnabled = !editor->snapEnabled;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_TAB &&
            (lParam & (static_cast<LPARAM>(1) << 30)) == 0)
        {
            const int direction =
                (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1;
            editor->selected =
                vh::CycleElement(editor->selected, direction);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_HOME)
        {
            vh::CenterElement(&editor->layout, editor->selected);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_END)
        {
            ResetSelectedHudElement(*editor);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_LEFT || wParam == VK_RIGHT ||
            wParam == VK_UP || wParam == VK_DOWN)
        {
            vh::Point center = vh::ElementCenter(
                editor->layout,
                editor->selected);
            const float step =
                (GetKeyState(VK_SHIFT) & 0x8000) != 0
                    ? 10.0f
                    : 1.0f;
            if (wParam == VK_LEFT) center.x -= step;
            if (wParam == VK_RIGHT) center.x += step;
            if (wParam == VK_UP) center.y -= step;
            if (wParam == VK_DOWN) center.y += step;
            vh::MoveElement(
                &editor->layout,
                editor->selected,
                center,
                false);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void OpenVisualHudEditor(AppState& state)
{
    ReadAllControls(state);
    HudEditorState editor;
    editor.app = &state;
    editor.layout = kc::HudLayoutFromSettings(state.values);
    editor.window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kHudEditorClass,
        L"KisakCOD VR - Visual HUD Editor",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1120,
        760,
        state.window,
        nullptr,
        state.instance,
        &editor);
    if (editor.window == nullptr)
    {
        MessageBoxW(
            state.window,
            L"Windows could not open the visual HUD editor.",
            L"HUD editor failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    RECT parent = {};
    GetWindowRect(state.window, &parent);
    SetWindowPos(
        editor.window,
        HWND_TOP,
        parent.left + ((parent.right - parent.left) - 1120) / 2,
        parent.top + ((parent.bottom - parent.top) - 760) / 2,
        0,
        0,
        SWP_NOSIZE);
    EnableWindow(state.window, FALSE);
    ShowWindow(editor.window, SW_SHOW);
    UpdateWindow(editor.window);

    MSG message = {};
    bool sawQuit = false;
    WPARAM quitCode = 0;
    while (IsWindow(editor.window))
    {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
        {
            sawQuit = result == 0;
            quitCode = message.wParam;
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    EnableWindow(state.window, TRUE);
    SetForegroundWindow(state.window);
    if (sawQuit)
    {
        PostQuitMessage(static_cast<int>(quitCode));
        return;
    }
    if (!editor.accepted)
    {
        return;
    }

    kc::ApplyHudLayoutToSettings(editor.layout, &state.values);
    MarkSettingsDirty(state, "Custom");
    UpdateAllControls(state);
    UpdateValidation(state);
}

bool WaitForMapperProcess(
    const HANDLE process,
    const DWORD timeoutMilliseconds)
{
    const ULONGLONG started = GetTickCount64();

    while (true)
    {
        const ULONGLONG elapsed =
            GetTickCount64() - started;

        if (elapsed >= timeoutMilliseconds)
        {
            return false;
        }

        const DWORD remaining =
            static_cast<DWORD>(
                timeoutMilliseconds - elapsed);

        const DWORD waitResult =
            MsgWaitForMultipleObjects(
                1u,
                &process,
                FALSE,
                remaining,
                QS_ALLINPUT);

        if (waitResult == WAIT_OBJECT_0)
        {
            return true;
        }

        if (waitResult == WAIT_OBJECT_0 + 1u)
        {
            MSG message = {};
            while (PeekMessageW(
                       &message,
                       nullptr,
                       0,
                       0,
                       PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    PostQuitMessage(
                        static_cast<int>(message.wParam));
                    return false;
                }

                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            continue;
        }

        return false;
    }
}

std::map<std::string, std::string> ReadMapperResult(
    const std::filesystem::path& path)
{
    std::map<std::string, std::string> values;
    std::ifstream input(path, std::ios::binary);
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

        values[line.substr(0u, separator)] =
            line.substr(separator + 1u);
    }

    return values;
}

void CaptureControllerBinding(
    AppState& state,
    const bool alternate)
{
    const vi::ActionDefinition* const action =
        SelectedControllerAction(state);

    if (action == nullptr)
    {
        return;
    }

    const std::filesystem::path mapperPath =
        ExecutableDirectory() /
        L"KisakCOD-VR-Input-Mapper.exe";

    if (!std::filesystem::is_regular_file(mapperPath))
    {
        MessageBoxW(
            state.window,
            L"KisakCOD-VR-Input-Mapper.exe is missing. Re-extract the complete package, or select an input manually from the dropdown.",
            L"Input mapper missing",
            MB_OK | MB_ICONERROR);
        return;
    }

    const bool captureAxis =
        action->valueType == vi::ValueType::Vector2;

    const std::wstring instructions =
        L"The headset will briefly switch to a black controller capture session using the configured VR backend.\r\n\r\n"
        L"Release the controls first, put on the headset, then " +
        std::wstring(
            captureAxis
                ? L"move the desired stick or trackpad fully in any direction."
                : L"press the desired button/trigger/grip, or move the desired stick/trackpad in the direction you want to bind.") +
        L"\r\n\r\nPress Escape to cancel. Capture times out after 45 seconds.";

    if (MessageBoxW(
            state.window,
            instructions.c_str(),
            ToWide(action->label).c_str(),
            MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
    {
        return;
    }

    std::array<wchar_t, 32768> temporaryDirectory = {};
    const DWORD temporaryLength =
        GetTempPathW(
            static_cast<DWORD>(temporaryDirectory.size()),
            temporaryDirectory.data());

    if (temporaryLength == 0u ||
        temporaryLength >= temporaryDirectory.size())
    {
        MessageBoxW(
            state.window,
            L"Windows did not provide a temporary directory for binding capture.",
            L"Binding capture failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    const std::filesystem::path outputPath =
        std::filesystem::path(temporaryDirectory.data()) /
        (L"KisakCOD-VR-binding-" +
         std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L".txt");

    const auto backendValue =
        state.values.find("KISAK_VR_BACKEND");
    const std::wstring backend =
        backendValue != state.values.end()
            ? ToWide(backendValue->second)
            : L"auto";

    std::wstring command =
        L"\"" + mapperPath.wstring() + L"\" --capture " +
        (captureAxis ? L"vector2" : L"boolean") +
        L" --backend " + backend +
        L" --output \"" + outputPath.wstring() +
        L"\" --timeout-ms 45000";

    std::vector<wchar_t> mutableCommand(
        command.begin(),
        command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    const std::wstring workingDirectory =
        ExecutableDirectory().wstring();

    const BOOL created =
        CreateProcessW(
            mapperPath.c_str(),
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            0u,
            nullptr,
            workingDirectory.c_str(),
            &startup,
            &process);

    if (!created)
    {
        MessageBoxW(
            state.window,
            L"Windows could not start the VR input mapper.",
            L"Binding capture failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    EnableWindow(state.window, FALSE);
    const bool completed =
        WaitForMapperProcess(process.hProcess, 55000u);
    EnableWindow(state.window, TRUE);
    SetForegroundWindow(state.window);

    if (!completed)
    {
        TerminateProcess(process.hProcess, 3u);
        WaitForSingleObject(process.hProcess, 5000u);
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    const std::map<std::string, std::string> captured =
        ReadMapperResult(outputPath);

    std::error_code removeError;
    std::filesystem::remove(outputPath, removeError);

    const auto status = captured.find("status");
    const auto sourceValue = captured.find("source");

    if (!completed ||
        status == captured.end() ||
        status->second != "success" ||
        sourceValue == captured.end())
    {
        const auto message = captured.find("message");
        const std::wstring detail =
            message == captured.end()
                ? L"The VR input mapper did not return a binding. Make sure the configured runtime and controllers are active, then try again."
                : ToWide(message->second);

        MessageBoxW(
            state.window,
            detail.c_str(),
            L"Binding capture failed",
            MB_OK | MB_ICONWARNING);
        return;
    }

    vi::Source source = vi::Source::Unbound;
    if (!vi::ParseSource(sourceValue->second, &source) ||
        !vi::IsSourceCompatible(action->action, source))
    {
        MessageBoxW(
            state.window,
            L"The mapper returned an input that is incompatible with this action.",
            L"Binding capture failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    const char* const settingKey = alternate
        ? action->alternateSettingKey
        : action->settingKey;

    state.values[settingKey] =
        std::string(vi::SourceId(source));
    MarkSettingsDirty(state, "Custom");
    UpdateControllerBindingEditor(state);
    UpdateValidation(state);

    std::wstring success =
        L"Bound " + ToWide(action->label) + L" to " +
        ControllerSourceLabel(sourceValue->second) + L".";

    const auto localized = captured.find("localized");
    if (localized != captured.end() && !localized->second.empty())
    {
        success += L"\r\n\r\nRuntime name: " +
            ToWide(localized->second);
    }

    const auto profile = captured.find("profile");
    if (profile != captured.end() && !profile->second.empty())
    {
        success += L"\r\nProfile: " +
            ToWide(profile->second);
    }

    MessageBoxW(
        state.window,
        success.c_str(),
        L"Binding captured",
        MB_OK | MB_ICONINFORMATION);
}

bool HasValidationErrors(const AppState& state)
{
    return std::any_of(
        state.validation.begin(),
        state.validation.end(),
        [](const kc::ValidationMessage& message)
        {
            return message.severity == kc::ValidationMessage::Severity::Error;
        });
}

void ShowValidationError(AppState& state)
{
    const auto found = std::find_if(
        state.validation.begin(),
        state.validation.end(),
        [](const kc::ValidationMessage& message)
        {
            return message.severity == kc::ValidationMessage::Severity::Error;
        });

    if (found == state.validation.end())
    {
        return;
    }

    const kc::SettingDefinition* definition = kc::FindSetting(found->key);
    if (definition != nullptr)
    {
        const int page = PageIndex(definition->page);
        TabCtrl_SetCurSel(state.tabs, page);
        SelectPage(state, page);
    }

    const std::wstring message =
        L"Fix this setting before saving:\r\n\r\n" +
        ToWide(found->message);
    MessageBoxW(
        state.window,
        message.c_str(),
        L"Invalid VR setting",
        MB_OK | MB_ICONWARNING);
}

bool ConfirmReplaceInvalidWeaponProfiles(AppState& state)
{
    if (state.weaponProfilesLoadError.empty())
    {
        return true;
    }

    const std::wstring message =
        L"The existing per-weapon profile file was rejected and has not been changed.\r\n\r\n" +
        ToWide(state.weaponProfilesLoadError) +
        L"\r\n\r\nSaving now will replace it with the valid profiles currently shown. Continue?\r\n\r\nFile:\r\n" +
        state.weaponProfilesPath.wstring();
    return MessageBoxW(
        state.window,
        message.c_str(),
        L"Replace invalid weapon profile file?",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

bool SaveActiveSettings(AppState& state, const bool quiet)
{
    UpdateValidation(state);
    if (HasValidationErrors(state))
    {
        ShowValidationError(state);
        return false;
    }

    if (!ConfirmReplaceInvalidWeaponProfiles(state))
    {
        return false;
    }

    std::string weaponProfileError;
    if (!SaveWeaponProfiles(
            state.weaponProfilesPath,
            state.weaponProfiles,
            &weaponProfileError))
    {
        MessageBoxW(
            state.window,
            (L"The configurator could not save the per-weapon and gunstock profiles.\r\n\r\n" +
             ToWide(weaponProfileError)).c_str(),
            L"Weapon calibration save failed",
            MB_OK | MB_ICONERROR);
        return false;
    }
    state.weaponProfilesLoadError.clear();

    const kc::SaveResult result = kc::SaveUserSettingsAtomic(
        state.userPath,
        state.values,
        state.profileName);

    if (!result.success)
    {
        const std::wstring message =
            L"The configurator could not save your settings.\r\n\r\n" +
            ToWide(result.error);
        MessageBoxW(
            state.window,
            message.c_str(),
            L"Save failed",
            MB_OK | MB_ICONERROR);
        return false;
    }

    state.dirty = false;
    state.readBackVerified = result.readBackVerified;
    state.verifiedSettingCount = result.verifiedSettingCount;
    state.revision = result.revision;
    state.lastSavedAt = result.savedAt;
    state.activePath = result.settingsPath;
    state.profileName = result.profileName;
    UpdateValidation(state);

    if (!quiet)
    {
        std::wstring message =
            L"Settings saved and read-back verified.\r\n\r\n" +
            std::to_wstring(result.verifiedSettingCount) + L"/" +
            std::to_wstring(kc::SettingsCatalog().size()) +
            L" settings match the file on disk.\r\n\r\nProfile: " +
            ToWide(result.profileName) + L"\r\nRevision: " +
            ToWide(result.revision) + L"\r\nSaved: " +
            ToWide(result.savedAt) + L"\r\n\r\nActive file:\r\n" +
            result.settingsPath.wstring();
        if (!result.backupPath.empty())
        {
            message += L"\r\n\r\nPrevious settings backed up to:\r\n";
            message += result.backupPath.wstring();
        }
        MessageBoxW(
            state.window,
            message.c_str(),
            L"KisakCOD VR",
            MB_OK | MB_ICONINFORMATION);
    }

    return true;
}

bool LaunchBatch(AppState& state, const wchar_t* fileName)
{
    const std::filesystem::path launcher =
        state.gameDirectory / fileName;
    if (!std::filesystem::is_regular_file(launcher))
    {
        const std::wstring message =
            L"Launcher not found:\r\n" + launcher.wstring();
        MessageBoxW(
            state.window,
            message.c_str(),
            L"Cannot launch KisakCOD VR",
            MB_OK | MB_ICONERROR);
        return false;
    }

    const HINSTANCE launched = ShellExecuteW(
        state.window,
        L"open",
        launcher.c_str(),
        nullptr,
        state.gameDirectory.c_str(),
        SW_SHOWNORMAL);

    if (reinterpret_cast<INT_PTR>(launched) <= 32)
    {
        MessageBoxW(
            state.window,
            L"Windows could not start the KisakCOD VR launcher.",
            L"Launch failed",
            MB_OK | MB_ICONERROR);
        return false;
    }

    return true;
}

std::filesystem::path CalibrationRequestPath(
    const AppState& state)
{
    return state.userPath.parent_path() /
        L"Calibration-Request.txt";
}

std::filesystem::path CalibrationStatusPath(
    const AppState& state)
{
    return state.userPath.parent_path() /
        L"Calibration-Status.txt";
}

std::filesystem::path HudEditorRequestPath(
    const AppState& state)
{
    return state.userPath.parent_path() /
        L"HUD-Editor-Request.txt";
}

std::filesystem::path HudEditorStatusPath(
    const AppState& state)
{
    return state.userPath.parent_path() /
        L"HUD-Editor-Status.txt";
}

std::string NewHudEditorRequestId();
bool WriteHudEditorRequestAtomic(
    const std::filesystem::path& path,
    const vh::Request& request);
std::string ReadTextFileLimited(
    const std::filesystem::path& path,
    std::size_t maximumBytes);

bool IsProcessRunning(const wchar_t* const imageName)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPPROCESS,
        0u);

    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    bool found = false;

    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, imageName) == 0)
            {
                found = true;
                break;
            }
        }
        while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

std::string NewWeaponCalibrationRequestId()
{
    std::ostringstream output;
    output << "weapon-" << GetCurrentProcessId() << '-'
           << GetTickCount64();
    return output.str();
}

bool ReadWeaponRuntimeStatus(
    AppState& state,
    vwp::RuntimeStatus* const status)
{
    std::string parseError;
    const std::string text = ReadTextFileLimited(
        state.weaponCalibrationStatusPath,
        16384u);
    return !text.empty() &&
        vwp::ParseRuntimeStatus(text, status, &parseError);
}

void UpdateWeaponStatusLabel(AppState& state)
{
    if (state.weaponStatus == nullptr)
    {
        return;
    }

    if (!state.weaponProfilesLoadError.empty())
    {
        SetWindowTextW(
            state.weaponStatus,
            L"The saved weapon profile file is invalid; global calibration remains active. Open the editor to review or replace it.");
        return;
    }

    vwp::RuntimeStatus status;
    std::wstring label;
    if (ReadWeaponRuntimeStatus(state, &status) &&
        !status.weaponId.empty())
    {
        state.weaponRuntimeStatus = status;
        if (status.status == "invalid_profiles")
        {
            label = L"Runtime rejected the weapon profile file: " +
                ToWide(status.message);
        }
        else
        {
            label = L"Current: " + ToWide(status.weaponId) + L" (#" +
                std::to_wstring(status.weaponIndex) + L")  |  " +
                (status.effective.shoulderedBlend >= 0.5f
                    ? L"shouldered/ADS"
                    : L"hip-fire") +
                L"  |  gunstock: " + ToWide(status.activeGunstockId);
        }
    }
    else if (IsProcessRunning(L"KisakCOD-sp.exe"))
    {
        label = L"COD4 is running; enter an active mission to publish the current weapon.";
    }
    else
    {
        label = L"Launch a mission, then Refresh to select the equipped weapon automatically.";
    }
    SetWindowTextW(state.weaponStatus, label.c_str());
}

bool SendWeaponCalibrationRequest(
    AppState& state,
    vwp::Request request,
    vwp::RuntimeStatus* const response)
{
    if (!IsProcessRunning(L"KisakCOD-sp.exe"))
    {
        MessageBoxW(
            state.window,
            L"KisakCOD-sp.exe is not running. Launch the game, enter a mission, and retry the live calibration action.",
            L"Live weapon calibration",
            MB_OK | MB_ICONINFORMATION);
        return false;
    }

    request.requestId = NewWeaponCalibrationRequestId();
    std::error_code removeError;
    std::filesystem::remove(state.weaponCalibrationStatusPath, removeError);
    std::string writeError;
    if (!WriteTextFileAtomic(
            state.weaponCalibrationRequestPath,
            vwp::SerializeRequest(request),
            &writeError))
    {
        MessageBoxW(
            state.window,
            ToWide(writeError).c_str(),
            L"Live weapon calibration request failed",
            MB_OK | MB_ICONERROR);
        return false;
    }

    const ULONGLONG deadline = GetTickCount64() + 6000u;
    const BOOL mainWindowWasEnabled = IsWindowEnabled(state.window);
    if (mainWindowWasEnabled)
    {
        EnableWindow(state.window, FALSE);
    }
    bool matched = false;
    vwp::RuntimeStatus received;
    while (GetTickCount64() < deadline)
    {
        if (ReadWeaponRuntimeStatus(state, &received) &&
            received.requestId == request.requestId)
        {
            matched = true;
            break;
        }
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(50u);
    }
    if (mainWindowWasEnabled)
    {
        EnableWindow(state.window, TRUE);
        SetForegroundWindow(state.window);
    }

    if (!matched)
    {
        MessageBoxW(
            state.window,
            L"The running game did not acknowledge the request within six seconds. Make sure an actively rendering mission is open.",
            L"Live weapon calibration timed out",
            MB_OK | MB_ICONWARNING);
        return false;
    }
    state.weaponRuntimeStatus = received;
    if (response != nullptr)
    {
        *response = received;
    }
    UpdateWeaponStatusLabel(state);
    return true;
}

void SetHudStatus(
    AppState& state,
    const std::wstring& text)
{
    if (state.hudStatus != nullptr)
    {
        SetWindowTextW(
            state.hudStatus,
            (L"Status: " + text).c_str());
        UpdateWindow(state.hudStatus);
    }
}

void FinishHudEditorPolling(AppState& state)
{
    KillTimer(state.window, kIdHudPollTimer);
    state.pendingHudEditorRequestId.clear();
    state.pendingHudEditorLaunched = false;
    state.pendingHudEditorStartedAt = 0u;
}

void PollHudEditorStatus(AppState& state)
{
    if (state.pendingHudEditorRequestId.empty())
    {
        FinishHudEditorPolling(state);
        return;
    }

    const std::string text = ReadTextFileLimited(
        HudEditorStatusPath(state),
        8192u);
    vh::Response response;
    std::string parseError;
    if (!text.empty() &&
        vh::ParseResponse(text, &response, &parseError) &&
        response.requestId == state.pendingHudEditorRequestId)
    {
        if (response.status == vh::ResponseStatus::Active)
        {
            SetHudStatus(
                state,
                L"Live in headset. Point and hold the right trigger to drag; right-stick up/down resizes; A saves; B cancels.");
            return;
        }

        if (response.status == vh::ResponseStatus::Saved)
        {
            kc::ApplyHudLayoutToSettings(response.layout, &state.values);
            MarkSettingsDirty(state, "Custom");
            UpdateAllControls(state);
            UpdateValidation(state);
            const bool saved = SaveActiveSettings(state, true);
            FinishHudEditorPolling(state);
            SetHudStatus(
                state,
                saved
                    ? L"Headset layout imported and 142/142 settings read-back verified."
                    : L"Headset layout imported, but the settings file could not be saved.");
            MessageBoxW(
                state.window,
                saved
                    ? L"The in-headset HUD layout was imported and saved. The current game already uses it live; future launches will now use it too."
                    : L"The in-headset layout was imported into the configurator, but saving failed. Press Save before closing.",
                saved
                    ? L"HUD layout saved"
                    : L"HUD layout needs saving",
                MB_OK | (saved ? MB_ICONINFORMATION : MB_ICONWARNING));
            return;
        }

        FinishHudEditorPolling(state);
        if (response.status == vh::ResponseStatus::Canceled)
        {
            SetHudStatus(
                state,
                L"Headset edit canceled; the original layout remains unchanged.");
        }
        else
        {
            SetHudStatus(
                state,
                L"The runtime rejected the headset editor request.");
        }
        return;
    }

    const bool gameRunning =
        IsProcessRunning(L"KisakCOD-sp.exe");
    if (gameRunning)
    {
        SetHudStatus(
            state,
            L"Waiting for the VR runtime; enter a mission if the editor is not visible yet.");
        return;
    }

    if (GetTickCount64() - state.pendingHudEditorStartedAt > 20000u)
    {
        FinishHudEditorPolling(state);
        SetHudStatus(
            state,
            L"COD4 did not reach the VR runtime; no HUD values were changed.");
    }
}

void StartHeadsetHudEditor(AppState& state)
{
    UpdateValidation(state);
    if (HasValidationErrors(state))
    {
        ShowValidationError(state);
        return;
    }
    if (!SaveActiveSettings(state, true))
    {
        return;
    }

    vh::Request request;
    request.requestId = NewHudEditorRequestId();
    request.layout = kc::HudLayoutFromSettings(state.values);
    std::error_code removeError;
    std::filesystem::remove(HudEditorStatusPath(state), removeError);
    if (!WriteHudEditorRequestAtomic(
            HudEditorRequestPath(state),
            request))
    {
        SetHudStatus(state, L"Could not write HUD-Editor-Request.txt.");
        MessageBoxW(
            state.window,
            L"The configurator could not write HUD-Editor-Request.txt under your KisakCOD-VR settings folder.",
            L"Headset HUD editor failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    state.pendingHudEditorRequestId = request.requestId;
    state.pendingHudEditorStartedAt = GetTickCount64();
    const bool gameRunning = IsProcessRunning(L"KisakCOD-sp.exe");
    state.pendingHudEditorLaunched = !gameRunning;
    if (!gameRunning && !LaunchBatch(state, L"Launch-KisakCOD-VR.bat"))
    {
        FinishHudEditorPolling(state);
        SetHudStatus(state, L"The game launcher could not start.");
        return;
    }

    SetTimer(state.window, kIdHudPollTimer, 200u, nullptr);
    SetHudStatus(
        state,
        gameRunning
            ? L"Request sent. Put on the headset; the editor will appear over the current mission."
            : L"Launching COD4. Enter a mission; the editor will open automatically.");
    MessageBoxW(
        state.window,
        L"In the headset:\r\n\r\n"
        L"• Press Use / Next weapon (Quest X / Y) to select the previous / next HUD group.\r\n"
        L"• Press Sprint / Melee (Quest L3 / R3) to center / reset only the selected group.\r\n"
        L"• Keyboard: Shift+Tab / Tab selects, Home centers, and End resets.\r\n"
        L"• Point at a HUD group and hold the right trigger to drag it.\r\n"
        L"• Right-stick up/down resizes the selected group.\r\n"
        L"• Hold the off-hand grip to temporarily disable snapping.\r\n"
        L"• Point at SAVE or press A to keep the layout.\r\n"
        L"• Point at CANCEL or press B to restore the original layout.\r\n\r\n"
        L"Gameplay input is suppressed while the editor is active.",
        L"Live headset HUD editor",
        MB_OK | MB_ICONINFORMATION);
}

std::string NewCalibrationRequestId()
{
    std::ostringstream id;
    id << "cfg-" << GetCurrentProcessId() << '-'
       << static_cast<unsigned long long>(GetTickCount64());
    return id.str();
}

std::string NewHudEditorRequestId()
{
    std::ostringstream id;
    id << "hud-" << GetCurrentProcessId() << '-'
       << static_cast<unsigned long long>(GetTickCount64());
    return id.str();
}

bool WriteHudEditorRequestAtomic(
    const std::filesystem::path& path,
    const vh::Request& request)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        return false;
    }

    std::filesystem::path temporary = path;
    temporary += L".tmp";
    {
        std::ofstream output(
            temporary,
            std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return false;
        }
        output << vh::SerializeRequest(request);
        output.flush();
        if (!output)
        {
            return false;
        }
    }
    if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::string ReadTextFileLimited(
    const std::filesystem::path& path,
    const std::size_t maximumBytes)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    std::string text = contents.str();
    return text.size() <= maximumBytes ? text : std::string{};
}

std::map<std::string, std::string> ReadKeyValueFile(
    const std::filesystem::path& path)
{
    std::map<std::string, std::string> values;
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return values;
    }

    std::string line;
    std::size_t totalBytes = 0u;
    while (std::getline(input, line))
    {
        totalBytes += line.size();
        if (totalBytes > 16384u)
        {
            return {};
        }

        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        const std::size_t separator = line.find('=');
        if (separator != std::string::npos)
        {
            values[line.substr(0u, separator)] =
                line.substr(separator + 1u);
        }
    }

    return values;
}

bool WriteCalibrationRequestAtomic(
    const std::filesystem::path& path,
    const vc::Request& request)
{
    std::error_code error;
    std::filesystem::create_directories(
        path.parent_path(),
        error);

    if (error)
    {
        return false;
    }

    std::filesystem::path temporary = path;
    temporary += L".tmp";

    {
        std::ofstream output(
            temporary,
            std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return false;
        }

        output << vc::SerializeRequest(request);
        output.flush();
        if (!output)
        {
            return false;
        }
    }

    if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary, error);
        return false;
    }

    return true;
}

std::map<std::string, std::string> WaitForCalibrationStatus(
    const std::filesystem::path& path,
    const std::string& requestId)
{
    const ULONGLONG deadline =
        GetTickCount64() + 5000u;

    while (GetTickCount64() < deadline)
    {
        const auto status = ReadKeyValueFile(path);
        const auto found = status.find("REQUEST_ID");
        if (found != status.end() &&
            found->second == requestId)
        {
            return status;
        }

        Sleep(75u);
    }

    return {};
}

std::string ActiveCalibrationHeightKey(
    const AppState& state)
{
    return StringValue(
               state.values,
               "KISAK_VR_PLAY_MODE",
               "standing") == "seated"
        ? "KISAK_VR_SEATED_EYE_HEIGHT"
        : "KISAK_VR_STANDING_EYE_HEIGHT";
}

std::string CanonicalHeightValue(const double value)
{
    std::ostringstream formatted;
    formatted.setf(std::ios::fixed, std::ios::floatfield);
    formatted.precision(2);
    formatted << value;
    return formatted.str();
}

void SetCalibrationStatus(
    AppState& state,
    const std::wstring& text)
{
    if (state.calibrationStatus != nullptr)
    {
        SetWindowTextW(
            state.calibrationStatus,
            (L"Status: " + text).c_str());
        UpdateWindow(state.calibrationStatus);
    }
}

void SetCalibrationValue(
    AppState& state,
    const std::string& key,
    const std::string& value)
{
    ReadAllControls(state);
    state.values[key] = value;
    MarkSettingsDirty(state, "Custom");
    UpdateAllControls(state);
    UpdateValidation(state);
}

bool SendCalibrationCommand(
    AppState& state,
    const vc::Command command,
    const bool guidedCountdown)
{
    UpdateValidation(state);
    if (HasValidationErrors(state))
    {
        ShowValidationError(state);
        return false;
    }

    if (!SaveActiveSettings(state, true))
    {
        return false;
    }

    if (!IsProcessRunning(L"KisakCOD-sp.exe"))
    {
        SetCalibrationStatus(
            state,
            L"Settings saved. Launch COD4, enter a mission, and press the calibration button again for live capture.");
        MessageBoxW(
            state.window,
            L"Your height/posture settings were saved, but KisakCOD-sp.exe is not running.\r\n\r\nLaunch the game, enter a mission, then press this calibration button again.",
            L"Live calibration needs COD4",
            MB_OK | MB_ICONINFORMATION);
        return false;
    }

    if (guidedCountdown)
    {
        const wchar_t* instructions =
            L"Put on the headset and assume your normal playing posture. "
            L"After you press OK, the configurator waits three seconds before capturing.";
        switch (command)
        {
        case vc::Command::RecenterPosition:
            instructions =
                L"Put on the headset and assume your normal playing posture.\r\n\r\n"
                L"After you press OK, the configurator waits three seconds and recenters only your physical position. Your existing direction and level remain unchanged.";
            break;
        case vc::Command::RecenterDirectionLevel:
            instructions =
                L"Put on the headset, face the desired forward direction, and look level.\r\n\r\n"
                L"After you press OK, the configurator waits three seconds and captures only direction and level. Your positional origin remains unchanged.";
            break;
        case vc::Command::RecenterFull:
            instructions =
                L"Put on the headset, assume your normal playing posture, face the desired forward direction, and look level.\r\n\r\n"
                L"After you press OK, the configurator waits three seconds and captures both position and direction/level.";
            break;
        case vc::Command::MeasureStanding:
            instructions =
                L"Stand naturally with the headset on.\r\n\r\n"
                L"After you press OK, the configurator waits three seconds and measures eye height from the runtime floor. Position and direction/level remain unchanged.";
            break;
        default:
            break;
        }

        const int accepted = MessageBoxW(
            state.window,
            instructions,
            L"Guided VR calibration",
            MB_OKCANCEL | MB_ICONINFORMATION);

        if (accepted != IDOK)
        {
            return false;
        }

        for (int remaining = 3; remaining >= 1; --remaining)
        {
            SetCalibrationStatus(
                state,
                L"Capturing in " +
                    std::to_wstring(remaining) + L"...");
            Sleep(1000u);
        }
    }

    ReadAllControls(state);

    vc::Request request;
    request.requestId = NewCalibrationRequestId();
    request.command = command;
    request.playMode =
        StringValue(
            state.values,
            "KISAK_VR_PLAY_MODE",
            "standing") == "seated"
            ? vc::PlayMode::Seated
            : vc::PlayMode::Standing;

    const std::string heightKey =
        ActiveCalibrationHeightKey(state);
    request.targetEyeHeightInches =
        static_cast<float>(NumberValue(
            state.values,
            heightKey.c_str(),
            vc::kNativeStandingEyeHeightInches));

    const std::filesystem::path requestPath =
        CalibrationRequestPath(state);
    const std::filesystem::path statusPath =
        CalibrationStatusPath(state);

    std::error_code error;
    std::filesystem::remove(statusPath, error);

    if (!WriteCalibrationRequestAtomic(
            requestPath,
            request))
    {
        SetCalibrationStatus(
            state,
            L"Could not write the live calibration request.");
        MessageBoxW(
            state.window,
            L"The configurator could not write Calibration-Request.txt under your KisakCOD-VR settings folder.",
            L"Calibration request failed",
            MB_OK | MB_ICONERROR);
        return false;
    }

    SetCalibrationStatus(
        state,
        L"Waiting for the running game to acknowledge the request...");

    const auto response = WaitForCalibrationStatus(
        statusPath,
        request.requestId);

    if (response.empty())
    {
        SetCalibrationStatus(
            state,
            L"No runtime response. Make sure a mission is actively rendering, then retry.");
        MessageBoxW(
            state.window,
            L"COD4 was detected, but it did not acknowledge the calibration request within five seconds.\r\n\r\nMake sure you are inside an actively rendering mission and retry.",
            L"Calibration timed out",
            MB_OK | MB_ICONWARNING);
        return false;
    }

    const std::string status = response.count("STATUS") != 0u
        ? response.at("STATUS")
        : "UNKNOWN";

    if (status == "NO_TRACKED_POSE")
    {
        SetCalibrationStatus(
            state,
            L"The runtime has no valid tracked headset pose yet.");
        MessageBoxW(
            state.window,
            L"The game received the request, but the headset did not have a valid tracked pose. Wake the headset and retry.",
            L"No tracked headset pose",
            MB_OK | MB_ICONWARNING);
        return false;
    }

    const bool floorAvailable =
        response.count("FLOOR_AVAILABLE") != 0u &&
        response.at("FLOOR_AVAILABLE") == "1";

    if (command == vc::Command::MeasureStanding &&
        floorAvailable &&
        response.count("MEASURED_EYE_HEIGHT_INCHES") != 0u)
    {
        char* end = nullptr;
        const double measured = std::strtod(
            response.at("MEASURED_EYE_HEIGHT_INCHES").c_str(),
            &end);

        if (end != nullptr && end[0] == '\0' &&
            std::isfinite(measured) &&
            measured >= vc::kMinimumEyeHeightInches &&
            measured <= vc::kMaximumEyeHeightInches)
        {
            SetCalibrationValue(
                state,
                "KISAK_VR_STANDING_EYE_HEIGHT",
                CanonicalHeightValue(measured));
            if (!SaveActiveSettings(state, true))
            {
                SetCalibrationStatus(
                    state,
                    L"The measured height applied live, but could not be saved for the next launch.");
                return false;
            }
        }
    }

    const std::wstring backend = response.count("BACKEND") != 0u
        ? ToWide(response.at("BACKEND"))
        : L"runtime";
    const std::string appliedCanonical =
        response.count("APPLIED_EYE_HEIGHT_INCHES") != 0u
            ? response.at("APPLIED_EYE_HEIGHT_INCHES")
            : CanonicalHeightValue(request.targetEyeHeightInches);
    const std::wstring applied = DisplayMeasurement(
        state.values,
        heightKey.c_str(),
        appliedCanonical);

    std::wstring success;
    switch (command)
    {
    case vc::Command::RecenterPosition:
        success =
            L"Position recentered by " + backend +
            L"; direction and level were preserved";
        break;
    case vc::Command::RecenterDirectionLevel:
        success =
            L"Direction and level recentered by " + backend +
            L"; the positional origin was preserved";
        break;
    case vc::Command::RecenterFull:
        success =
            L"Full recenter applied by " + backend +
            L": position plus direction and level";
        break;
    case vc::Command::MeasureStanding:
        success =
            L"Standing eye height applied by " + backend + L": " +
            applied + L"; position and direction/level were preserved";
        break;
    default:
        success =
            L"Applied by " + backend + L": " + applied +
            L" virtual eye height; position and direction/level were preserved";
        break;
    }

    if (command == vc::Command::MeasureStanding &&
        !floorAvailable)
    {
        success +=
            L". This runtime did not expose a usable floor reference, so the saved manual height was used";
    }

    success += L".";
    SetCalibrationStatus(state, success);

    MessageBoxW(
        state.window,
        success.c_str(),
        L"VR calibration applied",
        MB_OK | MB_ICONINFORMATION);
    return true;
}

void RecenterPositionCalibration(AppState& state)
{
    SendCalibrationCommand(
        state,
        vc::Command::RecenterPosition,
        true);
}

void RecenterDirectionLevelCalibration(AppState& state)
{
    SendCalibrationCommand(
        state,
        vc::Command::RecenterDirectionLevel,
        true);
}

void RecenterFullCalibration(AppState& state)
{
    SendCalibrationCommand(
        state,
        vc::Command::RecenterFull,
        true);
}

void MeasureStandingCalibration(AppState& state)
{
    SetCalibrationValue(
        state,
        "KISAK_VR_PLAY_MODE",
        "standing");
    SendCalibrationCommand(
        state,
        vc::Command::MeasureStanding,
        true);
}

void ApplySeatedCalibration(AppState& state)
{
    SetCalibrationValue(
        state,
        "KISAK_VR_PLAY_MODE",
        "seated");
    SendCalibrationCommand(
        state,
        vc::Command::RecenterPosition,
        true);
}

void AdjustCalibrationHeight(
    AppState& state,
    const double displayDelta,
    const bool reset)
{
    ReadAllControls(state);
    const kc::MeasurementUnitSystem units =
        ActiveMeasurementUnits(state.values);
    const double deltaInches =
        units == kc::MeasurementUnitSystem::Metric
            ? displayDelta / 2.54
            : displayDelta;
    const std::string key =
        ActiveCalibrationHeightKey(state);
    const double current = NumberValue(
        state.values,
        key.c_str(),
        vc::kNativeStandingEyeHeightInches);
    const double requested = reset
        ? vc::kNativeStandingEyeHeightInches
        : std::clamp(
              current + deltaInches,
              static_cast<double>(vc::kMinimumEyeHeightInches),
              static_cast<double>(vc::kMaximumEyeHeightInches));

    SetCalibrationValue(
        state,
        key,
        CanonicalHeightValue(requested));
    SendCalibrationCommand(
        state,
        vc::Command::ApplyHeight,
        false);
}

std::filesystem::path ChooseBatchFile(
    HWND owner,
    const bool save,
    const wchar_t* title)
{
    std::array<wchar_t, 32768> path = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter =
        L"KisakCOD VR settings (*.bat)\0*.bat\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = title;
    dialog.lpstrDefExt = L"bat";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
        (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    const BOOL accepted = save
        ? GetSaveFileNameW(&dialog)
        : GetOpenFileNameW(&dialog);
    return accepted ? std::filesystem::path(path.data()) : std::filesystem::path();
}

void ImportSettings(AppState& state)
{
    const std::filesystem::path selected = ChooseBatchFile(
        state.window,
        false,
        L"Import KisakCOD VR settings");
    if (selected.empty())
    {
        return;
    }

    const kc::LoadResult loaded = kc::LoadSettings(
        state.defaultsPath,
        selected);
    const bool hasError = std::any_of(
        loaded.messages.begin(),
        loaded.messages.end(),
        [](const kc::ValidationMessage& message)
        {
            return message.severity == kc::ValidationMessage::Severity::Error;
        });

    if (hasError)
    {
        MessageBoxW(
            state.window,
            L"That file contains invalid or conflicting KisakCOD VR settings and was not imported.",
            L"Import failed",
            MB_OK | MB_ICONWARNING);
        return;
    }

    state.values = loaded.values;
    MarkSettingsDirty(state, "Imported");
    UpdateAllControls(state);
    UpdateValidation(state);
}

void ExportSettings(AppState& state)
{
    UpdateValidation(state);
    if (HasValidationErrors(state))
    {
        ShowValidationError(state);
        return;
    }

    const std::filesystem::path selected = ChooseBatchFile(
        state.window,
        true,
        L"Export KisakCOD VR settings");
    if (selected.empty())
    {
        return;
    }

    const kc::SaveResult saved = kc::SaveUserSettingsAtomic(
        selected,
        state.values,
        state.profileName);
    if (!saved.success)
    {
        MessageBoxW(
            state.window,
            ToWide(saved.error).c_str(),
            L"Export failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    MessageBoxW(
        state.window,
        (L"Profile exported to:\r\n" + selected.wstring()).c_str(),
        L"KisakCOD VR",
        MB_OK | MB_ICONINFORMATION);
}

void ApplySelectedPreset(AppState& state)
{
    const LRESULT selected = SendMessageW(state.preset, CB_GETCURSEL, 0, 0);
    const std::vector<std::string> presets = kc::PresetNames();
    if (selected < 0 || static_cast<std::size_t>(selected) >= presets.size())
    {
        return;
    }

    const std::string& name = presets[static_cast<std::size_t>(selected)];
    if (kc::ApplyPreset(name, &state.values))
    {
        MarkSettingsDirty(state, name);
        UpdateAllControls(state);
        UpdateValidation(state);
        RefreshCompatibility(state, false);
    }
}

void RestoreDefaults(AppState& state)
{
    if (MessageBoxW(
            state.window,
            L"Restore every setting to the tested Quest 3 / Virtual Desktop defaults?",
            L"Restore tested defaults",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    state.values = kc::BuiltInDefaults();
    MarkSettingsDirty(state, "Tested Quest 3");
    UpdateAllControls(state);
    UpdateValidation(state);
    RefreshCompatibility(state, false);
}

void DrawTextSimple(
    HDC dc,
    const std::wstring& text,
    RECT rect,
    const UINT format,
    const COLORREF color)
{
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, &rect, format);
}

void DrawCompatibilityPreview(
    HDC dc,
    const RECT& client,
    const AppState& state)
{
    RECT title = {18, 14, client.right - 18, 44};
    DrawTextSimple(
        dc,
        L"Launch readiness",
        title,
        DT_LEFT | DT_SINGLELINE,
        RGB(30, 39, 55));

    COLORREF statusColor = RGB(47, 121, 75);
    if (state.compatibilityReport.status == vrc::Status::Warning)
    {
        statusColor = RGB(196, 119, 47);
    }
    else if (state.compatibilityReport.status == vrc::Status::Blocked)
    {
        statusColor = RGB(178, 78, 42);
    }

    RECT badge = {18, 54, client.right - 18, 102};
    HBRUSH badgeBrush = CreateSolidBrush(statusColor);
    FillRect(dc, &badge, badgeBrush);
    DeleteObject(badgeBrush);
    DrawTextSimple(
        dc,
        CompatibilityStatusLabel(state.compatibilityReport.status),
        badge,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE,
        RGB(255, 255, 255));

    int y = 120;
    for (const vrc::Check& check : state.compatibilityReport.checks)
    {
        COLORREF rowColor = RGB(47, 121, 75);
        if (check.status == vrc::Status::Warning)
        {
            rowColor = RGB(196, 119, 47);
        }
        else if (check.status == vrc::Status::Blocked)
        {
            rowColor = RGB(178, 78, 42);
        }

        HBRUSH dot = CreateSolidBrush(rowColor);
        HGDIOBJ oldBrush = SelectObject(dc, dot);
        Ellipse(dc, 22, y + 2, 34, y + 14);
        SelectObject(dc, oldBrush);
        DeleteObject(dot);

        RECT label = {42, y, client.right - 18, y + 20};
        DrawTextSimple(
            dc,
            ToWide(check.label),
            label,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS,
            RGB(57, 68, 84));
        y += 35;
        if (y > 345)
        {
            break;
        }
    }

    RECT recommendation = {18, 370, client.right - 18, 432};
    DrawTextSimple(
        dc,
        ToWide(state.compatibilityReport.recommendationSummary),
        recommendation,
        DT_LEFT | DT_WORDBREAK,
        RGB(104, 63, 20));
}

void DrawControllerPreview(HDC dc, const RECT& client, const AppState& state)
{
    RECT title = {18, 14, client.right - 18, 44};
    DrawTextSimple(
        dc,
        L"Controller bindings",
        title,
        DT_LEFT | DT_SINGLELINE,
        RGB(30, 39, 55));

    HPEN outline = CreatePen(PS_SOLID, 2, RGB(76, 91, 112));
    HGDIOBJ oldPen = SelectObject(dc, outline);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(WHITE_BRUSH));

    Ellipse(dc, 38, 72, 150, 250);
    Ellipse(dc, 205, 72, 317, 250);
    Ellipse(dc, 70, 112, 108, 150);
    Ellipse(dc, 237, 112, 275, 150);

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(outline);

    const auto bindingLabel = [&](const char* key, const wchar_t* role)
    {
        const vi::ActionDefinition* const action =
            vi::FindActionDefinition(key);
        const std::string value =
            StringValue(state.values, key, "unbound");
        return std::wstring(role) + L": " +
            (action != nullptr
                ? ControllerBindingLabel(value, action->action)
                : ControllerSourceLabel(value));
    };

    RECT left = {24, 270, 170, 360};
    DrawTextSimple(
        dc,
        bindingLabel("KISAK_VR_BIND_ATTACK", L"Fire") + L"\r\n" +
            bindingLabel("KISAK_VR_BIND_JUMP", L"Jump") + L"\r\n" +
            bindingLabel("KISAK_VR_BIND_USE", L"Use"),
        left,
        DT_LEFT | DT_WORDBREAK,
        RGB(57, 68, 84));

    RECT right = {187, 270, 338, 360};
    DrawTextSimple(
        dc,
        bindingLabel("KISAK_VR_BIND_RELOAD", L"Reload") + L"\r\n" +
            bindingLabel("KISAK_VR_BIND_MELEE", L"Melee") + L"\r\n" +
            bindingLabel("KISAK_VR_BIND_STANCE", L"Stance"),
        right,
        DT_LEFT | DT_WORDBREAK,
        RGB(57, 68, 84));

    RECT fixed = {24, 370, 334, 425};
    DrawTextSimple(
        dc,
        L"Every action supports primary/alternate slots. Button slots may combine up to four inputs as an AND-chord.",
        fixed,
        DT_LEFT | DT_WORDBREAK,
        RGB(104, 63, 20));
}

void DrawGeneralPreview(HDC dc, const RECT& client, const AppState& state)
{
    RECT title = {18, 14, client.right - 18, 44};
    DrawTextSimple(
        dc,
        L"Live placement preview",
        title,
        DT_LEFT | DT_SINGLELINE,
        RGB(30, 39, 55));

    RECT viewport = {28, 58, 328, 228};
    HBRUSH viewportBrush = CreateSolidBrush(RGB(25, 35, 52));
    FillRect(dc, &viewport, viewportBrush);
    DeleteObject(viewportBrush);
    FrameRect(dc, &viewport, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

    const double safeX = NumberValue(state.values, "KISAK_VR_HUD_SAFE_X", 0.5);
    const double safeY = NumberValue(state.values, "KISAK_VR_HUD_SAFE_Y", 1.0);
    const int insetX = static_cast<int>((1.0 - safeX) * 0.5 * 300.0);
    const int insetY = static_cast<int>((1.0 - safeY) * 0.5 * 170.0);
    RECT safe = {
        viewport.left + insetX,
        viewport.top + insetY,
        viewport.right - insetX,
        viewport.bottom - insetY,
    };
    HPEN safePen = CreatePen(PS_DOT, 1, RGB(83, 181, 255));
    HGDIOBJ oldPen = SelectObject(dc, safePen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, safe.left, safe.top, safe.right, safe.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(safePen);

    const double hudScale = NumberValue(
        state.values,
        "KISAK_VR_HUD_BOTTOM_LEFT_SCALE",
        0.5);
    RECT ammo = {
        safe.left + 8,
        safe.bottom - static_cast<int>(42.0 * hudScale),
        safe.left + static_cast<int>(90.0 * hudScale),
        safe.bottom - 8,
    };
    HBRUSH ammoBrush = CreateSolidBrush(RGB(74, 149, 99));
    FillRect(dc, &ammo, ammoBrush);
    DeleteObject(ammoBrush);

    if (StringValue(state.values, "KISAK_VR_COMPASS_ENABLED", "1") == "1")
    {
        const double compassScale = NumberValue(
            state.values,
            "KISAK_VR_COMPASS_SIZE",
            1.0);
        const double compassInsetX = NumberValue(
            state.values,
            "KISAK_VR_COMPASS_INSET_X",
            220.0);
        const double compassInsetY = NumberValue(
            state.values,
            "KISAK_VR_COMPASS_INSET_Y",
            48.0);
        const int radius = static_cast<int>(17.0 * compassScale);
        const int centerX = viewport.right - 20 -
            static_cast<int>(compassInsetX / 320.0 * 90.0);
        const int centerY = viewport.bottom - 20 -
            static_cast<int>(compassInsetY / 180.0 * 50.0);
        HBRUSH compassBrush = CreateSolidBrush(RGB(210, 160, 69));
        HGDIOBJ oldCompass = SelectObject(dc, compassBrush);
        Ellipse(
            dc,
            centerX - radius,
            centerY - radius,
            centerX + radius,
            centerY + radius);
        SelectObject(dc, oldCompass);
        DeleteObject(compassBrush);
    }

    const int textX = static_cast<int>(NumberValue(
        state.values,
        "KISAK_VR_GAME_MESSAGE_X_OFFSET",
        0.0) / 300.0 * 65.0);
    const int textY = static_cast<int>(NumberValue(
        state.values,
        "KISAK_VR_GAME_MESSAGE_Y_OFFSET",
        72.0) / 200.0 * 70.0);
    const double textScale = NumberValue(
        state.values,
        "KISAK_VR_GAME_MESSAGE_SCALE",
        1.0);
    RECT message = {
        105 + textX,
        78 + textY,
        278 + textX,
        96 + textY + static_cast<int>(8.0 * textScale),
    };
    DrawTextSimple(
        dc,
        L"MISSION MESSAGE",
        message,
        DT_CENTER | DT_SINGLELINE,
        RGB(240, 240, 240));

    RECT label = {28, 238, 328, 260};
    DrawTextSimple(
        dc,
        L"Controller-local weapon, hand and belt calibration",
        label,
        DT_CENTER | DT_SINGLELINE,
        RGB(57, 68, 84));

    const int centerX = 178;
    const int headY = 294;
    HBRUSH headBrush = CreateSolidBrush(RGB(80, 105, 142));
    HGDIOBJ oldHead = SelectObject(dc, headBrush);
    Ellipse(dc, centerX - 20, headY - 20, centerX + 20, headY + 20);
    SelectObject(dc, oldHead);
    DeleteObject(headBrush);

    const double weaponLeft = NumberValue(
        state.values,
        "KISAK_VR_WEAPON_OFFSET_LEFT",
        0.0);
    const double weaponUp = NumberValue(
        state.values,
        "KISAK_VR_WEAPON_OFFSET_UP",
        0.0);
    const double weaponForward = NumberValue(
        state.values,
        "KISAK_VR_WEAPON_OFFSET_FORWARD",
        0.0);
    const POINT rightHand = {
        centerX + 52 - static_cast<int>(weaponLeft * 3.0),
        headY + 55 - static_cast<int>(weaponUp * 3.0),
    };
    const POINT muzzle = {
        rightHand.x + 62 + static_cast<int>(weaponForward * 3.0),
        rightHand.y - 30 - static_cast<int>(
            NumberValue(state.values, "KISAK_VR_WEAPON_PITCH", 0.0) * 0.6),
    };

    HPEN weaponPen = CreatePen(PS_SOLID, 6, RGB(61, 132, 104));
    oldPen = SelectObject(dc, weaponPen);
    MoveToEx(dc, rightHand.x, rightHand.y, nullptr);
    LineTo(dc, muzzle.x, muzzle.y);
    SelectObject(dc, oldPen);
    DeleteObject(weaponPen);

    const double leftOffset = NumberValue(
        state.values,
        "KISAK_VR_LEFT_HAND_OFFSET_LEFT",
        0.0);
    const double leftUp = NumberValue(
        state.values,
        "KISAK_VR_LEFT_HAND_OFFSET_UP",
        0.0);
    const POINT leftHand = {
        centerX - 50 - static_cast<int>(leftOffset * 3.0),
        headY + 58 - static_cast<int>(leftUp * 3.0),
    };
    HBRUSH handBrush = CreateSolidBrush(RGB(208, 139, 91));
    oldBrush = SelectObject(dc, handBrush);
    Ellipse(dc, leftHand.x - 8, leftHand.y - 8, leftHand.x + 8, leftHand.y + 8);
    Ellipse(dc, rightHand.x - 8, rightHand.y - 8, rightHand.x + 8, rightHand.y + 8);
    SelectObject(dc, oldBrush);
    DeleteObject(handBrush);

    const double beltHeight = NumberValue(
        state.values,
        "KISAK_VR_BELT_HEIGHT",
        -28.0);
    const double hipDistance = NumberValue(
        state.values,
        "KISAK_VR_BELT_HIP_DISTANCE",
        13.0);
    const double grabRadius = NumberValue(
        state.values,
        "KISAK_VR_BELT_GRAB_RADIUS",
        11.0);
    const int beltY = headY + 52 + static_cast<int>((-28.0 - beltHeight) * 2.0);
    const int hipPixels = static_cast<int>(hipDistance * 3.0);
    const int radiusPixels = static_cast<int>(grabRadius * 1.5);
    HPEN beltPen = CreatePen(PS_DOT, 2, RGB(157, 95, 177));
    oldPen = SelectObject(dc, beltPen);
    oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Ellipse(
        dc,
        centerX - hipPixels - radiusPixels,
        beltY - radiusPixels,
        centerX - hipPixels + radiusPixels,
        beltY + radiusPixels);
    Ellipse(
        dc,
        centerX + hipPixels - radiusPixels,
        beltY - radiusPixels,
        centerX + hipPixels + radiusPixels,
        beltY + radiusPixels);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(beltPen);

    RECT footer = {20, 405, client.right - 20, 438};
    DrawTextSimple(
        dc,
        state.validation.empty()
            ? L"Valid configuration"
            : ToWide(state.validation.front().message),
        footer,
        DT_CENTER | DT_WORDBREAK,
        state.validation.empty() ? RGB(47, 121, 75) : RGB(178, 78, 42));
}

void DrawCalibrationPreview(
    HDC dc,
    const RECT& client,
    const AppState& state)
{
    const bool seated =
        StringValue(
            state.values,
            "KISAK_VR_PLAY_MODE",
            "standing") == "seated";
    const char* const heightKey = seated
        ? "KISAK_VR_SEATED_EYE_HEIGHT"
        : "KISAK_VR_STANDING_EYE_HEIGHT";
    const double targetHeight = NumberValue(
        state.values,
        heightKey,
        vc::kNativeStandingEyeHeightInches);
    const double correction = targetHeight -
        vc::kNativeStandingEyeHeightInches;
    const std::wstring targetDisplay = DisplayMeasurement(
        state.values,
        heightKey,
        CanonicalHeightValue(targetHeight));
    std::wstring correctionDisplay = DisplayMeasurement(
        state.values,
        heightKey,
        CanonicalHeightValue(correction));
    if (correction > 0.0)
    {
        correctionDisplay.insert(correctionDisplay.begin(), L'+');
    }

    RECT title = {18, 14, client.right - 18, 42};
    DrawTextSimple(
        dc,
        L"View calibration",
        title,
        DT_LEFT | DT_SINGLELINE,
        RGB(30, 39, 55));

    RECT summary = {18, 48, client.right - 18, 104};
    std::wostringstream summaryText;
    summaryText << (seated ? L"Seated" : L"Standing")
                << L" mode\r\nTarget eye height: "
                << targetDisplay << L"  |  Correction: "
                << correctionDisplay;
    DrawTextSimple(
        dc,
        summaryText.str(),
        summary,
        DT_LEFT | DT_WORDBREAK,
        RGB(57, 68, 84));

    const int floorY = 368;
    const int centerX = client.right / 2;
    const int eyeY = floorY - static_cast<int>(
        std::clamp(targetHeight, 42.0, 84.0) * 3.5);

    HPEN floorPen = CreatePen(PS_SOLID, 2, RGB(76, 91, 112));
    HGDIOBJ oldPen = SelectObject(dc, floorPen);
    MoveToEx(dc, 34, floorY, nullptr);
    LineTo(dc, client.right - 34, floorY);

    HPEN bodyPen = CreatePen(PS_SOLID, 5, RGB(61, 132, 104));
    SelectObject(dc, bodyPen);
    MoveToEx(dc, centerX, floorY, nullptr);
    LineTo(dc, centerX, eyeY + 18);
    MoveToEx(dc, centerX, eyeY + 92, nullptr);
    LineTo(dc, centerX - 38, eyeY + 142);
    MoveToEx(dc, centerX, eyeY + 92, nullptr);
    LineTo(dc, centerX + 38, eyeY + 142);

    HBRUSH headBrush = CreateSolidBrush(RGB(83, 181, 255));
    HGDIOBJ oldBrush = SelectObject(dc, headBrush);
    Ellipse(dc, centerX - 19, eyeY - 12, centerX + 19, eyeY + 26);
    SelectObject(dc, oldBrush);
    SelectObject(dc, floorPen);
    DeleteObject(headBrush);
    DeleteObject(bodyPen);

    HPEN nativePen = CreatePen(PS_DOT, 1, RGB(196, 119, 47));
    SelectObject(dc, nativePen);
    const int nativeY = floorY - static_cast<int>(
        vc::kNativeStandingEyeHeightInches * 3.5f);
    MoveToEx(dc, 44, nativeY, nullptr);
    LineTo(dc, client.right - 44, nativeY);
    SelectObject(dc, oldPen);
    DeleteObject(nativePen);
    DeleteObject(floorPen);

    RECT label = {30, floorY + 8, client.right - 30, floorY + 34};
    DrawTextSimple(
        dc,
        L"Runtime floor / seated origin",
        label,
        DT_CENTER | DT_SINGLELINE,
        RGB(76, 91, 112));

    RECT footer = {22, 404, client.right - 22, 438};
    DrawTextSimple(
        dc,
        L"Position and direction/level recentering are independent. Height changes preserve COD4's native crouch and prone steps.",
        footer,
        DT_CENTER | DT_WORDBREAK,
        RGB(104, 63, 20));
}

LRESULT CALLBACK PreviewWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    AppState* state = reinterpret_cast<AppState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<AppState*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }

    if (message == WM_PAINT && state != nullptr)
    {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        RECT client = {};
        GetClientRect(window, &client);
        FillRect(dc, &client, state->previewBrush);
        FrameRect(dc, &client, static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
        SelectObject(dc, state->font);

        if (state->selectedPage == PageIndex(kc::SettingPage::Quick))
        {
            DrawCompatibilityPreview(dc, client, *state);
        }
        else if (state->selectedPage == PageIndex(kc::SettingPage::Controls))
        {
            DrawControllerPreview(dc, client, *state);
        }
        else if (state->selectedPage ==
                 PageIndex(kc::SettingPage::Calibration))
        {
            DrawCalibrationPreview(dc, client, *state);
        }
        else
        {
            DrawGeneralPreview(dc, client, *state);
        }

        EndPaint(window, &paint);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

void ShowSetupPanel(AppState& state, const bool show)
{
    for (HWND control : state.setupControls)
    {
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    }
}

void BuildSetupPanel(AppState& state)
{
    const auto add = [&](HWND control)
    {
        state.setupControls.push_back(control);
        return control;
    };

    add(CreateControl(
        state,
        L"STATIC",
        L"One read-only scan for installation, DirectX, GPU, 32/64-bit runtime registration, OpenVR fallback, headset history, and controllers",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 38,
        kTabWidth - 40,
        22));

    state.setupStatus = add(CreateControl(
        state,
        L"STATIC",
        L"Compatibility: scanning...",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 62,
        kTabWidth - 40,
        22));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Rescan system",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 20,
        kTabTop + 86,
        132,
        30,
        kIdSetupRescan));

    state.setupApplyRecommended = add(CreateControl(
        state,
        L"BUTTON",
        L"Apply recommended",
        BS_DEFPUSHBUTTON | WS_TABSTOP,
        kTabLeft + 160,
        kTabTop + 86,
        164,
        30,
        kIdSetupApplyRecommended));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Copy support report",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 332,
        kTabTop + 86,
        170,
        30,
        kIdSetupCopyReport));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Open report",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 510,
        kTabTop + 86,
        192,
        30,
        kIdSetupOpenReport));

    state.setupRecommendation = add(CreateControl(
        state,
        L"STATIC",
        L"Recommended setup: calculating...",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 122,
        kTabWidth - 40,
        42));

    state.setupDetails = add(CreateControl(
        state,
        L"EDIT",
        L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY |
            WS_VSCROLL,
        kTabLeft + 20,
        kTabTop + 166,
        kTabWidth - 40,
        154,
        0,
        WS_EX_CLIENTEDGE));
}

void BuildSettingControls(AppState& state)
{
    const auto& catalog = kc::SettingsCatalog();
    const kc::MeasurementUnitSystem units =
        ActiveMeasurementUnits(state.values);
    state.bindings.reserve(catalog.size());

    for (std::size_t index = 0; index < catalog.size(); ++index)
    {
        const kc::SettingDefinition& definition = catalog[index];

        if (definition.type == kc::SettingType::Binding ||
            definition.key == "KISAK_VR_INPUT_BINDINGS_VERSION")
        {
            continue;
        }

        ControlBinding binding;
        binding.definition = &definition;
        binding.description = ToWide(definition.description);
        binding.label = CreateControl(
            state,
            L"STATIC",
            ToWide(kc::DisplaySettingLabel(
                definition,
                units)).c_str(),
            SS_LEFT,
            0,
            0,
            100,
            18);

        const int identifier =
            kIdSettingBase +
            static_cast<int>(state.bindings.size());
        if (definition.type == kc::SettingType::Choice ||
            definition.type == kc::SettingType::Toggle ||
            definition.type == kc::SettingType::Binding)
        {
            binding.control = CreateControl(
                state,
                WC_COMBOBOXW,
                L"",
                CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                0,
                0,
                100,
                240,
                identifier,
                WS_EX_CLIENTEDGE);

            for (const kc::SettingChoice& choice : definition.choices)
            {
                const std::wstring label = ToWide(choice.label);
                SendMessageW(
                    binding.control,
                    CB_ADDSTRING,
                    0,
                    reinterpret_cast<LPARAM>(label.c_str()));
            }
        }
        else
        {
            binding.control = CreateControl(
                state,
                L"EDIT",
                L"",
                ES_AUTOHSCROLL | WS_TABSTOP,
                0,
                0,
                100,
                26,
                identifier,
                WS_EX_CLIENTEDGE);
        }

        state.bindings.push_back(std::move(binding));
    }

    // Tooltip text pointers must refer to the final vector storage.
    for (ControlBinding& binding : state.bindings)
    {
        AddTooltip(state, binding.label, &binding.description);
        AddTooltip(state, binding.control, &binding.description);
    }
}

void BuildControllerBindingEditor(AppState& state)
{
    const auto add = [&](HWND control)
    {
        state.bindingEditorControls.push_back(control);
        return control;
    };

    add(CreateControl(
        state,
        L"STATIC",
        L"Select an action. Stick directions mean movement, not stick click (for example, Right stick up). A slot may be one input or a chord of up to four inputs held together.",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 50,
        kTabWidth - 40,
        36));

    state.bindingActionList = add(CreateControl(
        state,
        L"LISTBOX",
        L"",
        LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP | WS_VSCROLL,
        kTabLeft + 20,
        kTabTop + 88,
        kTabWidth - 40,
        326,
        kIdBindingActionList,
        WS_EX_CLIENTEDGE));

    add(CreateControl(
        state,
        L"STATIC",
        L"Primary binding",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 424,
        315,
        20));

    add(CreateControl(
        state,
        L"STATIC",
        L"Alternate binding (optional)",
        SS_LEFT,
        kTabLeft + 382,
        kTabTop + 424,
        315,
        20));

    state.bindingPrimary = add(CreateControl(
        state,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
        kTabLeft + 20,
        kTabTop + 446,
        180,
        300,
        kIdBindingPrimary,
        WS_EX_CLIENTEDGE));

    state.capturePrimary = add(CreateControl(
        state,
        L"BUTTON",
        L"Bind...",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 206,
        kTabTop + 446,
        72,
        28,
        kIdCapturePrimary));

    state.chordPrimary = add(CreateControl(
        state,
        L"BUTTON",
        L"Chord...",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 284,
        kTabTop + 446,
        74,
        28,
        kIdChordPrimary));

    state.bindingAlternate = add(CreateControl(
        state,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
        kTabLeft + 382,
        kTabTop + 446,
        154,
        300,
        kIdBindingAlternate,
        WS_EX_CLIENTEDGE));

    state.captureAlternate = add(CreateControl(
        state,
        L"BUTTON",
        L"Bind...",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 542,
        kTabTop + 446,
        72,
        28,
        kIdCaptureAlternate));

    state.chordAlternate = add(CreateControl(
        state,
        L"BUTTON",
        L"Chord...",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 620,
        kTabTop + 446,
        74,
        28,
        kIdChordAlternate));

    state.clearBinding = add(CreateControl(
        state,
        L"BUTTON",
        L"Clear alternate",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 542,
        kTabTop + 486,
        152,
        28,
        kIdClearBinding));
}

void ShowCalibrationPanel(
    AppState& state,
    const bool show)
{
    for (HWND control : state.calibrationControls)
    {
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    }
}

void BuildCalibrationPanel(AppState& state)
{
    const auto add = [&](HWND control)
    {
        state.calibrationControls.push_back(control);
        return control;
    };

    add(CreateControl(
        state,
        L"STATIC",
        L"Guided live calibration (keep COD4 running in a mission)",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 312,
        kTabWidth - 40,
        22));

    add(CreateControl(
        state,
        L"STATIC",
        L"Choose exactly what to recenter. Position only never changes your angles; Direction / level only never moves the positional origin. Each guided action waits three seconds.",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 338,
        kTabWidth - 40,
        40));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Recenter position only",
        BS_DEFPUSHBUTTON | WS_TABSTOP,
        kTabLeft + 20,
        kTabTop + 382,
        216,
        34,
        kIdCalibrationRecenterPosition));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Recenter direction / level only",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 248,
        kTabTop + 382,
        225,
        34,
        kIdCalibrationRecenterDirectionLevel));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Full recenter",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 485,
        kTabTop + 382,
        215,
        34,
        kIdCalibrationRecenterFull));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Measure standing height (center unchanged)",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 20,
        kTabTop + 428,
        330,
        34,
        kIdCalibrationMeasureStanding));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Apply seated + recenter position",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 362,
        kTabTop + 428,
        338,
        34,
        kIdCalibrationApplySeated));

    add(CreateControl(
        state,
        L"STATIC",
        L"Fine height adjustment (applies live without changing your forward direction)",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 474,
        kTabWidth - 40,
        22));

    state.calibrationShorter = add(CreateControl(
        state,
        L"BUTTON",
        L"1 cm shorter",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 20,
        kTabTop + 500,
        150,
        32,
        kIdCalibrationShorter));

    state.calibrationResetHeight = add(CreateControl(
        state,
        L"BUTTON",
        L"Reset current mode to 152.4 cm",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 182,
        kTabTop + 500,
        260,
        32,
        kIdCalibrationResetHeight));

    state.calibrationTaller = add(CreateControl(
        state,
        L"BUTTON",
        L"1 cm taller",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 454,
        kTabTop + 500,
        150,
        32,
        kIdCalibrationTaller));

    state.calibrationStatus = add(CreateControl(
        state,
        L"STATIC",
        L"Status: Save and launch COD4, enter a mission, then use a calibration button.",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 540,
        kTabWidth - 40,
        38));
}

void ShowHudPanel(
    AppState& state,
    const bool show)
{
    for (HWND control : state.hudControls)
    {
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    }
}

void BuildHudPanel(AppState& state)
{
    const auto add = [&](HWND control)
    {
        state.hudControls.push_back(control);
        return control;
    };

    add(CreateControl(
        state,
        L"STATIC",
        L"Visual placement — drag snap points instead of guessing numeric offsets",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 48,
        kTabWidth - 40,
        22));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Open desktop visual editor",
        BS_DEFPUSHBUTTON | WS_TABSTOP,
        kTabLeft + 20,
        kTabTop + 74,
        326,
        34,
        kIdHudVisualEditor));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Edit live in headset",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 358,
        kTabTop + 74,
        342,
        34,
        kIdHudHeadsetEditor));

    state.hudStatus = add(CreateControl(
        state,
        L"STATIC",
        L"Status: Desktop edits are offline; headset edits manipulate the actual mission HUD and save back automatically.",
        SS_LEFT | SS_PATHELLIPSIS,
        kTabLeft + 20,
        kTabTop + 113,
        kTabWidth - 40,
        22));
}

HWND CreateWeaponEditorControl(
    WeaponEditorState& state,
    const wchar_t* const className,
    const wchar_t* const text,
    const DWORD style,
    const int x,
    const int y,
    const int width,
    const int height,
    const int identifier = 0,
    const DWORD extendedStyle = 0u)
{
    HWND control = CreateWindowExW(
        extendedStyle,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
        state.app->instance,
        nullptr);
    if (control != nullptr)
    {
        SetFont(control, state.app->font);
    }
    return control;
}

vwp::WeaponProfile* SelectedWeaponProfile(WeaponEditorState& state)
{
    return state.selectedWeaponId.empty()
        ? nullptr
        : vwp::FindWeapon(state.document, state.selectedWeaponId);
}

vwp::GunstockProfile* SelectedGunstockProfile(WeaponEditorState& state)
{
    return vwp::FindGunstock(state.document, state.selectedGunstockId);
}

vwp::Pose* SelectedWeaponEditorPose(WeaponEditorState& state)
{
    if (state.layer == WeaponEditorLayer::Gunstock)
    {
        vwp::GunstockProfile* const stock = SelectedGunstockProfile(state);
        return stock == nullptr ? nullptr : &stock->shouldered;
    }
    vwp::WeaponProfile* const weapon = SelectedWeaponProfile(state);
    if (weapon == nullptr)
    {
        return nullptr;
    }
    return state.layer == WeaponEditorLayer::WeaponShouldered
        ? &weapon->shouldered
        : &weapon->hip;
}

void PopulateWeaponEditorCombos(WeaponEditorState& state)
{
    state.suppressEvents = true;
    SendMessageW(state.weaponCombo, CB_RESETCONTENT, 0, 0);
    int selectedWeapon = -1;
    for (std::size_t index = 0u; index < state.document.weapons.size(); ++index)
    {
        const vwp::WeaponProfile& profile = state.document.weapons[index];
        const std::wstring label = ToWide(profile.name + " [" + profile.id + "]");
        const LRESULT item = SendMessageW(
            state.weaponCombo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(
            state.weaponCombo,
            CB_SETITEMDATA,
            item,
            static_cast<LPARAM>(index));
        if (profile.id == state.selectedWeaponId)
        {
            selectedWeapon = static_cast<int>(item);
        }
    }
    if (selectedWeapon < 0 && !state.document.weapons.empty())
    {
        selectedWeapon = 0;
        state.selectedWeaponId = state.document.weapons.front().id;
    }
    SendMessageW(state.weaponCombo, CB_SETCURSEL, selectedWeapon, 0);

    SendMessageW(state.gunstockCombo, CB_RESETCONTENT, 0, 0);
    int selectedStock = 0;
    for (std::size_t index = 0u; index < state.document.gunstocks.size(); ++index)
    {
        const vwp::GunstockProfile& profile = state.document.gunstocks[index];
        const std::wstring label = ToWide(profile.name + " [" + profile.id + "]");
        const LRESULT item = SendMessageW(
            state.gunstockCombo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(
            state.gunstockCombo,
            CB_SETITEMDATA,
            item,
            static_cast<LPARAM>(index));
        if (profile.id == state.selectedGunstockId)
        {
            selectedStock = static_cast<int>(item);
        }
    }
    SendMessageW(state.gunstockCombo, CB_SETCURSEL, selectedStock, 0);
    if (!state.document.gunstocks.empty())
    {
        state.selectedGunstockId =
            state.document.gunstocks[static_cast<std::size_t>(selectedStock)].id;
        state.document.activeGunstockId = state.selectedGunstockId;
    }
    state.suppressEvents = false;
}

void LoadWeaponEditorLayer(WeaponEditorState& state)
{
    state.suppressEvents = true;
    const vwp::Pose* const pose = SelectedWeaponEditorPose(state);
    const kc::MeasurementUnitSystem units =
        ActiveMeasurementUnits(state.app->values);
    const bool metric = units == kc::MeasurementUnitSystem::Metric;
    for (std::size_t index = 0u; index < state.valueEdits.size(); ++index)
    {
        float value = 0.0f;
        if (pose != nullptr)
        {
            value = index < 3u
                ? pose->offset[index]
                : pose->angles[index - 3u];
        }
        if (index < 3u && metric)
        {
            value *= 2.54f;
        }
        std::wostringstream rendered;
        rendered << std::fixed << std::setprecision(index < 3u ? 2 : 1)
                 << value;
        SetWindowTextW(state.valueEdits[index], rendered.str().c_str());
        EnableWindow(state.valueEdits[index], pose != nullptr);
    }

    bool enabled = false;
    std::string name;
    if (state.layer == WeaponEditorLayer::Gunstock)
    {
        const vwp::GunstockProfile* const profile =
            SelectedGunstockProfile(state);
        enabled = profile != nullptr && profile->enabled;
        name = profile != nullptr ? profile->name : "";
    }
    else
    {
        const vwp::WeaponProfile* const profile =
            SelectedWeaponProfile(state);
        enabled = profile != nullptr && profile->enabled;
        name = profile != nullptr ? profile->name : "";
    }
    SendMessageW(
        state.enabled,
        BM_SETCHECK,
        enabled ? BST_CHECKED : BST_UNCHECKED,
        0);
    SetWindowTextW(
        GetDlgItem(state.window, kIdWeaponEditName),
        ToWide(name).c_str());

    std::wstring status = L"Layer: ";
    if (state.layer == WeaponEditorLayer::Gunstock)
    {
        status += L"active physical gunstock mount correction";
    }
    else if (state.layer == WeaponEditorLayer::WeaponShouldered)
    {
        status += L"per-weapon shouldered/ADS delta";
    }
    else
    {
        status += L"per-weapon hip-fire delta";
    }
    status += metric
        ? L". Position values are centimeters; rotation is degrees."
        : L". Position values are inches; rotation is degrees.";
    SetWindowTextW(state.status, status.c_str());
    state.suppressEvents = false;
}

bool CommitWeaponEditorLayer(WeaponEditorState& state, const bool showError)
{
    vwp::Pose* const pose = SelectedWeaponEditorPose(state);
    if (pose == nullptr)
    {
        return state.layer == WeaponEditorLayer::Gunstock
            ? false
            : state.selectedWeaponId.empty();
    }

    const bool metric = ActiveMeasurementUnits(state.app->values) ==
        kc::MeasurementUnitSystem::Metric;
    vwp::Pose parsed = *pose;
    for (std::size_t index = 0u; index < state.valueEdits.size(); ++index)
    {
        const std::string text = ToUtf8(WindowText(state.valueEdits[index]));
        char* end = nullptr;
        float value = std::strtof(text.c_str(), &end);
        if (end == text.c_str() || end == nullptr || end[0] != '\0' ||
            !std::isfinite(value))
        {
            if (showError)
            {
                MessageBoxW(
                    state.window,
                    L"Every calibration value must be a finite number.",
                    L"Invalid calibration",
                    MB_OK | MB_ICONWARNING);
            }
            return false;
        }
        if (index < 3u)
        {
            if (metric)
            {
                value /= 2.54f;
            }
            if (std::abs(value) > vwp::kMaximumOffsetInches)
            {
                if (showError)
                {
                    MessageBoxW(
                        state.window,
                        metric
                            ? L"Position corrections must stay between -30.48 and +30.48 cm."
                            : L"Position corrections must stay between -12 and +12 inches.",
                        L"Calibration outside safe range",
                        MB_OK | MB_ICONWARNING);
                }
                return false;
            }
            parsed.offset[index] = value;
        }
        else
        {
            if (std::abs(value) > vwp::kMaximumAngleDegrees)
            {
                if (showError)
                {
                    MessageBoxW(
                        state.window,
                        L"Rotation corrections must stay between -90 and +90 degrees.",
                        L"Calibration outside safe range",
                        MB_OK | MB_ICONWARNING);
                }
                return false;
            }
            parsed.angles[index - 3u] = value;
        }
    }
    *pose = parsed;

    const std::string requestedName = ToUtf8(
        WindowText(GetDlgItem(state.window, kIdWeaponEditName)));
    if (requestedName.empty() || requestedName.size() > 96u)
    {
        if (showError)
        {
            MessageBoxW(
                state.window,
                L"Profile name must contain 1 through 96 characters.",
                L"Invalid profile name",
                MB_OK | MB_ICONWARNING);
        }
        return false;
    }
    const bool enabled = SendMessageW(
        state.enabled,
        BM_GETCHECK,
        0,
        0) == BST_CHECKED;
    if (state.layer == WeaponEditorLayer::Gunstock)
    {
        vwp::GunstockProfile* const profile = SelectedGunstockProfile(state);
        profile->name = requestedName;
        profile->enabled = enabled;
    }
    else
    {
        vwp::WeaponProfile* const profile = SelectedWeaponProfile(state);
        profile->name = requestedName;
        profile->enabled = enabled;
    }
    return true;
}

bool SaveWeaponEditorDocument(WeaponEditorState& state)
{
    if (!CommitWeaponEditorLayer(state, true))
    {
        return false;
    }
    if (!ConfirmReplaceInvalidWeaponProfiles(*state.app))
    {
        return false;
    }
    std::string error;
    if (!SaveWeaponProfiles(
            state.app->weaponProfilesPath,
            state.document,
            &error))
    {
        MessageBoxW(
            state.window,
            ToWide(error).c_str(),
            L"Weapon profile save failed",
            MB_OK | MB_ICONERROR);
        return false;
    }
    state.app->weaponProfiles = state.document;
    state.app->weaponProfilesLoadError.clear();
    return true;
}

void SelectWeaponFromCombo(WeaponEditorState& state)
{
    const LRESULT selected = SendMessageW(
        state.weaponCombo,
        CB_GETCURSEL,
        0,
        0);
    const LRESULT index = selected >= 0
        ? SendMessageW(state.weaponCombo, CB_GETITEMDATA, selected, 0)
        : CB_ERR;
    if (index != CB_ERR && index >= 0 &&
        static_cast<std::size_t>(index) < state.document.weapons.size())
    {
        state.selectedWeaponId =
            state.document.weapons[static_cast<std::size_t>(index)].id;
    }
}

void SelectGunstockFromCombo(WeaponEditorState& state)
{
    const LRESULT selected = SendMessageW(
        state.gunstockCombo,
        CB_GETCURSEL,
        0,
        0);
    const LRESULT index = selected >= 0
        ? SendMessageW(state.gunstockCombo, CB_GETITEMDATA, selected, 0)
        : CB_ERR;
    if (index != CB_ERR && index >= 0 &&
        static_cast<std::size_t>(index) < state.document.gunstocks.size())
    {
        state.selectedGunstockId =
            state.document.gunstocks[static_cast<std::size_t>(index)].id;
        state.document.activeGunstockId = state.selectedGunstockId;
    }
}

void UseCurrentWeaponInEditor(WeaponEditorState& state)
{
    UpdateWeaponStatusLabel(*state.app);
    const vwp::RuntimeStatus& runtime = state.app->weaponRuntimeStatus;
    if (runtime.weaponId.empty())
    {
        MessageBoxW(
            state.window,
            L"No active weapon has been published yet. Enter a mission, equip the desired weapon, and retry.",
            L"Current weapon unavailable",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    CommitWeaponEditorLayer(state, false);
    state.selectedWeaponId = runtime.weaponId;
    if (vwp::FindWeapon(state.document, runtime.weaponId) == nullptr)
    {
        if (state.document.weapons.size() >= vwp::kMaximumWeaponProfiles)
        {
            MessageBoxW(
                state.window,
                L"The guarded limit of 128 weapon profiles has been reached.",
                L"Cannot add weapon",
                MB_OK | MB_ICONWARNING);
            return;
        }
        vwp::WeaponProfile profile;
        profile.id = runtime.weaponId;
        profile.name = runtime.weaponName.empty()
            ? runtime.weaponId
            : runtime.weaponName;
        state.document.weapons.push_back(profile);
    }
    PopulateWeaponEditorCombos(state);
    LoadWeaponEditorLayer(state);
}

std::string NewGunstockId(const vwp::Document& document)
{
    for (int index = 1; index <= 999; ++index)
    {
        const std::string id = "custom_stock_" + std::to_string(index);
        if (vwp::FindGunstock(document, id) == nullptr)
        {
            return id;
        }
    }
    return {};
}

void AddGunstockInEditor(WeaponEditorState& state)
{
    if (!CommitWeaponEditorLayer(state, false) ||
        state.document.gunstocks.size() >= vwp::kMaximumGunstockProfiles)
    {
        MessageBoxW(
            state.window,
            L"The current values are invalid or the 32-profile gunstock limit has been reached.",
            L"Cannot add gunstock",
            MB_OK | MB_ICONWARNING);
        return;
    }
    vwp::GunstockProfile profile;
    profile.id = NewGunstockId(state.document);
    profile.name = "Custom gunstock " +
        std::to_string(state.document.gunstocks.size());
    state.document.gunstocks.push_back(profile);
    state.selectedGunstockId = profile.id;
    state.document.activeGunstockId = profile.id;
    state.layer = WeaponEditorLayer::Gunstock;
    SendMessageW(state.layerCombo, CB_SETCURSEL, 2, 0);
    PopulateWeaponEditorCombos(state);
    LoadWeaponEditorLayer(state);
}

std::filesystem::path ChooseGunstockFile(
    HWND owner,
    const bool save,
    const wchar_t* const title)
{
    std::array<wchar_t, 32768> path = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter =
        L"KisakCOD VR gunstock profile (*.vrstock)\0*.vrstock\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrTitle = title;
    dialog.lpstrDefExt = L"vrstock";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
        (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL accepted = save
        ? GetSaveFileNameW(&dialog)
        : GetOpenFileNameW(&dialog);
    return accepted
        ? std::filesystem::path(path.data())
        : std::filesystem::path();
}

void ImportGunstockInEditor(WeaponEditorState& state)
{
    if (!CommitWeaponEditorLayer(state, true))
    {
        return;
    }
    const std::filesystem::path selected = ChooseGunstockFile(
        state.window,
        false,
        L"Import gunstock calibration");
    if (selected.empty())
    {
        return;
    }
    vwp::GunstockProfile imported;
    std::string error;
    if (!vwp::ParseGunstock(ReadTextFile(selected), &imported, &error))
    {
        MessageBoxW(
            state.window,
            ToWide(error).c_str(),
            L"Gunstock import failed",
            MB_OK | MB_ICONWARNING);
        return;
    }
    vwp::GunstockProfile* existing =
        vwp::FindGunstock(state.document, imported.id);
    if (existing != nullptr)
    {
        if (MessageBoxW(
                state.window,
                L"A gunstock with this id already exists. Replace it?",
                L"Replace gunstock profile",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        {
            return;
        }
        *existing = imported;
    }
    else
    {
        if (state.document.gunstocks.size() >=
            vwp::kMaximumGunstockProfiles)
        {
            MessageBoxW(
                state.window,
                L"The guarded limit of 32 gunstock profiles has been reached. Delete an unused profile before importing another.",
                L"Gunstock import limit reached",
                MB_OK | MB_ICONWARNING);
            return;
        }
        state.document.gunstocks.push_back(imported);
    }
    state.selectedGunstockId = imported.id;
    state.document.activeGunstockId = imported.id;
    state.layer = WeaponEditorLayer::Gunstock;
    SendMessageW(state.layerCombo, CB_SETCURSEL, 2, 0);
    PopulateWeaponEditorCombos(state);
    LoadWeaponEditorLayer(state);
}

void ExportGunstockFromEditor(WeaponEditorState& state)
{
    if (!CommitWeaponEditorLayer(state, true))
    {
        return;
    }
    const vwp::GunstockProfile* const profile =
        SelectedGunstockProfile(state);
    if (profile == nullptr)
    {
        return;
    }
    const std::filesystem::path selected = ChooseGunstockFile(
        state.window,
        true,
        L"Export shareable gunstock calibration");
    if (selected.empty())
    {
        return;
    }
    std::string error;
    if (!WriteTextFileAtomic(
            selected,
            vwp::SerializeGunstock(*profile),
            &error))
    {
        MessageBoxW(
            state.window,
            ToWide(error).c_str(),
            L"Gunstock export failed",
            MB_OK | MB_ICONERROR);
    }
}

bool ApplyWeaponEditorLive(WeaponEditorState& state)
{
    if (!SaveWeaponEditorDocument(state))
    {
        return false;
    }
    if (!IsProcessRunning(L"KisakCOD-sp.exe"))
    {
        SetWindowTextW(
            state.status,
            L"Saved for the next launch. Start a mission to use Apply live or guided capture.");
        return true;
    }
    vwp::Request request;
    request.command = vwp::Command::Reload;
    request.weaponId = state.selectedWeaponId;
    request.gunstockId = state.selectedGunstockId;
    vwp::RuntimeStatus response;
    if (!SendWeaponCalibrationRequest(*state.app, request, &response))
    {
        return false;
    }
    if (response.status != "reloaded")
    {
        MessageBoxW(
            state.window,
            ToWide(response.message.empty()
                ? "The running game rejected the weapon profile reload."
                : response.message).c_str(),
            L"Live weapon calibration rejected",
            MB_OK | MB_ICONWARNING);
        return false;
    }
    state.runtimeStatus = response;
    SetWindowTextW(
        state.status,
        L"Applied live. Move between hip fire and two-hand/ADS to verify the smooth blend.");
    return true;
}

void CaptureWeaponEditorAim(WeaponEditorState& state)
{
    if (state.layer == WeaponEditorLayer::WeaponHip)
    {
        MessageBoxW(
            state.window,
            L"Guided aim capture belongs to the Gunstock or Per-weapon shouldered/ADS layer. Select one of those layers first.",
            L"Choose a shouldered layer",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (state.selectedWeaponId.empty() ||
        !SaveWeaponEditorDocument(state))
    {
        return;
    }
    UpdateWeaponStatusLabel(*state.app);
    if (state.app->weaponRuntimeStatus.weaponId != state.selectedWeaponId)
    {
        MessageBoxW(
            state.window,
            L"The selected profile does not match the weapon currently equipped in COD4. Equip the selected weapon and retry.",
            L"Equip the selected weapon",
            MB_OK | MB_ICONWARNING);
        return;
    }
    if (MessageBoxW(
            state.window,
            L"Shoulder the physical stock in your normal position and aim it at a fixed point directly ahead.\r\n\r\nAfter you press OK, the configurator gives you five seconds to put the headset on and hold perfectly still. It then captures the controller-to-bore rotation; position remains available for live fine adjustment.",
            L"Guided gunstock aim capture",
            MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
    {
        return;
    }

    ShowWindow(state.window, SW_MINIMIZE);
    const ULONGLONG captureAt = GetTickCount64() + 5000u;
    while (GetTickCount64() < captureAt)
    {
        Sleep(50u);
    }

    vwp::Request request;
    request.command = vwp::Command::CaptureAim;
    request.target = state.layer == WeaponEditorLayer::Gunstock
        ? vwp::CaptureTarget::Gunstock
        : vwp::CaptureTarget::WeaponShouldered;
    request.weaponId = state.selectedWeaponId;
    request.gunstockId = state.selectedGunstockId;
    vwp::RuntimeStatus response;
    const bool captured = SendWeaponCalibrationRequest(
        *state.app,
        request,
        &response);
    ShowWindow(state.window, SW_RESTORE);
    SetForegroundWindow(state.window);
    if (!captured || response.status != "captured" ||
        !response.capturedAnglesValid)
    {
        MessageBoxW(
            state.window,
            ToWide(response.message.empty()
                ? "The runtime could not capture a valid tracked weapon pose."
                : response.message).c_str(),
            L"Gunstock capture failed",
            MB_OK | MB_ICONWARNING);
        return;
    }

    const double globalAngles[3] = {
        NumberValue(state.app->values, "KISAK_VR_WEAPON_PITCH", 0.0),
        NumberValue(state.app->values, "KISAK_VR_WEAPON_YAW", 0.0),
        NumberValue(state.app->values, "KISAK_VR_WEAPON_ROLL", 0.0),
    };
    vwp::WeaponProfile* const weapon = SelectedWeaponProfile(state);
    vwp::GunstockProfile* const stock = SelectedGunstockProfile(state);
    if (weapon == nullptr || stock == nullptr)
    {
        return;
    }
    for (std::size_t component = 0u; component < 3u; ++component)
    {
        const float capturedEffective =
            response.capturedEffectiveAngles[component];
        if (state.layer == WeaponEditorLayer::Gunstock)
        {
            stock->shouldered.angles[component] = capturedEffective -
                static_cast<float>(globalAngles[component]) -
                weapon->hip.angles[component] -
                weapon->shouldered.angles[component];
        }
        else
        {
            weapon->shouldered.angles[component] = capturedEffective -
                static_cast<float>(globalAngles[component]) -
                weapon->hip.angles[component] -
                stock->shouldered.angles[component];
        }
    }
    LoadWeaponEditorLayer(state);
    if (ApplyWeaponEditorLive(state))
    {
        MessageBoxW(
            state.window,
            L"The shouldered aim rotation was captured, saved, and applied live. Fine-tune Forward/Left/Up if the virtual sights need positional adjustment, then test both hip fire and two-hand/ADS.",
            L"Gunstock calibration applied",
            MB_OK | MB_ICONINFORMATION);
    }
}

void DeleteWeaponInEditor(WeaponEditorState& state)
{
    if (state.selectedWeaponId.empty() ||
        MessageBoxW(
            state.window,
            L"Delete this weapon-specific override? The global baseline will still apply.",
            L"Delete weapon override",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }
    std::erase_if(
        state.document.weapons,
        [&state](const vwp::WeaponProfile& profile)
        {
            return profile.id == state.selectedWeaponId;
        });
    state.selectedWeaponId.clear();
    PopulateWeaponEditorCombos(state);
    LoadWeaponEditorLayer(state);
}

void DeleteGunstockInEditor(WeaponEditorState& state)
{
    if (state.document.gunstocks.size() <= 1u)
    {
        MessageBoxW(
            state.window,
            L"At least one gunstock profile must remain.",
            L"Cannot delete gunstock",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (MessageBoxW(
            state.window,
            L"Delete the selected gunstock profile?",
            L"Delete gunstock",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }
    std::erase_if(
        state.document.gunstocks,
        [&state](const vwp::GunstockProfile& profile)
        {
            return profile.id == state.selectedGunstockId;
        });
    state.selectedGunstockId = state.document.gunstocks.front().id;
    state.document.activeGunstockId = state.selectedGunstockId;
    PopulateWeaponEditorCombos(state);
    LoadWeaponEditorLayer(state);
}

LRESULT CALLBACK WeaponEditorWindowProc(
    HWND window,
    const UINT message,
    const WPARAM wParam,
    const LPARAM lParam)
{
    WeaponEditorState* state = reinterpret_cast<WeaponEditorState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<WeaponEditorState*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }
    if (state == nullptr)
    {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == WM_COMMAND)
    {
        const int identifier = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (state->suppressEvents)
        {
            return 0;
        }
        if (identifier == kIdWeaponEditWeapon &&
            notification == CBN_SELCHANGE)
        {
            CommitWeaponEditorLayer(*state, false);
            SelectWeaponFromCombo(*state);
            LoadWeaponEditorLayer(*state);
            return 0;
        }
        if (identifier == kIdWeaponEditGunstock &&
            notification == CBN_SELCHANGE)
        {
            CommitWeaponEditorLayer(*state, false);
            SelectGunstockFromCombo(*state);
            LoadWeaponEditorLayer(*state);
            return 0;
        }
        if (identifier == kIdWeaponEditLayer &&
            notification == CBN_SELCHANGE)
        {
            CommitWeaponEditorLayer(*state, false);
            const LRESULT selected = SendMessageW(
                state->layerCombo,
                CB_GETCURSEL,
                0,
                0);
            state->layer = selected == 2
                ? WeaponEditorLayer::Gunstock
                : (selected == 1
                    ? WeaponEditorLayer::WeaponShouldered
                    : WeaponEditorLayer::WeaponHip);
            LoadWeaponEditorLayer(*state);
            return 0;
        }
        switch (identifier)
        {
        case kIdWeaponEditUseCurrent:
            UseCurrentWeaponInEditor(*state);
            return 0;
        case kIdWeaponEditDeleteWeapon:
            DeleteWeaponInEditor(*state);
            return 0;
        case kIdWeaponEditNewGunstock:
            AddGunstockInEditor(*state);
            return 0;
        case kIdWeaponEditDeleteGunstock:
            DeleteGunstockInEditor(*state);
            return 0;
        case kIdWeaponEditImportGunstock:
            ImportGunstockInEditor(*state);
            return 0;
        case kIdWeaponEditExportGunstock:
            ExportGunstockFromEditor(*state);
            return 0;
        case kIdWeaponEditReset:
            if (vwp::Pose* const pose = SelectedWeaponEditorPose(*state))
            {
                *pose = {};
                LoadWeaponEditorLayer(*state);
            }
            return 0;
        case kIdWeaponEditApplyLive:
            ApplyWeaponEditorLive(*state);
            return 0;
        case kIdWeaponEditCapture:
            CaptureWeaponEditorAim(*state);
            return 0;
        case IDOK:
            if (SaveWeaponEditorDocument(*state))
            {
                state->accepted = true;
                DestroyWindow(window);
            }
            return 0;
        case IDCANCEL:
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
    }
    else if (message == WM_CLOSE)
    {
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void OpenWeaponCalibrationEditor(AppState& app)
{
    WeaponEditorState editor;
    editor.app = &app;
    editor.document = app.weaponProfiles;
    editor.runtimeStatus = app.weaponRuntimeStatus;
    editor.selectedGunstockId = editor.document.activeGunstockId;
    if (!editor.runtimeStatus.weaponId.empty())
    {
        editor.selectedWeaponId = editor.runtimeStatus.weaponId;
    }

    editor.window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kWeaponEditorClass,
        L"Per-Weapon & Physical Gunstock Calibration",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        930,
        660,
        app.window,
        nullptr,
        app.instance,
        &editor);
    if (editor.window == nullptr)
    {
        MessageBoxW(
            app.window,
            L"Windows could not open the weapon calibration editor.",
            L"Weapon editor failed",
            MB_OK | MB_ICONERROR);
        return;
    }

    CreateWeaponEditorControl(
        editor,
        L"STATIC",
        L"Global Weapons & Hands values remain the baseline. Add only the difference needed for each weapon and physical stock.",
        SS_LEFT,
        20,
        16,
        874,
        38);
    CreateWeaponEditorControl(editor, L"STATIC", L"Weapon override", SS_LEFT, 20, 64, 160, 20);
    editor.weaponCombo = CreateWeaponEditorControl(
        editor, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        20, 86, 410, 260, kIdWeaponEditWeapon, WS_EX_CLIENTEDGE);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"Use equipped weapon", BS_PUSHBUTTON | WS_TABSTOP,
        442, 86, 190, 30, kIdWeaponEditUseCurrent);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"Delete override", BS_PUSHBUTTON | WS_TABSTOP,
        644, 86, 130, 30, kIdWeaponEditDeleteWeapon);

    CreateWeaponEditorControl(editor, L"STATIC", L"Active gunstock profile", SS_LEFT, 20, 128, 180, 20);
    editor.gunstockCombo = CreateWeaponEditorControl(
        editor, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        20, 150, 410, 260, kIdWeaponEditGunstock, WS_EX_CLIENTEDGE);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"New", BS_PUSHBUTTON | WS_TABSTOP,
        442, 150, 74, 30, kIdWeaponEditNewGunstock);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"Delete", BS_PUSHBUTTON | WS_TABSTOP,
        524, 150, 74, 30, kIdWeaponEditDeleteGunstock);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"Import .vrstock", BS_PUSHBUTTON | WS_TABSTOP,
        606, 150, 134, 30, kIdWeaponEditImportGunstock);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"Export", BS_PUSHBUTTON | WS_TABSTOP,
        748, 150, 92, 30, kIdWeaponEditExportGunstock);

    CreateWeaponEditorControl(editor, L"STATIC", L"Calibration layer", SS_LEFT, 20, 196, 160, 20);
    editor.layerCombo = CreateWeaponEditorControl(
        editor, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
        20, 218, 410, 180, kIdWeaponEditLayer, WS_EX_CLIENTEDGE);
    const wchar_t* const layers[] = {
        L"Per-weapon hip-fire override",
        L"Per-weapon shouldered / ADS override",
        L"Active physical gunstock mount profile",
    };
    for (const wchar_t* const layer : layers)
    {
        SendMessageW(
            editor.layerCombo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(layer));
    }
    SendMessageW(editor.layerCombo, CB_SETCURSEL, 0, 0);
    editor.enabled = CreateWeaponEditorControl(
        editor, L"BUTTON", L"Enable selected profile", BS_AUTOCHECKBOX | WS_TABSTOP,
        452, 220, 210, 24, kIdWeaponEditEnabled);

    CreateWeaponEditorControl(editor, L"STATIC", L"Profile display name", SS_LEFT, 20, 260, 180, 20);
    CreateWeaponEditorControl(
        editor, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP,
        20, 282, 410, 28, kIdWeaponEditName, WS_EX_CLIENTEDGE);

    const wchar_t* const labels[6] = {
        L"Forward", L"Left", L"Up", L"Pitch (deg)", L"Yaw (deg)", L"Roll (deg)",
    };
    for (int index = 0; index < 6; ++index)
    {
        const int column = index % 3;
        const int row = index / 3;
        const int x = 20 + column * 288;
        const int y = 334 + row * 76;
        CreateWeaponEditorControl(editor, L"STATIC", labels[index], SS_LEFT, x, y, 260, 20);
        editor.valueEdits[static_cast<std::size_t>(index)] =
            CreateWeaponEditorControl(
                editor, L"EDIT", L"0", ES_AUTOHSCROLL | WS_TABSTOP,
                x, y + 22, 260, 28, kIdWeaponEditValueBase + index,
                WS_EX_CLIENTEDGE);
    }

    editor.status = CreateWeaponEditorControl(
        editor, L"STATIC", L"", SS_LEFT,
        20, 492, 874, 44);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"Reset selected layer", BS_PUSHBUTTON | WS_TABSTOP,
        20, 548, 180, 34, kIdWeaponEditReset);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"Apply live", BS_PUSHBUTTON | WS_TABSTOP,
        212, 548, 150, 34, kIdWeaponEditApplyLive);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"Guided aim capture", BS_PUSHBUTTON | WS_TABSTOP,
        374, 548, 190, 34, kIdWeaponEditCapture);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP,
        676, 548, 100, 34, IDCANCEL);
    CreateWeaponEditorControl(
        editor, L"BUTTON", L"Save && Close", BS_DEFPUSHBUTTON | WS_TABSTOP,
        786, 548, 108, 34, IDOK);

    PopulateWeaponEditorCombos(editor);
    LoadWeaponEditorLayer(editor);

    RECT parentRect = {};
    GetWindowRect(app.window, &parentRect);
    SetWindowPos(
        editor.window,
        HWND_TOP,
        parentRect.left + ((parentRect.right - parentRect.left) - 930) / 2,
        parentRect.top + ((parentRect.bottom - parentRect.top) - 660) / 2,
        0,
        0,
        SWP_NOSIZE);
    EnableWindow(app.window, FALSE);
    ShowWindow(editor.window, SW_SHOW);
    UpdateWindow(editor.window);

    MSG message = {};
    bool sawQuit = false;
    WPARAM quitCode = 0;
    while (IsWindow(editor.window))
    {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0)
        {
            sawQuit = result == 0;
            quitCode = message.wParam;
            break;
        }
        if (!IsDialogMessageW(editor.window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(app.window, TRUE);
    SetForegroundWindow(app.window);
    if (sawQuit)
    {
        PostQuitMessage(static_cast<int>(quitCode));
        return;
    }
    if (editor.accepted)
    {
        app.weaponProfiles = editor.document;
        UpdateWeaponStatusLabel(app);
    }
}

void ShowWeaponPanel(
    AppState& state,
    const bool show)
{
    for (HWND control : state.weaponControls)
    {
        ShowWindow(control, show ? SW_SHOW : SW_HIDE);
    }
}

void OpenWeaponCalibrationEditor(AppState& state);

void BuildWeaponPanel(AppState& state)
{
    const auto add = [&](HWND control)
    {
        state.weaponControls.push_back(control);
        return control;
    };

    add(CreateControl(
        state,
        L"STATIC",
        L"Per-weapon & physical gunstock calibration",
        SS_LEFT,
        kTabLeft + 20,
        kTabTop + 48,
        kTabWidth - 40,
        22));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Open calibration editor",
        BS_DEFPUSHBUTTON | WS_TABSTOP,
        kTabLeft + 20,
        kTabTop + 74,
        326,
        34,
        kIdWeaponEditor));

    add(CreateControl(
        state,
        L"BUTTON",
        L"Refresh equipped weapon",
        BS_PUSHBUTTON | WS_TABSTOP,
        kTabLeft + 358,
        kTabTop + 74,
        342,
        34,
        kIdWeaponRefresh));

    state.weaponStatus = add(CreateControl(
        state,
        L"STATIC",
        L"Launch a mission, then Refresh to select the equipped weapon automatically.",
        SS_LEFT | SS_PATHELLIPSIS,
        kTabLeft + 20,
        kTabTop + 114,
        kTabWidth - 40,
        40));
}

bool BuildMainWindow(AppState& state)
{
    state.font = CreateFontW(
        -16,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    state.titleFont = CreateFontW(
        -27,
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    state.backgroundBrush = CreateSolidBrush(RGB(246, 248, 251));
    state.previewBrush = CreateSolidBrush(RGB(255, 255, 255));

    state.window = CreateWindowExW(
        0,
        kWindowClass,
        kWindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        state.instance,
        &state);
    if (state.window == nullptr)
    {
        return false;
    }

    HWND title = CreateControl(
        state,
        L"STATIC",
        L"KisakCOD VR Configurator",
        SS_LEFT,
        20,
        14,
        450,
        36);
    SetFont(title, state.titleFont);

    CreateControl(
        state,
        L"STATIC",
        L"Beta.11 HUD recovery, recenter, VR prompts, and F.N.G. repair",
        SS_LEFT,
        22,
        52,
        550,
        22);

    CreateControl(
        state,
        L"STATIC",
        L"Preset:",
        SS_LEFT,
        610,
        18,
        56,
        22);

    state.preset = CreateControl(
        state,
        WC_COMBOBOXW,
        L"",
        CBS_DROPDOWNLIST | WS_TABSTOP,
        668,
        14,
        220,
        240,
        kIdPreset,
        WS_EX_CLIENTEDGE);
    for (const std::string& preset : kc::PresetNames())
    {
        const std::wstring name = ToWide(preset);
        SendMessageW(
            state.preset,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(name.c_str()));
    }
    SendMessageW(state.preset, CB_SETCURSEL, 0, 0);

    CreateControl(
        state,
        L"BUTTON",
        L"Apply",
        BS_PUSHBUTTON | WS_TABSTOP,
        896,
        14,
        76,
        28,
        kIdApplyPreset);

    state.advanced = CreateControl(
        state,
        L"BUTTON",
        L"Show advanced settings",
        BS_AUTOCHECKBOX | WS_TABSTOP,
        986,
        16,
        150,
        25,
        kIdAdvanced);

    state.tabs = CreateControl(
        state,
        WC_TABCONTROLW,
        L"",
        WS_TABSTOP | WS_CLIPSIBLINGS,
        kTabLeft,
        kTabTop,
        kTabWidth,
        kTabHeight,
        kIdTabs);

    for (int index = 0; index < static_cast<int>(kPageNames.size()); ++index)
    {
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(kPageNames[static_cast<std::size_t>(index)]);
        TabCtrl_InsertItem(state.tabs, index, &item);
    }

    state.tooltip = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        state.window,
        nullptr,
        state.instance,
        nullptr);
    SendMessageW(state.tooltip, TTM_SETMAXTIPWIDTH, 0, 440);
    SendMessageW(state.tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);

    BuildSettingControls(state);
    BuildSetupPanel(state);
    BuildControllerBindingEditor(state);
    BuildCalibrationPanel(state);
    BuildHudPanel(state);
    BuildWeaponPanel(state);

    state.preview = CreateWindowExW(
        0,
        kPreviewClass,
        L"",
        WS_CHILD | WS_VISIBLE,
        kPreviewLeft,
        kPreviewTop,
        kPreviewWidth,
        kPreviewHeight,
        state.window,
        nullptr,
        state.instance,
        &state);

    state.hint = CreateControl(
        state,
        L"STATIC",
        L"Hover over or focus a setting to see what it changes.",
        SS_LEFT,
        kPreviewLeft + 4,
        kPreviewTop + kPreviewHeight + 14,
        kPreviewWidth - 8,
        72);

    state.settingsPath = CreateControl(
        state,
        L"STATIC",
        L"Active settings: loading...",
        SS_LEFT | SS_PATHELLIPSIS,
        20,
        78,
        1115,
        22);

    state.status = CreateControl(
        state,
        L"STATIC",
        L"Loading settings...",
        SS_LEFT | SS_PATHELLIPSIS,
        20,
        710,
        690,
        24);

    const int buttonY = 704;
    CreateControl(state, L"BUTTON", L"Restore defaults", BS_PUSHBUTTON | WS_TABSTOP,
                  720, buttonY, 108, 30, kIdRestoreDefaults);
    CreateControl(state, L"BUTTON", L"Import", BS_PUSHBUTTON | WS_TABSTOP,
                  834, buttonY, 68, 30, kIdImport);
    CreateControl(state, L"BUTTON", L"Export", BS_PUSHBUTTON | WS_TABSTOP,
                  908, buttonY, 68, 30, kIdExport);
    CreateControl(state, L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP,
                  982, buttonY, 68, 30, kIdSave);
    CreateControl(state, L"BUTTON", L"Save && Launch", BS_PUSHBUTTON | WS_TABSTOP,
                  1056, buttonY, 84, 30, kIdSaveLaunch);

    CreateControl(
        state,
        L"BUTTON",
        L"Save && Launch Diagnostics",
        BS_PUSHBUTTON | WS_TABSTOP,
        kPreviewLeft,
        kPreviewTop + kPreviewHeight + 87,
        kPreviewWidth,
        31,
        kIdDiagnostics);

    UpdateAllControls(state);
    RefreshCompatibility(state, false);
    UpdateWeaponStatusLabel(state);
    SetTimer(state.window, kIdWeaponPollTimer, 1000u, nullptr);
    SelectPage(state, 0);
    UpdateValidation(state);
    return true;
}

LRESULT CALLBACK MainWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    AppState* state = reinterpret_cast<AppState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<AppState*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state));
    }

    if (state == nullptr)
    {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message)
    {
    case WM_COMMAND:
    {
        const int identifier = LOWORD(wParam);
        const int notification = HIWORD(wParam);

        if (identifier == kIdBindingActionList &&
            notification == LBN_SELCHANGE)
        {
            UpdateControllerBindingEditor(*state);
            return 0;
        }

        if (identifier == kIdBindingPrimary &&
            notification == CBN_SELCHANGE)
        {
            OnControllerBindingChoice(*state, false);
            return 0;
        }

        if (identifier == kIdBindingAlternate &&
            notification == CBN_SELCHANGE)
        {
            OnControllerBindingChoice(*state, true);
            return 0;
        }

        if (identifier >= kIdSettingBase &&
            identifier < kIdSettingBase + static_cast<int>(state->bindings.size()))
        {
            if (notification == EN_CHANGE || notification == CBN_SELCHANGE)
            {
                OnSettingChanged(*state, identifier);
            }
            if (notification == EN_SETFOCUS || notification == CBN_SETFOCUS)
            {
                UpdateHint(*state, identifier);
            }
            return 0;
        }

        switch (identifier)
        {
        case kIdSetupRescan:
            RefreshCompatibility(*state, true);
            return 0;
        case kIdSetupApplyRecommended:
            ApplyRecommendedCompatibility(*state);
            return 0;
        case kIdSetupCopyReport:
            RefreshCompatibility(*state, true);
            CopyCompatibilityReport(*state);
            return 0;
        case kIdSetupOpenReport:
            OpenCompatibilityReport(*state);
            return 0;
        case kIdApplyPreset:
            ApplySelectedPreset(*state);
            return 0;
        case kIdAdvanced:
            state->advancedMode =
                SendMessageW(state->advanced, BM_GETCHECK, 0, 0) == BST_CHECKED;
            LayoutSettings(*state);
            return 0;
        case kIdRestoreDefaults:
            RestoreDefaults(*state);
            return 0;
        case kIdImport:
            ImportSettings(*state);
            return 0;
        case kIdExport:
            ExportSettings(*state);
            return 0;
        case kIdSave:
            SaveActiveSettings(*state, false);
            return 0;
        case kIdSaveLaunch:
            if (SaveActiveSettings(*state, true))
            {
                LaunchBatch(*state, L"Launch-KisakCOD-VR.bat");
            }
            return 0;
        case kIdDiagnostics:
            if (SaveActiveSettings(*state, true))
            {
                LaunchBatch(*state, L"Launch-KisakCOD-VR-Diagnostics.bat");
            }
            return 0;
        case kIdClearBinding:
            ClearControllerAlternateBinding(*state);
            return 0;
        case kIdCapturePrimary:
            CaptureControllerBinding(*state, false);
            return 0;
        case kIdCaptureAlternate:
            CaptureControllerBinding(*state, true);
            return 0;
        case kIdChordPrimary:
            EditControllerChord(*state, false);
            return 0;
        case kIdChordAlternate:
            EditControllerChord(*state, true);
            return 0;
        case kIdCalibrationRecenterPosition:
            RecenterPositionCalibration(*state);
            return 0;
        case kIdCalibrationRecenterDirectionLevel:
            RecenterDirectionLevelCalibration(*state);
            return 0;
        case kIdCalibrationRecenterFull:
            RecenterFullCalibration(*state);
            return 0;
        case kIdCalibrationMeasureStanding:
            MeasureStandingCalibration(*state);
            return 0;
        case kIdCalibrationApplySeated:
            ApplySeatedCalibration(*state);
            return 0;
        case kIdCalibrationShorter:
            AdjustCalibrationHeight(*state, -1.0, false);
            return 0;
        case kIdCalibrationResetHeight:
            AdjustCalibrationHeight(*state, 0.0, true);
            return 0;
        case kIdCalibrationTaller:
            AdjustCalibrationHeight(*state, 1.0, false);
            return 0;
        case kIdHudVisualEditor:
            OpenVisualHudEditor(*state);
            return 0;
        case kIdHudHeadsetEditor:
            StartHeadsetHudEditor(*state);
            return 0;
        case kIdWeaponEditor:
            OpenWeaponCalibrationEditor(*state);
            return 0;
        case kIdWeaponRefresh:
            UpdateWeaponStatusLabel(*state);
            return 0;
        default:
            break;
        }
        break;
    }
    case WM_TIMER:
        if (wParam == static_cast<WPARAM>(kIdHudPollTimer))
        {
            PollHudEditorStatus(*state);
            return 0;
        }
        if (wParam == static_cast<WPARAM>(kIdWeaponPollTimer))
        {
            if (state->selectedPage == PageIndex(kc::SettingPage::Weapons))
            {
                UpdateWeaponStatusLabel(*state);
            }
            return 0;
        }
        break;
    case WM_NOTIFY:
    {
        const auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header != nullptr && header->idFrom == kIdTabs &&
            header->code == TCN_SELCHANGE)
        {
            SelectPage(*state, TabCtrl_GetCurSel(state->tabs));
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<INT_PTR>(state->backgroundBrush);
    case WM_ERASEBKGND:
    {
        RECT client = {};
        GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
        return 1;
    }
    case WM_CLOSE:
        if (state->dirty &&
            MessageBoxW(
                window,
                L"You have unsaved VR settings. Close without saving?",
                L"KisakCOD VR Configurator",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        {
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, kIdHudPollTimer);
        KillTimer(window, kIdWeaponPollTimer);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

int ValidateFromCommandLine(const std::filesystem::path& path)
{
    const kc::LoadResult result = kc::LoadSettings(
        ExecutableDirectory() / L"VR-Settings.bat",
        path);
    const auto error = std::find_if(
        result.messages.begin(),
        result.messages.end(),
        [](const kc::ValidationMessage& message)
        {
            return message.severity == kc::ValidationMessage::Severity::Error;
        });
    return error == result.messages.end() ? 0 : 2;
}

int CompatibilityReportFromCommandLine(
    const std::filesystem::path& outputPath)
{
    const auto environmentValue = [](const char* const key, const char* fallback)
    {
        const char* const value = std::getenv(key);
        return value != nullptr && value[0] != '\0'
            ? std::string(value)
            : std::string(fallback);
    };

    const vrc::Probe probe = wc::ProbeSystem(
        ExecutableDirectory(),
        UserRuntimeReceiptPath(),
        environmentValue("KISAK_VR_BACKEND", "auto"),
        environmentValue("VR_CUSTOM_MODE", "6016x2688"),
        environmentValue("KISAK_VR_OUTPUT_SCALE", "1.00"));
    const vrc::Report report = vrc::Evaluate(probe);
    std::string error;
    if (!wc::WriteReportAtomic(
            outputPath,
            vrc::SerializeReport(probe, report, wc::LocalTimestamp()),
            &error))
    {
        return 3;
    }
    return report.readyForLaunch ? 0 : 2;
}

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand)
{
    SetProcessDPIAware();

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(
        GetCommandLineW(),
        &argumentCount);
    if (arguments != nullptr && argumentCount == 3 &&
        _wcsicmp(arguments[1], L"--validate") == 0)
    {
        const int result = ValidateFromCommandLine(arguments[2]);
        LocalFree(arguments);
        return result;
    }
    if (arguments != nullptr && argumentCount == 3 &&
        _wcsicmp(arguments[1], L"--compatibility-report") == 0)
    {
        const int result = CompatibilityReportFromCommandLine(arguments[2]);
        LocalFree(arguments);
        return result;
    }
    if (arguments != nullptr)
    {
        LocalFree(arguments);
    }

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW previewClass = {};
    previewClass.cbSize = sizeof(previewClass);
    previewClass.style = CS_HREDRAW | CS_VREDRAW;
    previewClass.lpfnWndProc = PreviewWindowProc;
    previewClass.hInstance = instance;
    previewClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    previewClass.hbrBackground = nullptr;
    previewClass.lpszClassName = kPreviewClass;
    if (RegisterClassExW(&previewClass) == 0)
    {
        return 1;
    }

    WNDCLASSEXW chordClass = {};
    chordClass.cbSize = sizeof(chordClass);
    chordClass.style = CS_HREDRAW | CS_VREDRAW;
    chordClass.lpfnWndProc = ChordEditorWindowProc;
    chordClass.hInstance = instance;
    chordClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    chordClass.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    chordClass.lpszClassName = kChordEditorClass;
    if (RegisterClassExW(&chordClass) == 0)
    {
        return 1;
    }

    WNDCLASSEXW hudEditorClass = {};
    hudEditorClass.cbSize = sizeof(hudEditorClass);
    hudEditorClass.style = CS_HREDRAW | CS_VREDRAW;
    hudEditorClass.lpfnWndProc = HudEditorWindowProc;
    hudEditorClass.hInstance = instance;
    hudEditorClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    hudEditorClass.hbrBackground = nullptr;
    hudEditorClass.lpszClassName = kHudEditorClass;
    if (RegisterClassExW(&hudEditorClass) == 0)
    {
        return 1;
    }

    WNDCLASSEXW weaponEditorClass = {};
    weaponEditorClass.cbSize = sizeof(weaponEditorClass);
    weaponEditorClass.style = CS_HREDRAW | CS_VREDRAW;
    weaponEditorClass.lpfnWndProc = WeaponEditorWindowProc;
    weaponEditorClass.hInstance = instance;
    weaponEditorClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    weaponEditorClass.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    weaponEditorClass.lpszClassName = kWeaponEditorClass;
    if (RegisterClassExW(&weaponEditorClass) == 0)
    {
        return 1;
    }

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = MainWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0)
    {
        return 1;
    }

    AppState state;
    state.instance = instance;
    state.gameDirectory = ExecutableDirectory();
    state.defaultsPath = state.gameDirectory / L"VR-Settings.bat";
    state.userPath = UserSettingsPath();
    state.weaponProfilesPath = UserWeaponProfilesPath();
    state.weaponCalibrationRequestPath =
        UserWeaponCalibrationRequestPath();
    state.weaponCalibrationStatusPath =
        UserWeaponCalibrationStatusPath();
    state.runtimeReceiptPath = UserRuntimeReceiptPath();
    state.compatibilityReportPath = UserCompatibilityReportPath();
    state.weaponProfiles = LoadWeaponProfiles(
        state.weaponProfilesPath,
        &state.weaponProfilesLoadError);

    const kc::LoadResult loaded = kc::LoadSettings(
        state.defaultsPath,
        state.userPath);
    state.values = loaded.values;
    state.validation = loaded.messages;
    state.profileName = loaded.profileName;
    state.revision = loaded.revision;
    state.activePath = loaded.activePath;

    if (loaded.userFileFound && !loaded.revision.empty())
    {
        const kc::VerificationResult verification =
            kc::VerifyUserSettingsFile(
                state.userPath,
                state.values,
                state.profileName,
                state.revision);
        state.readBackVerified = verification.success;
        state.verifiedSettingCount = verification.verifiedSettingCount;
    }

    if (!BuildMainWindow(state))
    {
        return 1;
    }

    ShowWindow(state.window, showCommand);
    UpdateWindow(state.window);

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(state.window, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    DeleteObject(state.font);
    DeleteObject(state.titleFont);
    DeleteObject(state.backgroundBrush);
    DeleteObject(state.previewBrush);
    return static_cast<int>(message.wParam);
}
