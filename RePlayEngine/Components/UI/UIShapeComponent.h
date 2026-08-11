#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class UIShapeComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIShapeComponent)

    public:
        enum Shape : int
        {
            Rectangle = 0,
            Circle = 1,
            Line = 2,
            Polygon = 3,
            BezierPath = 4,
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

        UIShapeComponent() = default;

        int shape = Rectangle;
        DirectX::XMFLOAT4 fill_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 fill_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
        int fill_mode = Solid;
        float fill_angle = 0.0f;
        DirectX::XMFLOAT2 fill_center{ 0.5f, 0.5f };
        DirectX::XMFLOAT4 stroke_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 stroke_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
        int stroke_mode = StrokeSolid;
        float stroke_width = 0.0f;
        float corner_radius = 0.0f;
        float arc_curvature = 1.0f;
        int sides = 5;
        float trim_start = 0.0f;
        float trim_end = 1.0f;
        float trim_offset = 0.0f;
        float dash_length = 0.0f;
        float dash_gap = 0.0f;
        float dash_offset = 0.0f;
    };
}
