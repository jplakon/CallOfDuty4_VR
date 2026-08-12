#pragma once

#include "vr/vr_input_bindings.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace kisak::vr::prompts
{

// OpenVR's legacy controller API does not expose the same logical components
// as OpenXR. Keep the backend in the formatter so the label describes the
// input the mapper actually consumes instead of merely naming the headset.
enum class Backend
{
    OpenXr,
    OpenVr,
};

struct BindingLabels
{
    std::array<std::string, 2> values = {};
    std::size_t count = 0u;
};

// Maps COD4's native command names to the semantic Controller Input V4 action
// that performs the same operation in VR. Unknown commands deliberately
// return null so the caller can preserve COD4's keyboard resolver.
const input::ActionDefinition* FindPromptAction(
    std::string_view command);

// Produces one concise, text-only controller label. The active interaction
// profile is evaluated per hand, allowing asymmetric or compatibility setups
// to retain a safe controller-neutral fallback.
std::string SourcePromptLabel(
    input::Source source,
    std::string_view activeProfile,
    Backend backend);

// Formats the configured primary and alternate slots. Terms inside a slot are
// joined as a chord; duplicate primary/alternate labels are collapsed.
BindingLabels BuildBindingLabels(
    const std::array<input::Binding, 2>& bindings,
    const std::array<std::string_view, 2>& activeProfiles,
    Backend backend,
    std::string_view command = {});

} // namespace kisak::vr::prompts
