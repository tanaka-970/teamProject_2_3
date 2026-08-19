#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class UIHorizontalLayoutGroupComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIHorizontalLayoutGroupComponent)
    public:
        enum Alignment : int { Start = 0, Center = 1, End = 2 };
        DirectX::XMFLOAT4 padding{ 0.0f, 0.0f, 0.0f, 0.0f }; // left, top, right, bottom
        float spacing = 0.0f;
        int alignment = Start;
        bool control_child_width = false;
        bool control_child_height = true;
    };

    class UIVerticalLayoutGroupComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIVerticalLayoutGroupComponent)
    public:
        enum Alignment : int { Start = 0, Center = 1, End = 2 };
        DirectX::XMFLOAT4 padding{ 0.0f, 0.0f, 0.0f, 0.0f };
        float spacing = 0.0f;
        int alignment = Start;
        bool control_child_width = true;
        bool control_child_height = false;
    };

    class UIGridLayoutGroupComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIGridLayoutGroupComponent)
    public:
        enum Alignment : int { Start = 0, Center = 1, End = 2 };
        enum Constraint : int { FixedColumns = 0, FixedRows = 1, FlexibleWidth = 2 };
        DirectX::XMFLOAT4 padding{ 0.0f, 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT2 spacing{ 0.0f, 0.0f };
        DirectX::XMFLOAT2 cell_size{ 100.0f, 32.0f };
        int alignment = Start;
        int constraint = FixedColumns;
        int constraint_count = 1;
    };
}
