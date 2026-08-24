#include "UISliderComponent.h"

#include "RectTransformComponent.h"
#include "UISelectableComponent.h"
#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ReplayEngine::Components
{
    void UISliderComponent::OnAttach()
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;
        owner->AddComponent<RectTransformComponent>();
        owner->AddComponent<UISelectableComponent>();
        SetValue(value);
        last_emitted_value = value;
    }

    void UISliderComponent::OnPropertyChanged(const char* property_name)
    {
        if (property_name == nullptr || std::strcmp(property_name, "value") == 0 ||
            std::strcmp(property_name, "minimum") == 0 ||
            std::strcmp(property_name, "maximum") == 0 ||
            std::strcmp(property_name, "whole_numbers") == 0)
        {
            SetValue(value);
        }
        if (property_name == nullptr || std::strcmp(property_name, "direction") == 0)
            direction = (std::min)((std::max)(direction,
                static_cast<int>(LeftToRight)), static_cast<int>(TopToBottom));
        if (property_name == nullptr || std::strcmp(property_name, "keyboard_step") == 0)
        {
            if (!std::isfinite(keyboard_step)) keyboard_step = 0.1f;
            keyboard_step = (std::max)(0.0001f, std::fabs(keyboard_step));
        }
    }

    float UISliderComponent::NormalizedValue() const noexcept
    {
        const float low = (std::min)(minimum, maximum);
        const float high = (std::max)(minimum, maximum);
        const float range = high - low;
        return range > 0.000001f ? (value - low) / range : 0.0f;
    }

    bool UISliderComponent::SetValue(float next) noexcept
    {
        const float low = (std::min)(minimum, maximum);
        const float high = (std::max)(minimum, maximum);
        if (!std::isfinite(next)) next = low;
        if (whole_numbers) next = std::round(next);
        next = (std::min)((std::max)(next, low), high);
        const bool changed = std::fabs(next - value) > 0.000001f;
        value = next;
        return changed;
    }
}
