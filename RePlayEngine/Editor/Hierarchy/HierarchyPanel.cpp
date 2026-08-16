// Hierarchy Panel のうち、検索・選択・Drag & Drop を含むツリー描画だけを持つ。
//
//   HierarchyPanel.cpp          ... Hierarchy tree の描画と操作（このファイル）
//   HierarchyPanelCommands.cpp  ... 作成・複製・削除 command

#include "HierarchyPanel.h"

#include "../Core/EditorContext.h"
#include "../../Assets/AssetDatabase.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Object/Registry/ComponentRegistry.h"
#include "../../Components/Rendering/MeshRendererComponent.h"
#include "../../Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../Components/Landscape/LandscapeComponent.h"
#include "../../Components/Landscape/LandscapeRendererComponent.h"
#include "../../Components/Landscape/LandscapeColliderComponent.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
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

        std::string LowerAscii(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
            return text;
        }

        void CollectPreorder(GameObject& object, std::vector<GameObject*>& out, int depth = 0)
        {
            if (depth > maximum_depth || object.PendingDestroy()) return;
            out.push_back(&object);
            for (GameObject* child : object.Children())
            {
                if (child != nullptr) CollectPreorder(*child, out, depth + 1);
            }
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

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##HierarchySearch", "Search GameObjects / Components...",
            search_buffer_, search_buffer_size);
        ImGui::Separator();

        if (editable)
        {
            if (ImGui::Button("+ 作成"))
            {
                ImGui::OpenPopup("HierarchyCreatePopup");
            }
            if (ImGui::BeginPopup("HierarchyCreatePopup"))
            {
                DrawCreateMenu(context, nullptr);
                ImGui::EndPopup();
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
                    pending_reparent_parent_ = ObjectID::Invalid();
                    pending_drop_placement_ = DropPlacement::Root;
                }
            }
            ImGui::EndDragDropTarget();
        }

        // ツリー走査が終わってから親子/兄弟順を変更する。
        if (pending_reparent_child_.Valid() && editable)
        {
            GameObject* child = scene->FindGameObjectByID(pending_reparent_child_);
            GameObject* target = scene->FindGameObjectByID(pending_reparent_parent_);
            if (child != nullptr)
            {
                context.BeginEdit("Hierarchy の並びを変更");
                bool changed = false;
                if (pending_drop_placement_ == DropPlacement::Root)
                {
                    changed = child->SetParent(nullptr, true);
                }
                else if (target != nullptr && target != child)
                {
                    if (pending_drop_placement_ == DropPlacement::Child)
                    {
                        changed = child->SetParent(target, true);
                    }
                    else
                    {
                        GameObject* target_parent = target->Parent();
                        if (child->SetParent(target_parent, true))
                        {
                            const std::size_t target_index = target->SiblingIndex();
                            const std::size_t desired = target_index +
                                (pending_drop_placement_ == DropPlacement::After ? 1u : 0u);
                            changed = child->SetSiblingIndex(desired);
                        }
                    }
                }

                if (changed)
                {
                    context.CommitEdit();
                    context.SetStatus("Hierarchy の親子/兄弟順を変更しました");
                }
                else
                {
                    context.CancelEdit();
                    context.SetStatus("その位置へは移動できません（循環階層を含む）");
                }
            }
            pending_reparent_child_ = ObjectID::Invalid();
            pending_reparent_parent_ = ObjectID::Invalid();
            pending_drop_placement_ = DropPlacement::Child;
        }
    }

    namespace
    {
        bool HierarchyHasSiblingNameDuplicate(const Scene::Scene& scene,
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

    void HierarchyPanel::DrawNode(EditorContext& context, GameObject& object, int depth)
    {
        if (depth > maximum_depth) return;
        if (!NodeMatchesFilter(object)) return;

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
                    if (Scene::Scene* scene = context.GetScene();
                        scene != nullptr && HierarchyHasSiblingNameDuplicate(*scene, object, object.Name()))
                    {
                        context.SetStatus("同じ階層に同名 GameObject があります（参照は ObjectID で維持）");
                    }
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
        if (search_buffer_[0] != '\0') flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;
        if (!has_children) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        // 無効な GameObject は薄く表示する。
        const bool dimmed = !object.ActiveInHierarchy();
        const Assets::AssetDatabase* database = context.GetAssetDatabase();
        const bool missing_prefab = object.IsPrefabInstance() &&
            (database == nullptr || database->FindByGuid(object.PrefabSourceGUID()) == nullptr);
        const bool prefab = object.IsPrefabInstance();
        if (dimmed) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        else if (missing_prefab) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.30f, 0.25f, 1.0f));
        else if (prefab) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.65f, 1.0f, 1.0f));

        std::string node_label;
        if (object.IsPrefabRoot()) node_label += "[Prefab] ";
        else if (object.IsPrefabInstance()) node_label += "[P] ";
        if (context.GetScene() != nullptr &&
            context.GetScene()->Services().ControlledObject() == object.ID()) node_label += "[Controlled] ";
        node_label += object.Name();
        if (missing_prefab) node_label += " [Missing]";

        const bool opened = ImGui::TreeNodeEx("##Node", flags, "%s", node_label.c_str());

        if (dimmed || missing_prefab || prefab) ImGui::PopStyleColor();

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl)
            {
                context.Selection().Toggle(object.ID());
                selection_anchor_ = object.ID();
            }
            else if (io.KeyShift && selection_anchor_.Valid())
            {
                Scene::Scene* scene = context.GetScene();
                std::vector<GameObject*> ordered;
                if (scene != nullptr)
                {
                    for (GameObject* root : scene->RootGameObjects())
                    {
                        if (root != nullptr) CollectPreorder(*root, ordered);
                    }
                }

                auto anchor = std::find_if(ordered.begin(), ordered.end(), [this](const GameObject* item)
                {
                    return item != nullptr && item->ID() == selection_anchor_;
                });
                auto current = std::find(ordered.begin(), ordered.end(), &object);
                if (anchor != ordered.end() && current != ordered.end())
                {
                    if (anchor > current) std::swap(anchor, current);
                    context.Selection().Clear();
                    for (auto it = anchor; it <= current; ++it)
                    {
                        if (*it != nullptr) context.Selection().Select((*it)->ID(), true);
                    }
                }
                else
                {
                    context.Selection().Select(object.ID(), true);
                }
            }
            else
            {
                context.Selection().Select(object.ID(), false);
                selection_anchor_ = object.ID();
            }
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

    bool HierarchyPanel::NodeMatchesFilter(const GameObject& object) const
    {
        if (search_buffer_[0] == '\0') return true;

        const std::string needle = LowerAscii(search_buffer_);
        if (LowerAscii(object.Name()).find(needle) != std::string::npos) return true;

        for (std::size_t index = 0; index < object.ComponentCount(); ++index)
        {
            const Core::Component* component = object.ComponentAt(index);
            if (component == nullptr || component->PendingDestroy()) continue;
            const Core::ComponentTypeInfo* info = Core::ComponentRegistry::Find(component->TypeID());
            const std::string label = info != nullptr ? info->DisplayName() : component->TypeName();
            if (LowerAscii(label).find(needle) != std::string::npos) return true;
        }

        for (const GameObject* child : object.Children())
        {
            if (child != nullptr && !child->PendingDestroy() && NodeMatchesFilter(*child)) return true;
        }
        return false;
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

        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        if (ImGui::BeginDragDropTarget())
        {
            const float height = (std::max)(1.0f, item_max.y - item_min.y);
            const float local_y = ImGui::GetIO().MousePos.y - item_min.y;
            DropPlacement placement = DropPlacement::Child;
            if (local_y < height * 0.25f) placement = DropPlacement::Before;
            else if (local_y > height * 0.75f) placement = DropPlacement::After;

            // before / after は挿入位置を線で明示する。中央は通常の child drop。
            if (placement != DropPlacement::Child)
            {
                const float y = placement == DropPlacement::Before ? item_min.y : item_max.y;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(item_min.x, y), ImVec2(item_max.x, y),
                    IM_COL32(255, 205, 70, 255), 2.0f);
            }

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_drop_type))
            {
                if (payload->DataSize == sizeof(ObjectID::ValueType))
                {
                    ObjectID::ValueType raw = 0;
                    std::memcpy(&raw, payload->Data, sizeof(raw));
                    pending_reparent_child_ = ObjectID(raw);
                    pending_reparent_parent_ = object.ID();
                    pending_drop_placement_ = placement;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void HierarchyPanel::DrawContextMenu(EditorContext& context, GameObject* object)
    {
        const bool editable = context.CanEdit();

        if (editable)
        {
            if (ImGui::BeginMenu(object != nullptr ? "子を作成" : "作成"))
            {
                DrawCreateMenu(context, object);
                ImGui::EndMenu();
            }
        }
        else
        {
            ImGui::MenuItem("作成", nullptr, false, false);
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
            pending_reparent_parent_ = ObjectID::Invalid();
            pending_drop_placement_ = DropPlacement::Root;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("削除", "Del", false, editable)) DestroySelected(context);
    }

    void HierarchyPanel::BeginRenameSelection(EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return;
        GameObject* object = context.Selection().ResolvePrimary(*scene);
        if (object == nullptr || object->PendingDestroy()) return;
        renaming_ = object->ID();
        CopyToBuffer(rename_buffer_, rename_buffer_size, object->Name());
        context.SetStatus("F2: GameObject 名を変更");
    }

}
