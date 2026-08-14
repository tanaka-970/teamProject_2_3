#include "PostProcessPass.h"

#include "../../../Source/core/fullscreen_quad.h"
#include "../../../Source/core/shader.h"

namespace ReplayEngine::Rendering
{
    bool PostProcessPass::Initialize(ID3D11Device* device)
    {
        if (!device) return false;
        create_ps_from_cso(device, "final_pass_ps.cso", pixel_shader_.GetAddressOf());

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(Constants);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(device->CreateBuffer(&desc, nullptr, constants_.GetAddressOf()))
            && pixel_shader_ != nullptr;
    }

    void PostProcessPass::Execute(ID3D11DeviceContext* context, fullscreen_quad& quad,
        ID3D11ShaderResourceView* scene, ID3D11ShaderResourceView* bloom,
        float width, float height, bool bloom_enabled,
        bool vignette_enabled, bool fxaa_enabled) const
    {
        if (!context || !scene || !pixel_shader_ || !constants_) return;

        const Constants data{
            settings_.exposure,
            bloom_enabled ? settings_.bloom_intensity : 0.0f,
            vignette_enabled ? settings_.vignette_strength : 0.0f,
            fxaa_enabled ? settings_.fxaa_enable : 0.0f,
            { width, height }, { 0.0f, 0.0f },
            settings_.color_filter
        };
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        context->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
        ID3D11ShaderResourceView* resources[2]{ scene, bloom };
        quad.blit(context, resources, 0, 2, pixel_shader_.Get());
    }
}
