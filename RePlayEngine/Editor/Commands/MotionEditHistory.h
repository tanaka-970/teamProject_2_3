#pragma once

#include "../../Motion/MotionAsset.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    class MotionEditHistory final
    {
    public:
        void Begin(const Motion::MotionAsset& asset, std::string label);
        void Commit(const Motion::MotionAsset& asset);
        void Cancel() noexcept;

        bool InTransaction() const noexcept { return in_transaction_; }
        bool CanUndo() const noexcept { return cursor_ > 0; }
        bool CanRedo() const noexcept { return cursor_ < entries_.size(); }

        bool Undo(Motion::MotionAsset& asset, std::string& label);
        bool Redo(Motion::MotionAsset& asset, std::string& label);

        void Clear() noexcept;

    private:
        struct Entry
        {
            std::string label;
            Motion::MotionAsset before;
            Motion::MotionAsset after;
        };

        static constexpr std::size_t maximum_entries = 64;

        std::vector<Entry> entries_;
        std::size_t cursor_ = 0;

        bool in_transaction_ = false;
        std::string pending_label_;
        Motion::MotionAsset pending_before_;
    };
}
