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

    void EditorContext::ResetSceneState()
    {
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

    // Play 中に Undo / Redo を受け付けない理由と、その伝え方。
    //
    // ---------------------------------------------------------------------
    // 【なぜ実行しないか】
    //
    //   Undo は SceneData のスナップショットを Scene へ流し込む方式で、
    //   内部で Scene::Clear() を通って全 GameObject を作り直す。
    //   Play 中の Runtime World へこれを行うと、
    //     - 実行中の Component が Awake からやり直しになる
    //     - ObjectHandle / ComponentHandle がすべて WrongWorld になる
    //     - 編集 Scene の履歴が Runtime World の内容で上書きされる
    //   のいずれも起きる。Runtime 用の Undo は用意していないので、
    //   実行そのものを行わない。
    //
    // 【黙って無視しない】
    //   以前は CanEdit() で false を返すだけだったため、
    //   ユーザーには「押したのに何も起きない」としか見えなかった。
    //   理由を Status へ出して、不具合と区別できるようにする。
    // ---------------------------------------------------------------------
    bool EditorContext::Undo()
    {
        if (scene_ == nullptr) return false;
        if (play_mode_)
        {
            SetStatus("実行中は元に戻せません。Shift+F5 で停止してください。");
            return false;
        }

        std::string label;
        if (!history_.Undo(*scene_, label))
        {
            SetStatus("これ以上元に戻せません");
            return false;
        }

        // ApplySceneData は Scene を作り直す。
        // 実行状態の復元は SceneEditHistory::Undo が行う（started_ を戻す）。
        // ここでそれを重ねて呼ばないこと。二重に Start() すると
        // SynchronizeStates が余分に 1 回走る。
        selection_.PruneMissing(*scene_);
        MarkDirty();
        SetStatus("元に戻す: " + label);
        return true;
    }

    bool EditorContext::Redo()
    {
        if (scene_ == nullptr) return false;
        if (play_mode_)
        {
            SetStatus("実行中はやり直せません。Shift+F5 で停止してください。");
            return false;
        }

        std::string label;
        if (!history_.Redo(*scene_, label))
        {
            SetStatus("これ以上やり直せません");
            return false;
        }

        selection_.PruneMissing(*scene_);
        MarkDirty();
        SetStatus("やり直す: " + label);
        return true;
    }
}
