// Editor の補助機能のうち「Scene Note」だけを持つ。
//
//   framework_convenience_tools.cpp             … Scene View 座標と Scene Note（このファイル）
//   framework_convenience_tools_play.cpp        … Play From Here
//   framework_convenience_tools_scene_flow.cpp  … Scene Flow Asset 編集

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
    const char* NoteCategoryName(int category) noexcept
    {
        static const char* names[] = { "TODO", "BUG", "ART", "PROGRAM", "LEVEL", "IDEA" };
        return names[(std::max)(0, (std::min)(category, 5))];
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

    const auto ray = viewport_picking_ray(local_x, local_y);
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
    const float scene_width = scene_view_max_x - scene_view_min_x;
    const float scene_height = scene_view_max_y - scene_view_min_y;
    if (scene_width <= 1.0f || scene_height <= 1.0f) return;

    using namespace DirectX;
    const XMMATRIX view = viewport_view_matrix();
    const XMMATRIX projection = viewport_projection_matrix();
    POINT client_origin{ 0, 0 };
    ClientToScreen(hwnd, &client_origin);
    const float width = (std::max)(1.0f, static_cast<float>(client_width));
    const float height = (std::max)(1.0f, static_cast<float>(client_height));
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

            const ImVec2 anchor{ static_cast<float>(client_origin.x) + screen.x,
                static_cast<float>(client_origin.y) + screen.y };
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
