#pragma once

#include <cstdint>

namespace ReplayEngine::Rendering
{
    class SsaoPass final
    {
    public:
        bool Initialized() const noexcept { return true; }
        float radius = 0.75f;
        float intensity = 1.0f;
        float power = 1.6f;
        float thin_occluder = 1.0f;
        int slice_count = 4;
        int step_count = 8;
        float fade_start = 60.0f;
        float fade_end = 140.0f;
        float normal_bias = 0.35f;
        float blur_sharpness = 1.0f;
        bool enabled = true;
        bool blur_enabled = true;
    };
}
