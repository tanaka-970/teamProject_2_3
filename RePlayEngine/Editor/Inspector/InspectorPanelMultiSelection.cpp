// InspectorPanel のうち「複数選択と共通 Component の一括編集」だけを持つ。
//
// 単体選択のヘッダー・Prefab 表示は InspectorPanel.cpp、
// Component 単体の表示は InspectorPanelComponents.cpp に置く。
#include "InspectorPanel.h"

#include "PropertyDrawer.h"
#include "../Validation/SceneValidator.h"
#include "PlayerCompositionValidator.h"
#include "../../Assets/AssetDatabase.h"
#include "../../Object/Component/MissingComponent.h"
#include "../../Object/Registry/ComponentDependencyRules.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../Core/EditorContext.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/PrefabSerializer.h"
#include "../../Scripting/Core/ScriptComponent.h"
#include "../../Scripting/Core/ScriptTypes.h"
#include "../../Components/Landscape/LandscapeComponent.h"
#include "../../Components/Landscape/LandscapeRendererComponent.h"
#include "../../Components/Landscape/LandscapeColliderComponent.h"
#include "../../Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../Components/Rendering/LightComponents.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace ReplayEngine::Editor
{
    using Core::ComponentRegistry;
    using Core::ComponentTypeInfo;
    using Core::GameObject;

    namespace
    {
        // 値の比較は Reflection::ValuesEqual へ集約した。
        // PropertyType を足したときに直す場所を 1 か所に保つため。
        bool PropertyValuesEqual(const Reflection::PropertyValue& a,
            const Reflection::PropertyValue& b)
        {
            return Reflection::ValuesEqual(a, b);
        }

        std::string BulkRemovalBlockReason(const std::vector<GameObject*>& objects,
            const std::vector<Core::Component*>& components)
        {
            std::string reason;
            for (std::size_t index = 0; index < objects.size() &&
                index < components.size(); ++index)
            {
                GameObject* object = objects[index];
                Core::Component* component = components[index];
                if (object == nullptr || component == nullptr || component->PendingDestroy())
                {
                    if (!reason.empty()) reason += "; ";
                    reason += object != nullptr ? object->Name() : "(null)";
                    reason += ": 削除対象が無効";
                    continue;
                }
                if (!ComponentRegistry::IsRemovable(component->TypeID()))
                {
                    if (!reason.empty()) reason += "; ";
                    reason += object->Name() + ": 削除不可";
                    continue;
                }

                const std::vector<Core::Component*> dependents =
                    Core::ComponentDependencyRules::FindDirectDependents(
                        *object, *component);
                for (Core::Component* dependent : dependents)
                {
                    if (!reason.empty()) reason += "; ";
                    reason += object->Name() + ": " +
                        ComponentRegistry::DisplayNameOf(dependent->TypeID()) +
                        " が必須として使用中";
                }
            }
            return reason;
        }
    }

    void InspectorPanel::DrawMultiSelection(EditorContext& context,
        const std::vector<GameObject*>& objects)
    {
        if (objects.empty()) return;

        ImGui::Text("%zu GameObjects", objects.size());
        ImGui::TextDisabled("共通する Component と Property を一括編集します");

        bool enabled = objects.front()->Enabled();
        const bool enabled_mixed = std::any_of(objects.begin() + 1, objects.end(), [enabled](const GameObject* object)
        {
            return object != nullptr && object->Enabled() != enabled;
        });
        if (enabled_mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        const bool enabled_changed = ImGui::Checkbox("Enabled", &enabled);
        if (enabled_mixed) ImGui::PopItemFlag();
        if (enabled_changed && context.CanEdit())
        {
            context.BeginEdit("複数 GameObject の有効状態を変更");
            for (GameObject* object : objects) if (object != nullptr) object->SetEnabled(enabled);
            context.CommitEdit();
        }

        if (ImGui::CollapsingHeader("Advanced / IDs"))
        {
            for (const GameObject* object : objects)
            {
                if (object == nullptr) continue;
                ImGui::TextDisabled("%s: %s", object->Name().c_str(), object->ID().ToString().c_str());
            }
        }
        ImGui::Separator();

        std::vector<Core::ComponentTypeID> common_types;
        for (std::size_t index = 0; index < objects.front()->ComponentCount(); ++index)
        {
            Core::Component* component = objects.front()->ComponentAt(index);
            if (component == nullptr || component->PendingDestroy()) continue;
            const Core::ComponentTypeID type_id = component->TypeID();
            if (std::find(common_types.begin(), common_types.end(), type_id) != common_types.end()) continue;
            const bool common = std::all_of(objects.begin() + 1, objects.end(), [type_id](const GameObject* object)
            {
                return object != nullptr && object->FindComponent(type_id) != nullptr;
            });
            if (common) common_types.push_back(type_id);
        }

        for (const Core::ComponentTypeID type_id : common_types)
        {
            ImGui::PushID(static_cast<int>(type_id));
            DrawCommonComponent(context, objects, type_id);
            ImGui::PopID();
        }

        ImGui::Separator();
        if (!context.CanEdit())
        {
            ImGui::TextDisabled("実行中はコンポーネントを変更できません");
        }
        else if (ImGui::Button("コンポーネントを一括追加", ImVec2(-1.0f, 0.0f)))
        {
            ImGui::OpenPopup("BulkAddComponent");
        }

        if (ImGui::BeginPopup("BulkAddComponent"))
        {
            ImGui::TextDisabled("選択中の全 GameObject に追加");
            ImGui::Separator();
            for (const Core::ComponentTypeInfo& info : ComponentRegistry::All())
            {
                if (!info.editor_visible || info.built_in) continue;
                const bool all_present = !info.allow_multiple &&
                    std::all_of(objects.begin(), objects.end(), [&info](const GameObject* object)
                    {
                        return object != nullptr && object->FindComponent(info.type_id) != nullptr;
                    });
                if (all_present)
                {
                    ImGui::TextDisabled("%s (追加済み)", info.DisplayName().c_str());
                    continue;
                }
                if (ImGui::Selectable(info.DisplayName().c_str()))
                {
                    context.BeginEdit(info.DisplayName() + " を一括追加");
                    std::vector<std::pair<GameObject*, Core::ComponentDependencyPlan>> plans;
                    std::string planning_error;
                    for (GameObject* object : objects)
                    {
                        if (object == nullptr) continue;
                        if (!info.allow_multiple &&
                            object->FindComponent(info.type_id) != nullptr) continue;
                        Core::ComponentDependencyPlan plan =
                            Core::ComponentDependencyRules::PlanRequiredAdd(*object,
                                info.type_id,
                                Core::ComponentAvailabilityPolicy::Editor);
                        if (!plan.Valid())
                        {
                            planning_error = object->Name() + ": " +
                                Core::ComponentDependencyRules::DescribeIssue(plan.issue);
                            break;
                        }
                        plans.emplace_back(object, std::move(plan));
                    }

                    int added = 0;
                    std::size_t dependencies_added = 0;
                    bool apply_failed = false;
                    for (const auto& entry : plans)
                    {
                        if (!planning_error.empty()) break;
                        const Core::ComponentDependencyApplyResult result =
                            Core::ComponentDependencyRules::ApplyRequiredAddPlan(
                                *entry.first, entry.second);
                        if (!result.Succeeded())
                        {
                            planning_error = entry.first->Name() + ": " +
                                Core::ComponentDependencyRules::DescribeIssue(result.issue);
                            apply_failed = true;
                            break;
                        }
                        ++added;
                        dependencies_added += result.automatically_added;
                    }
                    if (added > 0 && planning_error.empty() && !apply_failed)
                    {
                        context.CommitEdit();
                        std::string status = info.DisplayName() + " を " +
                            std::to_string(added) + " 個へ追加しました";
                        if (dependencies_added > 0)
                        {
                            status += "（必須 Component " +
                                std::to_string(dependencies_added) + " 個を自動追加）";
                        }
                        context.SetStatus(status);
                    }
                    else
                    {
                        context.CancelEdit();
                        if (!planning_error.empty())
                            context.SetStatus("一括追加を中止しました: " + planning_error);
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

        if (!context.Status().empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", context.Status().c_str());
        }
    }

    void InspectorPanel::DrawCommonComponent(EditorContext& context,
        const std::vector<GameObject*>& objects, Core::ComponentTypeID type_id)
    {
        std::vector<Core::Component*> components;
        components.reserve(objects.size());
        for (GameObject* object : objects)
        {
            Core::Component* component = object != nullptr ? object->FindComponent(type_id) : nullptr;
            if (component == nullptr || component->PendingDestroy()) return;
            components.push_back(component);
        }
        if (components.empty()) return;

        const ComponentTypeInfo* info = ComponentRegistry::Find(type_id);
        const std::string title = info != nullptr ? info->DisplayName() : components.front()->TypeName();
        const bool editable = context.CanEdit();

        bool component_enabled = components.front()->Enabled();
        const bool enabled_mixed = std::any_of(components.begin() + 1, components.end(), [component_enabled](const Core::Component* component)
        {
            return component != nullptr && component->Enabled() != component_enabled;
        });
        if (enabled_mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        const bool enabled_changed = ImGui::Checkbox("##ComponentEnabled", &component_enabled);
        if (enabled_mixed) ImGui::PopItemFlag();
        if (enabled_changed && editable)
        {
            context.BeginEdit(title + " の有効状態を一括変更");
            for (Core::Component* component : components) component->SetEnabled(component_enabled);
            context.CommitEdit();
        }
        ImGui::SameLine();

        const bool opened = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (!opened) return;

        ImGui::Indent();
        if (!editable)
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        }

        for (const Reflection::PropertyDesc& desc : Reflection::PropertyRegistry::PropertiesOf(type_id))
        {
            if (!desc.editor_visible) continue;
            const Reflection::PropertyValue primary_value = desc.Capture(*components.front());
            const bool mixed = std::any_of(components.begin() + 1, components.end(), [&desc, &primary_value](const Core::Component* component)
            {
                return component == nullptr || !PropertyValuesEqual(primary_value, desc.Capture(*component));
            });

            const bool had_transaction = context.History().InTransaction();
            if (editable && !had_transaction) context.BeginEdit(title + " の設定を一括変更");

            ImGui::PushID(desc.name.c_str());
            const bool changed = PropertyDrawer::Draw(desc, *components.front(),
                context.GetAssetDatabase(), context.GetScene(), mixed);
            ImGui::PopID();

            if (changed)
            {
                const Reflection::PropertyValue updated = desc.Capture(*components.front());
                for (std::size_t index = 1; index < components.size(); ++index)
                {
                    desc.Apply(*components[index], updated);
                    components[index]->OnPropertyChanged(desc.name.c_str());
                }
                context.MarkDirty();
                prefab_cache_valid_ = false;
            }

            if (editable && context.History().InTransaction() && !ImGui::IsAnyItemActive())
            {
                if (changed || had_transaction) context.CommitEdit();
                else context.CancelEdit();
            }
        }

        if (!editable)
        {
            ImGui::PopStyleVar();
            ImGui::PopItemFlag();
        }

        const bool removable = ComponentRegistry::IsRemovable(type_id);
        const std::string removal_block_reason =
            BulkRemovalBlockReason(objects, components);
        if (!removable)
        {
            ImGui::TextDisabled("このコンポーネントは一括削除できません");
        }
        else if (!editable)
        {
            ImGui::TextDisabled("実行中は一括削除できません");
        }
        else if (!removal_block_reason.empty())
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                ImGui::GetStyle().Alpha * 0.5f);
            ImGui::Button("選択対象から一括削除");
            ImGui::PopStyleVar();
            ImGui::PopItemFlag();
            ImGui::TextWrapped("一括削除不可: %s", removal_block_reason.c_str());
        }
        else if (ImGui::Button("選択対象から一括削除"))
        {
            ImGui::OpenPopup("ConfirmBulkRemove");
        }
        if (ImGui::BeginPopupModal("ConfirmBulkRemove", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("%zu 個の GameObject から %s を削除しますか？", objects.size(), title.c_str());
            if (ImGui::Button("削除"))
            {
                context.BeginEdit(title + " を一括削除");
                const std::string rechecked =
                    BulkRemovalBlockReason(objects, components);
                if (!rechecked.empty())
                {
                    context.CancelEdit();
                    context.SetStatus("一括削除を中止しました: " + rechecked);
                }
                else
                {
                    bool removed_all = true;
                    for (std::size_t index = 0; index < objects.size(); ++index)
                    {
                        if (!objects[index]->RemoveComponent(components[index]))
                            removed_all = false;
                    }
                    if (removed_all)
                    {
                        context.CommitEdit();
                        context.SetStatus(title + " を一括削除しました");
                    }
                    else
                    {
                        context.CancelEdit();
                        context.SetStatus(title + " の一括削除に失敗しました");
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Unindent();
    }

    void InspectorPanel::DrawPlayerComposition(EditorContext& context, GameObject& object)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr) return;

        const auto result = PlayerCompositionValidator::Validate(*scene, object);

        // 操作対象でもなく、操作系 Component も 1 つも無い GameObject には出さない。
        // 通常の床や小物の Inspector が診断で埋まらないようにする。
        const bool relevant = result.is_controlled ||
            result.missing_required < static_cast<int>(
                std::count_if(result.requirements.begin(), result.requirements.end(),
                    [](const PlayerCompositionValidator::Requirement& r) { return r.required; }));
        if (!relevant) return;

        if (!ImGui::CollapsingHeader("操作構成の診断")) return;

        ImGui::Indent();

        if (result.is_controlled)
        {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "操作対象: この GameObject");
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                "操作対象ではありません（controlledObjectId が別を指しています）");
        }

        for (const auto& requirement : result.requirements)
        {
            if (requirement.present)
            {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "OK");
                ImGui::SameLine();
                ImGui::TextUnformatted(requirement.display_name.c_str());
            }
            else
            {
                const ImVec4 color = requirement.required
                    ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f) : ImVec4(0.75f, 0.75f, 0.4f, 1.0f);
                ImGui::TextColored(color, requirement.required ? "必須" : "任意");
                ImGui::SameLine();
                ImGui::TextUnformatted(requirement.display_name.c_str());
                ImGui::TextDisabled("        %s", requirement.missing_effect.c_str());
            }
        }

        const bool editable = context.CanEdit();
        ImGui::Spacing();

        // 操作対象の変更は GameObject 共通ヘッダーへ一本化した。
        // ここは Player テンプレート専用の入口にせず、構成診断だけを担当する。

        if (!result.complete && editable)
        {
            // ユーザーが明示的に押したときだけ追加する。
            // 削除した Component を勝手に復活させない。
            if (ImGui::Button("不足 Component を追加"))
            {
                context.BeginEdit("不足 Component を追加");
                int added = 0;
                std::size_t dependencies_added = 0;
                std::string failure;
                for (const auto& requirement : result.requirements)
                {
                    if (!requirement.required || requirement.present) continue;
                    const auto* info = Core::ComponentRegistry::Find(requirement.type_name);
                    if (info == nullptr) continue;
                    if (object.FindComponent(info->type_id) != nullptr) continue;
                    const Core::ComponentDependencyPlan plan =
                        Core::ComponentDependencyRules::PlanRequiredAdd(object,
                            info->type_id,
                            Core::ComponentAvailabilityPolicy::Editor);
                    const Core::ComponentDependencyApplyResult add_result =
                        Core::ComponentDependencyRules::ApplyRequiredAddPlan(object, plan);
                    if (!add_result.Succeeded())
                    {
                        const Core::ComponentDependencyIssue& issue = add_result.issue.Any()
                            ? add_result.issue : plan.issue;
                        failure = requirement.display_name + ": " +
                            Core::ComponentDependencyRules::DescribeIssue(issue);
                        break;
                    }
                    ++added;
                    dependencies_added += add_result.automatically_added;
                }
                if (added > 0 && failure.empty())
                {
                    context.CommitEdit();
                    std::string status = std::to_string(added) +
                        " 個の Component を追加しました";
                    if (dependencies_added > 0)
                    {
                        status += "（必須 Component " +
                            std::to_string(dependencies_added) + " 個を自動追加）";
                    }
                    context.SetStatus(status);
                }
                else
                {
                    context.CancelEdit();
                    if (!failure.empty())
                        context.SetStatus("不足 Component の追加を中止しました: " + failure);
                }
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("構成を再検証"))
        {
            context.SetStatus(result.complete
                ? "構成は揃っています"
                : "必須 Component が " + std::to_string(result.missing_required) + " 個不足しています");
        }

        ImGui::Unindent();
        ImGui::Spacing();
    }


}
