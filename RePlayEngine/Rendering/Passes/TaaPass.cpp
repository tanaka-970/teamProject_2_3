#include "TaaPass.h"

#include "../../../Source/core/fullscreen_quad.h"
#include "../../../Source/core/shader.h"

#include <algorithm>

namespace
{
    // Halton列。基数2と3で低食い違いなサブピクセル位置を作る。
    float Halton(uint32_t index, uint32_t base)
    {
        float result = 0.0f;
        float fraction = 1.0f / static_cast<float>(base);
        uint32_t current = index;
        while (current > 0)
        {
            result += static_cast<float>(current % base) * fraction;
            current /= base;
            fraction /= static_cast<float>(base);
        }
        return result;
    }
}

namespace ReplayEngine::Rendering
{
    bool TaaPass::Initialize(ID3D11Device* device, uint32_t width, uint32_t height)
    {
        initialized_ = false;
        history_valid_ = false;
        resolve_shader_.Reset();
        constants_.Reset();
        if (!device || width < 2 || height < 2) return false;

        // 合成はHDRのまま行う。トーンマップ前に掛けることで
        // 明部のエイリアスも正しく平均化される。
        constexpr DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (!resolved_.Create(device, width, height, format)) return false;
        if (!history_.Create(device, width, height, format)) return false;

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Constants);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, constants_.GetAddressOf())))
            return false;

        create_ps_from_cso(device, "taa_resolve_ps.cso", resolve_shader_.GetAddressOf());

        initialized_ = resolve_shader_ != nullptr;
        return initialized_;
    }

    DirectX::XMFLOAT2 TaaPass::CurrentJitter(uint32_t frame_index) const noexcept
    {
        if (!enabled) return { 0.0f, 0.0f };
        // Halton列は1始まりで使う(index=0は0になり、ジッターが効かない)。
        const uint32_t index = (frame_index % kJitterSampleCount) + 1;
        // [0,1) を [-0.5,0.5) へ寄せ、ピクセル単位のオフセットにする。
        const float offset_x = Halton(index, 2) - 0.5f;
        const float offset_y = Halton(index, 3) - 0.5f;
        return { offset_x, offset_y };
    }

    void TaaPass::UploadConstants(ID3D11DeviceContext* context)
    {
        Constants data{};
        data.params0 = { std::clamp(blend, 0.0f, 0.98f),
            (std::max)(variance_gamma, 0.0f), (std::max)(sharpness, 0.0f),
            enabled ? 1.0f : 0.0f };
        data.params1 = { history_valid_ ? 1.0f : 0.0f, 1.0f,
            (std::max)(max_velocity, 1.0f), 0.0f };
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        context->PSSetConstantBuffers(kConstantSlot, 1, constants_.GetAddressOf());
    }

    ID3D11ShaderResourceView* TaaPass::Execute(ID3D11DeviceContext* context,
        fullscreen_quad& quad,
        ID3D11ShaderResourceView* scene_color,
        ID3D11ShaderResourceView* depth,
        ID3D11ShaderResourceView* velocity)
    {
        if (!initialized_ || !context || !scene_color || !depth || !velocity)
            return scene_color;
        if (!enabled)
        {
            // 無効化中は履歴を捨てておく。再有効化した瞬間の残像を防ぐ。
            history_valid_ = false;
            return scene_color;
        }

        UploadConstants(context);

        ID3D11ShaderResourceView* inputs[4]{
            scene_color, history_.shader_resource_view.Get(), depth, velocity };
        resolved_.Activate(context);
        quad.blit(context, inputs, 0, 4, resolve_shader_.Get());

        ID3D11ShaderResourceView* null_views[4]{};
        context->PSSetShaderResources(0, 4, null_views);
        ID3D11RenderTargetView* null_rtv[1]{};
        context->OMSetRenderTargets(1, null_rtv, nullptr);

        // 次フレームの履歴として今フレームの結果を残す。
        context->CopyResource(history_.texture.Get(), resolved_.texture.Get());
        history_valid_ = true;

        return resolved_.shader_resource_view.Get();
    }
}
