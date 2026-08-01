#include "ViewportPicker.h"

#include "../../Components/Physics/ColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ReplayEngine::Editor
{
    namespace
    {
        bool RayAabb(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction,
            const DirectX::XMFLOAT3& minimum, const DirectX::XMFLOAT3& maximum,
            float& distance) noexcept
        {
            float near_distance = 0.0f;
            float far_distance = (std::numeric_limits<float>::max)();
            const float origin_values[3] = { origin.x, origin.y, origin.z };
            const float direction_values[3] = { direction.x, direction.y, direction.z };
            const float minimum_values[3] = { minimum.x, minimum.y, minimum.z };
            const float maximum_values[3] = { maximum.x, maximum.y, maximum.z };
            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(direction_values[axis]) < 0.000001f)
                {
                    if (origin_values[axis] < minimum_values[axis] ||
                        origin_values[axis] > maximum_values[axis]) return false;
                    continue;
                }
                const float inverse = 1.0f / direction_values[axis];
                float first = (minimum_values[axis] - origin_values[axis]) * inverse;
                float second = (maximum_values[axis] - origin_values[axis]) * inverse;
                if (first > second) std::swap(first, second);
                near_distance = (std::max)(near_distance, first);
                far_distance = (std::min)(far_distance, second);
                if (near_distance > far_distance) return false;
            }
            distance = near_distance;
            return far_distance >= 0.0f;
        }
    }

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

    Core::ObjectID ViewportPicker::Pick(const Scene::Scene& scene,
        const DirectX::XMFLOAT3& ray_origin,
        const DirectX::XMFLOAT3& ray_direction) noexcept
    {
        using namespace DirectX;
        XMFLOAT3 direction;
        XMStoreFloat3(&direction, XMVector3Normalize(XMLoadFloat3(&ray_direction)));

        float nearest = (std::numeric_limits<float>::max)();
        Core::ObjectID selected;
        for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
        {
            const Core::GameObject* object = scene.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;
            for (std::size_t index = 0; index < object->ComponentCount(); ++index)
            {
                const auto* collider = dynamic_cast<const Components::ColliderComponent*>(
                    object->ComponentAt(index));
                if (collider == nullptr || !collider->ActiveInHierarchy()) continue;
                XMFLOAT3 minimum;
                XMFLOAT3 maximum;
                if (!collider->ComputeWorldBounds(minimum, maximum)) continue;
                float distance = 0.0f;
                if (RayAabb(ray_origin, direction, minimum, maximum, distance) &&
                    distance < nearest)
                {
                    nearest = distance;
                    selected = object->ID();
                }
            }
        }
        if (selected.Valid()) return selected;

        const XMVECTOR origin = XMLoadFloat3(&ray_origin);
        const XMVECTOR ray = XMLoadFloat3(&direction);
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            const Core::GameObject* object = scene.GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;
            const XMFLOAT3 world = object->GetTransform().WorldPosition();
            const XMFLOAT3 scale = object->GetTransform().LocalScale();
            const float radius = (std::max)({ std::abs(scale.x), std::abs(scale.y),
                std::abs(scale.z), 0.25f });
            const XMVECTOR offset = XMLoadFloat3(&world) - origin;
            const float along = XMVectorGetX(XMVector3Dot(offset, ray));
            if (along < 0.0f) continue;
            const float distance = XMVectorGetX(XMVector3Length(offset - ray * along));
            if (distance <= radius && along < nearest)
            {
                nearest = along;
                selected = object->ID();
            }
        }
        return selected;
    }
}
