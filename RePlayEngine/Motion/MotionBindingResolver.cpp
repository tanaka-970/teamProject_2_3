#include "MotionBindingResolver.h"

#include "../Object/Component/Component.h"
#include "../Object/GameObject/GameObject.h"
#include "../Reflection/Registry/PropertyRegistry.h"
#include "../Scene/Runtime/Scene.h"

namespace ReplayEngine::Motion
{
    ResolvedMotionBinding MotionBindingResolver::Resolve(Scene::Scene& scene,
        const MotionBinding& binding) noexcept
    {
        if (!binding.Valid()) return {};

        Core::GameObject* object = scene.FindGameObjectByID(binding.object);
        if (object == nullptr || object->PendingDestroy()) return {};

        int type_index = 0;
        Core::Component* target = nullptr;
        const std::size_t count = object->ComponentCount();
        for (std::size_t i = 0; i < count; ++i)
        {
            Core::Component* component = object->ComponentAt(i);
            if (component == nullptr || component->PendingDestroy()) continue;
            if (component->TypeID() != binding.component_type) continue;

            if (type_index == binding.component_index)
            {
                target = component;
                break;
            }
            ++type_index;
        }

        if (target == nullptr) return {};

        const Reflection::PropertyDesc* desc =
            Reflection::PropertyRegistry::Find(binding.component_type, binding.property);
        if (desc == nullptr || desc->animatable == Reflection::Animatable::None)
        {
            return {};
        }

        return { target, desc };
    }
}
