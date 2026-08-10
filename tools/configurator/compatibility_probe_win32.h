#pragma once

#include <filesystem>
#include <string>

#include "vr/vr_compatibility.h"

namespace kisak::configurator::win32_compatibility
{

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
