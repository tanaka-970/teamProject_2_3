#include "EditorSelectionBounds.h"

#include "../../Components/Physics/ColliderComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>

namespace ReplayEngine::Editor
{
    using DirectX::XMFLOAT3;

    namespace
    {
        // 階層が壊れていても無限再帰しないための上限。
        constexpr int maximum_depth = 64;

        void Accumulate(const Core::GameObject& object, WorldBounds& bounds, int depth)
        {
            if (depth > maximum_depth) return;
            if (object.PendingDestroy()) return;

            // 1) Collider の World Bounds。もっとも正確な実体の範囲。
            bool found_collider_bounds = false;
            for (std::size_t slot = 0; slot < object.ComponentCount(); ++slot)
            {
                const Core::Component* component = object.ComponentAt(slot);
                if (component == nullptr || component->PendingDestroy()) continue;

                const auto* collider =
                    dynamic_cast<const Components::ColliderComponent*>(component);
                if (collider == nullptr) continue;

                XMFLOAT3 minimum{};
                XMFLOAT3 maximum{};
                // Mesh の Cook 前などは false が返る。その場合は無視して次へ。
                if (!collider->ComputeWorldBounds(minimum, maximum)) continue;

                bounds.Encapsulate(minimum, maximum);
                found_collider_bounds = true;
            }

            // 2) Collider が無い（または形が確定していない）場合は位置だけを使う。
            //    これで「Bounds がまったく無い GameObject」でもフォーカスが成立する。
            if (!found_collider_bounds)
            {
                bounds.Encapsulate(object.GetTransform().WorldPosition());
            }

            // 3) 子孫も含める。
            for (const Core::GameObject* child : object.Children())
            {
                if (child == nullptr) continue;
                Accumulate(*child, bounds, depth + 1);
            }
        }
    }

    void WorldBounds::Encapsulate(const XMFLOAT3& point) noexcept
    {
        if (!valid)
        {
            minimum = point;
            maximum = point;
            valid = true;
            return;
        }
        minimum.x = (std::min)(minimum.x, point.x);
        minimum.y = (std::min)(minimum.y, point.y);
        minimum.z = (std::min)(minimum.z, point.z);
        maximum.x = (std::max)(maximum.x, point.x);
        maximum.y = (std::max)(maximum.y, point.y);
        maximum.z = (std::max)(maximum.z, point.z);
    }

    void WorldBounds::Encapsulate(const XMFLOAT3& other_minimum,
        const XMFLOAT3& other_maximum) noexcept
    {
        Encapsulate(other_minimum);
        Encapsulate(other_maximum);
    }

    void WorldBounds::Encapsulate(const WorldBounds& other) noexcept
    {
        if (!other.valid) return;
        Encapsulate(other.minimum, other.maximum);
    }

    XMFLOAT3 WorldBounds::Center() const noexcept
    {
        return XMFLOAT3{
            (minimum.x + maximum.x) * 0.5f,
            (minimum.y + maximum.y) * 0.5f,
            (minimum.z + maximum.z) * 0.5f };
    }

    WorldBounds EditorSelectionBounds::Compute(const Core::GameObject& object)
    {
        WorldBounds bounds;
        Accumulate(object, bounds, 0);
        return bounds;
    }

    WorldBounds EditorSelectionBounds::Compute(const Scene::Scene& scene,
        const std::vector<Core::ObjectID>& selection)
    {
        WorldBounds bounds;
        for (const Core::ObjectID id : selection)
        {
            const Core::GameObject* object = scene.FindGameObjectByID(id);
            if (object == nullptr) continue;
            Accumulate(*object, bounds, 0);
        }
        return bounds;
    }
}
