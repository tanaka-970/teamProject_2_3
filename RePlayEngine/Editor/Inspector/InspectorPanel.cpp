// InspectorPanel の責務を 3 つのファイルへ分けている:
//   InspectorPanel.cpp                 … 単体選択のヘッダー・Prefab 表示（このファイル）
//   InspectorPanelMultiSelection.cpp   … 複数選択と共通 Component の一括編集
//   InspectorPanelComponents.cpp       … Component 単体の表示・診断・削除
//
// PropertyRegistry を通る描画経路は PropertyDrawer に委譲し、ここでは
// 選択状態と Component の入口だけを担当する。

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

        DrawGameObjectHeader(context, *object);
        DrawPlayerComposition(context, *object, show_game_template_components);
        ImGui::Separator();

        // 添字で回す。描画中に Component が追加されても、この回の走査は
        // 開始時点の個数で終わるため範囲外へ出ない。
        const std::size_t count = object->ComponentCount();
        for (std::size_t index = 0; index < count && index < object->ComponentCount(); ++index)
        {
            Core::Component* component = object->ComponentAt(index);
            if (component == nullptr) continue;

            // 削除予約済みのものは、その瞬間から表示しない。
            if (component->PendingDestroy()) continue;

            ImGui::PushID(static_cast<int>(index));
            DrawComponent(context, *component);
            ImGui::PopID();
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
            if (asset != nullptr) ImGui::TextWrapped("Path: %s", asset->source_path.string().c_str());
            ImGui::TextDisabled("AssetGUID: %s", root->PrefabSourceGUID().c_str());
            ImGui::TextDisabled("Local ID: %llu", static_cast<unsigned long long>(root->PrefabLocalID()));
        }
    }

}
