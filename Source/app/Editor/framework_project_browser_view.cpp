#include "framework.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Motion/CompositionAsset.h"
#include "../../RePlayEngine/Motion/EasingCurveAsset.h"
#include "../../RePlayEngine/Motion/MotionAsset.h"
#include "../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../RePlayEngine/Rendering/Shaders/ShaderAssetFactory.h"
#include "../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerAsset.h"
#include "../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerGenerator.h"
#include "../../RePlayEngine/Runtime/Scene/SceneFlowAsset.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include "framework_project_browserInternal.h"
using namespace framework_project_browser::Detail;

namespace
{
    void DrawEasingCurveProjectIcon(ImDrawList* draw_list, const ImVec2& min,
        const ImVec2& max, const ReplayEngine::Motion::EasingCurveAsset* curve)
    {
        const float width = (std::max)(1.0f, max.x - min.x);
        const ImVec2 inner_min(min.x + 7.0f, min.y + 7.0f);
        const ImVec2 inner_max(max.x - 7.0f, max.y - 7.0f);
        draw_list->AddRectFilled(inner_min, inner_max, IM_COL32(27, 30, 34, 245), 4.0f);
        float min_y = 0.0f;
        float max_y = 1.0f;
        if (curve != nullptr)
        {
            for (const float value : curve->samples)
            {
                if (!std::isfinite(value)) continue;
                min_y = (std::min)(min_y, value);
                max_y = (std::max)(max_y, value);
            }
        }
        float span = max_y - min_y;
        if (!std::isfinite(span) || span < 0.001f) span = 1.0f;
        const float padding = (std::max)(0.06f, span * 0.10f);
        min_y -= padding;
        max_y += padding;
        span = max_y - min_y;
        const auto point = [&](float t, float value)
        {
            const float x = inner_min.x + (inner_max.x - inner_min.x) * t;
            const float normalized = (value - min_y) / span;
            const float y = inner_max.y - (inner_max.y - inner_min.y) * normalized;
            return ImVec2(x, y);
        };
        draw_list->AddLine(point(0.0f, 0.0f), point(1.0f, 1.0f),
            IM_COL32(120, 125, 130, 90), 1.0f);
        ImVec2 previous = point(0.0f, curve != nullptr ? curve->Evaluate(0.0f) : 0.0f);
        constexpr int divisions = 28;
        for (int index = 1; index <= divisions; ++index)
        {
            const float t = static_cast<float>(index) / static_cast<float>(divisions);
            float value = curve != nullptr ? curve->Evaluate(t) : t;
            if (!std::isfinite(value)) value = t;
            const ImVec2 current = point(t, value);
            draw_list->AddLine(previous, current, IM_COL32(120, 235, 145, 255),
                (std::max)(1.5f, (std::min)(2.5f, width / 52.0f)));
            previous = current;
        }
        draw_list->AddCircleFilled(point(0.0f, 0.0f), 2.5f, IM_COL32(235, 240, 240, 230));
        draw_list->AddCircleFilled(point(1.0f, 1.0f), 2.5f, IM_COL32(235, 240, 240, 230));
    }
}

// フォルダツリーとフォルダ内容表示の関数本体

void framework::draw_project_folder_tree(const std::filesystem::path& folder, int depth)
{
    // 深い制作フォルダでも途中で消えないよう十分大きくする。
    // 異常な再帰だけを止める安全弁。
    if (depth > 64) return;

    std::error_code error;
    const std::filesystem::path root = std::filesystem::current_path(error);
    if (error) return;

    // 左ペインは「フォルダツリー」ではなく完全な Project Tree。
    // フォルダとファイルを同じ階層で表示する。
    const std::vector<ProjectEntry> children = ListProjectFolder(folder, false);

    for (const ProjectEntry& child : children)
    {
        const bool selected = !project_selected_entry_path.empty() &&
            NormalizeProjectPath(project_selected_entry_path) == NormalizeProjectPath(child.path);
        const bool renaming = !project_rename_target.empty() &&
            NormalizeProjectPath(project_rename_target) == NormalizeProjectPath(child.path);

        ImGui::PushID(child.path.generic_u8string().c_str());

        if (child.is_directory)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_SpanAvailWidth;
            const std::vector<ProjectEntry> grandchildren = ListProjectFolder(child.path, false);
            if (grandchildren.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            if (selected) flags |= ImGuiTreeNodeFlags_Selected;

            // 右ペインからAssetを選んだ場合、親フォルダを自動展開して
            // 左Treeにも同じAssetが見えるようにする。
            if (project_tree_reveal_selection_pending &&
                !project_selected_entry_path.empty() &&
                IsProjectPathInsideOrEqual(project_selected_entry_path, child.path))
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }

            // Drag中に閉じたフォルダの上へ約0.65秒置いたら自動で開く。
            if (project_tree_drag_hover_folder == child.path &&
                ImGui::GetDragDropPayload() != nullptr &&
                ImGui::GetTime() - project_tree_drag_hover_started >= 0.65)
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }

            bool opened = false;
            if (renaming)
            {
                if (project_rename_focus_pending)
                {
                    ImGui::SetKeyboardFocusHere();
                    project_rename_focus_pending = false;
                }
                ImGui::SetNextItemWidth((std::max)(100.0f, ImGui::GetContentRegionAvail().x));
                const bool committed = ImGui::InputText("##ProjectTreeRename",
                    project_rename_buffer, IM_ARRAYSIZE(project_rename_buffer),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (committed)
                {
                    project_rename_entry(child.path, project_rename_buffer);
                    project_rename_target.clear();
                }
                else if (ImGui::IsKeyPressed(VK_ESCAPE)) project_rename_target.clear();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text,
                    KindColor(ReplayEngine::Assets::AssetKind::Unknown, true));
                opened = ImGui::TreeNodeEx(child.name.c_str(), flags);
                ImGui::PopStyleColor();
            }

            if (!renaming && ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                set_project_folder(child.path);
                project_selected_entry_path = child.path;
                selected_editor_object = editor_selection::asset;
                selected_asset_guid.clear();
                project_browser_focused = true;
            }
            if (!renaming && ImGui::IsItemHovered() &&
                ImGui::GetDragDropPayload() != nullptr)
            {
                if (project_tree_drag_hover_folder != child.path)
                {
                    project_tree_drag_hover_folder = child.path;
                    project_tree_drag_hover_started = ImGui::GetTime();
                }
            }
            if (!renaming && ImGui::BeginPopupContextItem("##ProjectTreeEntryMenu"))
            {
                project_selected_entry_path = child.path;
                selected_editor_object = editor_selection::asset;
                selected_asset_guid.clear();
                project_browser_focused = true;
                draw_project_entry_context_items(child.path);
                ImGui::EndPopup();
            }
            if (!renaming && ImGui::IsItemActive() &&
                ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                const std::string source = child.path.generic_u8string();
                ImGui::SetDragDropPayload("REPLAY_PROJECT_PATH", source.c_str(), source.size() + 1);
                ImGui::TextUnformatted(child.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (!renaming && ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_PROJECT_PATH"))
                {
                    const char* source = static_cast<const char*>(payload->Data);
                    if (source != nullptr) project_move_entry(std::filesystem::u8path(source), child.path);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_ASSET_GUID"))
                {
                    const char* guid = static_cast<const char*>(payload->Data);
                    if (guid != nullptr)
                        if (const auto* dragged = asset_database.FindByGuid(guid))
                            project_move_entry(dragged->source_path, child.path);
                }
                ImGui::EndDragDropTarget();
            }

            if (opened)
            {
                draw_project_folder_tree(child.path, depth + 1);
                ImGui::TreePop();
            }
        }
        else
        {
            const ReplayEngine::Assets::AssetKind kind = project_kind_for(child.path);
            const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByPath(child.path);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selected) flags |= ImGuiTreeNodeFlags_Selected;

            if (renaming)
            {
                if (project_rename_focus_pending)
                {
                    ImGui::SetKeyboardFocusHere();
                    project_rename_focus_pending = false;
                }
                ImGui::SetNextItemWidth((std::max)(100.0f, ImGui::GetContentRegionAvail().x));
                const bool committed = ImGui::InputText("##ProjectTreeFileRename",
                    project_rename_buffer, IM_ARRAYSIZE(project_rename_buffer),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (committed)
                {
                    project_rename_entry(child.path, project_rename_buffer);
                    project_rename_target.clear();
                }
                else if (ImGui::IsKeyPressed(VK_ESCAPE)) project_rename_target.clear();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, KindColor(kind, false));
                ImGui::TreeNodeEx(child.name.c_str(), flags);
                ImGui::PopStyleColor();
            }

            const bool hovered = !renaming && ImGui::IsItemHovered();
            if (!renaming && ImGui::IsItemClicked())
            {
                set_project_folder(child.path.parent_path());
                project_selected_entry_path = child.path;
                selected_editor_object = editor_selection::asset;
                selected_asset_guid = record != nullptr ? record->guid : std::string();
                project_browser_focused = true;
            }
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                project_open_entry(child.path);

            if (!renaming && ImGui::BeginPopupContextItem("##ProjectTreeFileMenu"))
            {
                project_selected_entry_path = child.path;
                selected_editor_object = editor_selection::asset;
                selected_asset_guid = record != nullptr ? record->guid : std::string();
                project_browser_focused = true;
                draw_project_entry_context_items(child.path);
                ImGui::EndPopup();
            }

            if (!renaming && ImGui::IsItemActive() &&
                ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                // Scene/InspectorへはGUID payloadが必要。未登録ならDrag開始時だけ登録する。
                if (record == nullptr && kind != ReplayEngine::Assets::AssetKind::Unknown)
                {
                    const auto& registered = asset_database.Register(child.path, kind);
                    record = &registered;
                    std::string save_error;
                    if (!asset_database.Save(save_error))
                        push_editor_log("Warning", "AssetDatabase 保存失敗: " + save_error, child.path);
                }
                if (record != nullptr)
                {
                    ImGui::SetDragDropPayload("REPLAY_ASSET_GUID", record->guid.c_str(),
                        record->guid.size() + 1);
                }
                else
                {
                    const std::string source = child.path.generic_u8string();
                    ImGui::SetDragDropPayload("REPLAY_PROJECT_PATH", source.c_str(), source.size() + 1);
                }
                ImGui::TextUnformatted(child.name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        ImGui::PopID();
    }
}

bool framework::project_open_entry(const std::filesystem::path& path)
{
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error) || error)
    {
        project_browser_status = "開く対象が見つかりません";
        return false;
    }
    if (std::filesystem::is_directory(path, error) && !error)
    {
        set_project_folder(path);
        project_selected_entry_path = path;
        selected_editor_object = editor_selection::asset;
        selected_asset_guid.clear();
        project_tree_reveal_selection_pending = true;
        return true;
    }

    using ReplayEngine::Assets::AssetKind;
    const AssetKind kind = project_kind_for(path);
    const ReplayEngine::Assets::AssetRecord* record = asset_database.FindByPath(path);
    if (record == nullptr && kind != AssetKind::Unknown)
    {
        record = &asset_database.Register(path, kind);
        std::string save_error;
        if (!asset_database.Save(save_error))
            push_editor_log("Warning", "AssetDatabase 保存失敗: " + save_error, path);
    }

    set_project_folder(path.parent_path());
    project_selected_entry_path = path;
    selected_editor_object = editor_selection::asset;
    selected_asset_guid = record != nullptr ? record->guid : std::string();
    project_tree_reveal_selection_pending = true;

    if (kind == AssetKind::Script)
    {
        open_selected_csharp_asset();
        return true;
    }
    if (kind == AssetKind::SceneFlow && record != nullptr)
    {
        load_scene_flow_editor(*record);
        return true;
    }
    if ((kind == AssetKind::Motion || kind == AssetKind::Composition) && record != nullptr)
        return open_motion_asset(*record);
    if (kind == AssetKind::SpriteAtlas && record != nullptr)
        return open_sprite_atlas_asset(*record);
    if (kind == AssetKind::EasingCurve && record != nullptr)
        return open_easing_curve_asset(*record);
    if (kind == AssetKind::Shader)
    {
        std::string open_error;
        if (ToLowerCopy(path.extension().u8string()) ==
            ReplayEngine::Rendering::ShaderComposerAsset::file_extension)
        {
            if (!shader_composer_editor.Open(path, open_error))
            {
                push_editor_log("Warning", open_error, path);
                return false;
            }
            return true;
        }
        if (!ReplayEngine::Scripting::CSharp::CSharpProject::OpenVisualStudio(path, 1, open_error))
        {
            push_editor_log("Warning", open_error, path);
            return false;
        }
        return true;
    }

    // Scene/Material/Image/Audio等は、DoubleClickで勝手にSceneを変更・配置しない。
    // Inspector/Preview対象として選択するだけにする。
    project_browser_status = "Asset を選択しました: " + path.filename().u8string();
    return true;
}

void framework::draw_project_create_submenu(const std::filesystem::path& target_folder)
{
    if (!ImGui::BeginMenu("Create")) return;

    ImGui::TextDisabled("作成先: %s", target_folder.filename().empty()
        ? "Project" : target_folder.filename().u8string().c_str());
    ImGui::SetNextItemWidth(210.0f);
    ImGui::InputTextWithHint("##ProjectCreateName", "名前",
        project_new_item_name, IM_ARRAYSIZE(project_new_item_name));
    ImGui::Separator();

    const auto select_target = [&]()
    {
        set_project_folder(target_folder);
        project_selected_entry_path = target_folder;
        selected_editor_object = editor_selection::asset;
        selected_asset_guid.clear();
        project_tree_reveal_selection_pending = true;
    };

        if (ImGui::MenuItem("Folder"))
    {
        select_target();
        project_create_folder(project_new_item_name);
    }
    if (ImGui::BeginMenu("Script"))
    {
        if (ImGui::MenuItem("C# Script"))
        {
            select_target();
            project_create_csharp_behaviour(project_new_item_name);
        }
        ImGui::TextDisabled("Namespace: %s", new_csharp_namespace);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Animation"))
    {
        if (ImGui::MenuItem("Motion Asset"))
        {
            select_target();
            project_create_motion(project_new_item_name);
        }
        if (ImGui::MenuItem("Motion Composition"))
        {
            select_target();
            project_create_composition(project_new_item_name);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("UI / Presentation"))
    {
        if (ImGui::MenuItem("Sprite Atlas"))
        {
            select_target();
            project_create_sprite_atlas(project_new_item_name);
        }
        if (ImGui::MenuItem("Effect Preset"))
        {
            select_target();
            project_create_effect_preset(project_new_item_name);
        }
        if (ImGui::MenuItem(u8"イージングカーブ"))
        {
            select_target();
            project_create_easing_curve(project_new_item_name);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Data"))
    {
        if (ImGui::MenuItem("Material"))
        {
            select_target();
            project_create_material(project_new_item_name);
        }
        if (ImGui::MenuItem("Scene Flow"))
        {
            select_target();
            project_create_scene_flow(project_new_item_name);
        }
        if (ImGui::MenuItem("Localization Table"))
        {
            select_target();
            project_create_localization(project_new_item_name);
        }
        if (ImGui::MenuItem("Input Action Asset"))
        {
            select_target();
            project_create_input_action_asset(project_new_item_name);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Shader"))
    {
        if (ImGui::MenuItem("Surface Shader"))
        {
            select_target();
            project_create_surface_shader(project_new_item_name);
        }
        if (ImGui::MenuItem("Layer Shader"))
        {
            select_target();
            project_create_layer_shader(project_new_item_name);
        }
        if (ImGui::BeginMenu("Shader Composer"))
        {
            if (ImGui::MenuItem("Surface"))
            {
                select_target();
                project_create_shader_composer(project_new_item_name,
                    ReplayEngine::Rendering::ShaderDomain::Surface);
            }
            if (ImGui::MenuItem("Layer"))
            {
                select_target();
                project_create_shader_composer(project_new_item_name,
                    ReplayEngine::Rendering::ShaderDomain::Layer);
            }
            if (ImGui::MenuItem("PostProcess"))
            {
                select_target();
                project_create_shader_composer(project_new_item_name,
                    ReplayEngine::Rendering::ShaderDomain::PostProcess);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenu();
}

void framework::draw_project_entry_context_items(const std::filesystem::path& path)
{
    std::error_code error;
    const bool directory = std::filesystem::is_directory(path, error) && !error;
    const auto kind = directory ? ReplayEngine::Assets::AssetKind::Unknown : project_kind_for(path);
    const auto* record = directory ? nullptr : asset_database.FindByPath(path);

    draw_project_create_submenu(directory ? path : path.parent_path());
    ImGui::Separator();

    if (ImGui::MenuItem(directory ? "このフォルダを開く" : "開く", "Enter"))
        project_open_entry(path);
        if (ImGui::MenuItem("名前を変更", nullptr, false, object_editor_context.CanEdit()))
        project_begin_rename_selected();
        if (ImGui::MenuItem("複製", nullptr, false, object_editor_context.CanEdit()))
        project_duplicate_entry(path);

    ImGui::Separator();
    if (ImGui::MenuItem("エクスプローラーで表示")) project_show_in_explorer(path);
    if (ImGui::MenuItem("Project相対パスをコピー")) project_copy_path(path, false);
    if (ImGui::MenuItem("絶対パスをコピー")) project_copy_path(path, true);

    if (!directory && kind == ReplayEngine::Assets::AssetKind::Script &&
        ImGui::MenuItem("Visual Studio で開く"))
    {
        if (record != nullptr) selected_asset_guid = record->guid;
        open_selected_csharp_asset();
    }

    ImGui::Separator();
    if (ImGui::MenuItem("削除", "Backspace / Del", false, object_editor_context.CanEdit()))
        project_request_delete(path);
    ImGui::Separator();
    ImGui::TextDisabled("%s", path.filename().u8string().c_str());
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
            int filter_type = 12;
            if (kind == AssetKind::Model) filter_type = 1;
            else if (ToLowerCopy(entry.path.extension().u8string()) == ".replayprefab")
                filter_type = 2;
            else if (ToLowerCopy(entry.path.extension().u8string()) == ".replayscene")
                filter_type = 3;
            else if (kind == AssetKind::Material) filter_type = 4;
            else if (kind == AssetKind::Script) filter_type = 5;
            else if (kind == AssetKind::Shader) filter_type = 6;
            else if (kind == AssetKind::SceneFlow) filter_type = 7;
            else if (kind == AssetKind::Motion || kind == AssetKind::Composition) filter_type = 8;
            else if (kind == AssetKind::Font) filter_type = 9;
            else if (kind == AssetKind::Localization) filter_type = 10;
            else if (kind == AssetKind::EffectPreset) filter_type = 11;
            else if (kind == AssetKind::InputAction) filter_type = 12;
            else if (kind == AssetKind::EasingCurve) filter_type = 14;
            else filter_type = 13;
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
        const bool selected = (!project_selected_entry_path.empty() &&
            project_selected_entry_path == entry.path) ||
            (record != nullptr && !selected_asset_guid.empty() &&
                record->guid == selected_asset_guid);

        if (project_grid_view && drawn % columns != 0) ImGui::SameLine();
        ++drawn;

        ImGui::PushID(entry.path.generic_u8string().c_str());
        ImGui::BeginGroup();

        const bool renaming = !project_rename_target.empty() &&
            project_rename_target == entry.path;

        const bool has_thumbnail = !entry.is_directory &&
            IsImageExtension(ToLowerCopy(entry.path.extension().u8string()));
        const ImTextureID thumbnail_id = has_thumbnail
            ? reinterpret_cast<ImTextureID>(dx12_device_context.ImGuiTextureForPath(entry.path))
            : nullptr;

        const ImVec2 icon_size(project_thumbnail_size, project_thumbnail_size);
        if (thumbnail_id != nullptr)
        {
            if (ImGui::ImageButton(thumbnail_id,
                icon_size, ImVec2(0, 0), ImVec2(1, 1), 2))
            {
                project_selected_entry_path = entry.path;
                selected_editor_object = editor_selection::asset;
                if (record != nullptr) selected_asset_guid = record->guid;
                else selected_asset_guid.clear();
                project_browser_focused = true;
                project_tree_reveal_selection_pending = true;
            }
        }
        else if (kind == AssetKind::EasingCurve && record != nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.20f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.24f, 0.20f, 1.0f));
            if (ImGui::Button("##EasingCurveAssetIcon", icon_size))
            {
                project_selected_entry_path = entry.path;
                selected_editor_object = editor_selection::asset;
                selected_asset_guid = record->guid;
                project_browser_focused = true;
                project_tree_reveal_selection_pending = true;
            }
            ReplayEngine::Editor::EditorHelp::Item("button.project.asset_easing_icon");
            ImGui::PopStyleColor(3);
            const ReplayEngine::Reflection::AssetReference reference(record->guid);
            const ReplayEngine::Motion::EasingCurveAsset* curve =
                ReplayEngine::Motion::EasingCurveAsset::Resolve(&asset_database, reference);
            DrawEasingCurveProjectIcon(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax(), curve);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, KindColor(kind, entry.is_directory));
            if (ImGui::Button(KindBadge(kind, entry.is_directory), icon_size))
            {
                project_selected_entry_path = entry.path;
                selected_editor_object = editor_selection::asset;
                if (record != nullptr) selected_asset_guid = record->guid;
                else selected_asset_guid.clear();
                project_browser_focused = true;
                project_tree_reveal_selection_pending = true;
            }
            ReplayEngine::Editor::EditorHelp::Item("button.project.asset_kind_icon");
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
        if (entry.is_directory && ImGui::IsItemActive() &&
            ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            const std::string source = entry.path.generic_u8string();
            ImGui::SetDragDropPayload("REPLAY_PROJECT_PATH", source.c_str(), source.size() + 1);
            ImGui::TextUnformatted(entry.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (entry.is_directory && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_PROJECT_PATH"))
            {
                const char* source = static_cast<const char*>(payload->Data);
                if (source != nullptr) project_move_entry(std::filesystem::u8path(source), entry.path);
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_ASSET_GUID"))
            {
                const char* guid = static_cast<const char*>(payload->Data);
                if (guid != nullptr)
                    if (const auto* dragged = asset_database.FindByGuid(guid))
                        project_move_entry(dragged->source_path, entry.path);
            }
            ImGui::EndDragDropTarget();
        }

        // 項目ごとの右クリックメニュー。Create は階層メニューへ格納し、
        // Rename/Delete等は左Treeと右カードで同じ操作にする。
        if (ImGui::BeginPopupContextItem("##ProjectItemMenu"))
        {
            project_selected_entry_path = entry.path;
            selected_editor_object = editor_selection::asset;
            if (record != nullptr) selected_asset_guid = record->guid;
            else selected_asset_guid.clear();
            project_browser_focused = true;
            draw_project_entry_context_items(entry.path);
            ImGui::EndPopup();
        }

        if (icon_double_clicked)
        {
            project_open_entry(entry.path);
        }

        if (icon_hovered)
            ReplayEngine::Editor::EditorHelp::Item(
                "control.project.entry_path", entry.path.generic_u8string().c_str());

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
            if (label.size() > 18) label = label.substr(0, 17) + "..";
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.35f, 1.0f));
            const bool name_clicked = ImGui::Selectable(label.c_str(), selected,
                ImGuiSelectableFlags_AllowDoubleClick, ImVec2(project_thumbnail_size, 0.0f));
            if (selected) ImGui::PopStyleColor();
            if (name_clicked)
            {
                project_selected_entry_path = entry.path;
                selected_editor_object = editor_selection::asset;
                if (record != nullptr) selected_asset_guid = record->guid;
                else selected_asset_guid.clear();
                project_browser_focused = true;
                project_tree_reveal_selection_pending = true;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    project_open_entry(entry.path);
            }
            if (ImGui::BeginPopupContextItem("##ProjectNameMenu"))
            {
                project_selected_entry_path = entry.path;
                selected_editor_object = editor_selection::asset;
                if (record != nullptr) selected_asset_guid = record->guid;
                else selected_asset_guid.clear();
                project_browser_focused = true;
                draw_project_entry_context_items(entry.path);
                ImGui::EndPopup();
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
