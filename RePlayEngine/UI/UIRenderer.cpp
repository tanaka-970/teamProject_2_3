#include "UIRenderer.h"

#include "FontAtlas.h"
#include "UILayout.h"
#include "../Assets/AssetDatabase.h"
#include "../Components/UI/CanvasComponent.h"
#include "../Components/UI/RectTransformComponent.h"
#include "../Components/UI/UIImageComponent.h"
#include "../Components/UI/UIMaskComponent.h"
#include "../Components/UI/UIEffectStackComponent.h"
#include "../Components/UI/UIShapeComponent.h"
#include "../Components/UI/UITextComponent.h"
#include "../Components/UI/UITextAnimatorComponent.h"
#include "../Object/GameObject/GameObject.h"
#include "../Rendering/Shaders/ShaderCatalog.h"
#include "../Rendering/Shaders/ShaderAsset.h"
#include "../Scene/Runtime/Scene.h"
#include "../../Source/core/shader.h"
#include "../../Source/core/texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
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
        using Components::UIShapeComponent;
        using Components::UITextComponent;
        using Components::UITextAnimatorComponent;

        constexpr int maximum_ui_depth = 64;

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

    bool UIRenderer::Initialize(ID3D11Device* device)
    {
        Release();
        if (device == nullptr) return false;
        device_ = device;

        D3D11_INPUT_ELEMENT_DESC input_desc[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
                D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        if (FAILED(create_vs_from_cso(device, "ui_vs.cso",
            vertex_shader_.GetAddressOf(), input_layout_.GetAddressOf(),
            input_desc, static_cast<UINT>(sizeof(input_desc) / sizeof(input_desc[0])))))
            return false;

        if (FAILED(create_ps_from_cso(device, "ui_ps.cso",
            pixel_shader_.GetAddressOf())))
            return false;

        const std::array<const char*, effect_shader_count> effect_cso_names{
            "ui_effect_blur.cso",
            "ui_effect_glow.cso",
            "ui_effect_color_adjust.cso",
            "ui_effect_noise.cso",
            "ui_effect_shake.cso",
            "ui_effect_mask.cso",
            "ui_effect_wipe.cso",
            "ui_effect_dissolve.cso",
            "ui_effect_distortion.cso",
            "ui_effect_chromatic_aberration.cso",
        };
        for (std::size_t index = 0; index < effect_cso_names.size(); ++index)
        {
            if (FAILED(create_ps_from_cso(device, effect_cso_names[index],
                effect_pixel_shaders_[index].GetAddressOf())))
                return false;
        }

        D3D11_BUFFER_DESC cb_desc{};
        cb_desc.ByteWidth = sizeof(Constants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&cb_desc, nullptr, constant_buffer_.GetAddressOf())))
            return false;

        cb_desc.ByteWidth = sizeof(EffectConstants);
        if (FAILED(device->CreateBuffer(&cb_desc, nullptr,
            effect_constant_buffer_.GetAddressOf())))
            return false;

        if (FAILED(make_dummy_texture(device, white_texture_.GetAddressOf(),
            0xFFFFFFFFu, 1)))
            return false;

        render_target_pool_.Initialize(device);
        return true;
    }

    void UIRenderer::Release() noexcept
    {
        render_target_pool_.Release();
        texture_cache_.clear();
        custom_effect_shader_cache_.clear();
        vertices_.clear();
        vertex_capacity_ = 0;
        white_texture_.Reset();
        effect_constant_buffer_.Reset();
        constant_buffer_.Reset();
        vertex_buffer_.Reset();
        input_layout_.Reset();
        for (Microsoft::WRL::ComPtr<ID3D11PixelShader>& shader : effect_pixel_shaders_)
        {
            shader.Reset();
        }
        pixel_shader_.Reset();
        vertex_shader_.Reset();
        device_.Reset();
    }

    void UIRenderer::ReleaseTransientTargets() noexcept
    {
        render_target_pool_.Release();
    }

    bool UIRenderer::EnsureVertexCapacity(ID3D11Device* device, std::size_t vertex_count)
    {
        if (vertex_count <= vertex_capacity_) return true;
        std::size_t next_capacity = (std::max)(std::size_t{ 6 }, vertex_capacity_);
        while (next_capacity < vertex_count) next_capacity *= 2;

        vertex_buffer_.Reset();
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * next_capacity);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&desc, nullptr, vertex_buffer_.GetAddressOf())))
        {
            vertex_capacity_ = 0;
            return false;
        }
        vertex_capacity_ = next_capacity;
        return true;
    }

    ID3D11ShaderResourceView* UIRenderer::TextureFor(const std::string& guid,
        const Assets::AssetDatabase* asset_database)
    {
        if (guid.empty() || asset_database == nullptr) return white_texture_.Get();
        if (const auto it = texture_cache_.find(guid); it != texture_cache_.end())
            return it->second.Get();

        const Assets::AssetRecord* record = asset_database->FindByGuid(guid);
        if (record == nullptr || record->kind != Assets::AssetKind::Image)
            return white_texture_.Get();

        const std::filesystem::path path =
            record->cache_path.empty() ? record->source_path : record->cache_path;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> loaded;
        if (FAILED(load_texture_from_file(device_.Get(), path.wstring().c_str(),
            loaded.GetAddressOf(), nullptr)) || !loaded)
            return white_texture_.Get();

        texture_cache_[guid] = loaded;
        return loaded.Get();
    }

    ID3D11PixelShader* UIRenderer::EffectShaderFor(UIEffectKind kind) const noexcept
    {
        const int index = static_cast<int>(kind);
        if (index < 0 || index >= static_cast<int>(effect_pixel_shaders_.size()))
        {
            return nullptr;
        }
        return effect_pixel_shaders_[static_cast<std::size_t>(index)].Get();
    }

    ID3D11PixelShader* UIRenderer::CustomEffectShaderFor(
        const std::string& shader_guid,
        const Assets::AssetDatabase* asset_database,
        const Rendering::ShaderCatalog* shader_catalog)
    {
        if (shader_guid.empty() || asset_database == nullptr ||
            shader_catalog == nullptr || device_ == nullptr)
        {
            return nullptr;
        }
        if (const auto found = custom_effect_shader_cache_.find(shader_guid);
            found != custom_effect_shader_cache_.end())
        {
            return found->second.Get();
        }

        const Assets::AssetRecord* record = asset_database->FindByGuid(shader_guid);
        if (record == nullptr || record->kind != Assets::AssetKind::Shader)
            return nullptr;

        const auto normalize = [](std::filesystem::path path)
        {
            std::error_code error;
            std::filesystem::path absolute = path.is_absolute()
                ? path : std::filesystem::absolute(path, error);
            if (error) absolute = path;
            error.clear();
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(absolute, error);
            return error ? absolute.lexically_normal() : canonical.lexically_normal();
        };

        const std::filesystem::path source_path = normalize(record->source_path);
        const Rendering::ShaderCatalog::Entry* matched = nullptr;
        for (const Rendering::ShaderCatalog::Entry& entry : shader_catalog->All())
        {
            if (entry.info.domain != Rendering::ShaderDomain::PostProcess) continue;
            if (normalize(entry.info.source_path) == source_path)
            {
                matched = &entry;
                break;
            }
        }
        if (matched == nullptr) return nullptr;

        const Rendering::ShaderCatalog::VariantResult& variant =
            matched->At(Rendering::ShaderVariant::Static);
        if (!variant.compiled || !variant.bytecode) return nullptr;

        Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
        if (FAILED(device_->CreatePixelShader(
            variant.bytecode->GetBufferPointer(),
            variant.bytecode->GetBufferSize(), nullptr, shader.GetAddressOf())) ||
            !shader)
        {
            return nullptr;
        }

        ID3D11PixelShader* result = shader.Get();
        custom_effect_shader_cache_[shader_guid] = shader;
        return result;
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
        if (pixel_constant_buffer != nullptr)
        {
            ID3D11Buffer* ps_cb = pixel_constant_buffer;
            context->PSSetConstantBuffers(0, 1, &ps_cb);
        }
        ID3D11ShaderResourceView* srv = texture != nullptr ? texture : white_texture_.Get();
        context->PSSetShaderResources(0, 1, &srv);
        ID3D11SamplerState* sampler = states.sampler;
        context->PSSetSamplers(0, 1, &sampler);
        context->OMSetDepthStencilState(states.depth_disabled, 0);
        context->OMSetBlendState(blend_state != nullptr ? blend_state : states.blend_alpha,
            nullptr, 0xFFFFFFFF);

        if (scissor != nullptr)
        {
            context->RSSetState(states.rasterizer_scissor != nullptr
                ? states.rasterizer_scissor : states.rasterizer);
            context->RSSetScissorRects(1, scissor);
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
        const RenderStates& states)
    {
        if (context == nullptr || device_ == nullptr || !vertex_shader_ || !pixel_shader_)
            return;

        Constants constants{};
        constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
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
        const auto append_quad = [this, &draw_target_height](const DirectX::XMFLOAT4& rect,
            const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& uv,
            const DirectX::XMFLOAT4& color, float scale)
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

            vertices_.push_back({ p0, uv0, color });
            vertices_.push_back({ p3, uv3, color });
            vertices_.push_back({ p2, uv2, color });
            vertices_.push_back({ p0, uv0, color });
            vertices_.push_back({ p2, uv2, color });
            vertices_.push_back({ p1, uv1, color });
        };

        const auto append_quad_local =
            [this, &draw_target_height](const DirectX::XMFLOAT4& rect,
                const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& uv,
                const DirectX::XMFLOAT4& color, float scale,
                const DirectX::XMFLOAT2& local_scale, float rotation_degrees,
                const DirectX::XMFLOAT2& anchor)
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

            vertices_.push_back({ p0, uv0, color });
            vertices_.push_back({ p3, uv3, color });
            vertices_.push_back({ p2, uv2, color });
            vertices_.push_back({ p0, uv0, color });
            vertices_.push_back({ p2, uv2, color });
            vertices_.push_back({ p1, uv1, color });
        };

        const auto append_triangle_local =
            [this, &draw_target_height](const DirectX::XMFLOAT2& a,
                const DirectX::XMFLOAT2& b, const DirectX::XMFLOAT2& c,
                const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& color,
                float scale)
        {
            const DirectX::XMFLOAT2 uv{ 0.0f, 0.0f };
            const DirectX::XMFLOAT2 p0 = ToScreenPoint(
                TransformPoint(matrix, a.x, a.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p1 = ToScreenPoint(
                TransformPoint(matrix, b.x, b.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p2 = ToScreenPoint(
                TransformPoint(matrix, c.x, c.y), scale, draw_target_height);
            vertices_.push_back({ p0, uv, color });
            vertices_.push_back({ p1, uv, color });
            vertices_.push_back({ p2, uv, color });
        };

        const auto append_line_segment_local =
            [this, &draw_target_height](const DirectX::XMFLOAT2& a,
                const DirectX::XMFLOAT2& b, const DirectX::XMFLOAT4X4& matrix,
                const DirectX::XMFLOAT4& color, float width, float scale)
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

            vertices_.push_back({ q0, uv, color });
            vertices_.push_back({ q1, uv, color });
            vertices_.push_back({ q2, uv, color });
            vertices_.push_back({ q0, uv, color });
            vertices_.push_back({ q2, uv, color });
            vertices_.push_back({ q3, uv, color });
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
            const DirectX::XMFLOAT4& rect, bool& closed)
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
                const float cx = rect.x + rect.z * 0.5f;
                const float cy = rect.y + rect.w * 0.5f;
                const float rx = rect.z * 0.5f;
                const float ry = rect.w * 0.5f;
                append_arc(cx, cy, rx, ry, 0.0f, pi * 2.0f, 64);
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
                    append_line_segment_local(a, b, matrix, color, stroke_width, scale);
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
                            lerp_point(a, b, tb), matrix, color, stroke_width, scale);
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
            build_shape_path(shape, r, closed);
            if (shape_path.empty()) return;

            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            if (closed && shape.shape != UIShapeComponent::Line &&
                shape.fill_color.w * opacity > 0.0f && shape_path.size() >= 3)
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
                for (std::size_t index = 0; index < shape_path.size(); ++index)
                {
                    append_triangle_local(center, shape_path[index],
                        shape_path[(index + 1) % shape_path.size()],
                        matrix, fill, scale);
                }
            }

            float stroke_width = shape.stroke_width;
            DirectX::XMFLOAT4 stroke = shape.stroke_color;
            if (shape.shape == UIShapeComponent::Line && stroke_width <= 0.0f)
            {
                stroke_width = 1.0f;
                stroke = shape.fill_color;
            }
            append_stroked_path(shape, matrix, MultiplyAlpha(stroke, opacity),
                stroke_width, scale, closed);
            Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
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
                static_cast<float>(glyphs.size()));

            for (const UITextComponent::GlyphQuad& glyph : glyphs)
            {
                DirectX::XMFLOAT4 glyph_rect{
                    origin.x + glyph.position.x,
                    origin.y + glyph.position.y,
                    glyph.size.x,
                    glyph.size.y
                };
                DirectX::XMFLOAT4 color = base_color;
                DirectX::XMFLOAT2 local_scale{ 1.0f, 1.0f };
                DirectX::XMFLOAT2 anchor{ 0.5f, 0.5f };
                float rotation = 0.0f;
                bool transformed = false;

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
                        { base_color.x * animator->color.x,
                          base_color.y * animator->color.y,
                          base_color.z * animator->color.z,
                          base_color.w * animator->color.w },
                        influence);
                    color.w *= Lerp(1.0f, animator->opacity, influence);
                    anchor = animator_anchor(animator->anchor);
                    transformed = true;
                }

                if (transformed)
                {
                    append_quad_local(glyph_rect, matrix, glyph.uv, color, scale,
                        local_scale, rotation, anchor);
                }
                else
                {
                    append_quad(glyph_rect, matrix, glyph.uv, color, scale);
                }
            }
        };

        const auto render_text = [&](const Core::GameObject& object,
            UITextComponent& text, const RectTransformComponent& rect, float scale,
            float opacity, const D3D11_RECT* scissor)
        {
            if (!text.ActiveInHierarchy() || text.opacity <= 0.0f || text.text.empty())
                return;

            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            font_atlas.BuildGlyphs(text, r.z, r.w);
            const DirectX::XMFLOAT4 color = MultiplyAlpha(text.color, text.opacity * opacity);
            append_text_glyphs(object, text, r, rect.ResolvedMatrix(), color, scale);
            Flush(context, font_atlas.Texture(), states.blend_alpha, states, scissor);
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

            Constants offscreen_constants{};
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
            if (first == nullptr || second == nullptr || current == nullptr)
                return;

            DirectX::XMFLOAT4X4 identity{};
            DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
            const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            for (const UI::UIEffect& effect : effects.effects)
            {
                if (!effect.enabled) continue;
                ID3D11PixelShader* effect_shader =
                    CustomEffectShaderFor(effect.custom_shader,
                        asset_database, shader_catalog);
                if (effect_shader == nullptr)
                {
                    effect_shader = EffectShaderFor(
                        static_cast<UI::UIEffectKind>(effect.kind));
                }
                if (effect_shader == nullptr) continue;

                UIRenderTarget* destination = current == first ? second : first;
                configure_effect_target(*destination);
                context->ClearRenderTargetView(destination->rtv.Get(), clear);

                const float width = (std::max)(1.0f,
                    static_cast<float>(destination->width));
                const float height = (std::max)(1.0f,
                    static_cast<float>(destination->height));
                EffectConstants effect_constants{};
                effect_constants.effect_color = effect.color;
                effect_constants.effect_params0 = {
                    effect.radius, effect.intensity, effect.threshold, effect.amount };
                effect_constants.effect_params1 = {
                    effect.angle, effect.progress, effect.softness, effect.speed };
                effect_constants.effect_params2 = {
                    effect.direction.x, effect.direction.y, effect.seed, 0.0f };
                effect_constants.target_size = {
                    width, height, 1.0f / width, 1.0f / height };
                context->UpdateSubresource(effect_constant_buffer_.Get(), 0, nullptr,
                    &effect_constants, 0, 0);

                append_quad({ 0.0f, 0.0f, width, height }, identity,
                    { 0.0f, 0.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f);
                Flush(context, current->srv.Get(),
                    states.blend_none != nullptr ? states.blend_none : states.blend_alpha,
                    states, nullptr, effect_shader, effect_constant_buffer_.Get());

                ID3D11ShaderResourceView* null_srv = nullptr;
                context->PSSetShaderResources(0, 1, &null_srv);
                current = destination;
            }

            ID3D11Buffer* null_cb = nullptr;
            context->PSSetConstantBuffers(0, 1, &null_cb);
        };

        const auto render_effect_preview = [&](const UIEffectStackComponent& effects,
            UIImageComponent& image, const RectTransformComponent& rect, float scale,
            float opacity, const D3D11_RECT* scissor)
        {
            if (!effects.HasActiveEffects() || !image.ActiveInHierarchy() ||
                image.opacity <= 0.0f || image.fill_amount <= 0.0f)
            {
                return false;
            }

            const DirectX::XMFLOAT4 expansion = effects.ExpandBounds();
            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            const float expanded_width = (std::max)(1.0f,
                source_rect.z + expansion.x + expansion.z);
            const float expanded_height = (std::max)(1.0f,
                source_rect.w + expansion.y + expansion.w);
            const std::uint32_t rt_width = static_cast<std::uint32_t>(
                std::ceil(expanded_width * scale));
            const std::uint32_t rt_height = static_cast<std::uint32_t>(
                std::ceil(expanded_height * scale));
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
                source_rect.x - expansion.x,
                source_rect.y - expansion.y,
                expanded_width,
                expanded_height };
            append_quad(composite_rect, rect.ResolvedMatrix(),
                { 0.0f, 0.0f, 1.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f, 1.0f }, scale);
            Flush(context, current->srv.Get(), BlendForImage(image, states),
                states, scissor);
            return true;
        };

        const auto render_text_effect_preview = [&](const Core::GameObject& object,
            const UIEffectStackComponent& effects, UITextComponent& text,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!effects.HasActiveEffects() || !text.ActiveInHierarchy() ||
                text.opacity <= 0.0f || text.text.empty())
            {
                return false;
            }

            const DirectX::XMFLOAT4 expansion = effects.ExpandBounds();
            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            const float expanded_width = (std::max)(1.0f,
                source_rect.z + expansion.x + expansion.z);
            const float expanded_height = (std::max)(1.0f,
                source_rect.w + expansion.y + expansion.w);
            const std::uint32_t rt_width = static_cast<std::uint32_t>(
                std::ceil(expanded_width * scale));
            const std::uint32_t rt_height = static_cast<std::uint32_t>(
                std::ceil(expanded_height * scale));
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

            font_atlas.BuildGlyphs(text, source_rect.z, source_rect.w);
            const DirectX::XMFLOAT4 color =
                MultiplyAlpha(text.color, text.opacity * opacity);
            DirectX::XMFLOAT4X4 identity{};
            DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
            const auto draw_text_source = [&]()
            {
                append_text_glyphs(object, text, expansion, identity, color, scale);
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
                source_rect.x - expansion.x,
                source_rect.y - expansion.y,
                expanded_width,
                expanded_height };
            append_quad(composite_rect, rect.ResolvedMatrix(),
                { 0.0f, 0.0f, 1.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f, 1.0f }, scale);
            Flush(context, current->srv.Get(), states.blend_alpha, states, scissor);
            return true;
        };

        std::function<void(Core::GameObject&, float, float, const D3D11_RECT*, int)> render_object;
        render_object = [&](Core::GameObject& object, float scale, float opacity,
            const D3D11_RECT* inherited_scissor, int depth)
        {
            if (depth > maximum_ui_depth || object.PendingDestroy() || !object.ActiveInHierarchy())
                return;

            RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
            D3D11_RECT local_scissor{};
            const D3D11_RECT* active_scissor = inherited_scissor;
            bool render_self = true;

            if (rect != nullptr)
            {
                if (const UIMaskComponent* mask = object.GetComponent<UIMaskComponent>())
                {
                    if (mask->ActiveInHierarchy() && mask->enabled_mask)
                    {
                        local_scissor = MakeScissor(*rect, scale, screen_width, screen_height);
                        if (inherited_scissor != nullptr)
                            local_scissor = IntersectScissor(*inherited_scissor, local_scissor);
                        active_scissor = &local_scissor;
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
                    if (effects == nullptr ||
                        !render_effect_preview(*effects, *image, *rect, scale,
                            opacity, active_scissor))
                    {
                        render_image(*image, *rect, scale, opacity, active_scissor);
                    }
                }
                if (UITextComponent* text = object.GetComponent<UITextComponent>())
                {
                    if (effects == nullptr ||
                        !render_text_effect_preview(object, *effects, *text, *rect, scale,
                            opacity, active_scissor))
                    {
                        render_text(object, *text, *rect, scale, opacity, active_scissor);
                    }
                }
            }

            for (Core::GameObject* child : object.Children())
            {
                if (child != nullptr)
                    render_object(*child, scale, opacity, active_scissor, depth + 1);
            }
        };

        for (Core::GameObject* canvas_object : canvases)
        {
            if (canvas_object == nullptr) continue;
            CanvasComponent* canvas = canvas_object->GetComponent<CanvasComponent>();
            if (canvas == nullptr || !canvas->ActiveInHierarchy()) continue;

            const float scale = UILayout::CanvasScale(*canvas, screen_width, screen_height);
            const float safe_scale = scale > 0.0001f ? scale : 1.0f;
            render_object(*canvas_object, safe_scale,
                (std::min)((std::max)(canvas->opacity, 0.0f), 1.0f), nullptr, 0);
        }

        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
        context->RSSetScissorRects(0, nullptr);
    }
}
