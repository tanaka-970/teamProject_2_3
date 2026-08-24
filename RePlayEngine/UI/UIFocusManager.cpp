#include "UIFocusManager.h"

#include "../Components/UI/CanvasComponent.h"
#include "../Components/UI/RectTransformComponent.h"
#include "../Components/UI/UIButtonComponent.h"
#include "../Components/UI/UIInputFieldComponent.h"
#include "../Components/UI/UISelectableComponent.h"
#include "../Components/UI/UISliderComponent.h"
#include "../Object/GameObject/GameObject.h"
#include "../Scene/Runtime/Scene.h"
#include "../Scene/Services/IInputService.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ReplayEngine::UI
{
    namespace
    {
        using Components::CanvasComponent;
        using Components::RectTransformComponent;
        using Components::UIButtonComponent;
        using Components::UIInputFieldComponent;
        using Components::UISelectableComponent;
        using Components::UISliderComponent;

        Core::GameObject* CanvasRoot(Core::GameObject* object) noexcept
        {
            Core::GameObject* result = nullptr;
            for (Core::GameObject* current = object; current != nullptr; current = current->Parent())
            {
                if (current->GetComponent<CanvasComponent>() != nullptr) result = current;
            }
            return result;
        }

        bool Usable(const UISelectableComponent* selectable) noexcept
        {
            return selectable != nullptr && selectable->ActiveInHierarchy() &&
                selectable->interactable && selectable->navigation_enabled &&
                selectable->Owner() != nullptr &&
                selectable->Owner()->GetComponent<RectTransformComponent>() != nullptr;
        }

        std::vector<UISelectableComponent*> Gather(Scene::Scene& scene,
            Core::GameObject* canvas_filter)
        {
            std::vector<UISelectableComponent*> result;
            for (std::size_t i = 0; i < scene.GameObjectCount(); ++i)
            {
                Core::GameObject* object = scene.GameObjectAt(i);
                if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy())
                    continue;
                UISelectableComponent* selectable = object->GetComponent<UISelectableComponent>();
                if (!Usable(selectable)) continue;
                if (canvas_filter != nullptr && CanvasRoot(object) != canvas_filter) continue;
                result.push_back(selectable);
            }
            std::stable_sort(result.begin(), result.end(),
                [](const UISelectableComponent* lhs, const UISelectableComponent* rhs)
                { return lhs->navigation_order < rhs->navigation_order; });
            return result;
        }

        UISelectableComponent* ResolveReference(Scene::Scene& scene,
            const Reflection::ComponentReference& reference) noexcept
        {
            if (!reference.IsAssigned()) return nullptr;
            Core::GameObject* owner = scene.FindGameObjectByID(reference.owner);
            if (owner == nullptr) return nullptr;
            Core::Component* component = owner->FindComponentByStableID(reference.component);
            auto* selectable = dynamic_cast<UISelectableComponent*>(component);
            return Usable(selectable) ? selectable : nullptr;
        }

        const Reflection::ComponentReference& ReferenceFor(
            const UISelectableComponent& selectable, UIFocusManager::Direction direction)
        {
            switch (direction)
            {
            case UIFocusManager::Direction::Up: return selectable.navigate_up;
            case UIFocusManager::Direction::Down: return selectable.navigate_down;
            case UIFocusManager::Direction::Left: return selectable.navigate_left;
            default: return selectable.navigate_right;
            }
        }
    }

    Components::UISelectableComponent* UIFocusManager::Current(Scene::Scene& scene) noexcept
    {
        UISelectableComponent* found = nullptr;
        for (std::size_t i = 0; i < scene.GameObjectCount(); ++i)
        {
            Core::GameObject* object = scene.GameObjectAt(i);
            if (object == nullptr || object->PendingDestroy()) continue;
            UISelectableComponent* selectable = object->GetComponent<UISelectableComponent>();
            if (selectable == nullptr || !selectable->focused) continue;
            if (found == nullptr && Usable(selectable)) found = selectable;
            else selectable->focused = false;
        }
        return found;
    }

    void UIFocusManager::SetFocus(Scene::Scene& scene,
        UISelectableComponent* target) noexcept
    {
        for (std::size_t i = 0; i < scene.GameObjectCount(); ++i)
        {
            Core::GameObject* object = scene.GameObjectAt(i);
            if (object == nullptr) continue;
            if (UISelectableComponent* selectable = object->GetComponent<UISelectableComponent>())
                selectable->focused = selectable == target && Usable(selectable);
            if (UIButtonComponent* button = object->GetComponent<UIButtonComponent>())
                button->focused = target != nullptr && target->Owner() == object;
        }
    }

    UISelectableComponent* UIFocusManager::FindInDirection(Scene::Scene& scene,
        const UISelectableComponent& from, Direction direction) noexcept
    {
        UISelectableComponent* manual = ResolveReference(scene, ReferenceFor(from, direction));
        if (manual != nullptr) return manual;
        Core::GameObject* owner = from.Owner();
        const RectTransformComponent* from_rect = owner != nullptr
            ? owner->GetComponent<RectTransformComponent>() : nullptr;
        if (from_rect == nullptr) return nullptr;

        const DirectX::XMFLOAT4 a = from_rect->ResolvedRect();
        const float ax = a.x + a.z * 0.5f;
        const float ay = a.y + a.w * 0.5f;
        const float bias = (std::max)(0.0f, from.navigation_bias);
        UISelectableComponent* best = nullptr;
        float best_score = (std::numeric_limits<float>::max)();
        for (UISelectableComponent* candidate : Gather(scene, CanvasRoot(owner)))
        {
            if (candidate == &from || candidate->Owner() == nullptr) continue;
            const RectTransformComponent* rect =
                candidate->Owner()->GetComponent<RectTransformComponent>();
            if (rect == nullptr) continue;
            const DirectX::XMFLOAT4 b = rect->ResolvedRect();
            const float dx = b.x + b.z * 0.5f - ax;
            const float dy = b.y + b.w * 0.5f - ay;
            float forward = 0.0f, side = 0.0f;
            switch (direction)
            {
            case Direction::Up: forward = dy; side = std::fabs(dx); break;
            case Direction::Down: forward = -dy; side = std::fabs(dx); break;
            case Direction::Left: forward = -dx; side = std::fabs(dy); break;
            case Direction::Right: forward = dx; side = std::fabs(dy); break;
            }
            if (forward <= 0.0001f) continue;
            const float score = forward + side * bias;
            if (score < best_score)
            {
                best_score = score;
                best = candidate;
            }
        }
        return best;
    }

    void UIFocusManager::Update(Scene::Scene& scene, const Scene::IInputService& input)
    {
        UISelectableComponent* current = Current(scene);
        if (current == nullptr)
        {
            const bool wants_focus = input.Pressed("NavigateUp") || input.Pressed("NavigateDown") ||
                input.Pressed("NavigateLeft") || input.Pressed("NavigateRight") ||
                input.Pressed("UISubmit");
            if (!wants_focus) return;
            Core::GameObject* top_canvas = nullptr;
            int top_order = (std::numeric_limits<int>::min)();
            for (std::size_t i = 0; i < scene.GameObjectCount(); ++i)
            {
                Core::GameObject* object = scene.GameObjectAt(i);
                CanvasComponent* canvas = object != nullptr
                    ? object->GetComponent<CanvasComponent>() : nullptr;
                if (canvas == nullptr || !canvas->ActiveInHierarchy()) continue;
                if (top_canvas == nullptr || canvas->sort_order >= top_order)
                {
                    top_canvas = object;
                    top_order = canvas->sort_order;
                }
            }
            std::vector<UISelectableComponent*> all = Gather(scene, top_canvas);
            if (!all.empty()) SetFocus(scene, all.front());
            return;
        }

        // InputField は左右キーを文字カーソルに使う。上下は UI Navigation に残す。
        const bool text_editing = current->Owner() != nullptr &&
            current->Owner()->GetComponent<UIInputFieldComponent>() != nullptr;
        const UISliderComponent* slider = current->Owner() != nullptr
            ? current->Owner()->GetComponent<UISliderComponent>() : nullptr;
        const bool horizontal_slider = slider != nullptr &&
            (slider->direction == UISliderComponent::LeftToRight ||
                slider->direction == UISliderComponent::RightToLeft);
        const bool vertical_slider = slider != nullptr && !horizontal_slider;
        Direction direction = Direction::Down;
        bool move = false;
        if (!vertical_slider && input.Pressed("NavigateUp"))
            { direction = Direction::Up; move = true; }
        else if (!vertical_slider && input.Pressed("NavigateDown"))
            { direction = Direction::Down; move = true; }
        else if (!text_editing && !horizontal_slider && input.Pressed("NavigateLeft"))
            { direction = Direction::Left; move = true; }
        else if (!text_editing && !horizontal_slider && input.Pressed("NavigateRight"))
            { direction = Direction::Right; move = true; }
        if (!move) return;
        if (UISelectableComponent* next = FindInDirection(scene, *current, direction))
            SetFocus(scene, next);
    }
}
