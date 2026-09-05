// InspectorPanel の責務を 3 つのファイルへ分けている:
//   InspectorPanel.cpp                 … 単体選択のヘッダー・Prefab 表示（このファイル）
//   InspectorPanelMultiSelection.cpp   … 複数選択と共通 Component の一括編集
//   InspectorPanelComponents.cpp       … Component 単体の表示・診断・削除
//
// PropertyRegistry を通る描画経路は PropertyDrawer に委譲し、ここでは
// 選択状態と Component の入口だけを担当する。

#include "../ReorderableList.h"
#include "../Style/EditorStyle.h"
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

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    using Core::ComponentRegistry;
    using Core::ComponentTypeInfo;
    using Core::GameObject;

    namespace
    {
        void CopyToBuffer(char* buffer, int size, const std::string& text)
        {
            const int length = static_cast<int>(text.size()) < size - 1
                ? static_cast<int>(text.size()) : size - 1;
            std::memcpy(buffer, text.data(), static_cast<std::size_t>(length));
            buffer[length] = '\0';
        }

        bool HasSiblingNameDuplicate(const Scene::Scene& scene,
            const GameObject& object, const std::string& name)
        {
            if (object.Parent() != nullptr)
            {
                for (const GameObject* sibling : object.Parent()->Children())
                    if (sibling != nullptr && sibling != &object && sibling->Name() == name) return true;
                return false;
            }
            for (std::size_t i = 0; i < scene.GameObjectCount(); ++i)
            {
                const GameObject* sibling = scene.GameObjectAt(i);
                if (sibling != nullptr && sibling != &object && sibling->Parent() == nullptr &&
                    sibling->Name() == name) return true;
            }
            return false;
        }

    }

    void InspectorPanel::Draw(EditorContext& context)
    {
        PanelTabColorScope panel_tab_color("Editor");
        ImGui::Begin("インスペクター");
        DrawContents(context);
        ImGui::End();
    }

    void InspectorPanel::DrawContents(EditorContext& context)
    {
        bool show_game_template_components = false;
        (void)DrawContents(context, show_game_template_components);
    }

    bool InspectorPanel::DrawContents(EditorContext& context,
        bool& show_game_template_components)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr)
        {
            ImGui::TextDisabled("シーンが読み込まれていません");
            return false;
        }

        GameObject* object = context.Selection().ResolvePrimary(*scene);
        if (object == nullptr)
        {
            ImGui::TextDisabled("GameObject が選択されていません");
            return false;
        }

        if (context.PlayMode())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "実行中（読み取り専用）");
            ImGui::TextDisabled("プレイ中の変更は編集シーンへ保存されません");
            ImGui::Separator();
        }

        if (context.Selection().Count() > 1)
        {
            std::vector<GameObject*> objects;
            objects.reserve(context.Selection().Count());
            for (const Core::ObjectID id : context.Selection().All())
            {
                GameObject* selected = scene->FindGameObjectByID(id);
                if (selected != nullptr && !selected->PendingDestroy()) objects.push_back(selected);
            }
            if (objects.size() > 1)
            {
                DrawMultiSelection(context, objects);
                return false;
            }
        }

        if (selected_component_owner_ != object->ID().Value())
        {
            selected_component_owner_ = object->ID().Value();
            selected_component_stable_ = Core::invalid_component_stable_id;
        }

        DrawGameObjectHeader(context, *object);
        DrawPlayerComposition(context, *object, show_game_template_components);
        ImGui::Separator();

        const std::size_t count = object->ComponentCount();
        pending_component_move_source_ = static_cast<std::size_t>(-1);
        pending_component_move_destination_ = static_cast<std::size_t>(-1);
        if (!component_category_order_initialized_)
        {
            component_category_order_ = Core::ComponentRegistry::Categories();
            component_category_order_.push_back("未分類");
            component_category_order_initialized_ = true;
        }

        const auto component_group = [](const std::string& category)
        {
            if (category == "Rendering" || category == "Lighting" ||
                category == "Camera" || category == "Landscape" ||
                category == "UI")
                return 1;
            if (category == "Scripting" || category == "Gameplay" ||
                category == "Motion" || category == "Physics" ||
                category == "Navigation" || category == "Audio")
                return 2;
            return 3;
        };
        const auto component_category = [](const Core::Component* component) -> std::string
        {
            if (component == nullptr) return std::string("未分類");
            const Core::ComponentTypeInfo* info =
                Core::ComponentRegistry::Find(component->TypeID());
            if (info == nullptr) return "未分類";
            return info->category.empty() ? std::string("Gameplay") : info->category;
        };

        const void* summary_scene = context.GetScene();
        const std::uint32_t summary_generation = context.GetScene() != nullptr
            ? context.GetScene()->StructureGeneration() : 0;
        if (component_summary_scene_ != summary_scene ||
            component_summary_owner_ != object->ID().Value() ||
            component_summary_generation_ != summary_generation ||
            component_summary_count_ != count)
        {
            component_category_by_index_.assign(count, "未分類");
            component_category_summaries_.clear();
            component_category_summaries_.reserve(component_category_order_.size());
            for (const std::string& category : component_category_order_)
                component_category_summaries_.push_back({ category });

            for (std::size_t index = 0; index < count; ++index)
            {
                Core::Component* component = object->ComponentAt(index);
                const std::string category = component_category(component);
                component_category_by_index_[index] = category;
                if (component == nullptr || component->PendingDestroy()) continue;
                const auto found = std::find_if(component_category_summaries_.begin(),
                    component_category_summaries_.end(),
                    [&category](const ComponentCategorySummary& summary)
                    { return summary.category == category; });
                ComponentCategorySummary* summary = nullptr;
                if (found == component_category_summaries_.end())
                {
                    component_category_summaries_.push_back({ category });
                    summary = &component_category_summaries_.back();
                }
                else
                {
                    summary = &*found;
                }
                ++summary->count;
                const Core::ComponentTypeInfo* info =
                    Core::ComponentRegistry::Find(component->TypeID());
                const std::string name = info != nullptr
                    ? info->DisplayName() : component->TypeName();
                if (summary->names.size() < 96)
                {
                    if (!summary->names.empty()) summary->names += " / ";
                    summary->names += name;
                }
            }
            for (ComponentCategorySummary& summary : component_category_summaries_)
            {
                if (summary.count == 0) continue;
                if (summary.names.size() >= 96) summary.names += " / ...";
                const int group = component_group(summary.category);
                summary.display_label = summary.category + "  (" +
                    std::to_string(summary.count) + ")  " + summary.names +
                    "##InspectorComponentCategory_" + std::to_string(group) +
                    "_" + summary.category;
            }
            component_summary_scene_ = summary_scene;
            component_summary_owner_ = object->ID().Value();
            component_summary_generation_ = summary_generation;
            component_summary_count_ = count;
        }

        // 掴んでいる対象を一覧の外にも出す。行が動くと見失うため。
        if (const char* dragging = ActiveReorderLabel(object))
        {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget]);
            ImGui::Text("移動中: %s   落としたい行の上下へ", dragging);
            ImGui::PopStyleColor();
        }

        const auto draw_component_group = [&](int group)
        {
            // 「すべて」は追加順の一列にする。分類で束ねると並べ替えても動いて見えない。
            if (group == 0)
            {
                for (std::size_t index = 0; index < count &&
                    index < object->ComponentCount(); ++index)
                {
                    Core::Component* component = object->ComponentAt(index);
                    if (component == nullptr || component->PendingDestroy()) continue;
                    DrawComponent(context, *component, index, count, object);
                    if (pending_component_move_source_ != static_cast<std::size_t>(-1))
                        break;
                }
                return;
            }
            bool drew_category = false;
            for (const ComponentCategorySummary& summary : component_category_summaries_)
            {
                if (summary.count == 0 || (group != 0 &&
                    component_group(summary.category) != group)) continue;
                drew_category = true;
                if (!ImGui::CollapsingHeader(summary.display_label.c_str())) continue;
                ImGui::Indent();
                for (std::size_t index = 0; index < count &&
                    index < object->ComponentCount(); ++index)
                {
                    Core::Component* component = object->ComponentAt(index);
                    if (component == nullptr || component->PendingDestroy() ||
                        index >= component_category_by_index_.size() ||
                        component_category_by_index_[index] != summary.category)
                        continue;
                    DrawComponent(context, *component, index, count, object);
                    if (pending_component_move_source_ != static_cast<std::size_t>(-1))
                        break;
                }
                ImGui::Unindent();
                if (pending_component_move_source_ != static_cast<std::size_t>(-1))
                    break;
            }
            if (!drew_category)
                ImGui::TextDisabled("この分類にはコンポーネントがありません");
        };

        ImGui::TextDisabled("分類を開くと設定を表示します。順序は ◆ / ボタン / ドラッグで変更できます");
        if (ImGui::BeginTabBar("InspectorComponentGroups"))
        {
            if (ImGui::BeginTabItem("すべて"))
            {
                draw_component_group(0);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("レンダー"))
            {
                draw_component_group(1);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("スクリプト / 動作"))
            {
                draw_component_group(2);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("その他"))
            {
                draw_component_group(3);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (pending_component_move_source_ != static_cast<std::size_t>(-1) &&
            pending_removal_ == nullptr)
        {
            context.BeginEdit("コンポーネントを並べ替え");
            if (object->MoveComponent(pending_component_move_source_,
                pending_component_move_destination_))
            {
                context.CommitEdit();
                context.SetStatus("コンポーネントの順序を変更しました");
            }
            else
            {
                context.CancelEdit();
                context.SetStatus("コンポーネントの順序を変更できませんでした");
            }
        }

        // Inspector がキーボード所有者のときだけ Backspace を Component 削除へ使う。
        // InputText 等の文字編集が所有している間は文字削除を最優先する。
        if (pending_removal_ == nullptr && context.CanEdit() &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyPressed(VK_BACK) &&
            selected_component_stable_ != Core::invalid_component_stable_id)
        {
            Core::Component* selected_component =
                object->FindComponentByStableID(selected_component_stable_);
            if (selected_component != nullptr && !selected_component->PendingDestroy())
            {
                if (!Core::ComponentRegistry::IsRemovable(selected_component->TypeID()))
                {
                    context.SetStatus("このコンポーネントは削除できません");
                }
                else
                {
                    const auto dependents = Core::ComponentDependencyRules::FindDirectDependents(
                        *object, *selected_component);
                    if (!dependents.empty())
                    {
                        context.SetStatus(Core::ComponentRegistry::DisplayNameOf(
                            selected_component->TypeID()) + " は " +
                            Core::ComponentRegistry::DisplayNameOf(dependents.front()->TypeID()) +
                            " が必須として使用中です");
                    }
                    else
                    {
                        pending_removal_ = selected_component;
                        pending_removal_label_ = Core::ComponentRegistry::DisplayNameOf(
                            selected_component->TypeID());
                    }
                }
            }
        }

        // 走査を終えてから削除を確定させる。
        // 途中で確定すると Component コンテナが詰められ、残りの添字がずれてしまう。
        if (pending_removal_ != nullptr)
        {
            context.BeginEdit(pending_removal_label_ + " を削除");
            const std::vector<Core::Component*> dependents =
                Core::ComponentDependencyRules::FindDirectDependents(
                    *object, *pending_removal_);
            if (!dependents.empty())
            {
                context.CancelEdit();
                context.SetStatus(pending_removal_label_ + " は " +
                    Core::ComponentRegistry::DisplayNameOf(
                        dependents.front()->TypeID()) + " が必須として使用中です");
            }
            else if (object->RemoveComponent(pending_removal_))
            {
                context.CommitEdit();
                context.SetStatus(pending_removal_label_ + " を削除しました");
            }
            else
            {
                context.CancelEdit();
                context.SetStatus(pending_removal_label_ + " を削除できませんでした");
            }
            pending_removal_ = nullptr;
            pending_removal_label_.clear();
            selected_component_stable_ = Core::invalid_component_stable_id;
        }

        ImGui::Separator();

        const bool editable = context.CanEdit();
        if (!editable) ImGui::TextDisabled("実行中はコンポーネントを変更できません");
        else if (ImGui::Button("コンポーネントを追加", ImVec2(-1.0f, 0.0f)))
        {
            add_component_panel_.RequestOpen();
        }
        bool show_game_template_components_changed = false;
        add_component_panel_.Draw(context, *object, show_game_template_components,
            show_game_template_components_changed);

        if (!context.Status().empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", context.Status().c_str());
        }

        return show_game_template_components_changed;
    }

    void InspectorPanel::DrawGameObjectHeader(EditorContext& context, GameObject& object)
    {
        const bool editable = context.CanEdit();

        // 選択が変わったタイミングでだけ名前バッファを作り直す。
        // 毎フレーム上書きすると入力途中の文字が消えてしまう。
        if (name_buffer_owner_ != object.ID().Value())
        {
            name_buffer_owner_ = object.ID().Value();
            CopyToBuffer(name_buffer_, name_buffer_size, object.Name());
        }

        bool enabled = object.Enabled();
        if (ImGui::Checkbox("##GameObjectEnabled", &enabled) && editable)
        {
            context.BeginEdit("GameObject の有効状態を変更");
            object.SetEnabled(enabled);
            context.CommitEdit();
        }
        ImGui::SameLine();

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##GameObjectName", name_buffer_, name_buffer_size,
            ImGuiInputTextFlags_EnterReturnsTrue) && editable)
        {
            context.BeginEdit("GameObject 名を変更");
            object.SetName(name_buffer_);
            context.CommitEdit();
            if (Scene::Scene* scene = context.GetScene();
                scene != nullptr && HasSiblingNameDuplicate(*scene, object, object.Name()))
            {
                context.SetStatus("同じ階層に同名 GameObject があります（参照は ObjectID なので維持されます）");
            }
        }

        if (object.Parent() != nullptr)
        {
            ImGui::TextDisabled("親: %s", object.Parent()->Name().c_str());
        }
        else
        {
            ImGui::TextDisabled("親: なし（シーン直下）");
        }

        if (context.Selection().Count() > 1)
        {
            ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f),
                "%zu 個を選択中（主選択を表示）", context.Selection().Count());
        }

        if (Scene::Scene* scene = context.GetScene())
        {
            const bool controlled = scene->Services().ControlledObject() == object.ID();
            ImGui::TextDisabled("操作対象:");
            ImGui::SameLine();
            if (controlled)
            {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "この GameObject");
            }
            else if (editable && ImGui::SmallButton("この GameObject を操作対象に設定"))
            {
                context.BeginEdit("操作対象を変更");
                scene->Services().SetControlledObject(object.ID());
                context.CommitEdit();
                context.SetStatus(object.Name() + " を操作対象にしました");
            }
            else if (!editable)
            {
                ImGui::TextDisabled("実行中は変更できません");
            }
        }

        // Diagnostics パネルを開かなくても、選択中 GameObject の問題はここで見える。
        if (Scene::Scene* scene = context.GetScene())
        {
            const auto issues = SceneValidator::Validate(*scene, context.GetAssetDatabase());
            int object_issue_count = 0;
            for (const ValidationIssue& issue : issues)
                if (issue.object == object.ID() && issue.severity != ValidationSeverity::Info)
                    ++object_issue_count;
            if (object_issue_count > 0 && ImGui::CollapsingHeader("この GameObject の診断", ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (const ValidationIssue& issue : issues)
                {
                    if (issue.object != object.ID() || issue.severity == ValidationSeverity::Info) continue;
                    const ImVec4 color = issue.severity == ValidationSeverity::Error
                        ? ImVec4(1.0f, 0.35f, 0.30f, 1.0f)
                        : ImVec4(1.0f, 0.72f, 0.30f, 1.0f);
                    ImGui::TextColored(color, "[%s]", issue.code.c_str());
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", issue.message.c_str());
                    if (!issue.suggestion.empty())
                    {
                        ImGui::Indent();
                        ImGui::TextDisabled("%s", issue.suggestion.c_str());
                        ImGui::Unindent();
                    }
                }
                ImGui::Separator();
            }
        }

        DrawPrefabHeader(context, object);

        if (ImGui::CollapsingHeader("Advanced / IDs"))
        {
            ImGui::TextDisabled("ObjectID: %s", object.ID().ToString().c_str());
            if (object.Parent() != nullptr)
                ImGui::TextDisabled("Parent ObjectID: %s", object.Parent()->ID().ToString().c_str());
            else
                ImGui::TextDisabled("Parent ObjectID: (none)");
        }
    }

    void InspectorPanel::DrawPrefabHeader(EditorContext& context, GameObject& object)
    {
        if (!object.IsPrefabInstance()) return;
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr) return;

        GameObject* root = scene->FindGameObjectByID(object.PrefabInstanceRoot());
        if (root == nullptr)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.30f, 0.25f, 1.0f), "Missing Prefab Root");
            return;
        }

        const Assets::AssetDatabase* database = context.GetAssetDatabase();
        const Assets::AssetRecord* asset = database != nullptr
            ? database->FindByGuid(object.PrefabSourceGUID()) : nullptr;

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.0f, 1.0f),
            root == &object ? "Prefab Root" : "Prefab Instance");
        ImGui::SameLine();
        if (asset != nullptr)
            ImGui::TextUnformatted(asset->display_name.c_str());
        else
            ImGui::TextColored(ImVec4(1.0f, 0.30f, 0.25f, 1.0f), "[Missing Prefab]");

        if (root != &object)
        {
            if (ImGui::Button("Prefab Rootを選択")) context.Selection().Select(root->ID(), false);
            return;
        }

        if (prefab_cache_owner_ != root->ID().Value() ||
            prefab_cache_guid_ != root->PrefabSourceGUID())
        {
            prefab_cache_owner_ = root->ID().Value();
            prefab_cache_guid_ = root->PrefabSourceGUID();
            prefab_cache_valid_ = false;
        }

        bool refresh = ImGui::SmallButton("Overridesを再検証");
        if ((!prefab_cache_valid_ || refresh) && asset != nullptr)
        {
            const Scene::Serialization::PrefabOverrideSummary summary =
                Scene::Serialization::PrefabSerializer::InspectOverrides(
                    *scene, root->ID(), asset->source_path, root->PrefabSourceGUID());
            prefab_cache_missing_ = summary.missing_source;
            prefab_cache_nested_ = summary.unsupported_nested_prefab;
            prefab_cache_overrides_ = summary.has_overrides;
            prefab_cache_details_ = summary.details;
            prefab_cache_valid_ = true;
        }
        else if (asset == nullptr)
        {
            prefab_cache_missing_ = true;
            prefab_cache_valid_ = true;
        }

        ImGui::SameLine();
        if (prefab_cache_nested_)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Unsupported Nested Prefab");
        else if (prefab_cache_overrides_)
            ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.20f, 1.0f), "Overrides");
        else if (!prefab_cache_missing_)
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "No Overrides");

        if (prefab_cache_overrides_ && ImGui::TreeNode("Override Details"))
        {
            for (const std::string& detail : prefab_cache_details_)
                ImGui::BulletText("%s", detail.c_str());
            ImGui::TreePop();
        }

        const bool editable = context.CanEdit();
        const bool source_available = asset != nullptr && !prefab_cache_nested_;
        if (ImGui::Button("Apply", ImVec2(80.0f, 0.0f)) && editable && source_available)
            ImGui::OpenPopup("ConfirmPrefabApply");
        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(80.0f, 0.0f)) && editable && source_available)
            ImGui::OpenPopup("ConfirmPrefabRevert");
        ImGui::SameLine();
        if (ImGui::Button("Unpack", ImVec2(80.0f, 0.0f)) && editable)
            ImGui::OpenPopup("ConfirmPrefabUnpack");

        if (ImGui::BeginPopupModal("ConfirmPrefabApply", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Prefab Asset原本を現在のOverridesで更新します。");
            ImGui::TextDisabled("既存Assetは .bak へ退避され、失敗時は復元されます。");
            if (ImGui::Button("Apply Overrides") && asset != nullptr)
            {
                context.BeginEdit("Prefab OverridesをApply");
                std::string error;
                if (Scene::Serialization::PrefabSerializer::ApplyOverrides(
                    *scene, root->ID(), asset->source_path, root->PrefabSourceGUID(), error))
                {
                    context.CommitEdit();
                    context.SetStatus("Prefab Assetを更新しました: " + asset->display_name);
                }
                else
                {
                    context.CancelEdit();
                    context.SetStatus("Prefab Apply失敗: " + error);
                }
                prefab_cache_valid_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("ConfirmPrefabRevert", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("このInstanceのOverridesを破棄してAsset状態へ戻しますか？");
            if (ImGui::Button("Revert Overrides") && asset != nullptr)
            {
                context.BeginEdit("Prefab OverridesをRevert");
                std::string error;
                Scene::Serialization::SceneLoadReport report;
                if (Scene::Serialization::PrefabSerializer::RevertOverrides(
                    *scene, root->ID(), asset->source_path, root->PrefabSourceGUID(), error, &report))
                {
                    context.CommitEdit();
                    context.Selection().Select(root->ID(), false);
                    context.SetStatus("Prefab Overridesを戻しました");
                }
                else
                {
                    context.CancelEdit();
                    context.SetStatus("Prefab Revert失敗: " + error);
                }
                prefab_cache_valid_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("ConfirmPrefabUnpack", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("PrefabとのLinkを解除します。GameObjectは残ります。");
            if (ImGui::Button("Unpack Prefab"))
            {
                context.BeginEdit("PrefabをUnpack");
                std::string error;
                if (Scene::Serialization::PrefabSerializer::Unpack(*scene, root->ID(), error))
                {
                    context.CommitEdit();
                    context.SetStatus("PrefabをUnpackしました");
                }
                else
                {
                    context.CancelEdit();
                    context.SetStatus("Prefab Unpack失敗: " + error);
                }
                prefab_cache_valid_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::CollapsingHeader("Prefab Source Details"))
        {
            if (asset != nullptr)
                ImGui::TextWrapped("Path: %s", asset->source_path.u8string().c_str());
            ImGui::TextDisabled("AssetGUID: %s", root->PrefabSourceGUID().c_str());
            ImGui::TextDisabled("Local ID: %llu", static_cast<unsigned long long>(root->PrefabLocalID()));
        }
    }

}
