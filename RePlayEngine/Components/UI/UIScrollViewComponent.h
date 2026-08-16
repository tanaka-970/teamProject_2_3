#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class UIScrollViewComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIScrollViewComponent)
    public:
        UIScrollViewComponent() = default;
        void OnAttach() override;

        Reflection::ComponentReference content;
        bool horizontal = false;
        bool vertical = true;
        bool clamp_when_content_fits = true;
        float scroll_sensitivity = 48.0f;
        DirectX::XMFLOAT2 scroll_offset{ 0.0f, 0.0f };

        bool show_scrollbars = true;
        float scrollbar_width = 8.0f;
        DirectX::XMFLOAT4 scrollbar_track_color{ 0.12f, 0.12f, 0.12f, 0.40f };
        DirectX::XMFLOAT4 scrollbar_thumb_color{ 0.80f, 0.80f, 0.80f, 0.90f };
        float scrollbar_corner_radius = 4.0f;

        // UILayout が毎フレーム更新する派生値。保存しない。
        bool horizontal_overflow = false;
        bool vertical_overflow = false;
        float horizontal_visible_ratio = 1.0f;
        float vertical_visible_ratio = 1.0f;
        float horizontal_normalized = 0.0f;
        float vertical_normalized = 0.0f;

        // Pointer drag state. Runtime only / not registered.
        bool dragging_content = false;
        bool dragging_horizontal_thumb = false;
        bool dragging_vertical_thumb = false;
        DirectX::XMFLOAT2 drag_last_pointer{ 0.0f, 0.0f };
    };
}
