#pragma once

#include <DirectXMath.h>

namespace ReplayEngine::Rendering
{
    class PostProcessPass final
    {
    public:
        struct Settings
        {
            float exposure = 0.619f;
            float bloom_intensity = 0.25f;
            float vignette_strength = 0.138f;
            float fxaa_enable = 1.0f;
            DirectX::XMFLOAT4 color_filter{ 1.0f, 1.0f, 1.0f, 1.0f };
        };
        Settings& GetSettings() noexcept { return settings_; }
        const Settings& GetSettings() const noexcept { return settings_; }
    private:
        Settings settings_{};
    };
}
