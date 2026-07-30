#pragma once

#include "../../Scene/SceneDocument.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    class UndoStack final
    {
    public:
        void Commit(std::string label,
            const Scene::SceneDocument& before, const Scene::SceneDocument& after);
        bool Undo(Scene::SceneDocument& scene, std::string& label);
        bool Redo(Scene::SceneDocument& scene, std::string& label);
        void Clear() noexcept;

        bool CanUndo() const noexcept { return cursor_ > 0; }
        bool CanRedo() const noexcept { return cursor_ < entries_.size(); }

    private:
        struct Entry
        {
            std::string label;
            Scene::SceneDocument before;
            Scene::SceneDocument after;
        };
        std::vector<Entry> entries_;
        std::size_t cursor_ = 0;
        static constexpr std::size_t maximum_entries_ = 128;
    };
}
