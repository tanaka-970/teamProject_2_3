#include "InspectorPanel.h"

#include "PropertyDrawer.h"
#include "PlayerCompositionValidator.h"
#include "../../Assets/AssetDatabase.h"
#include "../../Object/Component/MissingComponent.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../Core/EditorContext.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/PrefabSerializer.h"

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

        // 値の比較は Reflection::ValuesEqual へ集約した。
        // PropertyType を足したときに直す場所を 1 か所に保つため。
        bool PropertyValuesEqual(const Reflection::PropertyValue& a,
            const Reflection::PropertyValue& b)
        {
            return Reflection::ValuesEqual(a, b);
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
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr)
        {
            ImGui::TextDisabled("シーンが読み込まれていません");
            return;
        }

        GameObject* object = context.Selection().ResolvePrimary(*scene);
        if (object == nullptr)
        {
            ImGui::TextDisabled("GameObject が選択されていません");
            return;
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
                return;
            }
        }

        DrawGameObjectHeader(context, *object);
        DrawPlayerComposition(context, *object);
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
            object->RemoveComponent(pending_removal_);
            context.CommitEdit();
            context.SetStatus(pending_removal_label_ + " を削除しました");
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
        add_component_panel_.Draw(context, *object);

        if (!context.Status().empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", context.Status().c_str());
        }
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
                    int added = 0;
                    for (GameObject* object : objects)
                    {
                        if (object == nullptr) continue;
                        if (!info.allow_multiple && object->FindComponent(info.type_id) != nullptr) continue;
                        if (object->AddComponent(info.type_id) != nullptr) ++added;
                    }
                    if (added > 0)
                    {
                        context.CommitEdit();
                        context.SetStatus(info.DisplayName() + " を " + std::to_string(added) + " 個へ追加しました");
                    }
                    else context.CancelEdit();
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
        if (removable && editable && ImGui::Button("選択対象から一括削除"))
            ImGui::OpenPopup("ConfirmBulkRemove");
        if (ImGui::BeginPopupModal("ConfirmBulkRemove", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("%zu 個の GameObject から %s を削除しますか？", objects.size(), title.c_str());
            if (ImGui::Button("削除"))
            {
                context.BeginEdit(title + " を一括削除");
                for (std::size_t index = 0; index < objects.size(); ++index)
                    objects[index]->RemoveComponent(components[index]);
                context.CommitEdit();
                context.SetStatus(title + " を一括削除しました");
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

        if (!ImGui::CollapsingHeader("操作対象としての構成", ImGuiTreeNodeFlags_DefaultOpen)) return;

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

        if (!result.is_controlled)
        {
            if (!editable) ImGui::TextDisabled("実行中は操作対象を変更できません");
            else if (ImGui::Button("操作対象に設定"))
            {
                context.BeginEdit("操作対象を変更");
                scene->Services().SetControlledObject(object.ID());
                context.CommitEdit();
                context.SetStatus(object.Name() + " を操作対象にしました");
            }
            ImGui::SameLine();
        }

        if (!result.complete && editable)
        {
            // ユーザーが明示的に押したときだけ追加する。
            // 削除した Component を勝手に復活させない。
            if (ImGui::Button("不足 Component を追加"))
            {
                context.BeginEdit("不足 Component を追加");
                int added = 0;
                for (const auto& requirement : result.requirements)
                {
                    if (!requirement.required || requirement.present) continue;
                    const auto* info = Core::ComponentRegistry::Find(requirement.type_name);
                    if (info == nullptr) continue;
                    // AddComponent は重複禁止型なら既存を返すので二重追加にならない。
                    if (object.FindComponent(info->type_id) != nullptr) continue;
                    if (object.AddComponent(info->type_id) != nullptr) ++added;
                }
                if (added > 0)
                {
                    context.CommitEdit();
                    context.SetStatus(std::to_string(added) + " 個の Component を追加しました");
                }
                else
                {
                    context.CancelEdit();
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

    // 読み込めなかった Component の中身を、そのまま見せる。
    //
    // 【なぜ表示するのか】
    //   MissingComponent は「型が使えないあいだ預かっているだけ」の入れ物で、
    //   保存し直せば元の型として書き戻される。ここで何も見せないと、
    //   ユーザーには「Component が消えた」ようにしか見えず、
    //   まだ生きているデータを手で消してしまう。
    //
    // 【触らないもの】
    //   PropertyBag は読み取り専用で出す。編集欄を作らない。
    //   型が分からない状態で値を書き換えると、型が戻ったときに解釈できない。
    //   消えるのはユーザーが明示的に Remove Component を押したときだけ。
    void InspectorPanel::DrawMissingComponentDetails(const Core::MissingComponent& missing)
    {
        const Core::MissingComponent::Record& record = missing.Original();

        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
            u8"この Component の型がこのビルドに存在しません");
        ImGui::TextDisabled(u8"%s", missing.DescribeReason().c_str());

        ImGui::Separator();
        ImGui::Text(u8"元の型名: %s", record.type_name.empty()
            ? u8"(不明)" : record.type_name.c_str());
        ImGui::Text(u8"Type GUID: %s", record.type_guid.IsValid()
            ? record.type_guid.ToString().c_str() : u8"(未記録)");
        ImGui::Text(u8"Module ID: %s", record.module_id.empty()
            ? u8"(未記録)" : record.module_id.c_str());
        ImGui::Text(u8"Type Version: %d", record.type_version);

        ImGui::Separator();
        ImGui::Text(u8"保持しているプロパティ: %zu 件", record.properties.Size());

        if (ImGui::TreeNodeEx(u8"Serialized Property（読み取り専用）",
            ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const Reflection::PropertyBag::Entry& entry : record.properties.Entries())
            {
                // 参照型は「何を指していたか」まで出す。
                // 型が戻ったときに参照が復元されることを確かめられるようにするため。
                switch (entry.value.Type())
                {
                case Reflection::PropertyType::ObjectReference:
                    ImGui::BulletText(u8"%s : ObjectReference -> ObjectID %llu",
                        entry.name.c_str(), static_cast<unsigned long long>(
                            entry.value.AsObjectReference().Value()));
                    break;
                case Reflection::PropertyType::ComponentReference:
                {
                    const Reflection::ComponentReference reference =
                        entry.value.AsComponentReference();
                    ImGui::BulletText(
                        u8"%s : ComponentReference -> ObjectID %llu / StableID %u",
                        entry.name.c_str(),
                        static_cast<unsigned long long>(reference.owner.Value()),
                        static_cast<unsigned int>(reference.component));
                    break;
                }
                case Reflection::PropertyType::AssetReference:
                case Reflection::PropertyType::SceneReference:
                    ImGui::BulletText(u8"%s : %s -> %s", entry.name.c_str(),
                        Reflection::ToString(entry.value.Type()),
                        entry.value.AsString().empty()
                            ? u8"(未設定)" : entry.value.AsString().c_str());
                    break;
                case Reflection::PropertyType::Array:
                    ImGui::BulletText(u8"%s : Array<%s> × %zu", entry.name.c_str(),
                        Reflection::ToString(entry.value.ArrayElementType()),
                        entry.value.ArrayElements().size());
                    break;
                case Reflection::PropertyType::String:
                    ImGui::BulletText(u8"%s : String = \"%s\"", entry.name.c_str(),
                        entry.value.AsString().c_str());
                    break;
                default:
                    ImGui::BulletText(u8"%s : %s", entry.name.c_str(),
                        Reflection::ToString(entry.value.Type()));
                    break;
                }
            }
            if (record.properties.Size() == 0)
            {
                ImGui::TextDisabled(u8"（保持しているプロパティはありません）");
            }
            ImGui::TreePop();
        }

        ImGui::TextDisabled(
            u8"型が使えるようになれば、次に読み込んだ時点で自動的に復元されます。");
        ImGui::TextDisabled(
            u8"保存し直しても、この内容は元の型として書き戻されます。");
    }

    void InspectorPanel::DrawComponent(EditorContext& context, Core::Component& component)
    {
        const ComponentTypeInfo* info = ComponentRegistry::Find(component.TypeID());
        const auto* missing = dynamic_cast<const Core::MissingComponent*>(&component);

        const std::string title = missing != nullptr
            ? missing->DescribeMissingType()
            : (info != nullptr
                ? info->DisplayName()
                : std::string("(未登録) ") + component.TypeName());

        const bool editable = context.CanEdit();
        const bool removable = ComponentRegistry::IsRemovable(component.TypeID());

        // 有効チェックボックスをヘッダーの左へ置く。
        bool component_enabled = component.Enabled();
        if (ImGui::Checkbox("##ComponentEnabled", &component_enabled) && editable)
        {
            context.BeginEdit(title + " の有効状態を変更");
            component.SetEnabled(component_enabled);
            context.CommitEdit();
        }
        ImGui::SameLine();

        const bool opened = ImGui::CollapsingHeader(title.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen);

        if (info != nullptr && !info->tooltip.empty() && ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(info->tooltip.c_str());
            ImGui::EndTooltip();
        }

        if (!opened) return;

        ImGui::Indent();

        if (missing != nullptr)
        {
            // 預かっている内容を出すだけ。PropertyRegistry の対象外なので、
            // 下の通常の描画経路には入れない。
            DrawMissingComponentDetails(*missing);

            // 削除の導線は通常の Component と同じものを使う。
            // 「明示的に消すまで残る」ことと「消せる」ことを両立させる。
            ImGui::Spacing();
            if (!editable)
            {
                ImGui::TextDisabled("実行中は削除できません");
            }
            else if (ImGui::Button("この Missing Component を削除"))
            {
                pending_removal_ = &component;
                pending_removal_label_ = title;
            }
            ImGui::TextDisabled(
                u8"削除すると、預かっている内容も一緒に失われます。");

            ImGui::Unindent();
            return;
        }

        // 静的な登録と、インスタンスごとの申告のどちらかがあれば編集欄を出す。
        // 型ごとの分岐は書かない。Script は後者だけを持つ。
        const bool has_dynamic = component.DynamicProperties() != nullptr &&
            !component.DynamicProperties()->empty();

        if (Reflection::PropertyRegistry::HasProperties(component.TypeID()) || has_dynamic)
        {
            const bool had_transaction = context.History().InTransaction();
            if (editable && !had_transaction) context.BeginEdit(title + " の設定を変更");
            if (!editable)
            {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            }

            const bool changed = PropertyDrawer::DrawAll(component,
                context.GetAssetDatabase(), context.GetScene());

            if (!editable)
            {
                ImGui::PopStyleVar();
                ImGui::PopItemFlag();
            }
            if (changed)
            {
                context.MarkDirty();
                prefab_cache_valid_ = false;
            }

            // Draw の前に Snapshot を取るため、最初の変更も Undo へ正しく入る。
            // Drag 中は transaction を保持し、マウスを離した Frame で 1 操作として確定する。
            if (editable && context.History().InTransaction() && !ImGui::IsAnyItemActive())
            {
                if (changed || had_transaction)
                {
                    // 編集が確定したこの瞬間に、型へ変更を知らせる。
                    //
                    // ここへ置く理由:
                    //   DrawAll が回している最中に呼ぶと、DynamicProperties() が
                    //   返した配列を型が作り直した場合に、走査中のコンテナが
                    //   入れ替わる。ここは「どの入力欄も掴まれていないフレーム」なので、
                    //   走査はすべて終わっている。
                    //
                    //   Drag 中は毎フレーム呼ばれない。掴んでいる間は
                    //   IsAnyItemActive() が true のままで、この分岐へ来ない。
                    //
                    //   Script では、これによって Script Asset を選び替えた直後に
                    //   Field の顔ぶれが入れ替わる。
                    component.OnPropertyChanged(nullptr);
                    context.CommitEdit();
                }
                else
                {
                    context.CancelEdit();
                }
            }
        }
        else
        {
            ImGui::TextDisabled("編集できる設定はありません");
        }

        ImGui::Spacing();
        if (!removable)
        {
            ImGui::TextDisabled("このコンポーネントは削除できません");
        }
        else if (!editable)
        {
            ImGui::TextDisabled("実行中は削除できません");
        }
        else if (ImGui::Button("コンポーネントを削除"))
        {
            // ここでは予約だけ控える。実際の削除は Component 一覧の走査が終わってから。
            pending_removal_ = &component;
            pending_removal_label_ = title;
        }

        ImGui::Unindent();
    }
}
