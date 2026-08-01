#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Landscape
{
    class LandscapeData;

    class LandscapeUndoCommand final
    {
    public:
        struct Sample
        {
            std::size_t index = 0;
            float before = 0.0f;
            float after = 0.0f;
        };

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
