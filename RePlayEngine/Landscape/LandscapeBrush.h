#pragma once

namespace ReplayEngine::Landscape
{
    enum class LandscapeBrushMode
    {
        Raise,
        Lower,
        Smooth,
        Flatten,
        Noise,
    };

    enum class LandscapeSculptDirection
    {
        LocalY,
        VertexNormal,
    };

    struct LandscapeBrush
    {
        float radius = 4.0f;
        float strength = 2.0f;
        float falloff = 0.5f;
        float flatten_height = 0.0f;
        float noise_scale = 0.35f;
        LandscapeSculptDirection direction = LandscapeSculptDirection::LocalY;
    };
}
