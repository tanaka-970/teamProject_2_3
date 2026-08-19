#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class UISelectableComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UISelectableComponent)

    public:
        UISelectableComponent() = default;

        bool interactable = true;
        bool navigation_enabled = true;
        int navigation_order = 0;
        float navigation_bias = 2.0f;

        Reflection::ComponentReference navigate_up;
        Reflection::ComponentReference navigate_down;
        Reflection::ComponentReference navigate_left;
        Reflection::ComponentReference navigate_right;

        // Project 共通 Focus Style を要素単位で上書きする。
        bool override_focus_style = false;
        bool focus_outline_enabled = true;
        DirectX::XMFLOAT4 focus_outline_color{ 0.25f, 0.78f, 1.0f, 1.0f };
        float focus_outline_width = 2.0f;
        float focus_corner_radius = 4.0f;

        // Runtime only. PropertyRegistry では NotSerializable で登録する。
        bool focused = false;
    };
}
