#include "compatibility_probe_win32.h"

#include <windows.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <cwctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <system_error>
#include <vector>

namespace kisak::configurator::win32_compatibility
{
namespace
{

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0)
    {
        return std::string(value.begin(), value.end());
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
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

std::wstring ExpandRegistryPath(const std::wstring& value)
{
    const DWORD required = ExpandEnvironmentStringsW(
        value.c_str(),
        nullptr,
        0u);
    if (required == 0u)
    {
        return value;
    }
    std::wstring expanded(required, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(
        value.c_str(),
        expanded.data(),
        required);
    if (written == 0u || written > required)
    {
        return value;
    }
    expanded.resize(written > 0u ? written - 1u : 0u);
    return expanded;
}

bool ReadRegistryString(
    const HKEY root,
    const wchar_t* const keyPath,
    const wchar_t* const valueName,
    const REGSAM view,
    std::wstring* const value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(
            root,
            keyPath,
            0u,
            KEY_QUERY_VALUE | view,
            &key) != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD type = 0u;
    DWORD bytes = 0u;
    LONG result = RegQueryValueExW(
        key,
        valueName,
        nullptr,
        &type,
        nullptr,
        &bytes);
    if (result != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes < sizeof(wchar_t))
    {
        RegCloseKey(key);
        return false;
    }

    std::vector<wchar_t> buffer(
        static_cast<std::size_t>(bytes / sizeof(wchar_t)) + 1u,
        L'\0');
    result = RegQueryValueExW(
        key,
        valueName,
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(buffer.data()),
        &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
    {
        return false;
    }

    *value = buffer.data();
    if (type == REG_EXPAND_SZ)
    {
        *value = ExpandRegistryPath(*value);
    }
    return !value->empty();
}

bool ReadActiveRuntime(
    const REGSAM view,
    std::wstring* const manifestPath)
{
    constexpr wchar_t kOpenXrKey[] = L"SOFTWARE\\Khronos\\OpenXR\\1";
    return ReadRegistryString(
               HKEY_LOCAL_MACHINE,
               kOpenXrKey,
               L"ActiveRuntime",
               view,
               manifestPath) ||
        ReadRegistryString(
               HKEY_CURRENT_USER,
               kOpenXrKey,
               L"ActiveRuntime",
               view,
               manifestPath);
}

std::wstring LowerWide(std::wstring value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const wchar_t character)
        {
            return static_cast<wchar_t>(
                std::towlower(character));
        });
    return value;
}

bool IsPimaxRuntimeManifest(
    const std::filesystem::path& path)
{
    const std::wstring evidence =
        LowerWide(path.wstring());
    return evidence.find(L"pimax") != std::wstring::npos ||
        evidence.find(L"piopenxr") != std::wstring::npos;
}

bool IsPimaxX64Manifest(
    const std::filesystem::path& path)
{
    const std::wstring filename =
        LowerWide(path.filename().wstring());
    return filename.find(L"piopenxr_64.json") !=
        std::wstring::npos;
}

std::filesystem::path EnvironmentDirectory(
    const wchar_t* const variable)
{
    std::array<wchar_t, 32768u> value = {};
    const DWORD length = GetEnvironmentVariableW(
        variable,
        value.data(),
        static_cast<DWORD>(value.size()));
    if (length == 0u || length >= value.size())
    {
        return {};
    }
    return std::filesystem::path(value.data());
}

std::filesystem::path FindPimaxX86Manifest(
    const std::filesystem::path& activeManifest)
{
    std::vector<std::filesystem::path> candidates;
    if (!activeManifest.empty() &&
        activeManifest.has_parent_path())
    {
        candidates.push_back(
            activeManifest.parent_path() /
            L"PiOpenXR_32.json");
    }

    const std::filesystem::path programFiles64 =
        EnvironmentDirectory(L"ProgramW6432");
    if (!programFiles64.empty())
    {
        candidates.push_back(
            programFiles64 / L"Pimax" / L"Runtime" /
            L"PiOpenXR_32.json");
    }

    const std::filesystem::path programFiles =
        EnvironmentDirectory(L"ProgramFiles");
    if (!programFiles.empty())
    {
        candidates.push_back(
            programFiles / L"Pimax" / L"Runtime" /
            L"PiOpenXR_32.json");
    }

    for (const std::filesystem::path& candidate : candidates)
    {
        if (std::filesystem::is_regular_file(candidate))
        {
            return candidate;
        }
    }
    return {};
}

bool Is64BitWindows()
{
    SYSTEM_INFO information = {};
    GetNativeSystemInfo(&information);
    return information.wProcessorArchitecture ==
            PROCESSOR_ARCHITECTURE_AMD64 ||
        information.wProcessorArchitecture ==
            PROCESSOR_ARCHITECTURE_ARM64 ||
        information.wProcessorArchitecture ==
            PROCESSOR_ARCHITECTURE_IA64;
}

bool DirectXRuntimePresent(const std::filesystem::path& gameDirectory)
{
    if (std::filesystem::is_regular_file(gameDirectory / L"d3dx9_43.dll"))
    {
        return true;
    }

    std::array<wchar_t, MAX_PATH + 1u> folder = {};
    const UINT wowLength = GetSystemWow64DirectoryW(
        folder.data(),
        static_cast<UINT>(folder.size()));
    if (wowLength > 0u && wowLength < folder.size() &&
        std::filesystem::is_regular_file(
            std::filesystem::path(folder.data()) / L"d3dx9_43.dll"))
    {
        return true;
    }

    folder.fill(L'\0');
    const UINT systemLength = GetSystemDirectoryW(
        folder.data(),
        static_cast<UINT>(folder.size()));
    return systemLength > 0u && systemLength < folder.size() &&
        std::filesystem::is_regular_file(
            std::filesystem::path(folder.data()) / L"d3dx9_43.dll");
}

void DetectGpu(
    std::string* const name,
    std::uint64_t* const memoryBytes)
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(
            __uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(&factory))) ||
        factory == nullptr)
    {
        return;
    }

    std::uint64_t largestMemory = 0u;
    std::wstring selectedName;
    for (UINT index = 0u;; ++index)
    {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT result = factory->EnumAdapters1(index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        if (FAILED(result) || adapter == nullptr)
        {
            continue;
        }

        DXGI_ADAPTER_DESC1 description = {};
        if (SUCCEEDED(adapter->GetDesc1(&description)) &&
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0u &&
            (selectedName.empty() ||
             description.DedicatedVideoMemory >= largestMemory))
        {
            selectedName = description.Description;
            largestMemory = static_cast<std::uint64_t>(
                description.DedicatedVideoMemory);
        }
        adapter->Release();
    }
    factory->Release();

    *name = WideToUtf8(selectedName);
    *memoryBytes = largestMemory;
}

std::filesystem::path LocalAppDataDirectory()
{
    std::array<wchar_t, 32768u> value = {};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        value.data(),
        static_cast<DWORD>(value.size()));
    if (length == 0u || length >= value.size())
    {
        return {};
    }
    return std::filesystem::path(value.data());
}

void DetectOpenVr(
    bool* const installed,
    std::string* const evidence)
{
    const std::filesystem::path localAppData = LocalAppDataDirectory();
    if (!localAppData.empty())
    {
        const std::filesystem::path vrPaths =
            localAppData / L"openvr" / L"openvrpaths.vrpath";
        const std::string text = ReadTextFile(vrPaths);
        if (!text.empty() && text.find("runtime") != std::string::npos)
        {
            *installed = true;
            *evidence = WideToUtf8(vrPaths.wstring());
            return;
        }
    }

    std::wstring steamPath;
    if (!ReadRegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Valve\\Steam",
            L"SteamPath",
            0u,
            &steamPath))
    {
        return;
    }

    const std::filesystem::path steamVr =
        std::filesystem::path(steamPath) /
        L"steamapps" / L"common" / L"SteamVR";
    const std::array<std::filesystem::path, 2> candidates = {
        steamVr / L"bin" / L"win32" / L"vrclient.dll",
        steamVr / L"bin" / L"win64" / L"vrclient_x64.dll",
    };
    for (const std::filesystem::path& candidate : candidates)
    {
        if (std::filesystem::is_regular_file(candidate))
        {
            *installed = true;
            *evidence = WideToUtf8(candidate.wstring());
            return;
        }
    }
}

std::map<std::string, std::string> ParseReceipt(
    const std::filesystem::path& path)
{
    std::map<std::string, std::string> values;
    std::istringstream input(ReadTextFile(path));
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0u)
        {
            continue;
        }
        values[line.substr(0u, separator)] = line.substr(separator + 1u);
    }
    return values;
}

std::string ReceiptValue(
    const std::map<std::string, std::string>& values,
    const char* const key)
{
    const auto found = values.find(key);
    return found == values.end() ? std::string() : found->second;
}

} // namespace

kisak::vr::compatibility::Probe ProbeSystem(
    const std::filesystem::path& gameDirectory,
    const std::filesystem::path& runtimeReceiptPath,
    const std::string& backendPolicy,
    const std::string& currentPackedMode,
    const std::string& currentOutputScale)
{
    namespace vrc = kisak::vr::compatibility;
    vrc::Probe probe;
    probe.is64BitWindows = Is64BitWindows();
    probe.gameExecutablePresent =
        std::filesystem::is_regular_file(gameDirectory / L"iw3sp.exe");
    probe.modExecutablePresent =
        std::filesystem::is_regular_file(gameDirectory / L"KisakCOD-sp.exe");
    probe.configuratorPresent = std::filesystem::is_regular_file(
        gameDirectory / L"KisakCOD-VR-Configurator.exe");
    probe.settingsPresent =
        std::filesystem::is_regular_file(gameDirectory / L"VR-Settings.bat");
    probe.launcherPresent = std::filesystem::is_regular_file(
        gameDirectory / L"Launch-KisakCOD-VR.bat");
    probe.d3dx9Present = DirectXRuntimePresent(gameDirectory);
    probe.backendPolicy = backendPolicy.empty() ? "auto" : backendPolicy;
    probe.currentPackedMode = currentPackedMode;
    probe.currentOutputScale = currentOutputScale;

    std::wstring openXr32Path;
    probe.openXr32Registered = ReadActiveRuntime(
        KEY_WOW64_32KEY,
        &openXr32Path);
    if (probe.openXr32Registered)
    {
        probe.openXr32ManifestPath = WideToUtf8(openXr32Path);
        probe.openXr32ManifestPresent =
            std::filesystem::is_regular_file(openXr32Path);
        if (probe.openXr32ManifestPresent)
        {
            probe.openXr32ManifestText = ReadTextFile(openXr32Path);
        }
    }

    std::wstring openXr64Path;
    probe.openXr64Registered = ReadActiveRuntime(
        KEY_WOW64_64KEY,
        &openXr64Path);
    if (probe.openXr64Registered)
    {
        probe.openXr64ManifestPath = WideToUtf8(openXr64Path);
        probe.openXr64ManifestPresent =
            std::filesystem::is_regular_file(openXr64Path);
    }

    // KISAK_SP_VR_PIMAX_X86_RUNTIME_V86
    // Pimax Play can publish PiOpenXR_64.json through the 32-bit registry
    // view. The launcher repairs only an actively selected Pimax runtime, so
    // mirror that decision here before the shared compatibility evaluator can
    // incorrectly block this x86 game. A valid non-Pimax x86 registration is
    // never replaced merely because Pimax is installed.
    const std::filesystem::path registeredOpenXr32 =
        openXr32Path;
    const std::filesystem::path registeredOpenXr64 =
        openXr64Path;
    const bool activePimaxRuntime =
        (probe.openXr32Registered &&
         IsPimaxRuntimeManifest(registeredOpenXr32)) ||
        (!probe.openXr32Registered &&
         probe.openXr64Registered &&
         IsPimaxRuntimeManifest(registeredOpenXr64));

    if (activePimaxRuntime)
    {
        const std::filesystem::path pimaxEvidence =
            probe.openXr32Registered
                ? registeredOpenXr32
                : registeredOpenXr64;
        const std::filesystem::path pimaxX86Manifest =
            FindPimaxX86Manifest(pimaxEvidence);

        if (!pimaxX86Manifest.empty())
        {
            probe.openXr32Registered = true;
            probe.openXr32ManifestPresent = true;
            probe.openXr32ManifestPath =
                WideToUtf8(pimaxX86Manifest.wstring());
            probe.openXr32ManifestText =
                ReadTextFile(pimaxX86Manifest);
        }
        else if (probe.openXr32Registered &&
                 IsPimaxX64Manifest(registeredOpenXr32))
        {
            probe.openXr32ManifestPresent = false;
            probe.openXr32ManifestText.clear();
        }
    }

    DetectOpenVr(&probe.openVrInstalled, &probe.openVrEvidence);
    DetectGpu(&probe.gpuName, &probe.dedicatedVideoMemoryBytes);

    const std::map<std::string, std::string> receipt =
        ParseReceipt(runtimeReceiptPath);
    probe.lastRuntimeStatus = ReceiptValue(
        receipt,
        "RUNTIME_COMPATIBILITY_STATUS");
    probe.lastRuntimeBackend = ReceiptValue(
        receipt,
        "RUNTIME_COMPATIBILITY_BACKEND");
    probe.lastRuntimeName = ReceiptValue(
        receipt,
        "RUNTIME_COMPATIBILITY_RUNTIME");
    probe.lastHeadsetName = ReceiptValue(
        receipt,
        "RUNTIME_COMPATIBILITY_HEADSET");
    probe.lastLeftControllerProfile = ReceiptValue(
        receipt,
        "RUNTIME_COMPATIBILITY_LEFT_CONTROLLER");
    probe.lastRightControllerProfile = ReceiptValue(
        receipt,
        "RUNTIME_COMPATIBILITY_RIGHT_CONTROLLER");
    return probe;
}

std::string LocalTimestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%dT%H:%M:%S%z");
    return output.str();
}

bool WriteReportAtomic(
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
            *error = "Could not create the compatibility-report folder.";
        }
        return false;
    }

    const std::filesystem::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            if (error != nullptr)
            {
                *error = "Could not open the temporary compatibility report.";
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
                *error = "The compatibility report could not be flushed.";
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
            *error = "Windows could not atomically replace the compatibility report.";
        }
        return false;
    }
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

} // namespace kisak::configurator::win32_compatibility
