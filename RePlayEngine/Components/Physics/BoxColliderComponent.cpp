#include "BoxColliderComponent.h"

#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Components
{
    XMFLOAT3 BoxColliderComponent::WorldHalfExtents() const noexcept
    {
        XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
        if (const Core::GameObject* owner = Owner())
        {
            scale = owner->GetTransform().WorldScale();
        }
        return XMFLOAT3{
            std::fabs(size.x * scale.x) * 0.5f,
            std::fabs(size.y * scale.y) * 0.5f,
            std::fabs(size.z * scale.z) * 0.5f };
    }

    bool BoxColliderComponent::ComputeWorldBounds(XMFLOAT3& minimum, XMFLOAT3& maximum) const
    {
        const XMFLOAT3 half = WorldHalfExtents();
        if (half.x <= 0.0f || half.y <= 0.0f || half.z <= 0.0f) return false;

        const Core::GameObject* owner = Owner();
        const XMFLOAT3 center = WorldCenter();

        if (owner == nullptr)
        {
            minimum = XMFLOAT3{ center.x - half.x, center.y - half.y, center.z - half.z };
            maximum = XMFLOAT3{ center.x + half.x, center.y + half.y, center.z + half.z };
            return true;
        }

        // 回転を含めた AABB。
        // 回転行列の各成分の絶対値を半辺長へ掛けると、軸並行の外接箱が求まる。
        const XMFLOAT3 euler = owner->GetTransform().LocalRotationEuler();
        const XMMATRIX rotation = XMMatrixRotationRollPitchYaw(euler.x, euler.y, euler.z);

        XMFLOAT4X4 matrix{};
        XMStoreFloat4x4(&matrix, rotation);

        const float extent_x = std::fabs(matrix._11) * half.x +
            std::fabs(matrix._21) * half.y + std::fabs(matrix._31) * half.z;
        const float extent_y = std::fabs(matrix._12) * half.x +
            std::fabs(matrix._22) * half.y + std::fabs(matrix._32) * half.z;
        const float extent_z = std::fabs(matrix._13) * half.x +
            std::fabs(matrix._23) * half.y + std::fabs(matrix._33) * half.z;

        minimum = XMFLOAT3{ center.x - extent_x, center.y - extent_y, center.z - extent_z };
        maximum = XMFLOAT3{ center.x + extent_x, center.y + extent_y, center.z + extent_z };
        return true;
    }

    std::string BoxColliderComponent::StatusMessage() const
    {
        if (size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f)
        {
            return "サイズに 0 以下の軸があります。この Collider は衝突しません。";
        }
        return std::string();
    }
}
