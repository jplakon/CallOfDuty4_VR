#pragma once

#include <cstdint>

namespace kisak::vr::postfx
{

inline bool UseEyeLocalColorOnly(
    const bool sameFrameStereoEnabled,
    const std::uint32_t viewCount,
    const bool retailFullscreenView)
{
    const bool packedStereoViewSet =
        sameFrameStereoEnabled &&
        (viewCount == 2u || viewCount == 3u);

    return
        packedStereoViewSet &&
        retailFullscreenView;
}

} // namespace kisak::vr::postfx
