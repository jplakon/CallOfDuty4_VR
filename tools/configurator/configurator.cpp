#include "settings_core.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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

namespace
{

constexpr wchar_t kWindowClass[] = L"KisakCODVrConfiguratorV56";
constexpr wchar_t kPreviewClass[] = L"KisakCODVrPreviewV56";
constexpr wchar_t kWindowTitle[] = L"KisakCOD VR Configurator - V56";

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
constexpr int kIdSettingBase = 2000;

const std::array<kc::SettingPage, 8> kPageOrder = {
    kc::SettingPage::Quick,
    kc::SettingPage::Hud,
    kc::SettingPage::Weapons,
    kc::SettingPage::Interactions,
    kc::SettingPage::Scope,
    kc::SettingPage::Graphics,
    kc::SettingPage::Controls,
    kc::SettingPage::Advanced,
};

const std::array<const wchar_t*, 8> kPageNames = {
    L"Quick Setup",
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

struct ControlBinding
{
    const kc::SettingDefinition* definition = nullptr;
    HWND label = nullptr;
    HWND control = nullptr;
    std::wstring description;
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
    HFONT font = nullptr;
    HFONT titleFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH previewBrush = nullptr;

    std::filesystem::path gameDirectory;
    std::filesystem::path defaultsPath;
    std::filesystem::path userPath;
    kc::SettingsMap values;
    std::vector<ControlBinding> bindings;
    std::vector<kc::ValidationMessage> validation;
    int selectedPage = 0;
    bool advancedMode = false;
    bool dirty = false;
    bool suppressEvents = false;
    std::string profileName = "Custom";
};

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
    const ControlBinding& binding,
    const std::string& value)
{
    const kc::SettingDefinition& definition = *binding.definition;
    if (definition.type == kc::SettingType::Choice ||
        definition.type == kc::SettingType::Toggle)
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
        SetWindowTextW(binding.control, ToWide(value).c_str());
    }
}

std::string ValueFromControl(const ControlBinding& binding)
{
    const kc::SettingDefinition& definition = *binding.definition;
    if (definition.type == kc::SettingType::Choice ||
        definition.type == kc::SettingType::Toggle)
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

    return ToUtf8(WindowText(binding.control));
}

void UpdateAllControls(AppState& state)
{
    state.suppressEvents = true;
    for (const ControlBinding& binding : state.bindings)
    {
        const auto found = state.values.find(binding.definition->key);
        SetControlFromValue(
            binding,
            found == state.values.end()
                ? binding.definition->defaultValue
                : found->second);
    }
    state.suppressEvents = false;
    InvalidateRect(state.preview, nullptr, TRUE);
}

void ReadAllControls(AppState& state)
{
    for (const ControlBinding& binding : state.bindings)
    {
        state.values[binding.definition->key] = ValueFromControl(binding);
    }
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
        status << (state.dirty ? L"Unsaved changes - " : L"")
               << L"All settings are valid.";
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
    InvalidateRect(state.preview, nullptr, TRUE);
}

void LayoutSettings(AppState& state)
{
    std::vector<ControlBinding*> visible;
    for (ControlBinding& binding : state.bindings)
    {
        const bool onPage =
            PageIndex(binding.definition->page) == state.selectedPage;
        const bool permitted =
            state.advancedMode || !binding.definition->advanced;
        const bool show = onPage && permitted;
        ShowWindow(binding.label, show ? SW_SHOW : SW_HIDE);
        ShowWindow(binding.control, show ? SW_SHOW : SW_HIDE);
        if (show)
        {
            visible.push_back(&binding);
        }
    }

    const int pageX = kTabLeft + 18;
    const int pageY = kTabTop + 54;
    const int count = static_cast<int>(visible.size());
    const int columns = count > 8 ? 2 : 1;
    const int rows = columns == 1 ? count : (count + 1) / 2;
    const int columnWidth = columns == 1 ? 520 : 344;
    const int rowHeight = rows > 7 ? 54 : 62;

    for (int index = 0; index < count; ++index)
    {
        const int column = columns == 1 ? 0 : index / rows;
        const int row = columns == 1 ? index : index % rows;
        const int x = pageX + column * columnWidth;
        const int y = pageY + row * rowHeight;
        const int controlWidth = columns == 1 ? 300 : 292;

        MoveWindow(visible[index]->label, x, y, controlWidth, 18, TRUE);
        MoveWindow(
            visible[index]->control,
            x,
            y + 20,
            controlWidth,
            visible[index]->definition->type == kc::SettingType::Choice ||
                    visible[index]->definition->type == kc::SettingType::Toggle
                ? 220
                : 26,
            TRUE);
    }

    InvalidateRect(state.preview, nullptr, TRUE);
}

void SelectPage(AppState& state, const int page)
{
    state.selectedPage = std::clamp(page, 0, 7);
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
    const std::wstring hint =
        ToWide(binding.definition->label) + L"\r\n" + binding.description;
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
    state.values[binding.definition->key] = ValueFromControl(binding);
    state.dirty = true;
    state.profileName = "Custom";
    SynchronizePackedMode(state, binding.definition->key);
    UpdateValidation(state);
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

bool SaveActiveSettings(AppState& state, const bool quiet)
{
    UpdateValidation(state);
    if (HasValidationErrors(state))
    {
        ShowValidationError(state);
        return false;
    }

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
    UpdateValidation(state);

    if (!quiet)
    {
        std::wstring message =
            L"Settings saved successfully.\r\n\r\n" +
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
    state.profileName = "Imported";
    state.dirty = true;
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
        state.profileName = name;
        state.dirty = true;
        UpdateAllControls(state);
        UpdateValidation(state);
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
    state.profileName = "Tested Quest 3";
    state.dirty = true;
    UpdateAllControls(state);
    UpdateValidation(state);
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

    const auto leftLabel = [&](const char* key, const wchar_t* role)
    {
        return std::wstring(role) + L": " + ToWide(StringValue(state.values, key, ""));
    };
    const auto rightLabel = [&](const char* key, const wchar_t* role)
    {
        return std::wstring(role) + L": " + ToWide(StringValue(state.values, key, ""));
    };

    RECT left = {24, 270, 170, 360};
    DrawTextSimple(
        dc,
        leftLabel("KISAK_VR_BIND_USE", L"Use") + L"\r\n" +
            leftLabel("KISAK_VR_BIND_SPRINT", L"Sprint") + L"\r\n" +
            leftLabel("KISAK_VR_BIND_NEXT_WEAPON", L"Weapon"),
        left,
        DT_LEFT | DT_WORDBREAK,
        RGB(57, 68, 84));

    RECT right = {187, 270, 338, 360};
    DrawTextSimple(
        dc,
        rightLabel("KISAK_VR_BIND_RELOAD", L"Reload") + L"\r\n" +
            rightLabel("KISAK_VR_BIND_MELEE", L"Melee") + L"\r\n" +
            rightLabel("KISAK_VR_BIND_STANCE", L"Stance"),
        right,
        DT_LEFT | DT_WORDBREAK,
        RGB(57, 68, 84));

    RECT fixed = {24, 370, 334, 425};
    DrawTextSimple(
        dc,
        L"Fixed: triggers, grips, movement/turn sticks, menu and right-thumbrest modifier.",
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

        if (state->selectedPage == PageIndex(kc::SettingPage::Controls))
        {
            DrawControllerPreview(dc, client, *state);
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

void BuildSettingControls(AppState& state)
{
    const auto& catalog = kc::SettingsCatalog();
    state.bindings.reserve(catalog.size());

    for (std::size_t index = 0; index < catalog.size(); ++index)
    {
        const kc::SettingDefinition& definition = catalog[index];
        ControlBinding binding;
        binding.definition = &definition;
        binding.description = ToWide(definition.description);
        binding.label = CreateControl(
            state,
            L"STATIC",
            ToWide(definition.label).c_str(),
            SS_LEFT,
            0,
            0,
            100,
            18);

        const int identifier = kIdSettingBase + static_cast<int>(index);
        if (definition.type == kc::SettingType::Choice ||
            definition.type == kc::SettingType::Toggle)
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
        L"Portable V56 settings - changes take effect the next time COD4 starts",
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
        (L"User settings: " + state.userPath.wstring()).c_str(),
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
        default:
            break;
        }
        break;
    }
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

    const kc::LoadResult loaded = kc::LoadSettings(
        state.defaultsPath,
        state.userPath);
    state.values = loaded.values;
    state.validation = loaded.messages;

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
