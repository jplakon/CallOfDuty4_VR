#pragma once

#include <filesystem>
#include <string>

#include "vr/vr_compatibility.h"

namespace kisak::configurator::win32_compatibility
{

// Resolves the runtime selected by OpenVR's own path-registry shim and accepts
// it only when the architecture-matched 32-bit client file exists at the path
// used by the pinned OpenVR loader. This deliberately rejects stale path
// registries and x64-only installations before they are presented as a usable
// fallback.
bool FindOpenVrX86Client(
    std::filesystem::path* clientPath);

kisak::vr::compatibility::Probe ProbeSystem(
    const std::filesystem::path& gameDirectory,
    const std::filesystem::path& runtimeReceiptPath,
    const std::string& backendPolicy,
    const std::string& currentPackedMode,
    const std::string& currentOutputScale);

std::string LocalTimestamp();

bool WriteReportAtomic(
    const std::filesystem::path& path,
    const std::string& text,
    std::string* error);

} // namespace kisak::configurator::win32_compatibility
