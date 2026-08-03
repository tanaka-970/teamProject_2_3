#pragma once

#include "../Behaviour/BehaviourEvents.h"
#include "../Handles/RuntimeHandles.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Runtime
{
    // CharacterMotor の接触を Behaviour の OnCollisionEnter / Stay / Exit へ配送する。
    //
    // ---------------------------------------------------------------------
    // 【配れる接触の範囲】
    //
    //   RePlayEngine に剛体物理エンジンは無い。
    //   Collider どうしの衝突を解く処理そのものが存在しないため、
    //   一般的な RigidBody 衝突は「取得手段が無い」。ここでも配らない。
    //
    //   実際に取れるのは、CharacterMotor が自分の移動を解決するために
    //   毎 FixedUpdate で撃っている 2 種類の問い合わせの結果だけ。
    //
    //     CharacterGround … QueryGroundFiltered が返した床
    //                       接地点と面法線が取れる
    //     CharacterWall   … SweepSphereFiltered が当たった壁
    //                       接触時の球中心と面法線が取れる
    //
    //   どちらも CollisionHitKind で必ず区別して配る。
    //   「Motor 専用の Hit を汎用 Collision として配る」ことはしない。
    //
    //   次は配れない（実装したふりをしない）:
    //     - Collider 対 Collider の一般衝突
    //     - Dynamic どうし / Static どうしの接触
    //     - めり込み量 (penetration depth)
    //     - 相対速度
    //
    // ---------------------------------------------------------------------
    // 【Trigger との二重配送を起こさない理由】
    //
    //   経路も入口も完全に分かれている。
    //     Trigger    … SceneCollisionWorld::DispatchTriggerEvents が
    //                  overlap ペアを判定し、Component::OnTriggerXxx を呼ぶ。
    //     Collision  … このクラスが CharacterMotor の記録を読み、
    //                  BehaviourComponent::OnCollisionXxx を呼ぶ。
    //
    //   同じ 2 つの GameObject が Trigger でも接触しうるが、
    //   その場合に届くのは OnTrigger 系と OnCollision 系という別のコールバックで、
    //   同じコールバックが 2 回呼ばれることはない。
    //
    //   さらに、Trigger として登録された Collider は
    //   Motor の問い合わせ（SweepSphere / QueryGround）の対象から
    //   SceneCollisionWorld 側で除外されている。
    //   つまり Trigger Collider が CollisionEvent の相手になることは無い。
    //
    // ---------------------------------------------------------------------
    // 所有関係:
    //   Scene も Motor も所有しない。毎フレーム Scene を走査して読むだけ。
    //   接触状態は ObjectHandle と ColliderID でしか覚えないので、
    //   相手が消えても宙に浮いたポインタが残らない。
    class CollisionEventDispatcher final
    {
    public:
        CollisionEventDispatcher() = default;

        // 1 フレーム分の配送。FixedUpdate のあと、位置が確定してから呼ぶ。
        //
        // world が前回と違う World 実体になっていた場合、
        // 接触状態は自動的に捨てられる（Scene 切り替え・再読み込み対応）。
        // 消える World の接触に対して Exit を配ることはしない。
        // 相手も自分も既に居ないため、配送先が存在しない。
        void Dispatch(Scene::Scene& world, std::uint64_t frame_index);

        // 接触状態を明示的に捨てる。Play 停止・Scene 破棄で呼ぶ。
        void Reset() noexcept;

        // ---- 診断 ------------------------------------------------------------

        std::size_t ActiveContactCount() const noexcept { return contacts_.size(); }
        std::uint64_t EnterCount() const noexcept { return enter_count_; }
        std::uint64_t StayCount() const noexcept { return stay_count_; }
        std::uint64_t ExitCount() const noexcept { return exit_count_; }

        // 配送先が見つからず捨てた件数（Behaviour が無効・削除予約済みなど）。
        std::uint64_t SkippedCount() const noexcept { return skipped_count_; }

    private:
        // 接触 1 件分の状態。
        //
        // 生ポインタを持たない。次のフレームまで持ち越す情報なので、
        // ポインタで覚えると、その間に消えた相手を指したままになる。
        struct Contact
        {
            ObjectHandle self;
            CollisionHitKind kind = CollisionHitKind::Unknown;

            Core::ObjectID other;
            Scene::ColliderID other_collider = Scene::invalid_collider_id;

            DirectX::XMFLOAT3 point{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };

            // このフレームでも接触が続いていたか。走査後に false のものが Exit。
            bool seen_this_frame = false;
        };

        // Behaviour へ 1 件配る。配送してよいかの判定もここで行う。
        void Deliver(Scene::Scene& world, const Contact& contact, ContactPhase phase,
            std::uint64_t frame_index);

        // 今フレームの接触を記録する。既存と相手が違えば Exit -> Enter になる。
        void Observe(Scene::Scene& world, const ObjectHandle& self, CollisionHitKind kind,
            Core::ObjectID other, Scene::ColliderID other_collider,
            const DirectX::XMFLOAT3& point, const DirectX::XMFLOAT3& normal,
            std::uint64_t frame_index);

        std::vector<Contact> contacts_;

        // 直近に配送した World。変わったら接触状態を捨てる。
        Core::WorldInstanceID world_instance_ = Core::invalid_world_instance_id;

        std::uint64_t enter_count_ = 0;
        std::uint64_t stay_count_ = 0;
        std::uint64_t exit_count_ = 0;
        std::uint64_t skipped_count_ = 0;
    };
}
