// UI workspace のうち「Canvas プレビューの描画と選択」だけを持つ。
//
// Scene View の overlay は framework_ui_workspace_overlay.cpp に置く。

#include "framework.h"

#include "../../RePlayEngine/Components/UI/CanvasComponent.h"
#include "../../RePlayEngine/Components/UI/RectTransformComponent.h"
#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/UI/UIButtonComponent.h"
#include "../../RePlayEngine/Components/UI/UIMaskComponent.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../RePlayEngine/UI/UILayout.h"

// PushItemFlag / ImGuiItemFlags_Disabled を使うため。
// 同梱の ImGui は 1.80 WIP で BeginDisabled / EndDisabled がまだ無い。
// 既存の InspectorPanel.cpp / PropertyDrawer.cpp と同じ取り込み方に合わせる。
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "framework_ui_workspaceInternal.h"

    namespace
    {
    using ReplayEngine::Components::CanvasComponent;
    using ReplayEngine::Components::RectTransformComponent;
    using ReplayEngine::Components::UIImageComponent;
    using ReplayEngine::Components::UITextComponent;
    using ReplayEngine::Components::UIButtonComponent;
    using ReplayEngine::Components::UIMaskComponent;
    namespace Assets = ReplayEngine::Assets;
    namespace Core = ReplayEngine::Core;
    namespace Scene = ReplayEngine::Scene;
        using namespace framework_ui_workspace_detail;

    ImVec2 ToPreviewPoint(const DirectX::XMFLOAT2& canvas_point,
        const ImVec2& origin, float canvas_height, float zoom)
    {
        return ImVec2(origin.x + canvas_point.x * zoom,
            origin.y + (canvas_height - canvas_point.y) * zoom);
    }

    ImU32 ColorToU32(const DirectX::XMFLOAT4& color, float fallback_alpha = 1.0f)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            color.x, color.y, color.z, color.w * fallback_alpha));
    }

    void PreviewQuad(const RectTransformComponent& rect,
        const DirectX::XMFLOAT4& draw_rect, const ImVec2& origin,
        float canvas_height, float zoom, ImVec2 out[4])
    {
        const DirectX::XMFLOAT4X4 m = rect.ResolvedMatrix();
        out[0] = ToPreviewPoint(TransformPoint(m, draw_rect.x, draw_rect.y), origin, canvas_height, zoom);
        out[1] = ToPreviewPoint(TransformPoint(m, draw_rect.x + draw_rect.z, draw_rect.y), origin, canvas_height, zoom);
        out[2] = ToPreviewPoint(TransformPoint(m, draw_rect.x + draw_rect.z, draw_rect.y + draw_rect.w), origin, canvas_height, zoom);
        out[3] = ToPreviewPoint(TransformPoint(m, draw_rect.x, draw_rect.y + draw_rect.w), origin, canvas_height, zoom);
    }

    void PreviewQuad(const RectTransformComponent& rect, const ImVec2& origin,
        float canvas_height, float zoom, ImVec2 out[4])
    {
        PreviewQuad(rect, rect.ResolvedRect(), origin, canvas_height, zoom, out);
    }

    void ApplyUIImageFill(const UIImageComponent& image,
        DirectX::XMFLOAT4& draw_rect, DirectX::XMFLOAT4& uv)
    {
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
    }

    enum UIResizeHandle : int
    {
        ResizeBottomLeft = 0,
        ResizeBottom = 1,
        ResizeBottomRight = 2,
        ResizeRight = 3,
        ResizeTopRight = 4,
        ResizeTop = 5,
        ResizeTopLeft = 6,
        ResizeLeft = 7,
    };

    ImVec2 Midpoint(const ImVec2& a, const ImVec2& b) noexcept
    {
        return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    }

    void ResizeHandlePoints(const RectTransformComponent& rect,
        const ImVec2& origin, float canvas_height, float zoom, ImVec2 out[8])
    {
        ImVec2 corners[4]{};
        PreviewQuad(rect, origin, canvas_height, zoom, corners);
        out[ResizeBottomLeft] = corners[0];
        out[ResizeBottom] = Midpoint(corners[0], corners[1]);
        out[ResizeBottomRight] = corners[1];
        out[ResizeRight] = Midpoint(corners[1], corners[2]);
        out[ResizeTopRight] = corners[2];
        out[ResizeTop] = Midpoint(corners[2], corners[3]);
        out[ResizeTopLeft] = corners[3];
        out[ResizeLeft] = Midpoint(corners[3], corners[0]);
    }

    int HitResizeHandle(const ImVec2 points[8], const ImVec2& mouse,
        float radius = 7.0f) noexcept
    {
        const float radius_squared = radius * radius;
        for (int index = 0; index < 8; ++index)
        {
            const float dx = mouse.x - points[index].x;
            const float dy = mouse.y - points[index].y;
            if (dx * dx + dy * dy <= radius_squared) return index;
        }
        return -1;
    }

    DirectX::XMFLOAT2 CanvasPointFromScreen(const ImVec2& mouse,
        const ImVec2& origin, float canvas_height, float zoom) noexcept
    {
        return {
            (mouse.x - origin.x) / zoom,
            canvas_height - (mouse.y - origin.y) / zoom
        };
    }

    bool InverseTransformPoint(const DirectX::XMFLOAT4X4& matrix,
        const DirectX::XMFLOAT2& point, DirectX::XMFLOAT2& output) noexcept
    {
        const DirectX::XMMATRIX source = DirectX::XMLoadFloat4x4(&matrix);
        DirectX::XMVECTOR determinant{};
        const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant, source);
        const float determinant_value = DirectX::XMVectorGetX(determinant);
        if (!std::isfinite(determinant_value) || std::fabs(determinant_value) < 1.0e-8f)
            return false;
        const DirectX::XMVECTOR p = DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(point.x, point.y, 0.0f, 1.0f), inverse);
        const float x = DirectX::XMVectorGetX(p);
        const float y = DirectX::XMVectorGetY(p);
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        output = { x, y };
        return true;
    }

    DirectX::XMFLOAT4 ParentResolvedRect(const Core::GameObject& object,
        float canvas_width, float canvas_height) noexcept
    {
        const Core::GameObject* parent = object.Parent();
        const RectTransformComponent* parent_rect = parent != nullptr
            ? parent->GetComponent<RectTransformComponent>() : nullptr;
        if (parent_rect != nullptr) return parent_rect->ResolvedRect();
        return { 0.0f, 0.0f, canvas_width, canvas_height };
    }

    void ApplyResolvedRect(RectTransformComponent& rect,
        const DirectX::XMFLOAT4& parent, const DirectX::XMFLOAT4& desired) noexcept
    {
        const float width = (std::max)(1.0f, desired.z);
        const float height = (std::max)(1.0f, desired.w);
        const float anchor_span_x = parent.z * (rect.anchor_max.x - rect.anchor_min.x);
        const float anchor_span_y = parent.w * (rect.anchor_max.y - rect.anchor_min.y);
        const float anchor_min_x = parent.x + parent.z * rect.anchor_min.x;
        const float anchor_min_y = parent.y + parent.w * rect.anchor_min.y;

        rect.size_delta.x = width - anchor_span_x;
        rect.size_delta.y = height - anchor_span_y;
        rect.anchored_position.x = desired.x - anchor_min_x +
            rect.size_delta.x * rect.pivot.x;
        rect.anchored_position.y = desired.y - anchor_min_y +
            rect.size_delta.y * rect.pivot.y;
    }

    void ResizeDirections(int handle, int& horizontal, int& vertical) noexcept
    {
        horizontal = 0;
        vertical = 0;
        if (handle == ResizeBottomLeft || handle == ResizeLeft || handle == ResizeTopLeft)
            horizontal = -1;
        else if (handle == ResizeBottomRight || handle == ResizeRight || handle == ResizeTopRight)
            horizontal = 1;
        if (handle == ResizeBottomLeft || handle == ResizeBottom || handle == ResizeBottomRight)
            vertical = -1;
        else if (handle == ResizeTopLeft || handle == ResizeTop || handle == ResizeTopRight)
            vertical = 1;
    }

    DirectX::XMFLOAT4 ResizedRectFromHandle(const DirectX::XMFLOAT4& start,
        int handle, const DirectX::XMFLOAT2& local_mouse, bool keep_aspect,
        bool from_center) noexcept
    {
        float left = start.x;
        float bottom = start.y;
        float right = start.x + start.z;
        float top = start.y + start.w;
        int horizontal = 0;
        int vertical = 0;
        ResizeDirections(handle, horizontal, vertical);

        if (horizontal < 0)
        {
            if (from_center)
            {
                const float delta = local_mouse.x - start.x;
                left = local_mouse.x;
                right = start.x + start.z - delta;
            }
            else left = local_mouse.x;
        }
        else if (horizontal > 0)
        {
            if (from_center)
            {
                const float delta = local_mouse.x - (start.x + start.z);
                right = local_mouse.x;
                left = start.x - delta;
            }
            else right = local_mouse.x;
        }

        if (vertical < 0)
        {
            if (from_center)
            {
                const float delta = local_mouse.y - start.y;
                bottom = local_mouse.y;
                top = start.y + start.w - delta;
            }
            else bottom = local_mouse.y;
        }
        else if (vertical > 0)
        {
            if (from_center)
            {
                const float delta = local_mouse.y - (start.y + start.w);
                top = local_mouse.y;
                bottom = start.y - delta;
            }
            else top = local_mouse.y;
        }

        constexpr float minimum_size = 1.0f;
        if (right - left < minimum_size)
        {
            if (horizontal < 0) left = right - minimum_size;
            else if (horizontal > 0) right = left + minimum_size;
        }
        if (top - bottom < minimum_size)
        {
            if (vertical < 0) bottom = top - minimum_size;
            else if (vertical > 0) top = bottom + minimum_size;
        }

        if (keep_aspect && horizontal != 0 && vertical != 0 &&
            start.z > minimum_size && start.w > minimum_size)
        {
            const float aspect = start.z / start.w;
            float width = right - left;
            float height = top - bottom;
            const float width_change = std::fabs(width - start.z) / start.z;
            const float height_change = std::fabs(height - start.w) / start.w;
            if (width_change >= height_change)
            {
                height = (std::max)(minimum_size, width / aspect);
                if (from_center)
                {
                    const float center = start.y + start.w * 0.5f;
                    bottom = center - height * 0.5f;
                    top = center + height * 0.5f;
                }
                else if (vertical < 0) bottom = top - height;
                else top = bottom + height;
            }
            else
            {
                width = (std::max)(minimum_size, height * aspect);
                if (from_center)
                {
                    const float center = start.x + start.z * 0.5f;
                    left = center - width * 0.5f;
                    right = center + width * 0.5f;
                }
                else if (horizontal < 0) left = right - width;
                else right = left + width;
            }
        }

        return { left, bottom, right - left, top - bottom };
    }

    Core::GameObject* PickUIObject(Core::GameObject& object, float x, float y)
    {
        std::vector<Core::GameObject*> children = object.Children();
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            if (*it == nullptr) continue;
            if (Core::GameObject* picked = PickUIObject(**it, x, y))
                return picked;
        }

        const RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
        if (rect != nullptr && HasUIComponent(object) && RectHit(*rect, x, y))
            return &object;
        return nullptr;
    }


    void DrawPreviewObject(ImDrawList* draw_list, Core::GameObject& object,
        const ImVec2& origin, float canvas_height, float zoom,
        Core::ObjectID selected,
        const std::function<ID3D11ShaderResourceView*(const UIImageComponent&)>& texture_for_image)
    {
        RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
        if (rect != nullptr && HasUIComponent(object))
        {
            ImVec2 p[4]{};
            PreviewQuad(*rect, origin, canvas_height, zoom, p);

            if (const UIImageComponent* image = object.GetComponent<UIImageComponent>())
            {
                if (image->opacity > 0.0f && image->fill_amount > 0.0f)
                {
                    DirectX::XMFLOAT4 draw_rect = rect->ResolvedRect();
                    DirectX::XMFLOAT4 uv{ image->uv_offset.x, image->uv_offset.y,
                        image->uv_scale.x, image->uv_scale.y };
                    ApplyUIImageFill(*image, draw_rect, uv);

                    if (draw_rect.z > 0.0f && draw_rect.w > 0.0f)
                    {
                        ImVec2 image_points[4]{};
                        PreviewQuad(*rect, draw_rect, origin, canvas_height, zoom,
                            image_points);
                        const ImU32 tint = ColorToU32(image->color, image->opacity);
                        ID3D11ShaderResourceView* texture = texture_for_image(*image);
                        if (texture != nullptr)
                        {
                            const ImVec2 uv0(uv.x, uv.y + uv.w);
                            const ImVec2 uv1(uv.x + uv.z, uv.y + uv.w);
                            const ImVec2 uv2(uv.x + uv.z, uv.y);
                            const ImVec2 uv3(uv.x, uv.y);
                            draw_list->AddImageQuad(
                                reinterpret_cast<ImTextureID>(texture),
                                image_points[0], image_points[1],
                                image_points[2], image_points[3],
                                uv0, uv1, uv2, uv3, tint);
                        }
                        else
                        {
                            draw_list->AddQuadFilled(image_points[0],
                                image_points[1], image_points[2], image_points[3],
                                tint);
                        }
                    }
                }
            }
            else if (object.GetComponent<CanvasComponent>() == nullptr)
                draw_list->AddQuadFilled(p[0], p[1], p[2], p[3],
                    ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f, 0.20f, 0.22f, 0.18f)));

            const bool selected_object = object.ID() == selected;
            const bool mask_object = object.GetComponent<UIMaskComponent>() != nullptr;
            const bool button_object = object.GetComponent<UIButtonComponent>() != nullptr;
            const ImU32 outline = selected_object
                ? IM_COL32(255, 210, 80, 255)
                : (mask_object ? IM_COL32(90, 170, 255, 220)
                    : (button_object ? IM_COL32(120, 210, 170, 200)
                        : IM_COL32(190, 195, 205, 125)));
            draw_list->AddQuad(p[0], p[1], p[2], p[3], outline, selected_object ? 2.0f : 1.0f);

            if (const UITextComponent* text = object.GetComponent<UITextComponent>())
            {
                const DirectX::XMFLOAT4 r = rect->ResolvedRect();
                const ImVec2 top_left = ToPreviewPoint(
                    { r.x, r.y + r.w }, origin, canvas_height, zoom);
                draw_list->AddText(top_left, ColorToU32(text->color, text->opacity),
                    text->text.c_str());
            }
        }

        for (Core::GameObject* child : object.Children())
        {
            if (child != nullptr) DrawPreviewObject(draw_list, *child,
                origin, canvas_height, zoom, selected, texture_for_image);
        }
    }


    }

bool framework::ensure_ui_preview_render_target(int width, int height)
{
    width = (std::max)(1, width);
    height = (std::max)(1, height);
    if (ui_preview_runtime_texture && ui_preview_runtime_srv && ui_preview_runtime_rtv &&
        ui_preview_runtime_width == width && ui_preview_runtime_height == height)
    {
        return true;
    }
    ui_preview_runtime_srv.Reset();
    ui_preview_runtime_rtv.Reset();
    ui_preview_runtime_texture.Reset();
    ui_preview_runtime_width = 0;
    ui_preview_runtime_height = 0;
    if (!device) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, ui_preview_runtime_texture.GetAddressOf())))
        return false;
    if (FAILED(device->CreateRenderTargetView(ui_preview_runtime_texture.Get(), nullptr,
        ui_preview_runtime_rtv.GetAddressOf())))
    {
        ui_preview_runtime_texture.Reset();
        return false;
    }
    if (FAILED(device->CreateShaderResourceView(ui_preview_runtime_texture.Get(), nullptr,
        ui_preview_runtime_srv.GetAddressOf())))
    {
        ui_preview_runtime_rtv.Reset();
        ui_preview_runtime_texture.Reset();
        return false;
    }
    ui_preview_runtime_width = width;
    ui_preview_runtime_height = height;
    return true;
}

void framework::render_ui_preview_target()
{
    if (!editor_mode || !show_ui_preview_panel || !ui_preview_runtime_requested ||
        !ui_preview_runtime_rtv || immediate_context == nullptr)
    {
        return;
    }
    ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
    if (scene == nullptr) return;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> previous_rtv;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> previous_dsv;
    immediate_context->OMGetRenderTargets(1, previous_rtv.GetAddressOf(),
        previous_dsv.GetAddressOf());
    D3D11_VIEWPORT previous_viewport{};
    UINT viewport_count = 1;
    immediate_context->RSGetViewports(&viewport_count, &previous_viewport);

    ID3D11RenderTargetView* target = ui_preview_runtime_rtv.Get();
    immediate_context->OMSetRenderTargets(1, &target, nullptr);
    const float clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
    immediate_context->ClearRenderTargetView(target, clear);
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(ui_preview_runtime_width);
    viewport.Height = static_cast<float>(ui_preview_runtime_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    immediate_context->RSSetViewports(1, &viewport);

    ReplayEngine::UI::UILayout::Resolve(*scene,
        static_cast<float>(ui_preview_runtime_width),
        static_cast<float>(ui_preview_runtime_height));
    ReplayEngine::UI::UIRenderer::RenderStates states{};
    states.depth_disabled = depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get();
    states.depth_enabled = depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get();
    DirectX::XMStoreFloat4x4(&states.world_view_projection,
        viewport_view_matrix() * viewport_projection_matrix());
    states.rasterizer = rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get();
    states.rasterizer_scissor = rasterizer_states[(size_t)RASTER_STATE::SCISSOR].Get();
    states.blend_none = blend_states[(size_t)BLEND_STATE::NONE].Get();
    states.blend_alpha = blend_states[(size_t)BLEND_STATE::ALPHA].Get();
    states.blend_add = blend_states[(size_t)BLEND_STATE::ADD].Get();
    states.blend_multiply = blend_states[(size_t)BLEND_STATE::MULTIPLY].Get();
    states.blend_screen = blend_states[(size_t)BLEND_STATE::SCREEN].Get();
    states.blend_premultiplied = blend_states[(size_t)BLEND_STATE::PREMULTIPLIED].Get();
    states.sampler = sampler_states[(size_t)SAMPLER_STATE::LINEAR].Get();
    states.focus_outline_enabled = project_settings.FocusOutlineEnabled();
    states.focus_outline_color = project_settings.FocusOutlineColor();
    states.focus_outline_width = project_settings.FocusOutlineWidth();
    states.focus_corner_radius = project_settings.FocusCornerRadius();
    states.scissor_bounds_enabled = true;
    states.scissor_bounds.left = 0;
    states.scissor_bounds.top = 0;
    states.scissor_bounds.right = ui_preview_runtime_width;
    states.scissor_bounds.bottom = ui_preview_runtime_height;
    ui_renderer.Render(immediate_context.Get(), *scene, &asset_database,
        &shader_library.Catalog(), ui_font_atlas,
        static_cast<float>(ui_preview_runtime_width),
        static_cast<float>(ui_preview_runtime_height), shader_composer_time, states);

    ID3D11RenderTargetView* restore = previous_rtv.Get();
    immediate_context->OMSetRenderTargets(1, &restore, previous_dsv.Get());
    if (viewport_count > 0) immediate_context->RSSetViewports(1, &previous_viewport);
}

void framework::draw_ui_preview()
{
    if (!show_ui_preview_panel) return;
    if (!ImGui::Begin("Canvas プレビュー", &show_ui_preview_panel))
    {
        ImGui::End();
        return;
    }

    Scene::Scene* scene = object_editor_context.GetScene();
    if (scene == nullptr)
    {
        ImGui::TextDisabled("Scene がありません");
        ImGui::End();
        return;
    }

    const char* resolutions[] = { "1920 x 1080", "1280 x 720", "1080 x 1920", u8"カスタム" };
    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo("##UIResolution", &ui_preview_resolution_index,
        resolutions, IM_ARRAYSIZE(resolutions));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::SliderFloat(u8"拡大", &ui_preview_zoom, 0.10f, 2.0f, "%.2f");
    ImGui::SameLine();
    ImGui::Checkbox(u8"グリッド", &ui_preview_grid);

    int preview_width = 1920;
    int preview_height = 1080;
    if (ui_preview_resolution_index == 1) { preview_width = 1280; preview_height = 720; }
    else if (ui_preview_resolution_index == 2) { preview_width = 1080; preview_height = 1920; }
    else if (ui_preview_resolution_index == 3)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputInt("W", &ui_preview_custom_width, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputInt("H", &ui_preview_custom_height, 0, 0);
        preview_width = (std::max)(1, ui_preview_custom_width);
        preview_height = (std::max)(1, ui_preview_custom_height);
    }

    ReplayEngine::UI::UILayout::Resolve(*scene,
        static_cast<float>(preview_width), static_cast<float>(preview_height));

    // Runtime と同一の UIRenderer を使う Preview RT は描画フェーズで更新される。
    // この panel から必要解像度を要求しておき、次の render pass から同じ RT を使う。
    ui_preview_runtime_requested = true;
    ensure_ui_preview_render_target(preview_width, preview_height);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = (std::max)(avail.x, 64.0f);
    avail.y = (std::max)(avail.y, 64.0f);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##CanvasPreviewSurface", avail);
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        ui_preview_zoom = (std::min)(2.0f, (std::max)(0.1f,
            ui_preview_zoom + ImGui::GetIO().MouseWheel * 0.05f));
    }

    const ImVec2 canvas_size(
        static_cast<float>(preview_width) * ui_preview_zoom,
        static_cast<float>(preview_height) * ui_preview_zoom);
    ImVec2 origin(
        cursor.x + (avail.x - canvas_size.x) * 0.5f + ui_preview_pan.x,
        cursor.y + (avail.y - canvas_size.y) * 0.5f + ui_preview_pan.y);

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ui_preview_pan.x += delta.x;
        ui_preview_pan.y += delta.y;
        origin.x += delta.x;
        origin.y += delta.y;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float canvas_mouse_x = (mouse.x - origin.x) / ui_preview_zoom;
    const float canvas_mouse_y = static_cast<float>(preview_height) -
        (mouse.y - origin.y) / ui_preview_zoom;

    Core::GameObject* selected =
        object_editor_context.Selection().ResolvePrimary(*scene);
    RectTransformComponent* selected_rect = selected != nullptr
        ? selected->GetComponent<RectTransformComponent>() : nullptr;

    int hovered_resize_handle = -1;
    ImVec2 resize_handle_points[8]{};
    if (selected_rect != nullptr && HasUIComponent(*selected))
    {
        ResizeHandlePoints(*selected_rect, origin,
            static_cast<float>(preview_height), ui_preview_zoom,
            resize_handle_points);
        hovered_resize_handle = HitResizeHandle(resize_handle_points, mouse);
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // Rect handle は Object pick より優先する。小さい UI の corner を掴んだ時に
        // 背後の UI へ選択が飛ぶのを防ぐ。
        if (selected != nullptr && selected_rect != nullptr &&
            hovered_resize_handle >= 0 && object_editor_context.CanEdit())
        {
            ui_preview_resize_candidate = true;
            ui_preview_resizing = false;
            ui_preview_resize_handle = hovered_resize_handle;
            ui_preview_resize_object = selected->ID();
            ui_preview_resize_start_mouse = mouse;
            ui_preview_resize_start_rect = selected_rect->ResolvedRect();
            ui_preview_resize_parent_rect = ParentResolvedRect(*selected,
                static_cast<float>(preview_width), static_cast<float>(preview_height));
            ui_preview_resize_start_matrix = selected_rect->ResolvedMatrix();

            // Move candidate が残っていると同じ drag で resize と move が競合する。
            ui_preview_drag_object = Core::ObjectID::Invalid();
            ui_preview_dragging = false;
        }
        else
        {
            Core::GameObject* picked = nullptr;
            std::vector<Core::GameObject*> canvases = SortedCanvases(*scene);
            for (auto it = canvases.rbegin(); it != canvases.rend() && picked == nullptr; ++it)
            {
                if (*it != nullptr) picked = PickUIObject(**it, canvas_mouse_x, canvas_mouse_y);
            }
            if (picked != nullptr)
            {
                object_editor_context.Selection().Select(picked->ID(), false);
                selected_editor_object = editor_selection::game_object;
                if (RectTransformComponent* rect = picked->GetComponent<RectTransformComponent>())
                {
                    ui_preview_drag_object = picked->ID();
                    ui_preview_drag_start_mouse = mouse;
                    ui_preview_drag_start_position = rect->anchored_position;
                }
            }
        }
    }

    // Selection は click で変わることがあるので取り直す。
    selected = object_editor_context.Selection().ResolvePrimary(*scene);
    selected_rect = selected != nullptr
        ? selected->GetComponent<RectTransformComponent>() : nullptr;

    if (active && selected_rect != nullptr && ui_preview_resize_candidate &&
        ui_preview_resize_object == selected->ID() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
    {
        if (!ui_preview_resizing && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("UI 要素をリサイズ");
            ui_preview_resizing = true;
        }
        if (ui_preview_resizing)
        {
            const DirectX::XMFLOAT2 canvas_mouse = CanvasPointFromScreen(mouse, origin,
                static_cast<float>(preview_height), ui_preview_zoom);
            DirectX::XMFLOAT2 local_mouse{};
            if (!InverseTransformPoint(ui_preview_resize_start_matrix, canvas_mouse, local_mouse))
            {
                object_editor_context.CancelEdit();
                ui_preview_resizing = false;
                ui_preview_resize_candidate = false;
                ui_preview_resize_handle = -1;
            }
            const ImGuiIO& io = ImGui::GetIO();
            if (ui_preview_resizing)
            {
                const DirectX::XMFLOAT4 desired = ResizedRectFromHandle(
                    ui_preview_resize_start_rect, ui_preview_resize_handle,
                    local_mouse, io.KeyShift, io.KeyAlt);
                ApplyResolvedRect(*selected_rect, ui_preview_resize_parent_rect, desired);
                ReplayEngine::UI::UILayout::Resolve(*scene,
                    static_cast<float>(preview_width), static_cast<float>(preview_height));
            }
        }
    }

    if (ui_preview_resize_candidate && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (ui_preview_resizing) object_editor_context.CommitEdit();
        ui_preview_resize_candidate = false;
        ui_preview_resizing = false;
        ui_preview_resize_handle = -1;
        ui_preview_resize_object = Core::ObjectID::Invalid();
    }

    if (active && selected_rect != nullptr && !ui_preview_resize_candidate &&
        ui_preview_drag_object == selected->ID() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
    {
        if (!ui_preview_dragging && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("UI 要素を移動");
            ui_preview_dragging = true;
        }
        if (ui_preview_dragging)
        {
            const ImVec2 delta(mouse.x - ui_preview_drag_start_mouse.x,
                mouse.y - ui_preview_drag_start_mouse.y);
            selected_rect->anchored_position = {
                ui_preview_drag_start_position.x + delta.x / ui_preview_zoom,
                ui_preview_drag_start_position.y - delta.y / ui_preview_zoom
            };
            ReplayEngine::UI::UILayout::Resolve(*scene,
                static_cast<float>(preview_width), static_cast<float>(preview_height));
        }
    }
    if (ui_preview_dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        object_editor_context.CommitEdit();
        ui_preview_dragging = false;
        ui_preview_drag_object = Core::ObjectID::Invalid();
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 clip_min = cursor;
    const ImVec2 clip_max(cursor.x + avail.x, cursor.y + avail.y);
    draw_list->PushClipRect(clip_min, clip_max, true);
    draw_list->AddRectFilled(clip_min, clip_max, IM_COL32(23, 26, 30, 255));
    draw_list->AddRectFilled(origin, ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
        IM_COL32(36, 38, 42, 255));

    if (ui_preview_grid && ui_preview_grid_size > 1.0f)
    {
        const float step = ui_preview_grid_size * ui_preview_zoom;
        for (float x = origin.x; x <= origin.x + canvas_size.x; x += step)
            draw_list->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + canvas_size.y),
                IM_COL32(255, 255, 255, 24));
        for (float y = origin.y; y <= origin.y + canvas_size.y; y += step)
            draw_list->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + canvas_size.x, y),
                IM_COL32(255, 255, 255, 24));
    }

    // 実際の Runtime UIRenderer が出した RT をそのまま表示する。
    // UIImage/UIText/UIShape/Mask/Effect/Puppet を ImGui で再実装しない。
    if (ui_preview_runtime_srv)
    {
        draw_list->AddImage(reinterpret_cast<ImTextureID>(ui_preview_runtime_srv.Get()),
            origin, ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE);
    }
    else
    {
        draw_list->AddText(ImVec2(origin.x + 12.0f, origin.y + 12.0f),
            IM_COL32(255, 160, 100, 255), "Runtime UI Preview target unavailable");
    }
    // Photoshop / Unity Rect Tool と同じく 4 corner + 4 edge handle。
    // DrawPreviewObject の selection outline より後に描くため、常に掴める位置が見える。
    selected = object_editor_context.Selection().ResolvePrimary(*scene);
    selected_rect = selected != nullptr
        ? selected->GetComponent<RectTransformComponent>() : nullptr;
    if (selected_rect != nullptr && HasUIComponent(*selected))
    {
        ImVec2 handles[8]{};
        ResizeHandlePoints(*selected_rect, origin,
            static_cast<float>(preview_height), ui_preview_zoom, handles);
        const int hot = HitResizeHandle(handles, mouse);
        for (int index = 0; index < 8; ++index)
        {
            const bool active_handle = ui_preview_resize_candidate &&
                ui_preview_resize_object == selected->ID() &&
                ui_preview_resize_handle == index;
            const bool hot_handle = hot == index;
            const ImU32 fill = active_handle
                ? IM_COL32(255, 190, 55, 255)
                : (hot_handle ? IM_COL32(255, 230, 130, 255)
                    : IM_COL32(245, 245, 250, 255));
            draw_list->AddCircleFilled(handles[index], 5.0f, fill, 12);
            draw_list->AddCircle(handles[index], 5.0f,
                IM_COL32(35, 38, 44, 255), 12, 1.0f);
        }
        if (hot >= 0 && hovered)
        {
            ImGui::SetMouseCursor((hot == ResizeLeft || hot == ResizeRight)
                ? ImGuiMouseCursor_ResizeEW
                : ((hot == ResizeTop || hot == ResizeBottom)
                    ? ImGuiMouseCursor_ResizeNS
                    : ImGuiMouseCursor_ResizeAll));
        }
    }

    draw_list->AddRect(origin, ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
        IM_COL32(230, 230, 235, 180), 0.0f, 0, 1.5f);
    draw_list->PopClipRect();

    ImGui::End();
}
