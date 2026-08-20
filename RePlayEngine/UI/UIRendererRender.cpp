// UIRenderer のうち「UI の描画補助、Flush、Render」を持つ。
//
// Effect Stack を持たない要素は従来の直接バッチ経路を通し、
// Effect を持つ要素だけが既存の offscreen 経路へ分岐する構造を変えない。
#include "UIRenderer.h"

#include "FontAtlas.h"
#include "UILayout.h"
#include "../Assets/AssetDatabase.h"
#include "../Components/UI/CanvasComponent.h"
#include "../Components/UI/RectTransformComponent.h"
#include "../Components/UI/UIImageComponent.h"
#include "../Components/UI/UIMaskComponent.h"
#include "../Components/UI/UIEffectStackComponent.h"
#include "../Components/UI/UISelectableComponent.h"
#include "../Components/UI/UIScrollViewComponent.h"
#include "../Components/UI/UIInputFieldComponent.h"
#include "../Components/UI/UIShapeComponent.h"
#include "../Components/UI/UIPuppetDeformComponent.h"
#include "../Components/UI/UITextComponent.h"
#include "../Components/UI/UITextAnimatorComponent.h"
#include "../Object/GameObject/GameObject.h"
#include "../Rendering/Shaders/ShaderCatalog.h"
#include "../Rendering/Effects/EffectChain.h"
#include "../Rendering/RenderStats.h"
#include "../Scene/Runtime/Scene.h"
#include "../../Source/core/shader.h"
#include "../../Source/core/texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ReplayEngine::UI
{
    namespace
    {
        using Components::CanvasComponent;
        using Components::RectTransformComponent;
        using Components::UIImageComponent;
        using Components::UIMaskComponent;
        using Components::UIEffectStackComponent;
        using Components::UISelectableComponent;
        using Components::UIScrollViewComponent;
        using Components::UIInputFieldComponent;
        using Components::UIShapeComponent;
        using Components::UIPuppetDeformComponent;
        using Components::UITextComponent;
        using Components::UITextAnimatorComponent;

        constexpr int maximum_ui_depth = 64;

        // Phase 1 の背景取り込みは、現在の描画先から切り出す領域だけを表す。
        // Pool の format を増やさず、検証できない描画先は caller で Legacy へ戻す。
        struct BackdropCapturePlan
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> source_texture;
            D3D11_BOX source_box{};
            D3D11_RECT output_rect{};
            std::uint32_t destination_x = 0;
            std::uint32_t destination_y = 0;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
        };

        DirectX::XMFLOAT2 TransformPoint(const DirectX::XMFLOAT4X4& matrix,
            float x, float y) noexcept
        {
            const DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&matrix);
            const DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(
                DirectX::XMVectorSet(x, y, 0.0f, 1.0f), m);
            return { DirectX::XMVectorGetX(p), DirectX::XMVectorGetY(p) };
        }

        DirectX::XMFLOAT2 ToScreenPoint(const DirectX::XMFLOAT2& canvas_point,
            float scale, float screen_height) noexcept
        {
            return { canvas_point.x * scale, screen_height - canvas_point.y * scale };
        }

        DirectX::XMFLOAT4 MultiplyAlpha(DirectX::XMFLOAT4 color, float alpha) noexcept
        {
            color.w *= alpha;
            return color;
        }

        D3D11_RECT MakeScissor(const RectTransformComponent& rect,
            float scale, float screen_width, float screen_height) noexcept
        {
            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            const DirectX::XMFLOAT4X4 m = rect.ResolvedMatrix();
            const DirectX::XMFLOAT2 p0 = ToScreenPoint(TransformPoint(m, r.x, r.y), scale, screen_height);
            const DirectX::XMFLOAT2 p1 = ToScreenPoint(TransformPoint(m, r.x + r.z, r.y), scale, screen_height);
            const DirectX::XMFLOAT2 p2 = ToScreenPoint(TransformPoint(m, r.x + r.z, r.y + r.w), scale, screen_height);
            const DirectX::XMFLOAT2 p3 = ToScreenPoint(TransformPoint(m, r.x, r.y + r.w), scale, screen_height);

            const float min_x = (std::max)(0.0f, (std::min)({ p0.x, p1.x, p2.x, p3.x }));
            const float max_x = (std::min)(screen_width, (std::max)({ p0.x, p1.x, p2.x, p3.x }));
            const float min_y = (std::max)(0.0f, (std::min)({ p0.y, p1.y, p2.y, p3.y }));
            const float max_y = (std::min)(screen_height, (std::max)({ p0.y, p1.y, p2.y, p3.y }));

            D3D11_RECT scissor{};
            scissor.left = static_cast<LONG>(std::floor(min_x));
            scissor.top = static_cast<LONG>(std::floor(min_y));
            scissor.right = static_cast<LONG>(std::ceil(max_x));
            scissor.bottom = static_cast<LONG>(std::ceil(max_y));
            return scissor;
        }

        D3D11_RECT IntersectScissor(const D3D11_RECT& a, const D3D11_RECT& b) noexcept
        {
            D3D11_RECT out{};
            out.left = (std::max)(a.left, b.left);
            out.top = (std::max)(a.top, b.top);
            out.right = (std::min)(a.right, b.right);
            out.bottom = (std::min)(a.bottom, b.bottom);
            if (out.right < out.left) out.right = out.left;
            if (out.bottom < out.top) out.bottom = out.top;
            return out;
        }

        ID3D11BlendState* BlendForImage(const UIImageComponent& image,
            const UIRenderer::RenderStates& states) noexcept
        {
            switch (image.blend_mode)
            {
            case UIImageComponent::Additive: return states.blend_add;
            case UIImageComponent::Multiply: return states.blend_multiply;
            case UIImageComponent::Screen:   return states.blend_screen;
            default:                         return states.blend_alpha;
            }
        }

        float Clamp01(float value) noexcept
        {
            return (std::max)(0.0f, (std::min)(1.0f, value));
        }

        float Lerp(float a, float b, float t) noexcept
        {
            return a + (b - a) * t;
        }

        DirectX::XMFLOAT4 LerpColor(const DirectX::XMFLOAT4& a,
            const DirectX::XMFLOAT4& b, float t) noexcept
        {
            return {
                Lerp(a.x, b.x, t),
                Lerp(a.y, b.y, t),
                Lerp(a.z, b.z, t),
                Lerp(a.w, b.w, t)
            };
        }

        float SmoothStep(float value) noexcept
        {
            value = Clamp01(value);
            return value * value * (3.0f - 2.0f * value);
        }

        float RangeInfluence(const UITextAnimatorComponent& animator,
            float position) noexcept
        {
            const float start = animator.range_start + animator.range_offset;
            const float end = animator.range_end + animator.range_offset;
            const float low = (std::min)(start, end);
            const float high = (std::max)(start, end);
            const float width = (std::max)(0.0001f, high - low);
            if (position < low || position > high) return 0.0f;

            const float t = Clamp01((position - low) / width);
            float influence = 1.0f;
            switch (animator.range_shape)
            {
            case UITextAnimatorComponent::RampUp:
                influence = t;
                break;
            case UITextAnimatorComponent::RampDown:
                influence = 1.0f - t;
                break;
            case UITextAnimatorComponent::Triangle:
                influence = 1.0f - std::fabs(t * 2.0f - 1.0f);
                break;
            case UITextAnimatorComponent::Round:
            {
                const float centered = t * 2.0f - 1.0f;
                influence = std::sqrt(Clamp01(1.0f - centered * centered));
                break;
            }
            case UITextAnimatorComponent::Smooth:
                influence = SmoothStep(t);
                break;
            default:
                influence = 1.0f;
                break;
            }

            const float smoothness = Clamp01(animator.range_smoothness);
            if (smoothness > 0.0f)
            {
                const float edge = (std::min)(0.5f, smoothness * 0.5f);
                const float in_edge = edge > 0.0f ? SmoothStep(t / edge) : 1.0f;
                const float out_edge = edge > 0.0f ? SmoothStep((1.0f - t) / edge) : 1.0f;
                influence *= (std::min)(in_edge, out_edge);
            }
            return Clamp01(influence);
        }

        std::uint32_t HashGlyph(int seed, int character_index,
            std::uint32_t salt) noexcept
        {
            std::uint32_t value = static_cast<std::uint32_t>(seed);
            value ^= static_cast<std::uint32_t>(character_index) + 0x9e3779b9u +
                (value << 6) + (value >> 2);
            value ^= salt;
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }

        float RandomSigned(int seed, int character_index,
            std::uint32_t salt) noexcept
        {
            const std::uint32_t value = HashGlyph(seed, character_index, salt);
            return (static_cast<float>(value & 0x00ffffffu) / 8388607.5f) - 1.0f;
        }

        void GatherTextAnimators(const Core::GameObject& object,
            std::vector<const UITextAnimatorComponent*>& out)
        {
            out.clear();
            for (std::size_t index = 0; index < object.ComponentCount(); ++index)
            {
                const auto* animator = dynamic_cast<const UITextAnimatorComponent*>(
                    object.ComponentAt(index));
                if (animator != nullptr && animator->ActiveInHierarchy())
                {
                    out.push_back(animator);
                }
            }
        }
    }
    void UIRenderer::Flush(ID3D11DeviceContext* context, ID3D11ShaderResourceView* texture,
        ID3D11BlendState* blend_state, const RenderStates& states,
        const D3D11_RECT* scissor, ID3D11PixelShader* pixel_shader_override,
        ID3D11Buffer* pixel_constant_buffer)
    {
        if (context == nullptr || vertices_.empty() || device_ == nullptr) return;
        if (!EnsureVertexCapacity(device_.Get(), vertices_.size())) return;

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(vertex_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            vertices_.clear();
            return;
        }
        std::memcpy(mapped.pData, vertices_.data(), sizeof(Vertex) * vertices_.size());
        context->Unmap(vertex_buffer_.Get(), 0);

        const UINT stride = sizeof(Vertex);
        const UINT offset = 0;
        ID3D11Buffer* vb = vertex_buffer_.Get();
        context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        Rendering::Stats().TrackStateSet(
            Rendering::RenderStats::StateKind::InputLayout, input_layout_.Get());
        context->IASetInputLayout(input_layout_.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
        context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Shader, false);
        context->PSSetShader(pixel_shader_override != nullptr
            ? pixel_shader_override : pixel_shader_.Get(), nullptr, 0);
        ID3D11Buffer* cb = constant_buffer_.Get();
        context->VSSetConstantBuffers(0, 1, &cb);
        if (pixel_constant_buffer == nullptr && visual_constant_buffer_ != nullptr)
        {
            context->UpdateSubresource(visual_constant_buffer_.Get(), 0, nullptr,
                &visual_constants_, 0, 0);
        }
        ID3D11Buffer* ps_cb = pixel_constant_buffer != nullptr
            ? pixel_constant_buffer : visual_constant_buffer_.Get();
        if (ps_cb != nullptr)
        {
            context->PSSetConstantBuffers(0, 1, &ps_cb);
        }
        ID3D11ShaderResourceView* srv = texture != nullptr ? texture : white_texture_.Get();
        context->PSSetShaderResources(0, 1, &srv);
        ID3D11SamplerState* sampler = states.sampler;
        context->PSSetSamplers(0, 1, &sampler);
        ID3D11DepthStencilState* selected_depth = world_space_canvas_ &&
            states.depth_enabled != nullptr ? states.depth_enabled : states.depth_disabled;
        Rendering::Stats().TrackStateSet(
            Rendering::RenderStats::StateKind::DepthStencil, selected_depth);
        context->OMSetDepthStencilState(selected_depth, 0);
        ID3D11BlendState* selected_blend =
            blend_state != nullptr ? blend_state : states.blend_alpha;
        Rendering::Stats().TrackStateSet(
            Rendering::RenderStats::StateKind::Blend, selected_blend);
        context->OMSetBlendState(selected_blend, nullptr, 0xFFFFFFFF);

        if (scissor != nullptr)
        {
            ID3D11RasterizerState* selected_rasterizer =
                states.rasterizer_scissor != nullptr
                    ? states.rasterizer_scissor : states.rasterizer;
            Rendering::Stats().TrackStateSet(
                Rendering::RenderStats::StateKind::Rasterizer,
                selected_rasterizer);
            context->RSSetState(selected_rasterizer);
            const float scale_x = states.viewport_scale_x > 0.0001f
                ? states.viewport_scale_x : 1.0f;
            const float scale_y = states.viewport_scale_y > 0.0001f
                ? states.viewport_scale_y : 1.0f;
            D3D11_RECT target_scissor{};
            // D3D11 scissor は render target 座標なので、Scene View viewport の左上と
            // 表示倍率を反映する。論理解像度だけを変えると Mask がずれる。
            target_scissor.left = static_cast<LONG>(
                std::floor(states.scissor_offset_x + scissor->left * scale_x));
            target_scissor.top = static_cast<LONG>(
                std::floor(states.scissor_offset_y + scissor->top * scale_y));
            target_scissor.right = static_cast<LONG>(
                std::ceil(states.scissor_offset_x + scissor->right * scale_x));
            target_scissor.bottom = static_cast<LONG>(
                std::ceil(states.scissor_offset_y + scissor->bottom * scale_y));
            if (states.scissor_bounds_enabled)
                target_scissor = IntersectScissor(target_scissor, states.scissor_bounds);
            context->RSSetScissorRects(1, &target_scissor);
        }
        else
        {
            Rendering::Stats().TrackStateSet(
                Rendering::RenderStats::StateKind::Rasterizer,
                states.rasterizer);
            context->RSSetState(states.rasterizer);
            context->RSSetScissorRects(0, nullptr);
        }

        Rendering::Stats().CountDraw(static_cast<std::uint32_t>(vertices_.size()));
        context->Draw(static_cast<UINT>(vertices_.size()), 0);
        vertices_.clear();
    }
    void UIRenderer::Render(ID3D11DeviceContext* context,
        Scene::Scene& scene,
        const Assets::AssetDatabase* asset_database,
        const Rendering::ShaderCatalog* shader_catalog,
        FontAtlas& font_atlas,
        float screen_width,
        float screen_height,
        float effect_time,
        const RenderStates& states)
    {
        // Render() の処理順・ローカル変数の寿命を変えず、連続断片を内部 .inl へ移動する。
#include "UIRendererRenderSetup.inl"
#include "UIRendererRenderTextInput.inl"
#include "UIRendererRenderBackdrop.inl"
#include "UIRendererRenderEffects.inl"
#include "UIRendererRenderHierarchy.inl"
    }
}
