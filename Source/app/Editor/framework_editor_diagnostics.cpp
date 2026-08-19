// Editor のうち「Runtime Mode 表示と操作キャラクター診断」だけを持つ。
#include "framework.h"

#include "../../RePlayEngine/Components/Core/PivotComponent.h"
#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerControllerComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerInputComponent.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <string>
void framework::draw_runtime_mode_banner()
{
    ImGui::Separator();

    if (object_scene_play_mode)
    {
        const ImVec4 color = object_scene_paused
            ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
            : ImVec4(0.4f, 0.95f, 0.5f, 1.0f);
        ImGui::TextColored(color, object_scene_paused
            ? u8"❚❚ 一時停止中" : u8"▶ 実行中");
        ImGui::TextDisabled(object_scene_paused
            ? u8"実行シーンを一時停止中"
            : u8"実行シーンで動作中 / 入力有効 / C# の Update が回っています");
    }
    else if (object_runtime_active())
    {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), u8"▶ 実行中（編集シーン）");
        ImGui::TextDisabled(u8"編集シーンをそのまま実行中 / 入力有効");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), u8"■ 編集中（停止）");
        ImGui::TextDisabled(u8"物理・入力・C# スクリプトはすべて停止中");
        ImGui::TextDisabled(u8"動かすには上の緑の「▶ 実行」ボタン、または F5");
    }

#ifdef _DEBUG
    // ウィンドウがアクティブでないと GetAsyncKeyState が拾えないことがある。
    if (::GetForegroundWindow() != hwnd)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
            "ゲーム画面をクリックすると入力を受け取ります");
    }
#endif
}
void framework::draw_editor_camera_gate_diagnostics()
{
    // Scene View のカメラ操作が全部死ぬときの切り分け用。
    //
    // 【なぜ要るか】
    //   ズーム・Pan・Orbit・Fly はすべて EditorCameraController の
    //   CanBeginInteraction() という 1 つの関門を通る。ここが false になると
    //   カメラが一切動かなくなるが、7 つの条件のどれが原因か外から見えない。
    //   2026-08-17 に実際にカメラが死に、推測で候補を潰すしかなくなった。
    //
    //   立っている条件を出しておけば、次は 5 秒で切り分けられる。
    if (!ImGui::CollapsingHeader(u8"カメラ操作の関門")) return;

    const ReplayEngine::Editor::EditorCameraInput& input = last_editor_camera_input;

    struct Gate final { const char* name; bool blocking; };
    const Gate gates[]{
        { u8"ウィンドウが非アクティブ",            !input.window_focused },
        { u8"Scene View に hover / focus が無い",
          !input.viewport_hovered && !input.viewport_focused },
        { u8"UI がマウスを要求 (ui_wants_mouse)",   input.ui_wants_mouse },
        { u8"UI がキーボードを要求",                input.ui_wants_keyboard },
        { u8"テキスト入力中 (ui_text_input_active)", input.ui_text_input_active },
        { u8"ポップアップが開いている",             input.ui_popup_open },
        { u8"Gizmo / 範囲選択のドラッグ中",         input.gizmo_dragging },
    };

    bool any = false;
    for (const Gate& gate : gates)
    {
        if (gate.blocking)
        {
            any = true;
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                u8"✕ %s", gate.name);
        }
        else
        {
            ImGui::TextDisabled(u8"○ %s", gate.name);
        }
    }

    if (any)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
            u8"→ 上の ✕ が原因でカメラ操作が止まっています");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f),
            u8"→ 関門は開いています（カメラは操作できる状態）");
    }

    ImGui::Separator();
    ImGui::TextDisabled(u8"内訳: WantTextInput=%s / search_input_active=%s",
        ImGui::GetIO().WantTextInput ? "true" : "false",
        search_input_active ? "true" : "false");
    ImGui::TextDisabled(u8"内訳: IsAnyItemActive=%s / viewport_drag_selecting=%s",
        ImGui::IsAnyItemActive() ? "true" : "false",
        viewport_drag_selecting ? "true" : "false");
    ImGui::TextDisabled(u8"wheel=%.2f", input.wheel);
}

void framework::draw_controlled_character_diagnostics()
{
#ifdef _DEBUG
    // Debug ビルドでのみ表示する。Release へ診断処理を残さない。
    if (!ImGui::CollapsingHeader("Controlled Character Diagnostics")) return;

    namespace Components = ReplayEngine::Components;
    const ReplayEngine::Scene::Scene& scene = active_object_scene();

    ImGui::Text("Mode: %s", object_scene_play_mode ? "Play"
        : (object_runtime_active() ? "Running" : "Edit"));
    ImGui::Text("Runtime active: %s", object_runtime_active() ? "true" : "false");
    ImGui::Text("Fixed accumulator: %.4f", object_fixed_accumulator);
    ImGui::Text("Render items: %zu", object_render_items.Size());

    const ReplayEngine::Core::ObjectID controlled = scene.Services().ControlledObject();
    const ReplayEngine::Core::GameObject* target =
        controlled.Valid() ? scene.FindGameObjectByID(controlled) : nullptr;

    if (target == nullptr)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
            "このシーンには操作対象が設定されていません");
        ImGui::TextDisabled("インスペクターの「操作対象に設定」で指定してください");
        return;
    }

    ImGui::Text("Controlled Object: %s (ObjectID %s)",
        target->Name().c_str(), controlled.ToString().c_str());

    if (const auto* input = target->GetComponent<Components::PlayerInputComponent>())
    {
        ImGui::Text("Input enabled: %s", input->ActiveInHierarchy() ? "true" : "false");
        ImGui::Text("Input X/Y: %.2f / %.2f", input->MoveX(), input->MoveY());
        ImGui::Text("Jump latched: %s", input->JumpLatched() ? "true" : "false");
    }
    else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Player Input: なし");

    if (const auto* controller = target->GetComponent<Components::PlayerControllerComponent>())
    {
        ImGui::Text("Controller enabled: %s", controller->ActiveInHierarchy() ? "true" : "false");
        if (!controller->HasRequiredComponents())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "  %s",
                controller->MissingRequirementText());
    }
    else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Player Controller: なし");

    if (const auto* motor = target->GetComponent<Components::CharacterMotorComponent>())
    {
        ImGui::Text("Motor enabled: %s", motor->ActiveInHierarchy() ? "true" : "false");
        const auto& velocity = motor->Velocity();
        ImGui::Text("Velocity: %.2f / %.2f / %.2f", velocity.x, velocity.y, velocity.z);
        ImGui::Text("Grounded: %s", motor->Grounded() ? "true" : "false");
        ImGui::Text("Vertical physics: %s", motor->vertical_physics ? "true" : "false");
    }
    else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Character Motor: なし");

    const auto position = target->GetTransform().WorldPosition();
    ImGui::Text("Position: %.2f / %.2f / %.2f", position.x, position.y, position.z);

    // ---- 衝突の出所 -------------------------------------------------------
    //
    // 「今どちらのバックエンドで当たっているか」が分からないと、
    // MeshCollider を置いても効いているのか判断できない。
    ImGui::Separator();
    ImGui::Text("Collision available: %s",
        object_collision_world.CollisionAvailable() ? "true" : "false");
    ImGui::TextUnformatted("Backend mode: Scene Colliders Only");
    ImGui::Text("Active colliders: %zu (blocking %zu / trigger %zu / mesh %zu)",
        object_collision_world.ActiveColliderCount(),
        object_collision_world.BlockingColliderCount(),
        object_collision_world.TriggerColliderCount(),
        object_collision_world.MeshColliderCount());
    const auto& ground_source = object_collision_world.LastGroundSource();
    const auto& sweep_source = object_collision_world.LastSweepSource();
    ImGui::Text("Ground hit from: %s (Object %s / Collider %u)",
        ReplayEngine::Scene::ToString(ground_source.backend),
        ground_source.object.ToString().c_str(), ground_source.collider);
    ImGui::Text("Wall hit from: %s (Object %s / Collider %u)",
        ReplayEngine::Scene::ToString(sweep_source.backend),
        sweep_source.object.ToString().c_str(), sweep_source.collider);

    if (ImGui::Button("衝突の診断ウィンドウを開く")) show_collision_diagnostics = true;
    ImGui::SameLine();
    ImGui::Checkbox(u8"コライダーを描画", &show_collider_debug_draw);
#endif
}
