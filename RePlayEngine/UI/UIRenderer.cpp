#include "UIRenderer.h"

#include "FontAtlas.h"
#include "UILayout.h"
#include "../Assets/AssetDatabase.h"
#include "../Components/UI/CanvasComponent.h"
#include "../Components/UI/RectTransformComponent.h"
#include "../Components/UI/UIImageComponent.h"
#include "../Components/UI/UIMaskComponent.h"
#include "../Components/UI/UITextComponent.h"
#include "../Object/GameObject/GameObject.h"
#include "../Scene/Runtime/Scene.h"
#include "../../Source/core/shader.h"
#include "../../Source/core/texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace ReplayEngine::UI
{
    namespace
    {
        using Components::CanvasComponent;
        using Components::RectTransformComponent;
        using Components::UIImageComponent;
        using Components::UIMaskComponent;
        using Components::UITextComponent;

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

        D3D11_BUFFER_DESC cb_desc{};
        cb_desc.ByteWidth = sizeof(Constants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&cb_desc, nullptr, constant_buffer_.GetAddressOf())))
            return false;

        if (FAILED(make_dummy_texture(device, white_texture_.GetAddressOf(),
            0xFFFFFFFFu, 1)))
            return false;

        return true;
    }

    void UIRenderer::Release() noexcept
    {
        texture_cache_.clear();
        vertices_.clear();
        vertex_capacity_ = 0;
        white_texture_.Reset();
        constant_buffer_.Reset();
        vertex_buffer_.Reset();
        input_layout_.Reset();
        pixel_shader_.Reset();
        vertex_shader_.Reset();
        device_.Reset();
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

    void UIRenderer::Flush(ID3D11DeviceContext* context, ID3D11ShaderResourceView* texture,
        ID3D11BlendState* blend_state, const RenderStates& states, const D3D11_RECT* scissor)
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
        context->PSSetShader(pixel_shader_.Get(), nullptr, 0);
        ID3D11Buffer* cb = constant_buffer_.Get();
        context->VSSetConstantBuffers(0, 1, &cb);
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

        const auto append_quad = [this, screen_height](const DirectX::XMFLOAT4& rect,
            const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& uv,
            const DirectX::XMFLOAT4& color, float scale)
        {
            const DirectX::XMFLOAT2 p0 = ToScreenPoint(
                TransformPoint(matrix, rect.x, rect.y), scale, screen_height);
            const DirectX::XMFLOAT2 p1 = ToScreenPoint(
                TransformPoint(matrix, rect.x + rect.z, rect.y), scale, screen_height);
            const DirectX::XMFLOAT2 p2 = ToScreenPoint(
                TransformPoint(matrix, rect.x + rect.z, rect.y + rect.w), scale, screen_height);
            const DirectX::XMFLOAT2 p3 = ToScreenPoint(
                TransformPoint(matrix, rect.x, rect.y + rect.w), scale, screen_height);

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

        const auto render_text = [&](UITextComponent& text,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!text.ActiveInHierarchy() || text.opacity <= 0.0f || text.text.empty())
                return;

            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            font_atlas.BuildGlyphs(text, r.z, r.w);
            const DirectX::XMFLOAT4 color = MultiplyAlpha(text.color, text.opacity * opacity);
            for (const UITextComponent::GlyphQuad& glyph : text.Glyphs())
            {
                const DirectX::XMFLOAT4 glyph_rect{
                    r.x + glyph.position.x,
                    r.y + glyph.position.y,
                    glyph.size.x,
                    glyph.size.y
                };
                append_quad(glyph_rect, rect.ResolvedMatrix(), glyph.uv, color, scale);
            }
            Flush(context, font_atlas.Texture(), states.blend_alpha, states, scissor);
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
                if (UIImageComponent* image = object.GetComponent<UIImageComponent>())
                    render_image(*image, *rect, scale, opacity, active_scissor);
                if (UITextComponent* text = object.GetComponent<UITextComponent>())
                    render_text(*text, *rect, scale, opacity, active_scissor);
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
