#include "RotatorComponent.h"

#include "../../Object/GameObject/GameObject.h"

#include <cmath>

namespace ReplayEngine::Components
{
    void RotatorComponent::OnUpdate(float delta_time)
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;

        elapsed_seconds += delta_time;

        const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        if (length <= 1.0e-6f) return;

        const float radians = degrees_per_second * (DirectX::XM_PI / 180.0f) * delta_time;
        const float scale = radians / length;

        Core::Transform& transform = owner->GetTransform();
        DirectX::XMFLOAT3 euler = transform.LocalRotationEuler();
        euler.x += axis.x * scale;
        euler.y += axis.y * scale;
        euler.z += axis.z * scale;

        // 角度が際限なく増えて float の精度を失わないよう、-2π〜2π へ畳む。
        const float two_pi = DirectX::XM_2PI;
        euler.x = std::fmod(euler.x, two_pi);
        euler.y = std::fmod(euler.y, two_pi);
        euler.z = std::fmod(euler.z, two_pi);

        transform.SetLocalRotationEuler(euler);
    }
}
