#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

namespace ReplayEngine::Components
{
    class UISliderComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UISliderComponent)

    public:
        enum Direction : int
        {
            LeftToRight = 0,
            RightToLeft = 1,
            BottomToTop = 2,
            TopToBottom = 3,
        };

        void OnAttach() override;
        void OnPropertyChanged(const char* property_name) override;

        float minimum = 0.0f;
        float maximum = 1.0f;
        float value = 0.0f;
        bool whole_numbers = false;
        int direction = LeftToRight;
        bool interactable = true;
        Reflection::ComponentReference fill_image;
        Reflection::ComponentReference handle_rect;
        float keyboard_step = 0.1f;

        // Runtime 派生値。
        bool dragging = false;
        float last_emitted_value = 0.0f;

        float NormalizedValue() const noexcept;
        bool SetValue(float next) noexcept;
    };
}
