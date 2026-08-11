#include "SphereColliderComponent.h"

#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Components
{
    SphereColliderComponent::SphereColliderComponent()
    {
        // 旧 Player と同じ既定値。基底が持つ center_offset へ設定する。
        center_offset = DirectX::XMFLOAT3{ 0.0f, 0.38f, 0.0f };
    }

    float SphereColliderComponent::EffectiveRadius() const noexcept
    {
        const Core::GameObject* owner = Owner();
        if (owner == nullptr) return radius;

        const DirectX::XMFLOAT3 scale = owner->GetTransform().WorldScale();
        const float largest = (std::max)({ std::fabs(scale.x),
            std::fabs(scale.y), std::fabs(scale.z) });
        return radius * (largest > 0.0f ? largest : 1.0f);
    }

    bool SphereColliderComponent::ComputeWorldBounds(DirectX::XMFLOAT3& minimum,
        DirectX::XMFLOAT3& maximum) const
    {
        if (radius <= 0.0f) return false;

        const DirectX::XMFLOAT3 center = WorldCenter();
        const float effective = EffectiveRadius();
        minimum = DirectX::XMFLOAT3{ center.x - effective, center.y - effective, center.z - effective };
        maximum = DirectX::XMFLOAT3{ center.x + effective, center.y + effective, center.z + effective };
        return true;
    }

    std::string SphereColliderComponent::StatusMessage() const
    {
        if (radius <= 0.0f)
        {
            return "半径が 0 以下です。この Collider は衝突しません。";
        }

        const Core::GameObject* owner = Owner();
        if (owner != nullptr)
        {
            const DirectX::XMFLOAT3 scale = owner->GetTransform().WorldScale();
            const float smallest = (std::min)({ std::fabs(scale.x),
                std::fabs(scale.y), std::fabs(scale.z) });
            const float largest = (std::max)({ std::fabs(scale.x),
                std::fabs(scale.y), std::fabs(scale.z) });
            if (largest - smallest > 1.0e-4f * (std::max)(1.0f, largest))
            {
                return "拡大率が軸ごとに異なります。球は楕円体になりませんので、"
                    "最大軸に合わせた大きめの半径として扱います。";
            }
        }
        return std::string();
    }
}
