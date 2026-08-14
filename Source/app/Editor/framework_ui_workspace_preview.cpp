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

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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

    Core::GameObject* selected =
        object_editor_context.Selection().ResolvePrimary(*scene);
    RectTransformComponent* selected_rect = selected != nullptr
        ? selected->GetComponent<RectTransformComponent>() : nullptr;
    if (active && selected_rect != nullptr &&
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

    const auto preview_texture_for_image =
        [this](const UIImageComponent& image) -> ID3D11ShaderResourceView*
        {
            if (image.sprite.guid.empty()) return nullptr;
            const Assets::AssetRecord* record =
                asset_database.FindByGuid(image.sprite.guid);
            if (record == nullptr || record->kind != Assets::AssetKind::Image)
                return nullptr;

            const std::filesystem::path path = content_path(record->cache_path.empty()
                ? record->source_path : record->cache_path);
            if (path.empty()) return nullptr;
            return project_thumbnail_for(path);
        };

    for (Core::GameObject* canvas : SortedCanvases(*scene))
    {
        if (canvas != nullptr)
            DrawPreviewObject(draw_list, *canvas, origin,
                static_cast<float>(preview_height), ui_preview_zoom,
                object_editor_context.Selection().Primary(),
                preview_texture_for_image);
    }
    draw_list->AddRect(origin, ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
        IM_COL32(230, 230, 235, 180), 0.0f, 0, 1.5f);
    draw_list->PopClipRect();

    ImGui::End();
}
