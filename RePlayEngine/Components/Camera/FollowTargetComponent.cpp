#include "FollowTargetComponent.h"

#include "CameraTargetComponent.h"
#include "../Core/TransformComponent.h"
#include "../../Motion/MotionMixer.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <windows.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Components
{
    namespace
    {
        XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b) noexcept
        {
            return XMFLOAT3{ a.x + b.x, a.y + b.y, a.z + b.z };
        }

        XMFLOAT3 ClampFiniteFollowValues(float distance, float height, float lag)
        {
            const float safe_distance =
                std::isfinite(distance) ? std::clamp(distance, 0.5f, 100.0f) : 6.5f;
            const float safe_height =
                std::isfinite(height) ? std::clamp(height, -10.0f, 50.0f) : 2.25f;
            const float safe_lag =
                std::isfinite(lag) ? std::clamp(lag, 0.0f, 60.0f) : 12.0f;
            return XMFLOAT3{ safe_distance, safe_height, safe_lag };
        }
    }

    void FollowTargetComponent::OnLateUpdate(float delta_time)
    {
        Core::GameObject* owner = Owner();
        Scene::Scene* scene = GetScene();
        if (owner == nullptr || scene == nullptr) return;

        const CameraTargetSelection selection = ResolveCameraTargetSelection(
            *scene, scene->Services().ControlledObject());
        if (!selection.Valid()) return;

        if (yield_to_motion)
        {
            const Motion::MotionMixer* mixer = scene->Services().MotionMixer();
            const TransformComponent* transform =
                owner->GetComponent<TransformComponent>();
            if (mixer != nullptr && transform != nullptr &&
                (mixer->WasDriven(*transform, "position") ||
                    mixer->WasDriven(*transform, "rotation") ||
                    mixer->WasDriven(*transform, "scale")))
            {
                ResetCursorTracking();
                return;
            }
        }

        UpdateRotationInput(delta_time);

        const XMFLOAT3 target_world = selection.object->GetTransform().WorldPosition();
        const CameraTargetComponent& target = *selection.component;
        const XMFLOAT3 anchor = Add(target_world, target.target_offset);
        const XMFLOAT3 focus = Add(anchor, target.look_at_offset);

        const XMFLOAT3 safe = ClampFiniteFollowValues(
            follow_distance, follow_height, follow_lag);
        const float distance = safe.x;
        const float height = safe.y;
        const float lag = safe.z;

        const float cp = std::cos(pitch_offset);
        const XMFLOAT3 desired{
            focus.x - std::sin(yaw_offset) * cp * distance,
            focus.y + height + std::sin(pitch_offset) * distance,
            focus.z - std::cos(yaw_offset) * cp * distance
        };

        const XMFLOAT3 current_eye = owner->GetTransform().WorldPosition();
        const float elapsed = (std::max)(0.0f, delta_time);
        const float t = lag > 0.0f ? (1.0f - std::exp(-lag * elapsed)) : 1.0f;
        const XMFLOAT3 new_eye{
            current_eye.x + (desired.x - current_eye.x) * t,
            current_eye.y + (desired.y - current_eye.y) * t,
            current_eye.z + (desired.z - current_eye.z) * t
        };

        const XMMATRIX view = XMMatrixLookAtLH(
            XMLoadFloat3(&new_eye), XMLoadFloat3(&focus), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        XMVECTOR determinant{};
        const XMMATRIX world = XMMatrixInverse(&determinant, view);
        if (!XMVector4Equal(determinant, XMVectorZero()))
        {
            owner->GetTransform().SetFromWorldMatrix(world);
        }
    }

    void FollowTargetComponent::OnDisable()
    {
        ResetCursorTracking();
    }

    void FollowTargetComponent::UpdateRotationInput(float delta_time)
    {
        if (!rotation_input_enabled)
        {
            ResetCursorTracking();
            return;
        }

        POINT cursor{};
        if (!GetCursorPos(&cursor)) return;

        if (!cursor_initialized_)
        {
            previous_cursor_x_ = cursor.x;
            previous_cursor_y_ = cursor.y;
            cursor_initialized_ = true;
        }

        const long delta_x = cursor.x - previous_cursor_x_;
        const long delta_y = cursor.y - previous_cursor_y_;
        previous_cursor_x_ = cursor.x;
        previous_cursor_y_ = cursor.y;

        if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
        {
            constexpr float radians_per_pixel = 0.006f;
            yaw_offset += static_cast<float>(delta_x) * radians_per_pixel;
            pitch_offset += -static_cast<float>(delta_y) * radians_per_pixel;
        }

        const float key_rotate_speed = 1.8f * (std::max)(0.0f, delta_time);
        if (GetAsyncKeyState('J') & 0x8000) yaw_offset -= key_rotate_speed;
        if (GetAsyncKeyState('L') & 0x8000) yaw_offset += key_rotate_speed;
        if (GetAsyncKeyState('I') & 0x8000) pitch_offset += key_rotate_speed;
        if (GetAsyncKeyState('K') & 0x8000) pitch_offset -= key_rotate_speed;

        constexpr float pitch_limit = 1.40f;
        pitch_offset = std::clamp(pitch_offset, -pitch_limit, pitch_limit);
    }

    void FollowTargetComponent::ResetCursorTracking() noexcept
    {
        cursor_initialized_ = false;
        previous_cursor_x_ = 0;
        previous_cursor_y_ = 0;
    }
}
