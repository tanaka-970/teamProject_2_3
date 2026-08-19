// UI workspace のうち「Scene View の UI overlay・選択・ドラッグ」だけを持つ。
//
// Canvas パネルのプレビューは framework_ui_workspace_preview.cpp に置く。

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

    Core::GameObject* PickUISceneObject(Core::GameObject& object, float x, float y)
    {
        if (object.PendingDestroy() || !object.ActiveInHierarchy()) return nullptr;

        std::vector<Core::GameObject*> children = object.Children();
        for (auto it = children.rbegin(); it != children.rend(); ++it)
        {
            if (*it == nullptr) continue;
            if (Core::GameObject* picked = PickUISceneObject(**it, x, y))
                return picked;
        }

        const RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
        const bool canvas_only = object.GetComponent<CanvasComponent>() != nullptr &&
            object.GetComponent<UIImageComponent>() == nullptr &&
            object.GetComponent<UITextComponent>() == nullptr &&
            object.GetComponent<UIButtonComponent>() == nullptr &&
            object.GetComponent<UIMaskComponent>() == nullptr;
        if (rect != nullptr && HasUIComponent(object) && !canvas_only && RectHit(*rect, x, y))
            return &object;
        return nullptr;
    }

    Core::GameObject* CanvasForObject(Core::GameObject* object) noexcept
    {
        for (Core::GameObject* current = object; current != nullptr;
            current = current->Parent())
        {
            if (current->GetComponent<CanvasComponent>() != nullptr)
                return current;
        }
        return nullptr;
    }

    float SafeCanvasScale(const CanvasComponent& canvas,
        float logical_width, float logical_height) noexcept
    {
        const float scale = ReplayEngine::UI::UILayout::CanvasScale(
            canvas, logical_width, logical_height);
        return scale > 0.0001f ? scale : 1.0f;
    }

    ImVec2 ToSceneUIPoint(const DirectX::XMFLOAT2& canvas_point,
        float canvas_scale, float left, float top, float width, float height,
        float logical_width, float logical_height)
    {
        const float screen_x = canvas_point.x * canvas_scale;
        const float screen_y = canvas_point.y * canvas_scale;
        return ImVec2(
            left + (screen_x / logical_width) * width,
            top + ((logical_height - screen_y) / logical_height) * height);
    }

    void SceneUIQuad(const RectTransformComponent& rect, float canvas_scale,
        float left, float top, float width, float height,
        float logical_width, float logical_height, ImVec2 out[4])
    {
        const DirectX::XMFLOAT4 r = rect.ResolvedRect();
        const DirectX::XMFLOAT4X4 m = rect.ResolvedMatrix();
        out[0] = ToSceneUIPoint(TransformPoint(m, r.x, r.y), canvas_scale,
            left, top, width, height, logical_width, logical_height);
        out[1] = ToSceneUIPoint(TransformPoint(m, r.x + r.z, r.y), canvas_scale,
            left, top, width, height, logical_width, logical_height);
        out[2] = ToSceneUIPoint(TransformPoint(m, r.x + r.z, r.y + r.w),
            canvas_scale, left, top, width, height, logical_width, logical_height);
        out[3] = ToSceneUIPoint(TransformPoint(m, r.x, r.y + r.w), canvas_scale,
            left, top, width, height, logical_width, logical_height);
    }


    }

void framework::draw_ui_scene_overlay()
{
    ui_scene_view_input_consumed = false;
    if (active_editor_workspace != editor_workspace::ui ||
        active_editor_view != editor_view::scene ||
        !scene_view_overlay_valid)
        return;

    Scene::Scene* scene = object_editor_context.GetScene();
    if (scene == nullptr) return;

    const object_ui_viewport target = object_ui_viewport_target();
    const float target_width = (std::max)(1.0f, target.width);
    const float target_height = (std::max)(1.0f, target.height);
    const float logical_width = (std::max)(1.0f, target.logical_width);
    const float logical_height = (std::max)(1.0f, target.logical_height);

    ReplayEngine::UI::UILayout::Resolve(*scene, logical_width, logical_height);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float local_x = mouse.x - target.left;
    const float local_y = mouse.y - target.top;
    const bool target_hovered = scene_view_hovered &&
        local_x >= 0.0f && local_y >= 0.0f &&
        local_x < target_width && local_y < target_height;
    const float logical_mouse_x = local_x * (logical_width / target_width);
    const float logical_mouse_y = logical_height -
        local_y * (logical_height / target_height);

    if (target_hovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        ui_preview_zoom = (std::min)(2.0f, (std::max)(0.10f,
            ui_preview_zoom + ImGui::GetIO().MouseWheel * 0.05f));
        ui_scene_view_input_consumed = true;
    }

    if (target_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        Core::GameObject* picked = nullptr;
        std::vector<Core::GameObject*> canvases = SortedCanvases(*scene);
        for (auto it = canvases.rbegin(); it != canvases.rend() && picked == nullptr; ++it)
        {
            Core::GameObject* canvas_object = *it;
            if (canvas_object == nullptr) continue;
            const CanvasComponent* canvas =
                canvas_object->GetComponent<CanvasComponent>();
            if (canvas == nullptr) continue;
            const float canvas_scale = SafeCanvasScale(
                *canvas, logical_width, logical_height);
            picked = PickUISceneObject(*canvas_object,
                logical_mouse_x / canvas_scale,
                logical_mouse_y / canvas_scale);
        }

        if (picked != nullptr)
        {
            object_editor_context.Selection().Select(picked->ID(), false);
            selected_editor_object = editor_selection::game_object;
            if (RectTransformComponent* rect =
                picked->GetComponent<RectTransformComponent>())
            {
                ui_preview_drag_object = picked->ID();
                ui_preview_drag_start_mouse = mouse;
                ui_preview_drag_start_position = rect->anchored_position;
            }
            ui_scene_view_input_consumed = true;
        }
    }

    Core::GameObject* selected =
        object_editor_context.Selection().ResolvePrimary(*scene);
    RectTransformComponent* selected_rect = selected != nullptr
        ? selected->GetComponent<RectTransformComponent>() : nullptr;
    const bool dragging_candidate = selected_rect != nullptr &&
        selected != nullptr && ui_preview_drag_object == selected->ID();

    if (dragging_candidate && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
    {
        ui_scene_view_input_consumed = true;
        if (!ui_preview_dragging && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("UI 要素を移動");
            ui_preview_dragging = true;
        }
        if (ui_preview_dragging)
        {
            float canvas_scale = 1.0f;
            if (Core::GameObject* canvas_object = CanvasForObject(selected))
            {
                if (const CanvasComponent* canvas =
                    canvas_object->GetComponent<CanvasComponent>())
                {
                    canvas_scale = SafeCanvasScale(
                        *canvas, logical_width, logical_height);
                }
            }

            const ImVec2 delta(mouse.x - ui_preview_drag_start_mouse.x,
                mouse.y - ui_preview_drag_start_mouse.y);
            const float logical_delta_x =
                delta.x * (logical_width / target_width) / canvas_scale;
            const float logical_delta_y =
                delta.y * (logical_height / target_height) / canvas_scale;
            selected_rect->anchored_position = {
                ui_preview_drag_start_position.x + logical_delta_x,
                ui_preview_drag_start_position.y - logical_delta_y
            };
            ReplayEngine::UI::UILayout::Resolve(*scene, logical_width, logical_height);
        }
    }

    if (ui_preview_drag_object.Valid() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        ui_scene_view_input_consumed = true;

    if (ui_preview_drag_object.Valid() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (ui_preview_dragging) object_editor_context.CommitEdit();
        ui_preview_dragging = false;
        ui_preview_drag_object = Core::ObjectID::Invalid();
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 clip_min = scene_view_overlay_position;
    const ImVec2 clip_max(scene_view_overlay_position.x + scene_view_overlay_size.x,
        scene_view_overlay_position.y + scene_view_overlay_size.y);
    draw_list->PushClipRect(clip_min, clip_max, true);

    if (ui_preview_grid && ui_preview_grid_size > 1.0f)
    {
        const float step_x = ui_preview_grid_size * target_width / logical_width;
        const float step_y = ui_preview_grid_size * target_height / logical_height;
        if (step_x > 0.5f)
        {
            for (float x = target.left; x <= target.left + target_width; x += step_x)
            {
                draw_list->AddLine(ImVec2(x, target.top),
                    ImVec2(x, target.top + target_height),
                    IM_COL32(255, 255, 255, 28));
            }
        }
        if (step_y > 0.5f)
        {
            for (float y = target.top; y <= target.top + target_height; y += step_y)
            {
                draw_list->AddLine(ImVec2(target.left, y),
                    ImVec2(target.left + target_width, y),
                    IM_COL32(255, 255, 255, 28));
            }
        }
    }

    draw_list->AddRect(ImVec2(target.left, target.top),
        ImVec2(target.left + target_width, target.top + target_height),
        IM_COL32(230, 230, 235, 190), 0.0f, 0, 1.5f);

    if (selected != nullptr && selected_rect != nullptr && HasUIComponent(*selected))
    {
        float canvas_scale = 1.0f;
        if (Core::GameObject* canvas_object = CanvasForObject(selected))
        {
            if (const CanvasComponent* canvas =
                canvas_object->GetComponent<CanvasComponent>())
            {
                canvas_scale = SafeCanvasScale(*canvas, logical_width, logical_height);
            }
        }

        ImVec2 p[4]{};
        SceneUIQuad(*selected_rect, canvas_scale,
            target.left, target.top, target_width, target_height,
            logical_width, logical_height, p);
        draw_list->AddQuad(p[0], p[1], p[2], p[3],
            IM_COL32(255, 210, 80, 255), 2.0f);
    }

    draw_list->PopClipRect();
}
