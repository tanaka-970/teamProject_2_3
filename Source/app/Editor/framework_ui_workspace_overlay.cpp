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
#include "../../RePlayEngine/Components/UI/UIPuppetDeformComponent.h"
#include "../../RePlayEngine/Components/UI/UIShapeComponent.h"
#include "../../RePlayEngine/Components/UI/UIShapeImageComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
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
#include <limits>
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
    using ReplayEngine::Components::UIPuppetDeformComponent;
    using ReplayEngine::Components::UIShapeComponent;
    using ReplayEngine::Components::UIShapeImageComponent;
    using ReplayEngine::Components::UIEffectStackComponent;
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

    float CanvasScaleForObject(Core::GameObject* object,
        float logical_width, float logical_height) noexcept
    {
        Core::GameObject* canvas_object = CanvasForObject(object);
        const CanvasComponent* canvas = canvas_object != nullptr
            ? canvas_object->GetComponent<CanvasComponent>() : nullptr;
        return canvas != nullptr
            ? SafeCanvasScale(*canvas, logical_width, logical_height) : 1.0f;
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

    void SceneResizeHandlePoints(const RectTransformComponent& rect,
        float canvas_scale, float left, float top, float width, float height,
        float logical_width, float logical_height, ImVec2 out[8])
    {
        ImVec2 corners[4]{};
        SceneUIQuad(rect, canvas_scale, left, top, width, height,
            logical_width, logical_height, corners);
        ResizeHandlePoints(corners, out);
    }

    DirectX::XMFLOAT2 InverseTransformPoint2D(const DirectX::XMFLOAT4X4& matrix,
        float x, float y)
    {
        const DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&matrix);
        const DirectX::XMMATRIX inv = DirectX::XMMatrixInverse(nullptr, m);
        DirectX::XMFLOAT3 result{};
        DirectX::XMStoreFloat3(&result, DirectX::XMVector3TransformCoord(
            DirectX::XMVectorSet(x, y, 0.0f, 1.0f), inv));
        return { result.x, result.y };
    }

    DirectX::XMFLOAT2 NormalizedToCanvas(const RectTransformComponent& rect,
        const DirectX::XMFLOAT2& normalized)
    {
        const DirectX::XMFLOAT4 r = rect.ResolvedRect();
        return TransformPoint(rect.ResolvedMatrix(),
            r.x + normalized.x * r.z, r.y + normalized.y * r.w);
    }

    DirectX::XMFLOAT2 CanvasToNormalized(const RectTransformComponent& rect,
        const DirectX::XMFLOAT2& canvas)
    {
        const DirectX::XMFLOAT4 r = rect.ResolvedRect();
        const DirectX::XMFLOAT2 local = InverseTransformPoint2D(
            rect.ResolvedMatrix(), canvas.x, canvas.y);
        return {
            (local.x - r.x) / (std::max)(0.0001f, r.z),
            (local.y - r.y) / (std::max)(0.0001f, r.w)
        };
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

    // ImGuiとDX12は同じクライアント座標を使う。ここで画面座標へ変換すると、
    // ウィンドウ位置の分だけ選択枠と素材がずれるため変換を挟まない。
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

    // カメラ移動。ホイール長押しドラッグ、または Shift + ドラッグ。
    // 掴む場所は Canvas の外でもよいので scene_view_hovered で判定する。
    {
        ImGuiIO& io = ImGui::GetIO();
        const bool pan_held = ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
            (io.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left));
        if (!ui_preview_panning && scene_view_hovered && pan_held &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
            ui_preview_panning = true;
        if (ui_preview_panning && !pan_held) ui_preview_panning = false;
        if (ui_preview_panning)
        {
            ui_preview_pan_x += io.MouseDelta.x;
            ui_preview_pan_y += io.MouseDelta.y;
            ui_scene_view_input_consumed = true;
            viewport_drag_selecting = false;
        }
    }

    // ホイールはカーソル下の点を固定したまま拡大縮小する。
    // 中心固定だと目的の要素が画面外へ逃げて追いかける操作が増える。
    if (scene_view_hovered && !ui_preview_panning && ImGui::GetIO().MouseWheel != 0.0f)
    {
        const float previous_zoom = (std::max)(0.10f, ui_preview_zoom);
        const float next_zoom = (std::min)(2.0f, (std::max)(0.10f,
            previous_zoom + ImGui::GetIO().MouseWheel * 0.05f));
        if (next_zoom != previous_zoom)
        {
            const float canvas_x = (mouse.x - target.left) / previous_zoom;
            const float canvas_y = (mouse.y - target.top) / previous_zoom;
            ui_preview_pan_x += mouse.x - canvas_x * next_zoom - target.left -
                logical_width * (previous_zoom - next_zoom) * 0.5f;
            ui_preview_pan_y += mouse.y - canvas_y * next_zoom - target.top -
                logical_height * (previous_zoom - next_zoom) * 0.5f;
            ui_preview_zoom = next_zoom;
        }
        ui_scene_view_input_consumed = true;
    }

    Core::GameObject* selected =
        object_editor_context.Selection().ResolvePrimary(*scene);
    Core::GameObject* selected_transform_target = UITransformEditTarget(selected);
    RectTransformComponent* selected_rect = selected_transform_target != nullptr
        ? selected_transform_target->GetComponent<RectTransformComponent>() : nullptr;
    const bool selected_custom_shape = selected_transform_target != nullptr &&
        [&]()
        {
            const UIShapeComponent* shape =
                selected_transform_target->GetComponent<UIShapeComponent>();
            if (shape != nullptr && shape->shape == UIShapeComponent::CustomBezierPath)
                return true;
            return selected_transform_target->GetComponent<UIShapeImageComponent>() != nullptr;
        }();
    float selected_canvas_scale = CanvasScaleForObject(selected_transform_target,
        logical_width, logical_height);
    UIEffectStackComponent* selected_effect_stack = selected_transform_target != nullptr
        ? selected_transform_target->GetComponent<UIEffectStackComponent>() : nullptr;
    constexpr int max_effect_region_count =
        ReplayEngine::UI::UIEffectRegion::MaxAdditionalCount + 1;
    ImVec2 effect_region_outline[max_effect_region_count][4]{};
    ImVec2 effect_region_handle_points[max_effect_region_count][9]{};
    const auto effect_region_count = [&]()
    {
        if (selected_effect_stack == nullptr) return 0;
        return (std::min)(max_effect_region_count, 1 + static_cast<int>(
            selected_effect_stack->effect_region.additional.size()));
    };
    const auto effect_region_at = [&](int index)
        -> ReplayEngine::UI::UIEffectRegionData*
    {
        if (selected_effect_stack == nullptr || index < 0 ||
            index >= effect_region_count()) return nullptr;
        if (index == 0) return &selected_effect_stack->effect_region;
        return &selected_effect_stack->effect_region.additional[
            static_cast<std::size_t>(index - 1)];
    };
    const auto effect_region_uv_to_scene = [&](const DirectX::XMFLOAT2& uv)
    {
        if (selected_rect == nullptr) return ImVec2{};
        // Effect shaderのUVは左上原点、UI normalizedは左下原点。
        const DirectX::XMFLOAT2 normalized{ uv.x, 1.0f - uv.y };
        const DirectX::XMFLOAT2 canvas = NormalizedToCanvas(*selected_rect, normalized);
        return ToSceneUIPoint(canvas, selected_canvas_scale,
            target.left, target.top, target_width, target_height,
            logical_width, logical_height);
    };
    const auto scene_to_effect_region_uv = [&](const ImVec2& point)
    {
        if (selected_rect == nullptr) return DirectX::XMFLOAT2{};
        const DirectX::XMFLOAT2 canvas{
            (point.x - target.left) * (logical_width / target_width) /
                (std::max)(0.0001f, selected_canvas_scale),
            (logical_height - (point.y - target.top) *
                (logical_height / target_height)) /
                (std::max)(0.0001f, selected_canvas_scale) };
        const DirectX::XMFLOAT2 normalized = CanvasToNormalized(*selected_rect, canvas);
        return DirectX::XMFLOAT2{ normalized.x, 1.0f - normalized.y };
    };
    const auto effect_region_is_visible = [&](int index)
    {
        const ReplayEngine::UI::UIEffectRegionData* region = effect_region_at(index);
        return region != nullptr && selected_rect != nullptr &&
            selected_effect_stack->enabled && region->enabled;
    };
    const auto rebuild_effect_region_handles = [&]()
    {
        for (int region_index = 0; region_index < max_effect_region_count; ++region_index)
        {
            for (ImVec2& point : effect_region_outline[region_index]) point = ImVec2{};
            for (ImVec2& point : effect_region_handle_points[region_index]) point = ImVec2{};
            if (!effect_region_is_visible(region_index)) continue;

            const ReplayEngine::UI::UIEffectRegionData& region =
                *effect_region_at(region_index);
            const float half_x = (std::max)(0.001f, region.size.x);
            const float half_y = (std::max)(0.001f, region.size.y);
            const float angle = region.rotation * DirectX::XM_PI / 180.0f;
            const float s = std::sin(angle);
            const float c = std::cos(angle);
            const DirectX::XMFLOAT2 local_corners[4]{
                { -half_x, -half_y }, { half_x, -half_y },
                { half_x, half_y }, { -half_x, half_y } };
            const DirectX::XMFLOAT2 local_handles[8]{
                { -half_x, -half_y }, { half_x, -half_y },
                { half_x, half_y }, { -half_x, half_y },
                { -half_x, 0.0f }, { half_x, 0.0f },
                { 0.0f, half_y }, { 0.0f, -half_y } };
            const auto scene_point_from_region_uv = [&](const DirectX::XMFLOAT2& uv)
            {
                // Effect shader のUVは左上原点、UIのnormalized座標は左下原点。
                const DirectX::XMFLOAT2 normalized{ uv.x, 1.0f - uv.y };
                const DirectX::XMFLOAT2 canvas = NormalizedToCanvas(*selected_rect,
                    normalized);
                return ToSceneUIPoint(canvas, selected_canvas_scale,
                    target.left, target.top, target_width, target_height,
                    logical_width, logical_height);
            };
            const auto to_region_uv = [&](const DirectX::XMFLOAT2& local)
            {
                return DirectX::XMFLOAT2{
                    region.center.x + local.x * c - local.y * s,
                    region.center.y + local.x * s + local.y * c };
            };
            for (int index = 0; index < 4; ++index)
                effect_region_outline[region_index][index] = scene_point_from_region_uv(
                    to_region_uv(local_corners[index]));
            for (int index = 0; index < 8; ++index)
                effect_region_handle_points[region_index][index] = scene_point_from_region_uv(
                    to_region_uv(local_handles[index]));
            effect_region_handle_points[region_index][8] = scene_point_from_region_uv(
                to_region_uv({ 0.0f, -half_y - 0.08f }));
        }
    };
    rebuild_effect_region_handles();
    ImVec2 resize_handle_points[8]{};
    int hovered_resize_handle = -1;
    int pressed_resize_handle = -1;
    int hovered_effect_region_index = -1;
    int hovered_effect_region_handle = -1;
    int pressed_effect_region_index = -1;
    int pressed_effect_region_handle = -1;
    int hovered_effect_region_point = -1;
    int pressed_effect_region_point = -1;
    const ImGuiIO& input = ImGui::GetIO();
    const ImVec2 press_position = input.MouseClickedPos[ImGuiMouseButton_Left];
    const bool press_inside_target =
        press_position.x >= target.left && press_position.y >= target.top &&
        press_position.x < target.left + target_width &&
        press_position.y < target.top + target_height;
    if (effect_region_count() > 0)
    {
        const auto hit_effect_region_handle = [&](const ImVec2& position,
            int& out_region_index)
        {
            int hit = -1;
            float nearest = 12.0f * 12.0f;
            out_region_index = -1;
            for (int region_index = 0; region_index < effect_region_count(); ++region_index)
            {
                if (!effect_region_is_visible(region_index)) continue;
                const ReplayEngine::UI::UIEffectRegionData* region =
                    effect_region_at(region_index);
                if (region == nullptr || region->shape == static_cast<int>(
                    ReplayEngine::UI::UIEffectRegionShape::Freeform)) continue;
                for (int index = 0; index < 9; ++index)
                {
                    const float dx = effect_region_handle_points[region_index][index].x -
                        position.x;
                    const float dy = effect_region_handle_points[region_index][index].y -
                        position.y;
                    const float distance_sq = dx * dx + dy * dy;
                    if (distance_sq <= nearest)
                    {
                        nearest = distance_sq;
                        hit = index;
                        out_region_index = region_index;
                    }
                }
            }
            return hit;
        };
        if (scene_view_hovered)
            hovered_effect_region_handle = hit_effect_region_handle(mouse,
                hovered_effect_region_index);
        // 移動ドラッグ中は範囲ハンドルの座標も動くため、押した瞬間の判定だけを使う。
        if (input.MouseDown[ImGuiMouseButton_Left] && !ui_preview_drag_object.Valid())
            pressed_effect_region_handle = hit_effect_region_handle(press_position,
                pressed_effect_region_index);

        const auto hit_freeform = [&](const ImVec2& position,
            int& out_region_index, int& out_handle)
        {
            out_region_index = -1;
            out_handle = -1;
            float nearest = 12.0f * 12.0f;
            for (int region_index = 0; region_index < effect_region_count(); ++region_index)
            {
                const ReplayEngine::UI::UIEffectRegionData* region =
                    effect_region_at(region_index);
                if (!effect_region_is_visible(region_index) || region == nullptr ||
                    region->shape != static_cast<int>(ReplayEngine::UI::UIEffectRegionShape::Freeform) ||
                    region->path_points.size() < 2) continue;
                const std::size_t count = (std::min)(region->path_points.size(),
                    static_cast<std::size_t>(32));
                for (std::size_t index = 0; index < count; ++index)
                {
                    const ImVec2 point = effect_region_uv_to_scene(region->path_points[index]);
                    const float dx = point.x - position.x;
                    const float dy = point.y - position.y;
                    const float distance_sq = dx * dx + dy * dy;
                    if (distance_sq <= nearest)
                    {
                        nearest = distance_sq;
                        out_region_index = region_index;
                        out_handle = 9 + static_cast<int>(index);
                    }
                }
                const std::size_t segment_count = region->path_closed ? count : count - 1;
                for (std::size_t index = 0; index < segment_count; ++index)
                {
                    const std::size_t next = (index + 1) % count;
                    const ImVec2 a = effect_region_uv_to_scene(region->path_points[index]);
                    const ImVec2 b = effect_region_uv_to_scene(region->path_points[next]);
                    const float vx = b.x - a.x;
                    const float vy = b.y - a.y;
                    const float length_sq = vx * vx + vy * vy;
                    const float wx = position.x - a.x;
                    const float wy = position.y - a.y;
                    const float t = length_sq > 0.0001f
                        ? (std::max)(0.0f, (std::min)(1.0f,
                            (wx * vx + wy * vy) / length_sq)) : 0.0f;
                    const float dx = a.x + vx * t - position.x;
                    const float dy = a.y + vy * t - position.y;
                    const float distance_sq = dx * dx + dy * dy;
                    if (distance_sq <= nearest)
                    {
                        nearest = distance_sq;
                        out_region_index = region_index;
                        out_handle = 1000 + static_cast<int>(index);
                    }
                }
            }
            return out_region_index >= 0;
        };
        if (scene_view_hovered)
        {
            int freeform_handle = -1;
            int freeform_region = -1;
            if (hit_freeform(mouse, freeform_region, freeform_handle))
            {
                hovered_effect_region_index = freeform_region;
                hovered_effect_region_handle = freeform_handle;
                hovered_effect_region_point = freeform_handle >= 9 &&
                    freeform_handle < 1000 ? freeform_handle - 9 : -1;
            }
        }
        if (input.MouseDown[ImGuiMouseButton_Left] && !ui_preview_drag_object.Valid())
        {
            int freeform_handle = -1;
            int freeform_region = -1;
            if (hit_freeform(press_position, freeform_region, freeform_handle))
            {
                pressed_effect_region_index = freeform_region;
                pressed_effect_region_handle = freeform_handle;
                pressed_effect_region_point = freeform_handle >= 9 &&
                    freeform_handle < 1000 ? freeform_handle - 9 : -1;
            }
        }
    }

    // Effect範囲は通常のUI枠より先に拾う。範囲の回転ハンドルが対象外の
    // Object選択やRectTransform操作へ流れないようにする。
    bool effect_region_click = ui_effect_region_candidate;
    if (!ui_effect_region_candidate && pressed_effect_region_handle >= 0 &&
        input.MouseDown[ImGuiMouseButton_Left])
    {
        effect_region_click = true;
        ui_scene_view_input_consumed = true;
        viewport_drag_selecting = false;
        if (object_editor_context.CanEdit() && selected_transform_target != nullptr &&
            selected_effect_stack != nullptr)
        {
            ui_effect_region_candidate = true;
            ui_effect_region_editing = false;
            ui_effect_region_index = pressed_effect_region_index;
            ui_effect_region_handle = pressed_effect_region_handle;
            ui_effect_region_point = pressed_effect_region_point;
            ui_effect_region_object = selected_transform_target->ID();
            ui_effect_region_selected_index = pressed_effect_region_index;
            ui_effect_region_selected_point = pressed_effect_region_point;
            ui_effect_region_selected_object = selected_transform_target->ID();
            ui_effect_region_start_mouse = press_position;
            if (ReplayEngine::UI::UIEffectRegionData* region = effect_region_at(
                pressed_effect_region_index))
            {
                if (pressed_effect_region_handle >= 1000 && region->path_points.size() >= 2)
                {
                    const std::size_t segment = static_cast<std::size_t>(
                        pressed_effect_region_handle - 1000);
                    const std::size_t count = (std::min)(region->path_points.size(),
                        static_cast<std::size_t>(32));
                    if (segment < count && object_editor_context.CanEdit())
                    {
                        const std::size_t next = (segment + 1) % count;
                        const ImVec2 a = effect_region_uv_to_scene(region->path_points[segment]);
                        const ImVec2 b = effect_region_uv_to_scene(region->path_points[next]);
                        const float vx = b.x - a.x;
                        const float vy = b.y - a.y;
                        const float length_sq = vx * vx + vy * vy;
                        const float wx = press_position.x - a.x;
                        const float wy = press_position.y - a.y;
                        const float t = length_sq > 0.0001f
                            ? (std::max)(0.0f, (std::min)(1.0f,
                                (wx * vx + wy * vy) / length_sq)) : 0.5f;
                        const DirectX::XMFLOAT2 inserted{
                            region->path_points[segment].x +
                                (region->path_points[next].x - region->path_points[segment].x) * t,
                            region->path_points[segment].y +
                                (region->path_points[next].y - region->path_points[segment].y) * t };
                        const std::size_t insert_at = segment + 1;
                        object_editor_context.BeginEdit("Effect範囲の頂点を追加");
                        region->path_points.insert(region->path_points.begin() +
                            static_cast<std::ptrdiff_t>(insert_at), inserted);
                        ui_effect_region_point = static_cast<int>(insert_at);
                        ui_effect_region_handle = 9 + static_cast<int>(insert_at);
                        ui_effect_region_selected_index = pressed_effect_region_index;
                        ui_effect_region_selected_point = static_cast<int>(insert_at);
                        ui_effect_region_selected_object = selected_transform_target->ID();
                        ui_effect_region_editing = true;
                    }
                }
                ui_effect_region_start_center = region->center;
                ui_effect_region_start_size = region->size;
                ui_effect_region_start_rotation = region->rotation;
            }
            ui_preview_drag_object = Core::ObjectID::Invalid();
            ui_preview_dragging = false;
        }
    }
    if (!selected_custom_shape && selected_transform_target != nullptr &&
        selected_rect != nullptr && HasUIComponent(*selected_transform_target))
    {
        SceneResizeHandlePoints(*selected_rect, selected_canvas_scale,
            target.left, target.top, target_width, target_height,
            logical_width, logical_height, resize_handle_points);
        const ImVec2 resize_corners[4]{
            resize_handle_points[ResizeBottomLeft],
            resize_handle_points[ResizeBottomRight],
            resize_handle_points[ResizeTopRight],
            resize_handle_points[ResizeTopLeft] };
        if (target_hovered)
            hovered_resize_handle = HitResizeBorder(resize_corners, mouse);
        // 判定はボタンを押した瞬間だけ行う。ドラッグ中は矩形が動くので角ハンドルの
        // 座標も動き、押した位置へ後から重なった時点でリサイズへ乗り換えてしまう。
        if (input.MouseDown[ImGuiMouseButton_Left] && press_inside_target && !ui_preview_panning &&
            !ui_preview_drag_object.Valid())
            pressed_resize_handle = HitResizeBorder(resize_corners, press_position);
    }

    // Rect Tool は UI 内部の部位操作や Object 選択より先に拾う。
    // 同じドラッグを 3D の矩形選択へ渡さないことが最重要。
    bool resize_handle_click = effect_region_click;
    if (!effect_region_click && !ui_preview_resize_candidate && pressed_resize_handle >= 0 &&
        input.MouseDown[ImGuiMouseButton_Left])
    {
        resize_handle_click = true;
        ui_scene_view_input_consumed = true;
        viewport_drag_selecting = false;
        if (object_editor_context.CanEdit() && selected_transform_target != nullptr &&
            selected_rect != nullptr)
        {
            ui_preview_resize_candidate = true;
            ui_preview_resizing = false;
            ui_preview_resize_handle = pressed_resize_handle;
            ui_preview_resize_object = selected_transform_target->ID();
            ui_preview_resize_start_mouse = press_position;
            ui_preview_resize_start_rect = selected_rect->ResolvedRect();
            ui_preview_resize_parent_rect = ParentResolvedRect(
                *selected_transform_target, logical_width / selected_canvas_scale,
                logical_height / selected_canvas_scale);
            ui_preview_resize_start_matrix = selected_rect->ResolvedMatrix();

            ui_preview_drag_object = Core::ObjectID::Invalid();
            ui_preview_dragging = false;
        }
    }

    // Puppet Pin / Custom Bezier anchor・handle は RectTransform の移動より先に拾う。
    // これにより小さい部位編集で親Objectまで動いてしまわない。
    // 自由図形の子Imageを選択している場合も、輪郭コントローラーは
    // 親の自由図形マスクへ向ける。RectTransform のリサイズ操作とは独立した専用部位。
    Core::GameObject* control_selected = selected_custom_shape
        ? selected_transform_target : selected;
    RectTransformComponent* control_rect = control_selected != nullptr
        ? control_selected->GetComponent<RectTransformComponent>() : nullptr;
    const float control_canvas_scale = CanvasScaleForObject(control_selected,
        logical_width, logical_height);
    const DirectX::XMFLOAT2 mouse_canvas{
        logical_mouse_x / (std::max)(0.0001f, control_canvas_scale),
        logical_mouse_y / (std::max)(0.0001f, control_canvas_scale) };
    bool subcontrol_click = false;
    const auto scene_point_for_normalized = [&](const DirectX::XMFLOAT2& normalized)
    {
        if (control_rect == nullptr) return ImVec2{};
        const DirectX::XMFLOAT2 canvas = NormalizedToCanvas(*control_rect, normalized);
        return ToSceneUIPoint(canvas, control_canvas_scale,
            target.left, target.top, target_width, target_height,
            logical_width, logical_height);
    };
    const auto scene_distance_sq = [&](const DirectX::XMFLOAT2& normalized)
    {
        if (control_rect == nullptr) return (std::numeric_limits<float>::max)();
        const ImVec2 point = scene_point_for_normalized(normalized);
        const float dx = point.x - mouse.x;
        const float dy = point.y - mouse.y;
        return dx * dx + dy * dy;
    };

    if (!effect_region_click && !resize_handle_click && !ui_preview_panning && target_hovered &&
        control_selected != nullptr && control_rect != nullptr &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (UIPuppetDeformComponent* puppet =
            control_selected->GetComponent<UIPuppetDeformComponent>())
        {
            int nearest_radius = -1;
            float nearest_radius_d2 = 100.0f;
            for (int index = 0; index < puppet->PinCount(); ++index)
            {
                const std::size_t pin_index = static_cast<std::size_t>(index);
                if (pin_index >= puppet->pin_bind_positions.size() ||
                    pin_index >= puppet->pin_radii.size()) continue;
                const DirectX::XMFLOAT2 bind = puppet->pin_bind_positions[pin_index];
                const float radius = (std::max)(0.001f, puppet->pin_radii[pin_index]);
                const float d2 = scene_distance_sq({ bind.x + radius, bind.y });
                if (d2 < nearest_radius_d2)
                {
                    nearest_radius_d2 = d2;
                    nearest_radius = index;
                }
            }
            if (nearest_radius >= 0)
            {
                ui_puppet_active_pin = -1;
                ui_puppet_active_radius = nearest_radius;
                ui_puppet_selected_pin = nearest_radius;
                ui_shape_active_point = -1;
                ui_subcontrol_dragging = object_editor_context.CanEdit();
                if (ui_subcontrol_dragging)
                    object_editor_context.BeginEdit("Puppet Pin半径を変更");
                subcontrol_click = true;
                ui_scene_view_input_consumed = true;
            }
            if (!subcontrol_click)
            {
                int nearest = -1;
                float nearest_d2 = 100.0f;
                for (int index = 0; index < puppet->PinCount(); ++index)
                {
                    const float d2 = scene_distance_sq(puppet->pin_positions[
                        static_cast<std::size_t>(index)]);
                    if (d2 < nearest_d2) { nearest_d2 = d2; nearest = index; }
                }
                if (nearest >= 0)
                {
                    ui_puppet_active_pin = nearest;
                    ui_puppet_active_radius = -1;
                    ui_puppet_selected_pin = nearest;
                    ui_shape_active_point = -1;
                    ui_subcontrol_dragging = object_editor_context.CanEdit();
                    if (ui_subcontrol_dragging) object_editor_context.BeginEdit("Puppet Pinを移動");
                    subcontrol_click = true;
                    ui_scene_view_input_consumed = true;
                }
            }
        }
        if (!subcontrol_click)
        {
            int nearest_point = -1;
            int nearest_handle = 0;
            float nearest_d2 = 100.0f;
            const auto find_nearest_shape_control = [&](const auto& shape)
            {
                for (int index = 0; index < static_cast<int>(shape->path_points.size()); ++index)
                {
                    const DirectX::XMFLOAT2 anchor_point = shape->path_points[index];
                    const float anchor_d2 = scene_distance_sq(anchor_point);
                    if (anchor_d2 < nearest_d2)
                    { nearest_d2 = anchor_d2; nearest_point = index; nearest_handle = 0; }
                    if (index < static_cast<int>(shape->path_in_handles.size()))
                    {
                        const auto h = shape->path_in_handles[index];
                        const float d2 = scene_distance_sq({ anchor_point.x + h.x,
                            anchor_point.y + h.y });
                        if (d2 < nearest_d2)
                        { nearest_d2 = d2; nearest_point = index; nearest_handle = 1; }
                    }
                    if (index < static_cast<int>(shape->path_out_handles.size()))
                    {
                        const auto h = shape->path_out_handles[index];
                        const float d2 = scene_distance_sq({ anchor_point.x + h.x,
                            anchor_point.y + h.y });
                        if (d2 < nearest_d2)
                        { nearest_d2 = d2; nearest_point = index; nearest_handle = 2; }
                    }
                }
            };
            if (UIShapeComponent* shape = control_selected->GetComponent<UIShapeComponent>();
                shape != nullptr && shape->shape == UIShapeComponent::CustomBezierPath)
                find_nearest_shape_control(shape);
            else if (UIShapeImageComponent* shape =
                control_selected->GetComponent<UIShapeImageComponent>())
                find_nearest_shape_control(shape);

            if (nearest_point >= 0)
            {
                ui_shape_active_point = nearest_point;
                ui_shape_selected_point = nearest_point;
                ui_shape_active_handle = nearest_handle;
                ui_puppet_active_pin = -1;
                ui_subcontrol_dragging = object_editor_context.CanEdit();
                if (ui_subcontrol_dragging) object_editor_context.BeginEdit("自由図形の頂点を移動");
                subcontrol_click = true;
                ui_scene_view_input_consumed = true;
            }
            if (!subcontrol_click)
            {
                const auto lerp_point = [](const DirectX::XMFLOAT2& a,
                    const DirectX::XMFLOAT2& b, float t)
                {
                    return DirectX::XMFLOAT2{
                        a.x + (b.x - a.x) * t,
                        a.y + (b.y - a.y) * t };
                };
                const auto evaluate_curve = [&](const DirectX::XMFLOAT2& p0,
                    const DirectX::XMFLOAT2& p1, const DirectX::XMFLOAT2& p2,
                    const DirectX::XMFLOAT2& p3, float t)
                {
                    const float u = 1.0f - t;
                    return DirectX::XMFLOAT2{
                        u * u * u * p0.x + 3.0f * u * u * t * p1.x +
                            3.0f * u * t * t * p2.x + t * t * t * p3.x,
                        u * u * u * p0.y + 3.0f * u * u * t * p1.y +
                            3.0f * u * t * t * p2.y + t * t * t * p3.y };
                };
                const auto handle_at = [](const auto& handles, std::size_t index)
                {
                    return index < handles.size()
                        ? handles[index] : DirectX::XMFLOAT2{};
                };
                const auto find_nearest_shape_segment = [&](const auto* shape,
                    int& out_segment, float& out_t)
                {
                    out_segment = -1;
                    out_t = 0.0f;
                    if (shape == nullptr || shape->path_points.size() < 2)
                        return false;
                    const std::size_t count = shape->path_points.size();
                    const std::size_t segment_count = shape->path_closed
                        ? count : count - 1;
                    float nearest_segment_d2 = 100.0f;
                    constexpr int samples = 48;
                    for (std::size_t segment = 0; segment < segment_count; ++segment)
                    {
                        const std::size_t next = (segment + 1) % count;
                        const DirectX::XMFLOAT2 p0 = shape->path_points[segment];
                        const DirectX::XMFLOAT2 p3 = shape->path_points[next];
                        const DirectX::XMFLOAT2 p1 = {
                            p0.x + handle_at(shape->path_out_handles, segment).x,
                            p0.y + handle_at(shape->path_out_handles, segment).y };
                        const DirectX::XMFLOAT2 p2 = {
                            p3.x + handle_at(shape->path_in_handles, next).x,
                            p3.y + handle_at(shape->path_in_handles, next).y };
                        DirectX::XMFLOAT2 previous = p0;
                        ImVec2 previous_scene = scene_point_for_normalized(previous);
                        for (int sample = 1; sample <= samples; ++sample)
                        {
                            const float t1 = static_cast<float>(sample) /
                                static_cast<float>(samples);
                            const DirectX::XMFLOAT2 current = evaluate_curve(
                                p0, p1, p2, p3, t1);
                            const ImVec2 current_scene =
                                scene_point_for_normalized(current);
                            const float vx = current_scene.x - previous_scene.x;
                            const float vy = current_scene.y - previous_scene.y;
                            const float length_sq = vx * vx + vy * vy;
                            const float wx = mouse.x - previous_scene.x;
                            const float wy = mouse.y - previous_scene.y;
                            const float local_t = length_sq > 0.0001f
                                ? (std::max)(0.0f, (std::min)(1.0f,
                                    (wx * vx + wy * vy) / length_sq)) : 0.0f;
                            const DirectX::XMFLOAT2 candidate = lerp_point(
                                previous, current, local_t);
                            const ImVec2 candidate_scene =
                                scene_point_for_normalized(candidate);
                            const float dx = candidate_scene.x - mouse.x;
                            const float dy = candidate_scene.y - mouse.y;
                            const float distance_sq = dx * dx + dy * dy;
                            if (distance_sq < nearest_segment_d2)
                            {
                                nearest_segment_d2 = distance_sq;
                                out_segment = static_cast<int>(segment);
                                out_t = (static_cast<float>(sample - 1) + local_t) /
                                    static_cast<float>(samples);
                            }
                            previous = current;
                            previous_scene = current_scene;
                        }
                    }
                    return out_segment >= 0;
                };
                const auto insert_shape_point = [&](auto* shape, int segment, float t)
                {
                    if (shape == nullptr || !object_editor_context.CanEdit() ||
                        shape->path_points.size() >= 64 || segment < 0)
                        return false;
                    const std::size_t count = shape->path_points.size();
                    if (count < 2 || static_cast<std::size_t>(segment) >= count)
                        return false;
                    const std::size_t segment_index = static_cast<std::size_t>(segment);
                    const std::size_t next_index = (segment_index + 1) % count;
                    const DirectX::XMFLOAT2 p0 = shape->path_points[segment_index];
                    const DirectX::XMFLOAT2 p3 = shape->path_points[next_index];
                    shape->path_in_handles.resize(count, DirectX::XMFLOAT2{});
                    shape->path_out_handles.resize(count, DirectX::XMFLOAT2{});
                    const DirectX::XMFLOAT2 p1{
                        p0.x + shape->path_out_handles[segment_index].x,
                        p0.y + shape->path_out_handles[segment_index].y };
                    const DirectX::XMFLOAT2 p2{
                        p3.x + shape->path_in_handles[next_index].x,
                        p3.y + shape->path_in_handles[next_index].y };
                    t = (std::max)(0.01f, (std::min)(0.99f, t));
                    const DirectX::XMFLOAT2 q0 = lerp_point(p0, p1, t);
                    const DirectX::XMFLOAT2 q1 = lerp_point(p1, p2, t);
                    const DirectX::XMFLOAT2 q2 = lerp_point(p2, p3, t);
                    const DirectX::XMFLOAT2 r0 = lerp_point(q0, q1, t);
                    const DirectX::XMFLOAT2 r1 = lerp_point(q1, q2, t);
                    const DirectX::XMFLOAT2 inserted = lerp_point(r0, r1, t);

                    object_editor_context.BeginEdit("自由図形の線上に頂点を追加");
                    shape->path_out_handles[segment_index] = {
                        q0.x - p0.x, q0.y - p0.y };
                    shape->path_in_handles[next_index] = {
                        q2.x - p3.x, q2.y - p3.y };
                    const std::size_t insert_at = segment_index + 1;
                    shape->path_points.insert(shape->path_points.begin() +
                        static_cast<std::ptrdiff_t>(insert_at), inserted);
                    shape->path_in_handles.insert(shape->path_in_handles.begin() +
                        static_cast<std::ptrdiff_t>(insert_at),
                        DirectX::XMFLOAT2{ r0.x - inserted.x, r0.y - inserted.y });
                    shape->path_out_handles.insert(shape->path_out_handles.begin() +
                        static_cast<std::ptrdiff_t>(insert_at),
                        DirectX::XMFLOAT2{ r1.x - inserted.x, r1.y - inserted.y });
                    shape->OnPropertyChanged("path_points");
                    ui_shape_active_point = static_cast<int>(insert_at);
                    ui_shape_selected_point = static_cast<int>(insert_at);
                    ui_shape_active_handle = 0;
                    ui_puppet_active_pin = -1;
                    ui_subcontrol_dragging = true;
                    return true;
                };

                int nearest_segment = -1;
                float nearest_segment_t = 0.0f;
                bool inserted = false;
                if (UIShapeComponent* shape =
                    control_selected->GetComponent<UIShapeComponent>();
                    shape != nullptr && shape->shape == UIShapeComponent::CustomBezierPath &&
                    find_nearest_shape_segment(shape, nearest_segment, nearest_segment_t))
                {
                    inserted = insert_shape_point(shape, nearest_segment, nearest_segment_t);
                }
                else if (UIShapeImageComponent* shape =
                    control_selected->GetComponent<UIShapeImageComponent>();
                    shape != nullptr &&
                    find_nearest_shape_segment(shape, nearest_segment, nearest_segment_t))
                {
                    inserted = insert_shape_point(shape, nearest_segment, nearest_segment_t);
                }
                if (inserted)
                {
                    subcontrol_click = true;
                    ui_scene_view_input_consumed = true;
                }
            }
        }
    }

    if (ui_subcontrol_dragging && control_selected != nullptr && control_rect != nullptr &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const DirectX::XMFLOAT2 normalized = CanvasToNormalized(*control_rect, mouse_canvas);
        if (UIPuppetDeformComponent* puppet = control_selected->GetComponent<UIPuppetDeformComponent>();
            ui_puppet_active_radius >= 0 && puppet != nullptr &&
            ui_puppet_active_radius < puppet->PinCount())
        {
            const std::size_t index = static_cast<std::size_t>(ui_puppet_active_radius);
            if (index < puppet->pin_bind_positions.size() && index < puppet->pin_radii.size())
            {
                const DirectX::XMFLOAT2 bind = puppet->pin_bind_positions[index];
                const float dx = normalized.x - bind.x;
                const float dy = normalized.y - bind.y;
                puppet->pin_radii[index] = (std::max)(0.001f,
                    std::sqrt(dx * dx + dy * dy));
            }
        }
        else if (UIPuppetDeformComponent* puppet =
            control_selected->GetComponent<UIPuppetDeformComponent>();
            ui_puppet_active_pin >= 0 && puppet != nullptr &&
            ui_puppet_active_pin < puppet->PinCount())
        {
            puppet->pin_positions[static_cast<std::size_t>(ui_puppet_active_pin)] = normalized;
        }
        else
        {
            const auto drag_shape_control = [&](auto* shape)
            {
                if (shape == nullptr || ui_shape_active_point < 0 ||
                    ui_shape_active_point >= static_cast<int>(shape->path_points.size()))
                    return false;
                const std::size_t index = static_cast<std::size_t>(ui_shape_active_point);
                if (ui_shape_active_handle == 0) shape->path_points[index] = normalized;
                else
                {
                    const auto anchor_point = shape->path_points[index];
                    const DirectX::XMFLOAT2 relative{ normalized.x - anchor_point.x,
                        normalized.y - anchor_point.y };
                    if (ui_shape_active_handle == 1 && index < shape->path_in_handles.size())
                        shape->path_in_handles[index] = relative;
                    if (ui_shape_active_handle == 2 && index < shape->path_out_handles.size())
                        shape->path_out_handles[index] = relative;
                }
                return true;
            };
            if (!drag_shape_control(control_selected->GetComponent<UIShapeComponent>()))
                drag_shape_control(control_selected->GetComponent<UIShapeImageComponent>());
        }
        ui_scene_view_input_consumed = true;
    }
    if (ui_subcontrol_dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        object_editor_context.CommitEdit();
        ui_subcontrol_dragging = false;
        ui_puppet_active_pin = -1;
        ui_puppet_active_radius = -1;
        ui_shape_active_point = -1;
        ui_shape_active_handle = 0;
    }

    if (!resize_handle_click && !subcontrol_click && !ui_preview_panning && target_hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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
            Core::GameObject* transform_target = UITransformEditTarget(picked);
            if (RectTransformComponent* rect = transform_target != nullptr
                ? transform_target->GetComponent<RectTransformComponent>() : nullptr)
            {
                ui_preview_drag_object = transform_target->ID();
                ui_preview_drag_start_mouse = mouse;
                ui_preview_drag_start_position = rect->anchored_position;
            }
            ui_scene_view_input_consumed = true;
        }
    }

    // Object 選択で対象が変わる可能性があるため、操作開始前に取り直す。
    selected = object_editor_context.Selection().ResolvePrimary(*scene);
    selected_transform_target = UITransformEditTarget(selected);
    selected_rect = selected_transform_target != nullptr
        ? selected_transform_target->GetComponent<RectTransformComponent>() : nullptr;
    selected_canvas_scale = CanvasScaleForObject(selected_transform_target,
        logical_width, logical_height);
    selected_effect_stack = selected_transform_target != nullptr
        ? selected_transform_target->GetComponent<UIEffectStackComponent>() : nullptr;
    rebuild_effect_region_handles();

    if (selected_transform_target == nullptr ||
        ui_effect_region_selected_object != selected_transform_target->ID())
    {
        ui_effect_region_selected_index = -1;
        ui_effect_region_selected_point = -1;
        ui_effect_region_selected_object = Core::ObjectID::Invalid();
    }

    const bool effect_point_delete_pressed = scene_view_focused &&
        !ImGui::GetIO().WantTextInput && object_editor_context.CanEdit() &&
        (ImGui::IsKeyPressed(VK_BACK) || ImGui::IsKeyPressed(VK_DELETE));
    if (effect_point_delete_pressed && selected_effect_stack != nullptr &&
        ui_effect_region_selected_point >= 0 &&
        ui_effect_region_selected_index >= 0 &&
        ui_effect_region_selected_object == selected_transform_target->ID())
    {
        ReplayEngine::UI::UIEffectRegionData* selected_region = effect_region_at(
            ui_effect_region_selected_index);
        if (selected_region != nullptr && ui_effect_region_selected_point <
            static_cast<int>(selected_region->path_points.size()))
        {
            const std::size_t minimum_points = selected_region->path_closed ? 3u : 2u;
            if (selected_region->path_points.size() > minimum_points)
            {
                object_editor_context.BeginEdit("Effect範囲の頂点を削除");
                selected_region->path_points.erase(selected_region->path_points.begin() +
                    ui_effect_region_selected_point);
                selected_effect_stack->OnPropertyChanged(nullptr);
                ui_effect_region_selected_point = (std::min)(
                    ui_effect_region_selected_point,
                    static_cast<int>(selected_region->path_points.size()) - 1);
                object_editor_context.CommitEdit();
            }
            else
            {
                object_editor_context.SetStatus("閉じた適用範囲は3頂点未満にできません");
            }
        }
    }

    const bool effect_region_candidate = selected_transform_target != nullptr &&
        selected_effect_stack != nullptr && ui_effect_region_candidate &&
        effect_region_at(ui_effect_region_index) != nullptr &&
        ui_effect_region_object == selected_transform_target->ID();
    if (effect_region_candidate && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
    {
        ui_scene_view_input_consumed = true;
        viewport_drag_selecting = false;
        if (!ui_effect_region_editing && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("Effect適用範囲を編集");
            ui_effect_region_editing = true;
        }
        if (ui_effect_region_editing && selected_rect != nullptr)
        {
            const DirectX::XMFLOAT2 canvas_mouse{
                logical_mouse_x / (std::max)(0.0001f, selected_canvas_scale),
                logical_mouse_y / (std::max)(0.0001f, selected_canvas_scale) };
            const DirectX::XMFLOAT2 normalized = CanvasToNormalized(
                *selected_rect, canvas_mouse);
            const DirectX::XMFLOAT2 current_uv{
                normalized.x, 1.0f - normalized.y };
            const DirectX::XMFLOAT2 start_canvas_mouse{
                (ui_effect_region_start_mouse.x - target.left) *
                    (logical_width / target_width) / (std::max)(0.0001f,
                        selected_canvas_scale),
                (logical_height - (ui_effect_region_start_mouse.y - target.top) *
                    (logical_height / target_height)) /
                    (std::max)(0.0001f, selected_canvas_scale) };
            const DirectX::XMFLOAT2 start_normalized = CanvasToNormalized(
                *selected_rect, start_canvas_mouse);
            const DirectX::XMFLOAT2 start_uv{
                start_normalized.x, 1.0f - start_normalized.y };
            ReplayEngine::UI::UIEffectRegionData* active_region = effect_region_at(
                ui_effect_region_index);
            if (active_region == nullptr) return;
            ReplayEngine::UI::UIEffectRegionData& region = *active_region;
            if (ui_effect_region_point >= 0 && ui_effect_region_handle >= 9 &&
                ui_effect_region_handle < 41 &&
                static_cast<std::size_t>(ui_effect_region_point) < region.path_points.size())
            {
                region.path_points[static_cast<std::size_t>(ui_effect_region_point)] = current_uv;
            }
            else if (ui_effect_region_handle == 8)
            {
                const float start_dx = start_uv.x - ui_effect_region_start_center.x;
                const float start_dy = start_uv.y - ui_effect_region_start_center.y;
                const float current_dx = current_uv.x - ui_effect_region_start_center.x;
                const float current_dy = current_uv.y - ui_effect_region_start_center.y;
                if (std::abs(start_dx) + std::abs(start_dy) > 0.001f &&
                    std::abs(current_dx) + std::abs(current_dy) > 0.001f)
                {
                    const float start_angle = std::atan2(start_dy, start_dx);
                    const float current_angle = std::atan2(current_dy, current_dx);
                    region.rotation = ui_effect_region_start_rotation +
                        (current_angle - start_angle) * 180.0f / DirectX::XM_PI;
                }
            }
            else if (ui_effect_region_handle >= 0 && ui_effect_region_handle < 8)
            {
                const float angle = ui_effect_region_start_rotation *
                    DirectX::XM_PI / 180.0f;
                const float s = std::sin(angle);
                const float c = std::cos(angle);
                const DirectX::XMFLOAT2 delta{
                    current_uv.x - ui_effect_region_start_center.x,
                    current_uv.y - ui_effect_region_start_center.y };
                const DirectX::XMFLOAT2 local{
                    delta.x * c + delta.y * s,
                    -delta.x * s + delta.y * c };
                const int sign_x[8]{ -1, 1, 1, -1, -1, 1, 0, 0 };
                const int sign_y[8]{ -1, -1, 1, 1, 0, 0, 1, -1 };
                const int sx = sign_x[ui_effect_region_handle];
                const int sy = sign_y[ui_effect_region_handle];
                DirectX::XMFLOAT2 center_local{ 0.0f, 0.0f };
                DirectX::XMFLOAT2 size = ui_effect_region_start_size;
                if (sx != 0)
                {
                    const float anchor = -static_cast<float>(sx) *
                        ui_effect_region_start_size.x;
                    center_local.x = (local.x + anchor) * 0.5f;
                    size.x = (std::max)(0.001f, std::abs(local.x - anchor) * 0.5f);
                }
                if (sy != 0)
                {
                    const float anchor = -static_cast<float>(sy) *
                        ui_effect_region_start_size.y;
                    center_local.y = (local.y + anchor) * 0.5f;
                    size.y = (std::max)(0.001f, std::abs(local.y - anchor) * 0.5f);
                }
                region.center = {
                    ui_effect_region_start_center.x + center_local.x * c -
                        center_local.y * s,
                    ui_effect_region_start_center.y + center_local.x * s +
                        center_local.y * c };
                region.size = size;
            }
        }
    }
    if (ui_effect_region_candidate)
    {
        ui_scene_view_input_consumed = true;
        viewport_drag_selecting = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (ui_effect_region_editing) object_editor_context.CommitEdit();
            ui_effect_region_candidate = false;
            ui_effect_region_editing = false;
            ui_effect_region_index = 0;
            ui_effect_region_handle = -1;
            ui_effect_region_point = -1;
            ui_effect_region_object = Core::ObjectID::Invalid();
        }
    }

    const bool resizing_candidate = selected_rect != nullptr &&
        selected_transform_target != nullptr && ui_preview_resize_candidate &&
        ui_preview_resize_object == selected_transform_target->ID();
    if (resizing_candidate && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
    {
        ui_scene_view_input_consumed = true;
        viewport_drag_selecting = false;
        if (!ui_preview_resizing && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("UI 要素をリサイズ");
            ui_preview_resizing = true;
        }
        if (ui_preview_resizing)
        {
            const DirectX::XMFLOAT2 canvas_mouse{
                logical_mouse_x / (std::max)(0.0001f, selected_canvas_scale),
                logical_mouse_y / (std::max)(0.0001f, selected_canvas_scale) };
            DirectX::XMFLOAT2 local_mouse{};
            if (!InverseTransformPoint(ui_preview_resize_start_matrix,
                canvas_mouse, local_mouse))
            {
                object_editor_context.CancelEdit();
                ui_preview_resizing = false;
                ui_preview_resize_candidate = false;
                ui_preview_resize_handle = -1;
                ui_preview_resize_object = Core::ObjectID::Invalid();
            }
            else
            {
                const ImGuiIO& io = ImGui::GetIO();
                const DirectX::XMFLOAT4 desired = ResizedRectFromHandle(
                    ui_preview_resize_start_rect, ui_preview_resize_handle,
                    local_mouse, io.KeyShift, io.KeyAlt);
                const DirectX::XMFLOAT4 before = selected_rect->ResolvedRect();
                ApplyResolvedRect(*selected_rect, ui_preview_resize_parent_rect, desired);
                if (IsUIShapeMaskObject(selected_transform_target))
                {
                    // Scene上の枠操作は図形イメージ全体のリサイズとして扱う。
                    // その後、子Imageを選べば表示位置・サイズを個別に調整できる。
                    const DirectX::XMFLOAT2 ratio{
                        before.z > 0.01f ? desired.z / before.z : 1.0f,
                        before.w > 0.01f ? desired.w / before.w : 1.0f };
                    ScaleUIShapeImageDescendants(*selected_transform_target, ratio);
                }
                ReplayEngine::UI::UILayout::Resolve(*scene,
                    logical_width, logical_height);
            }
        }
    }

    if (ui_preview_resize_candidate)
    {
        ui_scene_view_input_consumed = true;
        viewport_drag_selecting = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (ui_preview_resizing) object_editor_context.CommitEdit();
            ui_preview_resize_candidate = false;
            ui_preview_resizing = false;
            ui_preview_resize_handle = -1;
            ui_preview_resize_object = Core::ObjectID::Invalid();
        }
    }

    const bool dragging_candidate = selected_rect != nullptr &&
        selected_transform_target != nullptr &&
        !ui_preview_resize_candidate &&
        !ui_effect_region_candidate &&
        ui_preview_drag_object == selected_transform_target->ID();

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
            const ImVec2 delta(mouse.x - ui_preview_drag_start_mouse.x,
                mouse.y - ui_preview_drag_start_mouse.y);
            const float logical_delta_x =
                delta.x * (logical_width / target_width) / selected_canvas_scale;
            const float logical_delta_y =
                delta.y * (logical_height / target_height) / selected_canvas_scale;
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

    if (selected != nullptr && selected_transform_target != nullptr &&
        selected_rect != nullptr && HasUIComponent(*selected_transform_target))
    {
        const float canvas_scale = selected_canvas_scale;

        ImVec2 p[4]{};
        SceneUIQuad(*selected_rect, canvas_scale,
            target.left, target.top, target_width, target_height,
            logical_width, logical_height, p);
        draw_list->AddQuad(p[0], p[1], p[2], p[3],
            IM_COL32(255, 210, 80, 255), 2.0f);

        // Effect Stack の適用範囲。黄色の選択枠とは別色にして、
        // 「UI要素の変形」と「エフェクトの適用範囲」を見間違えないようにする。
        for (int region_index = 0; region_index < effect_region_count(); ++region_index)
        {
            if (!effect_region_is_visible(region_index)) continue;
            rebuild_effect_region_handles();
            const ReplayEngine::UI::UIEffectRegionData& region =
                *effect_region_at(region_index);
            const ImU32 range_color = region.shape == 2
                ? IM_COL32(120, 220, 255, 235) : IM_COL32(255, 120, 220, 235);
            if (region.shape == static_cast<int>(ReplayEngine::UI::UIEffectRegionShape::Freeform))
            {
                const std::size_t count = (std::min)(region.path_points.size(),
                    static_cast<std::size_t>(32));
                if (count >= 2)
                {
                    const std::size_t segment_count = region.path_closed ? count : count - 1;
                    for (std::size_t index = 0; index < segment_count; ++index)
                    {
                        const std::size_t next = (index + 1) % count;
                        draw_list->AddLine(
                            effect_region_uv_to_scene(region.path_points[index]),
                            effect_region_uv_to_scene(region.path_points[next]),
                            range_color, 2.0f);
                    }
                }
                for (std::size_t index = 0; index < count; ++index)
                {
                    const bool active = (ui_effect_region_candidate &&
                        ui_effect_region_object == selected_transform_target->ID() &&
                        ui_effect_region_index == region_index &&
                        ui_effect_region_point == static_cast<int>(index)) ||
                        (ui_effect_region_selected_object == selected_transform_target->ID() &&
                            ui_effect_region_selected_index == region_index &&
                            ui_effect_region_selected_point == static_cast<int>(index));
                    const bool hot = hovered_effect_region_index == region_index &&
                        hovered_effect_region_point == static_cast<int>(index);
                    const ImVec2 point = effect_region_uv_to_scene(region.path_points[index]);
                    draw_list->AddCircleFilled(point, active || hot ? 7.0f : 5.5f,
                        active ? IM_COL32(255, 245, 120, 255) : range_color, 12);
                    draw_list->AddCircle(point, active || hot ? 7.0f : 5.5f,
                        IM_COL32(35, 38, 44, 255), 12, 1.0f);
                }
                if (count > 0)
                {
                    const ImVec2 label_point = effect_region_uv_to_scene(region.path_points[0]);
                    const std::string range_label = "EFFECT " +
                        std::to_string(region_index + 1) + " / FREEFORM";
                    draw_list->AddText(ImVec2(label_point.x + 7.0f, label_point.y + 7.0f),
                        range_color, range_label.c_str());
                }
            }
            else if (region.shape == 1)
            {
                const float half_x = (std::max)(0.001f, region.size.x);
                const float half_y = (std::max)(0.001f, region.size.y);
                const float angle = region.rotation * DirectX::XM_PI / 180.0f;
                const float s = std::sin(angle);
                const float c = std::cos(angle);
                const auto point_from_local = [&](float x, float y)
                {
                    const DirectX::XMFLOAT2 uv{
                        region.center.x + x * c - y * s,
                        region.center.y + x * s + y * c };
                    const DirectX::XMFLOAT2 normalized{ uv.x, 1.0f - uv.y };
                    return ToSceneUIPoint(NormalizedToCanvas(*selected_rect, normalized),
                        canvas_scale, target.left, target.top, target_width,
                        target_height, logical_width, logical_height);
                };
                constexpr int segments = 64;
                ImVec2 previous = point_from_local(half_x, 0.0f);
                for (int segment = 1; segment <= segments; ++segment)
                {
                    const float t = DirectX::XM_2PI * segment /
                        static_cast<float>(segments);
                    const ImVec2 current = point_from_local(
                        std::cos(t) * half_x, std::sin(t) * half_y);
                    draw_list->AddLine(previous, current, range_color, 2.0f);
                    previous = current;
                }
            }
            else if (region.shape != static_cast<int>(ReplayEngine::UI::UIEffectRegionShape::Freeform))
            {
                draw_list->AddQuad(effect_region_outline[region_index][0],
                    effect_region_outline[region_index][1],
                    effect_region_outline[region_index][2],
                    effect_region_outline[region_index][3], range_color, 2.0f);
            }
            if (region.shape != static_cast<int>(ReplayEngine::UI::UIEffectRegionShape::Freeform))
            {
                draw_list->AddLine(effect_region_handle_points[region_index][7],
                    effect_region_handle_points[region_index][8], range_color, 1.5f);
                for (int index = 0; index < 9; ++index)
                {
                    const bool active = ui_effect_region_candidate &&
                        ui_effect_region_object == selected_transform_target->ID() &&
                        ui_effect_region_index == region_index &&
                        ui_effect_region_handle == index;
                    const bool hot = hovered_effect_region_index == region_index &&
                        hovered_effect_region_handle == index;
                    const ImU32 fill = active
                        ? IM_COL32(255, 245, 120, 255)
                        : (hot ? IM_COL32(240, 255, 190, 255) : range_color);
                    if (index == 8)
                        draw_list->AddCircleFilled(effect_region_handle_points[region_index][index],
                            6.0f, fill, 12);
                    else
                        draw_list->AddCircleFilled(effect_region_handle_points[region_index][index],
                            5.0f, fill, 12);
                    draw_list->AddCircle(effect_region_handle_points[region_index][index],
                        index == 8 ? 6.0f : 5.0f, IM_COL32(35, 38, 44, 255), 12, 1.0f);
                }
            }
            if (region.shape != static_cast<int>(ReplayEngine::UI::UIEffectRegionShape::Freeform))
            {
                const char* shape_label = region.shape == 1 ? "ELLIPSE" :
                    (region.shape == 2 ? "MASK" : "RECT");
                const std::string range_label = "EFFECT " +
                    std::to_string(region_index + 1) + " / " + shape_label;
                draw_list->AddText(ImVec2(effect_region_outline[region_index][0].x + 7.0f,
                    effect_region_outline[region_index][0].y + 7.0f), range_color,
                    range_label.c_str());
            }
        }

        const auto norm_to_scene = [&](const DirectX::XMFLOAT2& normalized)
        {
            return ToSceneUIPoint(NormalizedToCanvas(*selected_rect, normalized),
                canvas_scale, target.left, target.top, target_width, target_height,
                logical_width, logical_height);
        };
        if (const UIPuppetDeformComponent* puppet =
            selected->GetComponent<UIPuppetDeformComponent>())
        {
            // Mesh grid preview. 実際の頂点変形と同じ DeformNormalizedPoint を使う。
            const int cols = (std::max)(1, puppet->grid_columns);
            const int rows = (std::max)(1, puppet->grid_rows);
            for (int x = 0; x <= cols; ++x)
            {
                ImVec2 previous{};
                for (int y = 0; y <= rows; ++y)
                {
                    const auto n = puppet->DeformNormalizedPoint({
                        x / static_cast<float>(cols), y / static_cast<float>(rows) });
                    const ImVec2 point = norm_to_scene(n);
                    if (y > 0) draw_list->AddLine(previous, point, IM_COL32(80,190,255,80));
                    previous = point;
                }
            }
            for (int y = 0; y <= rows; ++y)
            {
                ImVec2 previous{};
                for (int x = 0; x <= cols; ++x)
                {
                    const auto n = puppet->DeformNormalizedPoint({
                        x / static_cast<float>(cols), y / static_cast<float>(rows) });
                    const ImVec2 point = norm_to_scene(n);
                    if (x > 0) draw_list->AddLine(previous, point, IM_COL32(80,190,255,80));
                    previous = point;
                }
            }
            for (int index = 0; index < puppet->PinCount(); ++index)
            {
                const std::size_t pin_index = static_cast<std::size_t>(index);
                const ImVec2 point = norm_to_scene(puppet->pin_positions[pin_index]);
                const ImVec2 bind = norm_to_scene(puppet->pin_bind_positions[pin_index]);
                const bool selected_pin = index == ui_puppet_selected_pin;
                if (pin_index < puppet->pin_radii.size())
                {
                    const float radius = (std::max)(0.0f, puppet->pin_radii[pin_index]);
                    constexpr int segments = 48;
                    ImVec2 previous{};
                    for (int segment = 0; segment <= segments; ++segment)
                    {
                        const float angle = DirectX::XM_2PI *
                            segment / static_cast<float>(segments);
                        const auto center = puppet->pin_bind_positions[pin_index];
                        const ImVec2 ring = norm_to_scene({
                            center.x + std::cos(angle) * radius,
                            center.y + std::sin(angle) * radius });
                        if (segment > 0) draw_list->AddLine(previous, ring,
                            selected_pin ? IM_COL32(255,205,80,120) :
                                IM_COL32(110,180,255,55), 1.0f);
                        previous = ring;
                    }
                }
                if (pin_index < puppet->pin_radii.size() &&
                    pin_index < puppet->pin_bind_positions.size())
                {
                    const auto center = puppet->pin_bind_positions[pin_index];
                    const float radius = (std::max)(0.001f, puppet->pin_radii[pin_index]);
                    const ImVec2 radius_handle = norm_to_scene({ center.x + radius, center.y });
                    draw_list->AddCircleFilled(radius_handle, selected_pin ? 5.5f : 4.0f,
                        selected_pin ? IM_COL32(255,235,120,255) :
                            IM_COL32(110,180,255,180));
                }
                draw_list->AddLine(bind, point, IM_COL32(255,180,60,180), 1.0f);
                draw_list->AddCircleFilled(point, selected_pin ? 7.0f : 5.5f,
                    selected_pin ? IM_COL32(255,225,90,255) : IM_COL32(255,180,60,255));
                draw_list->AddCircle(bind, 4.0f, IM_COL32(180,180,190,200), 0, 1.0f);
            }
        }
        Core::GameObject* shape_overlay_object = selected_transform_target;
        const auto draw_shape_image_controls = [&](const auto* shape)
        {
            if (shape == nullptr) return;
            if (shape->path_points.size() >= 2)
            {
                const std::size_t segment_count = shape->path_closed
                    ? shape->path_points.size() : shape->path_points.size() - 1;
                for (std::size_t segment = 0; segment < segment_count; ++segment)
                {
                    const std::size_t next = (segment + 1) % shape->path_points.size();
                    const DirectX::XMFLOAT2 p0 = shape->path_points[segment];
                    const DirectX::XMFLOAT2 p3 = shape->path_points[next];
                    const DirectX::XMFLOAT2 out = segment < shape->path_out_handles.size()
                        ? shape->path_out_handles[segment] : DirectX::XMFLOAT2{};
                    const DirectX::XMFLOAT2 in = next < shape->path_in_handles.size()
                        ? shape->path_in_handles[next] : DirectX::XMFLOAT2{};
                    const DirectX::XMFLOAT2 p1{ p0.x + out.x, p0.y + out.y };
                    const DirectX::XMFLOAT2 p2{ p3.x + in.x, p3.y + in.y };
                    ImVec2 previous = norm_to_scene(p0);
                    for (int step = 1; step <= 32; ++step)
                    {
                        const float t = step / 32.0f;
                        const float u = 1.0f - t;
                        const DirectX::XMFLOAT2 point{
                            u*u*u*p0.x + 3.0f*u*u*t*p1.x +
                                3.0f*u*t*t*p2.x + t*t*t*p3.x,
                            u*u*u*p0.y + 3.0f*u*u*t*p1.y +
                                3.0f*u*t*t*p2.y + t*t*t*p3.y };
                        const ImVec2 current = norm_to_scene(point);
                        draw_list->AddLine(previous, current, IM_COL32(255,120,210,210), 2.0f);
                        previous = current;
                    }
                }
            }
            for (std::size_t index = 0; index < shape->path_points.size(); ++index)
            {
                const auto a = shape->path_points[index];
                const auto in = index < shape->path_in_handles.size()
                    ? shape->path_in_handles[index] : DirectX::XMFLOAT2{};
                const auto out = index < shape->path_out_handles.size()
                    ? shape->path_out_handles[index] : DirectX::XMFLOAT2{};
                const ImVec2 pa = norm_to_scene(a);
                const ImVec2 pi = norm_to_scene({ a.x + in.x, a.y + in.y });
                const ImVec2 po = norm_to_scene({ a.x + out.x, a.y + out.y });
                draw_list->AddLine(pa, pi, IM_COL32(180,120,255,180));
                draw_list->AddLine(pa, po, IM_COL32(180,120,255,180));
                const bool selected_point = static_cast<int>(index) == ui_shape_selected_point;
                draw_list->AddCircleFilled(pa, selected_point ? 7.0f : 5.0f,
                    selected_point ? IM_COL32(255,240,100,255) : IM_COL32(255,210,70,255));
                draw_list->AddCircleFilled(pi, 3.5f, IM_COL32(120,200,255,255));
                draw_list->AddCircleFilled(po, 3.5f, IM_COL32(255,120,210,255));
            }
        };
        if (const UIShapeComponent* shape = shape_overlay_object != nullptr
            ? shape_overlay_object->GetComponent<UIShapeComponent>() : nullptr;
            shape != nullptr && shape->shape == UIShapeComponent::CustomBezierPath)
            draw_shape_image_controls(shape);
        else if (const UIShapeImageComponent* shape = shape_overlay_object != nullptr
            ? shape_overlay_object->GetComponent<UIShapeImageComponent>() : nullptr)
            draw_shape_image_controls(shape);
        if (const UIMaskComponent* mask = selected->GetComponent<UIMaskComponent>();
            mask != nullptr && (mask->mask_mode == UIMaskComponent::ObjectAlpha ||
                mask->mask_mode == UIMaskComponent::ObjectLuma))
        {
            const ImVec2 source_center{
                (p[0].x + p[1].x + p[2].x + p[3].x) * 0.25f,
                (p[0].y + p[1].y + p[2].y + p[3].y) * 0.25f };
            const auto draw_matte_link = [&](const ReplayEngine::Reflection::ObjectReference& ref,
                int operation, bool primary)
            {
                if (!ref.IsAssigned()) return;
                Core::GameObject* matte_object = scene->FindGameObjectByID(ref.object);
                if (matte_object == nullptr) return;
                const RectTransformComponent* matte_rect =
                    matte_object->GetComponent<RectTransformComponent>();
                if (matte_rect == nullptr) return;
                float matte_scale = 1.0f;
                if (Core::GameObject* matte_canvas = CanvasForObject(matte_object))
                    if (const CanvasComponent* canvas = matte_canvas->GetComponent<CanvasComponent>())
                        matte_scale = SafeCanvasScale(*canvas, logical_width, logical_height);
                ImVec2 matte_quad[4]{};
                SceneUIQuad(*matte_rect, matte_scale, target.left, target.top, target_width,
                    target_height, logical_width, logical_height, matte_quad);
                const ImVec2 matte_center{
                    (matte_quad[0].x + matte_quad[1].x + matte_quad[2].x + matte_quad[3].x) * 0.25f,
                    (matte_quad[0].y + matte_quad[1].y + matte_quad[2].y + matte_quad[3].y) * 0.25f };
                ImU32 color = IM_COL32(100,220,255,210);
                const char* label = primary ? "PRIMARY" : "ADD";
                if (!primary && operation == UIMaskComponent::MatteSubtract)
                { color = IM_COL32(255,110,110,220); label = "SUB"; }
                else if (!primary && operation == UIMaskComponent::MatteIntersect)
                { color = IM_COL32(170,255,120,220); label = "INTERSECT"; }
                draw_list->AddLine(source_center, matte_center, color, 1.5f);
                draw_list->AddCircleFilled(matte_center, 4.0f, color);
                draw_list->AddText(ImVec2((source_center.x + matte_center.x) * 0.5f + 4.0f,
                    (source_center.y + matte_center.y) * 0.5f + 2.0f), color, label);
            };
            draw_matte_link(mask->mask_object, UIMaskComponent::MatteAdd, true);
            for (std::size_t index = 0; index < mask->matte_objects.size(); ++index)
            {
                const int operation = index < mask->matte_operations.size()
                    ? mask->matte_operations[index] : UIMaskComponent::MatteAdd;
                draw_matte_link(mask->matte_objects[index], operation, false);
            }
        }

        // 自由図形は通常の拡大縮小ハンドルを表示せず、頂点・Bezierハンドルだけを
        // 専用コントローラーとして表示する。通常UIのRect Toolと混同しない。
        const int hot = !selected_custom_shape && target_hovered
            ? HitResizeBorder(p, mouse) : -1;
        if (!selected_custom_shape)
        {
            // 枠線より前面へ 4 corner + 4 edge handle を描く。
            // Shift は縦横比固定、Alt は中心基準のリサイズに使う。
            ImVec2 handles[8]{};
            ResizeHandlePoints(p, handles);
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
        }
        if (hot >= 0)
        {
            ImGui::SetMouseCursor((hot == ResizeLeft || hot == ResizeRight)
                ? ImGuiMouseCursor_ResizeEW
                : ((hot == ResizeTop || hot == ResizeBottom)
                    ? ImGuiMouseCursor_ResizeNS
                    : ImGuiMouseCursor_ResizeAll));
        }
        if (hovered_effect_region_index >= 0 &&
            effect_region_is_visible(hovered_effect_region_index) &&
            hovered_effect_region_handle >= 0)
        {
            ImGui::SetMouseCursor(hovered_effect_region_handle == 8
                ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_ResizeAll);
        }
    }

    draw_list->PopClipRect();
}
