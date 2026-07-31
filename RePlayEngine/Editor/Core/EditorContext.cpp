#include "EditorContext.h"

#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Editor
{
    void EditorContext::AttachScene(Scene::Scene* scene)
    {
        if (scene_ == scene) return;
        scene_ = scene;

        // 別の Scene へ切り替わったら、前の Scene の ObjectID を指したままにしない。
        selection_.Clear();
        history_.Clear();
    }

    std::string EditorContext::DisplayTitle() const
    {
        std::string title = scene_path_.empty()
            ? std::string("(未保存のシーン)")
            : scene_path_.filename().string();
        if (dirty_) title += "*";
        return title;
    }

    void EditorContext::BeginEdit(std::string label)
    {
        if (!CanEdit()) return;
        history_.Begin(*scene_, std::move(label));
    }

    void EditorContext::CommitEdit()
    {
        if (!CanEdit() || !history_.InTransaction()) return;

        // 削除予約をここで確定させてから記録する。
        // 予約が残ったまま スナップショットを取ると、消えるはずの
        // GameObject / Component が Undo 履歴へ混ざってしまう。
        scene_->ProcessPendingOperations();

        history_.Commit(*scene_);
        MarkDirty();

        // 消えた GameObject を選択から外す。
        selection_.PruneMissing(*scene_);
    }

    void EditorContext::CancelEdit() noexcept
    {
        history_.Cancel();
    }

    bool EditorContext::Undo()
    {
        if (!CanEdit()) return false;

        std::string label;
        if (!history_.Undo(*scene_, label)) return false;

        selection_.PruneMissing(*scene_);
        MarkDirty();
        SetStatus("元に戻す: " + label);

        // ApplySceneData は Scene を作り直すため、実行中なら開始し直す必要がある。
        return true;
    }

    bool EditorContext::Redo()
    {
        if (!CanEdit()) return false;

        std::string label;
        if (!history_.Redo(*scene_, label)) return false;

        selection_.PruneMissing(*scene_);
        MarkDirty();
        SetStatus("やり直す: " + label);
        return true;
    }
}
