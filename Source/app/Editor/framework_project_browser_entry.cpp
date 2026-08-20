#include "framework.h"
#include "texture.h"
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

    // --- パンくず ---
    if (ImGui::SmallButton("Project"))
    {
        project_current_folder.clear();
    }
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
        }
        ImGui::PopID();
    }

    // --- 検索とフィルタ ---
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##ProjectSearch", "Search Project...",
        asset_search_text, IM_ARRAYSIZE(asset_search_text));
    if (asset_search_text[0] != '\0' && ImGui::IsItemHovered())
        ImGui::SetTooltip("Project 全体を再帰検索します");
    ImGui::SameLine();
    const char* filters[] =
        { "All", "Model", "Prefab", "Scene", "Material", "Script", "Shader", "Flow", "Motion", "Font", "Localization", "EffectPreset", "Input", "Other" };
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##ProjectFilter", &asset_type_filter, filters, IM_ARRAYSIZE(filters));
    ImGui::SameLine();
    ImGui::Checkbox("グリッド", &project_grid_view);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("##ProjectThumbSize", &project_thumbnail_size,
        48.0f, 160.0f, "%.0f px");

    ImGui::Separator();

    // --- 左: フォルダツリー ---
    //
    // BeginChild の戻り値は必ず見ること。false のときは SkipItems が立ち、
    // 中のウィジェットが ItemAdd を呼ばないため LastItemId が古いまま残る。
    // EndChild は戻り値に関わらず必ず呼ぶ。
    if (ImGui::BeginChild("##ProjectTree", ImVec2(project_tree_width, 320.0f), true))
    {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            project_browser_focused = true;
        ImGuiTreeNodeFlags root_flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (project_current_folder.empty()) root_flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98f, 0.80f, 0.36f, 1.0f));
        const bool root_open = ImGui::TreeNodeEx("Project", root_flags);
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            project_current_folder.clear();
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

    ImGui::SameLine();

    // --- 右: フォルダの中身 ---
    if (ImGui::BeginChild("##ProjectContents", ImVec2(0.0f, 320.0f), true))
    {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            project_browser_focused = true;
        draw_project_folder_contents();

        // 空白部分の右クリック = このフォルダに作る。
        // Unity と同じで「今いる場所に作られる」。
        if (ImGui::BeginPopupContextWindow("##ProjectCreateMenu", 1))
        {
            ImGui::TextDisabled("%s に作成",
                project_current_folder.empty()
                    ? "Project" : project_current_folder.generic_u8string().c_str());
            ImGui::Separator();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##ProjectNewName", "名前",
                project_new_item_name, IM_ARRAYSIZE(project_new_item_name));
            if (ImGui::MenuItem("C# Script"))
            {
                project_create_csharp_behaviour(project_new_item_name);
            }
            if (ImGui::MenuItem("Material"))
            {
                project_create_material(project_new_item_name);
            }
            if (ImGui::MenuItem("Scene Flow"))
            {
                project_create_scene_flow(project_new_item_name);
            }
            if (ImGui::MenuItem("Motion Asset"))
            {
                project_create_motion(project_new_item_name);
            }
            if (ImGui::MenuItem("Motion Composition"))
            {
                project_create_composition(project_new_item_name);
            }
            if (ImGui::MenuItem("Sprite Atlas"))
            {
                project_create_sprite_atlas(project_new_item_name);
            }
            if (ImGui::MenuItem("Localization Table"))
            {
                project_create_localization(project_new_item_name);
            }
            if (ImGui::MenuItem("Effect Preset"))
            {
                project_create_effect_preset(project_new_item_name);
            }
            if (ImGui::MenuItem("Input Action Asset"))
            {
                project_create_input_action_asset(project_new_item_name);
            }
            if (ImGui::MenuItem("Surface Shader"))
            {
                project_create_surface_shader(project_new_item_name);
            }
            if (ImGui::MenuItem("Layer Shader"))
            {
                project_create_layer_shader(project_new_item_name);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Shader Composer (Surface)"))
            {
                project_create_shader_composer(project_new_item_name,
                    ReplayEngine::Rendering::ShaderDomain::Surface);
            }
            if (ImGui::MenuItem("Shader Composer (Layer)"))
            {
                project_create_shader_composer(project_new_item_name,
                    ReplayEngine::Rendering::ShaderDomain::Layer);
            }
            if (ImGui::MenuItem("Shader Composer (PostProcess)"))
            {
                project_create_shader_composer(project_new_item_name,
                    ReplayEngine::Rendering::ShaderDomain::PostProcess);
            }
            if (ImGui::MenuItem("Folder"))
            {
                project_create_folder(project_new_item_name);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Namespace: %s", new_csharp_namespace);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    draw_project_delete_popup();

    if (!project_browser_status.empty())
    {
        ImGui::TextWrapped("%s", project_browser_status.c_str());
    }
}
