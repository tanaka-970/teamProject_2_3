#pragma once

#include "LandscapeBrush.h"
#include "LandscapeUndoCommand.h"

#include <memory>

namespace ReplayEngine::Landscape
{
    class LandscapeData;

    class LandscapeEditorTool final
    {
    public:
        bool BeginStroke(LandscapeData& data, LandscapeBrushMode mode,
            const LandscapeBrush& brush);
        bool ApplySample(float world_x, float world_z, float delta_time);
        std::unique_ptr<LandscapeUndoCommand> EndStroke();
        void CancelStroke();
        bool StrokeActive() const noexcept { return data_ != nullptr; }

    private:
        LandscapeData* data_ = nullptr;
        LandscapeBrushMode mode_ = LandscapeBrushMode::Raise;
        LandscapeBrush brush_;
        std::unique_ptr<LandscapeUndoCommand> command_;
    };
}
