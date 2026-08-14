#pragma once

#include "MotionAsset.h"
#include "../Reflection/Property/PropertyDesc.h"

namespace ReplayEngine::Core
{
    class Component;
    class GameObject;
}

namespace ReplayEngine::Scene
{
    class Scene;
}

namespace ReplayEngine::Motion
{
    struct ResolvedMotionBinding
    {
        Core::Component* component = nullptr;
        const Reflection::PropertyDesc* property = nullptr;

        bool Valid() const noexcept { return component != nullptr && property != nullptr; }
    };

    class MotionBindingResolver final
    {
    public:
        static ResolvedMotionBinding Resolve(Scene::Scene& scene,
            const MotionBinding& binding,
            Core::GameObject* origin_object = nullptr) noexcept;
    };
}
