#include "CapsuleColliderComponent.h"

#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Components
{
    namespace
    {
        XMFLOAT3 OwnerScale(const Core::GameObject* owner) noexcept
        {
            if (owner == nullptr) return XMFLOAT3{ 1.0f, 1.0f, 1.0f };
            return owner->GetTransform().WorldScale();
        }
    }

    float CapsuleColliderComponent::EffectiveRadius() const noexcept
    {
        const XMFLOAT3 scale = OwnerScale(Owner());

        // 半径は軸に垂直な 2 軸の拡縮を受ける。
        // 非一様なら大きい方を採る（早めに当たる側へ倒す）。
        float lateral = 1.0f;
        switch (axis)
        {
        case Axis_X: lateral = (std::max)(std::fabs(scale.y), std::fabs(scale.z)); break;
        case Axis_Z: lateral = (std::max)(std::fabs(scale.x), std::fabs(scale.y)); break;
        case Axis_Y:
        default:     lateral = (std::max)(std::fabs(scale.x), std::fabs(scale.z)); break;
        }
        if (lateral <= 0.0f) lateral = 1.0f;
        return radius * lateral;
    }

    float CapsuleColliderComponent::EffectiveHeight() const noexcept
    {
        const XMFLOAT3 scale = OwnerScale(Owner());
        float along = 1.0f;
        switch (axis)
        {
        case Axis_X: along = std::fabs(scale.x); break;
        case Axis_Z: along = std::fabs(scale.z); break;
        case Axis_Y:
        default:     along = std::fabs(scale.y); break;
        }
        if (along <= 0.0f) along = 1.0f;

        // 直径を下回る高さは形状として成り立たないので切り上げる。
        const float scaled = height * along;
        const float diameter = EffectiveRadius() * 2.0f;
        return (std::max)(scaled, diameter);
    }

    bool CapsuleColliderComponent::HeightTooSmall() const noexcept
    {
        return height < radius * 2.0f;
    }

    void CapsuleColliderComponent::WorldSegment(XMFLOAT3& start, XMFLOAT3& end) const noexcept
    {
        const XMFLOAT3 center = WorldCenter();
        const float half_cylinder =
            (std::max)(0.0f, EffectiveHeight() * 0.5f - EffectiveRadius());

        XMFLOAT3 direction{ 0.0f, 1.0f, 0.0f };
        switch (axis)
        {
        case Axis_X: direction = XMFLOAT3{ 1.0f, 0.0f, 0.0f }; break;
        case Axis_Z: direction = XMFLOAT3{ 0.0f, 0.0f, 1.0f }; break;
        case Axis_Y:
        default:     direction = XMFLOAT3{ 0.0f, 1.0f, 0.0f }; break;
        }

        // GameObject の回転を軸へ反映する。
        if (const Core::GameObject* owner = Owner())
        {
            const XMFLOAT3 euler = owner->GetTransform().LocalRotationEuler();
            const XMMATRIX rotation =
                XMMatrixRotationRollPitchYaw(euler.x, euler.y, euler.z);
            XMStoreFloat3(&direction,
                XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&direction), rotation)));
        }

        start = XMFLOAT3{
            center.x - direction.x * half_cylinder,
            center.y - direction.y * half_cylinder,
            center.z - direction.z * half_cylinder };
        end = XMFLOAT3{
            center.x + direction.x * half_cylinder,
            center.y + direction.y * half_cylinder,
            center.z + direction.z * half_cylinder };
    }

    bool CapsuleColliderComponent::ComputeWorldBounds(XMFLOAT3& minimum, XMFLOAT3& maximum) const
    {
        const float effective_radius = EffectiveRadius();
        if (effective_radius <= 0.0f) return false;

        XMFLOAT3 start{};
        XMFLOAT3 end{};
        WorldSegment(start, end);

        minimum = XMFLOAT3{
            (std::min)(start.x, end.x) - effective_radius,
            (std::min)(start.y, end.y) - effective_radius,
            (std::min)(start.z, end.z) - effective_radius };
        maximum = XMFLOAT3{
            (std::max)(start.x, end.x) + effective_radius,
            (std::max)(start.y, end.y) + effective_radius,
            (std::max)(start.z, end.z) + effective_radius };
        return true;
    }

    std::string CapsuleColliderComponent::StatusMessage() const
    {
        if (radius <= 0.0f)
        {
            return "半径が 0 以下です。この Collider は衝突しません。";
        }
        if (HeightTooSmall())
        {
            return "高さが直径 (" + std::to_string(radius * 2.0f) +
                ") を下回っています。形状として成立しないため、"
                "判定は直径まで切り上げた高さで行っています。";
        }
        return std::string();
    }
}
