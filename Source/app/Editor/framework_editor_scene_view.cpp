// Editor のうち「Scene View 後半（View / Search / Hierarchy）」を持つ。
// Scene View の描画・検索・Hierarchy 操作の関数本体はそのまま移動している。
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
#include "skinned_mesh.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

void framework::draw_scene_view_panel()
{
    scene_view_hovered = false;
    scene_view_focused = false;
    if (!show_scene_view) return;

    if (scene_view_overlay_valid)
    {
        ImGui::SetNextWindowPos(scene_view_overlay_position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(scene_view_overlay_size, ImGuiCond_Always);
    }
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    const ImGuiWindowFlags scene_view_flags = ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("Scene View", &show_scene_view, scene_view_flags))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("SceneGameTabs"))
    {
        const ImGuiTabItemFlags scene_flags =
            editor_view_tab_sync_pending && active_editor_view == editor_view::scene
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        const ImGuiTabItemFlags game_flags =
            editor_view_tab_sync_pending && active_editor_view == editor_view::game
                ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(u8"シーン", nullptr, scene_flags))
        {
            active_editor_view = editor_view::scene;
            remember_active_editor_view();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8"ゲーム", nullptr, game_flags))
        {
            active_editor_view = editor_view::game;
            remember_active_editor_view();
            ImGui::EndTabItem();
        }
        editor_view_tab_sync_pending = false;
        ImGui::EndTabBar();
    }

    if (active_editor_view == editor_view::scene)
    {
        if (active_editor_workspace == editor_workspace::ui)
        {
            const char* resolutions[] = {
                "1920 x 1080", "1280 x 720", "1080 x 1920", u8"カスタム"
            };
            ImGui::SetNextItemWidth(140.0f);
            ImGui::Combo("##UISceneResolution", &ui_preview_resolution_index,
                resolutions, IM_ARRAYSIZE(resolutions));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(105.0f);
            ImGui::SliderFloat(u8"拡大", &ui_preview_zoom, 0.10f, 2.0f, "%.2f");
            ImGui::SameLine();
            ImGui::Checkbox(u8"グリッド", &ui_preview_grid);
            if (ui_preview_resolution_index == 3)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                ImGui::InputInt("W##UISceneCustomWidth", &ui_preview_custom_width, 0, 0);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70.0f);
                ImGui::InputInt("H##UISceneCustomHeight", &ui_preview_custom_height, 0, 0);
                ui_preview_custom_width = (std::max)(1, ui_preview_custom_width);
                ui_preview_custom_height = (std::max)(1, ui_preview_custom_height);
            }
        }
        else
        {
            const char* modes[] = {
                u8"陰影付き", u8"陰影なし", u8"ワイヤーフレーム",
                u8"陰影付きワイヤーフレーム", u8"衝突"
            };
            ImGui::SetNextItemWidth(150.0f);
            ImGui::Combo("##SceneDrawMode", &scene_view_draw_mode, modes, IM_ARRAYSIZE(modes));
            ImGui::SameLine();
            ImGui::Checkbox(u8"コライダー", &show_collider_debug_draw);
            ImGui::SameLine();
            ImGui::Checkbox(u8"グリッド", &show_scene_grid);
            ImGui::SameLine();
            ImGui::TextDisabled("Perspective | %s | %s | %s",
                gizmo_local_space ? "Local" : "World",
                transform_gizmo.SnapEnabled() ? "Snap" : "Free",
                object_scene_play_mode ? (object_scene_paused ? "Paused" : "Playing") : "Editing");
        }
        if (active_editor_workspace != editor_workspace::ui)
        {
            ensure_editor_camera_presets_loaded();
            ImGui::TextDisabled(u8"Camera preset: %s | カメラ > プリセット管理 で操作を自由設定",
                active_editor_camera_preset().name.c_str());
            // Landscape を選択しているときだけ専用 Tool を出す。
            // Component 自体に ImGui / Editor 状態を持たせない。
            draw_landscape_editor_toolbar();
            if (landscape_edit_enabled)
            {
                const char* landscape_tool = landscape_edit_mode == 0
                    ? u8"Landscape / Sculpt"
                    : (landscape_topology_selection_mode == 0
                        ? u8"Landscape / Topology Face"
                        : u8"Landscape / Topology Edge");
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                    u8"ACTIVE TOOL: %s  (Escで終了 / Ctrl・Alt+左クリックで通常選択)", landscape_tool);
            }
            else
            {
                ImGui::TextDisabled("ACTIVE TOOL: Transform / Scene Selection");
            }
        }
    }
    else
    {
        ImGui::TextDisabled("Runtime Camera | Free Aspect");
        if (!object_scene_play_mode)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f),
                "PlayでRuntime Sceneをプレビュー");
        }
    }

    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1.0f) size.x = 1.0f;
    if (size.y < 1.0f) size.y = 1.0f;
    ImGui::InvisibleButton("##SceneViewportSurface", size);
    const ImVec2 maximum{ minimum.x + size.x, minimum.y + size.y };
    scene_view_min_x = minimum.x;
    scene_view_min_y = minimum.y;
    scene_view_max_x = maximum.x;
    scene_view_max_y = maximum.y;
    scene_view_hovered = ImGui::IsItemHovered();
    scene_view_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    draw_ui_scene_overlay();

    // 右クリックの Play From Here / Checkpoint / Scene Memo。
    // InvisibleButton の直後に置くことで ContextItem の対象を確実に Viewport にする。
    draw_play_from_here_context_menu();

    if (active_editor_view == editor_view::scene && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_ASSET_GUID"))
        {
            if (payload->Data != nullptr && payload->DataSize > 1)
            {
                const std::string guid(static_cast<const char*>(payload->Data));
                if (const auto* asset = asset_database.FindByGuid(guid))
                {
                    DirectX::XMFLOAT3 drop_position{};
                    const bool has_drop_position = scene_view_mouse_world_point(drop_position, nullptr);
                    ReplayEngine::Core::ObjectID drop_target = ReplayEngine::Core::ObjectID::Invalid();
                    const ImVec2 mouse = ImGui::GetMousePos();
                    const float local_x = mouse.x - scene_view_min_x;
                    const float local_y = mouse.y - scene_view_min_y;
                    if (local_x >= 0.0f && local_y >= 0.0f)
                    {
                        const auto ray = viewport_picking_ray(local_x, local_y);
                        drop_target = ReplayEngine::Editor::ViewportPicker::Pick(
                            object_scene, ray.origin, ray.direction);
                    }
                    place_asset_in_object_scene(*asset, asset_drop_add_collider,
                        has_drop_position ? &drop_position : nullptr, drop_target);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    draw_scene_note_overlay();
    ImGui::End();
}

void framework::draw_search_results()
{
    if (editor_search_text[0] == '\0') return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({ viewport->Pos.x + 410.0f, viewport->Pos.y + 38.0f }, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.98f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("##SearchResultsOverlay", nullptr, flags);
    ImGui::TextDisabled("機能検索結果");
    ImGui::Separator();

    std::string query = editor_search_text;
    std::transform(query.begin(), query.end(), query.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto matches = [&query](const char* keywords)
    {
        std::string text = keywords;
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text.find(query) != std::string::npos;
    };
    const auto result = [this, &matches](const char* label, const char* keywords,
        editor_selection selection)
    {
        if (matches(keywords) && ImGui::Selectable(label))
        {
            selected_editor_object = selection;
            editor_search_text[0] = '\0';
            search_input_active = false;
        }
    };
    result("ワールドを編集", "world ワールド 背景", editor_selection::world);
    result("カメラを編集", "camera カメラ 視点", editor_selection::camera);
    if (matches("player プレイヤー 操作 キャラクター character") &&
        ImGui::Selectable("操作対象 GameObject を選択"))
    {
        ReplayEngine::Scene::Scene& scene = active_object_scene();
        const ReplayEngine::Core::ObjectID controlled = scene.Services().ControlledObject();
        if (controlled.Valid() && scene.FindGameObjectByID(controlled) != nullptr)
        {
            object_editor_context.Selection().Select(controlled, false);
            selected_editor_object = editor_selection::game_object;
        }
        else object_editor_context.SetStatus("操作対象 GameObject が設定されていません");
        editor_search_text[0] = '\0';
        search_input_active = false;
    }
    result("描画設定を開く", "render rendering 描画 deferred shader", editor_selection::rendering);
    result("ポスト処理を開く", "post process ポスト bloom fxaa", editor_selection::post_process);
    if (matches("input capture editor 入力 キャプチャ") && ImGui::Selectable("Editor入力キャプチャを切り替える"))
    {
        set_edit_mode(!edit_mode_active);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace モデリング modeling") && ImGui::Selectable("モデリングWorkspaceへ"))
    {
        set_editor_workspace(editor_workspace::modeling);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace 配置 placement place") && ImGui::Selectable("配置Workspaceへ"))
    {
        set_editor_workspace(editor_workspace::placement);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace アニメーション animation") && ImGui::Selectable("アニメーションWorkspaceへ"))
    {
        set_editor_workspace(editor_workspace::animation);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace レンダリング rendering") && ImGui::Selectable("レンダリングWorkspaceへ"))
    {
        set_editor_workspace(editor_workspace::rendering);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace ui canvas userinterface ユーザーインターフェイス") &&
        ImGui::Selectable("UI Workspaceへ"))
    {
        set_editor_workspace(editor_workspace::ui);
        editor_search_text[0] = '\0';
    }
    if (matches("workspace motion timeline keyframe モーション タイムライン") &&
        ImGui::Selectable("Motion Workspaceへ"))
    {
        set_editor_workspace(editor_workspace::motion);
        editor_search_text[0] = '\0';
    }
    if (matches("shader material preset シェーダー 材質 プリセット") &&
        ImGui::Selectable("シェーダー調整テーブルへ"))
    {
        set_editor_workspace(editor_workspace::shader_adjustment);
        editor_search_text[0] = '\0';
    }
    if (matches("fullscreen 全画面 フルスクリーン") && ImGui::Selectable("全画面表示を切り替える"))
    {
        toggle_fullscreen();
        editor_search_text[0] = '\0';
    }
    ImGui::End();
}

void framework::draw_scene_hierarchy()
{
    ImGui::Begin("階層");
    const auto item = [this](const char* label, editor_selection value)
    {
        if (ImGui::Selectable(label, selected_editor_object == value)) selected_editor_object = value;
    };
    if (ImGui::TreeNodeEx("ゲームシーン", ImGuiTreeNodeFlags_DefaultOpen))
    {
        item("ワールド", editor_selection::world);
        item("メインカメラ", editor_selection::camera);
        // 「プレイヤー」という固定項目は無い。
        // 操作キャラクターは下の GameObject ツリーへ通常の GameObject として現れ、
        // 名前も自由に変えられる。同じ対象を 2 か所から編集できる状態は作らない。
        item("描画設定", editor_selection::rendering);
        item("ポスト処理", editor_selection::post_process);
        ImGui::TreePop();
    }

    ImGui::Separator();

    // GameObject / Component 基盤のツリー。
    // 表示・選択・作成・削除・複製・親子変更は HierarchyPanel が担当する。
    // framework 側は「選択が変わったらインスペクターの表示先を切り替える」だけ。
    if (ImGui::TreeNodeEx("GameObject", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // 操作対象が設定されていない Scene であることを伝えるだけ。
        // ここで何かを生成したり、代わりの GameObject を選んだりはしない。
        if (object_missing_controlled_target)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                "このシーンには操作対象が設定されていません");
            ImGui::TextDisabled(
                "GameObject を選び、インスペクターの「操作対象に設定」で指定してください");
            ImGui::Separator();
        }

        object_editor_context.SetPlayMode(object_scene_play_mode);
        object_editor_context.AttachScene(&active_object_scene());

        const ReplayEngine::Core::ObjectID before = object_editor_context.Selection().Primary();
        object_hierarchy_panel.DrawContents(object_editor_context);
        const ReplayEngine::Core::ObjectID after = object_editor_context.Selection().Primary();

        if (after.Valid() && after != before)
        {
            selected_editor_object = editor_selection::game_object;
        }
        ImGui::TreePop();
    }

    ImGui::End();
}
