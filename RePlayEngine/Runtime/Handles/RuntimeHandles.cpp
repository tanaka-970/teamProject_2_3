#include "RuntimeHandles.h"

namespace ReplayEngine::Runtime
{
    std::string ObjectHandle::ToString() const
    {
        if (IsEmpty()) return "ObjectHandle(none)";
        return "ObjectHandle(w" + std::to_string(world) +
            ":o" + object.ToString() +
            ":g" + std::to_string(generation) + ")";
    }

    std::string ComponentHandle::ToString() const
    {
        if (IsEmpty()) return "ComponentHandle(none)";
        return "ComponentHandle(" + owner.ToString() +
            ":i" + std::to_string(instance) +
            ":t" + std::to_string(type_id) + ")";
    }
}
