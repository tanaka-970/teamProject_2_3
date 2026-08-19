#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

class fullscreen_quad;

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

        bool Initialize(ID3D11Device* device);
        void Execute(ID3D11DeviceContext* context, fullscreen_quad& quad,
            ID3D11ShaderResourceView* scene, ID3D11ShaderResourceView* bloom,
            float width, float height, bool bloom_enabled,
            bool vignette_enabled, bool fxaa_enabled) const;

        Settings& GetSettings() noexcept { return settings_; }
        const Settings& GetSettings() const noexcept { return settings_; }

    private:
        struct Constants
        {
            float exposure;
            float bloom_intensity;
            float vignette_strength;
            float fxaa_enable;
            float screen_size[2];
            float padding[2];
            DirectX::XMFLOAT4 color_filter;
        };

        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        Settings settings_;
    };
}
