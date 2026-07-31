#pragma once

#include "ICameraBasisProvider.h"
#include "IPhysicsQueryService.h"
#include "ScenePlayerMigrationState.h"
#include "../../Core/ObjectID/ObjectID.h"

namespace ReplayEngine::Scene
{
    // Scene が Component へ提供する外部サービスの束。
    //
    // Singleton ではない。Scene が値メンバとして 1 つ持ち、
    // 中身はすべて「非所有の生ポインタ」。実体は framework 側が所有する。
    // 未設定なら nullptr のままで、Component は必ず null 確認をしてから使う。
    //
    // Component からの参照経路:
    //   Component -> GetScene() -> Services() -> Physics() / CameraBasis()
    //
    // これにより、Gameplay Component が Camera や Stage の具象型を
    // include しないまま、必要な情報だけを受け取れる。
    class SceneServices final
    {
    public:
        const ICameraBasisProvider* CameraBasis() const noexcept { return camera_basis_; }
        void SetCameraBasis(const ICameraBasisProvider* provider) noexcept
        {
            camera_basis_ = provider;
        }

        const IPhysicsQueryService* Physics() const noexcept { return physics_; }
        void SetPhysics(const IPhysicsQueryService* service) noexcept { physics_ = service; }

        // 操作対象の GameObject。
        // 「プレイヤーかどうか」を型ではなく ObjectID で表す。
        // 人型からメカ・ドローンへ切り替えても、GameObject のクラス型は変わらない。
        Core::ObjectID ControlledObject() const noexcept { return controlled_object_; }
        void SetControlledObject(Core::ObjectID id) noexcept { controlled_object_ = id; }

        // 旧 Player から GameObject への移行状態。
        // Component の有無とは独立しており、Component を消しても取り消されない。
        ScenePlayerMigrationState& PlayerMigration() noexcept { return migration_; }
        const ScenePlayerMigrationState& PlayerMigration() const noexcept { return migration_; }

        // Play Mode 中かどうか。
        // 入力を受け付けてよいか、物理を進めてよいかの判断に使う。
        bool Playing() const noexcept { return playing_; }
        void SetPlaying(bool value) noexcept { playing_ = value; }

        void Reset() noexcept
        {
            camera_basis_ = nullptr;
            physics_ = nullptr;
            controlled_object_ = Core::ObjectID::Invalid();
            playing_ = false;
            // 移行状態はここでリセットしない。Scene の内容に属する情報なので、
            // サービス参照の付け替えでは消えてはいけない。
        }

    private:
        const ICameraBasisProvider* camera_basis_ = nullptr;
        const IPhysicsQueryService* physics_ = nullptr;
        Core::ObjectID controlled_object_;
        ScenePlayerMigrationState migration_;
        bool playing_ = false;
    };
}
