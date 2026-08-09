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
        // v1 compatibility: Landscape local y=0 のブラシ中心。
        bool ApplySample(float local_x, float local_z, float delta_time)
        { return ApplySample({ local_x, 0.0f, local_z }, delta_time); }

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
