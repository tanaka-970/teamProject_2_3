#include "LandscapeUndoCommand.h"

#include "LandscapeData.h"

namespace ReplayEngine::Landscape
{
    void LandscapeUndoCommand::Record(std::size_t index, float before, float after)
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

    void LandscapeUndoCommand::Undo(LandscapeData& data) const
    {
        for (const Sample& sample : samples_) data.SetHeightByIndex(sample.index, sample.before);
    }

    void LandscapeUndoCommand::Redo(LandscapeData& data) const
    {
        for (const Sample& sample : samples_) data.SetHeightByIndex(sample.index, sample.after);
    }
}
