#pragma once

#include "../../Landscape/LandscapeUndoCommand.h"
#include "../../Scene/Serialization/SceneData.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Editor
{
    // Editor 操作の Undo / Redo。
    //
    // 実装方式:
    //   操作の前後で Scene 全体の SceneData スナップショットを取り、差し替えることで戻す。
    //   個々の操作を逆再生する方式より復元の正しさを保証しやすく、
    //   GameObject / Component の生ポインタを履歴へ持ち込まずに済むため、
    //   削除済みオブジェクトを指したまま Undo するといった事故が起きない。
    //
    //   SceneDataスナップショットを使い、GameObject/Component編集を一操作単位で復元する。
    //   Entity 数が増えるとメモリを食う方式なので、上限を設けて古い履歴から捨てる。
    //   将来、差分方式へ置き換える場合もこのクラスの内側だけで完結する。
    //
    // 使い方:
    //   history.Begin("Component を追加");   // 変更前の状態を控える
    //   ... Scene を書き換える ...
    //   history.Commit(scene);               // 変更後の状態を控えて 1 件積む
    //
    //   変更しなかった場合は Cancel() を呼ぶ。積まれない。
    class SceneEditHistory final
    {
    public:
        // 変更前スナップショットを取る。Begin 中にもう一度 Begin しても入れ子にはならない
        // （最初の Begin が優先される。ImGui のように毎フレーム描く UI から呼ぶため）。
        void Begin(const Scene::Scene& scene, std::string label);
        void Commit(const Scene::Scene& scene);
        void CommitLandscape(Core::ObjectID object,
            std::unique_ptr<Landscape::LandscapeUndoCommand> command, std::string label);
        void Cancel() noexcept;

        bool InTransaction() const noexcept { return in_transaction_; }

        bool CanUndo() const noexcept { return cursor_ > 0; }
        bool CanRedo() const noexcept { return cursor_ < entries_.size(); }

        // 成功したら scene を書き換え、label に操作名を入れる。
        bool Undo(Scene::Scene& scene, std::string& label);
        bool Redo(Scene::Scene& scene, std::string& label);

        void Clear() noexcept;

        std::size_t Count() const noexcept { return entries_.size(); }
        const std::string& LastLabel() const noexcept;

    private:
        struct Entry
        {
            std::string label;
            Scene::Serialization::SceneData before;
            Scene::Serialization::SceneData after;
            Core::ObjectID landscape_object;
            std::unique_ptr<Landscape::LandscapeUndoCommand> landscape_command;
        };

        static bool ApplyLandscape(const Entry& entry, Scene::Scene& scene, bool redo);

        // 1 操作あたり Scene 2 つぶんを持つため、既存 UndoStack より控えめにする。
        static constexpr std::size_t maximum_entries = 64;

        std::vector<Entry> entries_;
        std::size_t cursor_ = 0;

        bool in_transaction_ = false;
        std::string pending_label_;
        Scene::Serialization::SceneData pending_before_;
    };
}
