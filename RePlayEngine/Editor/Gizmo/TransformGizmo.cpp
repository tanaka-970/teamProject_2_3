#include "TransformGizmo.h"

#include <algorithm>

namespace ReplayEngine::Editor
{
    void TransformGizmo::SetSnapStep(float value) noexcept
    {
        snap_step_ = (std::max)(value, 0.001f);
    }
}
