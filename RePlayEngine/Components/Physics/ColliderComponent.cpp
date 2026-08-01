#include "ColliderComponent.h"

#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>

namespace ReplayEngine::Components
{
    namespace
    {
        // 実行時 ColliderID の採番。
        //
        // ObjectID だけでは、同じ GameObject に Collider が複数付いている場合を
        // 区別できないため別に振る。プロセス内で一意であればよく、保存はしない。
        // 保存が必要な参照には collider_key を使う。
        Scene::ColliderID NextColliderID() noexcept
        {
            static Scene::ColliderID next = 1;
            return next++;
        }
    }

    const char* ToString(ColliderShape shape) noexcept
    {
        switch (shape)
        {
        case ColliderShape::Sphere:  return "Sphere Collider";
        case ColliderShape::Box:     return "Box Collider";
        case ColliderShape::Capsule: return "Capsule Collider";
        case ColliderShape::Mesh:    return "Mesh Collider";
        }
        return "Collider";
    }

    ColliderComponent::ColliderComponent()
        : collider_id_(NextColliderID())
    {
        // ID はコンストラクタで確定させる。
        // OnAttach 待ちにすると、Attach 前に Inspector から参照された場合に
        // 無効 ID が見えてしまう。
    }

    void ColliderComponent::OnAttach()
    {
        AssignColliderKey();
        OnColliderAttach();
        NotifySceneStructureChanged();
    }

    void ColliderComponent::OnDetach()
    {
        OnColliderDetach();
        NotifySceneStructureChanged();
    }

    void ColliderComponent::OnEnable()
    {
        OnColliderEnable();
        NotifySceneStructureChanged();
    }

    void ColliderComponent::OnDisable()
    {
        OnColliderDisable();
        NotifySceneStructureChanged();
    }

    void ColliderComponent::NotifySceneStructureChanged() noexcept
    {
        if (Scene::Scene* scene = GetScene())
        {
            scene->BumpStructureGeneration();
        }
    }

    void ColliderComponent::AssignColliderKey() noexcept
    {
        // 既に値が入っているなら触らない。
        // Prefab 複製や Scene 読み込みでは、値が入ったあとに OnAttach が来る場合と
        // 来ない場合の両方があるため、上書きしないことを守る。
        if (collider_key > 0) return;

        const Core::GameObject* owner = Owner();
        if (owner == nullptr)
        {
            collider_key = 1;
            return;
        }

        int highest = 0;
        for (std::size_t index = 0; index < owner->ComponentCount(); ++index)
        {
            const Core::Component* component = owner->ComponentAt(index);
            if (component == nullptr || component == this) continue;
            const auto* collider = dynamic_cast<const ColliderComponent*>(component);
            if (collider == nullptr) continue;
            highest = std::max(highest, collider->collider_key);
        }
        collider_key = highest + 1;
    }

    DirectX::XMFLOAT3 ColliderComponent::WorldCenter() const noexcept
    {
        const Core::GameObject* owner = Owner();
        if (owner == nullptr) return DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };

        const DirectX::XMFLOAT3 position = owner->GetTransform().WorldPosition();
        return DirectX::XMFLOAT3{
            position.x + center_offset.x,
            position.y + center_offset.y,
            position.z + center_offset.z };
    }

    // -----------------------------------------------------------------------

    const ColliderComponent* FindColliderByKey(const Core::GameObject& owner,
        int collider_key) noexcept
    {
        if (collider_key <= 0) return nullptr;
        for (std::size_t index = 0; index < owner.ComponentCount(); ++index)
        {
            const Core::Component* component = owner.ComponentAt(index);
            if (component == nullptr || component->PendingDestroy()) continue;
            const auto* collider = dynamic_cast<const ColliderComponent*>(component);
            if (collider != nullptr && collider->collider_key == collider_key) return collider;
        }
        return nullptr;
    }

    ColliderComponent* FindColliderByKey(Core::GameObject& owner, int collider_key) noexcept
    {
        return const_cast<ColliderComponent*>(
            FindColliderByKey(static_cast<const Core::GameObject&>(owner), collider_key));
    }

    ColliderComponent* FindColliderByID(Core::GameObject& owner,
        Scene::ColliderID collider_id) noexcept
    {
        if (collider_id == Scene::invalid_collider_id) return nullptr;
        for (std::size_t index = 0; index < owner.ComponentCount(); ++index)
        {
            Core::Component* component = owner.ComponentAt(index);
            if (component == nullptr || component->PendingDestroy()) continue;
            auto* collider = dynamic_cast<ColliderComponent*>(component);
            if (collider != nullptr && collider->GetColliderID() == collider_id) return collider;
        }
        return nullptr;
    }
}
