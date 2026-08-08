#pragma once

#include <DirectXMath.h>

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Landscape
{
    class LandscapeData;

    // 1 stroke の頂点位置差分。
    // Height ではなく xyz を持つので、洞窟壁を法線方向へ Sculpt した操作も戻せる。
    class LandscapeUndoCommand final
    {
    public:
        struct Sample
        {
            std::size_t index = 0;
            DirectX::XMFLOAT3 before{};
            DirectX::XMFLOAT3 after{};
        };

        void RecordPosition(std::size_t index,
            const DirectX::XMFLOAT3& before, const DirectX::XMFLOAT3& after);

        // v1 validation / 呼び出し互換。Y だけの変更として記録する。
        void Record(std::size_t index, float before, float after);

        void Undo(LandscapeData& data) const;
        void Redo(LandscapeData& data) const;
        bool Empty() const noexcept { return samples_.empty(); }
        std::size_t ChangedSampleCount() const noexcept { return samples_.size(); }

    private:
        std::vector<Sample> samples_;
        std::unordered_map<std::size_t, std::size_t> lookup_;
    };
}
