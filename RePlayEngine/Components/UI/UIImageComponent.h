#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class UIImageComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIImageComponent)

    public:
        enum FillMethod : int
        {
            Horizontal = 0,
            Vertical = 1,
            Radial360 = 2,
        };

        enum BlendMode : int
        {
            Normal = 0,
            Additive = 1,
            Multiply = 2,
            Screen = 3,
        };

        enum FillMode : int
        {
            Solid = 0,
            LinearGradient = 1,
            RadialGradient = 2,
        };

        enum StrokeMode : int
        {
            StrokeSolid = 0,
            StrokeAlongLength = 1,
        };

        UIImageComponent() = default;

        void OnAttach() override;

        Reflection::AssetReference sprite;
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 fill_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
        int fill_mode = Solid;
        float fill_angle = 0.0f;
        DirectX::XMFLOAT2 fill_center{ 0.5f, 0.5f };
        int stroke_mode = StrokeSolid;
        DirectX::XMFLOAT4 stroke_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
        float opacity = 1.0f;
        float fill_amount = 1.0f;
        int fill_method = Horizontal;
        DirectX::XMFLOAT2 uv_offset{ 0.0f, 0.0f };
        DirectX::XMFLOAT2 uv_scale{ 1.0f, 1.0f };
        int blend_mode = Normal;
        DirectX::XMFLOAT4 nine_slice{ 0.0f, 0.0f, 0.0f, 0.0f };
        bool preserve_aspect = false;
    };
}
