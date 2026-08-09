#pragma once

#include "../../Core/ObjectID/ObjectID.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <cstddef>

namespace ReplayEngine::Components
{
    template<class T>
    struct PriorityComponentSelection final
    {
        const Core::GameObject* object = nullptr;
        const T* component = nullptr;
        bool used_controlled_object = false;

        bool Valid() const noexcept { return object != nullptr && component != nullptr; }
    };

    template<class T>
    PriorityComponentSelection<T> ResolvePriorityComponentSelection(
        const Scene::Scene& scene, Core::ObjectID controlled = Core::ObjectID::Invalid())
    {
        if (controlled.Valid())
        {
            const Core::GameObject* object = scene.FindGameObjectByID(controlled);
            const T* component = object != nullptr ? object->GetComponent<T>() : nullptr;
            if (component != nullptr && component->ActiveInHierarchy())
            {
                return { object, component, true };
            }
        }

        PriorityComponentSelection<T> best{};
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            const Core::GameObject* object = scene.GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy()) continue;

            const T* component = object->GetComponent<T>();
            if (component == nullptr || !component->ActiveInHierarchy()) continue;

            if (!best.Valid() || component->priority > best.component->priority)
            {
                best = { object, component, false };
            }
        }

        return best;
    }
}
