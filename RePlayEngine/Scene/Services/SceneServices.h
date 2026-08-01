#pragma once

#include "ICameraBasisProvider.h"
#include "IPhysicsQueryService.h"
#include "LegacyStageMigrationState.h"
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
        //
        // この ID が無効なとき、Scene には操作対象が居ない。
        // 代わりに別の GameObject を探したり、何かを自動生成したりはしない。
        Core::ObjectID ControlledObject() const noexcept { return controlled_object_; }
        void SetControlledObject(Core::ObjectID id) noexcept { controlled_object_ = id; }

        // ---- 衝突まわりの Scene 単位の設定 ----------------------------------
        //
        // 【なぜ Scene が持つのか】
        //   Backend Mode を Collider ごとの Property にすると、
        //   同じ Scene の中に「Legacy を見る Collider」と「見ない Collider」が
        //   混在してしまい、二重衝突の管理が破綻する。
        //   Scene に 1 つだけ持たせて、SceneCollisionWorld が読む形にしてある。
        //
        //   実体はここ（Scene の状態）にあり、SceneCollisionWorld へは
        //   framework が毎フレーム流し込む。Scene ファイルへ保存されるのもここ。
        //
        // 0 = Legacy Only / 1 = Hybrid / 2 = Scene Colliders Only
        int CollisionBackendMode() const noexcept { return collision_backend_mode_; }
        void SetCollisionBackendMode(int mode) noexcept { collision_backend_mode_ = mode; }

        // 旧 Stage の衝突から MeshCollider へ移行した「移行元」の集合。
        // 単一 bool ではないので、一部だけ移行した状態も表せる。
        LegacyStageMigrationState& LegacyStageMigration() noexcept { return legacy_stage_; }
        const LegacyStageMigrationState& LegacyStageMigration() const noexcept
        {
            return legacy_stage_;
        }

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
            // Backend Mode と旧 Stage の移行済み集合はここでリセットしない。
            // Scene の内容に属する情報なので、サービス参照の付け替えでは消えてはいけない。
        }

    private:
        const ICameraBasisProvider* camera_basis_ = nullptr;
        const IPhysicsQueryService* physics_ = nullptr;
        Core::ObjectID controlled_object_;
        LegacyStageMigrationState legacy_stage_;
        int collision_backend_mode_ = 1;   // Hybrid
        bool playing_ = false;
    };
}
