#pragma once

#include "ICameraBasisProvider.h"
#include "IPhysicsQueryService.h"
#include "RespawnService.h"
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

        // ---- Stage Gameplay -------------------------------------------------
        //
        // 復帰地点と出来事の記録。Scene が値で持つので Singleton にならない。
        // Play 用 Scene と編集 Scene がそれぞれ自分のぶんを持つ。
        RespawnService& Respawn() noexcept { return respawn_; }
        const RespawnService& Respawn() const noexcept { return respawn_; }

        GameplayEventLog& GameplayEvents() noexcept { return gameplay_events_; }
        const GameplayEventLog& GameplayEvents() const noexcept { return gameplay_events_; }

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
        }

    private:
        const ICameraBasisProvider* camera_basis_ = nullptr;
        const IPhysicsQueryService* physics_ = nullptr;
        Core::ObjectID controlled_object_;
        RespawnService respawn_;
        GameplayEventLog gameplay_events_;
        bool playing_ = false;
    };
}
