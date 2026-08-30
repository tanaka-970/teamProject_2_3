// Collider と AI / Navigation の Scene View 可視化。
//
// 新しい描画パイプラインは作らず、既存の DebugLine -> ImGui overlay 経路へ
// AI の塗り・文字・編集ハンドルを同居させる。Runtime Component は ImGui に依存しない。

#include "framework.h"

#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/EnemyBehaviourComponent.h"
#include "../../RePlayEngine/Components/Gameplay/StageGameplayComponents.h"
#include "../../RePlayEngine/Components/Navigation/NavAgentComponent.h"
#include "../../RePlayEngine/Components/Physics/ColliderComponent.h"
#include "../../RePlayEngine/Editor/Debug/AINavigationDebugDraw.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Physics/CollisionLayers.h"

#include <cmath>
#include <string>

#ifdef USE_IMGUI

namespace
{
    bool ProjectToScreen(const DirectX::XMMATRIX& view_projection,
        const DirectX::XMFLOAT3& world, const ImVec2& origin, const ImVec2& size,
        ImVec2& out)
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

    float DistanceSquared(const ImVec2& a, const ImVec2& b) noexcept
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return dx * dx + dy * dy;
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

    if (object_collider_debug_lines.empty() && ai_frame.lines.empty() &&
        ai_frame.fills.empty() && ai_frame.labels.empty() && !has_stage_markers) return;

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
        if (!ProjectToScreen(view_projection, line.start, main_origin, main_size, start)) continue;
        if (!ProjectToScreen(view_projection, line.end, main_origin, main_size, end)) continue;
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

    for (const ReplayEngine::Editor::DebugFilledPolygon& polygon : ai_frame.fills)
    {
        if (polygon.points.size() < 3) continue;
        ImVec2 projected[8]{};
        if (polygon.points.size() > (sizeof(projected) / sizeof(projected[0]))) continue;
        bool valid = true;
        for (std::size_t index = 0; index < polygon.points.size(); ++index)
        {
            if (!ProjectToScreen(view_projection, polygon.points[index], main_origin,
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
        if (!ProjectToScreen(view_projection, line.start, main_origin, main_size, start)) continue;
        if (!ProjectToScreen(view_projection, line.end, main_origin, main_size, end)) continue;
        draw_list->AddLine(start, end, line.color, 1.5f);
    }

    for (const ReplayEngine::Editor::DebugWorldLabel& label : ai_frame.labels)
    {
        ImVec2 position{};
        if (!ProjectToScreen(view_projection, label.position, main_origin, main_size, position))
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
            if (!ProjectToScreen(view_projection, center, main_origin, main_size, center_screen))
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
                if (!ProjectToScreen(view_projection, end, main_origin, main_size, end_screen))
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
                if (ProjectToScreen(view_projection, detection_world, main_origin, main_size,
                    detection_screen))
                {
                    draw_list->AddCircleFilled(detection_screen, 6.0f,
                        ReplayEngine::Editor::AINavigationDebugColors::detection);
                    draw_list->AddCircle(detection_screen, 8.0f, IM_COL32(255, 255, 255, 220));
                }
                if (ProjectToScreen(view_projection, attack_world, main_origin, main_size,
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

    const DirectX::XMMATRIX view_projection =
        viewport_view_matrix() * viewport_projection_matrix();
    // 描画と同じ投影規約を使う。ここだけ Scene View 矩形にすると、
    // 線は正しい位置に出ているのにハンドルだけ掴めない状態になる。
    const ImVec2 main_origin = ImGui::GetMainViewport()->Pos;
    const ImVec2 main_size = ImGui::GetMainViewport()->Size;
    const DirectX::XMFLOAT3 center = selected_object->GetTransform().WorldPosition();
    const DirectX::XMFLOAT3 detection_world{
        center.x + (std::max)(0.05f, enemy->detection_range), center.y, center.z };
    const DirectX::XMFLOAT3 attack_world{
        center.x - (std::max)(0.05f, enemy->attack_range), center.y, center.z };
    ImVec2 detection_screen{};
    ImVec2 attack_screen{};
    const bool detection_visible = ProjectToScreen(view_projection, detection_world,
        main_origin, main_size, detection_screen);
    const bool attack_visible = ProjectToScreen(view_projection, attack_world,
        main_origin, main_size, attack_screen);

    const ImVec2 mouse = ImGui::GetMousePos();
    const bool inside_scene = mouse.x >= scene_view_min_x && mouse.x <= scene_view_max_x &&
        mouse.y >= scene_view_min_y && mouse.y <= scene_view_max_y;

    if (ai_navigation_handle_state.kind == AINavigationHandleKind::None &&
        inside_scene && scene_view_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        constexpr float PickRadiusSquared = 12.0f * 12.0f;
        const float detection_distance = detection_visible
            ? DistanceSquared(mouse, detection_screen) : PickRadiusSquared + 1.0f;
        const float attack_distance = attack_visible
            ? DistanceSquared(mouse, attack_screen) : PickRadiusSquared + 1.0f;
        if (detection_distance <= PickRadiusSquared || attack_distance <= PickRadiusSquared)
        {
            ai_navigation_handle_state.object = selected_object->ID();
            ai_navigation_handle_state.kind = detection_distance <= attack_distance
                ? AINavigationHandleKind::DetectionRange : AINavigationHandleKind::AttackRange;
            ai_navigation_handle_state.dragging = false;
            viewport_drag_selecting = false;
            return true;
        }
    }

    if (ai_navigation_handle_state.kind == AINavigationHandleKind::None) return false;

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (!ai_navigation_handle_state.dragging &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
        {
            object_editor_context.BeginEdit(ai_navigation_handle_state.kind ==
                AINavigationHandleKind::DetectionRange
                ? "AI 索敵範囲を変更" : "AI 攻撃範囲を変更");
            ai_navigation_handle_state.dragging = true;
        }

        if (ai_navigation_handle_state.dragging)
        {
            const float local_x = mouse.x - scene_view_min_x;
            const float local_y = mouse.y - scene_view_min_y;
            const auto ray = viewport_picking_ray(local_x, local_y);

            // 水平面そのものとの交差だとカメラ角度によって不安定になるため、
            // XZ 平面上で中心に最も近い Ray 上の点から半径を求める。
            const float denominator = ray.direction.x * ray.direction.x +
                ray.direction.z * ray.direction.z;
            if (denominator > 1.0e-6f)
            {
                const float ox = ray.origin.x - center.x;
                const float oz = ray.origin.z - center.z;
                float t = -(ox * ray.direction.x + oz * ray.direction.z) / denominator;
                t = (std::max)(0.0f, t);
                const float px = ray.origin.x + ray.direction.x * t;
                const float pz = ray.origin.z + ray.direction.z * t;
                const float dx = px - center.x;
                const float dz = pz - center.z;
                const float radius = (std::max)(0.05f, std::sqrt(dx * dx + dz * dz));
                if (ai_navigation_handle_state.kind == AINavigationHandleKind::DetectionRange)
                {
                    enemy->detection_range = radius;
                    enemy->OnPropertyChanged("detection_range");
                }
                else
                {
                    enemy->attack_range = radius;
                    enemy->OnPropertyChanged("attack_range");
                }
            }
        }
        return true;
    }

    if (ai_navigation_handle_state.dragging) object_editor_context.CommitEdit();
    ClearAINavigationHandleState();
    return true;
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
    ImGui::TextDisabled(u8"Mesh は既定で境界ボックスのみです。"
        u8"三角形は Collider 側の「三角形を表示」も有効にしたものだけ描かれます。");

    ImGui::End();
}

#else

void framework::draw_collider_debug_overlay() {}
void framework::draw_collision_diagnostics_panel() {}
bool framework::handle_ai_navigation_debug_edit() { return false; }

#endif
