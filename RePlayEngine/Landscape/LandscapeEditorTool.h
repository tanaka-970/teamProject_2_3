#pragma once

#include "LandscapeBrush.h"
#include "LandscapeUndoCommand.h"
#include "LandscapeData.h"

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

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
        bool ApplyStrokeSample(const LandscapeRayHit& hit, float delta_time);
        void BreakStrokeSampling() noexcept { previous_hit_valid_ = false; }
        // v1 compatibility: Landscape local y=0 のブラシ中心。
        bool ApplySample(float local_x, float local_z, float delta_time)
        { return ApplySample({ local_x, 0.0f, local_z }, delta_time); }
        static bool ApplySubdivideSample(LandscapeData& data,
            const DirectX::XMFLOAT3& local_center, std::size_t hit_face,
            const LandscapeBrush& brush);

        std::unique_ptr<LandscapeUndoCommand> EndStroke();
        void CancelStroke();
        bool StrokeActive() const noexcept { return data_ != nullptr; }

    private:
        bool ApplySurfaceSample(const LandscapeRayHit& hit, float delta_time);
        LandscapeRayHit previous_hit_{};
        bool previous_hit_valid_ = false;
        LandscapeData::SurfaceRegion region_, interpolation_region_;
        struct Change { std::size_t index; DirectX::XMFLOAT3 before, after; };
        std::vector<Change> changes_;
        LandscapeData* data_ = nullptr;
        LandscapeBrushMode mode_ = LandscapeBrushMode::Raise;
        LandscapeBrush brush_;
        std::unique_ptr<LandscapeUndoCommand> command_;
        std::vector<std::vector<std::uint32_t>> adjacency_;
        std::vector<std::uint32_t> candidate_marks_;
        std::vector<std::uint32_t> unindexed_vertices_;
        std::uint32_t candidate_generation_ = 0;
    };
}
