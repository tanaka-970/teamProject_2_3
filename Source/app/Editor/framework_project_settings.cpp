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
#include "../../RePlayEngine/Object/Registry/ComponentRegistry.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

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
    if (!object_editor_context.CanEdit())
    {
        ImGui::TextDisabled("Play 中はプロジェクト設定を編集できません");
        draw_runtime_diagnostics_panel();
        return;
    }

    // ProjectSettings は Scene 外ファイルなので SceneEditHistory では復元できない。
    // snapshot は「実際に save する直前」に save_project_settings() が開始する。
    // パネルを開いているだけで毎フレーム disk read しない。
    project_settings_file_undo_enabled = object_editor_context.CanEdit();

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
            if (!IsPrefabAsset(record) || asset_database.IsMissing(record.guid)) continue;

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
        ReplayEngine::Editor::EditorHelp::Item("button.project.use_last_prefab",
            u8"直前に保存した Prefab を Default Controlled Character に設定します。");
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
            if (asset_database.IsMissing(record.guid)) continue;

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
        ReplayEngine::Editor::EditorHelp::Item("button.project.clear_startup_scene",
            u8"起動時に読み込む Startup Scene の指定を解除します。");
    }

    if (ImGui::TreeNode("詳細##StartupScene"))
    {
        ImGui::TextDisabled("AssetGUID: %s",
            startup.guid.empty() ? "(なし)" : startup.guid.c_str());
        ImGui::TreePop();
    }

    ImGui::Separator();

    // ---- Loading Screen Scene -----------------------------------------------
    ImGui::TextUnformatted("Loading Screen Scene（ロード画面）");
    const Project::AssetReferenceStatus loading =
        project_settings.ResolveLoadingScene(asset_database);
    const std::string loading_preview = loading.IsMissing()
        ? std::string("[ Missing Scene ]") : loading.DisplayLabel();

    if (loading.IsMissing())
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##LoadingScene", loading_preview.c_str()))
    {
        if (ImGui::Selectable("（未設定）", loading.IsUnset()))
        {
            project_settings.ClearLoadingScene();
            save_project_settings();
        }

        for (const auto& record : asset_database.Records())
        {
            if (record.kind != ReplayEngine::Assets::AssetKind::Scene) continue;
            if (asset_database.IsMissing(record.guid)) continue;

            const bool selected = record.guid == project_settings.LoadingSceneGuid();
            const std::string label = record.display_name.empty()
                ? record.source_path.filename().generic_string()
                : record.display_name;
            ImGui::PushID(record.guid.c_str());
            if (ImGui::Selectable(label.c_str(), selected))
            {
                project_settings.SetLoadingSceneGuid(record.guid);
                save_project_settings();
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (loading.IsMissing()) ImGui::PopStyleColor();

    if (loading.IsResolved())
    {
        ImGui::TextDisabled("Path: %s", loading.path.generic_u8string().c_str());
    }
    else if (loading.IsMissing())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
            "この Loading Screen Scene はプロジェクトに見つかりません（参照は保持）");
    }
    else
    {
        ImGui::TextDisabled("未指定なら従来の LoadingScene を使用します");
    }

    if (project_settings.HasLoadingScene())
    {
        if (ImGui::Button("Loading Screen Scene を解除##ClearLoadingScene"))
        {
            project_settings.ClearLoadingScene();
            save_project_settings();
        }
    }

    if (ImGui::TreeNode("詳細##LoadingScene"))
    {
        ImGui::TextDisabled("AssetGUID: %s",
            loading.guid.empty() ? "(なし)" : loading.guid.c_str());
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
            if (asset_database.IsMissing(record.guid)) continue;
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

    if (flow.IsResolved())
    {
        if (ImGui::Button("Scene Flow を開く"))
        {
            if (const auto* record = asset_database.FindByGuid(flow.guid))
                load_scene_flow_editor(*record);
        }
        ReplayEngine::Editor::EditorHelp::Item("button.project.open_scene_flow",
            u8"設定中の Scene Flow Asset を開いて編集します。");
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
        ReplayEngine::Editor::EditorHelp::Item("button.project.clear_scene_flow",
            u8"Active Scene Flow の指定を解除します。");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Input Action Asset");
    const ReplayEngine::Assets::AssetRecord* input_record =
        project_settings.InputActionAssetGuid().empty() ? nullptr :
        asset_database.FindByGuid(project_settings.InputActionAssetGuid());
    const bool input_missing = !project_settings.InputActionAssetGuid().empty() &&
        (input_record == nullptr || input_record->kind != ReplayEngine::Assets::AssetKind::InputAction);
    const std::string input_preview = input_missing
        ? std::string("[ Missing Input Action Asset ]")
        : (input_record != nullptr
            ? (input_record->display_name.empty()
                ? input_record->source_path.filename().u8string()
                : input_record->display_name)
            : std::string("（ハードコード既定値）"));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##InputActionAsset", input_preview.c_str()))
    {
        if (ImGui::Selectable("（ハードコード既定値）",
            project_settings.InputActionAssetGuid().empty()))
        {
            project_settings.ClearInputActionAsset();
            save_project_settings();
            load_active_input_action_asset();
        }
        for (const auto& record : asset_database.Records())
        {
            if (record.kind != ReplayEngine::Assets::AssetKind::InputAction) continue;
            if (asset_database.IsMissing(record.guid)) continue;
            const bool selected = record.guid == project_settings.InputActionAssetGuid();
            const std::string label = record.display_name.empty()
                ? record.source_path.filename().u8string() : record.display_name;
            if (ImGui::Selectable(label.c_str(), selected))
            {
                project_settings.SetInputActionAssetGuid(record.guid);
                save_project_settings();
                load_active_input_action_asset();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (input_missing)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
            "Input Asset が見つからないため、現在のハードコード既定値へフォールバックします。");
    else
        ImGui::TextDisabled("Asset が無い/壊れている場合も ResetDefaultBindings() へ安全に戻ります。");

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
            if (asset_database.IsMissing(record.guid)) continue;
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

    if (external_file_history.InTransaction() &&
        !ImGui::IsAnyItemActive())
    {
        std::string undo_error;
        external_file_history.Commit(undo_error);
        if (!undo_error.empty()) project_settings_status = undo_error;
    }
    project_settings_file_undo_enabled = false;
#endif
}

void framework::draw_new_object_scene_controls()
{
#ifdef USE_IMGUI
    if (ImGui::Button("新しいシーンを作成...")) ImGui::OpenPopup("NewObjectScenePopup");
    ReplayEngine::Editor::EditorHelp::Item("button.scene.new",
        u8"新しい Scene の名前と初期内容を選ぶダイアログを開きます。");

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
        ReplayEngine::Editor::EditorHelp::Item("button.scene.create_empty",
            u8"GameObject を持たない空の Scene を作成します。");

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
        ReplayEngine::Editor::EditorHelp::Item("button.scene.create_default",
            u8"既定の Prefab を配置した Scene を作成します。未設定なら空の Scene になります。");

        ImGui::Separator();
        if (ImGui::Button("キャンセル", { 200.0f, 0.0f })) ImGui::CloseCurrentPopup();
        ReplayEngine::Editor::EditorHelp::Item("button.scene.cancel_new",
            u8"新しい Scene を作成せず、ダイアログを閉じます。");
        ImGui::EndPopup();
    }
#endif
}

// 種類別アイコンの設定を独立ウィンドウへ表示する。
void framework::draw_icon_settings_panel()
{
#ifdef USE_IMGUI
    if (!show_icon_settings_window) return;
    ImGui::SetNextWindowSize(ImVec2(760.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(u8"種類別アイコン", &show_icon_settings_window))
    {
        ImGui::End();
        return;
    }

        ImGui::PushID("ProjectIcons");
        const auto& atlas_guids = project_settings.IconAtlasGuids();
        const auto& selected_regions = project_settings.IconRegions();
        const std::size_t selected_count = static_cast<std::size_t>(std::count_if(
            selected_regions.begin(), selected_regions.end(), [](const auto& entry)
            { return !entry.second.region.empty(); }));
        if (atlas_guids.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                "アトラスを指定してください");
        else if (project_settings.IconAutoMatchByName())
            ImGui::Text("名前が一致するアイコンを自動で使います（%zu 件を個別指定）",
                selected_count);
        else
            ImGui::Text("選んだアイコンだけが出ます（%zu 件指定済み）", selected_count);
        // 設定の中身ではなく、アトラスを実際に読めたかを出す。出ない原因の切り分け用。
        if (!editor_icon_provider.AtlasError().empty())
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                "%s", editor_icon_provider.AtlasError().c_str());
        else if (!atlas_guids.empty())
            ImGui::TextDisabled("読み込み済み: アトラス %zu 枚 / 領域 %zu 件",
                editor_icon_provider.LoadedAtlasCount(), editor_icon_provider.RegionCount());
        ImGui::Separator();
        ImGui::Text("アイコン用アトラス（最大 %zu 枚）",
            ReplayEngine::Project::ProjectSettings::maximum_icon_atlas_count);
        ImGui::TextDisabled("上にあるものが優先されます。名前が重なったときの順番です");
        {
            // 枠ごとに 1 行。差し替えと削除をその場でできるようにする。
            std::vector<std::string> updated = atlas_guids;
            bool atlas_changed = false;
            for (std::size_t slot = 0; slot < updated.size(); ++slot)
            {
                ImGui::PushID(static_cast<int>(slot));
                const auto* record = asset_database.FindByGuid(updated[slot]);
                const bool slot_missing = record == nullptr ||
                    record->kind != ReplayEngine::Assets::AssetKind::SpriteAtlas ||
                    asset_database.IsMissing(updated[slot]);
                const std::string preview = slot_missing ? "アトラス不明"
                    : (record->display_name.empty()
                        ? record->source_path.filename().u8string() : record->display_name);
                ImGui::SetNextItemWidth(-120.0f);
                if (ImGui::BeginCombo("##IconAtlasSlot", preview.c_str()))
                {
                    for (const auto& candidate : asset_database.Records())
                    {
                        if (candidate.kind != ReplayEngine::Assets::AssetKind::SpriteAtlas ||
                            asset_database.IsMissing(candidate.guid)) continue;
                        const std::string label = candidate.display_name.empty()
                            ? candidate.source_path.filename().u8string() : candidate.display_name;
                        ImGui::PushID(candidate.guid.c_str());
                        const bool is_selected = updated[slot] == candidate.guid;
                        if (ImGui::Selectable(label.c_str(), is_selected))
                        {
                            updated[slot] = candidate.guid;
                            atlas_changed = true;
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::Button("外す"))
                {
                    updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(slot));
                    atlas_changed = true;
                    ImGui::PopID();
                    break;
                }
                if (!slot_missing)
                {
                    ImGui::TextDisabled("  %s", record->source_path.generic_u8string().c_str());
                }
                ImGui::PopID();
            }
            if (updated.size() <
                ReplayEngine::Project::ProjectSettings::maximum_icon_atlas_count)
            {
                if (ImGui::Button("アトラスを追加")) ImGui::OpenPopup("AddIconAtlas");
                if (ImGui::BeginPopup("AddIconAtlas"))
                {
                    bool any = false;
                    for (const auto& candidate : asset_database.Records())
                    {
                        if (candidate.kind != ReplayEngine::Assets::AssetKind::SpriteAtlas ||
                            asset_database.IsMissing(candidate.guid)) continue;
                        if (std::find(updated.begin(), updated.end(), candidate.guid) !=
                            updated.end()) continue;
                        any = true;
                        const std::string label = candidate.display_name.empty()
                            ? candidate.source_path.filename().u8string() : candidate.display_name;
                        ImGui::PushID(candidate.guid.c_str());
                        if (ImGui::Selectable(label.c_str()))
                        {
                            updated.push_back(candidate.guid);
                            atlas_changed = true;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopID();
                    }
                    if (!any) ImGui::TextDisabled("追加できるアトラスがありません");
                    ImGui::EndPopup();
                }
            }
            if (atlas_changed)
            {
                project_settings.SetIconAtlasGuids(std::move(updated));
                save_project_settings();
            }
        }
        if (ImGui::Button("アイコンを再読込")) editor_icons_reload_pending = true;
        // 既定は off。選んだものだけ出す方が、意図しない絵が混ざらない。
        bool auto_match = project_settings.IconAutoMatchByName();
        if (ImGui::Checkbox("名前が同じ領域を自動で使う", &auto_match))
        {
            project_settings.SetIconAutoMatchByName(auto_match);
            editor_icon_provider.InvalidateResolved();
            save_project_settings();
        }
        ImGui::TextDisabled("off のときは、下で選んだアイコンだけが出ます");

        ImGui::TextWrapped("乗算色は型名、カテゴリ、スタイルのカテゴリ色の順で決まります。"
            "オブジェクト種別の既定色は白です。");
        const auto begin_icon_tint_columns = [](const char* id)
        {
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float preview_width = ImGui::GetFrameHeight() + gap * 2.0f;
            const float reset_width = ImGui::CalcTextSize("既定へ戻す").x
                + ImGui::GetStyle().FramePadding.x * 2.0f + gap * 2.0f;
            const float remaining = (std::max)(1.0f,
                ImGui::GetContentRegionAvail().x - preview_width - reset_width);
            ImGui::Columns(4, id, false);
            ImGui::SetColumnWidth(0, remaining * 0.4f);
            ImGui::SetColumnWidth(1, preview_width);
            ImGui::SetColumnWidth(2, remaining * 0.6f);
        };
        const auto draw_icon_tint_row = [this, auto_match](const std::string& key,
            const std::string& category, const ReplayEngine::Core::ComponentTypeInfo* info)
        {
            ImGui::PushID(key.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(key.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", key.c_str());
            ImGui::NextColumn();
            const ImVec2 preview_position = ImGui::GetCursorScreenPos();
            const float size = ImGui::GetFrameHeight();
            const auto& chosen = project_settings.IconRegions();
            const auto picked = chosen.find(key);
            // 押すとアトラスの絵から選べる。既定は「キーと同じ名前の領域」。
            if (ImGui::Button("##PickIcon", ImVec2(size, size))) ImGui::OpenPopup("IconPicker");
            // Columns は列ごとに別チャンネルへ描くので、必ずこの列にいる間に重ねる。
            {
                const auto preview = info != nullptr
                    ? editor_icon_provider.ResolveComponent(*info)
                    : editor_icon_provider.Resolve(key, category);
                if (preview.texture != nullptr)
                    ImGui::GetWindowDrawList()->AddImage(preview.texture, preview_position,
                        ImVec2(preview_position.x + size, preview_position.y + size),
                        preview.uv0, preview.uv1, ImGui::GetColorU32(preview.tint));
            }
            if (!auto_match && picked == chosen.end())
            {
                const ImVec2 dots = ImGui::CalcTextSize("...");
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(preview_position.x + (size - dots.x) * 0.5f,
                        preview_position.y + (size - dots.y) * 0.5f),
                    ImGui::GetColorU32(ImVec4(0.65f, 0.65f, 0.65f, 0.65f)), "...");
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(picked == chosen.end()
                    ? (auto_match ? "押して選ぶ（いまは名前で自動一致）"
                        : "押して選ぶ（いまは未指定）")
                    : (picked->second.region.empty() ? "押して選ぶ（いまは出さない指定）"
                        : ("押して選ぶ（いま: " + picked->second.region + "）").c_str()));
            }
            if (ImGui::BeginPopup("IconPicker"))
            {
                if (ImGui::Button("名前で自動一致へ戻す"))
                {
                    project_settings.ClearIconRegion(key);
                    editor_icon_provider.InvalidateResolved();
                    save_project_settings();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("出さない"))
                {
                    project_settings.SetIconRegion(key, std::string{});
                    editor_icon_provider.InvalidateResolved();
                    save_project_settings();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::Separator();
                const auto names = editor_icon_provider.RegionNames();
                if (names.empty()) ImGui::TextDisabled("アトラスが未指定です");
                const float cell = ImGui::GetTextLineHeight() * 2.0f;
                constexpr int per_row = 10;
                for (std::size_t index = 0; index < names.size(); ++index)
                {
                    ImGui::PushID(static_cast<int>(index));
                    const auto choice = editor_icon_provider.IconForRegion(
                        names[index].name, names[index].atlas_guid);
                    const bool clicked = choice.texture != nullptr
                        ? ImGui::ImageButton(choice.texture, ImVec2(cell, cell),
                            choice.uv0, choice.uv1, 2)
                        : ImGui::Button("?", ImVec2(cell, cell));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", names[index].name.c_str());
                    if (clicked)
                    {
                        project_settings.SetIconRegion(key, names[index].name,
                            names[index].atlas_guid);
                        editor_icon_provider.InvalidateResolved();
                        save_project_settings();
                        ImGui::CloseCurrentPopup();
                    }
                    if ((index + 1) % per_row != 0 && index + 1 < names.size())
                        ImGui::SameLine();
                    ImGui::PopID();
                }
                ImGui::EndPopup();
            }
            ImGui::NextColumn();
            ImVec4 color = editor_icon_provider.ResolveTint(key, category);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::ColorEdit4("##Tint", &color.x, ImGuiColorEditFlags_AlphaPreviewHalf))
            {
                project_settings.SetIconTint(key, { color.x, color.y, color.z, color.w });
                save_project_settings();
            }
            ImGui::NextColumn();
            if (ImGui::Button("既定へ戻す") &&
                project_settings.IconTints().find(key) != project_settings.IconTints().end())
            {
                project_settings.ClearIconTint(key);
                save_project_settings();
            }
            ImGui::NextColumn();
            ImGui::PopID();
        };

        ImGui::Separator();
        ImGui::TextUnformatted("オブジェクト種別");
        begin_icon_tint_columns("ObjectTints");
        const char* object_keys[] = { "object.folder", "object.empty", "object.prefab_root",
            "object.prefab_instance", "object.prefab_missing", "object.controlled", "object.inactive" };
        for (const char* key : object_keys) draw_icon_tint_row(key, {}, nullptr);
        ImGui::Columns(1);

        std::vector<std::string> categories;
        for (const auto& info : ReplayEngine::Core::ComponentRegistry::All())
            if (std::find(categories.begin(), categories.end(), info.category) == categories.end())
                categories.push_back(info.category);
        std::sort(categories.begin(), categories.end());
        ImGui::Separator();
        ImGui::TextUnformatted("カテゴリ");
        begin_icon_tint_columns("CategoryTints");
        for (const auto& category : categories) draw_icon_tint_row(category, category, nullptr);
        ImGui::Columns(1);

        ImGui::Separator();
        ImGui::TextUnformatted("型名");
        for (const auto& category : categories)
        {
            ImGui::PushID(category.c_str());
            if (ImGui::TreeNode("Types", "%s", category.empty() ? "カテゴリなし" : category.c_str()))
            {
                begin_icon_tint_columns("TypeTints");
                std::vector<const ReplayEngine::Core::ComponentTypeInfo*> types;
                for (const auto& info : ReplayEngine::Core::ComponentRegistry::All())
                    if (info.category == category) types.push_back(&info);
                std::sort(types.begin(), types.end(), [](const auto* left, const auto* right)
                    { return left->type_name < right->type_name; });
                for (const auto* info : types) draw_icon_tint_row(info->type_name, category, info);
                ImGui::Columns(1);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::End();
#endif
}
