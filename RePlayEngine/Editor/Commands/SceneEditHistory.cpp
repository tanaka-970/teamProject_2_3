#include "SceneEditHistory.h"

#include "../../Components/Landscape/LandscapeComponent.h"
#include "../../Object/GameObject/GameObject.h"
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

    void SceneEditHistory::CommitLandscape(Core::ObjectID object,
        std::unique_ptr<Landscape::LandscapeUndoCommand> command, std::string label)
    {
        if (command == nullptr || command->Empty() || !object.Valid()) return;
        Cancel();
        if (cursor_ < entries_.size()) entries_.erase(
            entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());

        Entry entry;
        entry.label = std::move(label);
        entry.landscape_object = object;
        entry.landscape_command = std::move(command);
        entries_.push_back(std::move(entry));
        if (entries_.size() > maximum_entries) entries_.erase(entries_.begin());
        cursor_ = entries_.size();
    }

    void SceneEditHistory::Cancel() noexcept
    {
        in_transaction_ = false;
        pending_label_.clear();
        pending_before_.Clear();
    }

    namespace
    {
        // スナップショットを流し込む。
        //
        // ---------------------------------------------------------------------
        // 【Scene::Start() を呼び直すのが要点】
        //
        //   ApplySceneData は内部で Scene::Clear() を通る。
        //   Clear() は最後に started_ を false へ落とす（Scene.cpp）。
        //   そして ApplySceneData 自身は Start() を呼ばない。
        //   これは仕様で、SceneData.h にも「呼び出し側が Scene::Start() を呼ぶ」と
        //   書かれている。
        //
        //   実際、他の呼び出し箇所はすべて対で呼んでいる。
        //     framework::load_object_scene   … ApplySceneData の直後に object_scene.Start()
        //     RuntimeSceneService::SwapWorlds … 入れ替え後に active_->Start()
        //
        //   Undo / Redo だけがこれを欠いていた。その結果、一度でも Undo を押すと
        //   scene.started_ が false のまま戻らず、
        //   Scene::Update / FixedUpdate / LateUpdate が先頭で早期 return するため、
        //   Scene 上の全 Component が止まる。
        //
        //   Animator が止まって見えるのはそれが一番目に見えるからで、
        //   実際には Rotator も CharacterMotor も同時に止まっていた。
        //   保存せずに再起動すると直るのは、読み込み経路が Start() を通るため。
        //
        // 【started_ を復元する形にした理由】
        //   無条件に Start() を呼ぶと、まだ開始していない Scene（Editor で
        //   読み込んだ直後など）を Undo しただけで動き出してしまう。
        //   「元の状態へ戻す」のが Undo の意味なので、実行状態も元へ戻す。
        // ---------------------------------------------------------------------
        void ApplySnapshot(const Scene::Serialization::SceneData& data, Scene::Scene& scene)
        {
            const bool was_started = scene.Started();

            SceneLoadReport report;
            ApplySceneData(data, scene, report);

            if (was_started) scene.Start();
        }
    }

    bool SceneEditHistory::ApplyLandscape(const Entry& entry, Scene::Scene& scene, bool redo)
    {
        Core::GameObject* object = scene.FindGameObjectByID(entry.landscape_object);
        if (object == nullptr || object->PendingDestroy() || entry.landscape_command == nullptr)
            return false;
        auto* landscape = object->GetComponent<Components::LandscapeComponent>();
        if (landscape == nullptr) return false;
        if (redo) entry.landscape_command->Redo(landscape->Data());
        else entry.landscape_command->Undo(landscape->Data());
        return true;
    }

    bool SceneEditHistory::Undo(Scene::Scene& scene, std::string& label)
    {
        if (!CanUndo()) return false;

        // 進行中の編集があれば捨てる。中途半端な状態を履歴へ持ち込まない。
        Cancel();

        const Entry& entry = entries_[cursor_ - 1];
        if (entry.landscape_command != nullptr)
        {
            if (!ApplyLandscape(entry, scene, false)) return false;
        }
        else ApplySnapshot(entry.before, scene);
        --cursor_;
        label = entry.label;
        return true;
    }

    bool SceneEditHistory::Redo(Scene::Scene& scene, std::string& label)
    {
        if (!CanRedo()) return false;

        Cancel();

        const Entry& entry = entries_[cursor_];
        if (entry.landscape_command != nullptr)
        {
            if (!ApplyLandscape(entry, scene, true)) return false;
        }
        else ApplySnapshot(entry.after, scene);
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
