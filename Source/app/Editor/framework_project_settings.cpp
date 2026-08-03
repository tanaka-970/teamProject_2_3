// プロジェクト設定と新規 Scene 作成の Editor UI。
//
// 【この 1 ファイルに分けている理由】
//   Scene の中身（GameObject）ではなく「プロジェクト全体の設定」を扱うため、
//   Scene 編集用の framework_scene_document.cpp とは責任が違う。
//   Default Controlled Character Prefab の指定と、新規 Scene の作り分けは
//   どちらもプロジェクト側の話なのでここへまとめた。
//
// 依存方向:
//   framework -> RePlayEngine の一方向。逆向きの参照は無い。

#include "framework.h"

#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"

#include <filesystem>
#include <string>

namespace
{
    namespace Project = ReplayEngine::Project;

    // Prefab として登録されている Asset だけを列挙する。
    // モデルやテクスチャが候補に出ると、選び間違いで Missing になるため。
    bool IsPrefabAsset(const ReplayEngine::Assets::AssetRecord& record)
    {
        return record.source_path.extension() ==
            ReplayEngine::Scene::Serialization::PrefabSerializer::file_extension;
    }
}

void framework::draw_project_settings_panel()
{
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("プロジェクト設定")) return;

    ImGui::Indent();

    // ---- Default Controlled Character Prefab -------------------------------
    //
    // 生の GUID は常時表示しない。名前とパスだけを出す。
    // GUID は「詳細」を開いたときにだけ見せる。
    ImGui::TextUnformatted("Default Controlled Character Prefab");

    const Project::PrefabReferenceStatus current = resolve_default_character_prefab();

    if (current.IsMissing())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
    }
    const std::string preview = current.DisplayLabel();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##DefaultCharacterPrefab", preview.c_str()))
    {
        if (ImGui::Selectable("（未設定）", current.IsUnset()))
        {
            project_settings.ClearDefaultCharacterPrefab();
            save_project_settings();
        }

        for (const auto& record : asset_database.Records())
        {
            if (!IsPrefabAsset(record)) continue;

            const bool selected = record.guid == project_settings.DefaultCharacterPrefabGuid();
            const std::string label = record.display_name.empty()
                ? record.source_path.filename().generic_string()
                : record.display_name;

            ImGui::PushID(record.guid.c_str());
            if (ImGui::Selectable(label.c_str(), selected))
            {
                // 参照は GUID。Prefab 名を後から変えても維持される。
                project_settings.SetDefaultCharacterPrefabGuid(record.guid);
                save_project_settings();
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (current.IsMissing()) ImGui::PopStyleColor();

    if (current.IsResolved())
    {
        ImGui::TextDisabled("Path: %s", current.path.generic_u8string().c_str());
    }
    else if (current.IsMissing())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
            "この Prefab はプロジェクトに見つかりません");
        ImGui::TextDisabled("取り込み直すと同じ参照で復帰します");
    }
    else
    {
        ImGui::TextDisabled("未設定でも問題ありません（Default Scene が空になるだけです）");
    }

    // 直前に保存した Prefab をそのまま既定にできる導線。
    if (!last_saved_prefab_guid.empty() &&
        last_saved_prefab_guid != project_settings.DefaultCharacterPrefabGuid())
    {
        if (ImGui::Button("直前に保存した Prefab を既定にする"))
        {
            project_settings.SetDefaultCharacterPrefabGuid(last_saved_prefab_guid);
            save_project_settings();
        }
    }

    if (ImGui::TreeNode("詳細##DefaultCharacterPrefab"))
    {
        ImGui::TextDisabled("AssetGUID: %s",
            current.guid.empty() ? "(なし)" : current.guid.c_str());
        ImGui::TreePop();
    }

    ImGui::TextDisabled("%s", project_settings_status.c_str());

    ImGui::Unindent();
#endif
}

void framework::draw_new_object_scene_controls()
{
#ifdef USE_IMGUI
    if (ImGui::Button("新しいシーンを作成...")) ImGui::OpenPopup("NewObjectScenePopup");

    if (ImGui::BeginPopupModal("NewObjectScenePopup", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("シーン名", new_object_scene_name,
            IM_ARRAYSIZE(new_object_scene_name));
        ImGui::Separator();

        // Empty ---------------------------------------------------------------
        ImGui::TextUnformatted("Empty Scene");
        ImGui::TextDisabled("GameObject を 1 つも作りません。操作対象は未設定です。");
        if (ImGui::Button("Empty で作成", { 200.0f, 0.0f }))
        {
            create_object_scene(new_object_scene_name, false);
            ImGui::CloseCurrentPopup();
        }

        ImGui::Separator();

        // Default -------------------------------------------------------------
        ImGui::TextUnformatted("Default Scene");
        const auto prefab = resolve_default_character_prefab();
        if (prefab.IsResolved())
        {
            ImGui::TextDisabled("%s を 1 体だけ配置し、操作対象に設定します",
                prefab.DisplayLabel().c_str());
        }
        else if (prefab.IsMissing())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                "既定の Prefab が見つかりません。空のシーンとして作成します");
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                "既定の Prefab が未設定です。空のシーンとして作成します");
        }

        if (ImGui::Button("Default で作成", { 200.0f, 0.0f }))
        {
            create_object_scene(new_object_scene_name, true);
            ImGui::CloseCurrentPopup();
        }

        ImGui::Separator();
        if (ImGui::Button("キャンセル", { 200.0f, 0.0f })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
#endif
}
