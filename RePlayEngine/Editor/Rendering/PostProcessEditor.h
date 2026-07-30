#pragma once

#include "../../Rendering/Passes/PostProcessPass.h"

namespace ReplayEngine::Editor
{
    class PostProcessEditor final
    {
    public:
        static void Draw(Rendering::PostProcessPass::Settings& settings,
            float& luminance_threshold, bool& luminance_enabled,
            bool& bloom_enabled, bool& vignette_enabled,
            bool& fxaa_enabled, bool& final_pass_enabled);
    };
}
