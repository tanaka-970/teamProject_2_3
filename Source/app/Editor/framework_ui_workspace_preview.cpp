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
#include "../../RePlayEngine/Assets/SpriteAtlasAsset.h"
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

    void ResizeHandlePoints(const RectTransformComponent& rect,
        const ImVec2& origin, float canvas_height, float zoom, ImVec2 out[8])
    {
        ImVec2 corners[4]{};
        PreviewQuad(rect, origin, canvas_height, zoom, corners);
        framework_ui_workspace_detail::ResizeHandlePoints(corners, out);
    }

    DirectX::XMFLOAT2 CanvasPointFromScreen(const ImVec2& mouse,
        const ImVec2& origin, float canvas_height, float zoom) noexcept
    {
        return {
            (mouse.x - origin.x) / zoom,
            canvas_height - (mouse.y - origin.y) / zoom
        };
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
        const std::function<ImTextureID(const UIImageComponent&)>& texture_for_image)
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
                        const ImTextureID texture = texture_for_image(*image);
                        if (texture != nullptr)
                        {
                            const ImVec2 uv0(uv.x, uv.y + uv.w);
                            const ImVec2 uv1(uv.x + uv.z, uv.y + uv.w);
                            const ImVec2 uv2(uv.x + uv.z, uv.y);
                            const ImVec2 uv3(uv.x, uv.y);
                            draw_list->AddImageQuad(
                                texture,
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

    std::filesystem::path PreviewImagePath(const UIImageComponent& image,
        const Assets::AssetDatabase& asset_database)
    {
        std::filesystem::path image_path;
        std::string image_guid = image.sprite.guid;
        if (!image.atlas.guid.empty() && !image.atlas_region.empty())
        {
            const Assets::AssetRecord* atlas_record =
                asset_database.FindByGuid(image.atlas.guid);
            if (atlas_record != nullptr && atlas_record->kind == Assets::AssetKind::SpriteAtlas)
            {
                const std::filesystem::path atlas_path = atlas_record->cache_path.empty()
                    ? atlas_record->source_path : atlas_record->cache_path;
                Assets::SpriteAtlasAsset atlas;
                std::string error;
                if (Assets::SpriteAtlasAsset::LoadFromFile(atlas_path, atlas, error))
                {
                    const Assets::SpriteAtlasRegion* region =
                        atlas.FindRegion(image.atlas_region);
                    if (region != nullptr)
                    {
                        image_guid = atlas.image_guid;
                        if (!atlas.embedded_texture_path.empty())
                        {
                            const std::filesystem::path embedded = atlas_path.parent_path() /
                                std::filesystem::u8path(atlas.embedded_texture_path);
                            std::error_code file_error;
                            if (std::filesystem::is_regular_file(embedded, file_error) &&
                                !file_error)
                                image_path = embedded;
                        }
                    }
                }
            }
        }
        if (image_path.empty() && !image_guid.empty())
        {
            const Assets::AssetRecord* image_record =
                asset_database.FindByGuid(image_guid);
            if (image_record != nullptr && image_record->kind == Assets::AssetKind::Image)
                image_path = image_record->cache_path.empty()
                    ? image_record->source_path : image_record->cache_path;
        }
        return image_path;
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
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::ColorEdit4(u8"Canvas 背景色", &ui_preview_background_color.x,
        ImGuiColorEditFlags_NoInputs);

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
    ui_preview_runtime_width = preview_width;
    ui_preview_runtime_height = preview_height;

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
    Core::GameObject* selected_transform_target = UITransformEditTarget(selected);
    RectTransformComponent* selected_rect = selected_transform_target != nullptr
        ? selected_transform_target->GetComponent<RectTransformComponent>() : nullptr;

    int hovered_resize_handle = -1;
    ImVec2 resize_handle_points[8]{};
    if (selected_transform_target != nullptr && selected_rect != nullptr &&
        HasUIComponent(*selected_transform_target))
    {
        ResizeHandlePoints(*selected_rect, origin,
            static_cast<float>(preview_height), ui_preview_zoom,
            resize_handle_points);
        hovered_resize_handle = HitResizeHandle(resize_handle_points, mouse);
    }
    const bool handle_interaction_hovered = hovered_resize_handle >= 0 &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if ((hovered || handle_interaction_hovered) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // Rect handle は Object pick より優先する。小さい UI の corner を掴んだ時に
        // 背後の UI へ選択が飛ぶのを防ぐ。
        if (selected_transform_target != nullptr && selected_rect != nullptr &&
            hovered_resize_handle >= 0 && object_editor_context.CanEdit())
        {
            ui_preview_resize_candidate = true;
            ui_preview_resizing = false;
            ui_preview_resize_handle = hovered_resize_handle;
            ui_preview_resize_object = selected_transform_target->ID();
            ui_preview_resize_start_mouse = mouse;
            ui_preview_resize_start_rect = selected_rect->ResolvedRect();
            ui_preview_resize_parent_rect = ParentResolvedRect(*selected_transform_target,
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
                Core::GameObject* transform_target = UITransformEditTarget(picked);
                if (RectTransformComponent* rect = transform_target != nullptr
                    ? transform_target->GetComponent<RectTransformComponent>() : nullptr)
                {
                    ui_preview_drag_object = transform_target->ID();
                    ui_preview_drag_start_mouse = mouse;
                    ui_preview_drag_start_position = rect->anchored_position;
                }
            }
        }
    }

    // Selection は click で変わることがあるので取り直す。
    selected = object_editor_context.Selection().ResolvePrimary(*scene);
    selected_transform_target = UITransformEditTarget(selected);
    selected_rect = selected_transform_target != nullptr
        ? selected_transform_target->GetComponent<RectTransformComponent>() : nullptr;

    if (selected_rect != nullptr && ui_preview_resize_candidate &&
        selected_transform_target != nullptr &&
        ui_preview_resize_object == selected_transform_target->ID() &&
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
        selected_transform_target != nullptr &&
        ui_preview_drag_object == selected_transform_target->ID() &&
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
    const ImU32 canvas_background = ImGui::ColorConvertFloat4ToU32(ImVec4(
        ui_preview_background_color.x, ui_preview_background_color.y,
        ui_preview_background_color.z, ui_preview_background_color.w));
    draw_list->AddRectFilled(origin, ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
        canvas_background);

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

    // Runtime と同じ CPU UI frame を DX12 Preview target へ描画し、その SRV を表示する。
    void* preview_texture = dx12_device_context.ImGuiTextureForUIPreview();
    if (preview_texture != nullptr)
        draw_list->AddImage(reinterpret_cast<ImTextureID>(preview_texture),
            origin, ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE);
    else
        draw_list->AddText(ImVec2(origin.x + 12.0f, origin.y + 12.0f),
            IM_COL32(255, 160, 100, 255),
            "DX12 UI Preview target unavailable");
    draw_list->PopClipRect();
    // Photoshop / Unity Rect Tool と同じく 4 corner + 4 edge handle。
    // DrawPreviewObject の selection outline より後に描くため、常に掴める位置が見える。
    selected = object_editor_context.Selection().ResolvePrimary(*scene);
    selected_transform_target = UITransformEditTarget(selected);
    selected_rect = selected_transform_target != nullptr
        ? selected_transform_target->GetComponent<RectTransformComponent>() : nullptr;
    if (selected_transform_target != nullptr && selected_rect != nullptr &&
        HasUIComponent(*selected_transform_target))
    {
        ImVec2 handles[8]{};
        ResizeHandlePoints(*selected_rect, origin,
            static_cast<float>(preview_height), ui_preview_zoom, handles);
        const int hot = HitResizeHandle(handles, mouse);
        for (int index = 0; index < 8; ++index)
        {
            const bool active_handle = ui_preview_resize_candidate &&
                ui_preview_resize_object == selected_transform_target->ID() &&
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
        if (hot >= 0 && (hovered ||
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)))
        {
            ImGui::SetMouseCursor((hot == ResizeLeft || hot == ResizeRight)
                ? ImGuiMouseCursor_ResizeEW
                : ((hot == ResizeTop || hot == ResizeBottom)
                    ? ImGuiMouseCursor_ResizeNS
                    : ImGuiMouseCursor_ResizeAll));
        }
    }

    draw_list->PushClipRect(clip_min, clip_max, true);
    draw_list->AddRect(origin, ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y),
        IM_COL32(230, 230, 235, 180), 0.0f, 0, 1.5f);
    draw_list->PopClipRect();

    ImGui::End();
}
