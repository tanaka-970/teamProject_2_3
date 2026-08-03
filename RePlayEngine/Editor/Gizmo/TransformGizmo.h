#pragma once

namespace ReplayEngine::Editor
{
    enum class GizmoOperation
    {
        Translate,
        Rotate,
        Scale
    };

    class TransformGizmo final
    {
    public:
        GizmoOperation Operation() const noexcept { return operation_; }
        void SetOperation(GizmoOperation operation) noexcept { operation_ = operation; }
        bool SnapEnabled() const noexcept { return snap_enabled_; }
        void SetSnapEnabled(bool enabled) noexcept { snap_enabled_ = enabled; }
        float SnapStep() const noexcept { return snap_step_; }
        void SetSnapStep(float value) noexcept;

    private:
        GizmoOperation operation_ = GizmoOperation::Translate;
        bool snap_enabled_ = false;
        float snap_step_ = 0.5f;
    };
}
