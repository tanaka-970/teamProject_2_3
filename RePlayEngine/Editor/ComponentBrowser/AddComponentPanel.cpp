#include "AddComponentPanel.h"

#include "../Core/EditorContext.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentDependencyRules.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Runtime/Behaviour/BehaviourRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scripting/Core/ScriptComponent.h"
#include "../../Scripting/Core/ScriptServices.h"
#include "../../Scripting/Core/ScriptTypeCatalog.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    using Core::ComponentRegistry;
    using Core::ComponentTypeInfo;
    namespace Scripting = ReplayEngine::Scripting;

    namespace
    {
        constexpr const char* popup_id = "RePlayAddComponentPopup";
        constexpr const char* template_module_prefix = "RePlayEngine.Template.";

        std::string ToLower(const std::string& text)
        {
            std::string lowered = text;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return lowered;
        }

        // 検索は型名と表示名の両方に対して部分一致で行う。
        bool Matches(const ComponentTypeInfo& info, const std::string& lowered_query)
        {
            if (lowered_query.empty()) return true;
            return ToLower(info.type_name).find(lowered_query) != std::string::npos ||
                ToLower(info.DisplayName()).find(lowered_query) != std::string::npos;
        }

        bool Matches(const Scripting::ScriptTypeDescriptor& script,
            const std::string& lowered_query)
        {
            if (lowered_query.empty()) return true;
            return ToLower(script.script_name).find(lowered_query) != std::string::npos ||
                ToLower(script.DisplayName()).find(lowered_query) != std::string::npos ||
                ToLower(script.class_name).find(lowered_query) != std::string::npos ||
                ToLower(script.asset_guid).find(lowered_query) != std::string::npos;
        }

        bool IsGameTemplateComponent(const ComponentTypeInfo& info)
        {
            return info.module_id.rfind(template_module_prefix, 0) == 0;
        }

        // 件数確認と実描画で必ず同じ判定を使う。
        // 検索中は Template 非表示設定より検索を優先し、名前を知っている人の導線を残す。
        bool IsComponentVisible(const ComponentTypeInfo& info, const std::string& category,
            const std::string& lowered_query, bool show_game_template_components)
        {
            if (!info.editor_visible) return false;
            if (info.category != category) return false;
            if (!Matches(info, lowered_query)) return false;
            if (!lowered_query.empty()) return true;
            return show_game_template_components || !IsGameTemplateComponent(info);
        }

        std::string RelationshipNames(const std::vector<Core::ComponentTypeID>& ids)
        {
            std::string result;
            for (Core::ComponentTypeID id : ids)
            {
                if (!result.empty()) result += ", ";
                result += ComponentRegistry::DisplayNameOf(id);
            }
            return result;
        }

        std::vector<std::string> ScriptCategories(
            const std::vector<Scripting::ScriptTypeDescriptor>& scripts)
        {
            std::vector<std::string> categories;
            for (const Scripting::ScriptTypeDescriptor& script : scripts)
            {
                const std::string category = script.ResolvedCategory();
                if (std::find(categories.begin(), categories.end(), category) ==
                    categories.end())
                {
                    categories.push_back(category);
                }
            }
            return categories;
        }
    }

    void AddComponentPanel::Close() noexcept
    {
        open_requested_ = false;
        search_text_[0] = '\0';
    }

    bool AddComponentPanel::Draw(EditorContext& context, Core::GameObject& target,
        bool& show_game_template_components,
        bool& show_game_template_components_changed)
    {
        show_game_template_components_changed = false;

        if (open_requested_)
        {
            open_requested_ = false;
            focus_search_ = true;
            search_text_[0] = '\0';
            ImGui::OpenPopup(popup_id);
        }

        if (!ImGui::BeginPopup(popup_id)) return false;

        ImGui::TextDisabled("コンポーネントを追加");
        ImGui::Separator();

        if (focus_search_)
        {
            ImGui::SetKeyboardFocusHere();
            focus_search_ = false;
        }
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##AddComponentSearch", "Search Components...",
            search_text_, search_buffer_size);

        if (ImGui::Checkbox("テンプレート部品も表示する", &show_game_template_components))
        {
            show_game_template_components_changed = true;
        }

        const std::string query = ToLower(std::string(search_text_));

        bool added = false;
        bool any_visible = false;
        for (const std::string& category : ComponentRegistry::Categories())
        {
            // このカテゴリに表示すべき型があるかを先に数える。
            // 空のカテゴリ見出しだけが残らないようにするため。
            int visible_in_category = 0;
            for (const ComponentTypeInfo& info : ComponentRegistry::All())
            {
                if (!IsComponentVisible(info, category, query,
                    show_game_template_components)) continue;
                ++visible_in_category;
            }
            if (visible_in_category == 0) continue;
            any_visible = true;

            ImGui::TextDisabled("%s", category.c_str());
            ImGui::Separator();

            for (const ComponentTypeInfo& info : ComponentRegistry::All())
            {
                if (!IsComponentVisible(info, category, query,
                    show_game_template_components)) continue;

                // 重複禁止の型が既に付いている場合は追加させない。
                // 押せてしまってから失敗するのではなく、押せないことを見た目で示す。
                const bool already_present =
                    !info.allow_multiple && target.FindComponent(info.type_id) != nullptr;

                ImGui::PushID(info.type_name.c_str());
                if (already_present)
                {
                    ImGui::TextDisabled("  %s  (追加済み)", info.DisplayName().c_str());
                }
                else if (ImGui::Selectable(("  " + info.DisplayName()).c_str()))
                {
                    context.BeginEdit(info.DisplayName() + " を追加");
                    const Core::ComponentDependencyPlan plan =
                        Core::ComponentDependencyRules::PlanRequiredAdd(target,
                            info.type_id, Core::ComponentAvailabilityPolicy::Editor);
                    const Core::ComponentDependencyApplyResult result =
                        Core::ComponentDependencyRules::ApplyRequiredAddPlan(target, plan);
                    if (result.Succeeded())
                    {
                        context.CommitEdit();
                        std::string status = info.DisplayName() + " を追加しました";
                        if (result.automatically_added > 0)
                            status += "（必須 Component " +
                                std::to_string(result.automatically_added) + " 個を自動追加）";
                        context.SetStatus(status);
                        added = true;
                    }
                    else
                    {
                        context.CancelEdit();
                        const Core::ComponentDependencyIssue& issue = result.issue.Any()
                            ? result.issue : plan.issue;
                        context.SetStatus(info.DisplayName() + " を追加できませんでした: " +
                            Core::ComponentDependencyRules::DescribeIssue(issue));
                    }
                    ImGui::CloseCurrentPopup();
                }
                // 型ごとの分岐は書かない。
                // 表示名・カテゴリ・説明・モジュール・型 GUID はすべて
                // ComponentTypeInfo から、供給元だけ BehaviourRegistry から引く。
                // Behaviour が増えてもここは 1 行も変わらない。
                const Runtime::BehaviourRegistry::Entry* behaviour =
                    Runtime::BehaviourRegistry::Find(info.type_id);

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    if (!info.tooltip.empty()) ImGui::TextUnformatted(info.tooltip.c_str());
                    if (!info.required_components.empty())
                    {
                        ImGui::Separator();
                        const std::string names = RelationshipNames(info.required_components);
                        ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.30f, 1.0f), "必須: %s", names.c_str());
                        ImGui::TextDisabled("不足分は追加時に自動で補います");
                    }
                    if (!info.recommended_components.empty())
                    {
                        const std::string names = RelationshipNames(info.recommended_components);
                        ImGui::TextDisabled("推奨: %s", names.c_str());
                    }
                    if (behaviour != nullptr)
                    {
                        ImGui::Separator();
                        ImGui::TextDisabled("Behaviour (%s)",
                            behaviour->provider != nullptr
                                ? behaviour->provider->ProviderName() : "Unknown");
                        if (!Runtime::BehaviourRegistry::CanInstantiate(behaviour->type_guid))
                        {
                            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                                "現在この型は生成できません");
                        }
                    }
                    if (info.type_guid.IsValid())
                    {
                        ImGui::TextDisabled("Type GUID: %s",
                            info.type_guid.ToString().c_str());
                    }
                    if (!info.module_id.empty())
                    {
                        ImGui::TextDisabled("Module: %s", info.module_id.c_str());
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();

                if (added) break;
            }
            if (added) break;
            ImGui::Spacing();
        }

        const Scene::Scene* scene = context.GetScene();
        const Scripting::IScriptServices* scripts =
            scene != nullptr ? scene->Services().Scripts() : nullptr;
        const std::vector<Scripting::ScriptTypeDescriptor>* script_types =
            scripts != nullptr ? &scripts->Catalog().All() : nullptr;

        if (!added && script_types != nullptr && !script_types->empty())
        {
            for (const std::string& category : ScriptCategories(*script_types))
            {
                int visible_in_category = 0;
                for (const Scripting::ScriptTypeDescriptor& script : *script_types)
                {
                    if (script.ResolvedCategory() != category) continue;
                    if (!Matches(script, query)) continue;
                    ++visible_in_category;
                }
                if (visible_in_category == 0) continue;
                any_visible = true;

                ImGui::TextDisabled("%s", category.c_str());
                ImGui::Separator();

                for (const Scripting::ScriptTypeDescriptor& script : *script_types)
                {
                    if (script.ResolvedCategory() != category) continue;
                    if (!Matches(script, query)) continue;

                    const std::string type_id = script.type_id.ToString();
                    ImGui::PushID(type_id.c_str());
                    if (ImGui::Selectable(("  " + script.DisplayName()).c_str()))
                    {
                        context.BeginEdit(script.DisplayName() + " を追加");
                        Core::Component* component =
                            target.AddComponent(Scripting::ScriptComponent::StaticTypeID());
                        Scripting::ScriptComponent* script_component = component != nullptr
                            ? Scripting::ScriptComponent::From(*component)
                            : nullptr;
                        if (script_component != nullptr)
                        {
                            script_component->AssignScriptType(script);
                            context.CommitEdit();
                            context.SetStatus(script.DisplayName() + " を追加しました");
                            added = true;
                        }
                        else
                        {
                            context.CancelEdit();
                            context.SetStatus(script.DisplayName() + " を追加できませんでした");
                        }
                        ImGui::CloseCurrentPopup();
                    }

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::TextDisabled("Script (%s)", ToString(script.language));
                        if (!script.script_name.empty())
                        {
                            ImGui::TextUnformatted(script.script_name.c_str());
                        }
                        if (!script.class_name.empty())
                        {
                            ImGui::TextDisabled("Class: %s", script.class_name.c_str());
                        }
                        if (!script.asset_guid.empty())
                        {
                            ImGui::TextDisabled("Asset: %s", script.asset_guid.c_str());
                        }
                        ImGui::TextDisabled("Script Type ID: %s", type_id.c_str());
                        if (script.status == Scripting::ScriptStatus::Error &&
                            !script.last_error.empty())
                        {
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                                "%s", script.last_error.c_str());
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();

                    if (added) break;
                }
                if (added) break;
                ImGui::Spacing();
            }
        }

        if (query.empty() && !any_visible)
        {
            ImGui::TextDisabled("登録された Component がありません");
        }

        ImGui::EndPopup();
        return added;
    }
}
