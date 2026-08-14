#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class RectTransformComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(RectTransformComponent)

    public:
        RectTransformComponent();

        DirectX::XMFLOAT2 anchor_min{ 0.5f, 0.5f };
        DirectX::XMFLOAT2 anchor_max{ 0.5f, 0.5f };
        DirectX::XMFLOAT2 anchored_position{ 0.0f, 0.0f };
        DirectX::XMFLOAT2 size_delta{ 100.0f, 100.0f };
        DirectX::XMFLOAT2 pivot{ 0.5f, 0.5f };
        float rotation = 0.0f;
        DirectX::XMFLOAT2 scale{ 1.0f, 1.0f };
        int sort_order = 0;

        const DirectX::XMFLOAT4& ResolvedRect() const noexcept { return resolved_rect_; }
        const DirectX::XMFLOAT4X4& ResolvedMatrix() const noexcept { return resolved_matrix_; }

        void SetResolvedRect(const DirectX::XMFLOAT4& rect) noexcept
        {
            resolved_rect_ = rect;
        }
        void SetResolvedMatrix(const DirectX::XMFLOAT4X4& matrix) noexcept
        {
            resolved_matrix_ = matrix;
        }

    private:
        DirectX::XMFLOAT4 resolved_rect_{ 0.0f, 0.0f, 100.0f, 100.0f };
        DirectX::XMFLOAT4X4 resolved_matrix_{};
    };
}
