#include "framework.h"

#include "../../RePlayEngine/Components/Physics/ColliderComponent.h"
#include "../../RePlayEngine/Components/Gameplay/StageGameplayComponents.h"
#include "../../RePlayEngine/Physics/CollisionLayers.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Runtime/Scene/SceneFlowAsset.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace
{
    using ReplayEngine::Runtime::SceneFlowCompareOp;
    using ReplayEngine::Runtime::SceneFlowConditionType;

    template<std::size_t N>
    void CopyText(std::array<char, N>& buffer, const std::string& text)
    {
        const std::size_t count = (std::min)(N - 1, text.size());
        std::memcpy(buffer.data(), text.data(), count);
        buffer[count] = '\0';
    }
}

bool framework::load_scene_flow_editor(const ReplayEngine::Assets::AssetRecord& record)
{
    if (record.kind != ReplayEngine::Assets::AssetKind::SceneFlow) return false;

    // 別 Asset へ移る前に編集内容を落とさない。
    if (scene_flow_editor_loaded && scene_flow_editor_dirty &&
        scene_flow_editor_guid != record.guid)
    {
        if (!save_scene_flow_editor()) return false;
    }

    std::string error;
    ReplayEngine::Runtime::SceneFlowAsset loaded;
    if (!ReplayEngine::Runtime::SceneFlowAsset::Load(loaded, record.source_path, error))
    {
        scene_flow_editor_status = "Scene Flow 読込失敗: " + error;
        return false;
    }
    scene_flow_editor_asset = std::move(loaded);
    scene_flow_editor_path = record.source_path;
    scene_flow_editor_guid = record.guid;
    scene_flow_editor_loaded = true;
    scene_flow_editor_dirty = false;
    scene_flow_editor_status = "Scene Flow を開きました: " + record.display_name;
    show_scene_flow_panel = true;
    return true;
}

bool framework::save_scene_flow_editor()
{
    if (!scene_flow_editor_loaded || scene_flow_editor_path.empty()) return false;
    std::string error;
    if (!ReplayEngine::Runtime::SceneFlowAsset::Save(
        scene_flow_editor_asset, scene_flow_editor_path, error))
    {
        scene_flow_editor_status = "Scene Flow 保存失敗: " + error;
        return false;
    }
    scene_flow_editor_dirty = false;
    scene_flow_editor_status = "保存しました: " + scene_flow_editor_path.filename().u8string();
    if (project_settings.SceneFlowGuid() == scene_flow_editor_guid)
        sync_runtime_scene_flow_asset();
    return true;
}

void framework::sync_runtime_scene_flow_asset()
{
    if (!object_scene_flow) return;
    const auto status = project_settings.ResolveSceneFlow(asset_database);
    if (!status.IsResolved())
    {
        object_scene_flow->ClearFlowAsset();
        return;
    }

    ReplayEngine::Runtime::SceneFlowAsset asset;
    std::string error;
    if (!ReplayEngine::Runtime::SceneFlowAsset::Load(asset, status.path, error))
    {
        object_scene_flow->ClearFlowAsset();
        push_editor_log("Warning", "Active Scene Flow を読み込めません: " + error, status.path);
        return;
    }
    object_scene_flow->SetFlowAsset(asset);
}

void framework::draw_scene_flow_panel()
{
#ifdef USE_IMGUI
    if (!show_scene_flow_panel) return;
    if (!ImGui::Begin("Scene Flow", &show_scene_flow_panel))
    {
        ImGui::End();
        // Close ボタンで閉じたフレームでも未保存の編集を失わない。
        if (!show_scene_flow_panel && scene_flow_editor_dirty) save_scene_flow_editor();
        return;
    }

    if (!scene_flow_editor_loaded)
    {
        ImGui::TextDisabled("Project で .replaysceneflow を作成/開いてください");
        ImGui::End();
        return;
    }

    ImGui::Text("%s%s", scene_flow_editor_asset.name.c_str(),
        scene_flow_editor_dirty ? " *" : "");
    ImGui::SameLine();
    if (ImGui::Button("Save")) save_scene_flow_editor();
    ReplayEngine::Editor::EditorHelp::Item("button.scene_flow.save");
    ImGui::SameLine();
    const bool is_active = project_settings.SceneFlowGuid() == scene_flow_editor_guid;
    if (!is_active)
    {
        if (ImGui::Button("Set Active"))
        {
            project_settings.SetSceneFlowGuid(scene_flow_editor_guid);
            save_project_settings();
            sync_runtime_scene_flow_asset();
        }
        ReplayEngine::Editor::EditorHelp::Item("button.scene_flow.set_active");
    }
    else ImGui::TextColored(ImVec4(0.4f, 0.95f, 0.55f, 1.0f), "ACTIVE");

    ImGui::TextDisabled("C#/C++ は LoadScene を決め打ちせず TriggerSceneFlow(\"Event\") を呼べます");
    ImGui::Separator();

    if (ImGui::Button("+ Transition"))
    {
        scene_flow_editor_asset.AddTransition();
        scene_flow_editor_dirty = true;
    }
    ReplayEngine::Editor::EditorHelp::Item("button.scene_flow.add_transition");

    auto& transitions = scene_flow_editor_asset.transitions;
    for (std::size_t i = 0; i < transitions.size(); )
    {
        auto& transition = transitions[i];
        ImGui::PushID(static_cast<int>(transition.id));
        const std::string header = "Transition #" + std::to_string(transition.id) +
            "  " + transition.event_name;
        bool remove = false;
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Checkbox("Enabled", &transition.enabled)) scene_flow_editor_dirty = true;
            ImGui::SameLine();
            if (ImGui::Button("Delete")) remove = true;
            ReplayEngine::Editor::EditorHelp::Item("button.scene_flow.delete_transition");

            std::array<char, 128> event{};
            CopyText(event, transition.event_name);
            if (ImGui::InputText("Event", event.data(), event.size()))
            {
                transition.event_name = event.data();
                scene_flow_editor_dirty = true;
            }
            if (ImGui::InputInt("Priority", &transition.priority)) scene_flow_editor_dirty = true;

            const auto scene_label = [this](const std::string& guid, const char* empty_label)
            {
                if (guid.empty()) return std::string(empty_label);
                const auto* record = asset_database.FindByGuid(guid);
                return record != nullptr ? (record->display_name.empty()
                    ? record->source_path.filename().u8string() : record->display_name)
                    : std::string("[Missing] ") + guid;
            };

            const std::string from_preview = scene_label(transition.from_scene_guid, "Any Scene");
            if (ImGui::BeginCombo("From", from_preview.c_str()))
            {
                if (ImGui::Selectable("Any Scene", transition.from_scene_guid.empty()))
                {
                    transition.from_scene_guid.clear(); scene_flow_editor_dirty = true;
                }
                for (const auto& record : asset_database.Records())
                {
                    if (record.kind != ReplayEngine::Assets::AssetKind::Scene) continue;
                    const std::string label = record.display_name.empty()
                        ? record.source_path.filename().u8string() : record.display_name;
                    if (ImGui::Selectable(label.c_str(), transition.from_scene_guid == record.guid))
                    {
                        transition.from_scene_guid = record.guid; scene_flow_editor_dirty = true;
                    }
                }
                ImGui::EndCombo();
            }

            const std::string to_preview = scene_label(transition.to_scene_guid, "Select Scene");
            if (ImGui::BeginCombo("To", to_preview.c_str()))
            {
                for (const auto& record : asset_database.Records())
                {
                    if (record.kind != ReplayEngine::Assets::AssetKind::Scene) continue;
                    const std::string label = record.display_name.empty()
                        ? record.source_path.filename().u8string() : record.display_name;
                    if (ImGui::Selectable(label.c_str(), transition.to_scene_guid == record.guid))
                    {
                        transition.to_scene_guid = record.guid; scene_flow_editor_dirty = true;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::TextDisabled("Conditions (AND)");
            for (std::size_t c = 0; c < transition.conditions.size(); )
            {
                auto& condition = transition.conditions[c];
                ImGui::PushID(static_cast<int>(c));
                int type = static_cast<int>(condition.type);
                const char* types[] = { "Bool", "Int", "Float" };
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::Combo("##type", &type, types, 3))
                {
                    condition.type = static_cast<SceneFlowConditionType>(type);
                    scene_flow_editor_dirty = true;
                }
                ImGui::SameLine();
                std::array<char, 96> key{}; CopyText(key, condition.key);
                ImGui::SetNextItemWidth(145.0f);
                if (ImGui::InputText("##key", key.data(), key.size()))
                {
                    condition.key = key.data(); scene_flow_editor_dirty = true;
                }
                ImGui::SameLine();
                int op = static_cast<int>(condition.op);
                const char* ops[] = { "==", "!=", "<", "<=", ">", ">=" };
                ImGui::SetNextItemWidth(65.0f);
                if (ImGui::Combo("##op", &op, ops, 6))
                {
                    condition.op = static_cast<SceneFlowCompareOp>(op);
                    scene_flow_editor_dirty = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                if (condition.type == SceneFlowConditionType::Bool)
                {
                    bool value = condition.value != 0.0;
                    if (ImGui::Checkbox("##value", &value))
                    {
                        condition.value = value ? 1.0 : 0.0;
                        scene_flow_editor_dirty = true;
                    }
                }
                else if (condition.type == SceneFlowConditionType::Int)
                {
                    int value = static_cast<int>(condition.value);
                    if (ImGui::InputInt("##value", &value))
                    {
                        condition.value = static_cast<double>(value);
                        scene_flow_editor_dirty = true;
                    }
                }
                else if (ImGui::InputDouble("##value", &condition.value, 0.1, 1.0, "%.3f"))
                {
                    scene_flow_editor_dirty = true;
                }
                ImGui::SameLine();
                bool remove_condition = ImGui::SmallButton("X");
                ReplayEngine::Editor::EditorHelp::Item("button.scene_flow.delete_condition");
                ImGui::PopID();
                if (remove_condition)
                {
                    transition.conditions.erase(transition.conditions.begin() + c);
                    scene_flow_editor_dirty = true;
                }
                else ++c;
            }
            if (ImGui::SmallButton("+ Condition"))
            {
                ReplayEngine::Runtime::SceneFlowCondition condition;
                condition.key = "Flag";
                transition.conditions.push_back(std::move(condition));
                scene_flow_editor_dirty = true;
            }
            ReplayEngine::Editor::EditorHelp::Item("button.scene_flow.add_condition");
        }
        ImGui::PopID();
        if (remove)
        {
            const std::uint64_t id = transition.id;
            scene_flow_editor_asset.RemoveTransition(id);
            scene_flow_editor_dirty = true;
        }
        else ++i;
    }

    ImGui::Separator();
    ImGui::TextDisabled("%s", scene_flow_editor_status.c_str());
    ImGui::End();

    // X で閉じた場合も未保存の編集を失わない。
    if (!show_scene_flow_panel && scene_flow_editor_dirty) save_scene_flow_editor();
#endif
}
