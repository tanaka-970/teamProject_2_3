#pragma once

#include "../../Core/ObjectID/ObjectID.h"

namespace ReplayEngine::Scene
{
    // 「旧 Player から GameObject への移行が済んでいるか」を持つ Scene 単位の状態。
    //
    // なぜ Component の有無と分けるか:
    //   以前は「PlayerControllerComponent があるか」で旧 Player 経路を切り替えていた。
    //   そのため Controller を削除しただけで旧 Player が復活し、
    //   同じキャラクターが 2 体表示されるという事故が起きていた。
    //
    //   移行状態と、操作可能かどうかは別の話。
    //     移行済み        … 旧 Player は二度と出さない（Scene に保存する）
    //     操作対象        … PlayerControlSystem が持つ ObjectID
    //     構成が揃っている … PlayerCompositionValidator が判定する
    //
    //   Component を消しても「移行済み」は取り消されない。
    //   消えるのは操作だけで、GameObject も表示もそのまま残る。
    //
    // この状態は Scene ファイル (v8 以降) へ保存され、再起動後も維持される。
    class ScenePlayerMigrationState final
    {
    public:
        bool Migrated() const noexcept { return migrated_; }

        // 移行対象として作られた GameObject。参考情報であり、
        // これが消えても Migrated() は false へ戻らない。
        Core::ObjectID MigratedObject() const noexcept { return migrated_object_; }

        void MarkMigrated(Core::ObjectID object) noexcept
        {
            migrated_ = true;
            migrated_object_ = object;
        }

        // 保存データからの復元用。
        void Restore(bool migrated, Core::ObjectID object) noexcept
        {
            migrated_ = migrated;
            migrated_object_ = object;
        }

        // 新規 Scene を作るときだけ使う。Component 削除では絶対に呼ばない。
        void Reset() noexcept
        {
            migrated_ = false;
            migrated_object_ = Core::ObjectID::Invalid();
        }

    private:
        bool migrated_ = false;
        Core::ObjectID migrated_object_;
    };
}
