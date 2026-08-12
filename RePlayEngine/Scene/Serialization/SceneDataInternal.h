#pragma once

// これは SceneData の分割内部で共有する実装であり、外部から使うものではない。

#include "SceneData.h"

namespace ReplayEngine::Scene::Serialization::Detail
{
    Reflection::PropertyBag RemapReferences(const Reflection::PropertyBag& source,
        const ObjectRemap* remap, bool clear_unresolved);
    void BuildComponents(const GameObjectData& source, Core::GameObject& target,
        SceneLoadReport& report, const ObjectRemap* object_remap = nullptr,
        bool clear_unresolved_references = false);
    void ApplyObjectBasics(const GameObjectData& source, Core::GameObject& target);
}
