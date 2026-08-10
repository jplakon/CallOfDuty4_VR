#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kisak::vr::compatibility
{

enum class Status
{
    Ready,
    Warning,
    Blocked,
};

enum class RuntimeFamily
{
    None,
    VirtualDesktop,
    MetaOculus,
    SteamVr,
    WindowsMixedReality,
    Pimax,
    Other,
};

struct Probe
{
    bool is64BitWindows = false;
    bool gameExecutablePresent = false;
    bool modExecutablePresent = false;
    bool configuratorPresent = false;
    bool settingsPresent = false;
    bool launcherPresent = false;
    bool d3dx9Present = false;

    bool openXr32Registered = false;
    bool openXr32ManifestPresent = false;
    std::string openXr32ManifestPath;
    std::string openXr32ManifestText;
    bool openXr64Registered = false;
    bool openXr64ManifestPresent = false;
    std::string openXr64ManifestPath;
    bool openVrInstalled = false;
    std::string openVrEvidence;

    std::string gpuName;
    std::uint64_t dedicatedVideoMemoryBytes = 0u;

    std::string backendPolicy = "auto";
    std::string currentPackedMode = "6016x2688";
    std::string currentOutputScale = "1.00";

    std::string lastRuntimeStatus;
    std::string lastRuntimeBackend;
    std::string lastRuntimeName;
    std::string lastHeadsetName;
    std::string lastLeftControllerProfile;
    std::string lastRightControllerProfile;
};

struct Check
{
    std::string id;
    std::string label;
    Status status = Status::Warning;
    std::string detail;
    std::string action;
};

struct Report
{
    Status status = Status::Warning;
    RuntimeFamily runtimeFamily = RuntimeFamily::None;
    bool readyForLaunch = false;
    bool headsetTestRequired = true;
    std::string recommendedBackend;
    std::string recommendedGraphicsProfile;
    std::string recommendationSummary;
    std::vector<Check> checks;
};

const char* StatusId(Status status);
const char* RuntimeFamilyId(RuntimeFamily family);

RuntimeFamily ClassifyRuntime(
    const std::string& manifestPath,
    const std::string& manifestText,
    const std::string& lastRuntimeName);

Report Evaluate(const Probe& probe);

std::string SerializeReport(
    const Probe& probe,
    const Report& report,
    const std::string& generatedAt);

} // namespace kisak::vr::compatibility
