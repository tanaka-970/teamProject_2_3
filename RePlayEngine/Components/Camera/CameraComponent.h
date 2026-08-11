#pragma once

#include "../Common/PriorityComponentSelection.h"
#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    enum class CameraProjectionMode
    {
        Perspective = 0,
        Orthographic = 1
    };

    class CameraComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(CameraComponent)

    public:
        CameraComponent() = default;

        int projection_mode = static_cast<int>(CameraProjectionMode::Perspective);
        float field_of_view_degrees = 50.0f;
        float orthographic_size = 10.0f;
        float near_clip = 0.1f;
        float far_clip = 10000.0f;
        int priority = 0;

        // false の既定値は旧 Scene の「優先度が最高の Camera 1 台を全画面」挙動を守る。
        // true の Camera が 1 台でもある Scene だけ、複数 Camera の Viewport 合成へ入る。
        bool viewport_enabled = false;

        // x, y, width, height in normalized 0..1 viewport coordinates.
        DirectX::XMFLOAT4 viewport_rect{ 0.0f, 0.0f, 1.0f, 1.0f };

        CameraProjectionMode ProjectionMode() const noexcept;
        DirectX::XMMATRIX ViewMatrix() const noexcept;
        DirectX::XMMATRIX ProjectionMatrix(float aspect) const noexcept;
        DirectX::XMFLOAT3 EyePosition() const noexcept;
        DirectX::XMFLOAT3 Forward() const noexcept;
        DirectX::XMFLOAT3 Right() const noexcept;
        DirectX::XMFLOAT3 Up() const noexcept;
    };

    using CameraSelection = PriorityComponentSelection<CameraComponent>;

    inline CameraSelection ResolveActiveCameraSelection(const Scene::Scene& scene)
    {
        return ResolvePriorityComponentSelection<CameraComponent>(
            scene, Core::ObjectID::Invalid());
    }
}
