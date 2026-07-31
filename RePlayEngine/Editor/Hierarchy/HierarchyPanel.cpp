#include "HierarchyPanel.h"

#include "../Core/EditorContext.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"

#include "imgui/imgui.h"

#include <cstring>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    using Core::GameObject;
    using Core::ObjectID;

    namespace
    {
        constexpr const char* drag_drop_type = "REPLAY_GAMEOBJECT";
        constexpr int maximum_depth = 64;

        void CopyToBuffer(char* buffer, int size, const std::string& text)
        {
            const int length = static_cast<int>(text.size()) < size - 1
                ? static_cast<int>(text.size()) : size - 1;
            std::memcpy(buffer, text.data(), static_cast<std::size_t>(length));
            buffer[length] = '\0';
        }
    }

    void HierarchyPanel::Draw(EditorContext& context)
    {
        ImGui::Begin("階層");
        DrawContents(context);
        ImGui::End();
    }

    void HierarchyPanel::DrawContents(EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr)
        {
            ImGui::TextDisabled("シーンが読み込まれていません");
            return;
        }

        const bool editable = context.CanEdit();

        ImGui::TextUnformatted(context.DisplayTitle().c_str());
        if (context.PlayMode())
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "[実行中]");
        }
        ImGui::Separator();

        if (editable)
        {
            if (ImGui::Button("GameObject を作成"))
            {
                CreateEmptyGameObject(context, nullptr);
            }
            ImGui::SameLine();
            if (ImGui::Button("複製")) DuplicateSelected(context);
            ImGui::SameLine();
            if (ImGui::Button("削除")) DestroySelected(context);
        }
        else
        {
            ImGui::TextDisabled("実行中は階層を編集できません");
        }
        ImGui::Separator();

        // 親を持たないものから再帰的に描く。
        for (GameObject* root : scene->RootGameObjects())
        {
            if (root == nullptr || root->PendingDestroy()) continue;
            DrawNode(context, *root, 0);
        }

        // 何も無いところで右クリックしたときのメニュー。
        if (ImGui::BeginPopupContextWindow("HierarchyBackground",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            DrawContextMenu(context, nullptr);
            ImGui::EndPopup();
        }

        // 空白部分へのドロップで Scene 直下へ移す。
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_drop_type))
            {
                if (payload->DataSize == sizeof(ObjectID::ValueType))
                {
                    ObjectID::ValueType raw = 0;
                    std::memcpy(&raw, payload->Data, sizeof(raw));
                    pending_reparent_child_ = ObjectID(raw);
                    pending_reparent_to_root_ = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        // ツリー走査が終わってから親子関係を変更する。
        if (pending_reparent_child_.Valid() && editable)
        {
            GameObject* child = scene->FindGameObjectByID(pending_reparent_child_);
            GameObject* parent = pending_reparent_to_root_
                ? nullptr : scene->FindGameObjectByID(pending_reparent_parent_);

            if (child != nullptr)
            {
                context.BeginEdit("親子関係を変更");
                // SetParent は自分自身や子孫を親にしようとすると false を返す。
                // 循環階層はここで確実に弾かれる。
                if (child->SetParent(parent, true))
                {
                    context.CommitEdit();
                    context.SetStatus(parent != nullptr
                        ? child->Name() + " を " + parent->Name() + " の子にしました"
                        : child->Name() + " をシーン直下へ移しました");
                }
                else
                {
                    context.CancelEdit();
                    context.SetStatus("自分自身または子孫を親にはできません");
                }
            }
            pending_reparent_child_ = ObjectID::Invalid();
            pending_reparent_parent_ = ObjectID::Invalid();
            pending_reparent_to_root_ = false;
        }
    }

    void HierarchyPanel::DrawNode(EditorContext& context, GameObject& object, int depth)
    {
        if (depth > maximum_depth) return;

        ImGui::PushID(static_cast<int>(object.ID().Value()));

        const bool editable = context.CanEdit();
        const bool selected = context.Selection().IsSelected(object.ID());
        const bool has_children = !object.Children().empty();

        // 有効チェックボックス。
        bool enabled = object.Enabled();
        if (ImGui::Checkbox("##Enabled", &enabled) && editable)
        {
            context.BeginEdit("GameObject の有効状態を変更");
            object.SetEnabled(enabled);
            context.CommitEdit();
        }
        ImGui::SameLine();

        // 名前変更中はテキスト入力へ差し替える。
        if (renaming_ == object.ID())
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##Rename", rename_buffer_, rename_buffer_size,
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            {
                if (editable)
                {
                    context.BeginEdit("GameObject 名を変更");
                    object.SetName(rename_buffer_);
                    context.CommitEdit();
                }
                renaming_ = ObjectID::Invalid();
            }
            else if (ImGui::IsItemDeactivated())
            {
                renaming_ = ObjectID::Invalid();
            }
            ImGui::PopID();
            return;
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;
        if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        // 無効な GameObject は薄く表示する。
        const bool dimmed = !object.ActiveInHierarchy();
        if (dimmed) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));

        const bool opened = ImGui::TreeNodeEx("##Node", flags, "%s", object.Name().c_str());

        if (dimmed) ImGui::PopStyleColor();

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            context.Selection().Select(object.ID(), ImGui::GetIO().KeyShift);
        }

        HandleDragAndDrop(context, object);

        if (ImGui::BeginPopupContextItem("NodeContext"))
        {
            // メニューを開いた対象を選択しておく。誤操作を防ぐため。
            if (!context.Selection().IsSelected(object.ID()))
            {
                context.Selection().Select(object.ID(), false);
            }
            DrawContextMenu(context, &object);
            ImGui::EndPopup();
        }

        if (opened && has_children)
        {
            // 走査中に子リストが変わらないよう控えを取る。
            const std::vector<GameObject*> children = object.Children();
            for (GameObject* child : children)
            {
                if (child == nullptr || child->PendingDestroy()) continue;
                DrawNode(context, *child, depth + 1);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    void HierarchyPanel::HandleDragAndDrop(EditorContext& context, GameObject& object)
    {
        if (!context.CanEdit()) return;

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            const ObjectID::ValueType raw = object.ID().Value();
            ImGui::SetDragDropPayload(drag_drop_type, &raw, sizeof(raw));
            ImGui::TextUnformatted(object.Name().c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_drop_type))
            {
                if (payload->DataSize == sizeof(ObjectID::ValueType))
                {
                    ObjectID::ValueType raw = 0;
                    std::memcpy(&raw, payload->Data, sizeof(raw));
                    // 実際の付け替えは走査後。ここでは要求だけ控える。
                    pending_reparent_child_ = ObjectID(raw);
                    pending_reparent_parent_ = object.ID();
                    pending_reparent_to_root_ = false;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void HierarchyPanel::DrawContextMenu(EditorContext& context, GameObject* object)
    {
        const bool editable = context.CanEdit();

        if (ImGui::MenuItem("GameObject を作成", nullptr, false, editable))
        {
            CreateEmptyGameObject(context, nullptr);
        }
        if (object != nullptr &&
            ImGui::MenuItem("子として GameObject を作成", nullptr, false, editable))
        {
            CreateEmptyGameObject(context, object);
        }

        if (object == nullptr) return;

        ImGui::Separator();
        if (ImGui::MenuItem("名前を変更", "F2", false, editable))
        {
            renaming_ = object->ID();
            CopyToBuffer(rename_buffer_, rename_buffer_size, object->Name());
        }
        if (ImGui::MenuItem("複製", "Ctrl+D", false, editable)) DuplicateSelected(context);
        if (ImGui::MenuItem("シーン直下へ移動", nullptr, false,
            editable && object->Parent() != nullptr))
        {
            pending_reparent_child_ = object->ID();
            pending_reparent_to_root_ = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("削除", "Del", false, editable)) DestroySelected(context);
    }

    void HierarchyPanel::CreateEmptyGameObject(EditorContext& context, GameObject* parent)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return;

        context.BeginEdit("GameObject を作成");
        GameObject* created = scene->CreateGameObject("GameObject");
        if (created == nullptr)
        {
            context.CancelEdit();
            return;
        }
        if (parent != nullptr) created->SetParent(parent, false);

        context.CommitEdit();
        context.Selection().Select(created->ID(), false);
        context.SetStatus("GameObject を作成しました");
    }

    void HierarchyPanel::DuplicateSelected(EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return;
        if (context.Selection().Empty()) return;

        // 走査中に Scene が伸びるので、対象 ID を先に控える。
        const std::vector<ObjectID> targets = context.Selection().All();

        context.BeginEdit("GameObject を複製");
        std::vector<ObjectID> created_ids;
        for (const ObjectID id : targets)
        {
            GameObject* source = scene->FindGameObjectByID(id);
            if (source == nullptr) continue;

            GameObject* clone = Scene::Serialization::DuplicateGameObject(*scene, *source, true);
            if (clone != nullptr) created_ids.push_back(clone->ID());
        }

        if (created_ids.empty())
        {
            context.CancelEdit();
            return;
        }
        context.CommitEdit();

        context.Selection().Clear();
        for (const ObjectID id : created_ids) context.Selection().Select(id, true);
        context.SetStatus(std::to_string(created_ids.size()) + " 個を複製しました");
    }

    void HierarchyPanel::DestroySelected(EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return;
        if (context.Selection().Empty()) return;

        const std::vector<ObjectID> targets = context.Selection().All();

        context.BeginEdit("GameObject を削除");
        for (const ObjectID id : targets)
        {
            // 削除予約が立つだけ。実体は CommitEdit の中で破棄される。
            scene->DestroyGameObject(id);
        }
        context.CommitEdit();

        context.Selection().Clear();
        context.SetStatus(std::to_string(targets.size()) + " 個を削除しました");
    }
}
