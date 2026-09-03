// Collider と AI / Navigation の Scene View 可視化。
//
// 新しい描画パイプラインは作らず、既存の DebugLine -> ImGui overlay 経路へ
// AI の塗り・文字・編集ハンドルを同居させる。Runtime Component は ImGui に依存しない。

#include "framework.h"

#include "imgui/ImGuizmo.h"

#include <cstdio>

#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/EnemyBehaviourComponent.h"
#include "../../RePlayEngine/Components/Gameplay/StageGameplayComponents.h"
#include "../../RePlayEngine/Components/Navigation/NavAgentComponent.h"
#include "../../RePlayEngine/Components/Physics/ColliderComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/NormalAdjustComponent.h"
#include "../../RePlayEngine/Editor/Debug/AINavigationDebugDraw.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Physics/CollisionLayers.h"

#include <cmath>
#include <string>

#ifdef USE_IMGUI

namespace
{
    const char* SourceText(const ReplayEngine::Scene::CollisionSourceInfo& source)
    {
        return ReplayEngine::Scene::ToString(source.backend);
    }

    enum class AINavigationHandleKind
    {
        None,
        DetectionRange,
        AttackRange,
    };

    struct AINavigationHandleState
    {
        ReplayEngine::Core::ObjectID object;
        AINavigationHandleKind kind = AINavigationHandleKind::None;
        bool dragging = false;
    };

    AINavigationHandleState ai_navigation_handle_state;

    void ClearAINavigationHandleState()
    {
        ai_navigation_handle_state = {};
    }

}

void framework::draw_collider_debug_overlay()
{
    if (game_scene == nullptr) return;

    if (show_collider_debug_draw)
    {
        ReplayEngine::Editor::ColliderDebugDraw::Options options;
        options.draw_bounds = show_collider_debug_bounds;
        options.draw_shapes = true;
        options.draw_mesh_wireframe = show_collider_debug_wireframe;
        ReplayEngine::Editor::ColliderDebugDraw::Build(
            object_collision_world, options, object_collider_debug_lines);
    }
    else
    {
        object_collider_debug_lines.clear();
    }

    ReplayEngine::Editor::AINavigationDebugFrame ai_frame;
    bool has_stage_markers = false;
    if (active_editor_view == editor_view::scene)
    {
        const ReplayEngine::Core::ObjectID selected = object_editor_context.Selection().Primary();
        ReplayEngine::Editor::AINavigationDebugDraw::Build(active_object_scene(), selected, ai_frame);
        for (std::size_t index = 0; index < active_object_scene().GameObjectCount(); ++index)
        {
            const ReplayEngine::Core::GameObject* object =
                active_object_scene().GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy())
                continue;
            const auto* spawn = object->GetComponent<
                ReplayEngine::Components::SpawnPointComponent>();
            const auto* jump = object->GetComponent<
                ReplayEngine::Components::JumpPadComponent>();
            has_stage_markers |= (spawn != nullptr && spawn->ActiveInHierarchy() &&
                spawn->debug_draw) || (jump != nullptr && jump->ActiveInHierarchy() &&
                jump->debug_draw);
        }
    }

    const ReplayEngine::Core::GameObject* light_range_object = nullptr;
    const ReplayEngine::Core::GameObject* normal_adjust_object = nullptr;
    if (show_light_range_debug_draw && active_editor_view == editor_view::scene)
    {
        const ReplayEngine::Core::ObjectID selected =
            object_editor_context.Selection().Primary();
        const ReplayEngine::Core::GameObject* selected_object =
            active_object_scene().FindGameObjectByID(selected);
        if (selected_object != nullptr && !selected_object->PendingDestroy() &&
            selected_object->ActiveInHierarchy() &&
            (selected_object->GetComponent<
                ReplayEngine::Components::PointLightComponent>() != nullptr ||
                selected_object->GetComponent<
                    ReplayEngine::Components::SpotLightComponent>() != nullptr))
        {
            light_range_object = selected_object;
        }
    }

    if (show_normal_adjust_debug_draw && active_editor_view == editor_view::scene)
    {
        const ReplayEngine::Core::GameObject* selected_object =
            active_object_scene().FindGameObjectByID(object_editor_context.Selection().Primary());
        if (selected_object != nullptr && !selected_object->PendingDestroy() &&
            selected_object->ActiveInHierarchy() && selected_object->GetComponent<
                ReplayEngine::Components::NormalAdjustComponent>() != nullptr)
            normal_adjust_object = selected_object;
    }

    if (object_collider_debug_lines.empty() && ai_frame.lines.empty() &&
        ai_frame.fills.empty() && ai_frame.labels.empty() && !has_stage_markers &&
        light_range_object == nullptr && normal_adjust_object == nullptr &&
        !show_rig_debug_draw) return;

    const DirectX::XMMATRIX view_projection =
        viewport_view_matrix() * viewport_projection_matrix();
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    // Collider は既存の投影規約を維持する。
    const ImVec2 main_origin = ImGui::GetMainViewport()->Pos;
    const ImVec2 main_size = ImGui::GetMainViewport()->Size;
    for (const ReplayEngine::Editor::DebugLine& line : object_collider_debug_lines)
    {
        ImVec2 start{};
        ImVec2 end{};
        if (!project_world_to_screen(view_projection, line.start, main_origin, main_size, start)) continue;
        if (!project_world_to_screen(view_projection, line.end, main_origin, main_size, end)) continue;
        draw_list->AddLine(start, end, line.color, 1.0f);
    }

    // AI もクライアント矩形へ投影する。Scene View の矩形は切り抜きにだけ使う。
    //
    // 3D はクライアント領域全体へ描かれており、viewport_projection_matrix() も
    // クライアントのアスペクトで作られている。ここで Scene View の矩形へ投影すると
    // アスペクトが食い違い、画面端へ行くほど実際の絵とズレる。
    // カメラを回すと線が動いて見えたのはこれが原因。
    // Gizmo / Grid / Collider は前からクライアント矩形を使っている。
    const ImVec2 scene_origin{ scene_view_min_x, scene_view_min_y };
    const ImVec2 scene_size{
        (std::max)(1.0f, scene_view_max_x - scene_view_min_x),
        (std::max)(1.0f, scene_view_max_y - scene_view_min_y) };
    draw_list->PushClipRect(scene_origin,
        { scene_origin.x + scene_size.x, scene_origin.y + scene_size.y }, true);

    if (light_range_object != nullptr)
    {
        constexpr int light_circle_segments = 24;
        const auto draw_projected_line = [&](const DirectX::XMFLOAT3& start,
                                             const DirectX::XMFLOAT3& end, ImU32 color)
        {
            ImVec2 screen_start{};
            ImVec2 screen_end{};
            if (project_world_to_screen(view_projection, start, main_origin, main_size, screen_start) &&
                project_world_to_screen(view_projection, end, main_origin, main_size, screen_end))
                draw_list->AddLine(screen_start, screen_end, color, 1.5f);
        };
        const ReplayEngine::Core::GameObject* object = light_range_object;
        const DirectX::XMFLOAT3 center = object->GetTransform().WorldPosition();
        if (const auto* point = object->GetComponent<
            ReplayEngine::Components::PointLightComponent>();
            point != nullptr && point->Enabled())
        {
            const float radius = (std::max)(0.05f, point->range);
            DirectX::XMFLOAT3 previous{
                center.x + radius, center.y, center.z };
            for (int segment = 1; segment <= light_circle_segments; ++segment)
            {
                const float angle = 2.0f * DirectX::XM_PI *
                    static_cast<float>(segment) / static_cast<float>(light_circle_segments);
                const DirectX::XMFLOAT3 current{
                    center.x + std::cos(angle) * radius, center.y,
                    center.z + std::sin(angle) * radius };
                draw_projected_line(previous, current, IM_COL32(255, 220, 80, 220));
                previous = current;
            }
        }
        else if (const auto* spot = object->GetComponent<
            ReplayEngine::Components::SpotLightComponent>();
            spot != nullptr && spot->Enabled())
        {
            const float range = (std::max)(0.05f, spot->range);
            const DirectX::XMFLOAT4X4 world = object->GetTransform().WorldMatrixFloat4x4();
            DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(
                DirectX::XMVectorSet(world._31, world._32, world._33, 0.0f));
            DirectX::XMVECTOR reference_up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            if (std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(forward, reference_up))) > 0.95f)
                reference_up = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
            const DirectX::XMVECTOR right = DirectX::XMVector3Normalize(
                DirectX::XMVector3Cross(reference_up, forward));
            const DirectX::XMVECTOR up = DirectX::XMVector3Normalize(
                DirectX::XMVector3Cross(forward, right));
            const float half_angle = spot->outer_angle_degrees * DirectX::XM_PI / 360.0f;
            const float radius = (std::max)(0.05f, std::tan(half_angle) * range);
            const DirectX::XMVECTOR center_vector = DirectX::XMLoadFloat3(&center);
            const DirectX::XMVECTOR tip_center = DirectX::XMVectorAdd(center_vector,
                DirectX::XMVectorScale(forward, range));
            DirectX::XMFLOAT3 previous{};
            DirectX::XMStoreFloat3(&previous, DirectX::XMVectorAdd(tip_center,
                DirectX::XMVectorScale(right, radius)));
            for (int segment = 1; segment <= light_circle_segments; ++segment)
            {
                const float angle = 2.0f * DirectX::XM_PI *
                    static_cast<float>(segment) / static_cast<float>(light_circle_segments);
                const DirectX::XMVECTOR rim = DirectX::XMVectorAdd(
                    DirectX::XMVectorScale(right, std::cos(angle) * radius),
                    DirectX::XMVectorScale(up, std::sin(angle) * radius));
                DirectX::XMFLOAT3 current{};
                DirectX::XMStoreFloat3(&current, DirectX::XMVectorAdd(tip_center, rim));
                draw_projected_line(previous, current, IM_COL32(255, 150, 80, 220));
                draw_projected_line(center, current, IM_COL32(255, 150, 80, 180));
                previous = current;
            }
        }
    }

    if (normal_adjust_object != nullptr)
    {
        constexpr int sphere_segments = 32;
        const auto draw_projected_line = [&](const DirectX::XMFLOAT3& start,
                                             const DirectX::XMFLOAT3& end)
        {
            ImVec2 screen_start{};
            ImVec2 screen_end{};
            if (project_world_to_screen(view_projection, start, main_origin, main_size, screen_start) &&
                project_world_to_screen(view_projection, end, main_origin, main_size, screen_end))
                draw_list->AddLine(screen_start, screen_end, IM_COL32(80, 225, 240, 220), 1.5f);
        };
        for (const ReplayEngine::Components::NormalAdjustComponent* adjust :
            normal_adjust_object->GetComponents<
                ReplayEngine::Components::NormalAdjustComponent>())
        {
            if (!adjust->ActiveInHierarchy() || !adjust->resolved_center_valid ||
                adjust->resolved_radius_world <= 0.0f) continue;
            const DirectX::XMFLOAT3 center = adjust->resolved_center_world;
            const float radius = adjust->resolved_radius_world;
            for (int axis = 0; axis < 3; ++axis)
            {
                DirectX::XMFLOAT3 previous{};
                for (int segment = 0; segment <= sphere_segments; ++segment)
                {
                    const float angle = DirectX::XM_2PI * static_cast<float>(segment) /
                        static_cast<float>(sphere_segments);
                    DirectX::XMFLOAT3 current = center;
                    const float first = std::cos(angle) * radius;
                    const float second = std::sin(angle) * radius;
                    if (axis == 0) { current.y += first; current.z += second; }
                    if (axis == 1) { current.x += first; current.z += second; }
                    if (axis == 2) { current.x += first; current.y += second; }
                    if (segment != 0) draw_projected_line(previous, current);
                    previous = current;
                }
            }
        }
    }

    for (const ReplayEngine::Editor::DebugFilledPolygon& polygon : ai_frame.fills)
    {
        if (polygon.points.size() < 3) continue;
        ImVec2 projected[8]{};
        if (polygon.points.size() > (sizeof(projected) / sizeof(projected[0]))) continue;
        bool valid = true;
        for (std::size_t index = 0; index < polygon.points.size(); ++index)
        {
            if (!project_world_to_screen(view_projection, polygon.points[index], main_origin,
                main_size, projected[index]))
            {
                valid = false;
                break;
            }
        }
        if (valid) draw_list->AddConvexPolyFilled(projected,
            static_cast<int>(polygon.points.size()), polygon.color);
    }

    for (const ReplayEngine::Editor::DebugLine& line : ai_frame.lines)
    {
        ImVec2 start{};
        ImVec2 end{};
        if (!project_world_to_screen(view_projection, line.start, main_origin, main_size, start)) continue;
        if (!project_world_to_screen(view_projection, line.end, main_origin, main_size, end)) continue;
        draw_list->AddLine(start, end, line.color, 1.5f);
    }

    if (show_rig_debug_draw && active_editor_view == editor_view::scene)
    {
        // 選択に関係なく、骨を持つモデルはすべて出す。選ばれた骨だけ強調する。
        const ImU32 bone_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(rig_bone_tint.x, rig_bone_tint.y, rig_bone_tint.z, rig_bone_tint.w));
        const ImU32 picked_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(rig_picked_tint.x, rig_picked_tint.y,
                rig_picked_tint.z, rig_picked_tint.w));
        const float picked_scale = (std::max)(1.0f, rig_picked_scale);
        std::size_t rig_bone_total = 0;
        for (const auto& entry : object_rig_debug_bones)
        {
            const std::vector<rig_debug_bone>& bones = entry.second;
            rig_bone_total += bones.size();
            for (const rig_debug_bone& bone : bones)
            {
                if (rig_max_depth > 0)
                {
                    // 根から数えた深さで間引く。指や髪まで出ると密になるため。
                    int depth = 0;
                    int walk = bone.parent;
                    while (walk >= 0 && static_cast<std::size_t>(walk) < bones.size() &&
                        depth <= rig_max_depth)
                    {
                        ++depth;
                        walk = bones[static_cast<std::size_t>(walk)].parent;
                    }
                    if (depth > rig_max_depth) continue;
                }
                const bool picked = !rig_selected_bone.empty() &&
                    bone.name == rig_selected_bone;
                ImVec2 joint{};
                if (!project_world_to_screen(view_projection, bone.world,
                    main_origin, main_size, joint))
                    continue;
                if (bone.parent >= 0 &&
                    static_cast<std::size_t>(bone.parent) < bones.size())
                {
                    ImVec2 parent{};
                    if (project_world_to_screen(view_projection,
                        bones[static_cast<std::size_t>(bone.parent)].world,
                        main_origin, main_size, parent))
                        draw_list->AddLine(parent, joint,
                            picked ? picked_color : bone_color,
                            picked ? rig_bone_thickness * picked_scale : rig_bone_thickness);
                }
                draw_list->AddCircleFilled(joint,
                    picked ? rig_joint_radius * picked_scale : rig_joint_radius,
                    picked ? picked_color : bone_color, 8);
                if (rig_show_names)
                    draw_list->AddText({ joint.x + 4.0f, joint.y - 2.0f },
                        picked ? picked_color : bone_color, bone.name.c_str());
            }
        }
        // 骨が 1 本も来ていないのか、来ていて画面外なのかを切り分ける。
        static std::size_t last_reported_total = static_cast<std::size_t>(-1);
        if (rig_bone_total != last_reported_total)
        {
            last_reported_total = rig_bone_total;
            std::fprintf(stderr, "[Rig] objects=%zu bones=%zu\n",
                object_rig_debug_bones.size(), rig_bone_total);
        }
    }

    for (const ReplayEngine::Editor::DebugWorldLabel& label : ai_frame.labels)
    {
        ImVec2 position{};
        if (!project_world_to_screen(view_projection, label.position, main_origin, main_size, position))
            continue;
        draw_list->AddText(position, label.color, label.text.c_str());
    }

    if (has_stage_markers)
    {
        for (std::size_t index = 0; index < active_object_scene().GameObjectCount(); ++index)
        {
            const ReplayEngine::Core::GameObject* object =
                active_object_scene().GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy())
                continue;
            const DirectX::XMFLOAT3 center = object->GetTransform().WorldPosition();
            ImVec2 center_screen{};
            if (!project_world_to_screen(view_projection, center, main_origin, main_size, center_screen))
                continue;
            if (const auto* spawn = object->GetComponent<
                ReplayEngine::Components::SpawnPointComponent>();
                spawn != nullptr && spawn->ActiveInHierarchy() && spawn->debug_draw)
            {
                constexpr ImU32 color = IM_COL32(70, 220, 255, 240);
                draw_list->AddCircle(center_screen, 10.0f, color, 12, 2.0f);
                draw_list->AddLine({ center_screen.x - 14.0f, center_screen.y },
                    { center_screen.x + 14.0f, center_screen.y }, color, 2.0f);
                draw_list->AddLine({ center_screen.x, center_screen.y - 14.0f },
                    { center_screen.x, center_screen.y + 14.0f }, color, 2.0f);
                const std::string label = "Spawn " + std::to_string(spawn->spawn_id) +
                    " / Team " + std::to_string(spawn->team);
                draw_list->AddText({ center_screen.x + 12.0f, center_screen.y + 8.0f },
                    color, label.c_str());
            }
            if (const auto* jump = object->GetComponent<
                ReplayEngine::Components::JumpPadComponent>();
                jump != nullptr && jump->ActiveInHierarchy() && jump->debug_draw)
            {
                DirectX::XMVECTOR direction = DirectX::XMLoadFloat3(&jump->direction);
                const float length = DirectX::XMVectorGetX(DirectX::XMVector3Length(direction));
                if (!std::isfinite(length) || length <= 1.0e-4f) continue;
                direction = DirectX::XMVectorScale(direction, 1.5f / length);
                DirectX::XMFLOAT3 end{};
                DirectX::XMStoreFloat3(&end, DirectX::XMVectorAdd(
                    DirectX::XMLoadFloat3(&center), direction));
                ImVec2 end_screen{};
                if (!project_world_to_screen(view_projection, end, main_origin, main_size, end_screen))
                    continue;
                constexpr ImU32 color = IM_COL32(255, 185, 65, 240);
                draw_list->AddCircleFilled(center_screen, 5.0f, color, 12);
                draw_list->AddLine(center_screen, end_screen, color, 3.0f);
                draw_list->AddCircleFilled(end_screen, 4.0f, color, 12);
                draw_list->AddText({ center_screen.x + 8.0f, center_screen.y + 8.0f },
                    color, "Jump Pad");
            }
        }
    }

    // 選択中 Enemy の編集ハンドル。Phase 1 では価値が高く衝突しにくい
    // detection_range / attack_range の 2 つだけを直接編集する。
    if (active_editor_view == editor_view::scene && !object_scene_play_mode &&
        object_editor_context.CanEdit())
    {
        ReplayEngine::Core::GameObject* selected_object =
            object_editor_context.Selection().ResolvePrimary(active_object_scene());
        if (selected_object != nullptr)
        {
            if (auto* enemy = selected_object->GetComponent<ReplayEngine::Components::EnemyBehaviourComponent>();
                enemy != nullptr && enemy->debug_draw)
            {
                const DirectX::XMFLOAT3 center = selected_object->GetTransform().WorldPosition();
                const DirectX::XMFLOAT3 detection_world{
                    center.x + (std::max)(0.05f, enemy->detection_range), center.y, center.z };
                const DirectX::XMFLOAT3 attack_world{
                    center.x - (std::max)(0.05f, enemy->attack_range), center.y, center.z };
                ImVec2 detection_screen{};
                ImVec2 attack_screen{};
                if (project_world_to_screen(view_projection, detection_world, main_origin, main_size,
                    detection_screen))
                {
                    draw_list->AddCircleFilled(detection_screen, 6.0f,
                        ReplayEngine::Editor::AINavigationDebugColors::detection);
                    draw_list->AddCircle(detection_screen, 8.0f, IM_COL32(255, 255, 255, 220));
                }
                if (project_world_to_screen(view_projection, attack_world, main_origin, main_size,
                    attack_screen))
                {
                    draw_list->AddCircleFilled(attack_screen, 6.0f,
                        ReplayEngine::Editor::AINavigationDebugColors::attack);
                    draw_list->AddCircle(attack_screen, 8.0f, IM_COL32(255, 255, 255, 220));
                }
            }
        }
    }

    draw_list->PopClipRect();
}

bool framework::handle_ai_navigation_debug_edit()
{
    if (game_scene == nullptr || object_scene_play_mode || !object_editor_context.CanEdit())
    {
        if (ai_navigation_handle_state.dragging) object_editor_context.CancelEdit();
        ClearAINavigationHandleState();
        return false;
    }

    ReplayEngine::Scene::Scene& scene = active_object_scene();
    ReplayEngine::Core::GameObject* selected_object =
        object_editor_context.Selection().ResolvePrimary(scene);
    auto* enemy = selected_object != nullptr
        ? selected_object->GetComponent<ReplayEngine::Components::EnemyBehaviourComponent>()
        : nullptr;
    if (selected_object == nullptr || enemy == nullptr || !enemy->debug_draw)
    {
        if (ai_navigation_handle_state.dragging) object_editor_context.CancelEdit();
        ClearAINavigationHandleState();
        return false;
    }

    if (ai_navigation_handle_state.kind != AINavigationHandleKind::None &&
        ai_navigation_handle_state.object != selected_object->ID())
    {
        if (ai_navigation_handle_state.dragging) object_editor_context.CancelEdit();
        ClearAINavigationHandleState();
    }

    ImGuiWindow* scene_window = ImGui::FindWindowByName("Scene View");
    if (scene_window == nullptr) return false;
    DirectX::XMFLOAT4X4 view{}, projection{};
    DirectX::XMStoreFloat4x4(&view, viewport_view_matrix());
    DirectX::XMStoreFloat4x4(&projection, viewport_projection_matrix());
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGuizmo::SetDrawlist(scene_window->DrawList);
    ImGuizmo::SetRect(main_viewport->Pos.x, main_viewport->Pos.y,
        main_viewport->Size.x, main_viewport->Size.y);
    const DirectX::XMFLOAT3 center = selected_object->GetTransform().WorldPosition();

    // 半径は中心からの距離そのものなので、X 軸だけの移動ギズモがそのまま使える。
    // 索敵は +X、攻撃は -X に置く。描いている円の見た目と掴む場所が一致する。
    bool consumed = false;
    bool over = false;
    const auto edit_range = [&](AINavigationHandleKind kind, int gizmo_id, float sign,
        float& range, const char* property, const char* edit_label)
    {
        DirectX::XMFLOAT4X4 world{};
        DirectX::XMStoreFloat4x4(&world, DirectX::XMMatrixTranslation(
            center.x + sign * (std::max)(0.05f, range), center.y, center.z));
        ImGuizmo::SetHostHovered(
            scene_view_hovered || ai_navigation_handle_state.dragging ? 1 : 0);
        ImGuizmo::SetID(gizmo_id);
        ImGuizmo::Manipulate(&view._11, &projection._11,
            ImGuizmo::TRANSLATE_X, ImGuizmo::WORLD, &world._11);
        over = over || ImGuizmo::IsOver();
        const bool using_now = ImGuizmo::IsUsingID(gizmo_id);
        if (using_now && !ai_navigation_handle_state.dragging)
        {
            ai_navigation_handle_state.object = selected_object->ID();
            ai_navigation_handle_state.kind = kind;
            ai_navigation_handle_state.dragging = true;
            viewport_drag_selecting = false;
            object_editor_context.BeginEdit(edit_label);
        }
        if (!ai_navigation_handle_state.dragging ||
            ai_navigation_handle_state.kind != kind) return;
        range = (std::max)(0.05f, sign * (world._41 - center.x));
        enemy->OnPropertyChanged(property);
        consumed = true;
        if (!using_now)
        {
            object_editor_context.CommitEdit();
            ClearAINavigationHandleState();
        }
    };
    edit_range(AINavigationHandleKind::DetectionRange, gizmo_id_ai_detection, 1.0f,
        enemy->detection_range, "detection_range", "AI 索敵範囲を変更");
    edit_range(AINavigationHandleKind::AttackRange, gizmo_id_ai_attack, -1.0f,
        enemy->attack_range, "attack_range", "AI 攻撃範囲を変更");
    return consumed || over;
}

void framework::draw_collision_diagnostics_panel()
{
    if (!show_collision_diagnostics) return;

    if (!ImGui::Begin(u8"衝突の診断", &show_collision_diagnostics))
    {
        ImGui::End();
        return;
    }

    ReplayEngine::Scene::Scene& scene = active_object_scene();
    ReplayEngine::Scene::SceneServices& services = scene.Services();

    // ---- 問い合わせ先の構成 ------------------------------------------------
    ImGui::TextUnformatted(u8"問い合わせ先");
    ImGui::Separator();
    ImGui::TextUnformatted(u8"Scene Colliders Only");
    ImGui::TextDisabled(u8"Runtime衝突はGameObject Colliderへ一本化されています。");

    // ---- 登録表 --------------------------------------------------------------
    ImGui::Spacing();
    ImGui::TextUnformatted(u8"Collider の登録表");
    ImGui::Separator();

    ImGui::Text(u8"登録: %zu   有効: %zu   押し戻し: %zu   Trigger: %zu",
        object_collision_world.RegisteredColliderCount(),
        object_collision_world.ActiveColliderCount(),
        object_collision_world.BlockingColliderCount(),
        object_collision_world.TriggerColliderCount());
    ImGui::Text(u8"Cook 済み Mesh: %zu   接触ペア: %zu",
        object_collision_world.MeshColliderCount(),
        object_collision_world.ActiveTriggerPairCount());
    ImGui::Text(u8"再走査した回数: %zu", object_collision_world.RescanCount());
    ImGui::TextDisabled(u8"再走査は Scene の構成が変わったときだけ増えます。"
        u8"毎フレーム増えていれば、どこかで構成変更が起き続けています。");

    ImGui::Text(u8"Cook キャッシュ: 生存 %zu / 表 %zu   Cook 実行 %zu 回",
        object_collision_cook_cache.LiveEntryCount(),
        object_collision_cook_cache.TableSize(),
        object_collision_cook_cache.CookCount());

    // ---- 直近の Hit 元 -------------------------------------------------------
    ImGui::Spacing();
    ImGui::TextUnformatted(u8"直近の Hit 元");
    ImGui::Separator();

    const auto& ground = object_collision_world.LastGroundSource();
    const auto& sweep = object_collision_world.LastSweepSource();
    ImGui::Text(u8"接地: %s  Object %s  Collider %u", SourceText(ground),
        ground.object.ToString().c_str(), ground.collider);
    ImGui::Text(u8"壁　: %s  Object %s  Collider %u", SourceText(sweep),
        sweep.object.ToString().c_str(), sweep.collider);

    // 操作対象の Motor が「何に」当たっているかも出す。
    const ReplayEngine::Core::ObjectID controlled = services.ControlledObject();
    if (const ReplayEngine::Core::GameObject* target = scene.FindGameObjectByID(controlled))
    {
        if (const auto* motor =
            target->GetComponent<ReplayEngine::Components::CharacterMotorComponent>())
        {
            ImGui::Spacing();
            ImGui::Text(u8"操作対象: %s", target->Name().c_str());
            ImGui::Text(u8"接地している: %s", motor->Grounded() ? u8"はい" : u8"いいえ");
            ImGui::Text(u8"地形を検出: %s", motor->HasGround() ? u8"はい" : u8"いいえ");

            const std::string status = motor->PrimaryColliderStatus();
            if (!status.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), u8"⚠ %s", status.c_str());
            }
            else if (const auto* primary = motor->ResolvePrimaryCollider())
            {
                ImGui::Text(u8"移動用 Collider: %s #%d",
                    ReplayEngine::Components::ToString(primary->Shape()),
                    primary->collider_key);
            }
        }
    }

    // ---- 表示設定 -------------------------------------------------------------
    ImGui::Spacing();
    ImGui::TextUnformatted(u8"表示");
    ImGui::Separator();
    ImGui::Checkbox(u8"Collider の形を描く", &show_collider_debug_draw);
    ImGui::Checkbox(u8"境界ボックスを描く", &show_collider_debug_bounds);
    ImGui::Checkbox(u8"Mesh の三角形を描く", &show_collider_debug_wireframe);
    ImGui::Checkbox(u8"ライトの範囲を描く", &show_light_range_debug_draw);
    ImGui::Checkbox("Normal Adjust の範囲を描く", &show_normal_adjust_debug_draw);
    ImGui::TextDisabled(u8"選択中の Point / Spot Light の範囲だけを表示します。");
    ImGui::TextDisabled(u8"Mesh は既定で境界ボックスのみです。"
        u8"三角形は Collider 側の「三角形を表示」も有効にしたものだけ描かれます。");

    ImGui::End();
}

#else

void framework::draw_collider_debug_overlay() {}
void framework::draw_collision_diagnostics_panel() {}
bool framework::handle_ai_navigation_debug_edit() { return false; }

#endif

// 世界座標を画面へ落とす。リグの描画と当たり判定で同じ式を使う。
bool framework::project_world_to_screen(const DirectX::XMMATRIX& view_projection,
    const DirectX::XMFLOAT3& world, const ImVec2& origin, const ImVec2& size,
    ImVec2& out) const noexcept
{
    using namespace DirectX;
    const XMVECTOR position = XMVectorSet(world.x, world.y, world.z, 1.0f);
    const XMVECTOR clip = XMVector4Transform(position, view_projection);
    const float w = XMVectorGetW(clip);
    if (w <= 1.0e-4f) return false;
    const float x = XMVectorGetX(clip) / w;
    const float y = XMVectorGetY(clip) / w;
    out.x = origin.x + (x * 0.5f + 0.5f) * size.x;
    out.y = origin.y + (0.5f - y * 0.5f) * size.y;
    return true;
}
