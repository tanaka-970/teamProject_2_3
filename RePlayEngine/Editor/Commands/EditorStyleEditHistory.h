#pragma once

#include "../Style/EditorStyle.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ReplayEngine::Editor
{
    class EditorStyleEditHistory final
    {
    public:
        using Colors = std::unordered_map<std::string, ImVec4>;
        static constexpr std::size_t maximum_entries = 32;

        void Begin(const Colors& colors, std::string label)
        {
            if (in_transaction_) return;
            in_transaction_ = true;
            pending_label_ = std::move(label);
            pending_before_ = colors;
        }

        void Commit(const Colors& colors)
        {
            if (!in_transaction_) return;
            if (!SameColors(pending_before_, colors))
            {
                if (cursor_ < entries_.size()) entries_.erase(entries_.begin() +
                    static_cast<std::ptrdiff_t>(cursor_), entries_.end());
                entries_.push_back({ pending_label_, pending_before_, colors });
                if (entries_.size() > maximum_entries) entries_.erase(entries_.begin());
                cursor_ = entries_.size();
            }
            Cancel();
        }

        void Cancel() noexcept
        {
            in_transaction_ = false;
            pending_label_.clear();
            pending_before_.clear();
        }

        bool Undo(Colors& colors, std::string& label)
        {
            if (cursor_ == 0 || in_transaction_) return false;
            --cursor_;
            colors = entries_[cursor_].before;
            label = entries_[cursor_].label;
            return true;
        }

        bool Redo(Colors& colors, std::string& label)
        {
            if (cursor_ >= entries_.size() || in_transaction_) return false;
            colors = entries_[cursor_].after;
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
        static bool SameColors(const Colors& left, const Colors& right) noexcept
        {
            if (left.size() != right.size()) return false;
            for (const auto& entry : left)
            {
                const auto found = right.find(entry.first);
                if (found == right.end()) return false;
                if (entry.second.x != found->second.x ||
                    entry.second.y != found->second.y ||
                    entry.second.z != found->second.z ||
                    entry.second.w != found->second.w) return false;
            }
            return true;
        }

        struct Entry final
        {
            std::string label;
            Colors before;
            Colors after;
        };

        std::vector<Entry> entries_;
        std::size_t cursor_ = 0;
        bool in_transaction_ = false;
        std::string pending_label_;
        Colors pending_before_;
    };
}
