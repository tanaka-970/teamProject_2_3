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

#include "imgui/ImGuizmo.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    // ポーズ 1 本ぶんの補正行列。描画側の組み立てと同じ順序でないと差分が合わない。
    DirectX::XMMATRIX BonePoseAdjust(const DirectX::XMFLOAT3& translation,
        const DirectX::XMFLOAT3& rotation, const DirectX::XMFLOAT3& scale)
    {
        return DirectX::XMMatrixScaling(scale.x, scale.y, scale.z) *
            DirectX::XMMatrixRotationRollPitchYaw(
                DirectX::XMConvertToRadians(rotation.x),
                DirectX::XMConvertToRadians(rotation.y),
                DirectX::XMConvertToRadians(rotation.z)) *
            DirectX::XMMatrixTranslation(translation.x, translation.y, translation.z);
    }

    // XMMatrixRotationRollPitchYaw の逆。Z→X→Y の順に組まれた行列から角度を戻す。
    DirectX::XMFLOAT3 BoneEulerDegrees(DirectX::FXMMATRIX rotation)
    {
        DirectX::XMFLOAT4X4 m{};
        DirectX::XMStoreFloat4x4(&m, rotation);
        const float sin_pitch = std::clamp(-m.m[2][1], -1.0f, 1.0f);
        DirectX::XMFLOAT3 euler{ std::asin(sin_pitch), 0.0f, 0.0f };
        if (std::abs(sin_pitch) < 0.999999f)
        {
            euler.y = std::atan2(m.m[2][0], m.m[2][2]);
            euler.z = std::atan2(m.m[0][1], m.m[1][1]);
        }
        else
        {
            // ジンバル。Y と Z が縮退するので Z を捨てて Y へまとめる。
            euler.y = std::atan2(-m.m[0][2], m.m[0][0]);
        }
        return DirectX::XMFLOAT3{ DirectX::XMConvertToDegrees(euler.x),
            DirectX::XMConvertToDegrees(euler.y), DirectX::XMConvertToDegrees(euler.z) };
    }

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

// 選択中の GameObject のギズモ。ImGuizmo へ 1 つ渡し、返った行列の差分を選択全体へ配る。
// 回転・拡縮の中心は Pivot。掴んでいる間と Hover 中は true を返して選択へ渡さない。
bool framework::draw_object_transform_gizmo()
{
    if (active_editor_view != editor_view::scene || !show_scene_view) return false;

    ReplayEngine::Scene::Scene& scene = active_object_scene();
    ReplayEngine::Core::GameObject* primary =
        object_editor_context.Selection().ResolvePrimary(scene);
    if (primary == nullptr) return false;
    ImGuiWindow* scene_window = ImGui::FindWindowByName("Scene View");
    if (scene_window == nullptr) return false;

    using namespace DirectX;
    const ReplayEngine::Editor::GizmoOperation current = transform_gizmo.Operation();
    const ImGuizmo::OPERATION operation =
        current == ReplayEngine::Editor::GizmoOperation::Translate ? ImGuizmo::TRANSLATE
        : current == ReplayEngine::Editor::GizmoOperation::Rotate ? ImGuizmo::ROTATE
        : ImGuizmo::SCALE;

    // 掴んでいる間は前フレームの結果をそのまま渡す。作り直すと差分が二重に効く。
    XMFLOAT4X4 world = object_gizmo_matrix;
    if (!object_gizmo_dragging)
    {
        // Pivot は移動そのものには使わない。回転・拡縮の中心だけを差し替える。
        const XMFLOAT3 center = current == ReplayEngine::Editor::GizmoOperation::Translate
            ? primary->GetTransform().WorldPosition() : resolve_object_pivot_world(*primary, scene);
        XMMATRIX basis = XMMatrixIdentity();
        if (gizmo_local_space)
        {
            // 姿勢だけ借りる。大きさは 1 に潰してギズモの寸法へ響かせない。
            basis = primary->GetTransform().WorldMatrix();
            for (int axis = 0; axis < 3; ++axis)
            {
                const XMVECTOR row = XMVector3Normalize(basis.r[axis]);
                basis.r[axis] = XMVector3Equal(row, XMVectorZero())
                    ? XMVectorSetByIndex(XMVectorZero(), 1.0f, static_cast<size_t>(axis)) : row;
            }
        }
        basis.r[3] = XMVectorSet(center.x, center.y, center.z, 1.0f);
        XMStoreFloat4x4(&world, basis);
    }

    XMFLOAT4X4 view{}, projection{};
    XMStoreFloat4x4(&view, viewport_view_matrix());
    XMStoreFloat4x4(&projection, viewport_projection_matrix());
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGuizmo::SetDrawlist(scene_window->DrawList);
    ImGuizmo::SetRect(main_viewport->Pos.x, main_viewport->Pos.y,
        main_viewport->Size.x, main_viewport->Size.y);

    float snap[3]{};
    const float* snap_values = nullptr;
    if (transform_gizmo.SnapEnabled())
    {
        snap[0] = snap[1] = snap[2] = (std::max)(0.0001f, transform_gizmo.SnapStep());
        snap_values = snap;
    }
    const XMFLOAT4X4 before = world;
    ImGuizmo::SetID(gizmo_id_object);
    ImGuizmo::Manipulate(&view._11, &projection._11, operation,
        gizmo_local_space ? ImGuizmo::LOCAL : ImGuizmo::WORLD, &world._11, nullptr, snap_values);
    const bool using_now = ImGuizmo::IsUsingID(gizmo_id_object);

    if (using_now && !object_gizmo_dragging)
    {
        if (!object_editor_context.CanEdit()) return true;
        object_gizmo_dragging = true;
        object_gizmo_start_matrix = before;
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
            XMStoreFloat4x4(&state.world_matrix, object->GetTransform().WorldMatrix());
            object_gizmo_states.push_back(state);
        }
        object_editor_context.BeginEdit(
            current == ReplayEngine::Editor::GizmoOperation::Translate ? "Gizmoで移動"
            : current == ReplayEngine::Editor::GizmoOperation::Rotate ? "Gizmoで回転" : "Gizmoで拡縮");
    }

    if (!object_gizmo_dragging) return ImGuizmo::IsOver();

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
        // ImGuizmo 側の «掴んでいる» を落とす。放置すると次のクリックまで残る。
        ImGuizmo::Enable(false);
        ImGuizmo::Enable(true);
        object_editor_context.SetStatus("Gizmo操作を取り消しました");
        return true;
    }

    object_gizmo_matrix = world;
    // ギズモの座標系で見た移動量。開始行列の逆を挟むので Pivot が回転・拡縮の中心になる。
    const XMMATRIX delta = XMMatrixInverse(nullptr,
        XMLoadFloat4x4(&object_gizmo_start_matrix)) * XMLoadFloat4x4(&world);
    for (const ObjectGizmoState& state : object_gizmo_states)
    {
        ReplayEngine::Core::GameObject* object = scene.FindGameObjectByID(state.id);
        if (object == nullptr || object->PendingDestroy()) continue;
        object->GetTransform().SetFromWorldMatrix(XMLoadFloat4x4(&state.world_matrix) * delta);
    }

    if (!using_now)
    {
        object_editor_context.CommitEdit();
        object_gizmo_dragging = false;
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
    // クリックはハンドルを選ぶだけ。動かすのは下のギズモに任せる。
    if (!normal_adjust_gizmo_dragging && hovered >= 0 && inside_scene &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        normal_adjust_gizmo_object = object->ID();
        normal_adjust_gizmo_component = adjusts[static_cast<std::size_t>(hovered)]->StableID();
        viewport_drag_selecting = false;
        return true;
    }
    if (normal_adjust_gizmo_component == ReplayEngine::Core::invalid_component_stable_id) return hovered >= 0 && inside_scene;

    ReplayEngine::Core::GameObject* drag_object = scene.FindGameObjectByID(normal_adjust_gizmo_object);
    auto* adjust = drag_object != nullptr ? dynamic_cast<NormalAdjustComponent*>(
        drag_object->FindComponentByStableID(normal_adjust_gizmo_component)) : nullptr;
    if (adjust == nullptr)
    {
        if (normal_adjust_gizmo_dragging) object_editor_context.CancelEdit();
        normal_adjust_gizmo_dragging = false;
        normal_adjust_gizmo_component = ReplayEngine::Core::invalid_component_stable_id;
        return false;
    }

    ImGuiWindow* scene_window = ImGui::FindWindowByName("Scene View");
    if (scene_window == nullptr) return hovered >= 0 && inside_scene;
    DirectX::XMFLOAT3 center_world{};
    DirectX::XMFLOAT4X4 center_matrix{};
    resolved_center(*adjust, center_world, center_matrix);
    if (normal_adjust_gizmo_dragging)
    {
        // 掴んでいる間は解決済みの値を待たない。1 フレーム遅れると移動量が二重に効く。
        DirectX::XMStoreFloat3(&center_world, DirectX::XMVector3TransformCoord(
            DirectX::XMLoadFloat3(&adjust->center),
            DirectX::XMLoadFloat4x4(&normal_adjust_gizmo_start_matrix)));
    }

    DirectX::XMFLOAT4X4 gizmo_view{}, gizmo_projection{}, gizmo_world{};
    DirectX::XMStoreFloat4x4(&gizmo_view, view);
    DirectX::XMStoreFloat4x4(&gizmo_projection, projection);
    DirectX::XMStoreFloat4x4(&gizmo_world,
        DirectX::XMMatrixTranslation(center_world.x, center_world.y, center_world.z));
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGuizmo::SetDrawlist(scene_window->DrawList);
    ImGuizmo::SetRect(main_viewport->Pos.x, main_viewport->Pos.y,
        main_viewport->Size.x, main_viewport->Size.y);
    ImGuizmo::SetID(gizmo_id_normal_adjust);
    ImGuizmo::Manipulate(&gizmo_view._11, &gizmo_projection._11,
        ImGuizmo::TRANSLATE, ImGuizmo::WORLD, &gizmo_world._11);
    const bool using_now = ImGuizmo::IsUsingID(gizmo_id_normal_adjust);

    if (using_now && !normal_adjust_gizmo_dragging)
    {
        normal_adjust_gizmo_dragging = true;
        normal_adjust_gizmo_start_center = adjust->center;
        normal_adjust_gizmo_start_world = center_world;
        normal_adjust_gizmo_start_matrix = center_matrix;
        viewport_drag_selecting = false;
        object_editor_context.BeginEdit("Normal Adjust の中心を移動");
    }
    if (!normal_adjust_gizmo_dragging) return ImGuizmo::IsOver() || (hovered >= 0 && inside_scene);

    if (ImGui::IsKeyPressed(VK_ESCAPE))
    {
        adjust->center = normal_adjust_gizmo_start_center;
        adjust->OnPropertyChanged("center");
        object_editor_context.CancelEdit();
        normal_adjust_gizmo_dragging = false;
        ImGuizmo::Enable(false);
        ImGuizmo::Enable(true);
        return true;
    }

    // ギズモはワールドで返るので、開始時の行列で中心のローカルへ戻す。
    DirectX::XMStoreFloat3(&adjust->center, DirectX::XMVector3TransformCoord(
        DirectX::XMLoadFloat4x4(&gizmo_world).r[3], DirectX::XMMatrixInverse(nullptr,
            DirectX::XMLoadFloat4x4(&normal_adjust_gizmo_start_matrix))));
    adjust->OnPropertyChanged("center");

    if (!using_now)
    {
        object_editor_context.CommitEdit();
        normal_adjust_gizmo_dragging = false;
        object_editor_context.SetStatus("Normal Adjust の中心を確定しました");
    }
    return true;
}

// 選択中の骨のギズモ。ImGuizmo へワールド行列を渡し、返った行列から差分だけを取る。
// 掴んでいる間は true を返して選択へ渡さない。
bool framework::draw_bone_transform_gizmo()
{
    if (rig_selected_bone.empty()) return false;
    if (!show_rig_debug_draw && !show_motion_rig_panel) return false;

    const ReplayEngine::Core::GameObject* target =
        active_object_scene().FindGameObjectByID(object_editor_context.Selection().Primary());
    if (target == nullptr) return false;
    const std::uint64_t owner = target->ID().Value();
    const auto rig = object_rig_debug_bones.find(owner);
    if (rig == object_rig_debug_bones.end()) return false;
    const rig_debug_bone* bone = nullptr;
    for (const rig_debug_bone& candidate : rig->second)
        if (candidate.name == rig_selected_bone) { bone = &candidate; break; }
    if (bone == nullptr) return false;

    using namespace DirectX;
    XMFLOAT4X4 view{}, projection{};
    XMStoreFloat4x4(&view, viewport_view_matrix());
    XMStoreFloat4x4(&projection, viewport_projection_matrix());
    // 骨の行列は前フレームの描画提出で作ったもので、いまのポーズと対になっている。
    XMFLOAT4X4 world = bone->world_matrix;

    // ImGuizmo は描画リストの持ち主の窓が Hover されている間だけ操作を通す。
    // 背景リストを渡すと持ち主が引けず、Scene View の上で一切反応しなくなる。
    ImGuiWindow* scene_window = ImGui::FindWindowByName("Scene View");
    if (scene_window == nullptr) return false;
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGuizmo::SetDrawlist(scene_window->DrawList);
    ImGuizmo::SetRect(main_viewport->Pos.x, main_viewport->Pos.y,
        main_viewport->Size.x, main_viewport->Size.y);

    const ReplayEngine::Editor::GizmoOperation current = transform_gizmo.Operation();
    const ImGuizmo::OPERATION operation =
        current == ReplayEngine::Editor::GizmoOperation::Translate ? ImGuizmo::TRANSLATE
        : current == ReplayEngine::Editor::GizmoOperation::Rotate ? ImGuizmo::ROTATE
        : ImGuizmo::SCALE;
    ImGuizmo::SetID(gizmo_id_bone);
    ImGuizmo::Manipulate(&view._11, &projection._11, operation,
        rig_gizmo_use_local && gizmo_local_space ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
        &world._11);
    if (!ImGuizmo::IsUsingID(gizmo_id_bone)) return false;

    rig_pose_override& entry = object_rig_pose[owner][rig_selected_bone];
    // 骨のローカルと親は動いていないので、ワールドの差分がそのままポーズの差分になる。
    const XMMATRIX edited = XMLoadFloat4x4(&world) *
        XMMatrixInverse(nullptr, XMLoadFloat4x4(&bone->world_matrix)) *
        BonePoseAdjust(entry.translation, entry.rotation, entry.scale);
    XMVECTOR scale{}, rotation{}, translation{};
    if (XMMatrixDecompose(&scale, &rotation, &translation, edited))
    {
        // 行列ごと返るので分解が他の成分へ滲む。掴んでいる種類だけ書き戻す。
        if (operation == ImGuizmo::TRANSLATE) XMStoreFloat3(&entry.translation, translation);
        else if (operation == ImGuizmo::SCALE) XMStoreFloat3(&entry.scale, scale);
        else entry.rotation = BoneEulerDegrees(XMMatrixRotationQuaternion(rotation));
    }
    // IsOver() では骨が密集した場所でギズモに隠れた骨を選び直せなくなる。
    return true;
}
