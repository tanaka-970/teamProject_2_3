// Editor のうち「Project / Console / Workspace パネル」だけを持つ。
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
void framework::draw_project_panel()
{
    ImGui::Begin("プロジェクト");
    // GameObject Scene (.replayscene) is the only authoring format.
    if (ImGui::CollapsingHeader("GameObject シーン", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("現在: %s", active_object_scene().Name().c_str());
        ImGui::TextDisabled("%s", object_scene_path.generic_u8string().c_str());
        draw_new_object_scene_controls();
        ImGui::SameLine();
        if (ImGui::Button("Prefabとして保存...")) save_selected_prefab(true);
        ImGui::TextDisabled("%s", object_editor_context.Status().c_str());
        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader("C# Scripts", ImGuiTreeNodeFlags_DefaultOpen))
    {
        namespace CSharp = ReplayEngine::Scripting::CSharp;

        ImGui::TextDisabled(u8"C# Script の作成は Project Browser の右クリック > Create に統一しました。");

        // 一括更新をいちばん目立つ位置へ置く。
        // Catalog 更新と再コンパイルを分けて覚えるのは negligible な区別で、
        // 実際には「とりあえず全部更新したい」がほとんど。
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.48f, 0.72f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.60f, 0.86f, 1.0f));
        if (ImGui::Button(u8"C# をすべて更新", ImVec2(150.0f, 0.0f))) rebuild_all_csharp();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(u8"Catalog の更新と Assembly の再コンパイルを"
                u8"まとめて行います。\n迷ったらこれを押してください。");
        }

        ImGui::SameLine();
        ImGui::Checkbox(u8"自動更新", &csharp_auto_reload);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(u8".cs を保存すると自動で再コンパイルします。\n"
                u8"失敗しても直前に成功した Assembly を使い続けるので、\n"
                u8"編集中の状態は壊れません。");
        }

        ImGui::SameLine();
        if (ImGui::Button("Open Selected .cs")) open_selected_csharp_asset();

        if (csharp_scripts_dirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.74f, 0.28f, 1.0f),
                csharp_auto_reload ? u8"変更検出 → まもなく更新"
                                   : u8"変更検出済み（自動更新は無効）");
        }

        // 個別操作は畳んでおく。普段は使わない。
        if (ImGui::TreeNode(u8"個別に実行"))
        {
            if (ImGui::Button("Refresh C# Catalog")) refresh_csharp_scripts();
            ImGui::SameLine();
            if (ImGui::Button("Build && Reload C#")) build_and_reload_csharp_scripts();
            ImGui::TreePop();
        }
        ImGui::TextDisabled("%s",
            CSharp::CSharpProject::GameScriptsProjectPath(
                content_root_path()).generic_u8string().c_str());
        ImGui::TextDisabled("%s",
            CSharp::CSharpProject::GameScriptsSolutionPath(
                content_root_path()).generic_u8string().c_str());
        ImGui::Separator();
    }

    if (ImGui::Button("モデルファイルを取り込む...")) browse_model_asset();
    ImGui::SameLine();
    if (ImGui::Button("Prefabを配置...")) load_prefab();
    ImGui::SameLine();
    ImGui::TextDisabled("FBXキャッシュ / GLB / glTF");
    if (async_stage_load_active)
    {
        ImGui::ProgressBar(async_asset_manager.Progress(), ImVec2(-1.0f, 0.0f), "モデルを確認中");
    }
    if (!selected_model_asset_path.empty())
    {
        ImGui::TextWrapped("選択中: %s", selected_model_asset_path.c_str());
        ImGui::TextWrapped("状態: %s", model_asset_status.c_str());
        if (!selected_model_cache_path.empty())
            ImGui::TextWrapped("キャッシュ: %s", selected_model_cache_path.c_str());
    }
    ImGui::Separator();
    ImGui::TextDisabled("Asset 作成は Project Browser の右クリック > Create から行います");
    ImGui::Separator();
    ImGui::Text("Assets: %zu", asset_database.Records().size());

    // Project ブラウザ本体 (Source/app/Editor/framework_project_browser.cpp)。
    // フォルダツリー + そのフォルダの中身。作成・改名・D&D はそちら側。
    draw_project_browser();

    ImGui::Checkbox("SceneへModel配置時にMesh Colliderも追加", &asset_drop_add_collider);
    ImGui::SameLine();
    const auto* selected_asset = selected_asset_guid.empty()
        ? nullptr : asset_database.FindByGuid(selected_asset_guid);
    if (selected_asset != nullptr && ImGui::Button("選択AssetをSceneへ配置"))
        place_asset_in_object_scene(*selected_asset, asset_drop_add_collider);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Colliderは左の設定が有効な場合だけ明示的に追加します");
    if (selected_asset != nullptr &&
        (selected_asset->kind == ReplayEngine::Assets::AssetKind::Script ||
            selected_asset->source_path.extension() == ".cs"))
    {
        ImGui::SameLine();
        if (ImGui::Button("Visual Studioで開く")) open_selected_csharp_asset();
    }
    if (selected_asset != nullptr &&
        selected_asset->kind == ReplayEngine::Assets::AssetKind::Shader)
    {
        ImGui::SameLine();
        if (ImGui::Button("ShaderをVisual Studioで開く"))
        {
            std::string open_error;
            if (!ReplayEngine::Scripting::CSharp::CSharpProject::OpenVisualStudio(
                selected_asset->source_path, 1, open_error))
            {
                push_editor_log("Warning", open_error, selected_asset->source_path);
            }
        }
    }
    draw_material_asset_editor();
    ImGui::Separator();
    ImGui::TextDisabled("現在のシーンが読み込んでいる素材");
    ImGui::BulletText("キャラクター / Renderer Component の AssetGUID から解決");
    ImGui::BulletText("Stage / Model / Prefab / Material Asset");
    ImGui::BulletText("IBL / 拡散・鏡面・BRDF LUT");
    ImGui::BulletText("シェーダー / PBR・Toon・Deferred・PostProcess");
    ImGui::End();
}
void framework::draw_console_panel()
{
    ImGui::Begin("コンソール");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##EditorCommand", "> コマンドを入力...",
        editor_command_text, IM_ARRAYSIZE(editor_command_text), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        execute_editor_command(editor_command_text);
        editor_command_text[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::TextWrapped("%s", editor_command_result.c_str());
    ImGui::Separator();
    if (ImGui::Button(u8"ログを消去"))
    {
        editor_log_entries.clear();
        selected_editor_log_index = -1;
    }
    ImGui::SameLine();
    ImGui::Text(u8"エディタログ: %zu", editor_log_entries.size());
    if (ImGui::BeginChild("EditorLogEntries", ImVec2(0.0f, 150.0f), true))
    {
        for (int index = 0; index < static_cast<int>(editor_log_entries.size()); ++index)
        {
            const editor_log_entry& entry = editor_log_entries[index];
            ImVec4 color{ 0.78f, 0.82f, 0.90f, 1.0f };
            if (entry.severity == "Error") color = { 1.0f, 0.43f, 0.38f, 1.0f };
            else if (entry.severity == "Warning") color = { 1.0f, 0.74f, 0.28f, 1.0f };
            else if (entry.severity == "Info") color = { 0.56f, 0.84f, 1.0f, 1.0f };

            std::string label = "[" + entry.severity + "] " + entry.message;
            if (!entry.file.empty())
            {
                label += " (" + entry.file.filename().generic_u8string();
                if (entry.line > 0) label += ":" + std::to_string(entry.line);
                label += ")";
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            const bool selected = selected_editor_log_index == index;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                selected_editor_log_index = index;
                if (!entry.file.empty())
                {
                    std::string error;
                    ReplayEngine::Scripting::CSharp::CSharpProject::OpenVisualStudio(
                        entry.file, entry.line, error);
                    if (!error.empty()) editor_command_result = error;
                }
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && !entry.file.empty())
            {
                ImGui::SetTooltip("%s", entry.file.generic_u8string().c_str());
            }
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextColored({ 0.45f, 0.85f, 0.55f, 1.0f }, "[正常] RePlayランタイム動作中");
    ImGui::Text("Editor入力: %s", edit_mode_active ? "編集操作" : "Game View入力キャプチャ");
    ImGui::Text("画面サイズ: %u x %u", client_width, client_height);
    ImGui::TextUnformatted("描画方式: Deferred（固定）");
    const char* outputs[] = { "Final", "HDR Scene", "Bloom", "Deferred Lit",
        "GBuffer Base Color", "GBuffer Normal", "GBuffer Material", "Depth" };
    ImGui::Text("出力: %s", outputs[render_graph.OutputIndex()]);
    ImGui::TextDisabled("Ctrl+S: 保存  Ctrl+Z/Y: 元に戻す/やり直す  Ctrl+C/V: コピー/貼り付け  Ctrl+D: 複製");
    ImGui::TextDisabled("F1: エディタ表示  F2: 名前変更  Ctrl+F2: 出力  F3: 入力キャプチャ  F5: 実行  F11: 全画面");
    ImGui::End();
}

void framework::draw_workspace_panel()
{
    ImGui::Begin("ワークスペース");
    switch (active_editor_workspace)
    {
    case editor_workspace::placement:
        ImGui::TextUnformatted("オブジェクト配置Workspace");
        ImGui::TextDisabled("ProjectのAsset BrowserからModelやPrefabをScene Viewへ配置します。");
        break;
    case editor_workspace::modeling:
        ImGui::TextUnformatted("モデリングWorkspace");
        ImGui::TextDisabled("形状編集用のテーブルです。配置操作は配置Workspaceへ分離しています。");
        if (ImGui::Button("選択GameObjectを編集")) selected_editor_object = editor_selection::game_object;
        ImGui::SameLine();
        if (ImGui::Button("配置Workspaceへ")) set_editor_workspace(editor_workspace::placement);
        break;
    case editor_workspace::animation:
        ImGui::TextUnformatted("アニメーションWorkspace");
        ImGui::TextDisabled("今後、タイムラインとアニメーション編集をここへ登録します。");
        ImGui::TextDisabled(
            "クリップの割り当てと再生は、対象 GameObject の Animator で編集します。");
        break;
    case editor_workspace::rendering:
        ImGui::TextUnformatted("レンダリングWorkspace");
        ImGui::TextDisabled("今後、RenderGraph・シェーダー・Profilerをここへ登録します。");
        if (ImGui::Button("描画設定を開く")) selected_editor_object = editor_selection::rendering;
        ImGui::SameLine();
        ImGui::TextUnformatted("Renderer: Deferred（固定）");
        break;
    case editor_workspace::shader_adjustment:
        ImGui::TextUnformatted("シェーダー調整Workspace");
        ImGui::TextDisabled("右上の専用テーブルで材質、合成順、プリセット、画面効果を編集します。");
        ImGui::TextUnformatted("方式: 型付きパラメータ + 順序付き追加パス");
        ImGui::TextDisabled("シェーダーグラフを使わず、安全な範囲で表現を組み合わせます。");
        break;
    case editor_workspace::ui:
        ImGui::TextUnformatted("UI Workspace");
        ImGui::TextDisabled("Canvas、RectTransform、UI Component を編集します。");
        if (ImGui::Button("UI 階層を開く")) show_ui_hierarchy_panel = true;
        ImGui::SameLine();
        if (ImGui::Button("Canvas プレビューを開く")) show_ui_preview_panel = true;
        break;
    case editor_workspace::motion:
        ImGui::TextUnformatted("Motion Workspace");
        ImGui::TextDisabled("Motion Asset、キー、プレビューを編集します。");
        if (ImGui::Button("Motion レイヤーを開く")) show_motion_layers_panel = true;
        ImGui::SameLine();
        if (ImGui::Button("タイムラインを開く")) show_motion_timeline_panel = true;
        break;
    default:
        ImGui::TextUnformatted("基本Workspace");
        ImGui::TextDisabled("シーン編集と実行状態の確認を行います。");
        if (ImGui::Button("ワールドを選択")) selected_editor_object = editor_selection::world;
        break;
    }
    ImGui::End();
}
