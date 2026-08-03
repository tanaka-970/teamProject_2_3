#pragma once

#include "../../Core/ObjectID/ObjectID.h"

#include <cstddef>
#include <vector>

namespace ReplayEngine::Scene { class Scene; }
namespace ReplayEngine::Core { class GameObject; }

namespace ReplayEngine::Editor
{
    // Editor が選択している GameObject を管理する。
    //
    // 重要な方針: GameObject の生ポインタを保持しない。
    //   選択状態は ObjectID だけで持ち、描画のたびに Scene から引き直す。
    //   こうしておくと、選択中の GameObject が削除されても無効ポインタが残らない。
    //   Inspector を描画している最中に対象が消えるケースはこの設計で構造的に防いでいる。
    class EditorSelection final
    {
    public:
        void Clear() noexcept;

        // 単独選択。additive が true なら追加選択。
        void Select(Core::ObjectID id, bool additive = false);
        void Deselect(Core::ObjectID id) noexcept;
        void Toggle(Core::ObjectID id);

        bool IsSelected(Core::ObjectID id) const noexcept;
        bool Empty() const noexcept { return ids_.empty(); }
        std::size_t Count() const noexcept { return ids_.size(); }

        // 主選択。Inspector が表示する対象。
        Core::ObjectID Primary() const noexcept { return primary_; }
        const std::vector<Core::ObjectID>& All() const noexcept { return ids_; }

        // Scene から実体を引き直す。存在しない ID はここで自動的に取り除かれる。
        Core::GameObject* ResolvePrimary(const Scene::Scene& scene) const;

        // 削除された GameObject を選択から外す。Scene 更新後に毎フレーム呼ぶ。
        void PruneMissing(const Scene::Scene& scene);

    private:
        std::vector<Core::ObjectID> ids_;
        Core::ObjectID primary_;
    };
}
