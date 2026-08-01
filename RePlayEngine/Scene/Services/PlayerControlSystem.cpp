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
        // Controller を消しても外れない（操作が止まるだけ）。
        if (IsValidTarget(scene, controlled_)) return controlled_;

        // 対象が消えた場合は無効化するだけ。代わりを探さない。
        // 探してしまうと、Player GameObject を削除した瞬間に
        // 別の GameObject が操作対象になり、意図しない挙動になる。
        controlled_ = Core::ObjectID::Invalid();
        return controlled_;
    }
}
