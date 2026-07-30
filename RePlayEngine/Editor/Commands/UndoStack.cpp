#include "UndoStack.h"

#include <utility>

namespace ReplayEngine::Editor
{
    void UndoStack::Commit(std::string label,
        const Scene::SceneDocument& before, const Scene::SceneDocument& after)
    {
        if (cursor_ < entries_.size()) entries_.erase(entries_.begin() + cursor_, entries_.end());
        entries_.push_back({ std::move(label), before, after });
        if (entries_.size() > maximum_entries_)
            entries_.erase(entries_.begin());
        cursor_ = entries_.size();
    }

    bool UndoStack::Undo(Scene::SceneDocument& scene, std::string& label)
    {
        if (!CanUndo()) return false;
        const Entry& entry = entries_[--cursor_];
        scene = entry.before;
        label = entry.label;
        return true;
    }

    bool UndoStack::Redo(Scene::SceneDocument& scene, std::string& label)
    {
        if (!CanRedo()) return false;
        const Entry& entry = entries_[cursor_++];
        scene = entry.after;
        label = entry.label;
        return true;
    }

    void UndoStack::Clear() noexcept
    {
        entries_.clear();
        cursor_ = 0;
    }
}
