#include "PlayerControlSystem.h"

#include "../Runtime/Scene.h"
#include "../../Components/Gameplay/PlayerControllerComponent.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Scene
{
    bool PlayerControlSystem::IsControllable(const Scene& scene, Core::ObjectID id)
    {
        if (!id.Valid()) return false;

        const Core::GameObject* object = scene.FindGameObjectByID(id);
        if (object == nullptr || object->PendingDestroy()) return false;

        return object->GetComponent<Components::PlayerControllerComponent>() != nullptr;
    }

    Core::ObjectID PlayerControlSystem::Resolve(const Scene& scene)
    {
        // 今の対象が使えるならそのまま。
        if (IsControllable(scene, controlled_)) return controlled_;

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
