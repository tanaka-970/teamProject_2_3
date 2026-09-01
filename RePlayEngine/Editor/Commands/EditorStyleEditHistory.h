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
        struct Snapshot final
        {
            float button_scale = 1.0f;
            float font_scale = 1.0f;
            ImVec4 text_color{ 1.0f, 1.0f, 1.0f, 1.0f };
            EditorStyleTokens tokens{};
            bool style_overridden = false;
            std::string active_preset_id;
            Colors category_colors;
        };
        static constexpr std::size_t maximum_entries = 32;

        void Begin(const Snapshot& snapshot, std::string label)
        {
            if (in_transaction_) return;
            in_transaction_ = true;
            pending_label_ = std::move(label);
            pending_before_ = snapshot;
        }

        void Commit(const Snapshot& snapshot)
        {
            if (!in_transaction_) return;
            if (!SameSnapshot(pending_before_, snapshot))
            {
                if (cursor_ < entries_.size()) entries_.erase(entries_.begin() +
                    static_cast<std::ptrdiff_t>(cursor_), entries_.end());
                entries_.push_back({ pending_label_, pending_before_, snapshot });
                if (entries_.size() > maximum_entries) entries_.erase(entries_.begin());
                cursor_ = entries_.size();
            }
            Cancel();
        }

        void Cancel() noexcept
        {
            in_transaction_ = false;
            pending_label_.clear();
            pending_before_ = Snapshot{};
        }

        bool Undo(Snapshot& snapshot, std::string& label)
        {
            if (cursor_ == 0 || in_transaction_) return false;
            --cursor_;
            snapshot = entries_[cursor_].before;
            label = entries_[cursor_].label;
            return true;
        }

        bool Redo(Snapshot& snapshot, std::string& label)
        {
            if (cursor_ >= entries_.size() || in_transaction_) return false;
            snapshot = entries_[cursor_].after;
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

        static bool SameSnapshot(const Snapshot& left, const Snapshot& right) noexcept
        {
            return left.button_scale == right.button_scale &&
                left.font_scale == right.font_scale &&
                left.text_color.x == right.text_color.x &&
                left.text_color.y == right.text_color.y &&
                left.text_color.z == right.text_color.z &&
                left.text_color.w == right.text_color.w &&
                SameTokens(left.tokens, right.tokens) &&
                left.style_overridden == right.style_overridden &&
                left.active_preset_id == right.active_preset_id &&
                SameColors(left.category_colors, right.category_colors);
        }

        static bool SameTokens(const EditorStyleTokens& left,
            const EditorStyleTokens& right) noexcept
        {
            const auto same_color = [](const ImVec4& a, const ImVec4& b)
            {
                return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
            };
            return same_color(left.main_background, right.main_background) && same_color(left.panel_background, right.panel_background) &&
                same_color(left.header_background, right.header_background) && same_color(left.toolbar_background, right.toolbar_background) &&
                same_color(left.border, right.border) && same_color(left.text, right.text) && same_color(left.secondary_text, right.secondary_text) &&
                same_color(left.disabled_text, right.disabled_text) && same_color(left.accent, right.accent) && same_color(left.selection, right.selection) &&
                same_color(left.hover, right.hover) && same_color(left.active, right.active) && same_color(left.success, right.success) &&
                same_color(left.warning, right.warning) && same_color(left.error, right.error) && same_color(left.missing, right.missing) &&
                same_color(left.prefab, right.prefab) && same_color(left.normal_collider, right.normal_collider) &&
                same_color(left.trigger_collider, right.trigger_collider) && same_color(left.primary_collider, right.primary_collider) &&
                left.padding == right.padding && left.item_spacing == right.item_spacing && left.panel_spacing == right.panel_spacing &&
                left.header_height == right.header_height && left.toolbar_height == right.toolbar_height && left.input_height == right.input_height &&
                left.border_thickness == right.border_thickness && left.corner_radius == right.corner_radius && left.font_size == right.font_size;
        }

        struct Entry final
        {
            std::string label;
            Snapshot before;
            Snapshot after;
        };

        std::vector<Entry> entries_;
        std::size_t cursor_ = 0;
        bool in_transaction_ = false;
        std::string pending_label_;
        Snapshot pending_before_;
    };
}
