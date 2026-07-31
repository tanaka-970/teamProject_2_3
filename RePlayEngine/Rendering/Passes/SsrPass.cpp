#include "SsrPass.h"

#include "../../../Source/core/fullscreen_quad.h"
#include "../../../Source/core/shader.h"

#include <algorithm>

namespace ReplayEngine::Rendering
{
    bool SsrPass::Initialize(ID3D11Device* device, uint32_t width, uint32_t height,
        uint32_t resolution_divisor)
    {
        initialized_ = false;
        history_valid_ = false;
        trace_shader_.Reset();
        resolve_shader_.Reset();
        constants_.Reset();
        if (!device || width < 2 || height < 2) return false;

        const uint32_t divisor = (std::max)(1u, resolution_divisor);
        const uint32_t pass_width = (std::max)(2u, width / divisor);
        const uint32_t pass_height = (std::max)(2u, height / divisor);

        // 反射色はHDRのまま扱う。信頼度をアルファに入れるので4ch必要。
        constexpr DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (!trace_.Create(device, pass_width, pass_height, format)) return false;
        if (!resolved_.Create(device, pass_width, pass_height, format)) return false;
        // 履歴は lit テクスチャから CopyResource するため、必ずフル解像度。
        if (!history_.Create(device, width, height, format)) return false;

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Constants);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, constants_.GetAddressOf())))
            return false;

        states_.Initialize(device);

        create_ps_from_cso(device, "ssr_trace_ps.cso", trace_shader_.GetAddressOf());
        create_ps_from_cso(device, "ssr_resolve_ps.cso", resolve_shader_.GetAddressOf());

        initialized_ = trace_shader_ && resolve_shader_;
        return initialized_;
    }

    void SsrPass::UploadConstants(ID3D11DeviceContext* context)
    {
        Constants data{};
        data.params0 = { (std::max)(max_distance, 0.1f), (std::max)(thickness, 0.001f),
            (std::max)(stride, 1.0f), static_cast<float>(std::clamp(max_step, 4, 128)) };
        data.params1 = { std::clamp(max_roughness, 0.01f, 1.0f),
            std::clamp(intensity, 0.0f, 4.0f), (std::max)(edge_fade, 0.0001f),
            static_cast<float>(std::clamp(refine_step, 0, 8)) };
        data.params2 = { enabled ? 1.0f : 0.0f, (std::max)(resolve_radius, 0.0f),
            (std::max)(ray_bias, 0.0f), history_valid_ ? 1.0f : 0.0f };
        data.params3 = { static_cast<float>(std::clamp(resolve_tap_count, 1, 16)),
            4.0f, 0.0f, 0.0f };
        // 半解像度で走るときは、ステップ幅やresolve半径をこの解像度基準にする。
        const float target_width = static_cast<float>((std::max)(trace_.width, 1u));
        const float target_height = static_cast<float>((std::max)(trace_.height, 1u));
        data.target_size = { target_width, target_height,
            1.0f / target_width, 1.0f / target_height };
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        context->PSSetConstantBuffers(kConstantSlot, 1, constants_.GetAddressOf());
    }

    ID3D11ShaderResourceView* SsrPass::Execute(ID3D11DeviceContext* context,
        fullscreen_quad& quad,
        ID3D11ShaderResourceView* depth,
        ID3D11ShaderResourceView* world_normal,
        ID3D11ShaderResourceView* material)
    {
        if (!initialized_ || !context || !depth || !world_normal || !material) return nullptr;
        // 履歴が無い初回フレームは反射源が存在しないので何も出さない。
        if (!enabled || !history_valid_) return nullptr;

        // シェーダーレイヤーがADD/MULTIPLYを残していることがあるため、
        // 必ず不透明ブレンドへ戻してから描く。
        states_.ApplyOpaque(context);
        UploadConstants(context);

        ID3D11ShaderResourceView* trace_inputs[4]{
            depth, world_normal, material, history_.shader_resource_view.Get() };
        trace_.Activate(context);
        trace_.Clear(context, 0.0f, 0.0f, 0.0f, 0.0f);
        quad.blit(context, trace_inputs, 0, 4, trace_shader_.Get());

        ID3D11ShaderResourceView* null_views[4]{};
        context->PSSetShaderResources(0, 4, null_views);

        ID3D11ShaderResourceView* resolve_inputs[4]{
            trace_.shader_resource_view.Get(), depth, world_normal, material };
        resolved_.Activate(context);
        resolved_.Clear(context, 0.0f, 0.0f, 0.0f, 0.0f);
        quad.blit(context, resolve_inputs, 0, 4, resolve_shader_.Get());
        context->PSSetShaderResources(0, 4, null_views);

        ID3D11RenderTargetView* null_rtv[1]{};
        context->OMSetRenderTargets(1, null_rtv, nullptr);
        return resolved_.shader_resource_view.Get();
    }

    void SsrPass::CaptureHistory(ID3D11DeviceContext* context, ID3D11Resource* lit_color)
    {
        if (!initialized_ || !context || !lit_color || !history_.texture) return;
        // 書式とサイズが一致している前提でGPU内コピーする。
        context->CopyResource(history_.texture.Get(), lit_color);
        history_valid_ = true;
    }

    ID3D11ShaderResourceView* SsrPass::Output() const noexcept
    {
        if (!initialized_ || !enabled || !history_valid_) return nullptr;
        return resolved_.shader_resource_view.Get();
    }
}
