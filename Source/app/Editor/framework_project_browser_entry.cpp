#include "framework.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Localization/LocalizationTable.h"
#include "../../RePlayEngine/Rendering/Effects/EffectPresetAsset.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Motion/CompositionAsset.h"
#include "../../RePlayEngine/Motion/MotionAsset.h"
#include "../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../RePlayEngine/Rendering/Shaders/ShaderAssetFactory.h"
#include "../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerAsset.h"
#include "../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerGenerator.h"
#include "../../RePlayEngine/Runtime/Scene/SceneFlowAsset.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include "framework_project_browserInternal.h"
using namespace framework_project_browser::Detail;

// Project Browser 入口の関数本体

void framework::draw_project_browser()
{
    project_browser_focused = false;
    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error)
    {
        ImGui::TextDisabled("プロジェクトフォルダを取得できません");
        return;
    }
    // 子ペインだけでなくProject Window本体・検索欄・Popupから戻った直後も
    // ショートカット所有者として扱う。文字入力中はWndProc側のWantTextInputで除外。
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        project_browser_focused = true;

    // --- パンくず ---
    if (ImGui::SmallButton("Project"))
    {
        project_current_folder.clear();
        project_selected_entry_path = root;
        selected_editor_object = editor_selection::asset;
        selected_asset_guid.clear();
        project_tree_reveal_selection_pending = true;
    }
    ReplayEngine::Editor::EditorHelp::Item("button.project.breadcrumb_root");
    std::filesystem::path walked;
    for (const std::filesystem::path& part : project_current_folder)
    {
        walked /= part;
        ImGui::SameLine();
        ImGui::TextDisabled("/");
        ImGui::SameLine();
        ImGui::PushID(walked.generic_u8string().c_str());
        if (ImGui::SmallButton(part.u8string().c_str()))
        {
            set_project_folder(root / walked);
            project_selected_entry_path = root / walked;
            selected_editor_object = editor_selection::asset;
            selected_asset_guid.clear();
            project_tree_reveal_selection_pending = true;
        }
        ReplayEngine::Editor::EditorHelp::Item("button.project.breadcrumb_folder");
        ImGui::PopID();
    }

    // --- 検索とフィルタ ---
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##ProjectSearch", "Search Project...",
        asset_search_text, IM_ARRAYSIZE(asset_search_text));
    ReplayEngine::Editor::EditorHelp::Item("control.project.search",
        u8"Project 全体を再帰検索します");
    ImGui::SameLine();
    const char* filters[] =
        { "All", "Model", "Prefab", "Scene", "Material", "Script", "Shader", "Flow", "Motion", "Font", "Localization", "EffectPreset", "Input", "Other", "Easing" };
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##ProjectFilter", &asset_type_filter, filters, IM_ARRAYSIZE(filters));
    ImGui::SameLine();
    ImGui::Checkbox("グリッド", &project_grid_view);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("##ProjectThumbSize", &project_thumbnail_size,
        48.0f, 160.0f, "%.0f px");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh Assets"))
    {
        std::string scan_error;
        const std::size_t registered = register_resource_assets(scan_error);
        project_browser_status = "resources を更新しました: " +
            std::to_string(registered) + "件追加";
        if (!scan_error.empty())
        {
            project_browser_status += " / " + scan_error;
            push_editor_log("Warning", scan_error);
        }
    }
    ReplayEngine::Editor::EditorHelp::Item("button.project.refresh_assets");

    ImGui::Separator();

    // Project Window全高を使う。以前の固定320pxだと下半分が空き、
    // Treeの深い階層ほど操作しづらかった。
    const float status_reserve = project_browser_status.empty() ? 8.0f : 46.0f;
    const float browser_height = (std::max)(220.0f,
        ImGui::GetContentRegionAvail().y - status_reserve);
    const float total_width = ImGui::GetContentRegionAvail().x;
    project_tree_width = (std::max)(150.0f,
        (std::min)(project_tree_width, (std::max)(150.0f, total_width - 190.0f)));

    // --- 左: 完全なProject Tree（フォルダ + ファイル） ---
    if (ImGui::BeginChild("##ProjectTree", ImVec2(project_tree_width, browser_height), true))
    {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            project_browser_focused = true;

        ImGuiTreeNodeFlags root_flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (project_selected_entry_path.empty() ||
            NormalizeProjectPath(project_selected_entry_path) == NormalizeProjectPath(root))
            root_flags |= ImGuiTreeNodeFlags_Selected;
        if (project_tree_reveal_selection_pending) ImGui::SetNextItemOpen(true, ImGuiCond_Always);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.80f, 0.36f, 1.0f));
        const bool root_open = ImGui::TreeNodeEx("Project", root_flags);
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            project_current_folder.clear();
            project_selected_entry_path = root;
            selected_editor_object = editor_selection::asset;
            selected_asset_guid.clear();
            project_browser_focused = true;
        }
        if (ImGui::BeginPopupContextItem("##ProjectRootMenu"))
        {
            project_selected_entry_path = root;
            selected_editor_object = editor_selection::asset;
            selected_asset_guid.clear();
            project_browser_focused = true;
            draw_project_create_submenu(root);
            ImGui::Separator();
            if (ImGui::MenuItem("エクスプローラーで表示")) project_show_in_explorer(root);
            if (ImGui::MenuItem("絶対パスをコピー")) project_copy_path(root, true);
            ImGui::EndPopup();
        }
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_PROJECT_PATH"))
            {
                const char* source = static_cast<const char*>(payload->Data);
                if (source != nullptr) project_move_entry(std::filesystem::u8path(source), root);
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_ASSET_GUID"))
            {
                const char* guid = static_cast<const char*>(payload->Data);
                if (guid != nullptr)
                    if (const auto* dragged = asset_database.FindByGuid(guid))
                        project_move_entry(dragged->source_path, root);
            }
            ImGui::EndDragDropTarget();
        }
        if (root_open)
        {
            draw_project_folder_tree(root, 0);
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    // このFrame開始時までに要求されていたRevealは左Treeで消費済み。
    // 右ペインでこの後選択されたAssetの要求は次Frameまで残す。
    project_tree_reveal_selection_pending = false;

    // ドラッグ可能なSplitter。Project Treeを広げたい時に直接掴める。
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::InvisibleButton("##ProjectPaneSplitter", ImVec2(6.0f, browser_height));
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive())
    {
        project_tree_width += ImGui::GetIO().MouseDelta.x;
        project_tree_width = (std::max)(150.0f,
            (std::min)(project_tree_width, (std::max)(150.0f, total_width - 190.0f)));
    }
    ImGui::SameLine(0.0f, 2.0f);

    // --- 右: 現在フォルダのカード/サムネイル ---
    if (ImGui::BeginChild("##ProjectContents", ImVec2(0.0f, browser_height), true))
    {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            project_browser_focused = true;
        draw_project_folder_contents();

        // 空白右クリックだけでCreateメニューを開く。
        // BeginPopupContextWindowをそのまま使うとカード上の右クリックまで奪い、
        // Rename/Deleteの項目メニューではなくCreate一覧が出る原因になる。
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("##ProjectCreateMenu");
        }
        if (ImGui::BeginPopup("##ProjectCreateMenu"))
        {
            draw_project_create_submenu(root / project_current_folder);
            ImGui::Separator();
            if (ImGui::MenuItem("エクスプローラーで表示"))
                project_show_in_explorer(root / project_current_folder);
            if (ImGui::MenuItem("Project相対パスをコピー"))
                project_copy_path(root / project_current_folder, false);
            if (ImGui::MenuItem("絶対パスをコピー"))
                project_copy_path(root / project_current_folder, true);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    if (ImGui::GetDragDropPayload() == nullptr)
    {
        project_tree_drag_hover_folder.clear();
        project_tree_drag_hover_started = 0.0;
    }

    draw_project_delete_popup();

    // Picker と同じ AssetDatabase の欠損キャッシュを使い、描画中の stat を避ける。
    bool missing_cache_refreshed = false;
    if (!asset_database.MissingFilesCached())
    {
        const std::filesystem::path resources_root = content_path("resources");
        asset_database.RefreshMissingFiles(resources_root);
        missing_cache_refreshed = true;
    }
    if (missing_cache_refreshed ||
        project_missing_assets_source_count != asset_database.Records().size())
    {
        project_missing_assets.clear();
        for (const auto& record : asset_database.Records())
        {
            if (asset_database.IsMissing(record.guid))
                project_missing_assets.push_back({ record.guid, record.display_name,
                    record.source_path.generic_u8string() });
        }
        project_missing_assets_source_count = asset_database.Records().size();
    }
    if (!project_missing_assets.empty())
    {
        const std::string missing_label = "Missing Assets (" +
            std::to_string(project_missing_assets.size()) + ")";
        if (ImGui::TreeNode(missing_label.c_str()))
        {
            for (const auto& missing : project_missing_assets)
            {
                ImGui::PushID(missing[0].c_str());
                const std::string label = "[Missing] " + missing[1];
                if (ImGui::Selectable(label.c_str()))
                {
                    selected_asset_guid = missing[0];
                    project_selected_entry_path = content_path(missing[2]);
                    selected_editor_object = editor_selection::asset;
                }
                ImGui::TextDisabled("%s", missing[2].c_str());
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }

    if (!project_browser_status.empty())
        ImGui::TextWrapped("%s", project_browser_status.c_str());
}
