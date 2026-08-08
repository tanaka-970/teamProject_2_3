#pragma once

#include "LandscapeBrush.h"
#include "LandscapeUndoCommand.h"

#include <DirectXMath.h>
#include <memory>

namespace ReplayEngine::Landscape
{
    class LandscapeData;

    class LandscapeEditorTool final
    {
    public:
        bool BeginStroke(LandscapeData& data, LandscapeBrushMode mode,
            const LandscapeBrush& brush);

        // v2: 任意 Mesh のため center は xyz で受ける。洞窟壁も編集できる。
        bool ApplySample(const DirectX::XMFLOAT3& local_center, float delta_time);
        // v1 compatibility: 地表 y=0 のブラシ中心。
        bool ApplySample(float world_x, float world_z, float delta_time)
        { return ApplySample({ world_x, 0.0f, world_z }, delta_time); }

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
