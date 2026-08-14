#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class UIButtonComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIButtonComponent)

    public:
        enum ButtonState : int
        {
            Normal = 0,
            Hover = 1,
            Pressed = 2,
            Disabled = 3,
        };

        UIButtonComponent() = default;

        void OnAttach() override;

        bool interactable = true;
        Reflection::ComponentReference target_image;
        DirectX::XMFLOAT4 normal_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 hover_color{ 0.86f, 0.86f, 0.86f, 1.0f };
        DirectX::XMFLOAT4 pressed_color{ 0.62f, 0.62f, 0.62f, 1.0f };
        DirectX::XMFLOAT4 disabled_color{ 0.28f, 0.28f, 0.28f, 1.0f };
        Reflection::AssetReference normal_motion;
        Reflection::AssetReference hover_motion;
        Reflection::AssetReference pressed_motion;
        Reflection::AssetReference disabled_motion;
        float state_blend_seconds = 0.08f;
        bool navigation_enabled = true;
        int navigation_order = 0;
        int state = Normal;

        // Runtime のフォーカス状態。Scene には保存しない。
        bool focused = false;
    };
}
