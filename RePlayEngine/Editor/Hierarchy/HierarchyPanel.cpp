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
#include "../../Components/Editor/FolderComponent.h"
#include "../../Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../Components/Landscape/LandscapeComponent.h"
#include "../../Components/Landscape/LandscapeRendererComponent.h"
#include "../../Components/Landscape/LandscapeColliderComponent.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"
#include "../ReorderableList.h"
#include "../Style/EditorStyle.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    using Core::GameObject;
    using Core::ObjectID;

    namespace
    {
        constexpr int maximum_depth = 64;
        struct HierarchyReorderScope final {};
        HierarchyReorderScope hierarchy_reorder_scope;

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

    }

    void HierarchyPanel::Draw(EditorContext& context)
    {
        PanelTabColorScope panel_tab_color("Scene");
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
            ImGui::SameLine();
            if (ImGui::Button("プレハブを置く"))
            {
                ObjectID parent = context.Selection().Primary();
                if (!parent.Valid() || scene->FindGameObjectByID(parent) == nullptr)
                    parent = ObjectID::Invalid();
                pending_asset_placement_request_ = {};
                pending_asset_placement_request_.parent = parent;
                pending_asset_placement_request_.browse_prefab = true;
            }
        }
        else
        {
            ImGui::TextDisabled("実行中は階層を編集できません");
        }
        if (toolbar_drawer_)
        {
            ImGui::SameLine();
            toolbar_drawer_();
        }
        ImGui::Separator();

        // 親を持たないものから再帰的に描く。
        // Search expansion must not overwrite the normal hierarchy's folded state.
        ImGui::PushID("CompactHierarchy");
        ImGui::PushID(search_buffer_[0] != '\0');
        visible_objects_.clear();
        const std::vector<GameObject*> roots = scene->RootGameObjects();
        for (GameObject* root : roots)
        {
            if (root == nullptr || root->PendingDestroy()) continue;
            DrawNode(context, *root, 0, roots);
        }
        ImGui::PopID();
        ImGui::PopID();

        // Shift selection includes only the rows actually visible in this view.
        if (pending_range_selection_.Valid())
        {
            auto anchor = std::find(visible_objects_.begin(), visible_objects_.end(), selection_anchor_);
            auto current = std::find(visible_objects_.begin(), visible_objects_.end(), pending_range_selection_);
            if (anchor != visible_objects_.end() && current != visible_objects_.end())
            {
                if (anchor > current) std::swap(anchor, current);
                context.Selection().Clear();
                for (auto it = anchor; it <= current; ++it)
                    context.Selection().Select(*it, true);
            }
            else
            {
                context.Selection().Select(pending_range_selection_, true);
            }
            pending_range_selection_ = ObjectID::Invalid();
        }

        if (const char* active_label = ActiveReorderLabel(); active_label != nullptr)
        {
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget],
                "移動中: %s", active_label);
        }

        ImVec2 empty_area = ImGui::GetContentRegionAvail();
        empty_area.x = (std::max)(1.0f, empty_area.x);
        empty_area.y = (std::max)(ImGui::GetFrameHeightWithSpacing(), empty_area.y);
        ImGui::InvisibleButton("##HierarchyEmptyArea", empty_area);

        // 何も無いところで右クリックしたときのメニュー。
        if (ImGui::BeginPopupContextItem("HierarchyBackground"))
        {
            DrawContextMenu(context, nullptr);
            ImGui::EndPopup();
        }

        // 空白へのドロップは並び替えを Scene 直下へ戻し、Asset を Scene 直下へ配置する。
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                "REPLAY_REORDERABLE_ITEM"))
            {
                if (payload->Data != nullptr &&
                    payload->DataSize == static_cast<int>(sizeof(ReorderPayload)) &&
                    payload->IsDelivery())
                {
                    ReorderPayload dragged{};
                    std::memcpy(&dragged, payload->Data, sizeof(dragged));
                    if (dragged.scope == reinterpret_cast<std::uintptr_t>(&hierarchy_reorder_scope) &&
                        dragged.item != 0)
                    {
                        pending_reparent_child_ = ObjectID(dragged.item);
                        pending_reparent_parent_ = ObjectID::Invalid();
                        pending_drop_placement_ = DropPlacement::Root;
                    }
                }
            }
            if (editable)
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                    "REPLAY_ASSET_GUID"))
                {
                    if (payload->Data != nullptr && payload->DataSize > 1 && payload->IsDelivery())
                    {
                        pending_asset_placement_request_ = {};
                        pending_asset_placement_request_.asset_guid.assign(
                            static_cast<const char*>(payload->Data),
                            static_cast<std::size_t>(payload->DataSize - 1));
                    }
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

    void HierarchyPanel::DrawNode(EditorContext& context, GameObject& object, int depth,
        const std::vector<GameObject*>& siblings)
    {
        if (depth > maximum_depth) return;
        if (!NodeMatchesFilter(object)) return;
        visible_objects_.push_back(object.ID());

        const bool editable = context.CanEdit();
        const bool selected = context.Selection().IsSelected(object.ID());
        const bool has_children = !object.Children().empty();
        const std::size_t sibling_index = object.SiblingIndex();
        const std::size_t sibling_count = siblings.size();
        const void* list_identity = object.Parent() != nullptr
            ? static_cast<const void*>(object.Parent()) : static_cast<const void*>(context.GetScene());
        const std::string item_id = "GameObject" + object.ID().ToString();

        // 名前変更中はテキスト入力へ差し替える。
        if (renaming_ == object.ID())
        {
            ImGui::PushID(item_id.c_str());
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

        // 無効な GameObject は薄く表示する。
        const bool dimmed = !object.ActiveInHierarchy();
        const ImVec4 dimmed_color(0.55f, 0.55f, 0.55f, 1.0f);
        const Assets::AssetDatabase* database = context.GetAssetDatabase();
        const bool missing_prefab = object.IsPrefabInstance() &&
            (database == nullptr || database->FindByGuid(object.PrefabSourceGUID()) == nullptr);
        const bool prefab = object.IsPrefabInstance();

        std::string node_label;
        if (object.IsPrefabRoot()) node_label += "[Prefab] ";
        else if (object.IsPrefabInstance()) node_label += "[P] ";
        if (context.GetScene() != nullptr &&
            context.GetScene()->Services().ControlledObject() == object.ID()) node_label += "[Controlled] ";
        node_label += object.Name();
        if (missing_prefab) node_label += " [Missing]";

        std::vector<EditorIconProvider::Icon> icons;
        std::string icon_type_names;
        const HierarchyIconDisplay icon_display = icon_display_ != nullptr
            ? *icon_display_ : HierarchyIconDisplay::Always;
        if (icon_provider_ != nullptr && icon_display != HierarchyIconDisplay::Hidden)
        {
            const bool controlled = context.GetScene() != nullptr &&
                context.GetScene()->Services().ControlledObject() == object.ID();
            const char* object_key = object.GetComponent<Components::FolderComponent>() != nullptr
                ? "object.folder" : missing_prefab ? "object.prefab_missing"
                : object.IsPrefabRoot() ? "object.prefab_root"
                : prefab ? "object.prefab_instance" : controlled ? "object.controlled"
                : dimmed ? "object.inactive" : "object.empty";
            if (const auto icon = icon_provider_->Resolve(object_key)) icons.push_back(icon);
            for (std::size_t index = 0; index < object.ComponentCount(); ++index)
            {
                const auto* component = object.ComponentAt(index);
                if (component == nullptr || component->PendingDestroy()) continue;
                const auto* info = Core::ComponentRegistry::Find(component->TypeID());
                if (info != nullptr && info->built_in) continue;
                if (!icon_type_names.empty()) icon_type_names += "\n";
                icon_type_names += info != nullptr ? info->type_name : component->TypeName();
                const auto icon = info != nullptr ? icon_provider_->ResolveComponent(*info)
                    : icon_provider_->Resolve(component->TypeName());
                if (icon) icons.push_back(icon);
            }
        }

        const bool dragging_this = IsReorderDragging(list_identity, sibling_index);
        const std::string visible_node_label = dragging_this
            ? (std::string("▲ ") + node_label + "  … 移動中") : node_label;
        const auto draw_header = [&](const char* header_title,
            ImGuiTreeNodeFlags header_flags)
        {
            (void)header_title;
            header_flags |= ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_OpenOnDoubleClick |
                ImGuiTreeNodeFlags_SpanAvailWidth |
                ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (!has_children)
                header_flags |= ImGuiTreeNodeFlags_Leaf |
                    ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (has_children && search_buffer_[0] != '\0')
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            if (dimmed || missing_prefab || prefab)
                ImGui::PushStyleColor(ImGuiCol_Text,
                    dimmed ? dimmed_color
                        : (missing_prefab ? ImVec4(1.0f, 0.30f, 0.25f, 1.0f)
                            : ImVec4(0.35f, 0.65f, 1.0f, 1.0f)));
            const bool open = EditorIconProvider::DrawHeader("##Node",
                visible_node_label.c_str(), header_flags, icons,
                icon_display == HierarchyIconDisplay::Hovered, dimmed ? dimmed_color.x : 1.0f);
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
                    pending_range_selection_ = object.ID();
                }
                else
                {
                    context.Selection().Select(object.ID(), false);
                    selection_anchor_ = object.ID();
                }
            }
            return open;
        };

        const auto draw_drop = [&](ReorderDropInfo& drop,
            const ImVec2& item_min, const ImVec2& item_max)
        {
            if (!editable || !drop.same_scope || drop.payload.item == 0 ||
                drop.payload.item == object.ID().Value()) return;
            const float height = (std::max)(1.0f, item_max.y - item_min.y);
            const float local_y = ImGui::GetIO().MousePos.y - item_min.y;
            constexpr float reorder_band = 0.35f;
            DropPlacement placement = DropPlacement::Child;
            if (local_y < height * reorder_band) placement = DropPlacement::Before;
            else if (local_y > height * (1.0f - reorder_band))
                placement = DropPlacement::After;
            drop.handled = true;
            if (placement != DropPlacement::Child)
            {
                const float line_y = placement == DropPlacement::Before
                    ? item_min.y : item_max.y;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(item_min.x, line_y), ImVec2(item_max.x, line_y),
                    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 4.0f);
            }
            if (drop.delivery && !pending_reparent_child_.Valid())
            {
                pending_reparent_child_ = ObjectID(drop.payload.item);
                pending_reparent_parent_ = object.ID();
                pending_drop_placement_ = placement;
            }
        };

        const ReorderableItemResult item = DrawReorderableItemEx(
            list_identity, item_id.c_str(), sibling_index, sibling_count,
            object.Name().c_str(), selected,
            false, editable, object.ID().Value(),
            &hierarchy_reorder_scope,
            draw_header,
            [&context, &object, this]
            {
                if (!context.Selection().IsSelected(object.ID()))
                    context.Selection().Select(object.ID(), false);
                DrawContextMenu(context, &object);
            }, draw_drop, true);

        if (editable && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("REPLAY_ASSET_GUID"))
            {
                if (payload->Data != nullptr && payload->DataSize > 1 && payload->IsDelivery())
                {
                    pending_asset_placement_request_ = {};
                    pending_asset_placement_request_.asset_guid.assign(
                        static_cast<const char*>(payload->Data),
                        static_cast<std::size_t>(payload->DataSize - 1));
                    pending_asset_placement_request_.parent = object.ID();
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (item.hovered && !icons.empty() && !icon_type_names.empty())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(icon_type_names.c_str());
            ImGui::EndTooltip();
        }

        if (item.request.Valid() && !pending_reparent_child_.Valid() &&
            item.request.destination < siblings.size())
        {
            pending_reparent_child_ = object.ID();
            pending_reparent_parent_ = siblings[item.request.destination] != nullptr
                ? siblings[item.request.destination]->ID() : ObjectID::Invalid();
            pending_drop_placement_ = item.request.source < item.request.destination
                ? DropPlacement::After : DropPlacement::Before;
        }

        if (item.opened && has_children)
        {
            // Push after the row helper has balanced its own ID/group scopes.
            ImGui::TreePush(item_id.c_str());
            // 走査中に子リストが変わらないよう控えを取る。
            const std::vector<GameObject*> children = object.Children();
            for (GameObject* child : children)
            {
                if (child == nullptr || child->PendingDestroy()) continue;
                DrawNode(context, *child, depth + 1, children);
            }
            ImGui::TreePop();
        }
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

    ObjectID HierarchyPanel::TakePrefabRequest()
    {
        const ObjectID request = pending_prefab_request_;
        pending_prefab_request_ = ObjectID::Invalid();
        return request;
    }

    AssetPlacementRequest HierarchyPanel::TakeAssetPlacementRequest()
    {
        AssetPlacementRequest request = std::move(pending_asset_placement_request_);
        pending_asset_placement_request_ = {};
        return request;
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
        if (ImGui::MenuItem("有効", nullptr, object->Enabled(), editable))
        {
            context.BeginEdit("GameObject の有効状態を変更");
            object->SetEnabled(!object->Enabled());
            context.CommitEdit();
        }
        if (ImGui::MenuItem("名前を変更", "F2", false, editable))
        {
            renaming_ = object->ID();
            CopyToBuffer(rename_buffer_, rename_buffer_size, object->Name());
        }
        if (ImGui::MenuItem("複製", "Ctrl+D", false, editable)) DuplicateSelected(context);
        if (ImGui::MenuItem("プレハブにする", nullptr, false, editable))
        {
            pending_prefab_request_ = object->ID();
        }
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
