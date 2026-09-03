#pragma once

#include <limits>

namespace kisak::vr::packed_layout
{

struct CaptureLayout
{
    int mainStereoWidth = 0;
    int scopePanelX = 0;
    int scopePanelY = 0;
    int scopePanelSize = 0;
};

inline bool ResolveCaptureLayout(
    const int backbufferWidth,
    const int backbufferHeight,
    const int leftEyeWidth,
    const int rightEyeWidth,
    const int requestedScopeSize,
    CaptureLayout* layout)
{
    if (layout == nullptr ||
        backbufferWidth < 2 ||
        backbufferHeight < 1 ||
        leftEyeWidth < 1 ||
        rightEyeWidth < 1 ||
        requestedScopeSize < 1 ||
        leftEyeWidth >
            (std::numeric_limits<int>::max)() - rightEyeWidth)
    {
        return false;
    }

    int resolvedScopeSize = requestedScopeSize;
    if (resolvedScopeSize < 512)
    {
        resolvedScopeSize = 512;
    }
    if (resolvedScopeSize > backbufferHeight)
    {
        resolvedScopeSize = backbufferHeight;
    }

    if (resolvedScopeSize < 512 ||
        resolvedScopeSize > backbufferWidth)
    {
        return false;
    }

    const int resolvedMainStereoWidth =
        leftEyeWidth + rightEyeWidth;

    if (resolvedMainStereoWidth >
        backbufferWidth - resolvedScopeSize)
    {
        return false;
    }

    layout->mainStereoWidth = resolvedMainStereoWidth;
    layout->scopePanelX = resolvedMainStereoWidth;
    layout->scopePanelY = 0;
    layout->scopePanelSize = resolvedScopeSize;
    return true;
}

} // namespace kisak::vr::packed_layout
