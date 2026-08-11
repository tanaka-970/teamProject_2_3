#include "framework.h"

#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Components/Core/PivotComponent.h"
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

    DirectX::XMFLOAT3 TransformPoint(const DirectX::XMFLOAT3& point, DirectX::FXMMATRIX matrix)
    {
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(&result, DirectX::XMVector3TransformCoord(
            DirectX::XMLoadFloat3(&point), matrix));
        return result;
    }

    float DistanceSquared(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
    {
        const float x = a.x - b.x;
        const float y = a.y - b.y;
        const float z = a.z - b.z;
        return x * x + y * y + z * z;
    }

    DirectX::XMFLOAT3 ClosestPointOnSegment(const DirectX::XMFLOAT3& point,
        const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) noexcept
    {
        const DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&point);
        const DirectX::XMVECTOR av = DirectX::XMLoadFloat3(&a);
        const DirectX::XMVECTOR ab = DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&b), av);
        const float denominator = DirectX::XMVectorGetX(DirectX::XMVector3Dot(ab, ab));
        float amount = denominator > 0.0000001f
            ? DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(p, av), ab)) / denominator
            : 0.0f;
        amount = (std::max)(0.0f, (std::min)(amount, 1.0f));
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(&result, DirectX::XMVectorMultiplyAdd(
            DirectX::XMVectorReplicate(amount), ab, av));
        return result;
    }

    // Ericson の領域判定と同じ形で、三角形上の最短点を返す。
    // Surface Snap が「面の平面」ではなく実際の三角形内へ吸着するために必要。
    DirectX::XMFLOAT3 ClosestPointOnTriangle(const DirectX::XMFLOAT3& point,
        const ReplayEngine::Physics::Triangle& triangle) noexcept
    {
        using namespace DirectX;
        const XMVECTOR p = XMLoadFloat3(&point);
        const XMVECTOR a = XMLoadFloat3(&triangle.vertices[0]);
        const XMVECTOR b = XMLoadFloat3(&triangle.vertices[1]);
        const XMVECTOR c = XMLoadFloat3(&triangle.vertices[2]);
        const XMVECTOR ab = XMVectorSubtract(b, a);
        const XMVECTOR ac = XMVectorSubtract(c, a);
        const XMVECTOR ap = XMVectorSubtract(p, a);
        const float d1 = XMVectorGetX(XMVector3Dot(ab, ap));
        const float d2 = XMVectorGetX(XMVector3Dot(ac, ap));
        if (d1 <= 0.0f && d2 <= 0.0f) return triangle.vertices[0];

        const XMVECTOR bp = XMVectorSubtract(p, b);
        const float d3 = XMVectorGetX(XMVector3Dot(ab, bp));
        const float d4 = XMVectorGetX(XMVector3Dot(ac, bp));
        if (d3 >= 0.0f && d4 <= d3) return triangle.vertices[1];

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            const float v = d1 / (d1 - d3);
            XMFLOAT3 result;
            XMStoreFloat3(&result, XMVectorMultiplyAdd(XMVectorReplicate(v), ab, a));
            return result;
        }

        const XMVECTOR cp = XMVectorSubtract(p, c);
        const float d5 = XMVectorGetX(XMVector3Dot(ab, cp));
        const float d6 = XMVectorGetX(XMVector3Dot(ac, cp));
        if (d6 >= 0.0f && d5 <= d6) return triangle.vertices[2];

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            const float w = d2 / (d2 - d6);
            XMFLOAT3 result;
            XMStoreFloat3(&result, XMVectorMultiplyAdd(XMVectorReplicate(w), ac, a));
            return result;
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            XMFLOAT3 result;
            XMStoreFloat3(&result, XMVectorMultiplyAdd(
                XMVectorReplicate(w), XMVectorSubtract(c, b), b));
            return result;
        }

        const float denominator = 1.0f / (va + vb + vc);
        const float v = vb * denominator;
        const float w = vc * denominator;
        XMFLOAT3 result;
        XMStoreFloat3(&result, XMVectorAdd(a, XMVectorAdd(
            XMVectorScale(ab, v), XMVectorScale(ac, w))));
        return result;
    }

    const ReplayEngine::Physics::CookedMeshCollisionData* ObjectCookedMesh(
        ReplayEngine::Core::GameObject& object) noexcept
    {
        if (auto* mesh = object.GetComponent<ReplayEngine::Components::MeshColliderComponent>())
        {
            if (mesh->Cooked() && mesh->Cooked()->Valid()) return mesh->Cooked().get();
        }
        if (auto* landscape = object.GetComponent<ReplayEngine::Components::LandscapeColliderComponent>())
        {
            if (landscape->Cooked() && landscape->Cooked()->Valid()) return landscape->Cooked().get();
        }
        return nullptr;
    }
}

DirectX::XMFLOAT3 framework::resolve_object_pivot_world(ReplayEngine::Core::GameObject& object,
    ReplayEngine::Scene::Scene& scene) const
{
    using ReplayEngine::Components::PivotComponent;
    const DirectX::XMFLOAT3 origin = object.GetTransform().WorldPosition();
    const PivotComponent* pivot = object.GetComponent<PivotComponent>();
    if (pivot == nullptr || pivot->ModeValue() == PivotComponent::Mode::SelfOrigin) return origin;

    if (pivot->ModeValue() == PivotComponent::Mode::WorldPoint) return pivot->local_point;
    if (pivot->ModeValue() == PivotComponent::Mode::TargetObject)
    {
        if (pivot->target.IsAssigned())
        {
            if (ReplayEngine::Core::GameObject* target = scene.FindGameObjectByID(pivot->target.object))
                return target->GetTransform().WorldPosition();
        }
        return origin;
    }

    DirectX::XMFLOAT3 local = pivot->local_point;
    if (pivot->ModeValue() == PivotComponent::Mode::BoundsCenter ||
        pivot->ModeValue() == PivotComponent::Mode::BoundsFace)
    {
        const ReplayEngine::Physics::CookedMeshCollisionData* cooked = ObjectCookedMesh(object);
        if (cooked == nullptr) return origin;
        const DirectX::XMFLOAT3 minimum = cooked->LocalBoundsMin();
        const DirectX::XMFLOAT3 maximum = cooked->LocalBoundsMax();
        local = { (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f,
            (minimum.z + maximum.z) * 0.5f };
        if (pivot->ModeValue() == PivotComponent::Mode::BoundsFace)
        {
            const DirectX::XMFLOAT3 direction = pivot->local_point;
            const float ax = std::abs(direction.x);
            const float ay = std::abs(direction.y);
            const float az = std::abs(direction.z);
            if (ax >= ay && ax >= az) local.x = direction.x < 0.0f ? minimum.x : maximum.x;
            else if (ay >= az) local.y = direction.y < 0.0f ? minimum.y : maximum.y;
            else local.z = direction.z < 0.0f ? minimum.z : maximum.z;
        }
    }
    return TransformPoint(local, object.GetTransform().WorldMatrix());
}

bool framework::snap_primary_pivot_to_mesh(int mode)
{
    using ReplayEngine::Components::PivotComponent;
    ReplayEngine::Scene::Scene& scene = active_object_scene();
    ReplayEngine::Core::GameObject* object = object_editor_context.Selection().ResolvePrimary(scene);
    if (object == nullptr || !object_editor_context.CanEdit()) return false;
    PivotComponent* pivot = object->GetComponent<PivotComponent>();
    const ReplayEngine::Physics::CookedMeshCollisionData* cooked = ObjectCookedMesh(*object);
    if (pivot == nullptr || cooked == nullptr || cooked->TriangleCount() == 0) return false;

    DirectX::XMFLOAT3 query = pivot->local_point;
    if (pivot->ModeValue() == PivotComponent::Mode::WorldPoint)
    {
        DirectX::XMVECTOR determinant{};
        const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant,
            object->GetTransform().WorldMatrix());
        query = TransformPoint(pivot->local_point, inverse);
    }
    else if (pivot->ModeValue() == PivotComponent::Mode::SelfOrigin ||
        pivot->ModeValue() == PivotComponent::Mode::BoundsCenter ||
        pivot->ModeValue() == PivotComponent::Mode::TargetObject)
    {
        const DirectX::XMFLOAT3 minimum = cooked->LocalBoundsMin();
        const DirectX::XMFLOAT3 maximum = cooked->LocalBoundsMax();
        query = { (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f,
            (minimum.z + maximum.z) * 0.5f };
    }

    DirectX::XMFLOAT3 best{};
    float best_distance = (std::numeric_limits<float>::max)();
    for (std::size_t index = 0; index < cooked->TriangleCount(); ++index)
    {
        const ReplayEngine::Physics::Triangle& triangle = cooked->Triangles()[index];
        if (mode == 1)
        {
            for (const DirectX::XMFLOAT3& vertex : triangle.vertices)
            {
                const float distance = DistanceSquared(query, vertex);
                if (distance < best_distance) { best_distance = distance; best = vertex; }
            }
        }
        else if (mode == 2)
        {
            for (int edge = 0; edge < 3; ++edge)
            {
                const DirectX::XMFLOAT3 candidate = ClosestPointOnSegment(query,
                    triangle.vertices[edge], triangle.vertices[(edge + 1) % 3]);
                const float distance = DistanceSquared(query, candidate);
                if (distance < best_distance) { best_distance = distance; best = candidate; }
            }
        }
        else
        {
            const DirectX::XMFLOAT3 candidate = ClosestPointOnTriangle(query, triangle);
            const float distance = DistanceSquared(query, candidate);
            if (distance < best_distance) { best_distance = distance; best = candidate; }
        }
    }
    if (!std::isfinite(best_distance)) return false;

    object_editor_context.BeginEdit(u8"PivotをメッシュへSnap");
    if (pivot->ModeValue() == PivotComponent::Mode::WorldPoint)
        pivot->local_point = TransformPoint(best, object->GetTransform().WorldMatrix());
    else
    {
        pivot->SetMode(PivotComponent::Mode::CustomLocal);
        pivot->local_point = best;
    }
    object_editor_context.CommitEdit();
    object_editor_context.SetStatus(mode == 1 ? u8"Pivotを頂点へSnapしました" :
        mode == 2 ? u8"Pivotを辺へSnapしました" : u8"Pivotを面へSnapしました");
    return true;
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
