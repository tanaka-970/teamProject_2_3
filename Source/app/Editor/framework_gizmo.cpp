// Editor Gizmo のうち、Scene Grid と Transform Gizmo の描画・操作だけを持つ。
//
//   framework_gizmo.cpp        ... Grid と Transform Gizmo（このファイル）
//   framework_gizmo_pivot.cpp  ... Pivot 解決と Mesh Surface Snap

#include "framework.h"

#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Components/Core/PivotComponent.h"
#include "../../RePlayEngine/Components/Rendering/NormalAdjustComponent.h"
#include "../../RePlayEngine/Components/Physics/MeshColliderComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../RePlayEngine/Physics/CookedMeshCollision.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    bool ProjectPoint(const DirectX::XMFLOAT3& world, DirectX::FXMMATRIX view,
        DirectX::FXMMATRIX projection, float width, float height,
        const POINT& client_origin, ImVec2& screen)
    {
        if (width <= 0.0f || height <= 0.0f) return false;
        DirectX::XMFLOAT3 projected;
        DirectX::XMStoreFloat3(&projected, DirectX::XMVector3Project(
            DirectX::XMLoadFloat3(&world), 0.0f, 0.0f, width, height,
            0.0f, 1.0f, projection, view, DirectX::XMMatrixIdentity()));
        if (!std::isfinite(projected.x) || !std::isfinite(projected.y) ||
            projected.z < 0.0f || projected.z > 1.0f) return false;
        screen = ImVec2(projected.x + static_cast<float>(client_origin.x),
            projected.y + static_cast<float>(client_origin.y));
        return true;
    }

    float DistanceToSegment(const ImVec2& point, const ImVec2& first,
        const ImVec2& second) noexcept
    {
        const float dx = second.x - first.x;
        const float dy = second.y - first.y;
        const float length_squared = dx * dx + dy * dy;
        if (length_squared <= 0.0001f) return 100000.0f;
        float amount = ((point.x - first.x) * dx + (point.y - first.y) * dy) /
            length_squared;
        amount = (std::max)(0.0f, (std::min)(amount, 1.0f));
        const float px = point.x - (first.x + dx * amount);
        const float py = point.y - (first.y + dy * amount);
        return std::sqrt(px * px + py * py);
    }

    float SnapDelta(float value, bool enabled, float step) noexcept
    {
        if (!enabled || step <= 0.0f) return value;
        return std::round(value / step) * step;
    }

}

void framework::draw_scene_grid_overlay()
{
    if (!show_scene_grid || active_editor_view != editor_view::scene ||
        !show_scene_view || scene_grid_step <= 0.0f) return;

    POINT client_origin{ 0, 0 };
    ClientToScreen(hwnd, &client_origin);
    const DirectX::XMMATRIX view = viewport_view_matrix();
    const DirectX::XMMATRIX projection = viewport_projection_matrix();
    const DirectX::XMFLOAT3 eye = editor_camera.Position();
    const float step = (std::max)(scene_grid_step, 0.01f);
    const float center_x = std::floor(eye.x / step) * step;
    const float center_z = std::floor(eye.z / step) * step;
    constexpr int half_lines = 20;

    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    draw_list->PushClipRect(ImVec2(scene_view_min_x, scene_view_min_y),
        ImVec2(scene_view_max_x, scene_view_max_y), true);
    for (int index = -half_lines; index <= half_lines; ++index)
    {
        const float offset = static_cast<float>(index) * step;
        const DirectX::XMFLOAT3 x_first{ center_x - half_lines * step, 0.0f, center_z + offset };
        const DirectX::XMFLOAT3 x_second{ center_x + half_lines * step, 0.0f, center_z + offset };
        const DirectX::XMFLOAT3 z_first{ center_x + offset, 0.0f, center_z - half_lines * step };
        const DirectX::XMFLOAT3 z_second{ center_x + offset, 0.0f, center_z + half_lines * step };
        ImVec2 first;
        ImVec2 second;
        const ImU32 color = index == 0
            ? IM_COL32(105, 120, 140, 145) : IM_COL32(72, 82, 96, 90);
        if (ProjectPoint(x_first, view, projection, static_cast<float>(client_width),
            static_cast<float>(client_height), client_origin, first) &&
            ProjectPoint(x_second, view, projection, static_cast<float>(client_width),
                static_cast<float>(client_height), client_origin, second))
            draw_list->AddLine(first, second, color, index == 0 ? 1.5f : 1.0f);
        if (ProjectPoint(z_first, view, projection, static_cast<float>(client_width),
            static_cast<float>(client_height), client_origin, first) &&
            ProjectPoint(z_second, view, projection, static_cast<float>(client_width),
                static_cast<float>(client_height), client_origin, second))
            draw_list->AddLine(first, second, color, index == 0 ? 1.5f : 1.0f);
    }
    draw_list->PopClipRect();
}

bool framework::draw_object_transform_gizmo()
{
    if (active_editor_view != editor_view::scene || !show_scene_view) return false;

    ReplayEngine::Scene::Scene& scene = active_object_scene();
    ReplayEngine::Core::GameObject* primary =
        object_editor_context.Selection().ResolvePrimary(scene);
    if (primary == nullptr) return false;

    POINT client_origin{ 0, 0 };
    ClientToScreen(hwnd, &client_origin);
    const DirectX::XMMATRIX view = viewport_view_matrix();
    const DirectX::XMMATRIX projection = viewport_projection_matrix();
    const ReplayEngine::Editor::GizmoOperation operation = transform_gizmo.Operation();
    // Pivot は移動そのものには使わない。回転・拡縮の中心だけを差し替える。
    const DirectX::XMFLOAT3 center_world = operation == ReplayEngine::Editor::GizmoOperation::Translate
        ? primary->GetTransform().WorldPosition() : resolve_object_pivot_world(*primary, scene);
    ImVec2 center_screen;
    if (!ProjectPoint(center_world, view, projection, static_cast<float>(client_width),
        static_cast<float>(client_height), client_origin, center_screen)) return object_gizmo_dragging;

    DirectX::XMFLOAT3 axes[3] = {
        { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }
    };
    if (gizmo_local_space)
    {
        const DirectX::XMMATRIX world = primary->GetTransform().WorldMatrix();
        for (int axis = 0; axis < 3; ++axis)
        {
            DirectX::XMVECTOR transformed = DirectX::XMVector3TransformNormal(
                DirectX::XMLoadFloat3(&axes[axis]), world);
            const float length = DirectX::XMVectorGetX(DirectX::XMVector3Length(transformed));
            if (std::isfinite(length) && length > 0.0001f)
                DirectX::XMStoreFloat3(&axes[axis], DirectX::XMVectorScale(transformed, 1.0f / length));
        }
    }

    const DirectX::XMFLOAT3 eye = viewport_eye_position();
    const float dx = center_world.x - eye.x;
    const float dy = center_world.y - eye.y;
    const float dz = center_world.z - eye.z;
    const float camera_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float handle_length = (std::max)(0.5f, camera_distance * 0.10f);

    ImVec2 endpoints[3];
    bool endpoint_valid[3]{};
    float pixel_lengths[3]{};
    for (int axis = 0; axis < 3; ++axis)
    {
        const DirectX::XMFLOAT3 endpoint_world{
            center_world.x + axes[axis].x * handle_length,
            center_world.y + axes[axis].y * handle_length,
            center_world.z + axes[axis].z * handle_length
        };
        endpoint_valid[axis] = ProjectPoint(endpoint_world, view, projection,
            static_cast<float>(client_width), static_cast<float>(client_height),
            client_origin, endpoints[axis]);
        if (endpoint_valid[axis])
        {
            const float sx = endpoints[axis].x - center_screen.x;
            const float sy = endpoints[axis].y - center_screen.y;
            pixel_lengths[axis] = std::sqrt(sx * sx + sy * sy);
        }
    }

    // ---- モードごとの形 ---------------------------------------------------
    //
    // 動作は Operation で分岐していたが、見た目は 3 モードとも
    // 「線 + 先端の丸」で同じだった。今どのモードなのかが画面から分からない。
    //   移動 … 線 + 先端の丸（従来のまま）
    //   回転 … 各軸に垂直な円
    //   拡縮 … 線 + 先端の四角
    //
    // 掴める場所は必ず描いた形と一致させる。回転は円を折れ線に落とし、
    // その折れ線をそのまま当たり判定に使うので、見えている線の上でだけ掴める。
    const bool rotate_mode = operation == ReplayEngine::Editor::GizmoOperation::Rotate;

    constexpr int ring_sample_count = 48;
    ImVec2 ring_points[3][ring_sample_count + 1]{};
    bool ring_point_valid[3][ring_sample_count + 1]{};
    if (rotate_mode)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            // その軸に垂直な平面を張る 2 本。残り 2 軸をそのまま使う。
            const DirectX::XMFLOAT3& plane_u = axes[(axis + 1) % 3];
            const DirectX::XMFLOAT3& plane_v = axes[(axis + 2) % 3];
            for (int step = 0; step <= ring_sample_count; ++step)
            {
                const float angle = DirectX::XM_2PI * static_cast<float>(step) /
                    static_cast<float>(ring_sample_count);
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                const DirectX::XMFLOAT3 point_world{
                    center_world.x + (plane_u.x * cosine + plane_v.x * sine) * handle_length,
                    center_world.y + (plane_u.y * cosine + plane_v.y * sine) * handle_length,
                    center_world.z + (plane_u.z * cosine + plane_v.z * sine) * handle_length
                };
                ring_point_valid[axis][step] = ProjectPoint(point_world, view, projection,
                    static_cast<float>(client_width), static_cast<float>(client_height),
                    client_origin, ring_points[axis][step]);
            }
        }
    }

    const ImVec2 mouse = ImGui::GetMousePos();
    int hovered_axis = -1;
    int hovered_ring_step = -1;
    float nearest_handle = 9.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (rotate_mode)
        {
            for (int step = 0; step < ring_sample_count; ++step)
            {
                if (!ring_point_valid[axis][step] || !ring_point_valid[axis][step + 1]) continue;
                const float distance = DistanceToSegment(mouse,
                    ring_points[axis][step], ring_points[axis][step + 1]);
                if (distance < nearest_handle)
                {
                    nearest_handle = distance;
                    hovered_axis = axis;
                    hovered_ring_step = step;
                }
            }
            continue;
        }
        if (!endpoint_valid[axis]) continue;
        const float distance = DistanceToSegment(mouse, center_screen, endpoints[axis]);
        if (distance < nearest_handle)
        {
            nearest_handle = distance;
            hovered_axis = axis;
        }
    }

    static const ImU32 colors[3] = {
        IM_COL32(235, 75, 75, 255), IM_COL32(80, 220, 105, 255), IM_COL32(75, 135, 245, 255)
    };
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    draw_list->PushClipRect(ImVec2(scene_view_min_x, scene_view_min_y),
        ImVec2(scene_view_max_x, scene_view_max_y), true);
    for (int axis = 0; axis < 3; ++axis)
    {
        const bool highlighted = (object_gizmo_dragging && object_gizmo_axis == axis) ||
            (!object_gizmo_dragging && hovered_axis == axis);
        const ImU32 color = highlighted ? IM_COL32(255, 220, 80, 255) : colors[axis];

        if (rotate_mode)
        {
            // 回転は円。円周のどこを掴んでも、その軸まわりに回る。
            const float thickness = highlighted ? 4.0f : 2.5f;
            for (int step = 0; step < ring_sample_count; ++step)
            {
                if (!ring_point_valid[axis][step] || !ring_point_valid[axis][step + 1]) continue;
                draw_list->AddLine(ring_points[axis][step], ring_points[axis][step + 1],
                    color, thickness);
            }
            continue;
        }

        if (!endpoint_valid[axis]) continue;
        draw_list->AddLine(center_screen, endpoints[axis], color, highlighted ? 5.0f : 3.0f);
        if (operation == ReplayEngine::Editor::GizmoOperation::Scale)
        {
            // 拡縮は先端を四角にする。線の形は移動と同じなので、
            // 先端の形だけで «今どちらのモードか» が分かるようにする。
            const float half = highlighted ? 6.0f : 4.5f;
            draw_list->AddRectFilled(
                ImVec2(endpoints[axis].x - half, endpoints[axis].y - half),
                ImVec2(endpoints[axis].x + half, endpoints[axis].y + half), color);
        }
        else
        {
            draw_list->AddCircleFilled(endpoints[axis], highlighted ? 6.0f : 4.0f, color);
        }
    }
    draw_list->AddCircleFilled(center_screen, 4.0f, IM_COL32(235, 235, 240, 255));
    draw_list->PopClipRect();

    if (!object_gizmo_dragging && scene_view_hovered && hovered_axis >= 0 &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && object_editor_context.CanEdit())
    {
        object_gizmo_dragging = true;
        object_gizmo_axis = hovered_axis;
        object_gizmo_start_mouse_x = mouse.x;
        object_gizmo_start_mouse_y = mouse.y;
        object_gizmo_world_axis = axes[hovered_axis];
        if (rotate_mode && hovered_ring_step >= 0)
        {
            // 円に沿って引いたぶんだけ回るよう、掴んだ点の接線を基準にする。
            // 軸方向のままにすると、円を描いて見せているのに横へ引く操作になり、
            // 見た目と手の動きが噛み合わない。
            const ImVec2& from = ring_points[hovered_axis][hovered_ring_step];
            const ImVec2& to = ring_points[hovered_axis][hovered_ring_step + 1];
            const float tangent_x = to.x - from.x;
            const float tangent_y = to.y - from.y;
            const float tangent_length =
                (std::max)(std::sqrt(tangent_x * tangent_x + tangent_y * tangent_y), 0.0001f);
            object_gizmo_screen_axis_x = tangent_x / tangent_length;
            object_gizmo_screen_axis_y = tangent_y / tangent_length;
            // 回転は screen_delta をそのまま角度へ使うのでこの値は参照されない。
            object_gizmo_world_per_pixel = 1.0f;
        }
        else
        {
            const float pixel_length = (std::max)(pixel_lengths[hovered_axis], 1.0f);
            object_gizmo_screen_axis_x = (endpoints[hovered_axis].x - center_screen.x) / pixel_length;
            object_gizmo_screen_axis_y = (endpoints[hovered_axis].y - center_screen.y) / pixel_length;
            object_gizmo_world_per_pixel = handle_length / pixel_length;
        }
        object_gizmo_states.clear();
        for (const ReplayEngine::Core::ObjectID id : object_editor_context.Selection().All())
        {
            ReplayEngine::Core::GameObject* object = scene.FindGameObjectByID(id);
            if (object == nullptr || object->PendingDestroy()) continue;
            ObjectGizmoState state;
            state.id = id;
            state.world_position = object->GetTransform().WorldPosition();
            state.local_rotation = object->GetTransform().LocalRotationEuler();
            state.local_scale = object->GetTransform().LocalScale();
            state.pivot_world = resolve_object_pivot_world(*object, scene);
            object_gizmo_states.push_back(state);
        }
        const char* label = transform_gizmo.Operation() == ReplayEngine::Editor::GizmoOperation::Translate
            ? "Gizmoで移動" : transform_gizmo.Operation() == ReplayEngine::Editor::GizmoOperation::Rotate
            ? "Gizmoで回転" : "Gizmoで拡縮";
        object_editor_context.BeginEdit(label);
        return true;
    }

    if (!object_gizmo_dragging) return hovered_axis >= 0 && scene_view_hovered;

    if (ImGui::IsKeyPressed(VK_ESCAPE))
    {
        for (const ObjectGizmoState& state : object_gizmo_states)
        {
            if (ReplayEngine::Core::GameObject* object = scene.FindGameObjectByID(state.id))
            {
                object->GetTransform().SetWorldPosition(state.world_position);
                object->GetTransform().SetLocalRotationEuler(state.local_rotation);
                object->GetTransform().SetLocalScale(state.local_scale);
            }
        }
        object_editor_context.CancelEdit();
        object_gizmo_dragging = false;
        object_gizmo_axis = -1;
        object_editor_context.SetStatus("Gizmo操作を取り消しました");
        return true;
    }

    const float screen_delta =
        (mouse.x - object_gizmo_start_mouse_x) * object_gizmo_screen_axis_x +
        (mouse.y - object_gizmo_start_mouse_y) * object_gizmo_screen_axis_y;
    float delta = transform_gizmo.Operation() == ReplayEngine::Editor::GizmoOperation::Rotate
        ? screen_delta * 0.5f : screen_delta * object_gizmo_world_per_pixel;
    delta = SnapDelta(delta, transform_gizmo.SnapEnabled(), transform_gizmo.SnapStep());

    for (const ObjectGizmoState& state : object_gizmo_states)
    {
        ReplayEngine::Core::GameObject* object = scene.FindGameObjectByID(state.id);
        if (object == nullptr || object->PendingDestroy()) continue;
        if (transform_gizmo.Operation() == ReplayEngine::Editor::GizmoOperation::Translate)
        {
            object->GetTransform().SetWorldPosition({
                state.world_position.x + object_gizmo_world_axis.x * delta,
                state.world_position.y + object_gizmo_world_axis.y * delta,
                state.world_position.z + object_gizmo_world_axis.z * delta });
        }
        else if (transform_gizmo.Operation() == ReplayEngine::Editor::GizmoOperation::Rotate)
        {
            DirectX::XMFLOAT3 rotation = state.local_rotation;
            const float radians = DirectX::XMConvertToRadians(delta);
            if (object_gizmo_axis == 0) rotation.x += radians;
            if (object_gizmo_axis == 1) rotation.y += radians;
            if (object_gizmo_axis == 2) rotation.z += radians;
            object->GetTransform().SetLocalRotationEuler(rotation);

            // Orientation だけ変えると Custom/Target Pivot が見かけ上ずれる。
            // 開始時の原点を同じワールド軸で Pivot の周囲へ回して、Pivot 自体を固定する。
            const DirectX::XMVECTOR pivot = DirectX::XMLoadFloat3(&state.pivot_world);
            const DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&state.world_position);
            const DirectX::XMMATRIX rotation_matrix = DirectX::XMMatrixRotationAxis(
                DirectX::XMLoadFloat3(&object_gizmo_world_axis), radians);
            DirectX::XMFLOAT3 new_position;
            DirectX::XMStoreFloat3(&new_position, DirectX::XMVectorAdd(pivot,
                DirectX::XMVector3TransformNormal(DirectX::XMVectorSubtract(origin, pivot),
                    rotation_matrix)));
            object->GetTransform().SetWorldPosition(new_position);
        }
        else
        {
            DirectX::XMFLOAT3 scale = state.local_scale;
            float* component = object_gizmo_axis == 0 ? &scale.x :
                object_gizmo_axis == 1 ? &scale.y : &scale.z;
            const float original_component = *component;
            *component += delta;
            if (std::abs(*component) < 0.001f) *component = *component < 0.0f ? -0.001f : 0.001f;
            object->GetTransform().SetLocalScale(scale);

            // 1 軸拡縮の原点も Pivot を中心に同じ比率だけ動かす。
            // SelfOrigin なら relative=0 なので従来と同じ位置のままになる。
            const float safe_original = std::abs(original_component) < 0.001f
                ? (original_component < 0.0f ? -0.001f : 0.001f) : original_component;
            const float ratio = *component / safe_original;
            const DirectX::XMVECTOR axis = DirectX::XMLoadFloat3(&object_gizmo_world_axis);
            const DirectX::XMVECTOR pivot = DirectX::XMLoadFloat3(&state.pivot_world);
            const DirectX::XMVECTOR relative = DirectX::XMVectorSubtract(
                DirectX::XMLoadFloat3(&state.world_position), pivot);
            const float parallel_length = DirectX::XMVectorGetX(DirectX::XMVector3Dot(relative, axis));
            const DirectX::XMVECTOR moved = DirectX::XMVectorAdd(relative, DirectX::XMVectorScale(
                axis, parallel_length * (ratio - 1.0f)));
            DirectX::XMFLOAT3 new_position;
            DirectX::XMStoreFloat3(&new_position, DirectX::XMVectorAdd(pivot, moved));
            object->GetTransform().SetWorldPosition(new_position);
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        object_editor_context.CommitEdit();
        object_gizmo_dragging = false;
        object_gizmo_axis = -1;
        object_editor_context.SetStatus("Gizmo操作を確定しました");
    }
    return true;
}

bool framework::handle_normal_adjust_gizmo()
{
    using ReplayEngine::Components::NormalAdjustComponent;
    if (active_editor_view != editor_view::scene || !show_scene_view ||
        game_scene == nullptr || object_scene_play_mode || !object_editor_context.CanEdit())
        return false;
    ReplayEngine::Scene::Scene& scene = active_object_scene();
    ReplayEngine::Core::GameObject* object =
        object_editor_context.Selection().ResolvePrimary(scene);
    if (object == nullptr) return false;
    const std::vector<NormalAdjustComponent*> adjusts =
        object->GetComponents<NormalAdjustComponent>();
    if (adjusts.empty()) return false;
    POINT client_origin{ 0, 0 };
    ClientToScreen(hwnd, &client_origin);
    const DirectX::XMMATRIX view = viewport_view_matrix();
    const DirectX::XMMATRIX projection = viewport_projection_matrix();
    const auto resolved_center = [object](const NormalAdjustComponent& adjust,
        DirectX::XMFLOAT3& world, DirectX::XMFLOAT4X4& matrix)
    {
        matrix = adjust.resolved_center_matrix;
        if (adjust.resolved_center_valid)
        {
            world = adjust.resolved_center_world;
            return;
        }
        matrix = object->GetTransform().WorldMatrixFloat4x4();
        DirectX::XMStoreFloat3(&world, DirectX::XMVector3TransformCoord(
            DirectX::XMLoadFloat3(&adjust.center), DirectX::XMLoadFloat4x4(&matrix)));
    };
    const ImVec2 mouse = ImGui::GetMousePos();
    int hovered = -1;
    float nearest = 12.0f * 12.0f;
    ImVec2 screens[32]{};
    bool visible[32]{};
    const std::size_t count = (std::min)(adjusts.size(), static_cast<std::size_t>(32));
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    draw_list->PushClipRect(ImVec2(scene_view_min_x, scene_view_min_y),
        ImVec2(scene_view_max_x, scene_view_max_y), true);
    for (std::size_t index = 0; index < count; ++index)
    {
        DirectX::XMFLOAT3 center{};
        DirectX::XMFLOAT4X4 matrix{};
        resolved_center(*adjusts[index], center, matrix);
        visible[index] = ProjectPoint(center, view, projection, static_cast<float>(client_width),
            static_cast<float>(client_height), client_origin, screens[index]);
        if (!visible[index]) continue;
        const float dx = mouse.x - screens[index].x;
        const float dy = mouse.y - screens[index].y;
        const float distance = dx * dx + dy * dy;
        if (!normal_adjust_gizmo_dragging && distance < nearest)
        {
            nearest = distance;
            hovered = static_cast<int>(index);
        }
        const bool active = normal_adjust_gizmo_dragging &&
            normal_adjust_gizmo_component == adjusts[index]->StableID();
        const ImU32 color = active || static_cast<int>(index) == hovered
            ? IM_COL32(255, 235, 100, 255) : IM_COL32(80, 225, 240, 255);
        draw_list->AddCircleFilled(screens[index], active ? 7.0f : 5.0f, color, 12);
        draw_list->AddCircle(screens[index], active ? 9.0f : 7.0f,
            IM_COL32(20, 40, 45, 255), 12, 1.5f);
        draw_list->AddText({ screens[index].x + 9.0f, screens[index].y + 6.0f }, color,
            "Normal");
    }
    draw_list->PopClipRect();
    const bool inside_scene = scene_view_hovered && mouse.x >= scene_view_min_x &&
        mouse.x <= scene_view_max_x && mouse.y >= scene_view_min_y && mouse.y <= scene_view_max_y;
    if (!normal_adjust_gizmo_dragging && hovered >= 0 && inside_scene &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        NormalAdjustComponent* adjust = adjusts[static_cast<std::size_t>(hovered)];
        normal_adjust_gizmo_object = object->ID();
        normal_adjust_gizmo_component = adjust->StableID();
        normal_adjust_gizmo_start_center = adjust->center;
        resolved_center(*adjust, normal_adjust_gizmo_start_world,
            normal_adjust_gizmo_start_matrix);
        DirectX::XMFLOAT3 eye = viewport_eye_position();
        DirectX::XMVECTOR plane_normal = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(
            DirectX::XMLoadFloat3(&normal_adjust_gizmo_start_world), DirectX::XMLoadFloat3(&eye)));
        if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(plane_normal)) <= 1.0e-6f)
            plane_normal = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        DirectX::XMStoreFloat3(&normal_adjust_gizmo_plane_normal, plane_normal);
        normal_adjust_gizmo_dragging = true;
        viewport_drag_selecting = false;
        object_editor_context.BeginEdit("Normal Adjust の中心を移動");
        return true;
    }
    if (!normal_adjust_gizmo_dragging) return hovered >= 0 && inside_scene;
    ReplayEngine::Core::GameObject* drag_object = scene.FindGameObjectByID(normal_adjust_gizmo_object);
    auto* adjust = drag_object != nullptr ? dynamic_cast<NormalAdjustComponent*>(
        drag_object->FindComponentByStableID(normal_adjust_gizmo_component)) : nullptr;
    if (adjust == nullptr)
    {
        object_editor_context.CancelEdit();
        normal_adjust_gizmo_dragging = false;
        return true;
    }
    if (ImGui::IsKeyPressed(VK_ESCAPE))
    {
        adjust->center = normal_adjust_gizmo_start_center;
        adjust->OnPropertyChanged("center");
        object_editor_context.CancelEdit();
        normal_adjust_gizmo_dragging = false;
        return true;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const auto ray = viewport_picking_ray(mouse.x - scene_view_min_x,
            mouse.y - scene_view_min_y);
        const DirectX::XMVECTOR normal = DirectX::XMLoadFloat3(&normal_adjust_gizmo_plane_normal);
        const DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&ray.origin);
        const DirectX::XMVECTOR direction = DirectX::XMLoadFloat3(&ray.direction);
        const float denominator = DirectX::XMVectorGetX(DirectX::XMVector3Dot(direction, normal));
        if (std::abs(denominator) > 1.0e-6f)
        {
            const float distance = DirectX::XMVectorGetX(DirectX::XMVector3Dot(
                DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&normal_adjust_gizmo_start_world),
                    origin), normal)) / denominator;
            DirectX::XMVECTOR point = DirectX::XMVectorAdd(origin,
                DirectX::XMVectorScale(direction, distance));
            const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(nullptr,
                DirectX::XMLoadFloat4x4(&normal_adjust_gizmo_start_matrix));
            DirectX::XMStoreFloat3(&adjust->center,
                DirectX::XMVector3TransformCoord(point, inverse));
            adjust->OnPropertyChanged("center");
        }
        return true;
    }
    object_editor_context.CommitEdit();
    normal_adjust_gizmo_dragging = false;
    object_editor_context.SetStatus("Normal Adjust の中心を確定しました");
    return true;
}
