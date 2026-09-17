#pragma once

#include <cstdint>

namespace kisak::vr::postfx
{

struct FullSceneDepthQuad
{
    float x;
    float y;
    float width;
    float height;
};

inline FullSceneDepthQuad ResolveFullSceneDepthQuad(
    const bool sameFrameStereoEnabled,
    const float viewportX,
    const float viewportY,
    const float viewportWidth,
    const float viewportHeight)
{
    FullSceneDepthQuad quad = {
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight,
    };

    // R_DepthPrepass installs sceneViewport as the D3D viewport before this
    // 2D quad is drawn. Its vertices therefore use viewport-local coordinates;
    // retaining the packed target-space origin would apply that offset twice.
    if (sameFrameStereoEnabled)
    {
        quad.x = 0.0f;
        quad.y = 0.0f;
    }

    return quad;
}

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
