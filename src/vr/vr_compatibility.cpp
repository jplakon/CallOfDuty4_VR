#include "vr/vr_compatibility.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace kisak::vr::compatibility
{
namespace
{

constexpr std::uint64_t kGiB = 1024ull * 1024ull * 1024ull;

std::string Lower(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool Contains(const std::string& haystack, const char* const needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::string SafeLine(std::string value)
{
    for (char& character : value)
    {
        if (character == '\r' || character == '\n')
        {
            character = ' ';
        }
    }
    return value;
}

void AddCheck(
    Report* const report,
    const char* const id,
    const char* const label,
    const Status status,
    const std::string& detail,
    const std::string& action = {})
{
    report->checks.push_back({id, label, status, detail, action});
}

bool IsOpenXrReady(const Probe& probe)
{
    return probe.openXr32Registered &&
        probe.openXr32ManifestPresent;
}

} // namespace

const char* StatusId(const Status status)
{
    switch (status)
    {
        case Status::Ready:
            return "READY";
        case Status::Warning:
            return "WARNING";
        case Status::Blocked:
            return "BLOCKED";
        default:
            return "UNKNOWN";
    }
}

const char* RuntimeFamilyId(const RuntimeFamily family)
{
    switch (family)
    {
        case RuntimeFamily::None:
            return "none";
        case RuntimeFamily::VirtualDesktop:
            return "virtual_desktop";
        case RuntimeFamily::MetaOculus:
            return "meta_oculus";
        case RuntimeFamily::SteamVr:
            return "steamvr";
        case RuntimeFamily::WindowsMixedReality:
            return "windows_mixed_reality";
        case RuntimeFamily::Pimax:
            return "pimax";
        case RuntimeFamily::Other:
            return "other";
        default:
            return "unknown";
    }
}

RuntimeFamily ClassifyRuntime(
    const std::string& manifestPath,
    const std::string& manifestText,
    const std::string& lastRuntimeName)
{
    const std::string evidence = Lower(
        manifestPath + "\n" + manifestText + "\n" + lastRuntimeName);

    if (Contains(evidence, "virtualdesktop") ||
        Contains(evidence, "virtual desktop") ||
        Contains(evidence, "vdxr"))
    {
        return RuntimeFamily::VirtualDesktop;
    }
    if (Contains(evidence, "oculus") || Contains(evidence, "meta openxr"))
    {
        return RuntimeFamily::MetaOculus;
    }
    if (Contains(evidence, "steamvr") || Contains(evidence, "steam vr"))
    {
        return RuntimeFamily::SteamVr;
    }
    if (Contains(evidence, "mixedreality") ||
        Contains(evidence, "mixed reality") ||
        Contains(evidence, "windowsmr"))
    {
        return RuntimeFamily::WindowsMixedReality;
    }
    if (Contains(evidence, "pimax"))
    {
        return RuntimeFamily::Pimax;
    }
    return evidence.find_first_not_of("\r\n \t") == std::string::npos
        ? RuntimeFamily::None
        : RuntimeFamily::Other;
}

Report Evaluate(const Probe& probe)
{
    Report report;
    report.runtimeFamily = ClassifyRuntime(
        probe.openXr32ManifestPath,
        probe.openXr32ManifestText,
        probe.lastRuntimeName);

    const bool installationReady =
        probe.gameExecutablePresent &&
        probe.modExecutablePresent &&
        probe.configuratorPresent &&
        probe.settingsPresent &&
        probe.launcherPresent;
    AddCheck(
        &report,
        "installation",
        "Game and mod files",
        installationReady ? Status::Ready : Status::Blocked,
        installationReady
            ? "COD4 and every required KisakCOD VR launch file are present."
            : "One or more required COD4/KisakCOD VR files are missing.",
        installationReady
            ? std::string()
            : "Install the complete standalone package into the folder containing iw3sp.exe.");

    AddCheck(
        &report,
        "directx",
        "DirectX June 2010 runtime",
        probe.d3dx9Present ? Status::Ready : Status::Blocked,
        probe.d3dx9Present
            ? "d3dx9_43.dll is available to this 32-bit process."
            : "The required Microsoft d3dx9_43.dll runtime is unavailable.",
        probe.d3dx9Present
            ? std::string()
            : "Install Microsoft's DirectX End-User Runtimes (June 2010); do not download a loose DLL.");

    AddCheck(
        &report,
        "windows_architecture",
        "Windows architecture",
        probe.is64BitWindows ? Status::Ready : Status::Warning,
        probe.is64BitWindows
            ? "64-bit Windows can host the mod's required 32-bit game and runtime components."
            : "A 64-bit Windows host was not detected; current PC VR runtimes may be unavailable.",
        probe.is64BitWindows
            ? std::string()
            : "Use a supported 64-bit Windows installation for PC VR.");

    if (probe.gpuName.empty())
    {
        AddCheck(
            &report,
            "graphics",
            "Graphics adapter",
            Status::Warning,
            "No hardware graphics adapter could be identified offline.",
            "Run the headset test and attach this report if graphics initialization fails.");
    }
    else
    {
        const bool lowMemory =
            probe.dedicatedVideoMemoryBytes > 0u &&
            probe.dedicatedVideoMemoryBytes < 4u * kGiB;
        std::ostringstream detail;
        detail << probe.gpuName;
        if (probe.dedicatedVideoMemoryBytes > 0u)
        {
            detail << " ("
                   << (probe.dedicatedVideoMemoryBytes / kGiB)
                   << " GiB dedicated memory)";
        }
        detail << (lowMemory
            ? "; use the Performance graphics profile."
            : "; a hardware adapter is available for the D3D9Ex/D3D11 bridge.");
        AddCheck(
            &report,
            "graphics",
            "Graphics adapter",
            lowMemory ? Status::Warning : Status::Ready,
            detail.str(),
            lowMemory
                ? "Apply the recommended Performance profile."
                : std::string());
    }

    const bool openXrReady = IsOpenXrReady(probe);
    const std::string policy = Lower(probe.backendPolicy);
    Status runtimeStatus = Status::Ready;
    std::string runtimeDetail;
    std::string runtimeAction;

    if (policy == "openxr")
    {
        if (!openXrReady)
        {
            runtimeStatus = Status::Blocked;
            runtimeDetail = "OpenXR-only is selected, but no usable 32-bit ActiveRuntime manifest is registered.";
            runtimeAction = "Activate a 32-bit-capable OpenXR runtime or select OpenVR after starting SteamVR.";
        }
        else
        {
            runtimeDetail = "OpenXR-only is selected and the 32-bit runtime manifest exists.";
        }
    }
    else if (policy == "openvr")
    {
        if (!probe.openVrInstalled)
        {
            runtimeStatus = Status::Blocked;
            runtimeDetail = "OpenVR is selected, but a SteamVR/OpenVR runtime was not found.";
            runtimeAction = "Install and start SteamVR, or select OpenXR with a valid 32-bit runtime.";
        }
        else
        {
            runtimeStatus = Status::Warning;
            runtimeDetail = "The experimental 32-bit OpenVR fallback is installed and explicitly selected.";
            runtimeAction = "Use OpenXR/VDXR when available; keep OpenVR for compatibility testing.";
        }
    }
    else if (openXrReady)
    {
        runtimeDetail = "Automatic mode has a valid 32-bit OpenXR runtime and may fall back to OpenVR if needed.";
    }
    else if (probe.openVrInstalled)
    {
        runtimeStatus = Status::Warning;
        runtimeDetail = probe.openXr64Registered && probe.openXr64ManifestPresent
            ? "Only a 64-bit OpenXR runtime is registered for this 32-bit game; automatic mode can use the experimental OpenVR fallback."
            : "No valid 32-bit OpenXR runtime is registered; automatic mode can use the experimental OpenVR fallback.";
        runtimeAction = "Start SteamVR before launching, or activate a 32-bit-capable OpenXR runtime.";
    }
    else
    {
        runtimeStatus = Status::Blocked;
        runtimeDetail = probe.openXr64Registered && probe.openXr64ManifestPresent
            ? "A 64-bit OpenXR runtime exists, but this 32-bit game has neither a valid 32-bit OpenXR runtime nor OpenVR fallback."
            : "Neither a valid 32-bit OpenXR runtime nor an OpenVR fallback was found.";
        runtimeAction = "Activate a compatible 32-bit OpenXR runtime or install/start SteamVR.";
    }
    AddCheck(
        &report,
        "runtime",
        "VR runtime/backend",
        runtimeStatus,
        runtimeDetail,
        runtimeAction);

    const bool previousRuntimeReady =
        Lower(probe.lastRuntimeStatus) == "ready";
    const bool controllerKnown =
        !probe.lastLeftControllerProfile.empty() ||
        !probe.lastRightControllerProfile.empty();
    if (previousRuntimeReady)
    {
        std::string detail = "Last successful headset test: " +
            (probe.lastRuntimeBackend.empty()
                ? std::string("unknown backend")
                : probe.lastRuntimeBackend);
        if (!probe.lastRuntimeName.empty())
        {
            detail += " / " + probe.lastRuntimeName;
        }
        if (!probe.lastHeadsetName.empty())
        {
            detail += " / " + probe.lastHeadsetName;
        }
        AddCheck(
            &report,
            "headset",
            "Last headset test",
            Status::Ready,
            detail + ".");
        report.headsetTestRequired = false;
    }
    else
    {
        AddCheck(
            &report,
            "headset",
            "Headset session",
            Status::Warning,
            "Offline checks cannot prove that the headset is awake, on the same GPU, and able to create a session.",
            "Run Save & Launch Diagnostics once with the headset connected and awake.");
    }

    AddCheck(
        &report,
        "controllers",
        "Motion-controller profile",
        controllerKnown ? Status::Ready : Status::Warning,
        controllerKnown
            ? "The last runtime reported an active controller interaction profile."
            : "No successful controller profile has been recorded yet; controller-neutral bindings remain available.",
        controllerKnown
            ? std::string()
            : "Complete one headset test, then rescan this page.");

    report.recommendedBackend = probe.backendPolicy;
    if (openXrReady)
    {
        report.recommendedBackend =
            report.runtimeFamily == RuntimeFamily::VirtualDesktop
                ? "openxr"
                : "auto";
    }
    else if (probe.openVrInstalled)
    {
        report.recommendedBackend = "openvr";
    }

    if (probe.dedicatedVideoMemoryBytes >= 10u * kGiB)
    {
        report.recommendedGraphicsProfile = "native";
    }
    else if (probe.dedicatedVideoMemoryBytes > 0u)
    {
        report.recommendedGraphicsProfile = "performance";
    }
    else
    {
        report.recommendedGraphicsProfile =
            probe.currentPackedMode == "4768x2016"
                ? "performance"
                : "native";
    }

    std::ostringstream recommendation;
    recommendation << "Backend: " << report.recommendedBackend
                   << "; graphics: " << report.recommendedGraphicsProfile;
    if (report.runtimeFamily == RuntimeFamily::VirtualDesktop)
    {
        recommendation << "; detected Virtual Desktop/VDXR, the primary tested path";
    }
    else if (!openXrReady && probe.openVrInstalled)
    {
        recommendation << "; OpenVR is experimental";
    }
    recommendation << ". Handedness, units, comfort, controls, HUD, and calibration are not changed.";
    report.recommendationSummary = recommendation.str();

    report.status = Status::Ready;
    for (const Check& check : report.checks)
    {
        if (check.status == Status::Blocked)
        {
            report.status = Status::Blocked;
            break;
        }
        if (check.status == Status::Warning)
        {
            report.status = Status::Warning;
        }
    }
    report.readyForLaunch = report.status != Status::Blocked;
    return report;
}

std::string SerializeReport(
    const Probe& probe,
    const Report& report,
    const std::string& generatedAt)
{
    std::ostringstream output;
    output << "KisakCOD VR beta.11 unified setup and compatibility report\r\n";
    output << "STATUS=" << StatusId(report.status) << "\r\n";
    output << "READY_FOR_LAUNCH=" << (report.readyForLaunch ? 1 : 0) << "\r\n";
    output << "HEADSET_TEST_REQUIRED=" << (report.headsetTestRequired ? 1 : 0) << "\r\n";
    output << "GENERATED_AT=" << SafeLine(generatedAt) << "\r\n";
    output << "RUNTIME_FAMILY=" << RuntimeFamilyId(report.runtimeFamily) << "\r\n";
    output << "BACKEND_POLICY=" << SafeLine(probe.backendPolicy) << "\r\n";
    output << "OPENXR_32_REGISTERED=" << (probe.openXr32Registered ? 1 : 0) << "\r\n";
    output << "OPENXR_32_MANIFEST_PRESENT=" << (probe.openXr32ManifestPresent ? 1 : 0) << "\r\n";
    output << "OPENXR_32_MANIFEST=" << SafeLine(probe.openXr32ManifestPath) << "\r\n";
    output << "OPENXR_64_REGISTERED=" << (probe.openXr64Registered ? 1 : 0) << "\r\n";
    output << "OPENXR_64_MANIFEST_PRESENT=" << (probe.openXr64ManifestPresent ? 1 : 0) << "\r\n";
    output << "OPENXR_64_MANIFEST=" << SafeLine(probe.openXr64ManifestPath) << "\r\n";
    output << "OPENVR_INSTALLED=" << (probe.openVrInstalled ? 1 : 0) << "\r\n";
    output << "OPENVR_EVIDENCE=" << SafeLine(probe.openVrEvidence) << "\r\n";
    output << "GPU=" << SafeLine(probe.gpuName) << "\r\n";
    output << "GPU_DEDICATED_MEMORY_BYTES=" << probe.dedicatedVideoMemoryBytes << "\r\n";
    output << "LAST_RUNTIME_STATUS=" << SafeLine(probe.lastRuntimeStatus) << "\r\n";
    output << "LAST_RUNTIME_BACKEND=" << SafeLine(probe.lastRuntimeBackend) << "\r\n";
    output << "LAST_RUNTIME_NAME=" << SafeLine(probe.lastRuntimeName) << "\r\n";
    output << "LAST_HEADSET=" << SafeLine(probe.lastHeadsetName) << "\r\n";
    output << "LAST_LEFT_CONTROLLER_PROFILE=" << SafeLine(probe.lastLeftControllerProfile) << "\r\n";
    output << "LAST_RIGHT_CONTROLLER_PROFILE=" << SafeLine(probe.lastRightControllerProfile) << "\r\n";
    output << "RECOMMENDED_BACKEND=" << SafeLine(report.recommendedBackend) << "\r\n";
    output << "RECOMMENDED_GRAPHICS=" << SafeLine(report.recommendedGraphicsProfile) << "\r\n";
    output << "RECOMMENDATION=" << SafeLine(report.recommendationSummary) << "\r\n";
    for (const Check& check : report.checks)
    {
        output << "\r\nCHECK_" << check.id << "=" << StatusId(check.status) << "\r\n";
        output << "CHECK_" << check.id << "_DETAIL=" << SafeLine(check.detail) << "\r\n";
        output << "CHECK_" << check.id << "_ACTION=" << SafeLine(check.action) << "\r\n";
    }
    return output.str();
}

} // namespace kisak::vr::compatibility
