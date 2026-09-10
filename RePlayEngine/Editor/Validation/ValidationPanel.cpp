#include "ValidationPanel.h"

#include "../Core/EditorContext.h"
#include "../Style/EditorStyle.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Services/SceneCollisionWorld.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Reflection/Property/PropertyValue.h"

#include "imgui/imgui.h"

namespace ReplayEngine::Editor
{
    namespace
    {
        // C++ に登録されている Component と Property をそのまま見せる。
        // C# の型付き入口 (Components*.cs) はここから起こしているので、
        // 「C# から何が触れるか」もこの一覧で分かる。
        void DrawComponentApiTab()
        {
            static ImGuiTextFilter filter;
            filter.Draw("検索", 240.0f);

            const auto& types = Core::ComponentRegistry::All();
            std::size_t property_count = 0;
            std::size_t csharp_count = 0;
            for (const Core::ComponentTypeInfo& info : types)
                for (const Reflection::PropertyDesc& desc :
                    Reflection::PropertyRegistry::PropertiesOf(info.type_id))
                {
                    ++property_count;
                    if (desc.type != Reflection::PropertyType::Array) ++csharp_count;
                }

            ImGui::Text("Component %zu 種 / Property %zu 個。C# から触れるのは %zu 個",
                types.size(), property_count, csharp_count);
            ImGui::TextDisabled("Array は ComponentAccessor に窓口が無いので C# へ出していない");
            ImGui::TextDisabled("作り直し: 3dgp.exe --dump-component-properties → "
                "python Tools/generate_component_bindings.py");
            ImGui::Separator();

            for (const Core::ComponentTypeInfo& info : types)
            {
                const auto& properties =
                    Reflection::PropertyRegistry::PropertiesOf(info.type_id);
                if (!filter.PassFilter(info.type_name.c_str())) continue;

                const std::string label = info.type_name + "  [" + info.category +
                    "]  " + std::to_string(properties.size()) + " 個";
                if (!ImGui::TreeNode(label.c_str())) continue;

                if (properties.empty())
                {
                    ImGui::TextDisabled("Property の登録なし");
                }
                else
                {
                    for (const Reflection::PropertyDesc& desc : properties)
                    {
                        // Array だけは ComponentAccessor に窓口が無く、C# へ出していない。
                        const bool usable =
                            desc.type != Reflection::PropertyType::Array;
                        const char* write = desc.read_only ? "  読取のみ" : "";
                        if (usable)
                        {
                            ImGui::Text("%-30s %-16s%s", desc.name.c_str(),
                                Reflection::ToString(desc.type), write);
                        }
                        else
                        {
                            ImGui::TextDisabled("%-30s %-16s%s  C# 未対応",
                                desc.name.c_str(),
                                Reflection::ToString(desc.type), write);
                        }
                    }
                }
                ImGui::TreePop();
            }
        }
        const char* SeverityLabel(ValidationSeverity severity) noexcept
        {
            switch (severity)
            {
            case ValidationSeverity::Info: return "INFO";
            case ValidationSeverity::Warning: return "WARN";
            case ValidationSeverity::Error: return "ERROR";
            default: return "?";
            }
        }

        ImVec4 SeverityColor(ValidationSeverity severity) noexcept
        {
            const EditorStyleTokens& token = EditorStyle::Tokens();
            switch (severity)
            {
            case ValidationSeverity::Info: return token.accent;
            case ValidationSeverity::Warning: return token.warning;
            case ValidationSeverity::Error: return token.error;
            default: return token.text;
            }
        }
    }

    void ValidationPanel::Revalidate(EditorContext& context,
        const Assets::AssetDatabase* assets)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr)
        {
            issues_.clear();
            validated_generation_ = 0;
            return;
        }
        issues_ = SceneValidator::Validate(*scene, assets);
        validated_generation_ = scene->StructureGeneration();
        force_validation_ = false;
    }

    void ValidationPanel::Draw(EditorContext& context, const Assets::AssetDatabase* assets,
        const Scene::SceneCollisionWorld* collision_world, std::size_t render_item_count)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene != nullptr && (force_validation_ ||
            validated_generation_ != scene->StructureGeneration()))
            Revalidate(context, assets);

        ImGui::Begin("Validation & Diagnostics");
        if (ImGui::BeginTabBar("ValidationTabs"))
        {
            if (ImGui::BeginTabItem("Validation"))
            {
                if (ImGui::Button("再検証")) Revalidate(context, assets);
                ImGui::SameLine();
                int errors = 0;
                int warnings = 0;
                for (const ValidationIssue& issue : issues_)
                {
                    if (issue.severity == ValidationSeverity::Error) ++errors;
                    if (issue.severity == ValidationSeverity::Warning) ++warnings;
                }
                ImGui::Text("Error %d / Warning %d / Total %zu", errors, warnings, issues_.size());
                ImGui::Separator();

                if (issues_.empty())
                {
                    ImGui::TextColored(EditorStyle::Tokens().success,
                        "現在検出できる問題はありません。");
                }
                else
                {
                    const bool issue_list_visible = ImGui::BeginChild(
                        "ValidationIssueList", ImVec2(0.0f, 0.0f), false);
                    if (issue_list_visible)
                    {
                        for (std::size_t index = 0; index < issues_.size(); ++index)
                        {
                            const ValidationIssue& issue = issues_[index];
                            ImGui::PushID(static_cast<int>(index));
                            ImGui::TextColored(SeverityColor(issue.severity), "%s",
                                SeverityLabel(issue.severity));
                            ImGui::SameLine();
                            if (ImGui::Selectable(issue.message.c_str(), false,
                                ImGuiSelectableFlags_AllowDoubleClick) && issue.object.Valid())
                            {
                                context.Selection().Select(issue.object, false);
                                context.SetStatus(issue.message);
                            }
                            if (!issue.suggestion.empty())
                            {
                                ImGui::Indent();
                                ImGui::TextDisabled("修正案: %s", issue.suggestion.c_str());
                                ImGui::Unindent();
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Diagnostics"))
            {
                if (scene == nullptr) ImGui::TextDisabled("Sceneがありません");
                else
                {
                    ImGui::Text("Scene: %s", scene->Name().c_str());
                    ImGui::Text("GameObjects: %zu", scene->GameObjectCount());
                    ImGui::Text("Mode: %s", context.PlayMode() ? "Runtime" : "Edit");
                    ImGui::Text("Dirty: %s", context.Dirty() ? "Yes" : "No");
                    ImGui::Text("Render Items: %zu", render_item_count);
                    ImGui::TextUnformatted("Collision Backend: Scene Colliders Only");
                    ImGui::Text("Gameplay events: %zu",
                        scene->Services().GameplayEvents().Events().size());
                }
                if (collision_world != nullptr)
                {
                    ImGui::Separator();
                    ImGui::Text("Colliders: %zu (blocking %zu / trigger %zu / mesh %zu)",
                        collision_world->ActiveColliderCount(),
                        collision_world->BlockingColliderCount(),
                        collision_world->TriggerColliderCount(),
                        collision_world->MeshColliderCount());
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Component API"))
            {
                DrawComponentApiTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
}
