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
                a.expression.enabled != b.expression.enabled ||
                a.expression.source != b.expression.source ||
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

        bool SameEvent(const Motion::MotionEvent& a,
            const Motion::MotionEvent& b) noexcept
        {
            return NearlyEqual(a.time, b.time) && a.name == b.name &&
                a.parameter == b.parameter;
        }

        bool SameEventTrack(const Motion::MotionEventTrack& a,
            const Motion::MotionEventTrack& b) noexcept
        {
            if (a.object != b.object || a.events.size() != b.events.size()) return false;
            for (std::size_t i = 0; i < a.events.size(); ++i)
            {
                if (!SameEvent(a.events[i], b.events[i])) return false;
            }
            return true;
        }

        bool SameAsset(const Motion::MotionAsset& a,
            const Motion::MotionAsset& b) noexcept
        {
            if (a.name != b.name || !NearlyEqual(a.duration, b.duration) ||
                a.time_remap.guid != b.time_remap.guid ||
                a.tracks.size() != b.tracks.size() ||
                a.event_tracks.size() != b.event_tracks.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.tracks.size(); ++i)
            {
                if (!SameTrack(a.tracks[i], b.tracks[i])) return false;
            }
            for (std::size_t i = 0; i < a.event_tracks.size(); ++i)
            {
                if (!SameEventTrack(a.event_tracks[i], b.event_tracks[i])) return false;
            }
            return true;
        }

        bool SameCompositionLayer(const Motion::CompositionMotionLayer& a,
            const Motion::CompositionMotionLayer& b) noexcept
        {
            return a.name == b.name && a.motion_guid == b.motion_guid &&
                a.composition_guid == b.composition_guid &&
                NearlyEqual(a.start_offset, b.start_offset) &&
                NearlyEqual(a.in_time, b.in_time) &&
                NearlyEqual(a.out_time, b.out_time) &&
                NearlyEqual(a.time_scale, b.time_scale) &&
                NearlyEqual(a.weight, b.weight) && a.enabled == b.enabled;
        }

        bool SameCompositionMarker(const Motion::CompositionMarker& a,
            const Motion::CompositionMarker& b) noexcept
        {
            return a.name == b.name && NearlyEqual(a.time, b.time);
        }

        bool SameComposition(const Motion::CompositionAsset& a,
            const Motion::CompositionAsset& b) noexcept
        {
            if (a.name != b.name || !NearlyEqual(a.duration, b.duration) ||
                a.layers.size() != b.layers.size() ||
                a.markers.size() != b.markers.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.layers.size(); ++i)
            {
                if (!SameCompositionLayer(a.layers[i], b.layers[i])) return false;
            }
            for (std::size_t i = 0; i < a.markers.size(); ++i)
            {
                if (!SameCompositionMarker(a.markers[i], b.markers[i])) return false;
            }
            return true;
        }

        bool SameSceneFlowCondition(const Runtime::SceneFlowCondition& a,
            const Runtime::SceneFlowCondition& b) noexcept
        {
            return a.type == b.type && a.op == b.op && a.key == b.key &&
                a.value == b.value;
        }

        bool SameSceneFlowTransition(const Runtime::SceneFlowTransition& a,
            const Runtime::SceneFlowTransition& b) noexcept
        {
            if (a.id != b.id || a.enabled != b.enabled || a.priority != b.priority ||
                a.from_scene_guid != b.from_scene_guid ||
                a.event_name != b.event_name || a.to_scene_guid != b.to_scene_guid ||
                a.conditions.size() != b.conditions.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.conditions.size(); ++i)
            {
                if (!SameSceneFlowCondition(a.conditions[i], b.conditions[i])) return false;
            }
            return true;
        }

        bool SameSceneFlow(const Runtime::SceneFlowAsset& a,
            const Runtime::SceneFlowAsset& b) noexcept
        {
            if (a.name != b.name || a.transitions.size() != b.transitions.size())
                return false;
            for (std::size_t i = 0; i < a.transitions.size(); ++i)
            {
                if (!SameSceneFlowTransition(a.transitions[i], b.transitions[i])) return false;
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

    void CompositionEditHistory::Begin(const Motion::CompositionAsset& asset,
        std::string label)
    {
        if (in_transaction_) return;
        in_transaction_ = true;
        pending_label_ = std::move(label);
        pending_before_ = asset;
    }

    void CompositionEditHistory::Commit(const Motion::CompositionAsset& asset)
    {
        if (!in_transaction_) return;
        in_transaction_ = false;

        if (SameComposition(pending_before_, asset))
        {
            pending_label_.clear();
            pending_before_ = Motion::CompositionAsset{};
            return;
        }

        Entry entry;
        entry.label = std::move(pending_label_);
        entry.before = std::move(pending_before_);
        entry.after = asset;

        pending_label_.clear();
        pending_before_ = Motion::CompositionAsset{};

        if (cursor_ < entries_.size())
        {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                entries_.end());
        }

        entries_.push_back(std::move(entry));
        if (entries_.size() > maximum_entries) entries_.erase(entries_.begin());
        cursor_ = entries_.size();
    }

    void CompositionEditHistory::Cancel() noexcept
    {
        in_transaction_ = false;
        pending_label_.clear();
        pending_before_ = Motion::CompositionAsset{};
    }

    bool CompositionEditHistory::Undo(Motion::CompositionAsset& asset,
        std::string& label)
    {
        if (!CanUndo()) return false;
        Cancel();

        --cursor_;
        const Entry& entry = entries_[cursor_];
        asset = entry.before;
        label = entry.label;
        return true;
    }

    bool CompositionEditHistory::Redo(Motion::CompositionAsset& asset,
        std::string& label)
    {
        if (!CanRedo()) return false;
        Cancel();

        const Entry& entry = entries_[cursor_];
        asset = entry.after;
        label = entry.label;
        ++cursor_;
        return true;
    }

    void CompositionEditHistory::Clear() noexcept
    {
        entries_.clear();
        cursor_ = 0;
        Cancel();
    }

    void SceneFlowEditHistory::Begin(const Runtime::SceneFlowAsset& asset,
        std::string label)
    {
        if (in_transaction_) return;
        in_transaction_ = true;
        pending_label_ = std::move(label);
        pending_before_ = asset;
    }

    void SceneFlowEditHistory::Commit(const Runtime::SceneFlowAsset& asset)
    {
        if (!in_transaction_) return;
        in_transaction_ = false;

        if (SameSceneFlow(pending_before_, asset))
        {
            pending_label_.clear();
            pending_before_ = Runtime::SceneFlowAsset{};
            return;
        }

        Entry entry;
        entry.label = std::move(pending_label_);
        entry.before = std::move(pending_before_);
        entry.after = asset;

        pending_label_.clear();
        pending_before_ = Runtime::SceneFlowAsset{};

        if (cursor_ < entries_.size())
        {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                entries_.end());
        }

        entries_.push_back(std::move(entry));
        if (entries_.size() > maximum_entries) entries_.erase(entries_.begin());
        cursor_ = entries_.size();
    }

    void SceneFlowEditHistory::Cancel() noexcept
    {
        in_transaction_ = false;
        pending_label_.clear();
        pending_before_ = Runtime::SceneFlowAsset{};
    }

    bool SceneFlowEditHistory::Undo(Runtime::SceneFlowAsset& asset,
        std::string& label)
    {
        if (!CanUndo()) return false;
        Cancel();

        --cursor_;
        const Entry& entry = entries_[cursor_];
        asset = entry.before;
        label = entry.label;
        return true;
    }

    bool SceneFlowEditHistory::Redo(Runtime::SceneFlowAsset& asset,
        std::string& label)
    {
        if (!CanRedo()) return false;
        Cancel();

        const Entry& entry = entries_[cursor_];
        asset = entry.after;
        label = entry.label;
        ++cursor_;
        return true;
    }

    void SceneFlowEditHistory::Clear() noexcept
    {
        entries_.clear();
        cursor_ = 0;
        Cancel();
    }
}
