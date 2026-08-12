#include "framework.h"
#include "texture.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
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

// フォルダツリーとフォルダ内容表示の関数本体

void framework::draw_project_folder_tree(const std::filesystem::path& folder, int depth)
{
    if (depth > 12) return;

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return;

    const std::vector<ProjectEntry> children = ListProjectFolder(folder, true);
    const std::filesystem::path current = root / project_current_folder;

    for (const ProjectEntry& child : children)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth;

        const std::vector<ProjectEntry> grandchildren =
            ListProjectFolder(child.path, true);
        if (grandchildren.empty()) flags |= ImGuiTreeNodeFlags_Leaf;

        std::error_code compare_error;
        if (std::filesystem::equivalent(child.path, current, compare_error) &&
            !compare_error)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(child.path.generic_u8string().c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, KindColor(
            ReplayEngine::Assets::AssetKind::Unknown, true));
        const bool opened = ImGui::TreeNodeEx(child.name.c_str(), flags);
        ImGui::PopStyleColor();

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            set_project_folder(child.path);
        }
        if (opened)
        {
            draw_project_folder_tree(child.path, depth + 1);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

// -----------------------------------------------------------------------------
//  右ペイン: フォルダの中身
// -----------------------------------------------------------------------------
void framework::draw_project_folder_contents()
{
    using ReplayEngine::Assets::AssetKind;

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return;

    const std::filesystem::path current = root / project_current_folder;
    const std::string query = ToLowerCopy(asset_search_text);
    const std::vector<ProjectEntry> entries = query.empty()
        ? ListProjectFolder(current, false)
        : SearchProjectFiles(root, query);

    const float cell = project_thumbnail_size + 22.0f;
    const float available = ImGui::GetContentRegionAvail().x;
    int columns = project_grid_view ? static_cast<int>(available / (cell + 8.0f)) : 1;
    if (columns < 1) columns = 1;

    int drawn = 0;
    bool registered_any = false;
    for (const ProjectEntry& entry : entries)
    {
        if (!query.empty() &&
            ToLowerCopy(entry.name).find(query) == std::string::npos)
        {
            continue;
        }

        const AssetKind kind = entry.is_directory
            ? AssetKind::Unknown : project_kind_for(entry.path);
        if (!entry.is_directory && asset_type_filter != 0)
        {
            int filter_type = 10;
            if (kind == AssetKind::Model) filter_type = 1;
            else if (ToLowerCopy(entry.path.extension().u8string()) == ".replayprefab")
                filter_type = 2;
            else if (ToLowerCopy(entry.path.extension().u8string()) == ".replayscene")
                filter_type = 3;
            else if (kind == AssetKind::Material) filter_type = 4;
            else if (kind == AssetKind::Script) filter_type = 5;
            else if (kind == AssetKind::Shader) filter_type = 6;
            else if (kind == AssetKind::SceneFlow) filter_type = 7;
            else if (kind == AssetKind::Motion) filter_type = 8;
            else if (kind == AssetKind::Font) filter_type = 9;
            if (asset_type_filter != filter_type) continue;
        }

        // 未登録のファイルはここで AssetDatabase へ登録する。
        // 登録されていないと GUID が無く、ドラッグもシーン配置もできない。
        // Register は正規化パスから GUID を導くので、既にあれば
        // 既存レコードがそのまま返る。Save は新規が出た時だけ最後に 1 回。
        const ReplayEngine::Assets::AssetRecord* record =
            asset_database.FindByPath(entry.path);
        if (record == nullptr && !entry.is_directory && kind != AssetKind::Unknown)
        {
            record = &asset_database.Register(entry.path, kind);
            registered_any = true;
        }
        const bool selected = record != nullptr &&
            !selected_asset_guid.empty() && record->guid == selected_asset_guid;

        if (project_grid_view && drawn % columns != 0) ImGui::SameLine();
        ++drawn;

        ImGui::PushID(entry.path.generic_u8string().c_str());
        ImGui::BeginGroup();

        const bool renaming = !project_rename_target.empty() &&
            project_rename_target == entry.path;

        ID3D11ShaderResourceView* thumbnail =
            entry.is_directory ? nullptr : project_thumbnail_for(entry.path);

        const ImVec2 icon_size(project_thumbnail_size, project_thumbnail_size);
        if (thumbnail != nullptr)
        {
            if (ImGui::ImageButton(reinterpret_cast<ImTextureID>(thumbnail),
                icon_size, ImVec2(0, 0), ImVec2(1, 1), 2))
            {
                if (record != nullptr) selected_asset_guid = record->guid;
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, KindColor(kind, entry.is_directory));
            if (ImGui::Button(KindBadge(kind, entry.is_directory), icon_size))
            {
                if (record != nullptr) selected_asset_guid = record->guid;
            }
            ImGui::PopStyleColor();
        }

        const bool icon_hovered = ImGui::IsItemHovered();
        const bool icon_double_clicked = icon_hovered &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        // .cs も含めて全種別をドラッグできるようにする。
        // 受け側 (Scene ビュー / Inspector) が種別で判断する。
        //
        // 直前のアイコンボタンが「掴まれている」時だけドラッグ元にする。
        // ImGui の BeginDragDropSource は LastItemId が 0 のまま呼ぶと
        // IM_ASSERT(0) で落ちる。子ウィンドウがクリップされて SkipItems が
        // 立つと Button() が ItemAdd を呼ばず LastItemId が 0 のまま残るので、
        // ここを無条件で呼ぶとクリックした瞬間に落ちていた。
        // SourceAllowNullID も付けて二重に防ぐ。
        if (record != nullptr && !entry.is_directory && ImGui::IsItemActive() &&
            ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("REPLAY_ASSET_GUID", record->guid.c_str(),
                record->guid.size() + 1);
            ImGui::TextUnformatted(entry.name.c_str());
            ImGui::EndDragDropSource();
        }

        // 項目ごとの右クリックメニュー。
        // EndGroup 後だと LastItemId が保証されないので、
        // アイコンボタン直後に置く。
        if (ImGui::BeginPopupContextItem("##ProjectItemMenu"))
        {
            if (ImGui::MenuItem("名前を変更"))
            {
                project_rename_target = entry.path;
                project_rename_focus_pending = true;
                const std::string stem = entry.is_directory
                    ? entry.name : entry.path.stem().u8string();
                strncpy_s(project_rename_buffer, sizeof(project_rename_buffer),
                    stem.c_str(), _TRUNCATE);
            }
            if (!entry.is_directory && kind == AssetKind::Script &&
                ImGui::MenuItem("Visual Studio で開く"))
            {
                if (record != nullptr) selected_asset_guid = record->guid;
                open_selected_csharp_asset();
            }
            if (!entry.is_directory && kind == AssetKind::SceneFlow &&
                ImGui::MenuItem("Scene Flow で開く"))
            {
                if (record != nullptr)
                {
                    selected_asset_guid = record->guid;
                    load_scene_flow_editor(*record);
                }
            }
            if (!entry.is_directory && kind == AssetKind::Motion &&
                ImGui::MenuItem("Motion Workspace で開く"))
            {
                if (record != nullptr)
                {
                    selected_asset_guid = record->guid;
                    open_motion_asset(*record);
                }
            }
            if (!entry.is_directory && kind == AssetKind::Shader)
            {
                const bool is_composer = ToLowerCopy(entry.path.extension().u8string()) ==
                    ReplayEngine::Rendering::ShaderComposerAsset::file_extension;
                if (is_composer)
                {
                    if (ImGui::MenuItem("Shader Composer で開く"))
                    {
                        std::string open_error;
                        if (!shader_composer_editor.Open(entry.path, open_error))
                            push_editor_log("Warning", open_error, entry.path);
                    }
                }
                else if (ImGui::MenuItem("Visual Studio で開く"))
                {
                    std::string open_error;
                    if (!ReplayEngine::Scripting::CSharp::CSharpProject::OpenVisualStudio(
                        entry.path, 1, open_error))
                    {
                        push_editor_log("Warning", open_error, entry.path);
                    }
                }
            }
            if (entry.is_directory && ImGui::MenuItem("このフォルダを開く"))
            {
                set_project_folder(entry.path);
            }
            ImGui::Separator();
            ImGui::TextDisabled("%s", entry.name.c_str());
            ImGui::EndPopup();
        }

        if (icon_double_clicked)
        {
            if (entry.is_directory)
            {
                set_project_folder(entry.path);
            }
            else if (kind == AssetKind::Script)
            {
                if (record != nullptr) selected_asset_guid = record->guid;
                open_selected_csharp_asset();
            }
            else if (kind == AssetKind::SceneFlow)
            {
                if (record != nullptr)
                {
                    selected_asset_guid = record->guid;
                    load_scene_flow_editor(*record);
                }
            }
            else if (kind == AssetKind::Motion)
            {
                if (record != nullptr)
                {
                    selected_asset_guid = record->guid;
                    open_motion_asset(*record);
                }
            }
            else if (kind == AssetKind::Shader)
            {
                if (record != nullptr) selected_asset_guid = record->guid;
                std::string open_error;
                if (ToLowerCopy(entry.path.extension().u8string()) ==
                    ReplayEngine::Rendering::ShaderComposerAsset::file_extension)
                {
                    if (!shader_composer_editor.Open(entry.path, open_error))
                        push_editor_log("Warning", open_error, entry.path);
                }
                else if (!ReplayEngine::Scripting::CSharp::CSharpProject::OpenVisualStudio(
                    entry.path, 1, open_error))
                {
                    push_editor_log("Warning", open_error, entry.path);
                }
            }
            else if (record != nullptr)
            {
                // Double click は「開く/選択」であり Scene を変更しない。
                // 配置/割当は Scene View D&D または明示的な配置ボタンだけ。
                selected_asset_guid = record->guid;
                project_browser_status = "Asset を選択しました: " + record->display_name +
                    "（Sceneへ配置するにはドラッグ&ドロップ）";
            }
        }

        if (icon_hovered)
        {
            ImGui::SetTooltip("%s", entry.path.generic_u8string().c_str());
        }

        // 名前。改名中はインライン入力に差し替える。
        ImGui::PushItemWidth(project_thumbnail_size);
        if (renaming)
        {
            if (project_rename_focus_pending)
            {
                ImGui::SetKeyboardFocusHere();
                project_rename_focus_pending = false;
            }
            const bool committed = ImGui::InputText("##ProjectRename",
                project_rename_buffer, IM_ARRAYSIZE(project_rename_buffer),
                ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_AutoSelectAll);
            if (committed)
            {
                project_rename_entry(entry.path, project_rename_buffer);
                project_rename_target.clear();
            }
            else if (ImGui::IsKeyPressed(VK_ESCAPE))
            {
                project_rename_target.clear();
            }
        }
        else
        {
            std::string label = entry.name;
            if (label.size() > 16) label = label.substr(0, 15) + "..";
            if (selected)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", label.c_str());
            }
            else
            {
                ImGui::TextUnformatted(label.c_str());
            }
        }
        ImGui::PopItemWidth();

        ImGui::EndGroup();
        ImGui::PopID();
    }

    if (registered_any)
    {
        std::string save_error;
        if (!asset_database.Save(save_error))
        {
            push_editor_log("Warning", "AssetDatabase 保存失敗: " + save_error);
        }
    }

    if (drawn == 0)
    {
        ImGui::TextDisabled("このフォルダには表示できる項目がありません");
    }
}

// -----------------------------------------------------------------------------
//  Project ブラウザ本体
// -----------------------------------------------------------------------------
