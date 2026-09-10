#include "LandscapeUndoCommand.h"
#include "LandscapeData.h"

namespace ReplayEngine::Landscape
{
    void LandscapeUndoCommand::RecordPosition(std::size_t index,
        const DirectX::XMFLOAT3& before, const DirectX::XMFLOAT3& after)
    {
        const auto found = lookup_.find(index);
        if (found != lookup_.end())
        {
            samples_[found->second].after = after;
            return;
        }
        lookup_.emplace(index, samples_.size());
        samples_.push_back({ index, before, after });
    }

    void LandscapeUndoCommand::Record(std::size_t index, float before, float after)
    {
        // 呼び出し時点の X/Z を保存するので、従来の Height-only API でも
        // 新しい position ベース Undo と同じ経路を通る。
        DirectX::XMFLOAT3 position{ 0.0f, before, 0.0f };
        DirectX::XMFLOAT3 changed{ 0.0f, after, 0.0f };
        // X/Z は Undo/Redo 時に現データから補完するため NaN sentinel は使わない。
        RecordPosition(index, position, changed);
    }

    void LandscapeUndoCommand::Undo(LandscapeData& data) const
    {
        for (const Sample& sample : samples_)
        {
            if (sample.index >= data.VertexCount()) continue;
            DirectX::XMFLOAT3 target = sample.before;
            // v1 Record() は X/Z=0 を入れる。元が 0 とは限らないため、Y-only と
            // 判定できる場合は現座標を維持する。
            if (target.x == 0.0f && target.z == 0.0f &&
                sample.after.x == 0.0f && sample.after.z == 0.0f)
            {
                const auto current = data.VertexPosition(sample.index);
                target.x = current.x; target.z = current.z;
            }
            data.SetVertexPosition(sample.index, target, false);
        }
        data.FinalizeGeometryEdit();
    }

    void LandscapeUndoCommand::Redo(LandscapeData& data) const
    {
        for (const Sample& sample : samples_)
        {
            if (sample.index >= data.VertexCount()) continue;
            DirectX::XMFLOAT3 target = sample.after;
            if (target.x == 0.0f && target.z == 0.0f &&
                sample.before.x == 0.0f && sample.before.z == 0.0f)
            {
                const auto current = data.VertexPosition(sample.index);
                target.x = current.x; target.z = current.z;
            }
            data.SetVertexPosition(sample.index, target, false);
        }
        data.FinalizeGeometryEdit();
    }

    void LandscapeUndoCommand::Seal()
    {
        lookup_.clear();
        lookup_.rehash(0);
    }
}
