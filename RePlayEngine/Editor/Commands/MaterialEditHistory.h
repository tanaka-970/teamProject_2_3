#pragma once

#include "../../Rendering/Materials/MaterialAsset.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ReplayEngine::Editor
{
    class MaterialEditHistory final
    {
    public:
        static constexpr std::size_t maximum_entries = 64;

        void Begin(const Rendering::MaterialAsset& asset, std::string label)
        {
            if (in_transaction_) return;
            in_transaction_ = true;
            pending_label_ = std::move(label);
            pending_before_ = asset;
        }

        void Commit(const Rendering::MaterialAsset& asset)
        {
            if (!in_transaction_) return;
            if (cursor_ < entries_.size()) entries_.erase(entries_.begin() +
                static_cast<std::ptrdiff_t>(cursor_), entries_.end());
            entries_.push_back({ pending_label_, pending_before_, asset });
            if (entries_.size() > maximum_entries) entries_.erase(entries_.begin());
            cursor_ = entries_.size();
            in_transaction_ = false;
            pending_label_.clear();
            pending_before_ = Rendering::MaterialAsset{};
        }

        void Cancel() noexcept
        {
            in_transaction_ = false;
            pending_label_.clear();
            pending_before_ = Rendering::MaterialAsset{};
        }

        bool Undo(Rendering::MaterialAsset& asset, std::string& label)
        {
            if (cursor_ == 0 || in_transaction_) return false;
            --cursor_;
            asset = entries_[cursor_].before;
            label = entries_[cursor_].label;
            return true;
        }

        bool Redo(Rendering::MaterialAsset& asset, std::string& label)
        {
            if (cursor_ >= entries_.size() || in_transaction_) return false;
            asset = entries_[cursor_].after;
            label = entries_[cursor_].label;
            ++cursor_;
            return true;
        }

        bool InTransaction() const noexcept { return in_transaction_; }
        bool CanUndo() const noexcept { return cursor_ > 0; }
        bool CanRedo() const noexcept { return cursor_ < entries_.size(); }

        void Clear() noexcept
        {
            entries_.clear();
            cursor_ = 0;
            Cancel();
        }

    private:
        struct Entry final
        {
            std::string label;
            Rendering::MaterialAsset before;
            Rendering::MaterialAsset after;
        };

        std::vector<Entry> entries_;
        std::size_t cursor_ = 0;
        bool in_transaction_ = false;
        std::string pending_label_;
        Rendering::MaterialAsset pending_before_;
    };
}
