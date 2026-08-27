#include "MotionEditHistory.h"

#include "../../Reflection/Property/PropertyValue.h"

#include <cmath>
#include <utility>

namespace ReplayEngine::Editor
{
    namespace
    {
        bool NearlyEqual(float a, float b) noexcept
        {
            return std::fabs(a - b) <= 0.00001f;
        }

        bool SameBinding(const Motion::MotionBinding& a,
            const Motion::MotionBinding& b) noexcept
        {
            return a.origin == b.origin &&
                a.object == b.object &&
                a.component_type == b.component_type &&
                a.component_index == b.component_index &&
                a.property == b.property &&
                a.relative_path == b.relative_path;
        }

        bool SameKey(const Motion::MotionKeyframe& a,
            const Motion::MotionKeyframe& b) noexcept
        {
            return NearlyEqual(a.time, b.time) &&
                a.easing == b.easing &&
                a.easing_curve.guid == b.easing_curve.guid &&
                NearlyEqual(a.bezier.out_handle.x, b.bezier.out_handle.x) &&
                NearlyEqual(a.bezier.out_handle.y, b.bezier.out_handle.y) &&
                NearlyEqual(a.bezier.in_handle.x, b.bezier.in_handle.x) &&
                NearlyEqual(a.bezier.in_handle.y, b.bezier.in_handle.y) &&
                Reflection::ValuesEqual(a.value, b.value);
        }

        bool SameTrack(const Motion::MotionTrack& a,
            const Motion::MotionTrack& b) noexcept
        {
            if (a.name != b.name || !SameBinding(a.binding, b.binding) ||
                a.value_type != b.value_type || a.enabled != b.enabled ||
                a.blend_mode != b.blend_mode || a.loop != b.loop ||
                a.wiggle.enabled != b.wiggle.enabled ||
                !NearlyEqual(a.wiggle.amplitude, b.wiggle.amplitude) ||
                !NearlyEqual(a.wiggle.frequency, b.wiggle.frequency) ||
                a.wiggle.seed != b.wiggle.seed || a.wiggle.octaves != b.wiggle.octaves ||
                a.keys.size() != b.keys.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.keys.size(); ++i)
            {
                if (!SameKey(a.keys[i], b.keys[i])) return false;
            }
            return true;
        }

        bool SameAsset(const Motion::MotionAsset& a,
            const Motion::MotionAsset& b) noexcept
        {
            if (a.name != b.name || !NearlyEqual(a.duration, b.duration) ||
                a.time_remap.guid != b.time_remap.guid ||
                a.tracks.size() != b.tracks.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.tracks.size(); ++i)
            {
                if (!SameTrack(a.tracks[i], b.tracks[i])) return false;
            }
            return true;
        }
    }

    void MotionEditHistory::Begin(const Motion::MotionAsset& asset,
        std::string label)
    {
        if (in_transaction_) return;
        in_transaction_ = true;
        pending_label_ = std::move(label);
        pending_before_ = asset;
    }

    void MotionEditHistory::Commit(const Motion::MotionAsset& asset)
    {
        if (!in_transaction_) return;
        in_transaction_ = false;

        if (SameAsset(pending_before_, asset))
        {
            pending_label_.clear();
            pending_before_ = Motion::MotionAsset{};
            return;
        }

        Entry entry;
        entry.label = std::move(pending_label_);
        entry.before = std::move(pending_before_);
        entry.after = asset;

        pending_label_.clear();
        pending_before_ = Motion::MotionAsset{};

        if (cursor_ < entries_.size())
        {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                entries_.end());
        }

        entries_.push_back(std::move(entry));
        if (entries_.size() > maximum_entries) entries_.erase(entries_.begin());
        cursor_ = entries_.size();
    }

    void MotionEditHistory::Cancel() noexcept
    {
        in_transaction_ = false;
        pending_label_.clear();
        pending_before_ = Motion::MotionAsset{};
    }

    bool MotionEditHistory::Undo(Motion::MotionAsset& asset, std::string& label)
    {
        if (!CanUndo()) return false;
        Cancel();

        --cursor_;
        const Entry& entry = entries_[cursor_];
        asset = entry.before;
        label = entry.label;
        return true;
    }

    bool MotionEditHistory::Redo(Motion::MotionAsset& asset, std::string& label)
    {
        if (!CanRedo()) return false;
        Cancel();

        const Entry& entry = entries_[cursor_];
        asset = entry.after;
        label = entry.label;
        ++cursor_;
        return true;
    }

    void MotionEditHistory::Clear() noexcept
    {
        entries_.clear();
        cursor_ = 0;
        Cancel();
    }
}
