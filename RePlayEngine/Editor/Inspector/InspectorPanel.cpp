#include "InspectorPanel.h"

#include "PropertyDrawer.h"
#include "PlayerCompositionValidator.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../Core/EditorContext.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"
#include "../../Scene/Runtime/Scene.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cstring>
#include <string>

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

        ImGui::TextDisabled("ObjectID %s", object.ID().ToString().c_str());
        if (object.Parent() != nullptr)
        {
            ImGui::TextDisabled("親: %s (ID %s)",
                object.Parent()->Name().c_str(), object.Parent()->ID().ToString().c_str());
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

    void InspectorPanel::DrawComponent(EditorContext& context, Core::Component& component)
    {
        const ComponentTypeInfo* info = ComponentRegistry::Find(component.TypeID());
        const std::string title = info != nullptr
            ? info->DisplayName()
            : std::string("(未登録) ") + component.TypeName();

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

        if (Reflection::PropertyRegistry::HasProperties(component.TypeID()))
        {
            if (PropertyDrawer::DrawAll(component, context.GetAssetDatabase(), context.GetScene()))
            {
                // 値の変更は 1 操作としてまとめる。
                // ドラッグ中は毎フレーム通るが、BeginEdit は最初の 1 回だけ有効。
                context.BeginEdit(title + " の設定を変更");
                context.MarkDirty();
            }
            // ドラッグが終わったところで確定させる。
            if (context.History().InTransaction() && !ImGui::IsAnyItemActive())
            {
                context.CommitEdit();
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
