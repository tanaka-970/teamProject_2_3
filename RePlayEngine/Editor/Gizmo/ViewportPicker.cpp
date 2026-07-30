#include "ViewportPicker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ReplayEngine::Editor
{
    Scene::EntityId ViewportPicker::Pick(const Scene::SceneDocument& scene,
        const DirectX::XMFLOAT3& ray_origin,
        const DirectX::XMFLOAT3& ray_direction) noexcept
    {
        using namespace DirectX;
        const XMVECTOR origin = XMLoadFloat3(&ray_origin);
        const XMVECTOR direction = XMVector3Normalize(XMLoadFloat3(&ray_direction));
        float nearest = (std::numeric_limits<float>::max)();
        Scene::EntityId selected = 0;
        for (const Scene::SceneEntity& entity : scene.Entities())
        {
            if (!entity.active || !entity.transform) continue;
            const auto& transform = *entity.transform;
            const float radius = (std::max)({ std::abs(transform.scale.x),
                std::abs(transform.scale.y), std::abs(transform.scale.z), 0.5f });
            const XMVECTOR center = XMLoadFloat3(&transform.position);
            const XMVECTOR offset = center - origin;
            const float along = XMVectorGetX(XMVector3Dot(offset, direction));
            if (along < 0.0f) continue;
            const XMVECTOR closest = origin + direction * along;
            const float distance = XMVectorGetX(XMVector3Length(center - closest));
            if (distance <= radius && along < nearest)
            {
                nearest = along;
                selected = entity.id;
            }
        }
        return selected;
    }
}
