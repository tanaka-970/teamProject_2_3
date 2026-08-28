#include "UILayout.h"

#include "UIFocusManager.h"
#include "UIInputFieldSystem.h"
#include "../Components/UI/CanvasComponent.h"
#include "../Components/UI/RectTransformComponent.h"
#include "../Components/UI/UIButtonComponent.h"
#include "../Components/UI/UIImageComponent.h"
#include "../Components/UI/UIInputFieldComponent.h"
#include "../Components/UI/UILayoutGroupComponents.h"
#include "../Components/UI/UIMaskComponent.h"
#include "../Components/UI/UIScrollViewComponent.h"
#include "../Components/UI/UISelectableComponent.h"
#include "../Components/UI/UISliderComponent.h"
#include "../Components/Motion/MotionPlayerComponent.h"
#include "../Object/GameObject/GameObject.h"
#include "../Runtime/API/RuntimeContext.h"
#include "../Runtime/Events/EventBus.h"
#include "../Scene/Runtime/Scene.h"
#include "../Scene/Services/IInputService.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace ReplayEngine::UI
{
    namespace
    {
        using Components::CanvasComponent;
        using Components::MotionPlayerComponent;
        using Components::RectTransformComponent;
        using Components::UIButtonComponent;
        using Components::UIImageComponent;
        using Components::UIInputFieldComponent;
        using Components::UIHorizontalLayoutGroupComponent;
        using Components::UIVerticalLayoutGroupComponent;
        using Components::UIGridLayoutGroupComponent;
        using Components::UIMaskComponent;
        using Components::UIScrollViewComponent;
        using Components::UISelectableComponent;
        using Components::UISliderComponent;

        constexpr int maximum_ui_depth = 64;

        bool NearlyEqual(float a, float b) noexcept
        {
            return std::fabs(a - b) <= 0.0001f;
        }

        DirectX::XMFLOAT4 ResolveRect(const RectTransformComponent& rect,
            const DirectX::XMFLOAT4& parent)
        {
            const float parent_min_x = parent.x;
            const float parent_min_y = parent.y;
            const float parent_w = parent.z;
            const float parent_h = parent.w;

            const float ax0 = parent_min_x + parent_w * rect.anchor_min.x;
            const float ay0 = parent_min_y + parent_h * rect.anchor_min.y;
            const float ax1 = parent_min_x + parent_w * rect.anchor_max.x;
            const float ay1 = parent_min_y + parent_h * rect.anchor_max.y;

            float min_x = 0.0f;
            float min_y = 0.0f;
            float size_x = 0.0f;
            float size_y = 0.0f;

            if (NearlyEqual(rect.anchor_min.x, rect.anchor_max.x))
            {
                size_x = rect.size_delta.x;
                const float center = ax0 + rect.anchored_position.x;
                min_x = center - size_x * rect.pivot.x;
            }
            else
            {
                min_x = ax0 + rect.anchored_position.x - rect.size_delta.x * rect.pivot.x;
                const float max_x = ax1 + rect.anchored_position.x +
                    rect.size_delta.x * (1.0f - rect.pivot.x);
                size_x = max_x - min_x;
            }

            if (NearlyEqual(rect.anchor_min.y, rect.anchor_max.y))
            {
                size_y = rect.size_delta.y;
                const float center = ay0 + rect.anchored_position.y;
                min_y = center - size_y * rect.pivot.y;
            }
            else
            {
                min_y = ay0 + rect.anchored_position.y - rect.size_delta.y * rect.pivot.y;
                const float max_y = ay1 + rect.anchored_position.y +
                    rect.size_delta.y * (1.0f - rect.pivot.y);
                size_y = max_y - min_y;
            }
            return { min_x, min_y, (std::max)(0.0f, size_x), (std::max)(0.0f, size_y) };
        }

        DirectX::XMFLOAT4X4 ResolveMatrix(const RectTransformComponent& rect,
            const DirectX::XMFLOAT4& resolved)
        {
            const float pivot_x = resolved.x + resolved.z * rect.pivot.x;
            const float pivot_y = resolved.y + resolved.w * rect.pivot.y;
            const DirectX::XMMATRIX matrix =
                DirectX::XMMatrixTranslation(-pivot_x, -pivot_y, 0.0f) *
                DirectX::XMMatrixScaling(rect.scale.x, rect.scale.y, 1.0f) *
                DirectX::XMMatrixRotationZ(DirectX::XMConvertToRadians(rect.rotation)) *
                DirectX::XMMatrixTranslation(pivot_x, pivot_y, 0.0f);
            DirectX::XMFLOAT4X4 stored{};
            DirectX::XMStoreFloat4x4(&stored, matrix);
            return stored;
        }

        struct ChildLayout final
        {
            Core::GameObject* object = nullptr;
            DirectX::XMFLOAT4 rect{};
        };

        std::vector<Core::GameObject*> ActiveRectChildren(Core::GameObject& object)
        {
            std::vector<Core::GameObject*> children;
            for (Core::GameObject* child : object.Children())
            {
                if (child == nullptr || child->PendingDestroy() ||
                    child->GetComponent<RectTransformComponent>() == nullptr) continue;
                children.push_back(child);
            }
            return children;
        }

        std::vector<ChildLayout> BuildLayoutOverrides(Core::GameObject& object,
            const DirectX::XMFLOAT4& parent)
        {
            std::vector<ChildLayout> result;
            const std::vector<Core::GameObject*> children = ActiveRectChildren(object);
            if (children.empty()) return result;

            if (const auto* layout = object.GetComponent<UIHorizontalLayoutGroupComponent>())
            {
                const float left = parent.x + (std::max)(0.0f, layout->padding.x);
                const float bottom = parent.y + (std::max)(0.0f, layout->padding.w);
                const float available_w = (std::max)(0.0f, parent.z -
                    (std::max)(0.0f, layout->padding.x) - (std::max)(0.0f, layout->padding.z));
                const float available_h = (std::max)(0.0f, parent.w -
                    (std::max)(0.0f, layout->padding.y) - (std::max)(0.0f, layout->padding.w));
                const float spacing = (std::max)(0.0f, layout->spacing);
                std::vector<DirectX::XMFLOAT4> natural;
                natural.reserve(children.size());
                float total_w = spacing * static_cast<float>(children.size() - 1u);
                for (Core::GameObject* child : children)
                {
                    DirectX::XMFLOAT4 r = ResolveRect(
                        *child->GetComponent<RectTransformComponent>(), parent);
                    natural.push_back(r);
                    if (!layout->control_child_width) total_w += r.z;
                }
                const float controlled_w = children.empty() ? 0.0f :
                    (std::max)(0.0f, available_w - spacing *
                        static_cast<float>(children.size() - 1u)) /
                        static_cast<float>(children.size());
                if (layout->control_child_width) total_w = available_w;
                float x = left;
                if (layout->alignment == UIHorizontalLayoutGroupComponent::Center)
                    x += (available_w - total_w) * 0.5f;
                else if (layout->alignment == UIHorizontalLayoutGroupComponent::End)
                    x += available_w - total_w;
                for (std::size_t i = 0; i < children.size(); ++i)
                {
                    const float width = layout->control_child_width ? controlled_w : natural[i].z;
                    const float height = layout->control_child_height ? available_h : natural[i].w;
                    const float y = bottom + (available_h - height) * 0.5f;
                    result.push_back({ children[i], { x, y, width, height } });
                    x += width + spacing;
                }
                return result;
            }

            if (const auto* layout = object.GetComponent<UIVerticalLayoutGroupComponent>())
            {
                const float left = parent.x + (std::max)(0.0f, layout->padding.x);
                const float bottom = parent.y + (std::max)(0.0f, layout->padding.w);
                const float available_w = (std::max)(0.0f, parent.z -
                    (std::max)(0.0f, layout->padding.x) - (std::max)(0.0f, layout->padding.z));
                const float available_h = (std::max)(0.0f, parent.w -
                    (std::max)(0.0f, layout->padding.y) - (std::max)(0.0f, layout->padding.w));
                const float spacing = (std::max)(0.0f, layout->spacing);
                std::vector<DirectX::XMFLOAT4> natural;
                natural.reserve(children.size());
                float total_h = spacing * static_cast<float>(children.size() - 1u);
                for (Core::GameObject* child : children)
                {
                    DirectX::XMFLOAT4 r = ResolveRect(
                        *child->GetComponent<RectTransformComponent>(), parent);
                    natural.push_back(r);
                    if (!layout->control_child_height) total_h += r.w;
                }
                const float controlled_h = children.empty() ? 0.0f :
                    (std::max)(0.0f, available_h - spacing *
                        static_cast<float>(children.size() - 1u)) /
                        static_cast<float>(children.size());
                if (layout->control_child_height) total_h = available_h;
                float cursor_top = bottom + available_h;
                if (layout->alignment == UIVerticalLayoutGroupComponent::Center)
                    cursor_top = bottom + (available_h + total_h) * 0.5f;
                else if (layout->alignment == UIVerticalLayoutGroupComponent::End)
                    cursor_top = bottom + total_h;
                for (std::size_t i = 0; i < children.size(); ++i)
                {
                    const float height = layout->control_child_height ? controlled_h : natural[i].w;
                    const float width = layout->control_child_width ? available_w : natural[i].z;
                    const float x = left + (available_w - width) * 0.5f;
                    const float y = cursor_top - height;
                    result.push_back({ children[i], { x, y, width, height } });
                    cursor_top -= height + spacing;
                }
                return result;
            }

            if (const auto* layout = object.GetComponent<UIGridLayoutGroupComponent>())
            {
                const float left = parent.x + (std::max)(0.0f, layout->padding.x);
                const float bottom = parent.y + (std::max)(0.0f, layout->padding.w);
                const float available_w = (std::max)(0.0f, parent.z -
                    (std::max)(0.0f, layout->padding.x) - (std::max)(0.0f, layout->padding.z));
                const float available_h = (std::max)(0.0f, parent.w -
                    (std::max)(0.0f, layout->padding.y) - (std::max)(0.0f, layout->padding.w));
                const float cell_w = (std::max)(1.0f, layout->cell_size.x);
                const float cell_h = (std::max)(1.0f, layout->cell_size.y);
                const float sx = (std::max)(0.0f, layout->spacing.x);
                const float sy = (std::max)(0.0f, layout->spacing.y);
                const int count = static_cast<int>(children.size());
                int columns = 1;
                int rows = 1;
                if (layout->constraint == UIGridLayoutGroupComponent::FixedRows)
                {
                    rows = (std::max)(1, layout->constraint_count);
                    columns = (std::max)(1, (count + rows - 1) / rows);
                }
                else if (layout->constraint == UIGridLayoutGroupComponent::FlexibleWidth)
                {
                    columns = (std::max)(1, static_cast<int>(
                        std::floor((available_w + sx) / (cell_w + sx))));
                    rows = (std::max)(1, (count + columns - 1) / columns);
                }
                else
                {
                    columns = (std::max)(1, layout->constraint_count);
                    rows = (std::max)(1, (count + columns - 1) / columns);
                }
                const float block_w = cell_w * static_cast<float>(columns) +
                    sx * static_cast<float>(columns - 1);
                const float block_h = cell_h * static_cast<float>(rows) +
                    sy * static_cast<float>(rows - 1);
                float block_x = left;
                if (layout->alignment == UIGridLayoutGroupComponent::Center)
                    block_x += (available_w - block_w) * 0.5f;
                else if (layout->alignment == UIGridLayoutGroupComponent::End)
                    block_x += available_w - block_w;
                float block_top = bottom + available_h;
                if (layout->alignment == UIGridLayoutGroupComponent::Center)
                    block_top = bottom + (available_h + block_h) * 0.5f;
                else if (layout->alignment == UIGridLayoutGroupComponent::End)
                    block_top = bottom + block_h;
                for (int index = 0; index < count; ++index)
                {
                    const int row = index / columns;
                    const int column = index % columns;
                    const float x = block_x + static_cast<float>(column) * (cell_w + sx);
                    const float y = block_top - cell_h - static_cast<float>(row) * (cell_h + sy);
                    result.push_back({ children[static_cast<std::size_t>(index)],
                        { x, y, cell_w, cell_h } });
                }
                return result;
            }
            return result;
        }

        RectTransformComponent* ResolveReferencedRect(Core::GameObject& owner,
            const Reflection::ComponentReference& reference) noexcept
        {
            if (!reference.IsAssigned() || owner.GetScene() == nullptr) return nullptr;
            Core::GameObject* target = owner.GetScene()->FindGameObjectByID(reference.owner);
            if (target == nullptr) return nullptr;
            Core::Component* component = target->FindComponentByStableID(reference.component);
            return dynamic_cast<RectTransformComponent*>(component);
        }

        UIImageComponent* ResolveReferencedImage(Core::GameObject& owner,
            const Reflection::ComponentReference& reference) noexcept
        {
            if (!reference.IsAssigned() || owner.GetScene() == nullptr) return nullptr;
            Core::GameObject* target = owner.GetScene()->FindGameObjectByID(reference.owner);
            if (target == nullptr) return nullptr;
            Core::Component* component = target->FindComponentByStableID(reference.component);
            return dynamic_cast<UIImageComponent*>(component);
        }

        void ApplyScrollToChild(Core::GameObject& object, Core::GameObject& child,
            const DirectX::XMFLOAT4& viewport, DirectX::XMFLOAT4& child_rect)
        {
            UIScrollViewComponent* scroll = object.GetComponent<UIScrollViewComponent>();
            if (scroll == nullptr) return;
            RectTransformComponent* content = ResolveReferencedRect(object, scroll->content);
            if (content == nullptr || content->Owner() != &child) return;

            const float overflow_x = (std::max)(0.0f, child_rect.z - viewport.z);
            const float overflow_y = (std::max)(0.0f, child_rect.w - viewport.w);
            scroll->horizontal_overflow = scroll->horizontal && overflow_x > 0.0001f;
            scroll->vertical_overflow = scroll->vertical && overflow_y > 0.0001f;
            scroll->horizontal_visible_ratio = child_rect.z > 0.0001f
                ? (std::min)(1.0f, viewport.z / child_rect.z) : 1.0f;
            scroll->vertical_visible_ratio = child_rect.w > 0.0001f
                ? (std::min)(1.0f, viewport.w / child_rect.w) : 1.0f;

            if (!scroll->horizontal || (scroll->clamp_when_content_fits && overflow_x <= 0.0f))
                scroll->scroll_offset.x = 0.0f;
            else scroll->scroll_offset.x =
                (std::min)((std::max)(scroll->scroll_offset.x, 0.0f), overflow_x);
            if (!scroll->vertical || (scroll->clamp_when_content_fits && overflow_y <= 0.0f))
                scroll->scroll_offset.y = 0.0f;
            else scroll->scroll_offset.y =
                (std::min)((std::max)(scroll->scroll_offset.y, 0.0f), overflow_y);

            scroll->horizontal_normalized = overflow_x > 0.0f
                ? scroll->scroll_offset.x / overflow_x : 0.0f;
            scroll->vertical_normalized = overflow_y > 0.0f
                ? scroll->scroll_offset.y / overflow_y : 0.0f;
            child_rect.x -= scroll->scroll_offset.x;
            child_rect.y += scroll->scroll_offset.y;
        }

        void ResolveChildren(Core::GameObject& object,
            const DirectX::XMFLOAT4& parent_rect, int depth,
            const DirectX::XMFLOAT4* forced_rect = nullptr)
        {
            if (depth > maximum_ui_depth || object.PendingDestroy()) return;

            DirectX::XMFLOAT4 current_parent = parent_rect;
            if (RectTransformComponent* rect = object.GetComponent<RectTransformComponent>())
            {
                const DirectX::XMFLOAT4 resolved = forced_rect != nullptr
                    ? *forced_rect : ResolveRect(*rect, parent_rect);
                rect->SetResolvedRect(resolved);
                rect->SetResolvedMatrix(ResolveMatrix(*rect, resolved));
                current_parent = resolved;
            }

            std::vector<ChildLayout> overrides = BuildLayoutOverrides(object, current_parent);
            const std::vector<Core::GameObject*> children = object.Children();
            for (Core::GameObject* child : children)
            {
                if (child == nullptr || child->PendingDestroy()) continue;
                const DirectX::XMFLOAT4* forced = nullptr;
                DirectX::XMFLOAT4 effective{};
                for (const ChildLayout& entry : overrides)
                {
                    if (entry.object == child)
                    {
                        effective = entry.rect;
                        forced = &effective;
                        break;
                    }
                }
                if (RectTransformComponent* child_rect = child->GetComponent<RectTransformComponent>())
                {
                    if (forced == nullptr)
                    {
                        effective = ResolveRect(*child_rect, current_parent);
                        forced = &effective;
                    }
                    ApplyScrollToChild(object, *child, current_parent, effective);
                }
                ResolveChildren(*child, current_parent, depth + 1, forced);
            }
        }

        void GatherCanvases(Scene::Scene& scene, std::vector<Core::GameObject*>& canvases)
        {
            canvases.clear();
            for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
            {
                Core::GameObject* object = scene.GameObjectAt(index);
                if (object == nullptr || object->PendingDestroy()) continue;
                if (object->GetComponent<CanvasComponent>() != nullptr) canvases.push_back(object);
            }
            std::stable_sort(canvases.begin(), canvases.end(),
                [](const Core::GameObject* lhs, const Core::GameObject* rhs)
                {
                    const CanvasComponent* a = lhs != nullptr ? lhs->GetComponent<CanvasComponent>() : nullptr;
                    const CanvasComponent* b = rhs != nullptr ? rhs->GetComponent<CanvasComponent>() : nullptr;
                    return (a != nullptr ? a->sort_order : 0) < (b != nullptr ? b->sort_order : 0);
                });
        }

        bool HitTest(const RectTransformComponent& rect, float x, float y) noexcept
        {
            const DirectX::XMFLOAT4 resolved = rect.ResolvedRect();
            const DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&rect.ResolvedMatrix());
            DirectX::XMVECTOR determinant{};
            const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(&determinant, matrix);
            const DirectX::XMVECTOR point = DirectX::XMVectorSet(x, y, 0.0f, 1.0f);
            const DirectX::XMVECTOR local = DirectX::XMVector2Transform(point, inverse);
            const float lx = DirectX::XMVectorGetX(local);
            const float ly = DirectX::XMVectorGetY(local);
            return lx >= resolved.x && lx <= resolved.x + resolved.z &&
                ly >= resolved.y && ly <= resolved.y + resolved.w;
        }

        bool VisibleThroughAncestorMasks(const Core::GameObject& object, float x, float y) noexcept
        {
            for (const Core::GameObject* current = object.Parent(); current != nullptr;
                current = current->Parent())
            {
                const UIMaskComponent* mask = current->GetComponent<UIMaskComponent>();
                const RectTransformComponent* rect = current->GetComponent<RectTransformComponent>();
                if (mask != nullptr && rect != nullptr && mask->enabled_mask &&
                    mask->mask_mode == UIMaskComponent::Rectangle && !HitTest(*rect, x, y))
                    return false;
            }
            return true;
        }

        bool PointerInsideDescendantScroll(const Core::GameObject& object,
            float x, float y, int depth = 0) noexcept
        {
            if (depth > maximum_ui_depth) return false;
            for (Core::GameObject* child : object.Children())
            {
                if (child == nullptr || child->PendingDestroy()) continue;
                const UIScrollViewComponent* scroll = child->GetComponent<UIScrollViewComponent>();
                const RectTransformComponent* rect = child->GetComponent<RectTransformComponent>();
                if (scroll != nullptr && rect != nullptr && scroll->ActiveInHierarchy() &&
                    HitTest(*rect, x, y) && VisibleThroughAncestorMasks(*child, x, y))
                {
                    return true;
                }
                if (PointerInsideDescendantScroll(*child, x, y, depth + 1)) return true;
            }
            return false;
        }

        UIImageComponent* ResolveTargetImage(UIButtonComponent& button)
        {
            Core::GameObject* owner = button.Owner();
            if (owner == nullptr) return nullptr;
            if (button.target_image.IsAssigned())
            {
                if (Scene::Scene* scene = owner->GetScene())
                {
                    if (Core::GameObject* target_owner = scene->FindGameObjectByID(button.target_image.owner))
                    {
                        if (Core::Component* component = target_owner->FindComponentByStableID(button.target_image.component))
                            if (auto* image = dynamic_cast<UIImageComponent*>(component)) return image;
                    }
                }
            }
            return owner->GetComponent<UIImageComponent>();
        }

        void ApplyButtonVisual(UIButtonComponent& button)
        {
            UIImageComponent* image = ResolveTargetImage(button);
            if (image == nullptr) return;
            switch (button.state)
            {
            case UIButtonComponent::Hover: image->color = button.hover_color; break;
            case UIButtonComponent::Pressed: image->color = button.pressed_color; break;
            case UIButtonComponent::Disabled: image->color = button.disabled_color; break;
            default: image->color = button.normal_color; break;
            }
        }

        const Reflection::AssetReference* MotionForState(const UIButtonComponent& button,
            int state) noexcept
        {
            switch (state)
            {
            case UIButtonComponent::Hover: return &button.hover_motion;
            case UIButtonComponent::Pressed: return &button.pressed_motion;
            case UIButtonComponent::Disabled: return &button.disabled_motion;
            default: return &button.normal_motion;
            }
        }

        void PlayButtonMotion(UIButtonComponent& button, int state)
        {
            const Reflection::AssetReference* motion = MotionForState(button, state);
            if (motion == nullptr || !motion->IsAssigned()) return;
            Core::GameObject* owner = button.Owner();
            if (owner == nullptr) return;
            MotionPlayerComponent* player = owner->GetComponent<MotionPlayerComponent>();
            if (player == nullptr) player = owner->AddComponent<MotionPlayerComponent>();
            if (player == nullptr) return;
            player->motion = *motion;
            player->play_on_start = true;
            player->loop = false;
            player->wrap_mode = MotionPlayerComponent::ClampForever;
            player->auto_stop_on_end = false;
            player->blend_in_seconds = (std::max)(0.0f, button.state_blend_seconds);
            player->weight = 1.0f;
            player->PlayFrom(0.0f);
        }

        bool PointInScrollThumb(const UIScrollViewComponent& scroll,
            const RectTransformComponent& rect, float x, float y, bool vertical) noexcept
        {
            const DirectX::XMFLOAT4 viewport = rect.ResolvedRect();
            const float width = (std::max)(2.0f, scroll.scrollbar_width);
            if (vertical && scroll.vertical_overflow)
            {
                const float track = viewport.w;
                const float thumb = (std::max)(width, track * scroll.vertical_visible_ratio);
                const float travel = (std::max)(0.0f, track - thumb);
                const float bottom = viewport.y + travel * (1.0f - scroll.vertical_normalized);
                return x >= viewport.x + viewport.z - width && x <= viewport.x + viewport.z &&
                    y >= bottom && y <= bottom + thumb;
            }
            if (!vertical && scroll.horizontal_overflow)
            {
                const float track = viewport.z;
                const float thumb = (std::max)(width, track * scroll.horizontal_visible_ratio);
                const float travel = (std::max)(0.0f, track - thumb);
                const float left = viewport.x + travel * scroll.horizontal_normalized;
                return y >= viewport.y && y <= viewport.y + width &&
                    x >= left && x <= left + thumb;
            }
            return false;
        }

        void UpdateScrollInput(Core::GameObject& object, float mouse_x, float mouse_y,
            bool mouse_down, bool mouse_pressed, bool mouse_released, float mouse_wheel,
            bool input_captured)
        {
            UIScrollViewComponent* scroll = object.GetComponent<UIScrollViewComponent>();
            RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
            if (scroll == nullptr || rect == nullptr || !scroll->ActiveInHierarchy()) return;
            const bool hovered = HitTest(*rect, mouse_x, mouse_y) &&
                VisibleThroughAncestorMasks(object, mouse_x, mouse_y);
            // Nested ScrollView は最も内側の View が pointer 操作を所有する。
            // 親子が同時に drag/wheel すると両方の offset が動くため、親は開始しない。
            const bool descendant_scroll_hovered = hovered &&
                PointerInsideDescendantScroll(object, mouse_x, mouse_y);
            if (!input_captured && hovered && !descendant_scroll_hovered && mouse_wheel != 0.0f)
            {
                if (scroll->vertical && scroll->vertical_overflow)
                    scroll->scroll_offset.y -= mouse_wheel * scroll->scroll_sensitivity;
                else if (scroll->horizontal && scroll->horizontal_overflow)
                    scroll->scroll_offset.x -= mouse_wheel * scroll->scroll_sensitivity;
            }
            if (!input_captured && hovered && !descendant_scroll_hovered && mouse_pressed)
            {
                if (UISelectableComponent* selectable = object.GetComponent<UISelectableComponent>())
                    if (Scene::Scene* scene = object.GetScene()) UIFocusManager::SetFocus(*scene, selectable);
                scroll->dragging_vertical_thumb = PointInScrollThumb(*scroll, *rect,
                    mouse_x, mouse_y, true);
                scroll->dragging_horizontal_thumb = !scroll->dragging_vertical_thumb &&
                    PointInScrollThumb(*scroll, *rect, mouse_x, mouse_y, false);
                scroll->dragging_content = !scroll->dragging_vertical_thumb &&
                    !scroll->dragging_horizontal_thumb;
                scroll->drag_last_pointer = { mouse_x, mouse_y };
            }
            if (mouse_down && (scroll->dragging_content || scroll->dragging_vertical_thumb ||
                scroll->dragging_horizontal_thumb))
            {
                const float dx = mouse_x - scroll->drag_last_pointer.x;
                const float dy = mouse_y - scroll->drag_last_pointer.y;
                const DirectX::XMFLOAT4 viewport = rect->ResolvedRect();
                if (scroll->dragging_content)
                {
                    if (scroll->horizontal) scroll->scroll_offset.x -= dx;
                    if (scroll->vertical) scroll->scroll_offset.y += dy;
                }
                if (scroll->dragging_vertical_thumb && scroll->vertical_overflow)
                {
                    const float ratio = (std::max)(0.0001f, 1.0f - scroll->vertical_visible_ratio);
                    scroll->vertical_normalized -= dy / (std::max)(1.0f, viewport.w * ratio);
                    scroll->vertical_normalized = (std::min)((std::max)(scroll->vertical_normalized, 0.0f), 1.0f);
                    RectTransformComponent* content = ResolveReferencedRect(object, scroll->content);
                    if (content != nullptr)
                    {
                        const float overflow = (std::max)(0.0f, content->ResolvedRect().w - viewport.w);
                        scroll->scroll_offset.y = overflow * scroll->vertical_normalized;
                    }
                }
                if (scroll->dragging_horizontal_thumb && scroll->horizontal_overflow)
                {
                    const float ratio = (std::max)(0.0001f, 1.0f - scroll->horizontal_visible_ratio);
                    scroll->horizontal_normalized += dx / (std::max)(1.0f, viewport.z * ratio);
                    scroll->horizontal_normalized = (std::min)((std::max)(scroll->horizontal_normalized, 0.0f), 1.0f);
                    RectTransformComponent* content = ResolveReferencedRect(object, scroll->content);
                    if (content != nullptr)
                    {
                        const float overflow = (std::max)(0.0f, content->ResolvedRect().z - viewport.z);
                        scroll->scroll_offset.x = overflow * scroll->horizontal_normalized;
                    }
                }
                scroll->drag_last_pointer = { mouse_x, mouse_y };
            }
            if (mouse_released || !mouse_down)
            {
                scroll->dragging_content = false;
                scroll->dragging_vertical_thumb = false;
                scroll->dragging_horizontal_thumb = false;
            }
        }

        void ApplySliderVisual(Core::GameObject& object, UISliderComponent& slider)
        {
            const float normalized = slider.NormalizedValue();
            UIImageComponent* fill = ResolveReferencedImage(object, slider.fill_image);
            if (fill == nullptr && !slider.fill_image.IsAssigned())
                fill = object.GetComponent<UIImageComponent>();
            if (fill != nullptr)
            {
                fill->fill_amount = normalized;
                fill->fill_method = slider.direction == UISliderComponent::LeftToRight ||
                    slider.direction == UISliderComponent::RightToLeft
                    ? UIImageComponent::Horizontal : UIImageComponent::Vertical;
                fill->fill_reverse = slider.direction == UISliderComponent::RightToLeft ||
                    slider.direction == UISliderComponent::TopToBottom;
            }

            RectTransformComponent* handle = ResolveReferencedRect(object, slider.handle_rect);
            if (handle == nullptr) return;
            float position = normalized;
            if (slider.direction == UISliderComponent::RightToLeft ||
                slider.direction == UISliderComponent::TopToBottom)
                position = 1.0f - position;
            if (slider.direction == UISliderComponent::LeftToRight ||
                slider.direction == UISliderComponent::RightToLeft)
            {
                handle->anchor_min.x = position;
                handle->anchor_max.x = position;
            }
            else
            {
                handle->anchor_min.y = position;
                handle->anchor_max.y = position;
            }
        }

        void PublishSliderChange(UISliderComponent& slider)
        {
            if (std::fabs(slider.value - slider.last_emitted_value) <= 0.000001f) return;
            slider.last_emitted_value = slider.value;
            Runtime::RuntimeContext* runtime = slider.GetScene() != nullptr
                ? slider.GetScene()->Services().Runtime() : nullptr;
            Core::GameObject* owner = slider.Owner();
            if (runtime == nullptr || owner == nullptr) return;

            Runtime::EventRecord record;
            record.type = Runtime::EngineEvents::SliderValueChanged;
            record.type_name = "SliderValueChanged";
            record.source = runtime->Resolver().MakeHandle(owner);
            record.frame_index = runtime->FrameIndex();
            record.payload.Set("value", Reflection::PropertyValue::MakeFloat(slider.value));
            record.payload.Set("normalized",
                Reflection::PropertyValue::MakeFloat(slider.NormalizedValue()));
            record.payload.Set("slider_component",
                Reflection::PropertyValue::MakeUInt64(slider.StableID()));
            runtime->Events().Publish(std::move(record));
        }

        void UpdateSliderInput(Core::GameObject& object, float mouse_x, float mouse_y,
            bool mouse_down, bool mouse_pressed, bool mouse_released, bool input_captured)
        {
            UISliderComponent* slider = object.GetComponent<UISliderComponent>();
            RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
            if (slider == nullptr || rect == nullptr) return;

            UISelectableComponent* selectable = object.GetComponent<UISelectableComponent>();
            if (selectable != nullptr) selectable->interactable = slider->interactable;
            const bool active = slider->interactable && slider->ActiveInHierarchy();
            const bool hovered = active && HitTest(*rect, mouse_x, mouse_y) &&
                VisibleThroughAncestorMasks(object, mouse_x, mouse_y);
            if (!input_captured && hovered && mouse_pressed)
            {
                if (selectable != nullptr && object.GetScene() != nullptr)
                    UIFocusManager::SetFocus(*object.GetScene(), selectable);
                slider->dragging = true;
            }

            // 押下開始時に所有権を得たdragは、ポインターが矩形外へ出ても継続する。
            // 毎フレームのhover/captureで止めるとSliderだけ操作感が途切れる。
            if (active && slider->dragging && mouse_down)
            {
                const DirectX::XMFLOAT4 bounds = rect->ResolvedRect();
                float normalized = 0.0f;
                if (slider->direction == UISliderComponent::LeftToRight ||
                    slider->direction == UISliderComponent::RightToLeft)
                    normalized = (mouse_x - bounds.x) / (std::max)(bounds.z, 0.0001f);
                else
                    normalized = (mouse_y - bounds.y) / (std::max)(bounds.w, 0.0001f);
                normalized = (std::min)((std::max)(normalized, 0.0f), 1.0f);
                if (slider->direction == UISliderComponent::RightToLeft ||
                    slider->direction == UISliderComponent::TopToBottom)
                    normalized = 1.0f - normalized;
                const float low = (std::min)(slider->minimum, slider->maximum);
                const float high = (std::max)(slider->minimum, slider->maximum);
                slider->SetValue(low + normalized * (high - low));
            }

            const Scene::IInputService* input = object.GetScene() != nullptr
                ? object.GetScene()->Services().Input() : nullptr;
            if (!input_captured && active && selectable != nullptr && selectable->focused &&
                input != nullptr)
            {
                int direction = 0;
                if (slider->direction == UISliderComponent::LeftToRight ||
                    slider->direction == UISliderComponent::RightToLeft)
                {
                    if (input->Pressed("NavigateLeft")) direction = -1;
                    else if (input->Pressed("NavigateRight")) direction = 1;
                    if (slider->direction == UISliderComponent::RightToLeft) direction = -direction;
                }
                else
                {
                    if (input->Pressed("NavigateDown")) direction = -1;
                    else if (input->Pressed("NavigateUp")) direction = 1;
                    if (slider->direction == UISliderComponent::TopToBottom) direction = -direction;
                }
                if (direction != 0)
                    slider->SetValue(slider->value +
                        static_cast<float>(direction) * slider->keyboard_step);
            }

            if (mouse_released || !mouse_down) slider->dragging = false;
            ApplySliderVisual(object, *slider);
            PublishSliderChange(*slider);
        }

        void UpdateInteractiveTree(Core::GameObject& object, float mouse_x, float mouse_y,
            bool mouse_down, bool mouse_pressed, bool mouse_released, float mouse_wheel,
            bool input_captured, bool play_state_motions, int depth)
        {
            if (depth > maximum_ui_depth || object.PendingDestroy()) return;

            UpdateScrollInput(object, mouse_x, mouse_y, mouse_down, mouse_pressed,
                mouse_released, mouse_wheel, input_captured);
            UpdateSliderInput(object, mouse_x, mouse_y, mouse_down, mouse_pressed,
                mouse_released, input_captured);

            if (!input_captured && mouse_pressed)
            {
                if (auto* input = object.GetComponent<UIInputFieldComponent>())
                {
                    if (RectTransformComponent* rect = object.GetComponent<RectTransformComponent>())
                    {
                        if (HitTest(*rect, mouse_x, mouse_y) &&
                            VisibleThroughAncestorMasks(object, mouse_x, mouse_y))
                        {
                            if (UISelectableComponent* selectable = object.GetComponent<UISelectableComponent>())
                                if (Scene::Scene* scene = object.GetScene()) UIFocusManager::SetFocus(*scene, selectable);
                            input->caret_index = input->CharacterCount();
                            input->selection_anchor = input->caret_index;
                            input->RefreshVisual();
                        }
                    }
                }
            }

            if (auto* button = object.GetComponent<UIButtonComponent>())
            {
                if (UISelectableComponent* selectable = object.GetComponent<UISelectableComponent>())
                {
                    // Legacy Button fields remain the serialized source for old scenes.
                    selectable->interactable = button->interactable;
                    selectable->navigation_enabled = button->navigation_enabled;
                    selectable->navigation_order = button->navigation_order;
                }
                const int previous_state = button->state;
                int next_state = UIButtonComponent::Normal;
                bool activated = false;
                if (!button->interactable || !button->ActiveInHierarchy())
                    next_state = UIButtonComponent::Disabled;
                else if (!input_captured)
                {
                    if (const auto* rect = object.GetComponent<RectTransformComponent>())
                    {
                        const bool hovered = HitTest(*rect, mouse_x, mouse_y) &&
                            VisibleThroughAncestorMasks(object, mouse_x, mouse_y);
                        Scene::Scene* scene = button->GetScene();
                        const Scene::IInputService* input = scene != nullptr ? scene->Services().Input() : nullptr;
                        UISelectableComponent* selectable = object.GetComponent<UISelectableComponent>();
                        if (hovered && selectable != nullptr && button->navigation_enabled && scene != nullptr)
                            UIFocusManager::SetFocus(*scene, selectable);
                        button->focused = selectable != nullptr && selectable->focused;
                        const bool submit_down = button->focused && input != nullptr && input->Held("UISubmit");
                        const bool submit_released = button->focused && input != nullptr &&
                            input->Released("UISubmit");
                        if ((hovered && mouse_down) || submit_down) next_state = UIButtonComponent::Pressed;
                        else if (hovered || button->focused) next_state = UIButtonComponent::Hover;
                        activated = previous_state == UIButtonComponent::Pressed &&
                            ((hovered && mouse_released) || submit_released);
                    }
                }

                button->state = next_state;
                ApplyButtonVisual(*button);
                if (previous_state != next_state)
                {
                    if (Runtime::RuntimeContext* runtime = button->GetScene() != nullptr
                        ? button->GetScene()->Services().Runtime() : nullptr)
                    {
                        if (Core::GameObject* owner = button->Owner())
                        {
                            Runtime::EventRecord record;
                            record.type = Runtime::EngineEvents::ButtonStateChanged;
                            record.type_name = "ButtonStateChanged";
                            record.source = runtime->Resolver().MakeHandle(owner);
                            record.frame_index = runtime->FrameIndex();
                            record.payload.Set("previous_state", Reflection::PropertyValue::MakeInt(previous_state));
                            record.payload.Set("state", Reflection::PropertyValue::MakeInt(next_state));
                            record.payload.Set("button_component", Reflection::PropertyValue::MakeUInt64(button->StableID()));
                            runtime->Events().Publish(std::move(record));
                        }
                    }
                }
                if (activated)
                {
                    if (Runtime::RuntimeContext* runtime = button->GetScene() != nullptr
                        ? button->GetScene()->Services().Runtime() : nullptr)
                    {
                        if (Core::GameObject* owner = button->Owner())
                        {
                            Runtime::EventRecord record;
                            record.type = Runtime::EngineEvents::ButtonClicked;
                            record.type_name = "ButtonClicked";
                            record.source = runtime->Resolver().MakeHandle(owner);
                            record.frame_index = runtime->FrameIndex();
                            record.payload.Set("button_component",
                                Reflection::PropertyValue::MakeUInt64(button->StableID()));
                            runtime->Events().Publish(std::move(record));
                        }
                    }
                }
                if (play_state_motions && previous_state != next_state) PlayButtonMotion(*button, next_state);
            }

            for (Core::GameObject* child : object.Children())
                if (child != nullptr) UpdateInteractiveTree(*child, mouse_x, mouse_y,
                    mouse_down, mouse_pressed, mouse_released, mouse_wheel,
                    input_captured, play_state_motions, depth + 1);
        }

        bool IsDescendantOf(const Core::GameObject* object, const Core::GameObject* ancestor) noexcept
        {
            for (const Core::GameObject* current = object; current != nullptr; current = current->Parent())
                if (current == ancestor) return true;
            return false;
        }

        bool FollowFocusedScroll(Scene::Scene& scene)
        {
            UISelectableComponent* focus = UIFocusManager::Current(scene);
            Core::GameObject* focused_object = focus != nullptr ? focus->Owner() : nullptr;
            if (focused_object == nullptr) return false;
            RectTransformComponent* focused_rect = focused_object->GetComponent<RectTransformComponent>();
            if (focused_rect == nullptr) return false;
            bool changed = false;
            for (Core::GameObject* parent = focused_object->Parent(); parent != nullptr; parent = parent->Parent())
            {
                UIScrollViewComponent* scroll = parent->GetComponent<UIScrollViewComponent>();
                RectTransformComponent* viewport_rect = parent->GetComponent<RectTransformComponent>();
                if (scroll == nullptr || viewport_rect == nullptr) continue;
                RectTransformComponent* content = ResolveReferencedRect(*parent, scroll->content);
                if (content == nullptr || !IsDescendantOf(focused_object, content->Owner())) continue;
                const DirectX::XMFLOAT4 item = focused_rect->ResolvedRect();
                const DirectX::XMFLOAT4 viewport = viewport_rect->ResolvedRect();
                if (scroll->vertical)
                {
                    if (item.y < viewport.y)
                    {
                        scroll->scroll_offset.y += viewport.y - item.y;
                        changed = true;
                    }
                    else if (item.y + item.w > viewport.y + viewport.w)
                    {
                        scroll->scroll_offset.y -= (item.y + item.w) - (viewport.y + viewport.w);
                        changed = true;
                    }
                }
                if (scroll->horizontal)
                {
                    if (item.x < viewport.x)
                    {
                        scroll->scroll_offset.x -= viewport.x - item.x;
                        changed = true;
                    }
                    else if (item.x + item.z > viewport.x + viewport.z)
                    {
                        scroll->scroll_offset.x += (item.x + item.z) - (viewport.x + viewport.z);
                        changed = true;
                    }
                }
            }
            return changed;
        }
    }

    float UILayout::CanvasScale(const CanvasComponent& canvas,
        float screen_width, float screen_height) noexcept
    {
        if (canvas.scale_mode == CanvasComponent::ConstantPixelSize) return 1.0f;
        const float ref_w = canvas.reference_resolution.x > 0.0f ? canvas.reference_resolution.x : 1920.0f;
        const float ref_h = canvas.reference_resolution.y > 0.0f ? canvas.reference_resolution.y : 1080.0f;
        const float log_w = std::log2((std::max)(1.0f, screen_width) / ref_w);
        const float log_h = std::log2((std::max)(1.0f, screen_height) / ref_h);
        const float match = (std::min)((std::max)(canvas.match_width_or_height, 0.0f), 1.0f);
        return std::pow(2.0f, log_w + (log_h - log_w) * match);
    }

    void UILayout::Resolve(Scene::Scene& scene, float screen_width, float screen_height)
    {
        std::vector<Core::GameObject*> canvases;
        GatherCanvases(scene, canvases);
        for (Core::GameObject* canvas : canvases)
            if (canvas != nullptr) ResolveCanvas(*canvas, screen_width, screen_height);
    }

    void UILayout::ResolveCanvas(Core::GameObject& canvas_object,
        float screen_width, float screen_height)
    {
        CanvasComponent* canvas = canvas_object.GetComponent<CanvasComponent>();
        if (canvas == nullptr) return;
        const float scale = CanvasScale(*canvas, screen_width, screen_height);
        const float safe_scale = scale > 0.0001f ? scale : 1.0f;
        DirectX::XMFLOAT4 root_rect{ 0.0f, 0.0f,
            screen_width / safe_scale, screen_height / safe_scale };
        if (RectTransformComponent* rect = canvas_object.GetComponent<RectTransformComponent>())
        {
            rect->SetResolvedRect(root_rect);
            rect->SetResolvedMatrix(ResolveMatrix(*rect, root_rect));
        }
        const std::vector<ChildLayout> overrides = BuildLayoutOverrides(canvas_object, root_rect);
        for (Core::GameObject* child : canvas_object.Children())
        {
            if (child == nullptr) continue;
            const DirectX::XMFLOAT4* forced = nullptr;
            DirectX::XMFLOAT4 effective{};
            for (const ChildLayout& entry : overrides)
                if (entry.object == child) { effective = entry.rect; forced = &effective; break; }
            if (RectTransformComponent* child_rect = child->GetComponent<RectTransformComponent>())
            {
                if (forced == nullptr) { effective = ResolveRect(*child_rect, root_rect); forced = &effective; }
                ApplyScrollToChild(canvas_object, *child, root_rect, effective);
            }
            ResolveChildren(*child, root_rect, 1, forced);
        }
    }

    void UILayout::UpdateButtons(Scene::Scene& scene,
        float screen_width, float screen_height, float mouse_x, float mouse_y,
        bool mouse_down, bool mouse_pressed, bool mouse_released, float mouse_wheel,
        bool input_captured, bool play_state_motions)
    {
        if (!input_captured)
        {
            if (const Scene::IInputService* input = scene.Services().Input())
                UIFocusManager::Update(scene, *input);
        }

        std::vector<Core::GameObject*> canvases;
        GatherCanvases(scene, canvases);
        // Reverse sort order for hit testing so the visually topmost Canvas owns pointer focus.
        for (auto it = canvases.rbegin(); it != canvases.rend(); ++it)
        {
            Core::GameObject* canvas_object = *it;
            if (canvas_object == nullptr) continue;
            const CanvasComponent* canvas = canvas_object->GetComponent<CanvasComponent>();
            if (canvas == nullptr) continue;
            const float scale = CanvasScale(*canvas, screen_width, screen_height);
            const float safe_scale = scale > 0.0001f ? scale : 1.0f;
            UpdateInteractiveTree(*canvas_object, mouse_x / safe_scale, mouse_y / safe_scale,
                mouse_down, mouse_pressed, mouse_released, mouse_wheel,
                input_captured, play_state_motions, 0);
        }
        if (FollowFocusedScroll(scene)) Resolve(scene, screen_width, screen_height);
        UIInputFieldSystem::Refresh(scene);
    }
}
