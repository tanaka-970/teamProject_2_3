#include "SceneEditHistory.h"

#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Editor
{
    using Scene::Serialization::ApplySceneData;
    using Scene::Serialization::CaptureScene;
    using Scene::Serialization::SceneLoadReport;

    namespace
    {
        const std::string& EmptyLabel() noexcept
        {
            static const std::string empty;
            return empty;
        }
    }

    void SceneEditHistory::Begin(const Scene::Scene& scene, std::string label)
    {
        // 既に開始済みなら最初のものを優先する。
        // ImGui は毎フレーム同じコードを通るため、二重 Begin が普通に起きる。
        if (in_transaction_) return;

        in_transaction_ = true;
        pending_label_ = std::move(label);
        CaptureScene(scene, pending_before_);
    }

    void SceneEditHistory::Commit(const Scene::Scene& scene)
    {
        if (!in_transaction_) return;
        in_transaction_ = false;

        Entry entry;
        entry.label = std::move(pending_label_);
        entry.before = std::move(pending_before_);
        CaptureScene(scene, entry.after);

        pending_label_.clear();
        pending_before_.Clear();

        // Undo 済みの先を捨ててから積む。
        if (cursor_ < entries_.size()) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());

        entries_.push_back(std::move(entry));
        if (entries_.size() > maximum_entries)
        {
            entries_.erase(entries_.begin());
        }
        cursor_ = entries_.size();
    }

    void SceneEditHistory::Cancel() noexcept
    {
        in_transaction_ = false;
        pending_label_.clear();
        pending_before_.Clear();
    }

    bool SceneEditHistory::Undo(Scene::Scene& scene, std::string& label)
    {
        if (!CanUndo()) return false;

        // 進行中の編集があれば捨てる。中途半端な状態を履歴へ持ち込まない。
        Cancel();

        --cursor_;
        const Entry& entry = entries_[cursor_];
        SceneLoadReport report;
        ApplySceneData(entry.before, scene, report);
        label = entry.label;
        return true;
    }

    bool SceneEditHistory::Redo(Scene::Scene& scene, std::string& label)
    {
        if (!CanRedo()) return false;

        Cancel();

        const Entry& entry = entries_[cursor_];
        SceneLoadReport report;
        ApplySceneData(entry.after, scene, report);
        label = entry.label;
        ++cursor_;
        return true;
    }

    void SceneEditHistory::Clear() noexcept
    {
        entries_.clear();
        cursor_ = 0;
        Cancel();
    }

    const std::string& SceneEditHistory::LastLabel() const noexcept
    {
        if (cursor_ == 0 || entries_.empty()) return EmptyLabel();
        return entries_[cursor_ - 1].label;
    }
}
