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
#include "../../RePlayEngine/Components/UI/UIShapeImageComponent.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../RePlayEngine/UI/UILayout.h"

// PushItemFlag / ImGuiItemFlags_Disabled を使うため。
// 同梱の ImGui は 1.80 WIP で BeginDisabled / EndDisabled がまだ無い。
// 既存の InspectorPanel.cpp / PropertyDrawer.cpp と同じ取り込み方に合わせる。
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <limits>
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
    using ReplayEngine::Components::UIShapeImageComponent;
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

    enum class UIHierarchyMoveDirection : int
    {
        Up = -1,
        Down = 1,
    };

    struct UIPrimitivePreset final
    {
        const char* menu_name;
        const char* object_name;
        UIShapeComponent::Shape shape;
        DirectX::XMFLOAT2 size_delta;
        int sides = 5;
        float corner_radius = 0.0f;
        float polar_base_radius = 1.0f;
        float polar_amplitude = 0.0f;
        float polar_lobes = 5.0f;
        float stroke_width = 0.0f;
    };

    const std::array<UIPrimitivePreset, 7>& UIPrimitivePresets()
    {
        static const std::array<UIPrimitivePreset, 7> presets{{
            { "矩形", "Rectangle", UIShapeComponent::Rectangle,
                { 160.0f, 80.0f } },
            { "角丸矩形", "Rounded Rectangle", UIShapeComponent::Rectangle,
                { 160.0f, 80.0f }, 5, 18.0f },
            { "円", "Circle", UIShapeComponent::Circle,
                { 128.0f, 128.0f } },
            { "三角形", "Triangle", UIShapeComponent::Polygon,
                { 128.0f, 128.0f }, 3 },
            { "六角形", "Hexagon", UIShapeComponent::Polygon,
                { 128.0f, 128.0f }, 6 },
            { "星形", "Star", UIShapeComponent::PolarFormula,
                { 128.0f, 128.0f }, 5, 0.0f, 0.62f, 0.38f, 5.0f },
            { "線", "Line", UIShapeComponent::Line,
                { 220.0f, 16.0f }, 5, 0.0f, 1.0f, 0.0f, 5.0f, 4.0f },
        }};
        return presets;
    }

    const UIPrimitivePreset& CustomShapePreset()
    {
        static const UIPrimitivePreset preset{
            "自由図形", "Custom Shape", UIShapeComponent::CustomBezierPath,
            { 160.0f, 100.0f } };
        return preset;
    }

    constexpr const char* ui_hierarchy_drag_type = "REPLAY_GAMEOBJECT";
    enum class UIHierarchyDropPlacement : int { Child = 0, Before = 1, After = 2, Root = 3 };
    struct UIHierarchyDropRequest final
    {
        Core::ObjectID child;
        Core::ObjectID target;
        UIHierarchyDropPlacement placement = UIHierarchyDropPlacement::Child;
    };
    UIHierarchyDropRequest ui_hierarchy_drop_request;

    enum class UIOrderAction : int
    {
        Backward,
        Forward,
        Backmost,
        Frontmost,
    };

    struct UIOrderActionLabel final
    {
        const char* menu_name;
        const char* button_name;
        UIOrderAction action;
    };

    const std::array<UIOrderActionLabel, 4>& UIOrderActions()
    {
        static const std::array<UIOrderActionLabel, 4> actions{{
            { "最背面へ", "最背面", UIOrderAction::Backmost },
            { "背面へ", "背面", UIOrderAction::Backward },
            { "前面へ", "前面", UIOrderAction::Forward },
            { "最前面へ", "最前面", UIOrderAction::Frontmost },
        }};
        return actions;
    }

    // UI 階層の並び（children_）と描画順（sort_order）は別の軸として扱う。
    // Canvas だけは Scene 全体、Canvas 配下の UI は同じ親の兄弟だけが並び替え対象。
    int* UIOrderValue(Core::GameObject& object)
    {
        if (CanvasComponent* canvas = object.GetComponent<CanvasComponent>())
            return &canvas->sort_order;
        if (RectTransformComponent* rect = object.GetComponent<RectTransformComponent>())
            return &rect->sort_order;
        return nullptr;
    }

    std::string UIHierarchyLabel(Core::GameObject& object)
    {
        const int* order = UIOrderValue(object);
        const std::string order_text = order != nullptr
            ? std::to_string(*order) : "-";
        return "[" + order_text + "] " + object.Name() +
            "##UI" + object.ID().ToString();
    }

    enum class UIHierarchyFilterKind
    {
        All,
        Canvas,
        Image,
        Text,
        Button,
        Shape,
        Mask,
    };

    struct UIHierarchyFilterOption final
    {
        const char* label;
        UIHierarchyFilterKind kind;
    };

    struct UIHierarchyFilterState final
    {
        std::string search;
        UIHierarchyFilterKind kind = UIHierarchyFilterKind::All;
    };

    std::array<char, 96> ui_hierarchy_search_buffer{};
    UIHierarchyFilterKind ui_hierarchy_filter_kind = UIHierarchyFilterKind::All;

    const std::array<UIHierarchyFilterOption, 7>& UIHierarchyFilterOptions()
    {
        static const std::array<UIHierarchyFilterOption, 7> options{{
            { "全て", UIHierarchyFilterKind::All },
            { "Canvas", UIHierarchyFilterKind::Canvas },
            { "Image", UIHierarchyFilterKind::Image },
            { "Text", UIHierarchyFilterKind::Text },
            { "Button", UIHierarchyFilterKind::Button },
            { "図形", UIHierarchyFilterKind::Shape },
            { "Mask", UIHierarchyFilterKind::Mask },
        }};
        return options;
    }

    const char* UIHierarchyFilterLabel(UIHierarchyFilterKind kind)
    {
        for (const UIHierarchyFilterOption& option : UIHierarchyFilterOptions())
        {
            if (option.kind == kind) return option.label;
        }
        return "全て";
    }

    std::string LowerAscii(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });
        return text;
    }

    bool UIHierarchyMatchesKind(const Core::GameObject& object,
        UIHierarchyFilterKind kind)
    {
        switch (kind)
        {
        case UIHierarchyFilterKind::All: return true;
        case UIHierarchyFilterKind::Canvas:
            return object.GetComponent<CanvasComponent>() != nullptr;
        case UIHierarchyFilterKind::Image:
            return object.GetComponent<UIImageComponent>() != nullptr;
        case UIHierarchyFilterKind::Text:
            return object.GetComponent<UITextComponent>() != nullptr;
        case UIHierarchyFilterKind::Button:
            return object.GetComponent<UIButtonComponent>() != nullptr;
        case UIHierarchyFilterKind::Shape:
            return object.GetComponent<UIShapeComponent>() != nullptr ||
                object.GetComponent<UIShapeImageComponent>() != nullptr;
        case UIHierarchyFilterKind::Mask:
            return object.GetComponent<UIMaskComponent>() != nullptr;
        }
        return false;
    }

    bool UIHierarchyObjectMatchesFilter(const Core::GameObject& object,
        const UIHierarchyFilterState& filter)
    {
        const bool name_matches = filter.search.empty() ||
            LowerAscii(object.Name()).find(filter.search) != std::string::npos;
        return name_matches && UIHierarchyMatchesKind(object, filter.kind);
    }

    bool UIHierarchyNodeMatchesFilter(const Core::GameObject& object,
        const UIHierarchyFilterState& filter)
    {
        if (!ContainsUI(object)) return false;
        if (UIHierarchyObjectMatchesFilter(object, filter)) return true;

        for (const Core::GameObject* child : object.Children())
        {
            if (child != nullptr && UIHierarchyNodeMatchesFilter(*child, filter))
                return true;
        }
        return false;
    }

    bool UIHierarchyFilterActive(const UIHierarchyFilterState& filter)
    {
        return !filter.search.empty() || filter.kind != UIHierarchyFilterKind::All;
    }

    void DrawUIHierarchyFilterControls()
    {
        const bool has_search = ui_hierarchy_search_buffer[0] != '\0';
        ImGui::TextDisabled("フォルダ / フィルター");
        ImGui::SetNextItemWidth(has_search ? -30.0f : -1.0f);
        ImGui::InputTextWithHint("##UIHierarchySearch", "名前で検索...",
            ui_hierarchy_search_buffer.data(), ui_hierarchy_search_buffer.size());
        if (has_search)
        {
            ImGui::SameLine();
            if (ImGui::Button("×##ClearUIHierarchySearch"))
                ui_hierarchy_search_buffer[0] = '\0';
        }

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##UIHierarchyFilter",
            UIHierarchyFilterLabel(ui_hierarchy_filter_kind)))
        {
            for (const UIHierarchyFilterOption& option : UIHierarchyFilterOptions())
            {
                const bool selected = option.kind == ui_hierarchy_filter_kind;
                if (ImGui::Selectable(option.label, selected))
                    ui_hierarchy_filter_kind = option.kind;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    std::vector<Core::GameObject*> UIOrderPeers(Scene::Scene& scene,
        Core::GameObject& object)
    {
        std::vector<Core::GameObject*> peers;
        if (object.GetComponent<CanvasComponent>() != nullptr)
        {
            for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
            {
                Core::GameObject* candidate = scene.GameObjectAt(index);
                if (candidate != nullptr && !candidate->PendingDestroy() &&
                    candidate->GetComponent<CanvasComponent>() != nullptr)
                    peers.push_back(candidate);
            }
            return peers;
        }

        const std::vector<Core::GameObject*> siblings = object.Parent() != nullptr
            ? object.Parent()->Children() : scene.RootGameObjects();
        for (Core::GameObject* candidate : siblings)
        {
            if (candidate != nullptr && !candidate->PendingDestroy() &&
                candidate->GetComponent<RectTransformComponent>() != nullptr)
                peers.push_back(candidate);
        }
        return peers;
    }

    bool FindUIOrderExtreme(const std::vector<Core::GameObject*>& peers,
        Core::GameObject& object, bool find_higher, int& value)
    {
        bool found = false;
        for (Core::GameObject* peer : peers)
        {
            if (peer == nullptr || peer == &object) continue;
            const int* peer_order = UIOrderValue(*peer);
            if (peer_order == nullptr) continue;

            if (!found || (find_higher ? *peer_order > value : *peer_order < value))
            {
                value = *peer_order;
                found = true;
            }
        }
        return found;
    }

    bool FindUIOrderNeighbor(const std::vector<Core::GameObject*>& peers,
        Core::GameObject& object, bool find_higher, int current, int& value)
    {
        bool found = false;
        for (Core::GameObject* peer : peers)
        {
            if (peer == nullptr || peer == &object) continue;
            const int* peer_order = UIOrderValue(*peer);
            if (peer_order == nullptr ||
                (find_higher ? *peer_order <= current : *peer_order >= current))
                continue;

            if (!found || (find_higher ? *peer_order < value : *peer_order > value))
            {
                value = *peer_order;
                found = true;
            }
        }
        return found;
    }

    bool ApplyUIOrder(ReplayEngine::Editor::EditorContext& context,
        Core::GameObject& object, UIOrderAction action)
    {
        if (!context.CanEdit()) return false;
        Scene::Scene* scene = context.GetScene();
        int* order = UIOrderValue(object);
        if (scene == nullptr || order == nullptr) return false;

        const std::vector<Core::GameObject*> peers = UIOrderPeers(*scene, object);
        if (peers.size() < 2) return false;

        const int current = *order;
        int target = current;
        const auto increment = [](int value) noexcept
        {
            return value == (std::numeric_limits<int>::max)() ? value : value + 1;
        };
        const auto decrement = [](int value) noexcept
        {
            return value == (std::numeric_limits<int>::min)() ? value : value - 1;
        };

        int peer_value = current;
        switch (action)
        {
        case UIOrderAction::Frontmost:
            if (FindUIOrderExtreme(peers, object, true, peer_value))
                target = increment((std::max)(current, peer_value));
            break;
        case UIOrderAction::Backmost:
            if (FindUIOrderExtreme(peers, object, false, peer_value))
                target = decrement((std::min)(current, peer_value));
            break;
        case UIOrderAction::Forward:
            target = FindUIOrderNeighbor(peers, object, true, current, peer_value)
                ? increment(peer_value) : increment(current);
            break;
        case UIOrderAction::Backward:
            target = FindUIOrderNeighbor(peers, object, false, current, peer_value)
                ? decrement(peer_value) : decrement(current);
            break;
        }

        if (target == current) return false;
        context.BeginEdit("UI の描画順を変更");
        *order = target;
        context.CommitEdit();
        return true;
    }

    bool MoveUIHierarchySibling(ReplayEngine::Editor::EditorContext& context,
        Core::GameObject& object, UIHierarchyMoveDirection direction)
    {
        if (!context.CanEdit()) return false;
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr) return false;

        const std::size_t current = object.SiblingIndex();
        const std::vector<Core::GameObject*> siblings = object.Parent() != nullptr
            ? object.Parent()->Children() : scene->RootGameObjects();
        if (direction == UIHierarchyMoveDirection::Up)
        {
            if (current == 0u) return false;
        }
        else if (current + 1u >= siblings.size())
        {
            return false;
        }

        const std::size_t desired = direction == UIHierarchyMoveDirection::Up
            ? current - 1u : current + 1u;
        context.BeginEdit("UI 階層の順番を変更");
        const bool changed = object.SetSiblingIndex(desired);
        if (changed)
        {
            context.CommitEdit();
            context.SetStatus("UI 階層の順番を変更しました");
        }
        else
        {
            context.CancelEdit();
        }
        return changed;
    }

    void DrawUIOrderMenu(ReplayEngine::Editor::EditorContext& context,
        Core::GameObject& object)
    {
        if (UIOrderValue(object) == nullptr) return;

        ImGui::Separator();
        ImGui::TextDisabled("描画順（UI階層とは独立）");
        const bool editable = context.CanEdit();
        for (const UIOrderActionLabel& action : UIOrderActions())
        {
            if (ImGui::MenuItem(action.menu_name, nullptr, false, editable))
                ApplyUIOrder(context, object, action.action);
        }
    }

    void DrawUIOrderControls(ReplayEngine::Editor::EditorContext& context,
        Core::GameObject* selected)
    {
        if (selected == nullptr) return;

        int* order = UIOrderValue(*selected);
        if (order == nullptr) return;

        ImGui::TextDisabled("選択中の順番");
        if (ImGui::Button("↑ 階層"))
            MoveUIHierarchySibling(context, *selected, UIHierarchyMoveDirection::Up);
        ImGui::SameLine();
        if (ImGui::Button("↓ 階層"))
            MoveUIHierarchySibling(context, *selected, UIHierarchyMoveDirection::Down);
        ImGui::SameLine();
        ImGui::TextDisabled("描画順");
        ImGui::SetNextItemWidth(80.0f);
        int edited_order = *order;
        if (ImGui::InputInt("##UIRenderOrder", &edited_order) && context.CanEdit())
        {
            context.BeginEdit("UI の描画順を変更");
            *order = edited_order;
            context.CommitEdit();
        }
        for (const UIOrderActionLabel& action : UIOrderActions())
        {
            ImGui::SameLine();
            if (ImGui::Button(action.button_name))
                ApplyUIOrder(context, *selected, action.action);
        }
        ImGui::TextDisabled("階層の順番と描画順は別々に保存されます。描画順は値が大きいほど手前です。");
        ImGui::Separator();
    }

    DirectX::XMFLOAT2 UIResolvedSize(const RectTransformComponent& rect) noexcept
    {
        const DirectX::XMFLOAT4 resolved = rect.ResolvedRect();
        return { (std::max)(0.1f, resolved.z), (std::max)(0.1f, resolved.w) };
    }

    void SetUIResolvedSize(Core::GameObject& object,
        const DirectX::XMFLOAT2& desired_size)
    {
        RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
        if (rect == nullptr) return;

        DirectX::XMFLOAT2 anchor_size{};
        if (Core::GameObject* parent = object.Parent())
        {
            if (RectTransformComponent* parent_rect =
                parent->GetComponent<RectTransformComponent>())
            {
                const DirectX::XMFLOAT4 parent_resolved = parent_rect->ResolvedRect();
                anchor_size = {
                    parent_resolved.z * (rect->anchor_max.x - rect->anchor_min.x),
                    parent_resolved.w * (rect->anchor_max.y - rect->anchor_min.y) };
            }
        }
        // size_delta ではなく、アンカー込みで画面に出ているサイズを指定する。
        // 伸縮アンカーのImageでも、UI編集欄から見た目のサイズを直接変えられる。
        rect->size_delta = {
            desired_size.x - anchor_size.x,
            desired_size.y - anchor_size.y };
    }

    void ApplyUIShapeImageScale(UIMaskComponent& mask,
        Core::GameObject& mask_object, const DirectX::XMFLOAT2& desired_scale)
    {
        const float old_x = (std::max)(0.01f, mask.group_scale.x);
        const float old_y = (std::max)(0.01f, mask.group_scale.y);
        const DirectX::XMFLOAT2 next{
            (std::max)(0.01f, (std::min)(desired_scale.x, 100.0f)),
            (std::max)(0.01f, (std::min)(desired_scale.y, 100.0f)) };
        const DirectX::XMFLOAT2 ratio{ next.x / old_x, next.y / old_y };
        if (RectTransformComponent* rect = mask_object.GetComponent<RectTransformComponent>())
        {
            const DirectX::XMFLOAT2 current_size = UIResolvedSize(*rect);
            SetUIResolvedSize(mask_object, {
                current_size.x * ratio.x, current_size.y * ratio.y });
        }
        ScaleUIShapeImageDescendants(mask_object, ratio);
        mask.group_scale = next;
    }

    // 図形マスクと、その中で表示する Image の範囲は UI 階層ウィンドウだけで編集する。
    // Inspector 側には shape_* を出さず、選択対象に応じて必要な操作だけをここへ出す。
    void DrawUIEditingControls(ReplayEngine::Editor::EditorContext& context,
        Core::GameObject* selected)
    {
        if (selected == nullptr) return;

        UIMaskComponent* selected_mask = selected->GetComponent<UIMaskComponent>();
        const bool has_shape_mask = selected_mask != nullptr &&
            selected_mask->mask_mode == UIMaskComponent::Shape;

        UIImageComponent* image = selected->GetComponent<UIImageComponent>();
        RectTransformComponent* image_rect = selected->GetComponent<RectTransformComponent>();
        UIMaskComponent* parent_mask = nullptr;
        Core::GameObject* parent_mask_object = nullptr;
        for (Core::GameObject* parent = selected->Parent(); parent != nullptr;
            parent = parent->Parent())
        {
            UIMaskComponent* candidate = parent->GetComponent<UIMaskComponent>();
            if (candidate == nullptr) continue;
            parent_mask = candidate;
            parent_mask_object = parent;
            break;
        }
        const bool has_masked_image = image != nullptr && image_rect != nullptr &&
            parent_mask != nullptr && parent_mask->mask_mode == UIMaskComponent::Shape;
        if (!has_shape_mask && !has_masked_image) return;

        // 選択中の対象にだけ現れる独立枠。UI 階層パネルの外へ編集欄を増やさない。
        ImGui::BeginChild("UIShapeImageEditorFrame", ImVec2(0.0f, 230.0f), true);
        ImGui::TextDisabled("UI図形イメージ");
        ImGui::TextDisabled("選択中の図形マスク / 子Imageを編集");
        ImGui::Separator();

        const bool editable = context.CanEdit();
        const bool had_transaction = context.History().InTransaction();
        if (editable && !had_transaction)
            context.BeginEdit("UI マスクを編集");
        if (!editable) BeginDisabledCompat();

        bool mask_changed = false;
        bool image_changed = false;
        UIMaskComponent* edit_mask = has_shape_mask ? selected_mask : parent_mask;
        Core::GameObject* edit_mask_object = has_shape_mask ? selected : parent_mask_object;
        RectTransformComponent* mask_rect = edit_mask_object != nullptr
            ? edit_mask_object->GetComponent<RectTransformComponent>() : nullptr;
        const std::string selected_id = selected->ID().ToString();
        ImGui::PushID(selected_id.c_str());

        if (edit_mask != nullptr && mask_rect != nullptr)
        {
            ImGui::TextDisabled("マスク枠の配置");

            DirectX::XMFLOAT2 mask_position = mask_rect->anchored_position;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2("マスク枠の位置", &mask_position.x, 0.5f,
                -100000.0f, 100000.0f, "%.1f"))
            {
                mask_rect->anchored_position = mask_position;
                mask_changed = true;
            }

            DirectX::XMFLOAT2 mask_size = UIResolvedSize(*mask_rect);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2("マスク枠のサイズ", &mask_size.x, 0.5f,
                0.1f, 100000.0f, "%.1f"))
            {
                SetUIResolvedSize(*edit_mask_object, mask_size);
                mask_changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("切り抜く枠だけを変更します。中の画像サイズは変わりません。");

            DirectX::XMFLOAT2 mask_scale = edit_mask->group_scale;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2("全体の拡大率", &mask_scale.x, 0.01f,
                0.01f, 100.0f, "%.2f"))
            {
                ApplyUIShapeImageScale(*edit_mask, *edit_mask_object, mask_scale);
                mask_changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("マスク枠と中の画像をまとめて拡大・縮小します。");

            if (ImGui::Button("子Imageをマスク枠に合わせる", ImVec2(-1.0f, 0.0f)))
            {
                const DirectX::XMFLOAT2 resolved_size = UIResolvedSize(*mask_rect);
                for (Core::GameObject* child : edit_mask_object->Children())
                {
                    if (child == nullptr ||
                        child->GetComponent<UIImageComponent>() == nullptr) continue;
                    RectTransformComponent* child_rect =
                        child->GetComponent<RectTransformComponent>();
                    if (child_rect == nullptr) continue;

                    child_rect->anchor_min = { 0.5f, 0.5f };
                    child_rect->anchor_max = { 0.5f, 0.5f };
                    child_rect->anchored_position = { 0.0f, 0.0f };
                    child_rect->size_delta = resolved_size;
                    child_rect->pivot = { 0.5f, 0.5f };
                    child_rect->scale = { 1.0f, 1.0f };
                    mask_changed = true;
                }
                if (mask_changed)
                    context.SetStatus("子Imageをマスク枠の中央・同サイズへ合わせました");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("白い図形が枠より小さい時に、中央・同サイズへ揃えます。");
        }

        if (has_shape_mask)
        {
            ImGui::TextDisabled("図形マスク（UI編集）");
            ImGui::TextDisabled("親の形で、子の描画を切り抜きます。");

            int shape_kind = (std::max)(0, (std::min)(
                selected_mask->shape_kind, 4));
            static const char shape_items[] =
                "矩形\0円\0多角形\0星形\0角丸矩形\0";
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("形状", &shape_kind, shape_items))
            {
                selected_mask->shape_kind = shape_kind;
                mask_changed = true;
            }

            if (shape_kind == UIMaskComponent::ShapePolygon ||
                shape_kind == UIMaskComponent::ShapeStar)
            {
                int sides = (std::max)(3, (std::min)(selected_mask->shape_sides, 64));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragInt("頂点数", &sides, 1.0f, 3, 64))
                {
                    selected_mask->shape_sides = sides;
                    mask_changed = true;
                }
            }
            if (shape_kind == UIMaskComponent::ShapeStar)
            {
                float inner_radius = (std::max)(0.05f,
                    (std::min)(selected_mask->shape_inner_radius, 0.95f));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat("星形の内側", &inner_radius,
                    0.01f, 0.05f, 0.95f, "%.2f"))
                {
                    selected_mask->shape_inner_radius = inner_radius;
                    mask_changed = true;
                }
            }
            if (shape_kind == UIMaskComponent::ShapeRoundedRectangle)
            {
                float corner_radius = (std::max)(0.0f,
                    (std::min)(selected_mask->shape_corner_radius, 1.0f));
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat("角丸量", &corner_radius,
                    0.01f, 0.0f, 1.0f, "%.2f"))
                {
                    selected_mask->shape_corner_radius = corner_radius;
                    mask_changed = true;
                }
            }

            float rotation = (std::max)(-180.0f,
                (std::min)(selected_mask->shape_rotation, 180.0f));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("形状の回転", &rotation, 1.0f,
                -180.0f, 180.0f, "%.0f 度"))
            {
                selected_mask->shape_rotation = rotation;
                mask_changed = true;
            }

            float softness = (std::max)(0.0f,
                (std::min)(selected_mask->softness, 1.0f));
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("境界の柔らかさ", &softness,
                0.01f, 0.0f, 1.0f, "%.2f"))
            {
                selected_mask->softness = softness;
                mask_changed = true;
            }
            if (ImGui::Checkbox("切り抜きを反転", &selected_mask->invert))
                mask_changed = true;
        }

        if (has_masked_image)
        {
            ImGui::Separator();
            ImGui::TextDisabled("切り抜き Image（UI編集）");
            ImGui::TextDisabled("位置・サイズで表示枠を、UVで元画像の範囲を決めます。");

            DirectX::XMFLOAT2 position = image_rect->anchored_position;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2("映す位置", &position.x, 0.5f,
                -100000.0f, 100000.0f, "%.1f"))
            {
                image_rect->anchored_position = position;
                image_changed = true;
            }

            DirectX::XMFLOAT2 size = UIResolvedSize(*image_rect);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2("映すサイズ", &size.x, 0.5f,
                0.0f, 100000.0f, "%.1f"))
            {
                SetUIResolvedSize(*selected, size);
                image_changed = true;
            }

            DirectX::XMFLOAT2 uv_offset = image->uv_offset;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2("画像のUV位置", &uv_offset.x, 0.001f,
                -100.0f, 100.0f, "%.3f"))
            {
                image->uv_offset = uv_offset;
                image_changed = true;
            }

            DirectX::XMFLOAT2 uv_scale = image->uv_scale;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2("画像のUV範囲", &uv_scale.x, 0.001f,
                0.001f, 100.0f, "%.3f"))
            {
                image->uv_scale = uv_scale;
                image_changed = true;
            }
        }

        ImGui::PopID();
        if (!editable) EndDisabledCompat();
        ImGui::EndChild();

        if (mask_changed || image_changed)
        {
            context.MarkDirty();
            if (mask_changed)
            {
                if (selected_mask != nullptr) selected_mask->OnPropertyChanged(nullptr);
                if (parent_mask != nullptr && parent_mask != selected_mask)
                    parent_mask->OnPropertyChanged(nullptr);
            }
        }

        // ドラッグ中はトランザクションを保持し、指を離した時点で Undo を 1 件にまとめる。
        if (editable && context.History().InTransaction() && !ImGui::IsAnyItemActive())
        {
            if (mask_changed || image_changed || had_transaction)
                context.CommitEdit();
            else
                context.CancelEdit();
        }
    }


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

    // 選択中の Image だけを対象にする。図形マスク済みの Image には再適用しない。
    Core::GameObject* SelectedUIImageForShapeMask(
        ReplayEngine::Editor::EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr) return nullptr;
        Core::GameObject* selected = context.Selection().ResolvePrimary(*scene);
        if (selected == nullptr || !ContainsUI(*selected) ||
            selected->GetComponent<UIImageComponent>() == nullptr ||
            selected->GetComponent<RectTransformComponent>() == nullptr)
            return nullptr;

        for (Core::GameObject* parent = selected->Parent(); parent != nullptr;
            parent = parent->Parent())
        {
            UIMaskComponent* mask = parent->GetComponent<UIMaskComponent>();
            if (mask != nullptr && mask->mask_mode == UIMaskComponent::Shape)
                return nullptr;
        }
        return selected;
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

    const char* UIElementName(UIElementKind kind) noexcept
    {
        switch (kind)
        {
        case UIElementKind::Image: return "Image";
        case UIElementKind::Text: return "Text";
        case UIElementKind::Button: return "Button";
        case UIElementKind::Mask: return "Mask";
        case UIElementKind::Canvas: return "Canvas";
        }
        return "UI Element";
    }

    DirectX::XMFLOAT2 UIElementSize(UIElementKind kind) noexcept
    {
        return kind == UIElementKind::Button
            ? DirectX::XMFLOAT2{ 180.0f, 52.0f }
            : DirectX::XMFLOAT2{ 160.0f, 80.0f };
    }

    Core::GameObject* CreateUIChildObject(Scene::Scene& scene,
        Core::GameObject* parent, const char* desired_name,
        const DirectX::XMFLOAT2& size)
    {
        if (parent == nullptr || desired_name == nullptr) return nullptr;

        Core::GameObject* object = scene.CreateGameObject(
            UniqueUIObjectName(scene, parent, desired_name));
        if (object == nullptr) return nullptr;
        if (!object->SetParent(parent, false))
        {
            scene.DestroyGameObject(object);
            return nullptr;
        }

        RectTransformComponent* rect = object->AddComponent<RectTransformComponent>();
        if (rect != nullptr) rect->size_delta = size;
        return object;
    }

    void AddButtonLabel(Scene::Scene& scene, Core::GameObject& button)
    {
        Core::GameObject* label = scene.CreateGameObject(
            UniqueUIObjectName(scene, &button, "Button Text"));
        if (label == nullptr) return;
        if (!label->SetParent(&button, false))
        {
            scene.DestroyGameObject(label);
            return;
        }

        RectTransformComponent* rect = label->AddComponent<RectTransformComponent>();
        if (rect != nullptr)
        {
            rect->anchor_min = { 0.0f, 0.0f };
            rect->anchor_max = { 1.0f, 1.0f };
            rect->size_delta = { 0.0f, 0.0f };
        }

        UITextComponent* text = label->AddComponent<UITextComponent>();
        if (text != nullptr)
        {
            text->text = "Button";
            text->font_size = 24.0f;
        }
    }

    void ConfigureUIPrimitive(UIShapeComponent& shape,
        const UIPrimitivePreset& preset)
    {
        shape.shape = preset.shape;
        shape.sides = preset.sides;
        shape.corner_radius = preset.corner_radius;
        shape.polar_base_radius = preset.polar_base_radius;
        shape.polar_amplitude = preset.polar_amplitude;
        shape.polar_lobes = preset.polar_lobes;
        shape.fill_color = { 0.25f, 0.65f, 1.0f, 1.0f };
        shape.stroke_color = { 0.08f, 0.18f, 0.32f, 1.0f };
        shape.stroke_width = preset.stroke_width;
        if (preset.shape == UIShapeComponent::CustomBezierPath)
        {
            // 自由図形は最初から輪郭を編集できるよう、四隅のアンカーを持たせる。
            // 通常の RectTransform のリサイズハンドルとは別の頂点コントローラーで編集する。
            shape.SetPathPointCount(4);
            shape.path_closed = true;
            shape.path_points[0] = { 0.12f, 0.12f };
            shape.path_points[1] = { 0.88f, 0.12f };
            shape.path_points[2] = { 0.88f, 0.88f };
            shape.path_points[3] = { 0.12f, 0.88f };
            shape.OnPropertyChanged("path_points");
        }
    }

    void ConfigureUIShapeImage(UIShapeImageComponent& shape)
    {
        shape.SetPathPointCount(4);
        shape.path_closed = true;
        shape.path_points[0] = { 0.12f, 0.12f };
        shape.path_points[1] = { 0.88f, 0.12f };
        shape.path_points[2] = { 0.88f, 0.88f };
        shape.path_points[3] = { 0.12f, 0.88f };
        shape.OnPropertyChanged("path_points");
    }

    Core::GameObject* CreateUIElement(ReplayEngine::Editor::EditorContext& context,
        UIElementKind kind)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return nullptr;

        context.BeginEdit("UI 要素を作成");
        Core::GameObject* created = nullptr;
        if (kind == UIElementKind::Canvas)
        {
            created = CreateCanvasObject(*scene);
        }
        else
        {
            Core::GameObject* parent = SelectedUIParent(context);
            if (parent == nullptr) parent = CreateCanvasObject(*scene);
            created = CreateUIChildObject(*scene, parent, UIElementName(kind),
                UIElementSize(kind));
            if (created != nullptr)
            {
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
                    AddButtonLabel(*scene, *created);
                }
                else if (kind == UIElementKind::Mask)
                {
                    UIImageComponent* image = created->AddComponent<UIImageComponent>();
                    if (image != nullptr) image->color = { 0.2f, 0.45f, 0.8f, 0.18f };
                    created->AddComponent<UIMaskComponent>();
                }
            }
        }

        if (created != nullptr)
            context.Selection().Select(created->ID(), false);
        context.CommitEdit();
        return created;
    }

    Core::GameObject* CreateUIPrimitive(
        ReplayEngine::Editor::EditorContext& context,
        const UIPrimitivePreset& preset)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return nullptr;

        context.BeginEdit("UI 図形を作成");
        Core::GameObject* parent = SelectedUIParent(context);
        if (parent == nullptr) parent = CreateCanvasObject(*scene);
        Core::GameObject* created = CreateUIChildObject(*scene, parent,
            preset.object_name, preset.size_delta);
        if (created != nullptr)
        {
            UIShapeComponent* shape = created->AddComponent<UIShapeComponent>();
            if (shape != nullptr) ConfigureUIPrimitive(*shape, preset);
            context.Selection().Select(created->ID(), false);
        }
        context.CommitEdit();
        return created;
    }

    void DrawUIPrimitiveMenu(ReplayEngine::Editor::EditorContext& context,
        bool close_popup)
    {
        for (const UIPrimitivePreset& preset : UIPrimitivePresets())
        {
            if (!ImGui::MenuItem(preset.menu_name)) continue;
            CreateUIPrimitive(context, preset);
            if (close_popup) ImGui::CloseCurrentPopup();
        }
    }

    Core::GameObject* CreateUICustomShape(
        ReplayEngine::Editor::EditorContext& context)
    {
        return CreateUIPrimitive(context, CustomShapePreset());
    }

    Core::GameObject* CreateUICustomShapeImage(
        ReplayEngine::Editor::EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit()) return nullptr;

        context.BeginEdit("自由図形 Image を作成");
        Core::GameObject* parent = SelectedUIParent(context);
        if (parent == nullptr) parent = CreateCanvasObject(*scene);
        Core::GameObject* created = CreateUIChildObject(*scene, parent,
            "自由図形 Image", { 160.0f, 100.0f });
        if (created != nullptr)
        {
            created->AddComponent<UIImageComponent>();
            UIShapeImageComponent* shape =
                created->AddComponent<UIShapeImageComponent>();
            if (shape != nullptr) ConfigureUIShapeImage(*shape);
            context.Selection().Select(created->ID(), false);
            context.SetStatus("自由図形 Image を作成しました。頂点コントローラーで輪郭を編集できます");
        }
        context.CommitEdit();
        return created;
    }

    int MaskShapeKindForPreset(const UIPrimitivePreset& preset) noexcept
    {
        if (preset.shape == UIShapeComponent::Circle)
            return UIMaskComponent::ShapeCircle;
        if (preset.shape == UIShapeComponent::Polygon)
            return UIMaskComponent::ShapePolygon;
        if (preset.shape == UIShapeComponent::PolarFormula)
            return UIMaskComponent::ShapeStar;
        if (preset.shape == UIShapeComponent::Rectangle &&
            preset.corner_radius > 0.0f)
            return UIMaskComponent::ShapeRoundedRectangle;
        if (preset.shape == UIShapeComponent::Rectangle)
            return UIMaskComponent::ShapeRectangle;
        return -1;
    }

    void ConfigureUIShapeMask(UIMaskComponent& mask,
        const UIPrimitivePreset& preset)
    {
        mask.enabled_mask = true;
        mask.show_mask_graphic = false;
        mask.mask_mode = UIMaskComponent::Shape;
        mask.shape_kind = MaskShapeKindForPreset(preset);
        mask.shape_sides = (std::max)(3, preset.sides);
        mask.shape_inner_radius = (std::max)(0.05f,
            (std::min)(0.95f, preset.polar_base_radius));
        const float minimum_size = (std::min)(preset.size_delta.x,
            preset.size_delta.y);
        mask.shape_corner_radius = minimum_size > 0.0f
            ? (std::max)(0.0f, (std::min)(1.0f,
                preset.corner_radius * 2.0f / minimum_size)) : 0.0f;
        mask.shape_rotation = 0.0f;
        mask.softness = 0.0f;
    }

    Core::GameObject* CreateUIShapeMaskedImage(
        ReplayEngine::Editor::EditorContext& context,
        const UIPrimitivePreset& preset)
    {
        Scene::Scene* scene = context.GetScene();
        if (scene == nullptr || !context.CanEdit() ||
            MaskShapeKindForPreset(preset) < 0)
            return nullptr;

        context.BeginEdit("図形マスク付き Image を作成");
        Core::GameObject* parent = SelectedUIParent(context);
        // Image を選択中に追加しても、Image の中へマスクを入れ子にしない。
        // 既存 Image を図形化する操作とは別に、新規追加は同じ親へ並べる。
        if (parent != nullptr && parent->GetComponent<UIImageComponent>() != nullptr)
            parent = parent->Parent();
        if (parent == nullptr) parent = CreateCanvasObject(*scene);

        const std::string mask_name = std::string(preset.object_name) + " Image Mask";
        Core::GameObject* mask_object = CreateUIChildObject(*scene, parent,
            mask_name.c_str(), preset.size_delta);
        Core::GameObject* image_object = nullptr;
        if (mask_object != nullptr)
        {
            UIMaskComponent* mask = mask_object->AddComponent<UIMaskComponent>();
            if (mask != nullptr) ConfigureUIShapeMask(*mask, preset);

            image_object = CreateUIChildObject(*scene, mask_object,
                "Masked Image", preset.size_delta);
            if (image_object != nullptr)
                image_object->AddComponent<UIImageComponent>();
        }

        if (mask_object != nullptr && image_object != nullptr)
        {
            // 最初は親マスクを選び、図形そのものを移動・拡縮できる状態にする。
            // 子の Image を選べば、UI図形イメージ枠から表示範囲だけを調整できる。
            context.Selection().Select(mask_object->ID(), false);
            context.SetStatus("図形イメージを作成しました。子Imageを選ぶと映す範囲を調整できます");
        }
        else if (mask_object != nullptr)
        {
            scene->DestroyGameObject(mask_object);
            mask_object = nullptr;
        }
        context.CommitEdit();
        return image_object;
    }

    Core::GameObject* CreateUICustomShapeMaskedImage(
        ReplayEngine::Editor::EditorContext& context)
    {
        // 自由形状 Image は親Mask＋子Imageではなく、専用コンポーネントを
        // 同じ GameObject に付ける。UIImageComponent の矩形責務を汚さない。
        return CreateUICustomShapeImage(context);
    }

    // 選択中の Image をその場で図形マスクの子へ移す。
    // 新しい Image を増やさないので、すでに設定した Sprite / UV / 色をそのまま使える。
    Core::GameObject* CreateUIShapeMaskForSelectedImage(
        ReplayEngine::Editor::EditorContext& context,
        const UIPrimitivePreset& preset)
    {
        Scene::Scene* scene = context.GetScene();
        Core::GameObject* image_object = SelectedUIImageForShapeMask(context);
        if (scene == nullptr || image_object == nullptr || !context.CanEdit() ||
            MaskShapeKindForPreset(preset) < 0)
            return nullptr;

        RectTransformComponent* image_rect =
            image_object->GetComponent<RectTransformComponent>();
        if (image_rect == nullptr) return nullptr;

        const DirectX::XMFLOAT2 old_size = image_rect->size_delta;
        const DirectX::XMFLOAT4 old_resolved_rect = image_rect->ResolvedRect();
        const float old_rotation = image_rect->rotation;
        const DirectX::XMFLOAT2 old_scale = image_rect->scale;
        Core::GameObject* old_parent = image_object->Parent();
        const std::size_t old_sibling_index = image_object->SiblingIndex();

        context.BeginEdit("選択中の Image を UI図形イメージ化");
        Core::GameObject* mask_parent = old_parent;
        if (mask_parent == nullptr)
        {
            // Scene 直下の Image は、最初の Canvas を親にして UI として扱えるようにする。
            mask_parent = FindFirstCanvas(*scene);
            if (mask_parent == nullptr) mask_parent = CreateCanvasObject(*scene);
        }

        Core::GameObject* mask_object = CreateUIChildObject(*scene, mask_parent,
            "UI図形イメージ", old_size);
        Core::GameObject* result = nullptr;
        if (mask_object != nullptr)
        {
            UIMaskComponent* mask = mask_object->AddComponent<UIMaskComponent>();
            RectTransformComponent* mask_rect =
                mask_object->GetComponent<RectTransformComponent>();
            if (mask != nullptr) ConfigureUIShapeMask(*mask, preset);
            if (mask_rect != nullptr)
            {
                // 元Imageのアンカー値をそのまま親Maskへ移すと、伸縮アンカーの
                // 差分でマスク枠だけが別サイズになる。解決済みの表示範囲を
                // 親座標へ変換し、固定アンカーの枠として作り直す。
                DirectX::XMFLOAT4 parent_rect{};
                if (RectTransformComponent* parent_transform =
                    mask_parent->GetComponent<RectTransformComponent>())
                    parent_rect = parent_transform->ResolvedRect();
                const float old_center_x = old_resolved_rect.x +
                    old_resolved_rect.z * 0.5f;
                const float old_center_y = old_resolved_rect.y +
                    old_resolved_rect.w * 0.5f;
                const float parent_center_x = parent_rect.x + parent_rect.z * 0.5f;
                const float parent_center_y = parent_rect.y + parent_rect.w * 0.5f;
                const DirectX::XMFLOAT2 resolved_size{
                    (std::max)(0.1f, old_resolved_rect.z > 0.1f
                        ? old_resolved_rect.z : old_size.x) *
                        (std::max)(0.01f, std::fabs(old_scale.x)),
                    (std::max)(0.1f, old_resolved_rect.w > 0.1f
                        ? old_resolved_rect.w : old_size.y) *
                        (std::max)(0.01f, std::fabs(old_scale.y)) };
                mask_rect->anchor_min = { 0.5f, 0.5f };
                mask_rect->anchor_max = { 0.5f, 0.5f };
                mask_rect->anchored_position = {
                    old_center_x - parent_center_x,
                    old_center_y - parent_center_y };
                mask_rect->size_delta = resolved_size;
                mask_rect->pivot = { 0.5f, 0.5f };
                mask_rect->rotation = old_rotation;
                // 親Maskの拡大率は1に固定し、子Imageへ元の倍率を残す。
                // このUIレイアウトは親のRectTransform拡大率を子へ継承しないため、
                // 親だけを拡大すると枠と中身がずれる。
                mask_rect->scale = { 1.0f, 1.0f };
            }

            if (mask != nullptr && image_object->SetParent(mask_object, false))
            {
                // Image はマスクの中央へ置き、元の Image のサイズと表示設定を引き継ぐ。
                image_rect->anchor_min = { 0.5f, 0.5f };
                image_rect->anchor_max = { 0.5f, 0.5f };
                image_rect->anchored_position = { 0.0f, 0.0f };
                image_rect->size_delta = {
                    (std::max)(0.1f, old_resolved_rect.z > 0.1f
                        ? old_resolved_rect.z : old_size.x),
                    (std::max)(0.1f, old_resolved_rect.w > 0.1f
                        ? old_resolved_rect.w : old_size.y) };
                image_rect->pivot = { 0.5f, 0.5f };
                image_rect->rotation = 0.0f;
                image_rect->scale = old_scale;
                if (old_parent != nullptr)
                    mask_object->SetSiblingIndex(old_sibling_index);
                result = image_object;
            }
        }

        if (result != nullptr)
        {
            // 作成直後は親を選択し、図形と枠を一緒に動かせるようにする。
            context.Selection().Select(mask_object->ID(), false);
            context.SetStatus("選択中のImageを図形マスク化しました。子Imageを選ぶと映す範囲を調整できます");
            context.CommitEdit();
        }
        else
        {
            if (mask_object != nullptr) scene->DestroyGameObject(mask_object);
            context.CancelEdit();
        }
        return result;
    }

    Core::GameObject* CreateUICustomShapeMaskForSelectedImage(
        ReplayEngine::Editor::EditorContext& context)
    {
        Scene::Scene* scene = context.GetScene();
        Core::GameObject* image_object = SelectedUIImageForShapeMask(context);
        if (scene == nullptr || image_object == nullptr || !context.CanEdit())
            return nullptr;

        context.BeginEdit("選択中の Image を自由図形 Image 化");
        UIShapeImageComponent* shape =
            image_object->GetComponent<UIShapeImageComponent>();
        if (shape == nullptr) shape = image_object->AddComponent<UIShapeImageComponent>();
        if (shape != nullptr)
        {
            ConfigureUIShapeImage(*shape);
            context.Selection().Select(image_object->ID(), false);
            context.SetStatus("選択中のImageを自由図形Image化しました。頂点コントローラーで輪郭を編集できます");
            context.CommitEdit();
            return image_object;
        }
        context.CancelEdit();
        return nullptr;
    }

    void DrawUISelectedShapeImageMenu(
        ReplayEngine::Editor::EditorContext& context, bool close_popup)
    {
        for (const UIPrimitivePreset& preset : UIPrimitivePresets())
        {
            if (MaskShapeKindForPreset(preset) < 0) continue;
            const std::string label = std::string(preset.menu_name) + "で切り抜く";
            if (!ImGui::MenuItem(label.c_str())) continue;
            CreateUIShapeMaskForSelectedImage(context, preset);
            if (close_popup) ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("自由図形で切り抜く"))
        {
            CreateUICustomShapeMaskForSelectedImage(context);
            if (close_popup) ImGui::CloseCurrentPopup();
        }
    }

    void DrawUIShapeMaskMenu(ReplayEngine::Editor::EditorContext& context,
        bool close_popup)
    {
        for (const UIPrimitivePreset& preset : UIPrimitivePresets())
        {
            if (MaskShapeKindForPreset(preset) < 0) continue;
            const std::string label = std::string(preset.menu_name) +
                "で切り抜く";
            if (!ImGui::MenuItem(label.c_str())) continue;
            CreateUIShapeMaskedImage(context, preset);
            if (close_popup) ImGui::CloseCurrentPopup();
        }
        if (ImGui::MenuItem("自由図形で切り抜く"))
        {
            CreateUICustomShapeMaskedImage(context);
            if (close_popup) ImGui::CloseCurrentPopup();
        }
    }

    void DrawUIImageCreationMenu(
        ReplayEngine::Editor::EditorContext& context, bool close_popup)
    {
        if (ImGui::MenuItem("通常のImageを追加"))
        {
            CreateUIElement(context, UIElementKind::Image);
            if (close_popup) ImGui::CloseCurrentPopup();
        }
        if (ImGui::BeginMenu("図形で切り抜いたImageを追加"))
        {
            DrawUIShapeMaskMenu(context, close_popup);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("自由図形Imageを追加"))
        {
            CreateUICustomShapeImage(context);
            if (close_popup) ImGui::CloseCurrentPopup();
        }
    }

    void DrawUIHierarchyCreateMenu(
        ReplayEngine::Editor::EditorContext& context)
    {
        if (ImGui::MenuItem("Canvasを追加"))
            CreateUIElement(context, UIElementKind::Canvas);
        DrawUIImageCreationMenu(context, false);
        if (ImGui::MenuItem("Textを追加"))
            CreateUIElement(context, UIElementKind::Text);
        if (ImGui::MenuItem("Buttonを追加"))
            CreateUIElement(context, UIElementKind::Button);
        if (ImGui::MenuItem("Maskを追加"))
            CreateUIElement(context, UIElementKind::Mask);
        if (ImGui::BeginMenu("図形を追加"))
        {
            DrawUIPrimitiveMenu(context, false);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("自由図形を追加"))
            CreateUICustomShape(context);
        if (SelectedUIImageForShapeMask(context) != nullptr &&
            ImGui::BeginMenu("図形イメージを追加"))
        {
            DrawUISelectedShapeImageMenu(context, false);
            ImGui::EndMenu();
        }
    }

    void DrawUIHierarchyCreateToolbar(
        ReplayEngine::Editor::EditorContext& context)
    {
        const bool show_selected_shape_image =
            SelectedUIImageForShapeMask(context) != nullptr;
        const float available_width = ImGui::GetContentRegionAvail().x;
        if (available_width < 250.0f)
        {
            // 極端に狭いときはボタンを隠さず、全機能を一つのメニューへまとめる。
            if (ImGui::Button("＋ UIを追加", ImVec2(-1.0f, 0.0f)))
                ImGui::OpenPopup("UIHierarchyCreatePopup");
            if (ImGui::BeginPopup("UIHierarchyCreatePopup"))
            {
                DrawUIHierarchyCreateMenu(context);
                ImGui::EndPopup();
            }
            // 狭い幅でも、通常の図形とは別の専用ボタンを常に見せる。
            if (!show_selected_shape_image) BeginDisabledCompat();
            if (ImGui::Button("図形イメージ", ImVec2(-1.0f, 0.0f)))
                ImGui::OpenPopup("UIShapeImageCreatePopup");
            if (!show_selected_shape_image) EndDisabledCompat();
            if (show_selected_shape_image && ImGui::BeginPopup("UIShapeImageCreatePopup"))
            {
                DrawUISelectedShapeImageMenu(context, true);
                ImGui::EndPopup();
            }
            if (ImGui::Button("自由図形イメージ", ImVec2(-1.0f, 0.0f)))
                CreateUICustomShapeImage(context);
            return;
        }

        bool first_button_on_line = true;
        const auto draw_button = [&first_button_on_line](const char* label,
            const std::function<void()>& on_click)
        {
            const float width = ImGui::CalcTextSize(label).x +
                ImGui::GetStyle().FramePadding.x * 2.0f;
            if (first_button_on_line)
            {
                first_button_on_line = false;
            }
            else if (ImGui::GetContentRegionAvail().x < width)
            {
                ImGui::NewLine();
            }
            else
            {
                ImGui::SameLine();
            }

            if (ImGui::Button(label, ImVec2(width, 0.0f))) on_click();
        };

        draw_button("Canvas", [&context]
        {
            CreateUIElement(context, UIElementKind::Canvas);
        });

        draw_button("Image", []
        {
            ImGui::OpenPopup("UIImageCreatePopup");
        });
        if (ImGui::BeginPopup("UIImageCreatePopup"))
        {
            DrawUIImageCreationMenu(context, true);
            ImGui::EndPopup();
        }

        draw_button("Text", [&context]
        {
            CreateUIElement(context, UIElementKind::Text);
        });

        draw_button("Button", [&context]
        {
            CreateUIElement(context, UIElementKind::Button);
        });

        draw_button("Mask", [&context]
        {
            CreateUIElement(context, UIElementKind::Mask);
        });

        draw_button("図形", []
        {
            ImGui::OpenPopup("UIShapeCreatePopup");
        });
        if (ImGui::BeginPopup("UIShapeCreatePopup"))
        {
            DrawUIPrimitiveMenu(context, true);
            ImGui::EndPopup();
        }

        if (!show_selected_shape_image) BeginDisabledCompat();
        draw_button("図形イメージ", []
        {
            ImGui::OpenPopup("UIShapeImageCreatePopup");
        });
        if (!show_selected_shape_image) EndDisabledCompat();
        if (show_selected_shape_image && ImGui::BeginPopup("UIShapeImageCreatePopup"))
        {
            DrawUISelectedShapeImageMenu(context, true);
            ImGui::EndPopup();
        }

        draw_button("自由図形", [&context]
        {
            CreateUICustomShape(context);
        });

        draw_button("自由図形イメージ", [&context]
        {
            CreateUICustomShapeImage(context);
        });
    }


    // selection_changed は「この呼び出しの中で GameObject が選ばれたか」を返す。
    //
    // editor_selection は framework の入れ子 enum で、selected_editor_object も
    // framework のメンバ。この関数はフリー関数なのでどちらにも触れない。
    // ここで直接代入するとコンパイルが通らないため、結果だけを呼び出し元へ返し、
    // framework 側で選択種別を切り替える。
    void DrawUINode(ReplayEngine::Editor::EditorContext& context,
        Core::GameObject& object, const UIHierarchyFilterState& filter,
        bool& selection_changed)
    {
        if (!UIHierarchyNodeMatchesFilter(object, filter)) return;

        const bool selected = context.Selection().IsSelected(object.ID());
        bool has_ui_child = false;
        for (const Core::GameObject* child : object.Children())
        {
            if (child != nullptr && UIHierarchyNodeMatchesFilter(*child, filter))
            {
                has_ui_child = true;
                break;
            }
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;
        if (!has_ui_child) flags |= ImGuiTreeNodeFlags_Leaf;

        if (UIHierarchyFilterActive(filter) && has_ui_child)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);

        const std::string label = UIHierarchyLabel(object);
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
                // 通常のドラッグは必ず兄弟順の入れ替えにする。
                // 行の中央へ落としただけで意図せず子になると、UI配置の並べ替えが
                // できないように見えてしまうため。親子付けは Ctrl + ドロップで行う。
                UIHierarchyDropPlacement placement = ImGui::GetIO().KeyCtrl
                    ? UIHierarchyDropPlacement::Child
                    : (local_y < height * 0.5f
                        ? UIHierarchyDropPlacement::Before
                        : UIHierarchyDropPlacement::After);
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
            if (ImGui::BeginMenu("Image を追加"))
            {
                DrawUIImageCreationMenu(context, false);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Text を追加")) CreateUIElement(context, UIElementKind::Text);
            if (ImGui::MenuItem("Button を追加")) CreateUIElement(context, UIElementKind::Button);
            if (ImGui::MenuItem("Mask を追加")) CreateUIElement(context, UIElementKind::Mask);
            if (ImGui::BeginMenu("図形を追加"))
            {
                DrawUIPrimitiveMenu(context, false);
                ImGui::EndMenu();
            }
            DrawUIOrderMenu(context, object);
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
                if (child != nullptr)
                    DrawUINode(context, *child, filter, selection_changed);
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
    DrawUIHierarchyCreateToolbar(object_editor_context);
    if (!can_edit) EndDisabledCompat();

    ImGui::Separator();
    ImGui::TextDisabled("ドラッグで上下に入れ替え / Ctrl + ドロップで子にする");
    DrawUIHierarchyFilterControls();
    if (scene == nullptr)
    {
        ImGui::TextDisabled("Scene がありません");
        ImGui::End();
        return;
    }

    Core::GameObject* selected_ui = object_editor_context.Selection().ResolvePrimary(*scene);
    if (selected_ui != nullptr && ContainsUI(*selected_ui))
    {
        DrawUIOrderControls(object_editor_context, selected_ui);
        DrawUIEditingControls(object_editor_context, selected_ui);
    }

    const UIHierarchyFilterState filter{
        LowerAscii(ui_hierarchy_search_buffer.data()), ui_hierarchy_filter_kind };
    bool any_ui_root = false;
    bool any_visible_root = false;
    bool ui_selection_changed = false;
    for (Core::GameObject* root : scene->RootGameObjects())
    {
        if (root == nullptr || !ContainsUI(*root)) continue;
        any_ui_root = true;
        if (!UIHierarchyNodeMatchesFilter(*root, filter)) continue;
        any_visible_root = true;
        DrawUINode(object_editor_context, *root, filter, ui_selection_changed);
    }
    // UI 階層で選んだものも Delete の対象にする。
    // framework_class.h の Delete 処理が selected_editor_object を見ているため。
    if (ui_selection_changed) selected_editor_object = editor_selection::game_object;
    if (!any_ui_root)
        ImGui::TextDisabled("Canvas はまだありません");
    else if (!any_visible_root)
        ImGui::TextDisabled("フィルターに一致するUIはありません");

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

    // UI ツリーの走査中には構造を変えず、描画完了後に 1 回の編集として反映する。
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

    const auto draw_custom_path_editor = [&](auto* shape, const char* title)
    {
        if (shape == nullptr) return;
        ImGui::TextDisabled(title);
        ImGui::Text("Points: %d", static_cast<int>(shape->path_points.size()));
        if (ImGui::Button("Point を追加") && object_editor_context.CanEdit())
        {
            object_editor_context.BeginEdit("自由図形の頂点を追加");
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
            object_editor_context.BeginEdit("自由図形の頂点を挿入");
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
            object_editor_context.BeginEdit("自由図形の頂点を削除");
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
    };

    if (UIShapeComponent* shape = selected->GetComponent<UIShapeComponent>();
        shape != nullptr && shape->shape == UIShapeComponent::CustomBezierPath)
    {
        draw_custom_path_editor(shape, "自由図形コントローラー");
    }
    else if (UIShapeImageComponent* shape =
        selected->GetComponent<UIShapeImageComponent>())
    {
        draw_custom_path_editor(shape, "自由図形Image コントローラー");
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
