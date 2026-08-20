// UI workspace の責務を 4 つのファイルへ分けている:
//   framework_ui_workspace.cpp          … UI 階層・UI Inspector と共通の作成導線（このファイル）
//   framework_ui_workspace_preview.cpp  … Canvas プレビューの描画・選択
//   framework_ui_workspace_overlay.cpp  … Scene View の UI overlay・選択・ドラッグ
//   framework_ui_workspaceInternal.h    … 分割後の UI helper 共通部
//
// BeginDisabledCompat / EndDisabledCompat は従来どおりこのファイルに残す。
#include "framework.h"

#include "../../RePlayEngine/Components/UI/CanvasComponent.h"
#include "../../RePlayEngine/Components/UI/RectTransformComponent.h"
#include "../../RePlayEngine/Components/UI/UIImageComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/UI/UIButtonComponent.h"
#include "../../RePlayEngine/Components/UI/UIMaskComponent.h"
#include "../../RePlayEngine/Components/UI/UIPuppetDeformComponent.h"
#include "../../RePlayEngine/Components/UI/UIShapeComponent.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../RePlayEngine/UI/UILayout.h"

// PushItemFlag / ImGuiItemFlags_Disabled を使うため。
// 同梱の ImGui は 1.80 WIP で BeginDisabled / EndDisabled がまだ無い。
// 既存の InspectorPanel.cpp / PropertyDrawer.cpp と同じ取り込み方に合わせる。
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "framework_ui_workspaceInternal.h"

    namespace
    {
        using namespace framework_ui_workspace_detail;
    void BeginDisabledCompat()
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }

    void EndDisabledCompat()
    {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    using ReplayEngine::Components::CanvasComponent;
    using ReplayEngine::Components::RectTransformComponent;
    using ReplayEngine::Components::UIImageComponent;
    using ReplayEngine::Components::UITextComponent;
    using ReplayEngine::Components::UIButtonComponent;
    using ReplayEngine::Components::UIMaskComponent;
    using ReplayEngine::Components::UIPuppetDeformComponent;
    using ReplayEngine::Components::UIShapeComponent;
    namespace Assets = ReplayEngine::Assets;
    namespace Core = ReplayEngine::Core;
    namespace Scene = ReplayEngine::Scene;

    enum class UIElementKind
    {
        Canvas,
        Image,
        Text,
        Button,
        Mask,
    };

    constexpr const char* ui_hierarchy_drag_type = "REPLAY_GAMEOBJECT";
    enum class UIHierarchyDropPlacement : int { Child = 0, Before = 1, After = 2, Root = 3 };
    struct UIHierarchyDropRequest final
    {
        Core::ObjectID child;
        Core::ObjectID target;
        UIHierarchyDropPlacement placement = UIHierarchyDropPlacement::Child;
    };
    UIHierarchyDropRequest ui_hierarchy_drop_request;


    std::string UniqueUIObjectName(const Scene::Scene& scene, const Core::GameObject* parent,
        const std::string& desired)
    {
        const auto exists = [&](const std::string& candidate)
        {
            for (std::size_t i = 0; i < scene.GameObjectCount(); ++i)
            {
                const Core::GameObject* object = scene.GameObjectAt(i);
                if (object != nullptr && !object->PendingDestroy() &&
                    object->Parent() == parent && object->Name() == candidate) return true;
            }
            return false;
        };
        if (!exists(desired)) return desired;
        for (int suffix = 1; suffix < 10000; ++suffix)
        {
            const std::string candidate = desired + " (" + std::to_string(suffix) + ")";
            if (!exists(candidate)) return candidate;
        }
        return desired;
    }

    Core::GameObject* FindFirstCanvas(Scene::Scene& scene)
    {
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            Core::GameObject* object = scene.GameObjectAt(index);
            if (object != nullptr && !object->PendingDestroy() &&
                object->GetComponent<CanvasComponent>() != nullptr)
                return object;
        }
        return nullptr;
    }


    Core::GameObject* SelectedUIParent(ReplayEngine::Editor::EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr) return nullptr;
        Core::GameObject* selected =
            context.Selection().ResolvePrimary(*scene);
        if (selected != nullptr && ContainsUI(*selected)) return selected;
        return FindFirstCanvas(*scene);
    }

    Core::GameObject* CreateCanvasObject(Scene::Scene& scene)
    {
        Core::GameObject* canvas = scene.CreateGameObject(UniqueUIObjectName(scene, nullptr, "Canvas"));
        if (canvas == nullptr) return nullptr;
        canvas->AddComponent<CanvasComponent>();
        RectTransformComponent* rect = canvas->GetComponent<RectTransformComponent>();
        if (rect != nullptr)
        {
            rect->anchor_min = { 0.0f, 0.0f };
            rect->anchor_max = { 1.0f, 1.0f };
            rect->anchored_position = { 0.0f, 0.0f };
            rect->size_delta = { 0.0f, 0.0f };
            rect->pivot = { 0.5f, 0.5f };
        }
        return canvas;
    }

    Core::GameObject* CreateUIElement(ReplayEngine::Editor::EditorContext& context,
        UIElementKind kind)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return nullptr;

        context.BeginEdit("UI 要素を作成");
        Core::GameObject* parent = nullptr;
        Core::GameObject* created = nullptr;

        if (kind == UIElementKind::Canvas)
        {
            created = CreateCanvasObject(*scene);
        }
        else
        {
            parent = SelectedUIParent(context);
            if (parent == nullptr) parent = CreateCanvasObject(*scene);
            const char* name = kind == UIElementKind::Text ? "Text" :
                kind == UIElementKind::Button ? "Button" :
                kind == UIElementKind::Mask ? "Mask" : "Image";
            created = scene->CreateGameObject(UniqueUIObjectName(*scene, parent, name));
            if (created != nullptr)
            {
                created->SetParent(parent, false);
                RectTransformComponent* rect = created->AddComponent<RectTransformComponent>();
                if (rect != nullptr)
                {
                    rect->size_delta = kind == UIElementKind::Button
                        ? DirectX::XMFLOAT2{ 180.0f, 52.0f }
                        : DirectX::XMFLOAT2{ 160.0f, 80.0f };
                }

                if (kind == UIElementKind::Image)
                {
                    created->AddComponent<UIImageComponent>();
                }
                else if (kind == UIElementKind::Text)
                {
                    UITextComponent* text = created->AddComponent<UITextComponent>();
                    if (text != nullptr) text->text = "Text";
                }
                else if (kind == UIElementKind::Button)
                {
                    created->AddComponent<UIImageComponent>();
                    created->AddComponent<UIButtonComponent>();

                    Core::GameObject* label = scene->CreateGameObject(UniqueUIObjectName(*scene, created, "Button Text"));
                    if (label != nullptr)
                    {
                        label->SetParent(created, false);
                        RectTransformComponent* label_rect =
                            label->AddComponent<RectTransformComponent>();
                        if (label_rect != nullptr)
                        {
                            label_rect->anchor_min = { 0.0f, 0.0f };
                            label_rect->anchor_max = { 1.0f, 1.0f };
                            label_rect->size_delta = { 0.0f, 0.0f };
                        }
                        UITextComponent* text = label->AddComponent<UITextComponent>();
                        if (text != nullptr)
                        {
                            text->text = "Button";
                            text->font_size = 24.0f;
                        }
                    }
                }
                else if (kind == UIElementKind::Mask)
                {
                    UIImageComponent* image = created->AddComponent<UIImageComponent>();
                    if (image != nullptr)
                        image->color = { 0.2f, 0.45f, 0.8f, 0.18f };
                    created->AddComponent<UIMaskComponent>();
                }
            }
        }

        if (created != nullptr)
            context.Selection().Select(created->ID(), false);
        context.CommitEdit();
        return created;
    }


    // selection_changed は「この呼び出しの中で GameObject が選ばれたか」を返す。
    //
    // editor_selection は framework の入れ子 enum で、selected_editor_object も
    // framework のメンバ。この関数はフリー関数なのでどちらにも触れない。
    // ここで直接代入するとコンパイルが通らないため、結果だけを呼び出し元へ返し、
    // framework 側で選択種別を切り替える。
    void DrawUINode(ReplayEngine::Editor::EditorContext& context,
        Core::GameObject& object, bool& selection_changed)
    {
        if (!ContainsUI(object)) return;

        const bool selected = context.Selection().IsSelected(object.ID());
        bool has_ui_child = false;
        for (const Core::GameObject* child : object.Children())
        {
            if (child != nullptr && ContainsUI(*child))
            {
                has_ui_child = true;
                break;
            }
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;
        if (!has_ui_child) flags |= ImGuiTreeNodeFlags_Leaf;

        std::string label = object.Name() + "##UI" + object.ID().ToString();
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            context.Selection().Select(object.ID(), false);
            selection_changed = true;
        }

        if (context.CanEdit() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            const Core::ObjectID::ValueType raw = object.ID().Value();
            ImGui::SetDragDropPayload(ui_hierarchy_drag_type, &raw, sizeof(raw));
            ImGui::TextUnformatted(object.Name().c_str());
            ImGui::EndDragDropSource();
        }
        if (context.CanEdit())
        {
            const ImVec2 item_min = ImGui::GetItemRectMin();
            const ImVec2 item_max = ImGui::GetItemRectMax();
            if (ImGui::BeginDragDropTarget())
            {
                const float height = (std::max)(1.0f, item_max.y - item_min.y);
                const float local_y = ImGui::GetIO().MousePos.y - item_min.y;
                UIHierarchyDropPlacement placement = UIHierarchyDropPlacement::Child;
                // 並べ替えの帯を広く取る。理由は HierarchyPanel.cpp と同じで、
                // 行の高さが 20px 前後だと 25% では 5px しかなく、
                // 並べ替えたいだけでも中央に当たって子にされてしまう。
                constexpr float reorder_band = 0.35f;
                if (local_y < height * reorder_band)
                    placement = UIHierarchyDropPlacement::Before;
                else if (local_y > height * (1.0f - reorder_band))
                    placement = UIHierarchyDropPlacement::After;
                if (placement != UIHierarchyDropPlacement::Child)
                {
                    const float y = placement == UIHierarchyDropPlacement::Before
                        ? item_min.y : item_max.y;
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(item_min.x, y),
                        ImVec2(item_max.x, y), IM_COL32(255, 205, 70, 255), 2.0f);
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui_hierarchy_drag_type))
                {
                    if (payload->DataSize == sizeof(Core::ObjectID::ValueType))
                    {
                        Core::ObjectID::ValueType raw = 0;
                        std::memcpy(&raw, payload->Data, sizeof(raw));
                        ui_hierarchy_drop_request.child = Core::ObjectID(raw);
                        ui_hierarchy_drop_request.target = object.ID();
                        ui_hierarchy_drop_request.placement = placement;
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        if (ImGui::BeginPopupContextItem())
        {
            if (!context.Selection().IsSelected(object.ID()))
                context.Selection().Select(object.ID(), false);
            selection_changed = true;
            if (ImGui::MenuItem("Image を追加")) CreateUIElement(context, UIElementKind::Image);
            if (ImGui::MenuItem("Text を追加")) CreateUIElement(context, UIElementKind::Text);
            if (ImGui::MenuItem("Button を追加")) CreateUIElement(context, UIElementKind::Button);
            if (ImGui::MenuItem("Mask を追加")) CreateUIElement(context, UIElementKind::Mask);
            ImGui::Separator();
            if (ImGui::MenuItem("シーン直下へ移動", nullptr, false,
                context.CanEdit() && object.Parent() != nullptr))
            {
                ui_hierarchy_drop_request.child = object.ID();
                ui_hierarchy_drop_request.target = Core::ObjectID::Invalid();
                ui_hierarchy_drop_request.placement = UIHierarchyDropPlacement::Root;
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            const std::vector<Core::GameObject*> children = object.Children();
            for (Core::GameObject* child : children)
            {
                if (child != nullptr) DrawUINode(context, *child, selection_changed);
            }
            ImGui::TreePop();
        }
    }

}

void framework::draw_ui_hierarchy()
{
    if (!show_ui_hierarchy_panel) return;
    if (!ImGui::Begin("UI 階層", &show_ui_hierarchy_panel))
    {
        ImGui::End();
        return;
    }

    Scene::Scene* scene = object_editor_context.GetScene();
    const bool can_edit = object_editor_context.CanEdit();
    if (!can_edit) BeginDisabledCompat();
    if (ImGui::Button("Canvas")) CreateUIElement(object_editor_context, UIElementKind::Canvas);
    ImGui::SameLine();
    if (ImGui::Button("Image")) CreateUIElement(object_editor_context, UIElementKind::Image);
    ImGui::SameLine();
    if (ImGui::Button("Text")) CreateUIElement(object_editor_context, UIElementKind::Text);
    ImGui::SameLine();
    if (ImGui::Button("Button")) CreateUIElement(object_editor_context, UIElementKind::Button);
    ImGui::SameLine();
    if (ImGui::Button("Mask")) CreateUIElement(object_editor_context, UIElementKind::Mask);
    if (!can_edit) EndDisabledCompat();

    ImGui::Separator();
    if (scene == nullptr)
    {
        ImGui::TextDisabled("Scene がありません");
        ImGui::End();
        return;
    }

    bool any_canvas = false;
    bool ui_selection_changed = false;
    for (Core::GameObject* root : scene->RootGameObjects())
    {
        if (root == nullptr || !ContainsUI(*root)) continue;
        any_canvas = true;
        DrawUINode(object_editor_context, *root, ui_selection_changed);
    }
    // UI 階層で選んだものも Delete の対象にする。
    // framework_class.h の Delete 処理が selected_editor_object を見ているため。
    if (ui_selection_changed) selected_editor_object = editor_selection::game_object;
    if (!any_canvas)
        ImGui::TextDisabled("Canvas はまだありません");

    ImGui::Separator();
    ImGui::TextDisabled("ここへドロップ: シーン直下へ移動");
    if (can_edit && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ui_hierarchy_drag_type))
        {
            if (payload->DataSize == sizeof(Core::ObjectID::ValueType))
            {
                Core::ObjectID::ValueType raw = 0;
                std::memcpy(&raw, payload->Data, sizeof(raw));
                ui_hierarchy_drop_request.child = Core::ObjectID(raw);
                ui_hierarchy_drop_request.target = Core::ObjectID::Invalid();
                ui_hierarchy_drop_request.placement = UIHierarchyDropPlacement::Root;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // UI tree の走査中には構造を変えず、描画完了後に 1 transaction で反映する。
    if (can_edit && ui_hierarchy_drop_request.child.Valid())
    {
        Core::GameObject* child = scene->FindGameObjectByID(ui_hierarchy_drop_request.child);
        Core::GameObject* target = scene->FindGameObjectByID(ui_hierarchy_drop_request.target);
        if (child != nullptr)
        {
            object_editor_context.BeginEdit("UI Hierarchy の並びを変更");
            bool changed = false;
            if (ui_hierarchy_drop_request.placement == UIHierarchyDropPlacement::Root)
                changed = child->SetParent(nullptr, true);
            else if (target != nullptr && target != child)
            {
                if (ui_hierarchy_drop_request.placement == UIHierarchyDropPlacement::Child)
                    changed = child->SetParent(target, true);
                else
                {
                    if (child->SetParent(target->Parent(), true))
                    {
                        const std::size_t target_index = target->SiblingIndex();
                        const std::size_t desired = target_index +
                            (ui_hierarchy_drop_request.placement == UIHierarchyDropPlacement::After ? 1u : 0u);
                        changed = child->SetSiblingIndex(desired);
                    }
                }
            }
            if (changed) object_editor_context.CommitEdit();
            else object_editor_context.CancelEdit();
        }
        ui_hierarchy_drop_request = {};
    }

    ImGui::End();
}


void framework::ui_preview_resolution_size(int& width, int& height) const noexcept
{
    width = 1920;
    height = 1080;
    if (ui_preview_resolution_index == 1)
    {
        width = 1280;
        height = 720;
    }
    else if (ui_preview_resolution_index == 2)
    {
        width = 1080;
        height = 1920;
    }
    else if (ui_preview_resolution_index == 3)
    {
        width = (std::max)(1, ui_preview_custom_width);
        height = (std::max)(1, ui_preview_custom_height);
    }
}


void framework::draw_ui_inspector()
{
    if (!show_ui_inspector_panel) return;
    if (!ImGui::Begin("UI インスペクター", &show_ui_inspector_panel))
    {
        ImGui::End();
        return;
    }

    Scene::Scene* scene = object_editor_context.GetScene();
    Core::GameObject* selected = scene != nullptr
        ? object_editor_context.Selection().ResolvePrimary(*scene) : nullptr;
    if (selected == nullptr || !HasUIComponent(*selected))
    {
        ImGui::TextDisabled("UI 要素を選択してください");
        if (ImGui::Button("Canvas を作成"))
            CreateUIElement(object_editor_context, UIElementKind::Canvas);
        ImGui::End();
        return;
    }

    // Backspace/Delete は「いま直接編集している細部」を最優先する。
    // テキスト入力中は文字編集へ渡し、その次に Puppet Pin、Bezier Point の順。
    const bool ui_delete_pressed = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && object_editor_context.CanEdit() &&
        ImGui::IsKeyPressed(VK_BACK);
    if (ui_delete_pressed)
    {
        if (UIPuppetDeformComponent* puppet = selected->GetComponent<UIPuppetDeformComponent>();
            puppet != nullptr && ui_puppet_selected_pin >= 0 &&
            ui_puppet_selected_pin < puppet->PinCount())
        {
            const int index = ui_puppet_selected_pin;
            const std::size_t i = static_cast<std::size_t>(index);
            object_editor_context.BeginEdit("Puppet Pinを削除");
            puppet->pin_positions.erase(puppet->pin_positions.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < puppet->pin_bind_positions.size())
                puppet->pin_bind_positions.erase(puppet->pin_bind_positions.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < puppet->pin_radii.size())
                puppet->pin_radii.erase(puppet->pin_radii.begin() + static_cast<std::ptrdiff_t>(i));
            puppet->OnPropertyChanged("pin_positions");
            ui_puppet_selected_pin = puppet->PinCount() > 0
                ? (std::min)(index, puppet->PinCount() - 1) : -1;
            object_editor_context.CommitEdit();
        }
        else if (UIShapeComponent* shape = selected->GetComponent<UIShapeComponent>();
            shape != nullptr && shape->shape == UIShapeComponent::CustomBezierPath &&
            ui_shape_selected_point >= 0 &&
            ui_shape_selected_point < static_cast<int>(shape->path_points.size()))
        {
            const int index = ui_shape_selected_point;
            const std::size_t i = static_cast<std::size_t>(index);
            object_editor_context.BeginEdit("Bezier Pointを削除");
            shape->path_points.erase(shape->path_points.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < shape->path_in_handles.size())
                shape->path_in_handles.erase(shape->path_in_handles.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < shape->path_out_handles.size())
                shape->path_out_handles.erase(shape->path_out_handles.begin() + static_cast<std::ptrdiff_t>(i));
            shape->OnPropertyChanged("path_points");
            ui_shape_selected_point = shape->path_points.empty() ? -1 :
                (std::min)(index, static_cast<int>(shape->path_points.size()) - 1);
            object_editor_context.CommitEdit();
        }
    }

    if (RectTransformComponent* rect = selected->GetComponent<RectTransformComponent>())
    {
        ImGui::TextDisabled("アンカー");
        const auto preset = [&](const char* label,
            DirectX::XMFLOAT2 min_anchor, DirectX::XMFLOAT2 max_anchor,
            DirectX::XMFLOAT2 pivot)
        {
            if (ImGui::Button(label) && object_editor_context.CanEdit())
            {
                object_editor_context.BeginEdit("アンカーを変更");
                rect->anchor_min = min_anchor;
                rect->anchor_max = max_anchor;
                rect->pivot = pivot;
                object_editor_context.CommitEdit();
            }
        };
        preset("左上", { 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 1.0f });
        ImGui::SameLine();
        preset("中央", { 0.5f, 0.5f }, { 0.5f, 0.5f }, { 0.5f, 0.5f });
        ImGui::SameLine();
        preset("全体", { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.5f, 0.5f });
        ImGui::Separator();
    }

    if (UIPuppetDeformComponent* puppet = selected->GetComponent<UIPuppetDeformComponent>())
    {
        ImGui::TextDisabled("Puppet Deform 編集");
        ImGui::Text("Pins: %d", puppet->PinCount());
        if (ImGui::Button("Pin を追加") && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("Puppet Pinを追加");
            const int old_count = puppet->PinCount();
            puppet->SetPinCount(old_count + 1);
            if (puppet->PinCount() > old_count)
            {
                const float offset = static_cast<float>(old_count % 5) * 0.035f;
                const DirectX::XMFLOAT2 position{ 0.5f + offset, 0.5f + offset };
                puppet->pin_positions[static_cast<std::size_t>(old_count)] = position;
                puppet->pin_bind_positions[static_cast<std::size_t>(old_count)] = position;
                ui_puppet_selected_pin = old_count;
            }
            object_editor_context.CommitEdit();
        }
        ImGui::SameLine();
        const bool can_remove_pin = puppet->PinCount() > 0 && object_editor_context.CanEdit();
        if (!can_remove_pin) BeginDisabledCompat();
        if (ImGui::Button("選択 Pin を削除") && can_remove_pin)
        {
            int index = ui_puppet_selected_pin;
            if (index < 0 || index >= puppet->PinCount()) index = puppet->PinCount() - 1;
            object_editor_context.BeginEdit("Puppet Pinを削除");
            const std::size_t i = static_cast<std::size_t>(index);
            puppet->pin_positions.erase(puppet->pin_positions.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < puppet->pin_bind_positions.size())
                puppet->pin_bind_positions.erase(puppet->pin_bind_positions.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < puppet->pin_radii.size())
                puppet->pin_radii.erase(puppet->pin_radii.begin() + static_cast<std::ptrdiff_t>(i));
            puppet->OnPropertyChanged("pin_positions");
            ui_puppet_selected_pin = puppet->PinCount() > 0
                ? (std::min)(index, puppet->PinCount() - 1) : -1;
            object_editor_context.CommitEdit();
        }
        if (!can_remove_pin) EndDisabledCompat();

        if (puppet->PinCount() > 0)
        {
            int selected_pin = ui_puppet_selected_pin;
            if (selected_pin < 0 || selected_pin >= puppet->PinCount()) selected_pin = 0;
            if (ImGui::SliderInt("選択 Pin", &selected_pin, 0, puppet->PinCount() - 1))
                ui_puppet_selected_pin = selected_pin;
        }
        if (puppet->PinCount() > 0)
        {
            int selected_pin = ui_puppet_selected_pin;
            if (selected_pin < 0 || selected_pin >= puppet->PinCount()) selected_pin = 0;
            const std::size_t pin_index = static_cast<std::size_t>(selected_pin);
            if (pin_index < puppet->pin_radii.size())
            {
                float radius = puppet->pin_radii[pin_index];
                const bool radius_changed = ImGui::DragFloat(
                    "選択 Pin 半径", &radius, 0.005f, 0.001f, 4.0f, "%.3f");
                if (radius_changed && object_editor_context.CanEdit())
                {
                    if (!ui_puppet_radius_editing)
                    {
                        object_editor_context.BeginEdit("Puppet Pin半径を変更");
                        ui_puppet_radius_editing = true;
                    }
                    puppet->pin_radii[pin_index] = (std::max)(0.001f, radius);
                }
                if (ImGui::IsItemDeactivated() && ui_puppet_radius_editing)
                {
                    object_editor_context.CommitEdit();
                    ui_puppet_radius_editing = false;
                }
            }
        }
        if (ImGui::Button("現在形状を Bind Pose にする") && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("Puppet Bind Poseを更新");
            puppet->pin_bind_positions = puppet->pin_positions;
            puppet->OnPropertyChanged("pin_bind_positions");
            object_editor_context.CommitEdit();
        }
        ImGui::SameLine();
        if (ImGui::Button("Bind Pose に戻す") && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("PuppetをBind Poseへ戻す");
            puppet->pin_positions = puppet->pin_bind_positions;
            puppet->OnPropertyChanged("pin_positions");
            object_editor_context.CommitEdit();
        }
        ImGui::Separator();
    }

    if (UIShapeComponent* shape = selected->GetComponent<UIShapeComponent>();
        shape != nullptr && shape->shape == UIShapeComponent::CustomBezierPath)
    {
        ImGui::TextDisabled("Custom Bezier Path 編集");
        ImGui::Text("Points: %d", static_cast<int>(shape->path_points.size()));
        if (ImGui::Button("Point を追加") && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("Bezier Pointを追加");
            const int old_count = static_cast<int>(shape->path_points.size());
            shape->SetPathPointCount(old_count + 1);
            if (static_cast<int>(shape->path_points.size()) > old_count)
            {
                DirectX::XMFLOAT2 position{ 0.5f, 0.5f };
                if (old_count > 0)
                {
                    const DirectX::XMFLOAT2 previous = shape->path_points[static_cast<std::size_t>(old_count - 1)];
                    position = { (std::min)(1.0f, previous.x + 0.1f), previous.y };
                }
                shape->path_points[static_cast<std::size_t>(old_count)] = position;
                ui_shape_selected_point = old_count;
                shape->OnPropertyChanged("path_points");
            }
            object_editor_context.CommitEdit();
        }
        ImGui::SameLine();
        const bool can_insert_point = !shape->path_points.empty() && object_editor_context.CanEdit();
        if (!can_insert_point) BeginDisabledCompat();
        if (ImGui::Button("選択の後へ挿入") && can_insert_point)
        {
            int index = ui_shape_selected_point;
            if (index < 0 || index >= static_cast<int>(shape->path_points.size()))
                index = static_cast<int>(shape->path_points.size()) - 1;
            const std::size_t insert_at = static_cast<std::size_t>(index + 1);
            const std::size_t next_index = shape->path_closed
                ? insert_at % shape->path_points.size()
                : (std::min)(insert_at, shape->path_points.size() - 1);
            const DirectX::XMFLOAT2 a = shape->path_points[static_cast<std::size_t>(index)];
            const DirectX::XMFLOAT2 b = shape->path_points[next_index];
            const DirectX::XMFLOAT2 midpoint{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
            object_editor_context.BeginEdit("Bezier Pointを挿入");
            shape->path_points.insert(shape->path_points.begin() +
                static_cast<std::ptrdiff_t>(insert_at), midpoint);
            const std::size_t in_at = (std::min)(insert_at, shape->path_in_handles.size());
            const std::size_t out_at = (std::min)(insert_at, shape->path_out_handles.size());
            shape->path_in_handles.insert(shape->path_in_handles.begin() +
                static_cast<std::ptrdiff_t>(in_at), DirectX::XMFLOAT2{});
            shape->path_out_handles.insert(shape->path_out_handles.begin() +
                static_cast<std::ptrdiff_t>(out_at), DirectX::XMFLOAT2{});
            shape->OnPropertyChanged("path_points");
            ui_shape_selected_point = static_cast<int>(insert_at);
            object_editor_context.CommitEdit();
        }
        if (!can_insert_point) EndDisabledCompat();
        ImGui::SameLine();
        const bool can_remove_point = !shape->path_points.empty() && object_editor_context.CanEdit();
        if (!can_remove_point) BeginDisabledCompat();
        if (ImGui::Button("選択 Point を削除") && can_remove_point)
        {
            int index = ui_shape_selected_point;
            if (index < 0 || index >= static_cast<int>(shape->path_points.size()))
                index = static_cast<int>(shape->path_points.size()) - 1;
            object_editor_context.BeginEdit("Bezier Pointを削除");
            const std::size_t i = static_cast<std::size_t>(index);
            shape->path_points.erase(shape->path_points.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < shape->path_in_handles.size())
                shape->path_in_handles.erase(shape->path_in_handles.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < shape->path_out_handles.size())
                shape->path_out_handles.erase(shape->path_out_handles.begin() + static_cast<std::ptrdiff_t>(i));
            shape->OnPropertyChanged("path_points");
            ui_shape_selected_point = shape->path_points.empty() ? -1 :
                (std::min)(index, static_cast<int>(shape->path_points.size()) - 1);
            object_editor_context.CommitEdit();
        }
        if (!can_remove_point) EndDisabledCompat();
        if (!shape->path_points.empty())
        {
            int selected_point = ui_shape_selected_point;
            if (selected_point < 0 || selected_point >= static_cast<int>(shape->path_points.size()))
                selected_point = 0;
            if (ImGui::SliderInt("選択 Point", &selected_point, 0,
                static_cast<int>(shape->path_points.size()) - 1))
                ui_shape_selected_point = selected_point;
        }
        ImGui::Separator();
    }

    if (UIMaskComponent* mask = selected->GetComponent<UIMaskComponent>();
        mask != nullptr && (mask->mask_mode == UIMaskComponent::ObjectAlpha ||
            mask->mask_mode == UIMaskComponent::ObjectLuma))
    {
        ImGui::TextDisabled("Track Matte 編集");
        ImGui::Text("Primary + Extra: %d",
            1 + static_cast<int>(mask->matte_objects.size()));
        if (ImGui::Button("Matte を追加") && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("Track Matteを追加");
            mask->matte_objects.push_back({});
            mask->matte_operations.push_back(UIMaskComponent::MatteAdd);
            mask->OnPropertyChanged("matte_objects");
            ui_mask_selected_matte = static_cast<int>(mask->matte_objects.size()) - 1;
            object_editor_context.CommitEdit();
        }
        ImGui::SameLine();
        const bool can_remove_matte = !mask->matte_objects.empty() &&
            object_editor_context.CanEdit();
        if (!can_remove_matte) BeginDisabledCompat();
        if (ImGui::Button("選択 Extra Matte を削除") && can_remove_matte)
        {
            int index = ui_mask_selected_matte;
            if (index < 0 || index >= static_cast<int>(mask->matte_objects.size()))
                index = static_cast<int>(mask->matte_objects.size()) - 1;
            const std::size_t i = static_cast<std::size_t>(index);
            object_editor_context.BeginEdit("Track Matteを削除");
            mask->matte_objects.erase(mask->matte_objects.begin() +
                static_cast<std::ptrdiff_t>(i));
            if (i < mask->matte_operations.size())
                mask->matte_operations.erase(mask->matte_operations.begin() +
                    static_cast<std::ptrdiff_t>(i));
            mask->OnPropertyChanged("matte_objects");
            ui_mask_selected_matte = mask->matte_objects.empty() ? -1 :
                (std::min)(index, static_cast<int>(mask->matte_objects.size()) - 1);
            object_editor_context.CommitEdit();
        }
        if (!can_remove_matte) EndDisabledCompat();
        if (!mask->matte_objects.empty())
        {
            int selected_matte = ui_mask_selected_matte;
            if (selected_matte < 0 ||
                selected_matte >= static_cast<int>(mask->matte_objects.size()))
                selected_matte = 0;
            if (ImGui::SliderInt("選択 Extra Matte", &selected_matte, 0,
                static_cast<int>(mask->matte_objects.size()) - 1))
                ui_mask_selected_matte = selected_matte;
        }
        ImGui::Separator();
    }

    bool show_game_template_components =
        project_settings.ShowGameTemplateComponents();
    if (object_inspector_panel.DrawContents(object_editor_context,
        show_game_template_components))
    {
        project_settings.SetShowGameTemplateComponents(
            show_game_template_components);
        save_project_settings();
    }
    ImGui::Separator();
    // Motion Workspace への導線。UI 側の編集状態は変えず、
    // Motion Asset の作成と Workspace 切り替えだけを担当する。
    if (ImGui::Button("Motion を作成"))
    {
        if (!motion_editor_loaded)
            project_create_motion("UIMotion");
        set_editor_workspace(editor_workspace::motion);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("選択中の UI 要素を Motion Workspace で編集します。");

    ImGui::End();
}
