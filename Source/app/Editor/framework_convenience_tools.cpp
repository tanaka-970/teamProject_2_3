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

namespace
{
    using ReplayEngine::Components::EditorNoteComponent;
    using ReplayEngine::Runtime::SceneFlowCompareOp;
    using ReplayEngine::Runtime::SceneFlowConditionType;

    const char* NoteCategoryName(int category) noexcept
    {
        static const char* names[] = { "TODO", "BUG", "ART", "PROGRAM", "LEVEL", "IDEA" };
        return names[(std::max)(0, (std::min)(category, 5))];
    }

    template<std::size_t N>
    void CopyText(std::array<char, N>& buffer, const std::string& text)
    {
        const std::size_t count = (std::min)(N - 1, text.size());
        std::memcpy(buffer.data(), text.data(), count);
        buffer[count] = '\0';
    }
}

bool framework::scene_view_mouse_world_point(DirectX::XMFLOAT3& out_position,
    DirectX::XMFLOAT3* out_normal) const
{
#ifdef USE_IMGUI
    const float width = scene_view_max_x - scene_view_min_x;
    const float height = scene_view_max_y - scene_view_min_y;
    if (width <= 1.0f || height <= 1.0f) return false;

    const ImVec2 mouse = ImGui::GetMousePos();
    const float local_x = mouse.x - scene_view_min_x;
    const float local_y = mouse.y - scene_view_min_y;
    if (local_x < 0.0f || local_y < 0.0f || local_x > width || local_y > height)
        return false;

    const auto ray = editor_camera.BuildPickingRay(local_x, local_y, width, height);
    ReplayEngine::Scene::RaycastHit hit{};
    ReplayEngine::Scene::CollisionQueryFilter filter;
    filter.mask = ReplayEngine::Physics::CollisionLayers::all_layers_mask;
    if (object_collision_world.RaycastFiltered(ray.origin, ray.direction,
        editor_camera.far_clip, filter, hit) && hit.valid)
    {
        out_position = hit.point;
        if (out_normal != nullptr) *out_normal = hit.normal;
        return true;
    }

    // Collider が無い空の Scene でも使えるよう、編集グリッドの y=0 と交差させる。
    if (std::fabs(ray.direction.y) > 1.0e-6f)
    {
        const float t = -ray.origin.y / ray.direction.y;
        if (t >= 0.0f && t <= editor_camera.far_clip)
        {
            out_position = {
                ray.origin.x + ray.direction.x * t,
                0.0f,
                ray.origin.z + ray.direction.z * t };
            if (out_normal != nullptr) *out_normal = { 0.0f, 1.0f, 0.0f };
            return true;
        }
    }

    // 最後の fallback。レイ前方 10m。右クリックが完全に無反応になるより明確。
    out_position = {
        ray.origin.x + ray.direction.x * 10.0f,
        ray.origin.y + ray.direction.y * 10.0f,
        ray.origin.z + ray.direction.z * 10.0f };
    if (out_normal != nullptr) *out_normal = { 0.0f, 1.0f, 0.0f };
    return true;
#else
    (void)out_position;
    (void)out_normal;
    return false;
#endif
}

ReplayEngine::Core::GameObject* framework::create_scene_note_at(
    const DirectX::XMFLOAT3& world_position, const std::string& text)
{
    if (object_scene_play_mode) return nullptr;

    ReplayEngine::Core::GameObject* object = object_scene.CreateGameObject("Scene Note");
    if (object == nullptr) return nullptr;
    object->GetTransform().SetWorldPosition(world_position);

    EditorNoteComponent* note = object->AddComponent<EditorNoteComponent>();
    if (note == nullptr)
    {
        object_scene.DestroyGameObject(object);
        return nullptr;
    }
    note->text = text.empty() ? std::string("ここを修正") : text;

    object_editor_context.Selection().Select(object->ID(), false);
    selected_editor_object = editor_selection::game_object;
    object_editor_context.MarkDirty();
    object_editor_context.SetStatus("シーンメモを追加しました");
    show_scene_notes_panel = true;
    return object;
}

void framework::draw_scene_note_overlay()
{
#ifdef USE_IMGUI
    if (object_scene_play_mode || active_editor_view != editor_view::scene) return;
    const float width = scene_view_max_x - scene_view_min_x;
    const float height = scene_view_max_y - scene_view_min_y;
    if (width <= 1.0f || height <= 1.0f) return;

    using namespace DirectX;
    const XMMATRIX view = viewport_view_matrix();
    const XMMATRIX projection = viewport_projection_matrix();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(scene_view_min_x, scene_view_min_y),
        ImVec2(scene_view_max_x, scene_view_max_y), true);

    for (std::size_t i = 0; i < object_scene.GameObjectCount(); ++i)
    {
        ReplayEngine::Core::GameObject* object = object_scene.GameObjectAt(i);
        if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;

        const auto notes = object->GetComponents<EditorNoteComponent>();
        if (notes.empty()) continue;
        const XMFLOAT3 base = object->GetTransform().WorldPosition();

        for (EditorNoteComponent* note : notes)
        {
            if (note == nullptr || !note->Enabled() || !note->show_in_viewport) continue;
            if (note->completed && note->hide_when_completed) continue;

            const XMFLOAT3 world{ base.x + note->offset.x,
                base.y + note->offset.y, base.z + note->offset.z };

            // カメラ後方の点は Project 後に画面内へ折り返すことがあるので除外する。
            const XMFLOAT3 camera_position = editor_camera.Position();
            const XMFLOAT3 camera_forward = editor_camera.Forward();
            const float facing = (world.x - camera_position.x) * camera_forward.x +
                (world.y - camera_position.y) * camera_forward.y +
                (world.z - camera_position.z) * camera_forward.z;
            if (facing <= 0.0f) continue;

            XMFLOAT3 screen{};
            XMStoreFloat3(&screen, XMVector3Project(XMLoadFloat3(&world),
                0.0f, 0.0f, width, height, 0.0f, 1.0f,
                projection, view, XMMatrixIdentity()));
            if (screen.z < 0.0f || screen.z > 1.0f) continue;

            const ImVec2 anchor{ scene_view_min_x + screen.x, scene_view_min_y + screen.y };
            if (anchor.x < scene_view_min_x || anchor.x > scene_view_max_x ||
                anchor.y < scene_view_min_y || anchor.y > scene_view_max_y) continue;

            // Unreal の World Note / Text Render に近い感覚にする。
            // カテゴリ名・吹き出し・枠を勝手に足さず、ユーザーが書いた文字だけを描く。
            if (note->text.empty()) continue;

            ImVec4 text_color{ note->color.x, note->color.y, note->color.z, note->color.w };
            text_color.x = std::clamp(text_color.x, 0.0f, 1.0f);
            text_color.y = std::clamp(text_color.y, 0.0f, 1.0f);
            text_color.z = std::clamp(text_color.z, 0.0f, 1.0f);
            text_color.w = std::clamp(text_color.w, 0.0f, 1.0f);
            if (note->completed) text_color.w *= 0.45f;

            const float font_size = ImGui::GetFontSize() *
                std::clamp(note->text_scale, 0.35f, 4.0f);
            draw->AddText(ImGui::GetFont(), font_size, anchor,
                ImGui::ColorConvertFloat4ToU32(text_color), note->text.c_str());
        }
    }
    draw->PopClipRect();
#endif
}

void framework::draw_scene_notes_panel()
{
#ifdef USE_IMGUI
    if (!show_scene_notes_panel) return;
    if (!ImGui::Begin("シーンメモ", &show_scene_notes_panel))
    {
        ImGui::End();
        return;
    }

    int total = 0;
    int open_count = 0;
    for (std::size_t i = 0; i < object_scene.GameObjectCount(); ++i)
    {
        ReplayEngine::Core::GameObject* object = object_scene.GameObjectAt(i);
        if (object == nullptr || object->PendingDestroy()) continue;
        for (EditorNoteComponent* note : object->GetComponents<EditorNoteComponent>())
        {
            if (note == nullptr) continue;
            ++total;
            if (!note->completed) ++open_count;
        }
    }
    ImGui::Text("未完了 %d / 全 %d", open_count, total);
    ImGui::SameLine();
    if (ImGui::Button("選択位置に新規メモ"))
    {
        DirectX::XMFLOAT3 position = editor_camera.OrbitPivot();
        create_scene_note_at(position);
    }
    ImGui::SameLine();
    const ReplayEngine::Core::ObjectID selected_note_target =
        object_editor_context.Selection().Primary();
    const bool can_attach_note = selected_note_target.Valid() && !object_scene_play_mode;
    if (can_attach_note)
    {
        if (ImGui::Button("選択GameObjectにメモ追加"))
        {
            if (ReplayEngine::Core::GameObject* selected =
                object_scene.FindGameObjectByID(selected_note_target))
            {
                if (selected->AddComponent<EditorNoteComponent>() != nullptr)
                {
                    object_editor_context.MarkDirty();
                    object_editor_context.SetStatus("選択GameObjectにシーンメモを追加しました");
                }
            }
        }
    }
    else
    {
        ImGui::TextDisabled("GameObjectを選択すると追従メモを追加できます");
    }
    ImGui::Separator();

    for (std::size_t i = 0; i < object_scene.GameObjectCount(); ++i)
    {
        ReplayEngine::Core::GameObject* object = object_scene.GameObjectAt(i);
        if (object == nullptr || object->PendingDestroy()) continue;
        auto notes = object->GetComponents<EditorNoteComponent>();
        for (std::size_t n = 0; n < notes.size(); ++n)
        {
            EditorNoteComponent* note = notes[n];
            if (note == nullptr) continue;
            ImGui::PushID(object);
            ImGui::PushID(static_cast<int>(n));
            bool completed = note->completed;
            if (ImGui::Checkbox("##done", &completed))
            {
                note->completed = completed;
                object_editor_context.MarkDirty();
            }
            ImGui::SameLine();
            const std::string label = std::string(NoteCategoryName(note->category)) +
                " | " + object->Name() + " | " + note->text;
            if (ImGui::Selectable(label.c_str(), false))
            {
                object_editor_context.Selection().Select(object->ID(), false);
                selected_editor_object = editor_selection::game_object;
                editor_camera.FocusOnPoint(object->GetTransform().WorldPosition());
            }
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    ImGui::End();
#endif
}

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

bool framework::load_scene_flow_editor(const ReplayEngine::Assets::AssetRecord& record)
{
    if (record.kind != ReplayEngine::Assets::AssetKind::SceneFlow) return false;

    // 別 Asset へ移る前に編集内容を落とさない。
    if (scene_flow_editor_loaded && scene_flow_editor_dirty &&
        scene_flow_editor_guid != record.guid)
    {
        if (!save_scene_flow_editor()) return false;
    }

    std::string error;
    ReplayEngine::Runtime::SceneFlowAsset loaded;
    if (!ReplayEngine::Runtime::SceneFlowAsset::Load(loaded, record.source_path, error))
    {
        scene_flow_editor_status = "Scene Flow 読込失敗: " + error;
        return false;
    }
    scene_flow_editor_asset = std::move(loaded);
    scene_flow_editor_path = record.source_path;
    scene_flow_editor_guid = record.guid;
    scene_flow_editor_loaded = true;
    scene_flow_editor_dirty = false;
    scene_flow_editor_status = "Scene Flow を開きました: " + record.display_name;
    show_scene_flow_panel = true;
    return true;
}

bool framework::save_scene_flow_editor()
{
    if (!scene_flow_editor_loaded || scene_flow_editor_path.empty()) return false;
    std::string error;
    if (!ReplayEngine::Runtime::SceneFlowAsset::Save(
        scene_flow_editor_asset, scene_flow_editor_path, error))
    {
        scene_flow_editor_status = "Scene Flow 保存失敗: " + error;
        return false;
    }
    scene_flow_editor_dirty = false;
    scene_flow_editor_status = "保存しました: " + scene_flow_editor_path.filename().u8string();
    if (project_settings.SceneFlowGuid() == scene_flow_editor_guid)
        sync_runtime_scene_flow_asset();
    return true;
}

void framework::sync_runtime_scene_flow_asset()
{
    if (!object_scene_flow) return;
    const auto status = project_settings.ResolveSceneFlow(asset_database);
    if (!status.IsResolved())
    {
        object_scene_flow->ClearFlowAsset();
        return;
    }

    ReplayEngine::Runtime::SceneFlowAsset asset;
    std::string error;
    if (!ReplayEngine::Runtime::SceneFlowAsset::Load(asset, status.path, error))
    {
        object_scene_flow->ClearFlowAsset();
        push_editor_log("Warning", "Active Scene Flow を読み込めません: " + error, status.path);
        return;
    }
    object_scene_flow->SetFlowAsset(asset);
}

void framework::draw_scene_flow_panel()
{
#ifdef USE_IMGUI
    if (!show_scene_flow_panel) return;
    if (!ImGui::Begin("Scene Flow", &show_scene_flow_panel))
    {
        ImGui::End();
        // Close ボタンで閉じたフレームでも未保存の編集を失わない。
        if (!show_scene_flow_panel && scene_flow_editor_dirty) save_scene_flow_editor();
        return;
    }

    if (!scene_flow_editor_loaded)
    {
        ImGui::TextDisabled("Project で .replaysceneflow を作成/開いてください");
        ImGui::End();
        return;
    }

    ImGui::Text("%s%s", scene_flow_editor_asset.name.c_str(),
        scene_flow_editor_dirty ? " *" : "");
    ImGui::SameLine();
    if (ImGui::Button("Save")) save_scene_flow_editor();
    ImGui::SameLine();
    const bool is_active = project_settings.SceneFlowGuid() == scene_flow_editor_guid;
    if (!is_active)
    {
        if (ImGui::Button("Set Active"))
        {
            project_settings.SetSceneFlowGuid(scene_flow_editor_guid);
            save_project_settings();
            sync_runtime_scene_flow_asset();
        }
    }
    else ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.55f, 1.0f), "ACTIVE");

    ImGui::TextDisabled("C#/C++ は LoadScene を決め打ちせず TriggerSceneFlow(\"Event\") を呼べます");
    ImGui::Separator();

    if (ImGui::Button("+ Transition"))
    {
        scene_flow_editor_asset.AddTransition();
        scene_flow_editor_dirty = true;
    }

    auto& transitions = scene_flow_editor_asset.transitions;
    for (std::size_t i = 0; i < transitions.size(); )
    {
        auto& transition = transitions[i];
        ImGui::PushID(static_cast<int>(transition.id));
        const std::string header = "Transition #" + std::to_string(transition.id) +
            "  " + transition.event_name;
        bool remove = false;
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("Enabled", &transition.enabled)) scene_flow_editor_dirty = true;
            ImGui::SameLine();
            if (ImGui::Button("Delete")) remove = true;

            std::array<char, 128> event{};
            CopyText(event, transition.event_name);
            if (ImGui::InputText("Event", event.data(), event.size()))
            {
                transition.event_name = event.data();
                scene_flow_editor_dirty = true;
            }
            if (ImGui::InputInt("Priority", &transition.priority)) scene_flow_editor_dirty = true;

            const auto scene_label = [this](const std::string& guid, const char* empty_label)
            {
                if (guid.empty()) return std::string(empty_label);
                const auto* record = asset_database.FindByGuid(guid);
                return record != nullptr ? (record->display_name.empty()
                    ? record->source_path.filename().u8string() : record->display_name)
                    : std::string("[Missing] ") + guid;
            };

            const std::string from_preview = scene_label(transition.from_scene_guid, "Any Scene");
            if (ImGui::BeginCombo("From", from_preview.c_str()))
            {
                if (ImGui::Selectable("Any Scene", transition.from_scene_guid.empty()))
                {
                    transition.from_scene_guid.clear(); scene_flow_editor_dirty = true;
                }
                for (const auto& record : asset_database.Records())
                {
                    if (record.kind != ReplayEngine::Assets::AssetKind::Scene) continue;
                    const std::string label = record.display_name.empty()
                        ? record.source_path.filename().u8string() : record.display_name;
                    if (ImGui::Selectable(label.c_str(), transition.from_scene_guid == record.guid))
                    {
                        transition.from_scene_guid = record.guid; scene_flow_editor_dirty = true;
                    }
                }
                ImGui::EndCombo();
            }

            const std::string to_preview = scene_label(transition.to_scene_guid, "Select Scene");
            if (ImGui::BeginCombo("To", to_preview.c_str()))
            {
                for (const auto& record : asset_database.Records())
                {
                    if (record.kind != ReplayEngine::Assets::AssetKind::Scene) continue;
                    const std::string label = record.display_name.empty()
                        ? record.source_path.filename().u8string() : record.display_name;
                    if (ImGui::Selectable(label.c_str(), transition.to_scene_guid == record.guid))
                    {
                        transition.to_scene_guid = record.guid; scene_flow_editor_dirty = true;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::TextDisabled("Conditions (AND)");
            for (std::size_t c = 0; c < transition.conditions.size(); )
            {
                auto& condition = transition.conditions[c];
                ImGui::PushID(static_cast<int>(c));
                int type = static_cast<int>(condition.type);
                const char* types[] = { "Bool", "Int", "Float" };
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::Combo("##type", &type, types, 3))
                {
                    condition.type = static_cast<SceneFlowConditionType>(type);
                    scene_flow_editor_dirty = true;
                }
                ImGui::SameLine();
                std::array<char, 96> key{}; CopyText(key, condition.key);
                ImGui::SetNextItemWidth(145.0f);
                if (ImGui::InputText("##key", key.data(), key.size()))
                {
                    condition.key = key.data(); scene_flow_editor_dirty = true;
                }
                ImGui::SameLine();
                int op = static_cast<int>(condition.op);
                const char* ops[] = { "==", "!=", "<", "<=", ">", ">=" };
                ImGui::SetNextItemWidth(65.0f);
                if (ImGui::Combo("##op", &op, ops, 6))
                {
                    condition.op = static_cast<SceneFlowCompareOp>(op);
                    scene_flow_editor_dirty = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                if (condition.type == SceneFlowConditionType::Bool)
                {
                    bool value = condition.value != 0.0;
                    if (ImGui::Checkbox("##value", &value))
                    {
                        condition.value = value ? 1.0 : 0.0;
                        scene_flow_editor_dirty = true;
                    }
                }
                else if (condition.type == SceneFlowConditionType::Int)
                {
                    int value = static_cast<int>(condition.value);
                    if (ImGui::InputInt("##value", &value))
                    {
                        condition.value = static_cast<double>(value);
                        scene_flow_editor_dirty = true;
                    }
                }
                else if (ImGui::InputDouble("##value", &condition.value, 0.1, 1.0, "%.3f"))
                {
                    scene_flow_editor_dirty = true;
                }
                ImGui::SameLine();
                bool remove_condition = ImGui::SmallButton("X");
                ImGui::PopID();
                if (remove_condition)
                {
                    transition.conditions.erase(transition.conditions.begin() + c);
                    scene_flow_editor_dirty = true;
                }
                else ++c;
            }
            if (ImGui::SmallButton("+ Condition"))
            {
                ReplayEngine::Runtime::SceneFlowCondition condition;
                condition.key = "Flag";
                transition.conditions.push_back(std::move(condition));
                scene_flow_editor_dirty = true;
            }
        }
        ImGui::PopID();
        if (remove)
        {
            const std::uint64_t id = transition.id;
            scene_flow_editor_asset.RemoveTransition(id);
            scene_flow_editor_dirty = true;
        }
        else ++i;
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s", scene_flow_editor_status.c_str());
    ImGui::End();

    // X で閉じた場合も未保存の編集を失わない。
    if (!show_scene_flow_panel && scene_flow_editor_dirty) save_scene_flow_editor();
#endif
}
