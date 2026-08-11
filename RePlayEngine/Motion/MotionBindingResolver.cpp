#include "MotionBindingResolver.h"

#include "../Object/Component/Component.h"
#include "../Object/GameObject/GameObject.h"
#include "../Reflection/Registry/PropertyRegistry.h"
#include "../Scene/Runtime/Scene.h"

#include <string>
#include <string_view>
#include <vector>

namespace ReplayEngine::Motion
{
    namespace
    {
        MotionBindingOrigin BindingOrigin(const MotionBinding& binding) noexcept
        {
            if (binding.origin < static_cast<int>(MotionBindingOrigin::Absolute) ||
                binding.origin > static_cast<int>(MotionBindingOrigin::ChildPath))
            {
                return MotionBindingOrigin::Absolute;
            }
            return static_cast<MotionBindingOrigin>(binding.origin);
        }

        Core::GameObject* FindChildByName(Core::GameObject* parent,
            std::string_view name) noexcept
        {
            if (parent == nullptr || name.empty()) return nullptr;
            for (Core::GameObject* child : parent->Children())
            {
                if (child != nullptr && !child->PendingDestroy() &&
                    child->Name().size() == name.size() &&
                    child->Name().compare(0, name.size(), name.data(), name.size()) == 0)
                    return child;
            }
            return nullptr;
        }

        Core::GameObject* ResolveChildPath(Core::GameObject* origin,
            std::string_view path) noexcept
        {
            if (origin == nullptr || path.empty()) return nullptr;

            Core::GameObject* current = origin;
            std::size_t begin = 0;
            while (begin < path.size())
            {
                const std::size_t separator = path.find('/', begin);
                const std::size_t end = separator == std::string::npos
                    ? path.size() : separator;
                if (end == begin) return nullptr;

                current = FindChildByName(current, path.substr(begin, end - begin));
                if (current == nullptr) return nullptr;
                if (separator == std::string::npos) break;
                begin = separator + 1;
            }
            return current;
        }

        Core::GameObject* ResolveTargetObject(Scene::Scene& scene,
            const MotionBinding& binding, Core::GameObject* origin_object) noexcept
        {
            const MotionBindingOrigin origin = BindingOrigin(binding);
            if (origin == MotionBindingOrigin::Absolute)
                return scene.FindGameObjectByID(binding.object);

            // Editor Preview など origin_object を渡せない呼び出し側では、
            // Asset に残した ObjectID を「起点の候補」として使う。
            Core::GameObject* base = origin_object;
            if (base == nullptr && binding.object.Valid())
                base = scene.FindGameObjectByID(binding.object);
            if (base == nullptr || base->PendingDestroy()) return nullptr;

            switch (origin)
            {
            case MotionBindingOrigin::Self:
                return base;
            case MotionBindingOrigin::Parent:
                return base->Parent();
            case MotionBindingOrigin::ChildPath:
                return ResolveChildPath(base, binding.relative_path);
            case MotionBindingOrigin::Absolute:
            default:
                return nullptr;
            }
        }
    }

    ResolvedMotionBinding MotionBindingResolver::Resolve(Scene::Scene& scene,
        const MotionBinding& binding, Core::GameObject* origin_object) noexcept
    {
        if (!binding.Valid()) return {};

        Core::GameObject* object = ResolveTargetObject(scene, binding, origin_object);
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
        if (desc == nullptr)
        {
            if (const std::vector<Reflection::PropertyDesc>* dynamic =
                target->DynamicProperties())
            {
                for (const Reflection::PropertyDesc& candidate : *dynamic)
                {
                    if (candidate.name == binding.property)
                    {
                        desc = &candidate;
                        break;
                    }
                }
            }
        }
        if (desc == nullptr || desc->animatable == Reflection::Animatable::None)
        {
            return {};
        }

        return { target, desc };
    }
}
