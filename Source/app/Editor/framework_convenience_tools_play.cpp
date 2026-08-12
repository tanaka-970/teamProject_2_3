#include "framework.h"

#include "../../RePlayEngine/Components/Physics/ColliderComponent.h"
#include "../../RePlayEngine/Components/Gameplay/StageGameplayComponents.h"
#include "../../RePlayEngine/Physics/CollisionLayers.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Runtime/Scene/SceneFlowAsset.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
void framework::request_play_from_here(const DirectX::XMFLOAT3& position,
    bool camera_direction, const char* label)
{
    if (object_scene_play_mode) return;
    play_spawn_override.active = true;
    play_spawn_override.apply_rotation = camera_direction;
    play_spawn_override.use_camera_direction = camera_direction;
    play_spawn_override.position = position;
    play_spawn_override.label = label != nullptr ? label : "Play From Here";
    play_spawn_override.rotation_radians = { 0.0f, editor_camera.Yaw(), 0.0f };
    enter_object_play_mode();
}

void framework::apply_play_spawn_override(
    ReplayEngine::Scene::Serialization::SceneData& snapshot)
{
    if (!play_spawn_override.active) return;

    const ReplayEngine::Core::ObjectID controlled = snapshot.controlled_object;
    ReplayEngine::Core::GameObject* object = object_scene.FindGameObjectByID(controlled);
    if (object == nullptr)
    {
        push_editor_log("Warning", "Play From Here: 操作対象 GameObject がありません");
        play_spawn_override.active = false;
        return;
    }

    DirectX::XMFLOAT3 target = play_spawn_override.position;

    // Collider の底面が指定地点へ接するように持ち上げる。
    // 編集 Scene の Transform を一瞬だけ動かしてローカル値を求め、
    // SceneData にだけ反映したあと即座に元へ戻す。
    // これにより Runtime の OnAwake / OnStart が走る「前」から開始位置が正しい。
    float clearance = 0.05f;
    const DirectX::XMFLOAT3 old_world = object->GetTransform().WorldPosition();
    const DirectX::XMFLOAT3 old_local = object->GetTransform().LocalPosition();
    const DirectX::XMFLOAT3 old_rotation = object->GetTransform().LocalRotationEuler();
    for (std::size_t i = 0; i < object->ComponentCount(); ++i)
    {
        auto* collider = dynamic_cast<ReplayEngine::Components::ColliderComponent*>(
            object->ComponentAt(i));
        if (collider == nullptr || !collider->Enabled() || collider->is_trigger) continue;
        DirectX::XMFLOAT3 minimum{}, maximum{};
        if (collider->ComputeWorldBounds(minimum, maximum))
            clearance = (std::max)(clearance, old_world.y - minimum.y + 0.03f);
    }
    target.y += clearance;
    object->GetTransform().SetWorldPosition(target);

    if (play_spawn_override.apply_rotation)
    {
        DirectX::XMFLOAT3 rotation = play_spawn_override.rotation_radians;
        if (play_spawn_override.use_camera_direction)
        {
            // 上下を向いていても Player 自体は水平を保ち、Yaw だけ合わせる。
            rotation.x = old_rotation.x;
            rotation.z = old_rotation.z;
            rotation.y = editor_camera.Yaw();
        }
        object->GetTransform().SetLocalRotationEuler(rotation);
    }

    bool copied = false;
    for (ReplayEngine::Scene::Serialization::GameObjectData& data : snapshot.objects)
    {
        if (data.id != controlled) continue;
        data.position = object->GetTransform().LocalPosition();
        data.rotation = object->GetTransform().LocalRotationEuler();
        copied = true;
        break;
    }

    // Edit World は絶対に変更したままにしない。
    object->GetTransform().SetLocalPosition(old_local);
    object->GetTransform().SetLocalRotationEuler(old_rotation);

    if (!copied)
    {
        push_editor_log("Warning", "Play From Here: SceneData に操作対象が見つかりません");
        play_spawn_override.active = false;
    }
}

void framework::draw_play_from_here_context_menu()
{
#ifdef USE_IMGUI
    if (object_scene_play_mode || active_editor_view != editor_view::scene) return;

    // RMB は Unreal 風のカメラ Look にも使う。
    // そのため「短い右クリック」は Context Menu、「右ドラッグ」はカメラ操作と
    // 明確に分ける。BeginPopupContextItem の既定挙動だとドラッグ後にも
    // メニューが出やすく、Fly 操作の手触りを壊していた。
    const ImVec2 mouse = ImGui::GetMousePos();
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        scene_context_right_click_tracking = true;
        scene_context_right_click_dragged = false;
        scene_context_right_click_start_x = mouse.x;
        scene_context_right_click_start_y = mouse.y;
        scene_context_world_point_valid =
            scene_view_mouse_world_point(scene_context_world_point, nullptr);
    }

    if (scene_context_right_click_tracking && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        const float dx = mouse.x - scene_context_right_click_start_x;
        const float dy = mouse.y - scene_context_right_click_start_y;
        if (dx * dx + dy * dy > 36.0f) scene_context_right_click_dragged = true;
    }

    if (scene_context_right_click_tracking && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        if (!scene_context_right_click_dragged && ImGui::IsItemHovered())
            ImGui::OpenPopup("##SceneViewportContext");
        scene_context_right_click_tracking = false;
    }

    if (!ImGui::BeginPopup("##SceneViewportContext")) return;

    const DirectX::XMFLOAT3 point = scene_context_world_point;
    const bool has_point = scene_context_world_point_valid;
    if (ImGui::MenuItem("Play From Here", nullptr, false, has_point))
        request_play_from_here(point, false, "Play From Here");
    if (ImGui::MenuItem("Play From Here + Camera Direction", nullptr, false, has_point))
        request_play_from_here(point, true, "Play From Here + Camera Direction");
    if (ImGui::MenuItem("Play From Camera"))
        request_play_from_here(editor_camera.Position(), true, "Play From Camera");

    if (ImGui::BeginMenu("Play From Checkpoint"))
    {
        bool any = false;
        for (std::size_t i = 0; i < object_scene.GameObjectCount(); ++i)
        {
            ReplayEngine::Core::GameObject* object = object_scene.GameObjectAt(i);
            if (object == nullptr || object->PendingDestroy()) continue;
            auto* checkpoint = object->GetComponent<ReplayEngine::Components::CheckpointComponent>();
            if (checkpoint == nullptr) continue;
            any = true;
            const DirectX::XMFLOAT3 base = object->GetTransform().WorldPosition();
            const DirectX::XMFLOAT3 spawn{ base.x + checkpoint->respawn_position_offset.x,
                base.y + checkpoint->respawn_position_offset.y,
                base.z + checkpoint->respawn_position_offset.z };
            const std::string label = object->Name() + "  (#" +
                std::to_string(checkpoint->checkpoint_id) + ")";
            if (ImGui::MenuItem(label.c_str()))
            {
                play_spawn_override.active = true;
                play_spawn_override.apply_rotation = true;
                play_spawn_override.use_camera_direction = false;
                play_spawn_override.position = spawn;
                play_spawn_override.rotation_radians = checkpoint->respawn_rotation;
                play_spawn_override.label = "Checkpoint " + label;
                enter_object_play_mode();
            }
        }
        if (!any) ImGui::TextDisabled("Checkpoint がありません");
        ImGui::EndMenu();
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Add Scene Memo Here", nullptr, false, has_point))
        create_scene_note_at(point);
    if (ImGui::MenuItem("Open Scene Notes")) show_scene_notes_panel = true;
    ImGui::EndPopup();
#endif
}
