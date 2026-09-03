#pragma once

#include <cstdint>

namespace kisak::vr::saved_screen
{

struct BlendRegion
{
    float destinationWidth = 0.0f;
    float destinationHeight = 0.0f;
    float sourceS0 = 0.0f;
    float sourceT0 = 0.0f;
    float sourceS1 = 0.0f;
    float sourceT1 = 0.0f;
};

inline bool IsPackedViewSet(
    const bool sameFrameStereoEnabled,
    const std::uint32_t viewCount)
{
    return
        sameFrameStereoEnabled &&
        (viewCount == 2u || viewCount == 3u);
}

inline bool ShouldCaptureFullPackedFrame(
    const bool sameFrameStereoEnabled,
    const std::uint32_t viewCount,
    const std::uint32_t viewIndex)
{
    if (!IsPackedViewSet(
            sameFrameStereoEnabled,
            viewCount) ||
        viewIndex >= viewCount)
    {
        return true;
    }

    return viewIndex + 1u == viewCount;
}

inline bool ResolvePackedBlendRegion(
    const bool sameFrameStereoEnabled,
    const std::uint32_t viewCount,
    const std::uint32_t viewIndex,
    const int renderTargetWidth,
    const int renderTargetHeight,
    const int viewX,
    const int viewY,
    const int viewWidth,
    const int viewHeight,
    const float commandS0,
    const float commandT0,
    const float commandWidth,
    const float commandHeight,
    BlendRegion* region)
{
    if (region == nullptr ||
        !IsPackedViewSet(
            sameFrameStereoEnabled,
            viewCount) ||
        viewIndex >= viewCount ||
        renderTargetWidth < 1 ||
        renderTargetHeight < 1 ||
        viewX < 0 ||
        viewY < 0 ||
        viewWidth < 1 ||
        viewHeight < 1 ||
        viewX > renderTargetWidth - viewWidth ||
        viewY > renderTargetHeight - viewHeight ||
        commandS0 < 0.0f ||
        commandT0 < 0.0f ||
        commandWidth < 0.0f ||
        commandHeight < 0.0f ||
        commandS0 > 1.0f - commandWidth ||
        commandT0 > 1.0f - commandHeight)
    {
        return false;
    }

    const float targetWidth =
        static_cast<float>(renderTargetWidth);
    const float targetHeight =
        static_cast<float>(renderTargetHeight);
    const float packedViewX =
        static_cast<float>(viewX);
    const float packedViewY =
        static_cast<float>(viewY);
    const float packedViewWidth =
        static_cast<float>(viewWidth);
    const float packedViewHeight =
        static_cast<float>(viewHeight);

    region->destinationWidth =
        packedViewWidth * commandWidth;
    region->destinationHeight =
        packedViewHeight * commandHeight;
    region->sourceS0 =
        (packedViewX + commandS0 * packedViewWidth) /
        targetWidth;
    region->sourceT0 =
        (packedViewY + commandT0 * packedViewHeight) /
        targetHeight;
    region->sourceS1 =
        (packedViewX +
         (commandS0 + commandWidth) * packedViewWidth) /
        targetWidth;
    region->sourceT1 =
        (packedViewY +
         (commandT0 + commandHeight) * packedViewHeight) /
        targetHeight;
    return true;
}

} // namespace kisak::vr::saved_screen
