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

#include <algorithm>
#include <cstdio>
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

    ImGui::Separator();

    // ---- Startup Scene ------------------------------------------------------
    //
    // 【Editor が最後に開いた Scene とは別物】
    //   Saved/EditorSession/ には「編集を再開する Scene」が入っている。
    //   あれは作業者ごとの都合。Startup Scene は「ゲームを起動したときに
    //   最初に始まる Scene」で、チーム全員が同じ値を共有する。
    //   混ぜると、誰かが別の Scene を編集しただけで起動先が変わる。
    ImGui::TextUnformatted("Startup Scene（ゲーム起動時に最初に読み込む Scene）");

    const Project::AssetReferenceStatus startup =
        project_settings.ResolveStartupScene(asset_database);

    if (startup.IsMissing())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
    }
    const std::string startup_preview = startup.IsMissing()
        ? std::string("[ Missing Scene ]") : startup.DisplayLabel();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##StartupScene", startup_preview.c_str()))
    {
        if (ImGui::Selectable("（未設定）", startup.IsUnset()))
        {
            // 明示的な Clear。空を許すので、これは正常な設定値。
            project_settings.ClearStartupScene();
            save_project_settings();
        }

        for (const auto& record : asset_database.Records())
        {
            // .replayscene として登録された Asset だけを候補に出す。
            // 種類で絞らないと、Texture を起動先に指定できてしまう。
            if (record.kind != ReplayEngine::Assets::AssetKind::Scene) continue;

            const bool selected = record.guid == project_settings.StartupSceneGuid();
            const std::string label = record.display_name.empty()
                ? record.source_path.filename().generic_string()
                : record.display_name;

            ImGui::PushID(record.guid.c_str());
            if (ImGui::Selectable(label.c_str(), selected))
            {
                // 保存するのは AssetGUID。Scene 名やパスは焼き込まない。
                project_settings.SetStartupSceneGuid(record.guid);
                save_project_settings();
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (startup.IsMissing()) ImGui::PopStyleColor();

    if (startup.IsResolved())
    {
        ImGui::TextDisabled("Path: %s", startup.path.generic_u8string().c_str());
    }
    else if (startup.IsMissing())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
            "この Scene はプロジェクトに見つかりません（設定は保持しています）");
        ImGui::TextDisabled("Scene を取り込み直すと同じ参照で復帰します");
    }
    else
    {
        ImGui::TextDisabled(
            "未設定のまま起動すると、Runtime は開始せず診断状態で停止します");
    }

    if (project_settings.HasStartupScene())
    {
        if (ImGui::Button("Startup Scene を解除##ClearStartupScene"))
        {
            project_settings.ClearStartupScene();
            save_project_settings();
        }
    }

    if (ImGui::TreeNode("詳細##StartupScene"))
    {
        ImGui::TextDisabled("AssetGUID: %s",
            startup.guid.empty() ? "(なし)" : startup.guid.c_str());
        ImGui::TreePop();
    }

    ImGui::Separator();

    // ---- Active Scene Flow --------------------------------------------------
    // Scene 遷移条件そのものは .replaysceneflow Asset に保存し、
    // ProjectSettings は「どの Flow を使うか」だけを GUID で持つ。
    ImGui::TextUnformatted("Active Scene Flow");
    const Project::AssetReferenceStatus flow =
        project_settings.ResolveSceneFlow(asset_database);
    const std::string flow_preview = flow.IsMissing()
        ? std::string("[ Missing Scene Flow ]")
        : (flow.IsResolved() ? (flow.display_name.empty()
            ? flow.path.filename().u8string() : flow.display_name)
            : std::string("（未設定）"));

    if (flow.IsMissing())
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##ActiveSceneFlow", flow_preview.c_str()))
    {
        if (ImGui::Selectable("（未設定）", flow.IsUnset()))
        {
            project_settings.ClearSceneFlow();
            save_project_settings();
            sync_runtime_scene_flow_asset();
        }
        for (const auto& record : asset_database.Records())
        {
            if (record.kind != ReplayEngine::Assets::AssetKind::SceneFlow) continue;
            const bool selected = record.guid == project_settings.SceneFlowGuid();
            const std::string label = record.display_name.empty()
                ? record.source_path.filename().u8string() : record.display_name;
            ImGui::PushID(record.guid.c_str());
            if (ImGui::Selectable(label.c_str(), selected))
            {
                project_settings.SetSceneFlowGuid(record.guid);
                save_project_settings();
                sync_runtime_scene_flow_asset();
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (flow.IsMissing()) ImGui::PopStyleColor();

    if (flow.IsResolved())
        ImGui::TextDisabled("Path: %s", flow.path.generic_u8string().c_str());
    else if (flow.IsMissing())
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
            "この Scene Flow はプロジェクトに見つかりません（参照は保持）");
    else
        ImGui::TextDisabled("未設定なら TriggerSceneFlow は遷移せず、既存 LoadScene はそのまま使えます");

    if (flow.IsResolved() && ImGui::Button("Scene Flow を開く"))
    {
        if (const auto* record = asset_database.FindByGuid(flow.guid))
            load_scene_flow_editor(*record);
    }
    if (project_settings.HasSceneFlow())
    {
        ImGui::SameLine();
        if (ImGui::Button("解除##ClearSceneFlow"))
        {
            project_settings.ClearSceneFlow();
            save_project_settings();
            sync_runtime_scene_flow_asset();
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Localization");
    const ReplayEngine::Assets::AssetRecord* localization_record =
        project_settings.LocalizationTableGuid().empty() ? nullptr :
        asset_database.FindByGuid(project_settings.LocalizationTableGuid());
    const bool localization_missing = !project_settings.LocalizationTableGuid().empty() &&
        (localization_record == nullptr ||
            localization_record->kind != ReplayEngine::Assets::AssetKind::Localization);
    const std::string localization_preview = localization_missing
        ? std::string("[ Missing Localization Table ]")
        : (localization_record != nullptr
            ? (localization_record->display_name.empty()
                ? localization_record->source_path.filename().u8string()
                : localization_record->display_name)
            : std::string("（未設定）"));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##LocalizationTable", localization_preview.c_str()))
    {
        if (ImGui::Selectable("（未設定）", project_settings.LocalizationTableGuid().empty()))
        {
            project_settings.ClearLocalizationTable();
            save_project_settings();
        }
        for (const auto& record : asset_database.Records())
        {
            if (record.kind != ReplayEngine::Assets::AssetKind::Localization) continue;
            const bool selected = record.guid == project_settings.LocalizationTableGuid();
            const std::string label = record.display_name.empty()
                ? record.source_path.filename().u8string() : record.display_name;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                project_settings.SetLocalizationTableGuid(record.guid);
                save_project_settings();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    char language_buffer[64]{};
    std::snprintf(language_buffer, sizeof(language_buffer), "%s",
        project_settings.DefaultLanguage().c_str());
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("既定言語", language_buffer, sizeof(language_buffer)))
    {
        project_settings.SetDefaultLanguage(language_buffer);
        save_project_settings();
    }
    ImGui::TextDisabled("UIText の Localization Key が空なら従来の Text をそのまま表示します。");

    ImGui::Separator();
    ImGui::TextUnformatted("UI Focus Outline");
    bool focus_enabled = project_settings.FocusOutlineEnabled();
    DirectX::XMFLOAT4 focus_color = project_settings.FocusOutlineColor();
    float focus_width = project_settings.FocusOutlineWidth();
    float focus_radius = project_settings.FocusCornerRadius();
    bool focus_changed = false;
    focus_changed |= ImGui::Checkbox("輪郭線を表示##UIFocus", &focus_enabled);
    focus_changed |= ImGui::ColorEdit4("輪郭線色##UIFocus", &focus_color.x);
    focus_changed |= ImGui::DragFloat("輪郭線幅##UIFocus", &focus_width, 0.25f, 0.0f, 32.0f);
    focus_changed |= ImGui::DragFloat("角丸##UIFocus", &focus_radius, 0.25f, 0.0f, 64.0f);
    if (focus_changed)
    {
        project_settings.SetFocusOutlineEnabled(focus_enabled);
        project_settings.SetFocusOutlineColor(focus_color);
        project_settings.SetFocusOutlineWidth((std::max)(0.0f, focus_width));
        project_settings.SetFocusCornerRadius((std::max)(0.0f, focus_radius));
        save_project_settings();
    }

    // 保存の結果はここへ出る。失敗した場合も同じ場所に理由が出る。
    ImGui::TextDisabled("%s", project_settings_status.c_str());

    ImGui::Unindent();

    // Runtime の読み取り専用診断。Runtime 側から Editor は一切参照しない。
    draw_runtime_diagnostics_panel();
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
