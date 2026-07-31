#include "SsaoPass.h"

#include "../../../Source/core/fullscreen_quad.h"
#include "../../../Source/core/shader.h"

#include <algorithm>

namespace ReplayEngine::Rendering
{
    bool SsaoPass::Initialize(ID3D11Device* device, uint32_t width, uint32_t height,
        uint32_t resolution_divisor)
    {
        initialized_ = false;
        occlusion_shader_.Reset();
        blur_shader_.Reset();
        constants_.Reset();
        if (!device || width < 2 || height < 2) return false;

        // AOは低周波なので半解像度でも見た目がほとんど変わらず、
        // ピクセルシェーダーの負荷が1/4になる。フルスクリーンパスが
        // 積み上がるとPS実行回数が支配的になるため効果が大きい。
        const uint32_t divisor = (std::max)(1u, resolution_divisor);
        const uint32_t pass_width = (std::max)(2u, width / divisor);
        const uint32_t pass_height = (std::max)(2u, height / divisor);

        // R=可視性, G=ビュー空間深度。ブラーが深度差を見るため2ch必要。
        constexpr DXGI_FORMAT format = DXGI_FORMAT_R16G16_FLOAT;
        if (!occlusion_.Create(device, pass_width, pass_height, format)) return false;
        if (!blur_.Create(device, pass_width, pass_height, format)) return false;
        if (!resolved_.Create(device, pass_width, pass_height, format)) return false;

        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(Constants);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, constants_.GetAddressOf())))
            return false;

        states_.Initialize(device);

        create_ps_from_cso(device, "ssao_ps.cso", occlusion_shader_.GetAddressOf());
        create_ps_from_cso(device, "ssao_blur_ps.cso", blur_shader_.GetAddressOf());

        initialized_ = occlusion_shader_ && blur_shader_;
        return initialized_;
    }

    void SsaoPass::UploadConstants(ID3D11DeviceContext* context,
        float blur_direction_x, float blur_direction_y)
    {
        Constants data{};
        data.params0 = { (std::max)(radius, 0.001f), std::clamp(intensity, 0.0f, 1.0f),
            (std::max)(power, 0.01f), std::clamp(thin_occluder, 0.0f, 1.0f) };
        data.params1 = { static_cast<float>(std::clamp(slice_count, 1, 16)),
            static_cast<float>(std::clamp(step_count, 1, 32)), 96.0f, 2.0f };
        data.params2 = { (std::max)(fade_start, 0.0f),
            (std::max)(fade_end, fade_start + 1.0f), blur_direction_x, blur_direction_y };
        data.params3 = { (std::max)(blur_sharpness, 0.01f), normal_bias,
            enabled ? 1.0f : 0.0f, 0.0f };
        // 半解像度で走るときは、ピクセル半径やタップ間隔をこの解像度基準にする。
        const float target_width = static_cast<float>((std::max)(occlusion_.width, 1u));
        const float target_height = static_cast<float>((std::max)(occlusion_.height, 1u));
        data.target_size = { target_width, target_height,
            1.0f / target_width, 1.0f / target_height };
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &data, 0, 0);
        context->PSSetConstantBuffers(kConstantSlot, 1, constants_.GetAddressOf());
    }

    ID3D11ShaderResourceView* SsaoPass::Execute(ID3D11DeviceContext* context,
        fullscreen_quad& quad,
        ID3D11ShaderResourceView* depth,
        ID3D11ShaderResourceView* world_normal)
    {
        if (!initialized_ || !context || !depth || !world_normal) return nullptr;

        // シェーダーレイヤーがADD/MULTIPLYを残していることがあるため、
        // 必ず不透明ブレンドへ戻してから描く。
        states_.ApplyOpaque(context);

        // 遮蔽率の生成。深度とG-Buffer法線を t0/t1 に置く。
        UploadConstants(context, 1.0f, 0.0f);
        ID3D11ShaderResourceView* inputs[2]{ depth, world_normal };
        occlusion_.Activate(context);
        occlusion_.Clear(context, 1.0f, 0.0f, 0.0f, 0.0f);
        quad.blit(context, inputs, 0, 2, occlusion_shader_.Get());

        ID3D11ShaderResourceView* null_views[2]{};
        context->PSSetShaderResources(0, 2, null_views);

        if (!blur_enabled)
        {
            // ブラー無しでも出力先を揃えたいので、生AOをそのまま返す。
            ID3D11RenderTargetView* null_rtv[1]{};
            context->OMSetRenderTargets(1, null_rtv, nullptr);
            return occlusion_.shader_resource_view.Get();
        }

        // 横方向ブラー。
        ID3D11ShaderResourceView* horizontal_input[1]{ occlusion_.shader_resource_view.Get() };
        blur_.Activate(context);
        quad.blit(context, horizontal_input, 0, 1, blur_shader_.Get());
        context->PSSetShaderResources(0, 1, null_views);

        // 縦方向ブラー。
        UploadConstants(context, 0.0f, 1.0f);
        ID3D11ShaderResourceView* vertical_input[1]{ blur_.shader_resource_view.Get() };
        resolved_.Activate(context);
        quad.blit(context, vertical_input, 0, 1, blur_shader_.Get());
        context->PSSetShaderResources(0, 1, null_views);

        ID3D11RenderTargetView* null_rtv[1]{};
        context->OMSetRenderTargets(1, null_rtv, nullptr);
        return resolved_.shader_resource_view.Get();
    }

    ID3D11ShaderResourceView* SsaoPass::Output() const noexcept
    {
        if (!initialized_) return nullptr;
        return blur_enabled
            ? resolved_.shader_resource_view.Get()
            : occlusion_.shader_resource_view.Get();
    }
}
