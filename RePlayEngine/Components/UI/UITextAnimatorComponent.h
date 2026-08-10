#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class UITextAnimatorComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UITextAnimatorComponent)

    public:
        enum RangeShape : int
        {
            Square = 0,
            RampUp = 1,
            RampDown = 2,
            Triangle = 3,
            Round = 4,
            Smooth = 5,
        };

        enum Anchor : int
        {
            Center = 0,
            BaselineLeft = 1,
            BaselineCenter = 2,
            TopLeft = 3,
            BottomCenter = 4,
        };

        UITextAnimatorComponent() = default;

        float range_start = 0.0f;
        float range_end = 1.0f;
        float range_offset = 0.0f;
        int range_shape = Square;
        float range_smoothness = 0.0f;

        DirectX::XMFLOAT2 position_offset{ 0.0f, 0.0f };
        float rotation = 0.0f;
        DirectX::XMFLOAT2 scale{ 1.0f, 1.0f };
        float opacity = 1.0f;
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float character_spacing = 0.0f;
        int random_seed = 0;
        DirectX::XMFLOAT2 random_position{ 0.0f, 0.0f };
        float random_rotation = 0.0f;
        int anchor = Center;
    };
}
