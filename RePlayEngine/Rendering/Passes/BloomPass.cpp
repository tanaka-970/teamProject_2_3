#include "BloomPass.h"

#include "../../../Source/core/framebuffer.h"
#include "../../../Source/core/fullscreen_quad.h"
#include "../../../Source/core/shader.h"

#include <algorithm>

namespace ReplayEngine::Rendering
{
    BloomPass::BloomPass() = default;
    BloomPass::~BloomPass() = default;

    bool BloomPass::Initialize(ID3D11Device* device, uint32_t width, uint32_t height)
    {
        if (!device || width < 2 || height < 2) return false;
        initialized_ = false;
        extraction_shader_.Reset();
        downsample_shader_.Reset();
        upsample_shader_.Reset();
        constants_.Reset();
        additive_blend_.Reset();
        opaque_blend_.Reset();
        for (size_t i = 0; i < kLevelCount; ++i)
        {
            widths_[i] = (std::max)(1u, width >> (i + 1));
            heights_[i] = (std::max)(1u, height >> (i + 1));
            levels_[i] = std::make_unique<framebuffer>(device, widths_[i], heights_[i]);
        }
        create_ps_from_cso(device, "luminance_extraction_ps.cso", extraction_shader_.GetAddressOf());
        create_ps_from_cso(device, "gaussian_blur_downsample_ps.cso", downsample_shader_.GetAddressOf());
        create_ps_from_cso(device, "gaussian_blur_upsample_ps.cso", upsample_shader_.GetAddressOf());

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Constants);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, constants_.GetAddressOf()))) return false;

        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blend, opaque_blend_.GetAddressOf()))) return false;
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
        blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (FAILED(device->CreateBlendState(&blend, additive_blend_.GetAddressOf()))) return false;
        initialized_ = extraction_shader_ && downsample_shader_ && upsample_shader_;
        return initialized_;
    }

    ID3D11ShaderResourceView* BloomPass::Execute(ID3D11DeviceContext* context,
        fullscreen_quad& quad, ID3D11ShaderResourceView* scene_color)
    {
        if (!initialized_ || !context || !scene_color) return nullptr;
        ID3D11ShaderResourceView* null_view = nullptr;
        context->OMSetBlendState(opaque_blend_.Get(), nullptr, 0xffffffff);

        Constants data{ threshold, 0, 0, 0 };
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        context->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
        levels_[0]->clear(context, 0, 0, 0, 1);
        levels_[0]->activate(context);
        quad.blit(context, &scene_color, 0, 1, extraction_shader_.Get());
        levels_[0]->deactivate(context);
        context->PSSetShaderResources(0, 1, &null_view);

        for (size_t i = 1; i < kLevelCount; ++i)
        {
            data = { 1.0f / static_cast<float>(widths_[i - 1]),
                1.0f / static_cast<float>(heights_[i - 1]), 0, 0 };
            context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
            ID3D11ShaderResourceView* source = levels_[i - 1]->shader_resource_views[0].Get();
            levels_[i]->clear(context, 0, 0, 0, 1);
            levels_[i]->activate(context);
            quad.blit(context, &source, 0, 1, downsample_shader_.Get());
            levels_[i]->deactivate(context);
            context->PSSetShaderResources(0, 1, &null_view);
        }

        context->OMSetBlendState(additive_blend_.Get(), nullptr, 0xffffffff);
        for (size_t i = kLevelCount - 1; i > 0; --i)
        {
            data = { 1.0f / static_cast<float>(widths_[i]),
                1.0f / static_cast<float>(heights_[i]), 0, 0 };
            context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
            ID3D11ShaderResourceView* source = levels_[i]->shader_resource_views[0].Get();
            levels_[i - 1]->activate(context);
            quad.blit(context, &source, 0, 1, upsample_shader_.Get());
            levels_[i - 1]->deactivate(context);
            context->PSSetShaderResources(0, 1, &null_view);
        }
        context->OMSetBlendState(opaque_blend_.Get(), nullptr, 0xffffffff);
        return Output();
    }

    ID3D11ShaderResourceView* BloomPass::Output() const noexcept
    {
        return initialized_ ? levels_[0]->shader_resource_views[0].Get() : nullptr;
    }
}
