#include "PlayerControlSystem.h"

#include "../Runtime/Scene.h"
#include "../../Components/Gameplay/PlayerControllerComponent.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Scene
{
    bool PlayerControlSystem::IsValidTarget(const Scene& scene, Core::ObjectID id)
    {
        if (!id.Valid()) return false;
        const Core::GameObject* object = scene.FindGameObjectByID(id);
        return object != nullptr && !object->PendingDestroy();
    }

    bool PlayerControlSystem::HasController(const Scene& scene, Core::ObjectID id)
    {
        if (!IsValidTarget(scene, id)) return false;
        const Core::GameObject* object = scene.FindGameObjectByID(id);
        return object->GetComponent<Components::PlayerControllerComponent>() != nullptr;
    }

    Core::ObjectID PlayerControlSystem::Resolve(const Scene& scene)
    {
        // 指定された対象がまだ存在するならそのまま維持する。
        // Controller を消しても乗り移らない（操作が止まるだけ）。
        if (IsValidTarget(scene, controlled_)) return controlled_;

        controlled_ = Core::ObjectID::Invalid();

        // PlayerControllerComponent を持つ GameObject を探す。
        // 複数あっても最初の 1 体だけを操作対象にする。
        // Prefab を 2 つ置いても操作が分裂しないようにするため。
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            const Core::GameObject* object = scene.GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy()) continue;
            if (!object->ActiveInHierarchy()) continue;
            if (object->GetComponent<Components::PlayerControllerComponent>() == nullptr) continue;

            controlled_ = object->ID();
            break;
        }
        return controlled_;
    }
}
