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
#include "../Components/UI/UITextComponent.h"
#include "../Components/UI/UITextAnimatorComponent.h"
#include "../Object/GameObject/GameObject.h"
#include "../Rendering/Shaders/ShaderCatalog.h"
#include "../Rendering/Effects/EffectChain.h"
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
        context->IASetInputLayout(input_layout_.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
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
        context->OMSetDepthStencilState(world_space_canvas_ &&
            states.depth_enabled != nullptr ? states.depth_enabled : states.depth_disabled, 0);
        context->OMSetBlendState(blend_state != nullptr ? blend_state : states.blend_alpha,
            nullptr, 0xFFFFFFFF);

        if (scissor != nullptr)
        {
            context->RSSetState(states.rasterizer_scissor != nullptr
                ? states.rasterizer_scissor : states.rasterizer);
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
            context->RSSetState(states.rasterizer);
            context->RSSetScissorRects(0, nullptr);
        }

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
        if (context == nullptr || device_ == nullptr || !vertex_shader_ || !pixel_shader_)
            return;

        Constants constants{};
        constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
        constants.world_view_projection = states.world_view_projection;
        context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);
        render_target_pool_.BeginFrame();

        std::vector<Core::GameObject*> canvases;
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            Core::GameObject* object = scene.GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;
            if (object->GetComponent<CanvasComponent>() != nullptr) canvases.push_back(object);
        }
        std::stable_sort(canvases.begin(), canvases.end(),
            [](const Core::GameObject* lhs, const Core::GameObject* rhs)
            {
                const CanvasComponent* a = lhs != nullptr
                    ? lhs->GetComponent<CanvasComponent>() : nullptr;
                const CanvasComponent* b = rhs != nullptr
                    ? rhs->GetComponent<CanvasComponent>() : nullptr;
                return (a != nullptr ? a->sort_order : 0) <
                    (b != nullptr ? b->sort_order : 0);
            });

        float draw_target_height = screen_height;
        visual_constants_ = VisualConstants{};
        const auto clamp_outline_width = [](float outline_width) noexcept
        {
            const float non_negative = (std::max)(outline_width, 0.0f);
            return (std::min)(non_negative,
                static_cast<float>(FontAtlas::AtlasPaddingPixels()));
        };
        const auto configure_visual = [this, &clamp_outline_width](
            const DirectX::XMFLOAT4& fill_color_2,
            int fill_mode, float fill_angle, const DirectX::XMFLOAT2& fill_center,
            const DirectX::XMFLOAT4& stroke_color_2, int stroke_mode,
            bool text_mode, float outline_width,
            const DirectX::XMFLOAT4& outline_color,
            const DirectX::XMFLOAT2& shadow_offset,
            const DirectX::XMFLOAT4& shadow_color)
        {
            visual_constants_.fill_color_2 = fill_color_2;
            visual_constants_.fill_params = {
                static_cast<float>(fill_mode),
                DirectX::XMConvertToRadians(fill_angle),
                fill_center.x, fill_center.y };
            visual_constants_.stroke_color_2 = stroke_color_2;
            visual_constants_.stroke_params = {
                static_cast<float>(stroke_mode), clamp_outline_width(outline_width),
                text_mode ? 1.0f : 0.0f, 0.0f };
            visual_constants_.outline_color = outline_color;
            visual_constants_.shadow_offset = { shadow_offset.x, shadow_offset.y,
                0.0f, 0.0f };
            visual_constants_.shadow_color = shadow_color;
            // 3・4 色目は Shape が明示した描画だけで有効にする。
            // ここで戻さないと、直前の Shape の多色設定が Image / Text へ漏れる。
            visual_constants_.fill_color_3 = { 1.0f, 1.0f, 1.0f, 1.0f };
            visual_constants_.fill_color_4 = { 1.0f, 1.0f, 1.0f, 1.0f };
            visual_constants_.fill_stops = { 1.0f, -1.0f, -1.0f, 0.0f };
        };

        const auto emit_quad = [this, &draw_target_height](const DirectX::XMFLOAT4& rect,
            const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& uv,
            const DirectX::XMFLOAT4& color, float scale,
            const DirectX::XMFLOAT4& uv_bounds)
        {
            const DirectX::XMFLOAT2 p0 = ToScreenPoint(
                TransformPoint(matrix, rect.x, rect.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p1 = ToScreenPoint(
                TransformPoint(matrix, rect.x + rect.z, rect.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p2 = ToScreenPoint(
                TransformPoint(matrix, rect.x + rect.z, rect.y + rect.w), scale, draw_target_height);
            const DirectX::XMFLOAT2 p3 = ToScreenPoint(
                TransformPoint(matrix, rect.x, rect.y + rect.w), scale, draw_target_height);

            const DirectX::XMFLOAT2 uv0{ uv.x, uv.y + uv.w };
            const DirectX::XMFLOAT2 uv1{ uv.x + uv.z, uv.y + uv.w };
            const DirectX::XMFLOAT2 uv2{ uv.x + uv.z, uv.y };
            const DirectX::XMFLOAT2 uv3{ uv.x, uv.y };
            vertices_.push_back({ p0, uv0, { 0.0f, 1.0f }, color, uv_bounds });
            vertices_.push_back({ p3, uv3, { 0.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p2, uv2, { 1.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p0, uv0, { 0.0f, 1.0f }, color, uv_bounds });
            vertices_.push_back({ p2, uv2, { 1.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p1, uv1, { 1.0f, 1.0f }, color, uv_bounds });
        };

        const auto append_quad = [&emit_quad](const DirectX::XMFLOAT4& rect,
            const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& uv,
            const DirectX::XMFLOAT4& color, float scale)
        {
            const DirectX::XMFLOAT4 uv_bounds{
                uv.x, uv.y, uv.x + uv.z, uv.y + uv.w };
            emit_quad(rect, matrix, uv, color, scale, uv_bounds);
        };

        const auto append_quad_with_bounds = [&emit_quad](
            const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
            const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color,
            float scale, const DirectX::XMFLOAT4& uv_bounds)
        {
            emit_quad(rect, matrix, uv, color, scale, uv_bounds);
        };

        const auto emit_quad_local =
            [this, &draw_target_height](const DirectX::XMFLOAT4& rect,
                const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& uv,
                const DirectX::XMFLOAT4& color, float scale,
                const DirectX::XMFLOAT2& local_scale, float rotation_degrees,
                const DirectX::XMFLOAT2& anchor, float shear_x,
                const DirectX::XMFLOAT4& uv_bounds)
        {
            const float pivot_x = rect.x + rect.z * anchor.x;
            const float pivot_y = rect.y + rect.w * anchor.y;
            const float radians = DirectX::XMConvertToRadians(rotation_degrees);
            const float c = std::cos(radians);
            const float s = std::sin(radians);

            const auto transform_local = [&](float x, float y)
            {
                float dx = (x - pivot_x) * local_scale.x;
                float dy = (y - pivot_y) * local_scale.y;
                dx += dy * shear_x;
                const float rx = dx * c - dy * s + pivot_x;
                const float ry = dx * s + dy * c + pivot_y;
                return ToScreenPoint(TransformPoint(matrix, rx, ry), scale,
                    draw_target_height);
            };

            const DirectX::XMFLOAT2 p0 = transform_local(rect.x, rect.y);
            const DirectX::XMFLOAT2 p1 = transform_local(rect.x + rect.z, rect.y);
            const DirectX::XMFLOAT2 p2 = transform_local(rect.x + rect.z, rect.y + rect.w);
            const DirectX::XMFLOAT2 p3 = transform_local(rect.x, rect.y + rect.w);

            const DirectX::XMFLOAT2 uv0{ uv.x, uv.y + uv.w };
            const DirectX::XMFLOAT2 uv1{ uv.x + uv.z, uv.y + uv.w };
            const DirectX::XMFLOAT2 uv2{ uv.x + uv.z, uv.y };
            const DirectX::XMFLOAT2 uv3{ uv.x, uv.y };
            vertices_.push_back({ p0, uv0, { 0.0f, 1.0f }, color, uv_bounds });
            vertices_.push_back({ p3, uv3, { 0.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p2, uv2, { 1.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p0, uv0, { 0.0f, 1.0f }, color, uv_bounds });
            vertices_.push_back({ p2, uv2, { 1.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p1, uv1, { 1.0f, 1.0f }, color, uv_bounds });
        };

        const auto append_quad_local = [&emit_quad_local](
            const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
            const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color, float scale,
            const DirectX::XMFLOAT2& local_scale, float rotation_degrees,
            const DirectX::XMFLOAT2& anchor)
        {
            const DirectX::XMFLOAT4 uv_bounds{
                uv.x, uv.y, uv.x + uv.z, uv.y + uv.w };
            emit_quad_local(rect, matrix, uv, color, scale, local_scale,
                rotation_degrees, anchor, 0.0f, uv_bounds);
        };

        const auto append_quad_local_with_bounds = [&emit_quad_local](
            const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
            const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color, float scale,
            const DirectX::XMFLOAT2& local_scale, float rotation_degrees,
            const DirectX::XMFLOAT2& anchor, float shear_x,
            const DirectX::XMFLOAT4& uv_bounds)
        {
            emit_quad_local(rect, matrix, uv, color, scale, local_scale,
                rotation_degrees, anchor, shear_x, uv_bounds);
        };

        const auto append_triangle_local =
            [this, &draw_target_height](const DirectX::XMFLOAT2& a,
                const DirectX::XMFLOAT2& b, const DirectX::XMFLOAT2& c,
                const DirectX::XMFLOAT4& bounds, const DirectX::XMFLOAT4X4& matrix,
                const DirectX::XMFLOAT4& color, float scale)
        {
            const DirectX::XMFLOAT2 uv{ 0.0f, 0.0f };
            const float width = (std::max)(0.0001f, std::fabs(bounds.z));
            const float height = (std::max)(0.0001f, std::fabs(bounds.w));
            const auto gradient_uv = [&bounds, width, height](
                const DirectX::XMFLOAT2& point)
            {
                return DirectX::XMFLOAT2{
                    (point.x - bounds.x) / width,
                    (point.y - bounds.y) / height };
            };
            const DirectX::XMFLOAT2 uv0 = gradient_uv(a);
            const DirectX::XMFLOAT2 uv1 = gradient_uv(b);
            const DirectX::XMFLOAT2 uv2 = gradient_uv(c);
            const DirectX::XMFLOAT2 p0 = ToScreenPoint(
                TransformPoint(matrix, a.x, a.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p1 = ToScreenPoint(
                TransformPoint(matrix, b.x, b.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p2 = ToScreenPoint(
                TransformPoint(matrix, c.x, c.y), scale, draw_target_height);
            vertices_.push_back({ p0, uv, uv0, color });
            vertices_.push_back({ p1, uv, uv1, color });
            vertices_.push_back({ p2, uv, uv2, color });
        };

        const auto append_line_segment_local =
            [this, &draw_target_height](const DirectX::XMFLOAT2& a,
                const DirectX::XMFLOAT2& b, const DirectX::XMFLOAT4X4& matrix,
                const DirectX::XMFLOAT4& color, float width, float scale,
                float gradient_u0, float gradient_u1)
        {
            const float pixel_width = width * scale;
            if (pixel_width <= 0.0f || color.w <= 0.0f) return;

            const DirectX::XMFLOAT2 p0 = ToScreenPoint(
                TransformPoint(matrix, a.x, a.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p1 = ToScreenPoint(
                TransformPoint(matrix, b.x, b.y), scale, draw_target_height);
            const float dx = p1.x - p0.x;
            const float dy = p1.y - p0.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0.001f) return;

            const float nx = -dy / length * pixel_width * 0.5f;
            const float ny = dx / length * pixel_width * 0.5f;
            const DirectX::XMFLOAT2 uv{ 0.0f, 0.0f };
            const DirectX::XMFLOAT2 q0{ p0.x - nx, p0.y - ny };
            const DirectX::XMFLOAT2 q1{ p0.x + nx, p0.y + ny };
            const DirectX::XMFLOAT2 q2{ p1.x + nx, p1.y + ny };
            const DirectX::XMFLOAT2 q3{ p1.x - nx, p1.y - ny };

            vertices_.push_back({ q0, uv, { gradient_u0, 0.0f }, color });
            vertices_.push_back({ q1, uv, { gradient_u0, 1.0f }, color });
            vertices_.push_back({ q2, uv, { gradient_u1, 1.0f }, color });
            vertices_.push_back({ q0, uv, { gradient_u0, 0.0f }, color });
            vertices_.push_back({ q2, uv, { gradient_u1, 1.0f }, color });
            vertices_.push_back({ q3, uv, { gradient_u1, 0.0f }, color });
        };

        const auto local_distance = [](const DirectX::XMFLOAT2& a,
            const DirectX::XMFLOAT2& b) noexcept
        {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        };

        const auto lerp_point = [](const DirectX::XMFLOAT2& a,
            const DirectX::XMFLOAT2& b, float t) noexcept
        {
            return DirectX::XMFLOAT2{ Lerp(a.x, b.x, t), Lerp(a.y, b.y, t) };
        };

        std::vector<DirectX::XMFLOAT2> shape_path;
        std::vector<float> shape_lengths;
        const auto build_shape_path = [&](const UIShapeComponent& shape,
            const DirectX::XMFLOAT4& rect, float scale, bool& closed)
        {
            shape_path.clear();
            closed = true;
            constexpr float pi = 3.14159265358979323846f;

            const auto append_arc = [&](float cx, float cy, float rx, float ry,
                float start, float end, int steps)
            {
                for (int step = 0; step <= steps; ++step)
                {
                    const float t = static_cast<float>(step) /
                        static_cast<float>((std::max)(1, steps));
                    const float angle = Lerp(start, end, t);
                    shape_path.push_back({
                        cx + std::cos(angle) * rx,
                        cy + std::sin(angle) * ry
                    });
                }
            };

            switch (shape.shape)
            {
            case UIShapeComponent::Circle:
            {
                const float curvature = Clamp01(shape.arc_curvature);
                if (curvature <= 0.0001f)
                {
                    // 半径無限大の極限は直線。曲率 0 を特別扱いして
                    // 1 / curvature の計算を行わない。
                    closed = false;
                    shape_path.push_back({ rect.x, rect.y + rect.w * 0.5f });
                    shape_path.push_back({ rect.x + rect.z, rect.y + rect.w * 0.5f });
                    break;
                }
                else
                {
                    // 正規化した横幅 1 の弦に対し、半径を 1 / curvature に比例
                    // させる。上下の弧をつなぐと curvature=1 で円になり、
                    // curvature が 0 へ近づくほど両方が同じ直線へ収束する。
                    const float radius = 0.5f / curvature;
                    const float half_chord_height = (std::sqrt)(
                        (std::max)(0.0f, radius * radius - 0.25f));
                    const auto to_rect = [&rect](float x, float y)
                    {
                        return DirectX::XMFLOAT2{
                            rect.x + rect.z * x,
                            rect.y + rect.w * y };
                    };

                    const float top_center_y = 0.5f + half_chord_height;
                    const float bottom_center_y = 0.5f - half_chord_height;
                    const float top_delta = (std::sqrt)(
                        (std::max)(0.0f, radius * radius - 0.25f));
                    float top_start = std::atan2(-top_delta, -0.5f);
                    if (top_start < 0.0f) top_start += 2.0f * pi;
                    float top_end = std::atan2(-top_delta, 0.5f);
                    if (top_end < 0.0f) top_end += 2.0f * pi;
                    if (top_end <= top_start) top_end += 2.0f * pi;
                    const float bottom_start = std::atan2(top_delta, 0.5f);
                    const float bottom_end = std::atan2(top_delta, -0.5f);
                    const float arc_angle = top_end - top_start;

                    // 弦のサグが 0.5px 以下になる分割数を求める。固定 32 分割では
                    // 大きい UI の端点付近で弧の近似が粗くなり、ストロークの継ぎ目に
                    // 隙間が見えるため、画面上の半径に応じて増減させる。
                    const float pixel_radius = (std::max)(
                        std::fabs(rect.z), std::fabs(rect.w)) *
                        (std::max)(std::fabs(scale), 0.0001f) * radius;
                    const float max_angle = pixel_radius > 0.5f
                        ? 2.0f * (std::acos)((std::max)(-1.0f, (std::min)(1.0f,
                            1.0f - 0.5f / pixel_radius)))
                        : arc_angle;
                    constexpr int maximum_arc_subdivisions = 256;
                    const int subdivisions = (std::min)(maximum_arc_subdivisions,
                        (std::max)(1, static_cast<int>(std::ceil(
                            arc_angle / (std::max)(0.0001f, max_angle)))));

                    for (int step = 0; step <= subdivisions; ++step)
                    {
                        const float t = static_cast<float>(step) /
                            static_cast<float>(subdivisions);
                        const float angle = Lerp(top_start, top_end, t);
                        shape_path.push_back(to_rect(
                            0.5f + std::cos(angle) * radius,
                            top_center_y + std::sin(angle) * radius));
                    }
                    for (int step = 1; step <= subdivisions; ++step)
                    {
                        const float t = static_cast<float>(step) /
                            static_cast<float>(subdivisions);
                        const float angle = Lerp(bottom_start, bottom_end, t);
                        shape_path.push_back(to_rect(
                            0.5f + std::cos(angle) * radius,
                            bottom_center_y + std::sin(angle) * radius));
                    }
                }
                break;
            }
            case UIShapeComponent::Line:
                closed = false;
                shape_path.push_back({ rect.x, rect.y + rect.w * 0.5f });
                shape_path.push_back({ rect.x + rect.z, rect.y + rect.w * 0.5f });
                break;
            case UIShapeComponent::Polygon:
            {
                const int sides = (std::min)((std::max)(shape.sides, 3), 64);
                const float cx = rect.x + rect.z * 0.5f;
                const float cy = rect.y + rect.w * 0.5f;
                const float rx = rect.z * 0.5f;
                const float ry = rect.w * 0.5f;
                for (int side = 0; side < sides; ++side)
                {
                    const float angle = -pi * 0.5f +
                        (pi * 2.0f * static_cast<float>(side)) /
                        static_cast<float>(sides);
                    shape_path.push_back({
                        cx + std::cos(angle) * rx,
                        cy + std::sin(angle) * ry
                    });
                }
                break;
            }
            case UIShapeComponent::BezierPath:
            {
                closed = false;
                const DirectX::XMFLOAT2 p0{ rect.x, rect.y + rect.w * 0.5f };
                const DirectX::XMFLOAT2 p1{ rect.x + rect.z * 0.35f, rect.y };
                const DirectX::XMFLOAT2 p2{ rect.x + rect.z * 0.65f, rect.y + rect.w };
                const DirectX::XMFLOAT2 p3{ rect.x + rect.z, rect.y + rect.w * 0.5f };
                for (int step = 0; step <= 48; ++step)
                {
                    const float t = static_cast<float>(step) / 48.0f;
                    const float u = 1.0f - t;
                    const float uu = u * u;
                    const float tt = t * t;
                    const float uuu = uu * u;
                    const float ttt = tt * t;
                    shape_path.push_back({
                        p0.x * uuu + 3.0f * p1.x * uu * t +
                            3.0f * p2.x * u * tt + p3.x * ttt,
                        p0.y * uuu + 3.0f * p1.y * uu * t +
                            3.0f * p2.y * u * tt + p3.y * ttt
                    });
                }
                break;
            }
            case UIShapeComponent::Superellipse:
            {
                constexpr int subdivisions = 128;
                const float exponent = (std::max)(0.25f,
                    (std::min)(16.0f, shape.superellipse_exponent));
                const float power = 2.0f / exponent;
                const float cx = rect.x + rect.z * 0.5f;
                const float cy = rect.y + rect.w * 0.5f;
                const float rx = rect.z * 0.5f;
                const float ry = rect.w * 0.5f;
                for (int segment = 0; segment < subdivisions; ++segment)
                {
                    const float angle = 2.0f * pi * static_cast<float>(segment) /
                        static_cast<float>(subdivisions);
                    const float cosine = std::cos(angle);
                    const float sine = std::sin(angle);
                    const float x = std::copysign(
                        (std::pow)(std::fabs(cosine), power), cosine);
                    const float y = std::copysign(
                        (std::pow)(std::fabs(sine), power), sine);
                    shape_path.push_back({ cx + x * rx, cy + y * ry });
                }
                break;
            }
            case UIShapeComponent::PolarFormula:
            {
                constexpr int subdivisions = 160;
                const float cx = rect.x + rect.z * 0.5f;
                const float cy = rect.y + rect.w * 0.5f;
                const float rx = rect.z * 0.5f;
                const float ry = rect.w * 0.5f;
                const float base_radius = (std::max)(0.05f,
                    (std::min)(1.5f, shape.polar_base_radius));
                const float amplitude = (std::max)(-1.0f,
                    (std::min)(1.0f, shape.polar_amplitude));
                const float lobes = (std::max)(1.0f,
                    (std::min)(32.0f, shape.polar_lobes));
                const float rotation = DirectX::XMConvertToRadians(
                    shape.polar_rotation);
                for (int segment = 0; segment < subdivisions; ++segment)
                {
                    const float theta = 2.0f * pi * static_cast<float>(segment) /
                        static_cast<float>(subdivisions);
                    const float radial = base_radius +
                        amplitude * std::cos(lobes * theta);
                    const float angle = theta + rotation;
                    shape_path.push_back({
                        cx + std::cos(angle) * rx * radial,
                        cy + std::sin(angle) * ry * radial });
                }
                break;
            }
            default:
            {
                const float radius = (std::min)(
                    (std::max)(0.0f, shape.corner_radius),
                    (std::min)(std::fabs(rect.z), std::fabs(rect.w)) * 0.5f);
                if (radius <= 0.001f)
                {
                    shape_path.push_back({ rect.x, rect.y });
                    shape_path.push_back({ rect.x + rect.z, rect.y });
                    shape_path.push_back({ rect.x + rect.z, rect.y + rect.w });
                    shape_path.push_back({ rect.x, rect.y + rect.w });
                }
                else
                {
                    append_arc(rect.x + rect.z - radius, rect.y + radius,
                        radius, radius, -pi * 0.5f, 0.0f, 8);
                    append_arc(rect.x + rect.z - radius, rect.y + rect.w - radius,
                        radius, radius, 0.0f, pi * 0.5f, 8);
                    append_arc(rect.x + radius, rect.y + rect.w - radius,
                        radius, radius, pi * 0.5f, pi, 8);
                    append_arc(rect.x + radius, rect.y + radius,
                        radius, radius, pi, pi * 1.5f, 8);
                }
                break;
            }
            }
        };

        const auto append_stroked_path = [&](const UIShapeComponent& shape,
            const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& color,
            float stroke_width, float scale, bool closed)
        {
            const std::size_t point_count = shape_path.size();
            if (point_count < 2 || stroke_width <= 0.0f || color.w <= 0.0f) return;
            const std::size_t segment_count = closed ? point_count : point_count - 1;
            if (segment_count == 0) return;

            shape_lengths.clear();
            shape_lengths.reserve(segment_count + 1);
            shape_lengths.push_back(0.0f);
            for (std::size_t segment = 0; segment < segment_count; ++segment)
            {
                const DirectX::XMFLOAT2& a = shape_path[segment];
                const DirectX::XMFLOAT2& b = shape_path[(segment + 1) % point_count];
                shape_lengths.push_back(shape_lengths.back() + local_distance(a, b));
            }
            const float total_length = shape_lengths.back();
            if (total_length <= 0.001f) return;

            const float base_start = Clamp01(shape.trim_start);
            const float base_end = Clamp01(shape.trim_end);
            const float span = (std::max)(0.0f, base_end - base_start);
            if (span <= 0.0f) return;

            float interval_starts[2]{ 0.0f, 0.0f };
            float interval_ends[2]{ 0.0f, 0.0f };
            int interval_count = 0;
            if (span >= 0.9999f)
            {
                interval_starts[interval_count] = 0.0f;
                interval_ends[interval_count] = total_length;
                ++interval_count;
            }
            else
            {
                float start = std::fmod(base_start + shape.trim_offset, 1.0f);
                if (start < 0.0f) start += 1.0f;
                const float end = start + span;
                interval_starts[interval_count] = start * total_length;
                interval_ends[interval_count] = (std::min)(end, 1.0f) * total_length;
                ++interval_count;
                if (end > 1.0f)
                {
                    interval_starts[interval_count] = 0.0f;
                    interval_ends[interval_count] = (end - 1.0f) * total_length;
                    ++interval_count;
                }
            }

            const float dash_length = (std::max)(0.0f, shape.dash_length);
            const float dash_gap = (std::max)(0.0f, shape.dash_gap);
            const float dash_pattern = dash_length + dash_gap;
            const bool dashed = dash_length > 0.0f && dash_gap > 0.0f;

            const auto emit_segment = [&](const DirectX::XMFLOAT2& a,
                const DirectX::XMFLOAT2& b, float abs_a, float abs_b)
            {
                if (abs_b <= abs_a) return;
                if (!dashed)
                {
                    append_line_segment_local(a, b, matrix, color, stroke_width, scale,
                        abs_a / total_length, abs_b / total_length);
                    return;
                }

                float cursor = abs_a;
                int guard = 0;
                while (cursor < abs_b && guard++ < 256)
                {
                    float phase = std::fmod(cursor + shape.dash_offset, dash_pattern);
                    if (phase < 0.0f) phase += dash_pattern;
                    if (phase < dash_length)
                    {
                        const float step = (std::min)(abs_b - cursor,
                            dash_length - phase);
                        const float next = cursor + step;
                        const float ta = (cursor - abs_a) / (abs_b - abs_a);
                        const float tb = (next - abs_a) / (abs_b - abs_a);
                        append_line_segment_local(lerp_point(a, b, ta),
                            lerp_point(a, b, tb), matrix, color, stroke_width, scale,
                            cursor / total_length, next / total_length);
                        cursor = next;
                    }
                    else
                    {
                        cursor += (std::min)(abs_b - cursor, dash_pattern - phase);
                    }
                }
            };

            for (std::size_t segment = 0; segment < segment_count; ++segment)
            {
                const float segment_start = shape_lengths[segment];
                const float segment_end = shape_lengths[segment + 1];
                if (segment_end <= segment_start) continue;

                const DirectX::XMFLOAT2& a = shape_path[segment];
                const DirectX::XMFLOAT2& b = shape_path[(segment + 1) % point_count];
                for (int interval = 0; interval < interval_count; ++interval)
                {
                    const float clipped_start =
                        (std::max)(segment_start, interval_starts[interval]);
                    const float clipped_end =
                        (std::min)(segment_end, interval_ends[interval]);
                    if (clipped_end <= clipped_start) continue;

                    const float ta = (clipped_start - segment_start) /
                        (segment_end - segment_start);
                    const float tb = (clipped_end - segment_start) /
                        (segment_end - segment_start);
                    emit_segment(lerp_point(a, b, ta), lerp_point(a, b, tb),
                        clipped_start, clipped_end);
                }
            }
        };

        const auto render_image = [&](UIImageComponent& image,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!image.ActiveInHierarchy() || image.opacity <= 0.0f || image.fill_amount <= 0.0f)
                return;

            DirectX::XMFLOAT4 draw_rect = rect.ResolvedRect();
            DirectX::XMFLOAT4 uv{ image.uv_offset.x, image.uv_offset.y,
                image.uv_scale.x, image.uv_scale.y };
            const float fill = (std::min)((std::max)(image.fill_amount, 0.0f), 1.0f);
            if (image.fill_method == UIImageComponent::Horizontal)
            {
                draw_rect.z *= fill;
                uv.z *= fill;
            }
            else if (image.fill_method == UIImageComponent::Vertical)
            {
                draw_rect.w *= fill;
                uv.w *= fill;
            }

            append_quad(draw_rect, rect.ResolvedMatrix(), uv,
                MultiplyAlpha(image.color, image.opacity * opacity), scale);
            configure_visual(image.fill_color_2, image.fill_mode, image.fill_angle,
                image.fill_center, image.stroke_color_2, image.stroke_mode,
                false, 0.0f, {}, {}, {});
            Flush(context, TextureFor(image.sprite.guid, asset_database),
                BlendForImage(image, states), states, scissor);
        };

        const auto render_shape = [&](UIShapeComponent& shape,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!shape.ActiveInHierarchy() || opacity <= 0.0f) return;

            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            bool closed = true;
            build_shape_path(shape, r, scale, closed);
            if (shape_path.empty()) return;

            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            const bool has_fill = closed && shape.shape != UIShapeComponent::Line &&
                shape.fill_color.w * opacity > 0.0f && shape_path.size() >= 3;
            const bool split_draws = shape.fill_mode != UIShapeComponent::Solid ||
                shape.stroke_mode != UIShapeComponent::StrokeSolid;
            if (has_fill)
            {
                DirectX::XMFLOAT2 center{ 0.0f, 0.0f };
                for (const DirectX::XMFLOAT2& point : shape_path)
                {
                    center.x += point.x;
                    center.y += point.y;
                }
                const float inv_count = 1.0f /
                    static_cast<float>((std::max)(std::size_t{ 1 }, shape_path.size()));
                center.x *= inv_count;
                center.y *= inv_count;

                const DirectX::XMFLOAT4 fill =
                    MultiplyAlpha(shape.fill_color, opacity);
                configure_visual(shape.fill_color_2, shape.fill_mode,
                    shape.fill_angle, shape.fill_center,
                    shape.stroke_color_2, shape.stroke_mode,
                    false, 0.0f, {}, {}, {});
                visual_constants_.fill_color_3 = shape.fill_color_3;
                visual_constants_.fill_color_4 = shape.fill_color_4;
                visual_constants_.fill_stops = {
                    shape.fill_stop_2, shape.fill_stop_3,
                    shape.fill_stop_4, 0.0f };
                for (std::size_t index = 0; index < shape_path.size(); ++index)
                {
                    append_triangle_local(center, shape_path[index],
                        shape_path[(index + 1) % shape_path.size()],
                        r, matrix, fill, scale);
                }
                if (split_draws)
                {
                    Flush(context, white_texture_.Get(), states.blend_alpha,
                        states, scissor);
                }
            }

            float stroke_width = shape.stroke_width;
            DirectX::XMFLOAT4 stroke = shape.stroke_color;
            if (shape.shape == UIShapeComponent::Line && stroke_width <= 0.0f)
            {
                stroke_width = 1.0f;
                stroke = shape.fill_color;
            }
            configure_visual(shape.fill_color_2, UIShapeComponent::Solid,
                0.0f, { 0.5f, 0.5f }, shape.stroke_color_2,
                shape.stroke_mode, false, 0.0f, {}, {}, {});
            append_stroked_path(shape, matrix, MultiplyAlpha(stroke, opacity),
                stroke_width, scale, closed);
            if (!split_draws || !has_fill)
            {
                Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
            }
            else if (!vertices_.empty())
            {
                Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
            }
        };

        const auto animator_anchor = [](int anchor) noexcept
        {
            switch (anchor)
            {
            case UITextAnimatorComponent::BaselineLeft: return DirectX::XMFLOAT2{ 0.0f, 0.5f };
            case UITextAnimatorComponent::BaselineCenter: return DirectX::XMFLOAT2{ 0.5f, 0.5f };
            case UITextAnimatorComponent::TopLeft: return DirectX::XMFLOAT2{ 0.0f, 0.0f };
            case UITextAnimatorComponent::BottomCenter: return DirectX::XMFLOAT2{ 0.5f, 1.0f };
            default: return DirectX::XMFLOAT2{ 0.5f, 0.5f };
            }
        };

        std::vector<const UITextAnimatorComponent*> text_animators;
        const auto append_text_glyphs = [&](const Core::GameObject& object,
            UITextComponent& text, const DirectX::XMFLOAT4& origin,
            const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& base_color,
            float scale)
        {
            GatherTextAnimators(object, text_animators);
            const std::vector<UITextComponent::GlyphQuad>& glyphs = text.Glyphs();
            const float glyph_count = (std::max)(1.0f,
                static_cast<float>(text.DisplayCharacterCount()));
            const float outline_extent = clamp_outline_width(text.outline_width);
            const float shadow_extent_x = (std::max)(0.0f,
                std::fabs(text.shadow_offset.x));
            const float shadow_extent_y = (std::max)(0.0f,
                std::fabs(text.shadow_offset.y));
            const bool has_text_effect = outline_extent > 0.0f ||
                text.shadow_color.w > 0.0f;

            for (const UITextComponent::GlyphQuad& glyph : glyphs)
            {
                DirectX::XMFLOAT4 glyph_rect{
                    origin.x + glyph.position.x,
                    origin.y + glyph.position.y,
                    glyph.size.x,
                    glyph.size.y
                };
                DirectX::XMFLOAT4 color{
                    base_color.x * glyph.rich_color.x,
                    base_color.y * glyph.rich_color.y,
                    base_color.z * glyph.rich_color.z,
                    base_color.w * glyph.rich_color.w };
                DirectX::XMFLOAT2 local_scale{
                    glyph.rich_bold ? 1.035f : 1.0f,
                    glyph.rich_bold ? 1.035f : 1.0f };
                DirectX::XMFLOAT2 anchor{ 0.5f, 0.5f };
                float rotation = 0.0f;
                bool transformed = glyph.rich_bold || glyph.rich_italic;
                const float rich_italic_shear = glyph.rich_italic ? -0.18f : 0.0f;

                for (const UITextAnimatorComponent* animator : text_animators)
                {
                    const float position =
                        (static_cast<float>(glyph.character_index) + 0.5f) / glyph_count;
                    const float influence = RangeInfluence(*animator, position);
                    if (influence <= 0.0f) continue;

                    glyph_rect.x += animator->position_offset.x * influence;
                    glyph_rect.y += animator->position_offset.y * influence;
                    glyph_rect.x += animator->character_spacing *
                        static_cast<float>(glyph.character_index) * influence;
                    glyph_rect.x += animator->random_position.x *
                        RandomSigned(animator->random_seed, glyph.character_index, 11u) *
                        influence;
                    glyph_rect.y += animator->random_position.y *
                        RandomSigned(animator->random_seed, glyph.character_index, 23u) *
                        influence;

                    rotation += animator->rotation * influence;
                    rotation += animator->random_rotation *
                        RandomSigned(animator->random_seed, glyph.character_index, 37u) *
                        influence;
                    local_scale.x *= Lerp(1.0f, animator->scale.x, influence);
                    local_scale.y *= Lerp(1.0f, animator->scale.y, influence);
                    color = LerpColor(color,
                        { base_color.x * glyph.rich_color.x * animator->color.x,
                          base_color.y * glyph.rich_color.y * animator->color.y,
                          base_color.z * glyph.rich_color.z * animator->color.z,
                          base_color.w * glyph.rich_color.w * animator->color.w },
                        influence);
                    color.w *= Lerp(1.0f, animator->opacity, influence);
                    anchor = animator_anchor(animator->anchor);
                    transformed = true;
                }

                const DirectX::XMFLOAT4 glyph_uv_bounds{
                    glyph.uv.x, glyph.uv.y,
                    glyph.uv.x + glyph.uv.z, glyph.uv.y + glyph.uv.w };
                DirectX::XMFLOAT4 draw_rect = glyph_rect;
                DirectX::XMFLOAT4 draw_uv = glyph.uv;
                if (has_text_effect)
                {
                    // outline_width / shadow_offset は画面ピクセルの値なので、
                    // CPU 側ではエフェクトを収める分だけクアッドを広げる。
                    // 実際のしきい値と UV の変化量はシェーダー側で計算するため、
                    // Text Animator の回転・拡縮でもサンプル方向を固定しない。
                    const float safe_canvas_scale = (std::max)(
                        std::fabs(scale), 0.0001f);
                    const float safe_local_scale_x = (std::max)(
                        std::fabs(local_scale.x), 0.0001f);
                    const float safe_local_scale_y = (std::max)(
                        std::fabs(local_scale.y), 0.0001f);
                    const float expand_x = (outline_extent + shadow_extent_x) /
                        (safe_canvas_scale * safe_local_scale_x);
                    const float expand_y = (outline_extent + shadow_extent_y) /
                        (safe_canvas_scale * safe_local_scale_y);
                    draw_rect.x -= expand_x;
                    draw_rect.y -= expand_y;
                    draw_rect.z += expand_x * 2.0f;
                    draw_rect.w += expand_y * 2.0f;
                    const float safe_glyph_width = (std::max)(
                        std::fabs(glyph_rect.z), 0.0001f);
                    const float safe_glyph_height = (std::max)(
                        std::fabs(glyph_rect.w), 0.0001f);
                    const float uv_expand_x = expand_x / safe_glyph_width * glyph.uv.z;
                    const float uv_expand_y = expand_y / safe_glyph_height * glyph.uv.w;
                    draw_uv.x -= uv_expand_x;
                    draw_uv.y -= uv_expand_y;
                    draw_uv.z += uv_expand_x * 2.0f;
                    draw_uv.w += uv_expand_y * 2.0f;
                }

                if (transformed)
                {
                    append_quad_local_with_bounds(draw_rect, matrix, draw_uv, color,
                        scale, local_scale, rotation, anchor, rich_italic_shear,
                        glyph_uv_bounds);
                }
                else
                {
                    append_quad_with_bounds(draw_rect, matrix, draw_uv, color, scale,
                        glyph_uv_bounds);
                }
            }
        };

        const auto render_text = [&](const Core::GameObject& object,
            UITextComponent& text, const RectTransformComponent& rect, float scale,
            float opacity, const D3D11_RECT* scissor)
        {
            UIInputFieldComponent* input = const_cast<Core::GameObject&>(object)
                .GetComponent<UIInputFieldComponent>();
            if (input == nullptr)
            {
                // Unity InputField と同様、表示 Text を子 GameObject に置く構成も許す。
                // text_target 参照先の UIText を描いている場合は、その InputField の
                // selection / caret をこの Text の矩形へ重ねる。
                Scene::Scene* scene = const_cast<Core::GameObject&>(object).GetScene();
                if (scene != nullptr)
                {
                    for (std::size_t index = 0; index < scene->GameObjectCount(); ++index)
                    {
                        Core::GameObject* candidate_object = scene->GameObjectAt(index);
                        UIInputFieldComponent* candidate = candidate_object != nullptr
                            ? candidate_object->GetComponent<UIInputFieldComponent>() : nullptr;
                        if (candidate == nullptr || !candidate->text_target.IsAssigned()) continue;
                        if (candidate->text_target.owner == object.ID() &&
                            candidate->text_target.component == text.StableID())
                        {
                            input = candidate;
                            break;
                        }
                    }
                }
            }
            Core::GameObject* input_owner = input != nullptr ? input->Owner() : nullptr;
            UISelectableComponent* selectable = input_owner != nullptr
                ? input_owner->GetComponent<UISelectableComponent>() : nullptr;
            const bool focused_input = input != nullptr && selectable != nullptr &&
                selectable->focused;
            if (!text.ActiveInHierarchy() || text.opacity <= 0.0f ||
                (text.ResolvedText().empty() && !focused_input)) return;

            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            font_atlas.BuildGlyphs(text, r.z, r.w, asset_database);

            // InputField selection is a background, so emit it before glyphs.
            if (focused_input && input->HasSelection() && !input->password)
            {
                const int selection_start = input->SelectionStart();
                const int selection_end = input->SelectionEnd();
                const DirectX::XMFLOAT4 selection_color =
                    MultiplyAlpha(input->selection_color, opacity);
                for (const UITextComponent::GlyphQuad& glyph : text.Glyphs())
                {
                    if (glyph.character_index < selection_start ||
                        glyph.character_index >= selection_end) continue;
                    DirectX::XMFLOAT4 select_rect{
                        r.x + glyph.position.x,
                        r.y + glyph.position.y,
                        (std::max)(glyph.advance, glyph.size.x),
                        glyph.size.y };
                    append_quad(select_rect, rect.ResolvedMatrix(),
                        { 0.0f, 0.0f, 1.0f, 1.0f }, selection_color, scale);
                }
                configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                    false, 0.0f, {}, {}, {});
                Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
            }

            if (!text.ResolvedText().empty())
            {
                const DirectX::XMFLOAT4 color = MultiplyAlpha(text.color,
                    text.opacity * opacity);
                append_text_glyphs(object, text, r, rect.ResolvedMatrix(), color, scale);
                configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0, true,
                    text.outline_width, text.outline_color,
                    text.shadow_offset, text.shadow_color);
                Flush(context, font_atlas.Texture(), states.blend_alpha, states, scissor);
            }

            if (focused_input)
            {
                const float blink = (std::max)(0.05f, input->caret_blink_seconds);
                const bool visible = std::fmod((std::max)(0.0f, effect_time), blink) < blink * 0.5f;
                if (visible)
                {
                    float caret_x = r.x;
                    float caret_y = r.y + (std::max)(0.0f, (r.w - text.font_size) * 0.5f);
                    float caret_h = (std::max)(1.0f, text.font_size);
                    bool position_found = false;
                    for (const UITextComponent::GlyphQuad& glyph : text.Glyphs())
                    {
                        if (glyph.character_index >= input->caret_index)
                        {
                            caret_x = r.x + glyph.position.x;
                            caret_y = r.y + glyph.position.y;
                            caret_h = (std::max)(1.0f, glyph.size.y);
                            position_found = true;
                            break;
                        }
                        caret_x = r.x + glyph.position.x + glyph.advance;
                        caret_y = r.y + glyph.position.y;
                        caret_h = (std::max)(1.0f, glyph.size.y);
                    }
                    (void)position_found;
                    const float logical_width = (std::max)(0.5f,
                        input->caret_width / (std::max)(0.0001f, scale));
                    append_quad({ caret_x, caret_y, logical_width, caret_h },
                        rect.ResolvedMatrix(), { 0.0f, 0.0f, 1.0f, 1.0f },
                        MultiplyAlpha(input->caret_color, opacity), scale);
                    configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                        false, 0.0f, {}, {}, {});
                    Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
                }
            }
        };

        const auto render_focus_outline = [&](const Core::GameObject& object,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            const UISelectableComponent* selectable =
                object.GetComponent<UISelectableComponent>();
            if (selectable == nullptr || !selectable->focused ||
                !selectable->ActiveInHierarchy()) return;
            const bool enabled = selectable->override_focus_style
                ? selectable->focus_outline_enabled : states.focus_outline_enabled;
            if (!enabled) return;
            const DirectX::XMFLOAT4 color = MultiplyAlpha(
                selectable->override_focus_style ? selectable->focus_outline_color
                    : states.focus_outline_color, opacity);
            const float pixel_width = selectable->override_focus_style
                ? selectable->focus_outline_width : states.focus_outline_width;
            const float width = (std::max)(0.5f,
                pixel_width / (std::max)(0.0001f, scale));
            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            const float half = width * 0.5f;
            const float pixel_radius = selectable->override_focus_style
                ? selectable->focus_corner_radius : states.focus_corner_radius;
            const float radius = (std::max)(0.0f,
                pixel_radius / (std::max)(0.0001f, scale));
            const DirectX::XMFLOAT4 outline_rect{
                r.x - half, r.y - half, r.z + width, r.w + width };
            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            UIShapeComponent outline_shape;
            outline_shape.shape = UIShapeComponent::Rectangle;
            outline_shape.corner_radius = radius + half;
            bool closed = true;
            build_shape_path(outline_shape, outline_rect, scale, closed);
            append_stroked_path(outline_shape, matrix, color, width, scale, true);
            configure_visual({}, UIShapeComponent::Solid, 0.0f, { 0.5f, 0.5f },
                {}, UIShapeComponent::StrokeSolid, false, 0.0f, {}, {}, {});
            Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
        };

        const auto render_scrollbars = [&](const Core::GameObject& object,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            const UIScrollViewComponent* scroll =
                object.GetComponent<UIScrollViewComponent>();
            if (scroll == nullptr || !scroll->show_scrollbars ||
                !scroll->ActiveInHierarchy()) return;

            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            const float width = (std::max)(2.0f,
                scroll->scrollbar_width / (std::max)(0.0001f, scale));
            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            const auto draw_rounded = [&](const DirectX::XMFLOAT4& bar,
                const DirectX::XMFLOAT4& color, float corner_radius)
            {
                UIShapeComponent shape;
                shape.shape = UIShapeComponent::Rectangle;
                shape.corner_radius = (std::min)((std::max)(0.0f, corner_radius),
                    (std::min)(std::fabs(bar.z), std::fabs(bar.w)) * 0.5f);
                bool closed = true;
                build_shape_path(shape, bar, scale, closed);
                if (shape_path.size() < 3) return;

                DirectX::XMFLOAT2 center{ 0.0f, 0.0f };
                for (const DirectX::XMFLOAT2& point : shape_path)
                {
                    center.x += point.x;
                    center.y += point.y;
                }
                const float inv_count = 1.0f /
                    static_cast<float>((std::max)(std::size_t{ 1 }, shape_path.size()));
                center.x *= inv_count;
                center.y *= inv_count;
                const DirectX::XMFLOAT4 fill = MultiplyAlpha(color, opacity);
                for (std::size_t index = 0; index < shape_path.size(); ++index)
                {
                    append_triangle_local(center, shape_path[index],
                        shape_path[(index + 1) % shape_path.size()],
                        bar, matrix, fill, scale);
                }
            };

            const float radius = (std::max)(0.0f,
                scroll->scrollbar_corner_radius / (std::max)(0.0001f, scale));
            if (scroll->vertical_overflow)
            {
                const DirectX::XMFLOAT4 track{ r.x + r.z - width, r.y, width, r.w };
                draw_rounded(track, scroll->scrollbar_track_color,
                    (std::min)(radius, width * 0.5f));
                const float thumb_h = (std::max)(width,
                    r.w * scroll->vertical_visible_ratio);
                const float travel = (std::max)(0.0f, r.w - thumb_h);
                const float thumb_y = r.y + travel * (1.0f - scroll->vertical_normalized);
                draw_rounded({ r.x + r.z - width, thumb_y, width, thumb_h },
                    scroll->scrollbar_thumb_color, radius);
            }
            if (scroll->horizontal_overflow)
            {
                const DirectX::XMFLOAT4 track{ r.x, r.y, r.z, width };
                draw_rounded(track, scroll->scrollbar_track_color,
                    (std::min)(radius, width * 0.5f));
                const float thumb_w = (std::max)(width,
                    r.z * scroll->horizontal_visible_ratio);
                const float travel = (std::max)(0.0f, r.z - thumb_w);
                const float thumb_x = r.x + travel * scroll->horizontal_normalized;
                draw_rounded({ thumb_x, r.y, thumb_w, width },
                    scroll->scrollbar_thumb_color, radius);
            }
            if (scroll->vertical_overflow || scroll->horizontal_overflow)
            {
                configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                    false, 0.0f, {}, {}, {});
                Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
            }
        };

        const auto configure_effect_target = [&](UIRenderTarget& target)
        {
            ID3D11RenderTargetView* offscreen = target.rtv.Get();
            context->OMSetRenderTargets(1, &offscreen, nullptr);
            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(target.width);
            viewport.Height = static_cast<float>(target.height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);

            Constants offscreen_constants = constants;
            offscreen_constants.screen_size = {
                static_cast<float>(target.width),
                static_cast<float>(target.height), 0.0f, 0.0f };
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                &offscreen_constants, 0, 0);
            draw_target_height = static_cast<float>(target.height);
        };

        const auto apply_effect_passes = [&](const UIEffectStackComponent& effects,
            UIRenderTarget*& current, UIRenderTarget* first, UIRenderTarget* second)
        {
            Rendering::Effects::EffectChain::Context chain_context{};
            chain_context.device_context = context;
            chain_context.asset_database = asset_database;
            chain_context.shader_catalog = shader_catalog;
            chain_context.time = effect_time;
            chain_context.depth_disabled = states.depth_disabled;
            chain_context.rasterizer = states.rasterizer;
            chain_context.blend_none = states.blend_none;
            chain_context.blend_alpha = states.blend_alpha;
            chain_context.sampler = states.sampler;
            chain_context.resolve_texture = [&](const std::string& guid)
            {
                // Texture cache / white fallback は従来どおり UIRenderer が一元所有する。
                return TextureFor(guid, asset_database);
            };
            chain_context.configure_target = [&](UIRenderTarget& target)
            {
                configure_effect_target(target);
            };
            chain_context.draw_plain_fullscreen = [&](float width, float height,
                ID3D11ShaderResourceView* source, ID3D11BlendState* blend)
            {
                DirectX::XMFLOAT4X4 identity{};
                DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                    false, 0.0f, {}, {}, {});
                append_quad({ 0.0f, 0.0f, width, height }, identity,
                    { 0.0f, 0.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f);
                Flush(context, source, blend, states, nullptr);
            };
            chain_context.draw_effect_fullscreen = [&](float width, float height,
                ID3D11ShaderResourceView* source, ID3D11BlendState* blend,
                ID3D11PixelShader* shader, ID3D11Buffer* effect_constants)
            {
                DirectX::XMFLOAT4X4 identity{};
                DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                append_quad({ 0.0f, 0.0f, width, height }, identity,
                    { 0.0f, 0.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f);
                Flush(context, source, blend, states, nullptr,
                    shader, effect_constants);
            };
            current = effect_chain_.Apply(chain_context, effects.EffectiveEffects(asset_database),
                current, first, second);
        };

        // UI の論理座標から、現在の描画先（Scene View の offset / zoom を含む）の
        // 実ピクセル座標へ変換する。Flush の scissor 変換と同じ値を使う。
        const auto to_output_point = [&](const DirectX::XMFLOAT2& canvas_point,
            float canvas_scale)
        {
            const DirectX::XMFLOAT2 screen_point = ToScreenPoint(canvas_point,
                canvas_scale, screen_height);
            const float scale_x = states.viewport_scale_x > 0.0001f
                ? states.viewport_scale_x : 1.0f;
            const float scale_y = states.viewport_scale_y > 0.0001f
                ? states.viewport_scale_y : 1.0f;
            return DirectX::XMFLOAT2{
                states.scissor_offset_x + screen_point.x * scale_x,
                states.scissor_offset_y + screen_point.y * scale_y };
        };

        const auto capture_scale_for = [&](const RectTransformComponent& rect,
            const DirectX::XMFLOAT4& source_rect, float canvas_scale,
            float& out_scale)
        {
            if (world_space_canvas_ || source_rect.z <= 0.0001f ||
                source_rect.w <= 0.0001f)
            {
                return false;
            }

            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            const DirectX::XMFLOAT2 p0 = to_output_point(
                TransformPoint(matrix, source_rect.x, source_rect.y), canvas_scale);
            const DirectX::XMFLOAT2 p1 = to_output_point(
                TransformPoint(matrix, source_rect.x + source_rect.z, source_rect.y),
                canvas_scale);
            const DirectX::XMFLOAT2 p2 = to_output_point(
                TransformPoint(matrix, source_rect.x + source_rect.z,
                    source_rect.y + source_rect.w), canvas_scale);
            const DirectX::XMFLOAT2 p3 = to_output_point(
                TransformPoint(matrix, source_rect.x, source_rect.y + source_rect.w),
                canvas_scale);
            constexpr float alignment_epsilon = 0.01f;
            if (std::fabs(p0.y - p1.y) > alignment_epsilon ||
                std::fabs(p1.x - p2.x) > alignment_epsilon ||
                std::fabs(p2.y - p3.y) > alignment_epsilon ||
                std::fabs(p3.x - p0.x) > alignment_epsilon ||
                p1.x <= p0.x || p0.y <= p3.y)
            {
                return false;
            }

            const float scale_x = (p1.x - p0.x) / source_rect.z;
            const float scale_y = (p0.y - p3.y) / source_rect.w;
            if (scale_x <= 0.0001f || scale_y <= 0.0001f ||
                std::fabs(scale_x - scale_y) >
                    (std::max)(scale_x, scale_y) * 0.0001f)
            {
                return false;
            }
            out_scale = scale_x;
            return true;
        };

        const auto make_backdrop_capture_plan = [&](const RectTransformComponent& rect,
            const DirectX::XMFLOAT4& composite_rect, float canvas_scale,
            const D3D11_RECT* scissor, BackdropCapturePlan& plan)
        {
            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            const DirectX::XMFLOAT2 p0 = to_output_point(
                TransformPoint(matrix, composite_rect.x, composite_rect.y), canvas_scale);
            const DirectX::XMFLOAT2 p1 = to_output_point(
                TransformPoint(matrix, composite_rect.x + composite_rect.z,
                    composite_rect.y), canvas_scale);
            const DirectX::XMFLOAT2 p2 = to_output_point(
                TransformPoint(matrix, composite_rect.x + composite_rect.z,
                    composite_rect.y + composite_rect.w), canvas_scale);
            const DirectX::XMFLOAT2 p3 = to_output_point(
                TransformPoint(matrix, composite_rect.x,
                    composite_rect.y + composite_rect.w), canvas_scale);
            const float min_x = (std::min)({ p0.x, p1.x, p2.x, p3.x });
            const float max_x = (std::max)({ p0.x, p1.x, p2.x, p3.x });
            const float min_y = (std::min)({ p0.y, p1.y, p2.y, p3.y });
            const float max_y = (std::max)({ p0.y, p1.y, p2.y, p3.y });
            D3D11_RECT full_rect{};
            full_rect.left = static_cast<LONG>(std::floor(min_x));
            full_rect.top = static_cast<LONG>(std::floor(min_y));
            full_rect.right = static_cast<LONG>(std::ceil(max_x));
            full_rect.bottom = static_cast<LONG>(std::ceil(max_y));
            if (full_rect.right <= full_rect.left || full_rect.bottom <= full_rect.top)
                return false;

            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> source_view;
            context->OMGetRenderTargets(1, source_view.GetAddressOf(), nullptr);
            if (!source_view) return false;
            Microsoft::WRL::ComPtr<ID3D11Resource> source_resource;
            source_view->GetResource(source_resource.GetAddressOf());
            if (!source_resource ||
                FAILED(source_resource.As(&plan.source_texture)) ||
                !plan.source_texture)
            {
                return false;
            }

            D3D11_TEXTURE2D_DESC source_desc{};
            plan.source_texture->GetDesc(&source_desc);
            if (source_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
                source_desc.SampleDesc.Count != 1)
            {
                return false;
            }

            D3D11_RECT copy_rect = full_rect;
            const D3D11_RECT source_bounds{ 0, 0,
                static_cast<LONG>(source_desc.Width),
                static_cast<LONG>(source_desc.Height) };
            copy_rect = IntersectScissor(copy_rect, source_bounds);
            if (scissor != nullptr)
            {
                const float scale_x = states.viewport_scale_x > 0.0001f
                    ? states.viewport_scale_x : 1.0f;
                const float scale_y = states.viewport_scale_y > 0.0001f
                    ? states.viewport_scale_y : 1.0f;
                D3D11_RECT output_scissor{};
                output_scissor.left = static_cast<LONG>(std::floor(
                    states.scissor_offset_x + scissor->left * scale_x));
                output_scissor.top = static_cast<LONG>(std::floor(
                    states.scissor_offset_y + scissor->top * scale_y));
                output_scissor.right = static_cast<LONG>(std::ceil(
                    states.scissor_offset_x + scissor->right * scale_x));
                output_scissor.bottom = static_cast<LONG>(std::ceil(
                    states.scissor_offset_y + scissor->bottom * scale_y));
                if (states.scissor_bounds_enabled)
                    output_scissor = IntersectScissor(output_scissor,
                        states.scissor_bounds);
                copy_rect = IntersectScissor(copy_rect, output_scissor);
            }
            if (copy_rect.right <= copy_rect.left || copy_rect.bottom <= copy_rect.top)
                return false;

            plan.output_rect = full_rect;
            plan.width = static_cast<std::uint32_t>(full_rect.right - full_rect.left);
            plan.height = static_cast<std::uint32_t>(full_rect.bottom - full_rect.top);
            plan.destination_x = static_cast<std::uint32_t>(
                copy_rect.left - full_rect.left);
            plan.destination_y = static_cast<std::uint32_t>(
                copy_rect.top - full_rect.top);
            plan.source_box.left = static_cast<UINT>(copy_rect.left);
            plan.source_box.top = static_cast<UINT>(copy_rect.top);
            plan.source_box.right = static_cast<UINT>(copy_rect.right);
            plan.source_box.bottom = static_cast<UINT>(copy_rect.bottom);
            plan.source_box.front = 0;
            plan.source_box.back = 1;
            return plan.width > 0 && plan.height > 0;
        };

        const auto render_effect_with_backdrop = [&](const UIEffectStackComponent& effects,
            const RectTransformComponent& rect, const DirectX::XMFLOAT4& source_rect,
            float canvas_scale, const D3D11_RECT* scissor, const auto& draw_source)
        {
            if (!effects.HasActiveEffects(asset_database) || states.blend_none == nullptr)
                return false;

            float capture_scale = 1.0f;
            if (!capture_scale_for(rect, source_rect, canvas_scale, capture_scale))
                return false;

            const DirectX::XMFLOAT4 expansion = effects.ExpandBounds(
                source_rect.z * capture_scale, source_rect.w * capture_scale, asset_database);
            const float inverse_scale = 1.0f / (std::max)(0.0001f, capture_scale);
            const float expanded_width = (std::max)(1.0f,
                source_rect.z + (expansion.x + expansion.z) * inverse_scale);
            const float expanded_height = (std::max)(1.0f,
                source_rect.w + (expansion.y + expansion.w) * inverse_scale);
            const DirectX::XMFLOAT4 composite_rect{
                source_rect.x - expansion.x * inverse_scale,
                source_rect.y - expansion.y * inverse_scale,
                expanded_width,
                expanded_height };
            BackdropCapturePlan plan{};
            if (!make_backdrop_capture_plan(rect, composite_rect, canvas_scale,
                scissor, plan))
            {
                return false;
            }

            UIRenderTarget* target = render_target_pool_.Acquire(plan.width, plan.height);
            UIRenderTarget* scratch = render_target_pool_.Acquire(plan.width, plan.height);
            if (target == nullptr || scratch == nullptr || !target->texture ||
                !target->rtv || !target->srv || !scratch->texture ||
                !scratch->rtv || !scratch->srv ||
                target->texture.Get() == plan.source_texture.Get() ||
                scratch->texture.Get() == plan.source_texture.Get())
            {
                return false;
            }

            ID3D11ShaderResourceView* null_srvs[2]{};
            context->PSSetShaderResources(0, _countof(null_srvs), null_srvs);
            const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            context->ClearRenderTargetView(target->rtv.Get(), clear);
            context->CopySubresourceRegion(target->texture.Get(), 0,
                plan.destination_x, plan.destination_y, 0, plan.source_texture.Get(), 0,
                &plan.source_box);

            ID3D11RenderTargetView* previous_rtv = nullptr;
            ID3D11DepthStencilView* previous_dsv = nullptr;
            context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
            UINT viewport_count = 1;
            D3D11_VIEWPORT previous_viewport{};
            context->RSGetViewports(&viewport_count, &previous_viewport);

            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            const DirectX::XMFLOAT2 source_p0 = to_output_point(
                TransformPoint(matrix, source_rect.x, source_rect.y), canvas_scale);
            const DirectX::XMFLOAT2 source_p2 = to_output_point(
                TransformPoint(matrix, source_rect.x + source_rect.z,
                    source_rect.y + source_rect.w), canvas_scale);
            const float source_left = (std::min)(source_p0.x, source_p2.x);
            const float source_bottom = (std::max)(source_p0.y, source_p2.y);
            DirectX::XMFLOAT4 draw_rect{
                (source_left - static_cast<float>(plan.output_rect.left)) / capture_scale,
                (static_cast<float>(plan.height) -
                    (source_bottom - static_cast<float>(plan.output_rect.top))) /
                    capture_scale,
                source_rect.z,
                source_rect.w };

            configure_effect_target(*target);
            draw_source(draw_rect, capture_scale);

            ID3D11ShaderResourceView* null_srv = nullptr;
            context->PSSetShaderResources(0, 1, &null_srv);
            UIRenderTarget* current = target;
            apply_effect_passes(effects, current, target, scratch);

            context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
            if (viewport_count > 0) context->RSSetViewports(1, &previous_viewport);
            if (previous_rtv != nullptr) previous_rtv->Release();
            if (previous_dsv != nullptr) previous_dsv->Release();

            constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                &constants, 0, 0);
            draw_target_height = screen_height;
            append_quad(composite_rect, rect.ResolvedMatrix(),
                { 0.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
                canvas_scale);
            configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                false, 0.0f, {}, {}, {});
            Flush(context, current->srv.Get(), states.blend_none, states, scissor);
            return true;
        };

        const auto render_image_effect_with_backdrop = [&](
            const UIEffectStackComponent& effects, UIImageComponent& image,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!image.ActiveInHierarchy() || image.opacity <= 0.0f ||
                image.fill_amount <= 0.0f)
            {
                return false;
            }
            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            return render_effect_with_backdrop(effects, rect, source_rect, scale,
                scissor, [&](const DirectX::XMFLOAT4& draw_rect, float capture_scale)
                {
                    DirectX::XMFLOAT4 source = draw_rect;
                    DirectX::XMFLOAT4 uv{ image.uv_offset.x, image.uv_offset.y,
                        image.uv_scale.x, image.uv_scale.y };
                    const float fill = (std::min)((std::max)(image.fill_amount, 0.0f),
                        1.0f);
                    if (image.fill_method == UIImageComponent::Horizontal)
                    {
                        source.z *= fill;
                        uv.z *= fill;
                    }
                    else if (image.fill_method == UIImageComponent::Vertical)
                    {
                        source.w *= fill;
                        uv.w *= fill;
                    }
                    DirectX::XMFLOAT4X4 identity{};
                    DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                    append_quad(source, identity, uv,
                        MultiplyAlpha(image.color, image.opacity * opacity), capture_scale);
                    configure_visual(image.fill_color_2, image.fill_mode, image.fill_angle,
                        image.fill_center, image.stroke_color_2, image.stroke_mode,
                        false, 0.0f, {}, {}, {});
                    Flush(context, TextureFor(image.sprite.guid, asset_database),
                        BlendForImage(image, states), states, nullptr);
                });
        };

        const auto render_text_effect_with_backdrop = [&](const Core::GameObject& object,
            const UIEffectStackComponent& effects, UITextComponent& text,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!text.ActiveInHierarchy() || text.opacity <= 0.0f || text.ResolvedText().empty())
                return false;
            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            return render_effect_with_backdrop(effects, rect, source_rect, scale,
                scissor, [&](const DirectX::XMFLOAT4& draw_rect, float capture_scale)
                {
                    font_atlas.BuildGlyphs(text, source_rect.z, source_rect.w,
                        asset_database);
                    DirectX::XMFLOAT4X4 identity{};
                    DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                    append_text_glyphs(object, text, draw_rect, identity,
                        MultiplyAlpha(text.color, text.opacity * opacity), capture_scale);
                    configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0, true,
                        text.outline_width, text.outline_color,
                        text.shadow_offset, text.shadow_color);
                    Flush(context, font_atlas.Texture(), states.blend_alpha, states, nullptr);
                });
        };

        const auto render_effect_preview = [&](const UIEffectStackComponent& effects,
            UIImageComponent& image, const RectTransformComponent& rect, float scale,
            float opacity, const D3D11_RECT* scissor)
        {
            if (!effects.HasActiveEffects(asset_database) || !image.ActiveInHierarchy() ||
                image.opacity <= 0.0f || image.fill_amount <= 0.0f)
            {
                return false;
            }

            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            const DirectX::XMFLOAT4 expansion = effects.ExpandBounds(
                source_rect.z * scale, source_rect.w * scale, asset_database);
            // Effect の変位量は target_size.zw を使う実ピクセル単位なので、
            // RT の確保量へ Canvas 拡大率を掛けてはいけない。
            // 一方 composite_rect は論理単位で積まれ、描画時に scale が掛かる。
            // したがって確保量は「論理単位へ戻して」から矩形へ足す。
            // ここを実ピクセルのまま足すと、RT の実幅が
            // source*scale + expansion なのに矩形の実幅が (source + expansion)*scale となり、
            // scale != 1 のとき結果が縮んで位置もずれる。
            const float inverse_scale = 1.0f / (std::max)(0.0001f, scale);
            const float expand_left = expansion.x * inverse_scale;
            const float expand_top = expansion.y * inverse_scale;
            const float expand_right = expansion.z * inverse_scale;
            const float expand_bottom = expansion.w * inverse_scale;
            const float expanded_width = (std::max)(1.0f,
                source_rect.z + expand_left + expand_right);
            const float expanded_height = (std::max)(1.0f,
                source_rect.w + expand_top + expand_bottom);
            const std::uint32_t rt_width = static_cast<std::uint32_t>(
                std::ceil((std::max)(1.0f, expanded_width * scale)));
            const std::uint32_t rt_height = static_cast<std::uint32_t>(
                std::ceil((std::max)(1.0f, expanded_height * scale)));
            UIRenderTarget* target = render_target_pool_.Acquire(rt_width, rt_height);
            UIRenderTarget* scratch = render_target_pool_.Acquire(rt_width, rt_height);
            if (target == nullptr || scratch == nullptr ||
                !target->rtv || !target->srv || !scratch->rtv || !scratch->srv)
            {
                return false;
            }

            ID3D11RenderTargetView* previous_rtv = nullptr;
            ID3D11DepthStencilView* previous_dsv = nullptr;
            context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
            UINT viewport_count = 1;
            D3D11_VIEWPORT previous_viewport{};
            context->RSGetViewports(&viewport_count, &previous_viewport);

            const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            configure_effect_target(*target);
            context->ClearRenderTargetView(target->rtv.Get(), clear);

            DirectX::XMFLOAT4 draw_rect{
                expansion.x,
                expansion.y,
                source_rect.z,
                source_rect.w };
            DirectX::XMFLOAT4 uv{ image.uv_offset.x, image.uv_offset.y,
                image.uv_scale.x, image.uv_scale.y };
            const float fill = (std::min)((std::max)(image.fill_amount, 0.0f), 1.0f);
            if (image.fill_method == UIImageComponent::Horizontal)
            {
                draw_rect.z *= fill;
                uv.z *= fill;
            }
            else if (image.fill_method == UIImageComponent::Vertical)
            {
                draw_rect.w *= fill;
                uv.w *= fill;
            }

            DirectX::XMFLOAT4X4 identity{};
            DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
            append_quad(draw_rect, identity, uv,
                MultiplyAlpha(image.color, image.opacity * opacity), scale);
            configure_visual(image.fill_color_2, image.fill_mode, image.fill_angle,
                image.fill_center, image.stroke_color_2, image.stroke_mode,
                false, 0.0f, {}, {}, {});
            Flush(context, TextureFor(image.sprite.guid, asset_database),
                states.blend_alpha, states, nullptr);

            ID3D11ShaderResourceView* null_srv = nullptr;
            context->PSSetShaderResources(0, 1, &null_srv);
            UIRenderTarget* current = target;
            apply_effect_passes(effects, current, target, scratch);

            context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
            if (viewport_count > 0) context->RSSetViewports(1, &previous_viewport);
            if (previous_rtv != nullptr) previous_rtv->Release();
            if (previous_dsv != nullptr) previous_dsv->Release();

            constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                &constants, 0, 0);
            draw_target_height = screen_height;

            DirectX::XMFLOAT4 composite_rect{
                source_rect.x - expand_left,
                source_rect.y - expand_top,
                expanded_width,
                expanded_height };
            append_quad(composite_rect, rect.ResolvedMatrix(),
                { 0.0f, 0.0f, 1.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f, 1.0f }, scale);
            configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                false, 0.0f, {}, {}, {});
            Flush(context, current->srv.Get(), BlendForImage(image, states),
                states, scissor);
            return true;
        };

        const auto render_text_effect_preview = [&](const Core::GameObject& object,
            const UIEffectStackComponent& effects, UITextComponent& text,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!effects.HasActiveEffects(asset_database) || !text.ActiveInHierarchy() ||
                text.opacity <= 0.0f || text.ResolvedText().empty())
            {
                return false;
            }

            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            const DirectX::XMFLOAT4 expansion = effects.ExpandBounds(
                source_rect.z * scale, source_rect.w * scale, asset_database);
            // Text も Image と同じ扱い。確保量は実ピクセルなので、
            // 論理単位の composite_rect へ足す前に拡大率で割り戻す。
            const float inverse_scale = 1.0f / (std::max)(0.0001f, scale);
            const float expand_left = expansion.x * inverse_scale;
            const float expand_top = expansion.y * inverse_scale;
            const float expand_right = expansion.z * inverse_scale;
            const float expand_bottom = expansion.w * inverse_scale;
            const float expanded_width = (std::max)(1.0f,
                source_rect.z + expand_left + expand_right);
            const float expanded_height = (std::max)(1.0f,
                source_rect.w + expand_top + expand_bottom);
            const std::uint32_t rt_width = static_cast<std::uint32_t>(
                std::ceil((std::max)(1.0f, expanded_width * scale)));
            const std::uint32_t rt_height = static_cast<std::uint32_t>(
                std::ceil((std::max)(1.0f, expanded_height * scale)));
            UIRenderTarget* target = render_target_pool_.Acquire(rt_width, rt_height);
            UIRenderTarget* scratch = render_target_pool_.Acquire(rt_width, rt_height);
            if (target == nullptr || scratch == nullptr ||
                !target->rtv || !target->srv || !scratch->rtv || !scratch->srv)
            {
                return false;
            }

            ID3D11RenderTargetView* previous_rtv = nullptr;
            ID3D11DepthStencilView* previous_dsv = nullptr;
            context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
            UINT viewport_count = 1;
            D3D11_VIEWPORT previous_viewport{};
            context->RSGetViewports(&viewport_count, &previous_viewport);

            const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            configure_effect_target(*target);
            context->ClearRenderTargetView(target->rtv.Get(), clear);

            font_atlas.BuildGlyphs(text, source_rect.z, source_rect.w, asset_database);
            const DirectX::XMFLOAT4 color =
                MultiplyAlpha(text.color, text.opacity * opacity);
            DirectX::XMFLOAT4X4 identity{};
            DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
            const auto draw_text_source = [&]()
            {
                append_text_glyphs(object, text, expansion, identity, color, scale);
                configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0, true,
                    text.outline_width, text.outline_color,
                    text.shadow_offset, text.shadow_color);
                Flush(context, font_atlas.Texture(), states.blend_alpha, states, nullptr);
            };

            draw_text_source();

            ID3D11ShaderResourceView* null_srv = nullptr;
            context->PSSetShaderResources(0, 1, &null_srv);
            UIRenderTarget* current = target;
            apply_effect_passes(effects, current, target, scratch);

            context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
            if (viewport_count > 0) context->RSSetViewports(1, &previous_viewport);
            if (previous_rtv != nullptr) previous_rtv->Release();
            if (previous_dsv != nullptr) previous_dsv->Release();

            constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                &constants, 0, 0);
            draw_target_height = screen_height;

            DirectX::XMFLOAT4 composite_rect{
                source_rect.x - expand_left,
                source_rect.y - expand_top,
                expanded_width,
                expanded_height };
            append_quad(composite_rect, rect.ResolvedMatrix(),
                { 0.0f, 0.0f, 1.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f, 1.0f }, scale);
            configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                false, 0.0f, {}, {}, {});
            Flush(context, current->srv.Get(), states.blend_alpha, states, scissor);
            return true;
        };

        std::function<void(Core::GameObject&, float, float, const D3D11_RECT*, int, bool)>
            render_object;
        render_object = [&](Core::GameObject& object, float scale, float opacity,
            const D3D11_RECT* inherited_scissor, int depth, bool backdrop_allowed)
        {
            if (depth > maximum_ui_depth || object.PendingDestroy() || !object.ActiveInHierarchy())
                return;

            RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
            D3D11_RECT local_scissor{};
            const D3D11_RECT* active_scissor = inherited_scissor;
            const UIMaskComponent* special_mask = nullptr;
            bool render_self = true;

            if (rect != nullptr)
            {
                if (const UIMaskComponent* mask = object.GetComponent<UIMaskComponent>())
                {
                    if (mask->ActiveInHierarchy() && mask->enabled_mask)
                    {
                        if (mask->mask_mode == UIMaskComponent::Rectangle)
                        {
                            local_scissor = MakeScissor(*rect, scale,
                                screen_width, screen_height);
                            if (inherited_scissor != nullptr)
                                local_scissor = IntersectScissor(*inherited_scissor,
                                    local_scissor);
                            active_scissor = &local_scissor;
                        }
                        else
                        {
                            // 画像／形状 Mask は既存 Effect Stack の Mask pass へ送る。
                            // ここでは新しい子描画経路を作らず、下の offscreen 合成だけを行う。
                            special_mask = mask;
                        }
                        render_self = mask->show_mask_graphic;
                    }
                }
            }

            if (rect != nullptr && render_self)
            {
                UIEffectStackComponent* effects =
                    object.GetComponent<UIEffectStackComponent>();
                if (UIShapeComponent* shape = object.GetComponent<UIShapeComponent>())
                {
                    render_shape(*shape, *rect, scale, opacity, active_scissor);
                }
                if (UIImageComponent* image = object.GetComponent<UIImageComponent>())
                {
                    const bool backdrop_rendered = backdrop_allowed && effects != nullptr &&
                        effects->capture_backdrop &&
                        render_image_effect_with_backdrop(*effects, *image, *rect, scale,
                            opacity, active_scissor);
                    if (!backdrop_rendered && (effects == nullptr ||
                        !render_effect_preview(*effects, *image, *rect, scale,
                            opacity, active_scissor)))
                    {
                        render_image(*image, *rect, scale, opacity, active_scissor);
                    }
                }
                if (UITextComponent* text = object.GetComponent<UITextComponent>())
                {
                    const bool backdrop_rendered = backdrop_allowed && effects != nullptr &&
                        effects->capture_backdrop &&
                        render_text_effect_with_backdrop(object, *effects, *text, *rect,
                            scale, opacity, active_scissor);
                    if (!backdrop_rendered && (effects == nullptr ||
                        !render_text_effect_preview(object, *effects, *text, *rect, scale,
                            opacity, active_scissor)))
                    {
                        render_text(object, *text, *rect, scale, opacity, active_scissor);
                    }
                }
                render_focus_outline(object, *rect, scale, opacity, active_scissor);
                render_scrollbars(object, *rect, scale, opacity, active_scissor);
            }

            if (special_mask != nullptr)
            {
                const std::uint32_t target_width = static_cast<std::uint32_t>(
                    (std::max)(1.0f, std::ceil(screen_width * scale)));
                const std::uint32_t target_height = static_cast<std::uint32_t>(
                    (std::max)(1.0f, std::ceil(screen_height * scale)));
                UIRenderTarget* target = render_target_pool_.Acquire(
                    target_width, target_height);
                UIRenderTarget* scratch = render_target_pool_.Acquire(
                    target_width, target_height);

                if (target != nullptr && scratch != nullptr && target->rtv &&
                    target->srv && scratch->rtv && scratch->srv)
                {
                    ID3D11RenderTargetView* previous_rtv = nullptr;
                    ID3D11DepthStencilView* previous_dsv = nullptr;
                    context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
                    UINT viewport_count = 1;
                    D3D11_VIEWPORT previous_viewport{};
                    context->RSGetViewports(&viewport_count, &previous_viewport);

                    const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                    configure_effect_target(*target);
                    context->ClearRenderTargetView(target->rtv.Get(), clear);
                     std::vector<Core::GameObject*> ordered_children = object.Children();
                     std::stable_sort(ordered_children.begin(), ordered_children.end(),
                         [](const Core::GameObject* lhs, const Core::GameObject* rhs)
                         {
                             const RectTransformComponent* a = lhs != nullptr
                                 ? lhs->GetComponent<RectTransformComponent>() : nullptr;
                             const RectTransformComponent* b = rhs != nullptr
                                 ? rhs->GetComponent<RectTransformComponent>() : nullptr;
                             return (a != nullptr ? a->sort_order : 0) <
                                 (b != nullptr ? b->sort_order : 0);
                         });
                     for (Core::GameObject* child : ordered_children)
                     {
                         if (child != nullptr)
                             render_object(*child, scale, opacity,
                                inherited_scissor, depth + 1, false);
                    }

                    UIEffectStackComponent mask_effects;
                    UI::UIEffect mask_effect;
                    mask_effect.kind = static_cast<int>(UI::UIEffectKind::Mask);
                    mask_effect.softness = Clamp01(special_mask->softness);
                    const DirectX::XMFLOAT4 mask_rect = rect != nullptr
                        ? rect->ResolvedRect() : DirectX::XMFLOAT4{};
                    mask_effect.direction = {
                        (mask_rect.x + mask_rect.z * 0.5f) /
                            (std::max)(1.0f, screen_width),
                        (mask_rect.y + mask_rect.w * 0.5f) /
                            (std::max)(1.0f, screen_height) };
                    mask_effect.seed = mask_rect.z /
                        (std::max)(1.0f, screen_width) * 0.5f;
                    mask_effect.speed = mask_rect.w /
                        (std::max)(1.0f, screen_height) * 0.5f;
                    if (special_mask->mask_mode == UIMaskComponent::Shape)
                    {
                        mask_effect.amount = 1.0f;
                    }
                    else
                    {
                        mask_effect.mask = special_mask->mask_image.guid;
                    }
                    mask_effects.effects.push_back(mask_effect);
                    UIRenderTarget* current = target;
                    apply_effect_passes(mask_effects, current, target, scratch);

                    context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
                    if (viewport_count > 0)
                        context->RSSetViewports(1, &previous_viewport);
                    if (previous_rtv != nullptr) previous_rtv->Release();
                    if (previous_dsv != nullptr) previous_dsv->Release();

                    constants.screen_size = {
                        screen_width, screen_height, 0.0f, 0.0f };
                    context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                        &constants, 0, 0);
                    draw_target_height = screen_height;
                    DirectX::XMFLOAT4X4 identity{};
                    DirectX::XMStoreFloat4x4(&identity,
                        DirectX::XMMatrixIdentity());
                    append_quad({ 0.0f, 0.0f, screen_width, screen_height },
                        identity, { 0.0f, 0.0f, 1.0f, 1.0f },
                        { 1.0f, 1.0f, 1.0f, 1.0f }, scale);
                    configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                        false, 0.0f, {}, {}, {});
                    Flush(context, current->srv.Get(), states.blend_alpha,
                        states, inherited_scissor);
                    return;
                }
            }

             std::vector<Core::GameObject*> ordered_children = object.Children();
             std::stable_sort(ordered_children.begin(), ordered_children.end(),
                 [](const Core::GameObject* lhs, const Core::GameObject* rhs)
                 {
                     const RectTransformComponent* a = lhs != nullptr
                         ? lhs->GetComponent<RectTransformComponent>() : nullptr;
                     const RectTransformComponent* b = rhs != nullptr
                         ? rhs->GetComponent<RectTransformComponent>() : nullptr;
                     return (a != nullptr ? a->sort_order : 0) <
                         (b != nullptr ? b->sort_order : 0);
                 });
             for (Core::GameObject* child : ordered_children)
             {
                 if (child != nullptr)
                     render_object(*child, scale, opacity, active_scissor, depth + 1,
                        backdrop_allowed);
            }
        };

        for (Core::GameObject* canvas_object : canvases)
        {
            if (canvas_object == nullptr) continue;
            CanvasComponent* canvas = canvas_object->GetComponent<CanvasComponent>();
            if (canvas == nullptr || !canvas->ActiveInHierarchy()) continue;

            const float scale = UILayout::CanvasScale(*canvas, screen_width, screen_height);
            const float safe_scale = scale > 0.0001f ? scale : 1.0f;
            // UILayout の解決矩形は共通のまま、World Space だけを Canvas の
            // ワールド変換とカメラ行列で投影する。平面の高さを 1 とし、
            // reference_resolution の比率で幅を決める。
            world_space_canvas_ = canvas->render_mode == CanvasComponent::WorldSpace;
            if (world_space_canvas_)
            {
                const float reference_width = canvas->reference_resolution.x > 0.0f
                    ? canvas->reference_resolution.x : 1920.0f;
                const float reference_height = canvas->reference_resolution.y > 0.0f
                    ? canvas->reference_resolution.y : 1080.0f;
                constants.world_canvas_params = {
                    1.0f, reference_width / reference_height, 1.0f, 0.0f };
                DirectX::XMStoreFloat4x4(&constants.world_canvas_matrix,
                    canvas_object->GetTransform().WorldMatrix());
            }
            else
            {
                constants.world_canvas_params = { 0.0f, 0.0f, 0.0f, 0.0f };
                DirectX::XMStoreFloat4x4(&constants.world_canvas_matrix,
                    DirectX::XMMatrixIdentity());
            }
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                &constants, 0, 0);
            render_object(*canvas_object, safe_scale,
                (std::min)((std::max)(canvas->opacity, 0.0f), 1.0f), nullptr, 0, true);
        }
        world_space_canvas_ = false;

        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
        context->RSSetScissorRects(0, nullptr);
    }
}
