#include "TransformGizmo.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Editor
{
    void TransformGizmo::SetSnapStep(float value) noexcept
    {
        snap_step_ = (std::max)(value, 0.001f);
    }

    void TransformGizmo::ApplyDelta(Scene::TransformData& transform,
        const DirectX::XMFLOAT3& delta) const noexcept
    {
        auto apply = [this](float value, float change)
        {
            float result = value + change;
            if (snap_enabled_) result = std::round(result / snap_step_) * snap_step_;
            return result;
        };
        DirectX::XMFLOAT3* target = &transform.position;
        if (operation_ == GizmoOperation::Rotate) target = &transform.rotation;
        if (operation_ == GizmoOperation::Scale) target = &transform.scale;
        target->x = apply(target->x, delta.x);
        target->y = apply(target->y, delta.y);
        target->z = apply(target->z, delta.z);
        if (operation_ == GizmoOperation::Scale)
        {
            target->x = (std::max)(target->x, 0.001f);
            target->y = (std::max)(target->y, 0.001f);
            target->z = (std::max)(target->z, 0.001f);
        }
    }
}
