// Editor のうち「Project / Console / Workspace パネル」だけを持つ。
#include "framework.h"

#include "../../RePlayEngine/Components/Core/PivotComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Editor/Inspector/PropertyDrawer.h"
#include "../../RePlayEngine/Editor/ReorderableList.h"
#include "../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../RePlayEngine/Rendering/Effects/EffectPresetAsset.h"
#include "../../RePlayEngine/UI/Effects/EffectKindLabels.h"
#include "../../RePlayEngine/Localization/LocalizationTable.h"
#include "../../RePlayEngine/Localization/LocalizationService.h"
#include "../../RePlayEngine/Rendering/Shaders/ShaderCatalog.h"
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

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <cctype>
#include <cstddef>
#include <string>
#include <sstream>
#include <vector>
#include <utility>
void framework::draw_project_panel()
{
    REPLAY_PROFILE_SCOPE("Editor/Project");
    ReplayEngine::Editor::PanelTabColorScope panel_tab_color("Core");
    ImGui::Begin("プロジェクト");
    // GameObject Scene (.replayscene) is the only authoring format.
    if (ImGui::CollapsingHeader("GameObject シーン", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("現在: %s", active_object_scene().Name().c_str());
        ImGui::TextDisabled("%s", object_scene_path.generic_u8string().c_str());
        draw_new_object_scene_controls();
        ImGui::SameLine();
        if (ImGui::Button("Prefabとして保存...")) save_selected_prefab(true);
        ReplayEngine::Editor::EditorHelp::Item("button.project.save_prefab",
            u8"選択中の Scene を Prefab Asset として保存します。");
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
        ReplayEngine::Editor::EditorHelp::Item("button.csharp.rebuild_all",
            u8"Catalog の更新と Assembly の再コンパイルをまとめて行います。\n迷ったらこれを押してください。");

        ImGui::SameLine();
        ImGui::Checkbox(u8"自動更新", &csharp_auto_reload);
        ReplayEngine::Editor::EditorHelp::Item("control.csharp.auto_reload",
            u8".cs を保存すると自動で再コンパイルします。\n"
            u8"失敗しても直前に成功した Assembly を使い続けるので、\n"
            u8"編集中の状態は壊れません。");

        ImGui::SameLine();
        if (ImGui::Button("Open Selected .cs")) open_selected_csharp_asset();
        ReplayEngine::Editor::EditorHelp::Item("button.csharp.open_selected",
            u8"Project Browser で選択中の C# Script を Visual Studio で開きます。");

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
            ReplayEngine::Editor::EditorHelp::Item("button.csharp.refresh_catalog",
                u8"C# Script の一覧を走査して Catalog を更新します。");
            ImGui::SameLine();
            if (ImGui::Button("Build && Reload C#")) build_and_reload_csharp_scripts();
            ReplayEngine::Editor::EditorHelp::Item("button.csharp.build_reload",
                u8"C# Script をビルドし、成功した Assembly をエディタへ読み込みます。");
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
    ReplayEngine::Editor::EditorHelp::Item("button.asset.import_model",
        u8"モデルファイルを選び、Project の Asset として取り込みます。");
    ImGui::SameLine();
    if (ImGui::Button("Prefabを配置...")) load_prefab();
    ReplayEngine::Editor::EditorHelp::Item("button.asset.place_prefab",
        u8"Prefab Asset を選んで現在の Scene へ配置します。");
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
    if (selected_asset != nullptr)
    {
        if (ImGui::Button("選択AssetをSceneへ配置"))
            place_asset_in_object_scene(*selected_asset, asset_drop_add_collider);
        ReplayEngine::Editor::EditorHelp::Item("button.asset.place_selected",
            u8"Colliderは左の設定が有効な場合だけ明示的に追加します");
    }
    if (selected_asset != nullptr &&
        (selected_asset->kind == ReplayEngine::Assets::AssetKind::Script ||
            selected_asset->source_path.extension() == ".cs"))
    {
        ImGui::SameLine();
        if (ImGui::Button("Visual Studioで開く")) open_selected_csharp_asset();
        ReplayEngine::Editor::EditorHelp::Item("button.asset.open_script",
            u8"選択中の Script Asset を Visual Studio で開きます。");
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
        ReplayEngine::Editor::EditorHelp::Item("button.asset.open_shader",
            u8"選択中の Shader Asset を Visual Studio で開きます。");
    }
    draw_material_asset_editor();

    // Localization Table editor。Scene 外 Asset なので FileEditHistory で Undo する。
    if (selected_asset != nullptr &&
        selected_asset->kind == ReplayEngine::Assets::AssetKind::Localization)
    {
        using ReplayEngine::Localization::LocalizationTable;
        static std::string editing_guid;
        static LocalizationTable table;
        static bool loaded = false;
        static std::string status;
        static std::string selected_key;
        static std::filesystem::file_time_type file_time{};
        static std::uint64_t reload_generation = 0;
        static char new_key[128]{};

        const auto reload = [&]()
        {
            std::string error;
            loaded = table.LoadFromFile(selected_asset->source_path, error);
            status = loaded ? "Localization Table を読み込みました" : error;
            if (loaded && !selected_key.empty() && !table.HasKey(selected_key)) selected_key.clear();
            std::error_code time_error;
            file_time = std::filesystem::last_write_time(selected_asset->source_path, time_error);
        };
        std::error_code time_error;
        const auto current_time = std::filesystem::last_write_time(selected_asset->source_path, time_error);
        if (editing_guid != selected_asset->guid ||
            reload_generation != external_file_reload_generation ||
            (!ImGui::IsAnyItemActive() && !time_error && current_time != file_time))
        {
            editing_guid = selected_asset->guid;
            reload_generation = external_file_reload_generation;
            reload();
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Localization Table Asset", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Button("再読み込み##LocalizationAsset")) reload();
            ReplayEngine::Editor::EditorHelp::Item("button.localization.reload",
                u8"保存済みの Localization Table を読み直します。");
            if (loaded && object_editor_context.CanEdit())
            {
                bool changed = false;
                std::string languages_text;
                for (std::size_t i = 0; i < table.Languages().size(); ++i)
                {
                    if (i != 0) languages_text += ", ";
                    languages_text += table.Languages()[i];
                }
                char languages[512]{};
                std::snprintf(languages, sizeof(languages), "%s", languages_text.c_str());
                if (ImGui::InputText("Languages (comma)", languages, sizeof(languages)))
                {
                    std::vector<std::string> values;
                    std::istringstream stream(languages);
                    std::string value;
                    while (std::getline(stream, value, ','))
                    {
                        const auto first = value.find_first_not_of(" \t");
                        const auto last = value.find_last_not_of(" \t");
                        if (first != std::string::npos) values.push_back(value.substr(first, last - first + 1));
                    }
                    table.SetLanguages(std::move(values));
                    changed = true;
                }

                ImGui::InputText("New Key", new_key, sizeof(new_key));
                ImGui::SameLine();
                if (ImGui::Button("追加##LocalizationKey") && new_key[0] != '\0')
                {
                    const std::string key = new_key;
                    const std::string language = table.Languages().empty() ? "ja" : table.Languages().front();
                    table.Set(key, language, "");
                    selected_key = key;
                    new_key[0] = '\0';
                    changed = true;
                }
                ReplayEngine::Editor::EditorHelp::Item("button.localization.add_key",
                    u8"入力した名前の Localization Key を追加します。空の翻訳欄から始まります。");

                ImGui::BeginChild("LocalizationKeys", ImVec2(180.0f, 220.0f), true);
                for (const std::string& key : table.Keys())
                {
                    if (ImGui::Selectable(key.c_str(), selected_key == key)) selected_key = key;
                }
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginGroup();
                if (!selected_key.empty())
                {
                    ImGui::Text("Key: %s", selected_key.c_str());
                    for (const std::string& language : table.Languages())
                    {
                        std::string text = table.Resolve(selected_key, language, "");
                        char buffer[1024]{};
                        std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
                        ImGui::PushID(language.c_str());
                        if (ImGui::InputTextMultiline(language.c_str(), buffer, sizeof(buffer), ImVec2(360.0f, 55.0f)))
                        {
                            table.Set(selected_key, language, buffer);
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                    if (ImGui::Button("このKeyを削除"))
                    {
                        table.RemoveKey(selected_key);
                        selected_key.clear();
                        changed = true;
                    }
                    ReplayEngine::Editor::EditorHelp::Item("button.localization.remove_key",
                        u8"選択中の Localization Key と翻訳を削除します。");
                }
                else ImGui::TextDisabled("左からKeyを選択してください");
                ImGui::EndGroup();

                if (changed && object_editor_context.CanEdit())
                {
                    std::string error;
                    bool undo_ready = external_file_history.InTransaction();
                    if (!undo_ready)
                        undo_ready = external_file_history.Begin(selected_asset->source_path,
                            "Localization Table を編集", error);
                    if (!undo_ready)
                    {
                        if (!error.empty()) status = error;
                    }
                    else if (table.SaveToFile(selected_asset->source_path, error))
                    {
                        status = "Localization Table を保存しました";
                        auto& service = ReplayEngine::Localization::LocalizationService::Global();
                        const std::string guid = project_settings.LocalizationTableGuid();
                        service.SetTableGuid("");
                        service.Configure(&asset_database, guid, project_settings.DefaultLanguage());
                        std::error_code e;
                        file_time = std::filesystem::last_write_time(selected_asset->source_path, e);
                    }
                    else status = error;
                }
            }
            else if (loaded)
            {
                ImGui::TextDisabled("Play 中は Localization Asset を編集できません");
            }
            if (external_file_history.InTransaction() && !ImGui::IsAnyItemActive())
            {
                std::string error;
                external_file_history.Commit(error);
                if (!error.empty()) status = error;
            }
            if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
        }
    }

    // Input Action Asset editor。既存 Action 名は変更せず Binding / Action Map のみ編集する。
    if (selected_asset != nullptr &&
        selected_asset->kind == ReplayEngine::Assets::AssetKind::InputAction)
    {
        static std::string editing_guid;
        static GameInput::InputState editing_input;
        static bool loaded = false;
        static std::string status;
        static std::filesystem::file_time_type file_time{};
        static std::uint64_t reload_generation = 0;

        const auto reload = [&]()
        {
            std::string error;
            loaded = editing_input.LoadActionAsset(selected_asset->source_path, error);
            status = loaded ? "Input Action Asset を読み込みました" :
                (error.empty() ? "Input Asset は不正です。ハードコード既定値を使用します" : error);
            std::error_code e;
            file_time = std::filesystem::last_write_time(selected_asset->source_path, e);
        };
        std::error_code time_error2;
        const auto current_time2 = std::filesystem::last_write_time(selected_asset->source_path, time_error2);
        if (editing_guid != selected_asset->guid ||
            reload_generation != external_file_reload_generation ||
            (!ImGui::IsAnyItemActive() && !time_error2 && current_time2 != file_time))
        {
            editing_guid = selected_asset->guid;
            reload_generation = external_file_reload_generation;
            reload();
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Input Action Asset", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("Action名は既存互換のため固定。Binding / Action Mapだけを編集します。");
            if (ImGui::Button("再読み込み##InputActionAsset")) reload();
            ReplayEngine::Editor::EditorHelp::Item("button.input_action.reload",
                u8"保存済みの Input Action Asset を読み直します。");
            if (loaded && object_editor_context.CanEdit())
            {
                bool changed = false;
                std::vector<std::string> action_names;
                action_names.reserve(editing_input.Actions().size());
                for (const auto& pair : editing_input.Actions())
                    if (pair.second.action_map != "Editor" && pair.second.action_map != "Motion")
                        action_names.push_back(pair.first);
                std::sort(action_names.begin(), action_names.end());
                if (ImGui::TreeNodeEx("Actions", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    for (const std::string& name : action_names)
                    {
                        auto binding = editing_input.Actions().at(name);
                        ImGui::PushID(name.c_str());
                        if (ImGui::TreeNode(name.c_str()))
                        {
                            char map[64]{};
                            std::snprintf(map, sizeof(map), "%s", binding.action_map.c_str());
                            changed = ImGui::InputText("Action Map", map, sizeof(map)) || changed;
                            if (std::string(map) != binding.action_map) binding.action_map = map;
                            changed = ImGui::InputInt("Keyboard Primary (VK)", &binding.keyboard_primary) || changed;
                            changed = ImGui::InputInt("Keyboard Secondary (VK)", &binding.keyboard_secondary) || changed;
                            int primary_modifiers = binding.keyboard_primary_modifiers;
                            changed = ImGui::CheckboxFlags("Primary Ctrl", &primary_modifiers,
                                GameInput::ActionModifierCtrl) || changed;
                            ImGui::SameLine();
                            changed = ImGui::CheckboxFlags("Primary Shift", &primary_modifiers,
                                GameInput::ActionModifierShift) || changed;
                            ImGui::SameLine();
                            changed = ImGui::CheckboxFlags("Primary Alt", &primary_modifiers,
                                GameInput::ActionModifierAlt) || changed;
                            binding.keyboard_primary_modifiers = static_cast<std::uint8_t>(
                                primary_modifiers & GameInput::ActionModifierAll);
                            int secondary_modifiers = binding.keyboard_secondary_modifiers;
                            changed = ImGui::CheckboxFlags("Secondary Ctrl", &secondary_modifiers,
                                GameInput::ActionModifierCtrl) || changed;
                            ImGui::SameLine();
                            changed = ImGui::CheckboxFlags("Secondary Shift", &secondary_modifiers,
                                GameInput::ActionModifierShift) || changed;
                            ImGui::SameLine();
                            changed = ImGui::CheckboxFlags("Secondary Alt", &secondary_modifiers,
                                GameInput::ActionModifierAlt) || changed;
                            binding.keyboard_secondary_modifiers = static_cast<std::uint8_t>(
                                secondary_modifiers & GameInput::ActionModifierAll);
                            int button = static_cast<int>(binding.gamepad_button);
                            if (ImGui::InputInt("Gamepad Button", &button))
                            {
                                binding.gamepad_button = static_cast<WORD>((std::max)(0, (std::min)(65535, button)));
                                changed = true;
                            }
                            editing_input.SetActionBinding(name, binding);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }

                std::vector<std::string> axis_names;
                axis_names.reserve(editing_input.Axes().size());
                for (const auto& pair : editing_input.Axes()) axis_names.push_back(pair.first);
                std::sort(axis_names.begin(), axis_names.end());
                if (ImGui::TreeNode("Axes"))
                {
                    for (const std::string& name : axis_names)
                    {
                        auto binding = editing_input.Axes().at(name);
                        ImGui::PushID(name.c_str());
                        if (ImGui::TreeNode(name.c_str()))
                        {
                            char map[64]{};
                            std::snprintf(map, sizeof(map), "%s", binding.action_map.c_str());
                            if (ImGui::InputText("Action Map", map, sizeof(map))) { binding.action_map = map; changed = true; }
                            changed = ImGui::InputInt("Negative Primary", &binding.negative_primary) || changed;
                            changed = ImGui::InputInt("Negative Secondary", &binding.negative_secondary) || changed;
                            changed = ImGui::InputInt("Positive Primary", &binding.positive_primary) || changed;
                            changed = ImGui::InputInt("Positive Secondary", &binding.positive_secondary) || changed;
                            int axis = static_cast<int>(binding.gamepad_axis);
                            if (ImGui::InputInt("Gamepad Axis", &axis))
                            {
                                axis = (std::max)(0, (std::min)(6, axis));
                                binding.gamepad_axis = static_cast<GameInput::GamepadAxis>(axis);
                                changed = true;
                            }
                            changed = ImGui::DragFloat("Dead Zone", &binding.dead_zone, 0.01f, 0.0f, 1.0f) || changed;
                            editing_input.SetAxisBinding(name, binding);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                if (changed && object_editor_context.CanEdit())
                {
                    std::string error;
                    bool undo_ready = external_file_history.InTransaction();
                    if (!undo_ready)
                        undo_ready = external_file_history.Begin(selected_asset->source_path,
                            "Input Action Asset を編集", error);
                    if (!undo_ready)
                    {
                        if (!error.empty()) status = error;
                    }
                    else if (editing_input.SaveActionAsset(selected_asset->source_path, error))
                    {
                        status = "Input Action Asset を保存しました";
                        if (selected_asset->guid == project_settings.InputActionAssetGuid())
                            load_active_input_action_asset();
                        std::error_code e;
                        file_time = std::filesystem::last_write_time(selected_asset->source_path, e);
                    }
                    else status = error;
                }
            }
            else if (loaded)
            {
                ImGui::TextDisabled("Play 中は Input Action Asset を編集できません");
            }
            if (external_file_history.InTransaction() && !ImGui::IsAnyItemActive())
            {
                std::string error;
                external_file_history.Commit(error);
                if (!error.empty()) status = error;
            }
            if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
        }
    }

    // Effect Preset は UI / Model / Screen で同じ Effect 列を共有する Asset。
    // UIEffectStackComponent の Dynamic Property をそのまま編集器として使うことで、
    // Effect ごとに別の Inspector 実装を増やさず、既存 Stack と同じ操作感を維持する。
    if (selected_asset != nullptr &&
        selected_asset->kind == ReplayEngine::Assets::AssetKind::EffectPreset)
    {
        using ReplayEngine::Components::UIEffectStackComponent;
        using ReplayEngine::Editor::PropertyDrawer;
        using ReplayEngine::Reflection::PropertyRegistry;
        using ReplayEngine::Rendering::Effects::EffectPresetAsset;

        static std::string editing_effect_preset_guid;
        static UIEffectStackComponent editing_effect_stack;
        static bool effect_preset_loaded = false;
        static std::string effect_preset_status;
        static std::filesystem::file_time_type effect_preset_file_time{};
        static std::uint64_t effect_preset_reload_generation = 0;

        const auto refresh_effect_preset_schemas = [&]()
        {
            using ReplayEngine::Assets::AssetKind;
            using ReplayEngine::Rendering::ShaderCatalog;
            using ReplayEngine::Rendering::ShaderDomain;

            const auto normalize = [](std::filesystem::path path)
            {
                std::error_code error;
                std::filesystem::path absolute = path.is_absolute()
                    ? path : std::filesystem::absolute(path, error);
                if (error) absolute = path;
                error.clear();
                const std::filesystem::path canonical =
                    std::filesystem::weakly_canonical(absolute, error);
                return error ? absolute.lexically_normal() : canonical.lexically_normal();
            };

            for (std::size_t effect_index = 0;
                effect_index < editing_effect_stack.effects.size(); ++effect_index)
            {
                ReplayEngine::Rendering::ShaderPropertySchemaRef schema;
                const auto& effect = editing_effect_stack.effects[effect_index];
                const auto* record = asset_database.FindByGuid(effect.custom_shader);
                if (record != nullptr && record->kind == AssetKind::Shader)
                {
                    const std::filesystem::path source =
                        normalize(content_path(record->source_path));
                    for (const ShaderCatalog::Entry& entry : shader_library.Catalog().All())
                    {
                        if (entry.info.domain != ShaderDomain::PostProcess) continue;
                        if (normalize(entry.info.source_path) != source) continue;
                        schema = entry.schema;
                        break;
                    }
                }
                editing_effect_stack.SetCustomShaderSchema(effect_index, std::move(schema));
            }
        };

        const auto reload_effect_preset = [&]()
        {
            EffectPresetAsset preset;
            std::string error;
            if (!preset.LoadFromFile(selected_asset->source_path, error))
            {
                effect_preset_loaded = false;
                effect_preset_status = error.empty()
                    ? u8"Effect Preset を読み込めませんでした。" : error;
                return;
            }

            editing_effect_stack.effect_count = static_cast<int>(preset.effects.size());
            editing_effect_stack.effects = std::move(preset.effects);
            editing_effect_stack.OnPropertyChanged("effect_count");
            refresh_effect_preset_schemas();
            effect_preset_loaded = true;
            effect_preset_status = u8"Effect Preset を読み込みました。";
            std::error_code time_error;
            effect_preset_file_time = std::filesystem::last_write_time(
                selected_asset->source_path, time_error);
        };

        std::error_code effect_time_error;
        const auto current_effect_time = std::filesystem::last_write_time(
            selected_asset->source_path, effect_time_error);
        if (editing_effect_preset_guid != selected_asset->guid ||
            effect_preset_reload_generation != external_file_reload_generation ||
            (!ImGui::IsAnyItemActive() && !effect_time_error &&
                current_effect_time != effect_preset_file_time))
        {
            editing_effect_preset_guid = selected_asset->guid;
            effect_preset_reload_generation = external_file_reload_generation;
            reload_effect_preset();
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader(u8"Effect Preset Asset", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("%s", selected_asset->source_path.generic_u8string().c_str());
            if (ImGui::Button(u8"再読み込み")) reload_effect_preset();
            ReplayEngine::Editor::EditorHelp::Item("button.effect_preset.reload",
                u8"保存済みの Effect Preset を読み直し、編集中の内容をファイルの状態へ戻します。");

            if (effect_preset_loaded && object_editor_context.CanEdit())
            {
                bool changed = false;
                bool shader_schema_dirty = false;
                bool structure_changed = false;
                if (const auto* effect_count = PropertyRegistry::Find(
                    editing_effect_stack.TypeID(), "effect_count"))
                {
                    ImGui::PushID("EffectPresetEffectCount");
                    changed = PropertyDrawer::Draw(*effect_count, editing_effect_stack,
                        &asset_database, &active_object_scene()) || changed;
                    ImGui::PopID();
                }

                const bool effect_limit_reached = editing_effect_stack.effects.size() >= 16;
                if (effect_limit_reached)
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                        ImGui::GetStyle().Alpha * 0.5f);
                if (effect_limit_reached)
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                if (ImGui::Button(u8"エフェクトを追加##EffectPresetAddEffect"))
                {
                    editing_effect_stack.effects.emplace_back();
                    editing_effect_stack.effect_count =
                        static_cast<int>(editing_effect_stack.effects.size());
                    editing_effect_stack.OnPropertyChanged("effect_count");
                    refresh_effect_preset_schemas();
                    changed = true;
                    structure_changed = true;
                }
                ReplayEngine::Editor::EditorHelp::Item("button.effect_preset.add",
                    u8"エフェクトは最大16件です。");
                if (effect_limit_reached)
                    ImGui::PopItemFlag();
                if (effect_limit_reached)
                    ImGui::PopStyleVar();

                ReplayEngine::Editor::ReorderRequest effect_move{};
                std::size_t remove_effect_index =
                    ReplayEngine::Editor::invalid_reorder_index;
                for (std::size_t effect_index = 0;
                    effect_index < editing_effect_stack.effects.size(); ++effect_index)
                {
                    const auto kind = static_cast<ReplayEngine::UI::UIEffectKind>(
                        editing_effect_stack.effects[effect_index].kind);
                    std::string effect_title = "Effect " +
                        std::to_string(effect_index + 1) + " / " +
                        ReplayEngine::UI::EffectKindLabel(kind);
                    if (ReplayEngine::UI::IsTimeDrivenEffect(kind)) effect_title += " [M]";
                    const std::string item_id = "EffectPresetEffect" +
                        std::to_string(effect_index);
                    const ReplayEngine::Editor::ReorderableItemResult item =
                        ReplayEngine::Editor::DrawReorderableItemEx(
                        &editing_effect_stack.effects, item_id.c_str(), effect_index,
                        editing_effect_stack.effects.size(), effect_title.c_str(), false,
                        false, object_editor_context.CanEdit(), 0, &editing_effect_stack,
                        [&remove_effect_index, effect_index](const char* header_title,
                            ImGuiTreeNodeFlags)
                        {
                            if (ImGui::SmallButton(u8"削除"))
                                remove_effect_index = effect_index;
                            ReplayEngine::Editor::EditorHelp::Item(
                                "button.effect_preset.remove",
                                u8"この Effect Preset から対象のエフェクトを削除します。");
                            ImGui::SameLine();
                            return ImGui::Selectable(header_title, false,
                                ImGuiSelectableFlags_SpanAllColumns);
                        },
                        [] {},
                        [](ReplayEngine::Editor::ReorderDropInfo&, const ImVec2&,
                            const ImVec2&) {});
                    if (item.request.Valid() && !effect_move.Valid())
                        effect_move = item.request;
                }
                if (const char* active_label = ReplayEngine::Editor::ActiveReorderLabel(
                    &editing_effect_stack.effects); active_label != nullptr)
                {
                    ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget],
                        u8"移動中: %s", active_label);
                }
                if (remove_effect_index < editing_effect_stack.effects.size())
                {
                    editing_effect_stack.effects.erase(editing_effect_stack.effects.begin() +
                        static_cast<std::ptrdiff_t>(remove_effect_index));
                    editing_effect_stack.effect_count =
                        static_cast<int>(editing_effect_stack.effects.size());
                    editing_effect_stack.OnPropertyChanged("effect_count");
                    refresh_effect_preset_schemas();
                    changed = true;
                    structure_changed = true;
                }
                else if (effect_move.Valid() && effect_move.source <
                    editing_effect_stack.effects.size() && effect_move.destination <
                    editing_effect_stack.effects.size())
                {
                    if (effect_move.source < effect_move.destination)
                    {
                        std::rotate(editing_effect_stack.effects.begin() +
                            static_cast<std::ptrdiff_t>(effect_move.source),
                            editing_effect_stack.effects.begin() +
                            static_cast<std::ptrdiff_t>(effect_move.source + 1),
                            editing_effect_stack.effects.begin() +
                            static_cast<std::ptrdiff_t>(effect_move.destination + 1));
                    }
                    else
                    {
                        std::rotate(editing_effect_stack.effects.begin() +
                            static_cast<std::ptrdiff_t>(effect_move.destination),
                            editing_effect_stack.effects.begin() +
                            static_cast<std::ptrdiff_t>(effect_move.source),
                            editing_effect_stack.effects.begin() +
                            static_cast<std::ptrdiff_t>(effect_move.source + 1));
                    }
                    editing_effect_stack.OnPropertyChanged("effect_count");
                    changed = true;
                }

                // effect_count / type の変更で DynamicProperties が再構築され得るため、
                // count の描画後にポインタを取り直す。PropertyDrawer 自体が変更通知を
                // Component::OnPropertyChanged へ集約するので、通常 Inspector と同じ経路になる。
                if (!structure_changed)
                {
                    if (const auto* dynamic = editing_effect_stack.DynamicProperties())
                    {
                        for (std::size_t index = 0; index < dynamic->size(); ++index)
                        {
                            // type の変更時には vector が再構築されるため、変更が起きたら
                            // そのフレームの残りを描かず次フレームへ送る。古い参照を踏まない。
                            const auto& desc = (*dynamic)[index];
                            // Draw(type) は OnPropertyChanged 内で dynamic_properties_ を
                            // 再構築するため、呼ぶ前に名前をコピーして参照寿命を切る。
                            const std::string property_name = desc.name;
                            ImGui::PushID(property_name.c_str());
                            const bool property_changed = PropertyDrawer::Draw(desc,
                                editing_effect_stack, &asset_database, &active_object_scene());
                            ImGui::PopID();
                            changed = property_changed || changed;
                            if (property_changed && property_name.size() >= 14 &&
                                property_name.compare(property_name.size() - 14, 14, ".custom_shader") == 0)
                            {
                                shader_schema_dirty = true;
                            }
                            if (property_changed && property_name.size() >= 5 &&
                                property_name.compare(property_name.size() - 5, 5, ".type") == 0)
                            {
                                break;
                            }
                        }
                    }
                }

                if (shader_schema_dirty)
                {
                    // Shader Composer schema の切替で Dynamic Property が増減する。
                    // 描画ループ終了後にまとめて再構築し、古い PropertyDesc 参照を踏まない。
                    refresh_effect_preset_schemas();
                }

                if (changed && object_editor_context.CanEdit())
                {
                    EffectPresetAsset preset;
                    preset.effects = editing_effect_stack.effects;
                    std::string error;
                    bool undo_ready = external_file_history.InTransaction();
                    if (!undo_ready)
                        undo_ready = external_file_history.Begin(selected_asset->source_path,
                            "Effect Preset を編集", error);
                    if (!undo_ready)
                    {
                        if (!error.empty()) effect_preset_status = error;
                    }
                    else if (preset.SaveToFile(selected_asset->source_path, error))
                    {
                        EffectPresetAsset::Invalidate(selected_asset->guid);
                        effect_preset_status = u8"保存しました。参照中の UI / Model / Screen に反映されます。";
                        std::error_code time_error;
                        effect_preset_file_time = std::filesystem::last_write_time(
                            selected_asset->source_path, time_error);
                    }
                    else
                    {
                        effect_preset_status = error.empty()
                            ? u8"Effect Preset を保存できませんでした。" : error;
                    }
                }
            }
            else if (effect_preset_loaded)
            {
                ImGui::TextDisabled("Play 中は Effect Preset を編集できません");
            }

            if (external_file_history.InTransaction() && !ImGui::IsAnyItemActive())
            {
                std::string undo_error;
                external_file_history.Commit(undo_error);
                if (!undo_error.empty()) effect_preset_status = undo_error;
            }
            if (!effect_preset_status.empty())
            {
                ImGui::TextWrapped("%s", effect_preset_status.c_str());
            }
        }
    }

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
    REPLAY_PROFILE_SCOPE("Editor/Console");
    ReplayEngine::Editor::PanelTabColorScope panel_tab_color("Core");
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
    ReplayEngine::Editor::EditorHelp::Item("button.console.clear_log",
        u8"エディタログの表示内容をすべて消去します。Asset や Scene は変更しません。");
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
            if (!entry.file.empty())
                ReplayEngine::Editor::EditorHelp::Item(
                    "control.console.entry_file", entry.file.generic_u8string().c_str());
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextColored({ 0.45f, 0.85f, 0.55f, 1.0f }, "[正常] RePlayランタイム動作中");
    ImGui::Text("Editor入力: %s", edit_mode_active ? "編集操作" : "Game View入力キャプチャ");
    ImGui::Text("画面サイズ: %u x %u", client_width, client_height);
    ImGui::TextUnformatted("描画方式: Deferred（固定）");
    ImGui::Text(u8"出力: %s", ReplayEngine::Rendering::RenderGraph::Name(render_graph.OutputIndex()));
        ImGui::TextDisabled(u8"ショートカットは Window > キー割り当て > Editor で確認・変更できます。");
    ImGui::End();
}

void framework::draw_workspace_panel()
{
    ReplayEngine::Editor::PanelTabColorScope panel_tab_color("Editor");
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
        ReplayEngine::Editor::EditorHelp::Item("button.workspace.edit_game_object",
            u8"選択中の GameObject を編集対象にして Inspector を開きます。");
        ImGui::SameLine();
        if (ImGui::Button("配置Workspaceへ")) set_editor_workspace(editor_workspace::placement);
        ReplayEngine::Editor::EditorHelp::Item("button.workspace.placement",
            u8"Asset を Scene View へ配置する Workspace に切り替えます。");
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
        ReplayEngine::Editor::EditorHelp::Item("button.workspace.rendering",
            u8"描画設定を編集対象にして Inspector を開きます。");
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
        ReplayEngine::Editor::EditorHelp::Item("button.workspace.ui_hierarchy",
            u8"UI Component の階層を表示するパネルを開きます。");
        ImGui::SameLine();
        if (ImGui::Button("Canvas プレビューを開く")) show_ui_preview_panel = true;
        ReplayEngine::Editor::EditorHelp::Item("button.workspace.ui_preview",
            u8"Canvas を実際の画面比率で確認するプレビューを開きます。");
        break;
    case editor_workspace::motion:
        ImGui::TextUnformatted("Motion Workspace");
        ImGui::TextDisabled("Motion Asset、キー、プレビューを編集します。");
        if (ImGui::Button("Motion レイヤーを開く")) show_motion_layers_panel = true;
        ReplayEngine::Editor::EditorHelp::Item("button.workspace.motion_layers",
            u8"Motion のレイヤーとトラックを編集するパネルを開きます。");
        ImGui::SameLine();
        if (ImGui::Button("タイムラインを開く")) show_motion_timeline_panel = true;
        ReplayEngine::Editor::EditorHelp::Item("button.workspace.motion_timeline",
            u8"Motion の時間軸とキーを編集するタイムラインを開きます。");
        break;
    default:
        ImGui::TextUnformatted("基本Workspace");
        ImGui::TextDisabled("シーン編集と実行状態の確認を行います。");
        if (ImGui::Button("ワールドを選択")) selected_editor_object = editor_selection::world;
        ReplayEngine::Editor::EditorHelp::Item("button.workspace.world",
            u8"Scene 全体の World 設定を編集対象にします。");
        break;
    }
    ImGui::End();
}
