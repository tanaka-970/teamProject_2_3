#pragma once

#include "../../Object/Component/Component.h"
#include "../../Scene/Services/IPhysicsQueryService.h"

#include <DirectXMath.h>

#include <string>

namespace ReplayEngine::Components
{
    class ColliderComponent;

    // キャラクターの移動・重力・ジャンプ・接地を担当する。
    //
    // 再利用可能であることが最重要://
    //   プレイヤー専用にしない。誰が移動要求を出したかを一切知らない。
    //     PlayerControllerComponent -> Move()/RequestJump() -> CharacterMotor
    //     EnemyAIComponent          -> Move()/RequestJump() -> CharacterMotor
    //   のどちらでも同じように動く。
    //
    // 依存してはいけないもの:
    //   キーボード / ゲームパッド、Camera 具象型、Animator のクリップ番号、
    //   Stage 具象型、特定のモデル。
    //   地形問い合わせは IPhysicsQueryService（Scene のサービス）経由でのみ行う。
    //
    // 更新タイミング:
    //   速度と位置の積分は OnFixedUpdate（固定 1/60 秒）で行う。
    //   移動要求は OnUpdate 側（可変フレーム）から積まれ、
    //   次の FixedUpdate でまとめて消費される。
    //
    // ---------------------------------------------------------------------
    // 【Primary Collider について】
    //   移動に使う Collider は Inspector から明示的に選ぶ。
    //   「同じ GameObject にある最初の Collider」のような暗黙の自動選択はしない。
    //   Collider が複数ある構成（本体 + 攻撃判定 + 検知範囲）で、
    //   どれが移動用なのかがコードを読まないと分からない状態を避けるため。
    //
    //   参照は collider_key（GameObject 内で一意・Scene へ保存される番号）で持つ。
    //   Component 名でも GameObject 名でもないので、名前を変えても壊れない。
    //
    //   Mesh Collider と Trigger Collider は選べない。
    //   Inspector 側でも候補に出さないし、ここでも受け付けない。
    class CharacterMotorComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(CharacterMotorComponent)

    public:
        CharacterMotorComponent() = default;

        void OnStart() override;
        void OnFixedUpdate(float fixed_delta_time) override;
        void OnDisable() override;

        // ---- 移動要求（外から呼ぶ API）--------------------------------------

        // ワールド空間の水平移動方向。長さは方向としてだけ扱い、正規化して使う。
        // 次の FixedUpdate で消費される。
        void Move(const DirectX::XMFLOAT3& world_direction) noexcept;

        // 方向と速度倍率を分けて渡す版。
        // speed_multiplier は move_speed に対する倍率で、Dash や AI の速度補正に使う。
        void Move(const DirectX::XMFLOAT3& world_direction,
            float speed_multiplier) noexcept;

        // ジャンプ要求。次の FixedUpdate で 1 回だけ消費される。
        // 可変フレームで複数回呼ばれても 1 回にまとまり、
        // FixedUpdate が 1 フレームに複数回走っても重複実行されない。
        void RequestJump() noexcept { jump_requested_ = true; }

        // 位置を直接指定する（テレポート）。速度は保持したままにする。
        void Teleport(const DirectX::XMFLOAT3& world_position);

        // JumpPad など汎用 Gameplay Component から速度を加える。
        // Player 型や入力系には依存せず、次の FixedUpdate で通常の衝突解決を通る。
        void ApplyImpulse(const DirectX::XMFLOAT3& impulse) noexcept
        {
            velocity_.x += impulse.x;
            velocity_.y += impulse.y;
            velocity_.z += impulse.z;
            grounded_ = false;
        }

        // ---- 公開状態（Animator などが読む）--------------------------------

        bool Grounded() const noexcept { return grounded_; }
        const DirectX::XMFLOAT3& Velocity() const noexcept { return velocity_; }
        const DirectX::XMFLOAT3& GroundNormal() const noexcept { return ground_normal_; }

        // 水平速度の大きさ。Animator が Idle / Walk を判定するのに使う。
        float PlanarSpeed() const noexcept;

        // 直近に受け取った移動方向。Controller が向きを決めるのに使う。
        const DirectX::XMFLOAT3& LastMoveDirection() const noexcept { return last_move_direction_; }

        // ---- Primary Collider ------------------------------------------------

        // 実際に使う Collider。見つからない・使えない形状なら nullptr。
        ColliderComponent* ResolvePrimaryCollider() const;

        // Inspector へ出す警告。空なら問題なし。
        std::string PrimaryColliderStatus() const;

        // 旧 Player 変換のときだけ使う。既存の Sphere Collider を Primary にする。
        void SetPrimaryCollider(const ColliderComponent& collider) noexcept;

        // ---- 診断 -------------------------------------------------------------

        const Scene::CollisionSourceInfo& LastGroundSource() const noexcept
        {
            return last_ground_source_;
        }
        const Scene::CollisionSourceInfo& LastWallSource() const noexcept
        {
            return last_wall_source_;
        }
        bool HasGround() const noexcept { return has_ground_; }

        // ---- 直近の接触の記録 (Collision Event 配送用) ------------------------
        //
        // ここに置いてあるのは「移動を解決するために撃った問い合わせが返した値」
        // そのものであり、剛体どうしの衝突解決の結果ではない。
        // 記録するだけで、移動の挙動には一切影響しない。
        //
        // CollisionEventDispatcher が毎フレームこれを読み、
        // 前フレームとの差分から Enter / Stay / Exit を組み立てる。
        // Motor 側からイベントを発行しないのは、配送の順序と
        // 生存確認を 1 か所（Dispatcher）に集めるため。

        // 直近の FixedUpdate で壁に接触したか。接触が無ければ false。
        bool HasWallContact() const noexcept { return has_wall_contact_; }

        // 接地点（ワールド）。HasGround() が false のときの値は意味を持たない。
        const DirectX::XMFLOAT3& LastGroundPoint() const noexcept
        {
            return last_ground_point_;
        }

        // 壁に接触したときの球中心（ワールド）と面法線。
        const DirectX::XMFLOAT3& LastWallPoint() const noexcept { return last_wall_point_; }
        const DirectX::XMFLOAT3& LastWallNormal() const noexcept { return last_wall_normal_; }

        // ---- 保存されるパラメータ（PropertyRegistry へ登録）------------------
        // 既定値は旧 Player の値をそのまま引き継いでいる。挙動を変えないため。

        float move_speed = 6.0f;
        float acceleration = 30.0f;
        float deceleration = 20.0f;

        // 空中での操作の効き（0〜1）。1 で地上と同じ。
        float air_control = 0.35f;

        float gravity = 18.0f;
        float jump_power = 8.0f;
        float maximum_fall_speed = 55.0f;

        // 地形が無い場合に床とみなす高さ。旧 Player の ground_y に相当。
        float fallback_ground_y = 0.0f;

        // 登れる段差の高さ。接地判定はこの高さだけ上から真下へ球を落として探す。
        //
        // 【なぜ必要か】
        //   ここは以前 80.0f という無名の定数だった。80 単位上から探すため、
        //   頭上に歩ける面があるとそれを「床」と判定し、キャラクターが
        //   そこまで一瞬で持ち上げられていた。壁の横に立っただけで
        //   壁の上へ瞬間移動する状態で、2026-08-17 に敵 AI が壁へ直進して
        //   初めて踏んだ。人が操作するプレイヤーは壁へ押し付け続けないため
        //   それまで表に出ていなかった。
        //
        //   「探索を何単位上から始めるか」は本来「どれだけの段差を登れるか」
        //   そのものなので、無名の定数ではなく設定として持たせる。
        //
        // 【値の意味】
        //   これより低い段差は登る。これより高い面は床として拾わないので、
        //   壁として ResolveWalls が押し戻す。
        //   大きくすると壁を登れてしまい、0 にすると段差を一切登れない。
        float max_step_height = 0.4f;

        // 重力と接地を有効にするか。
        // 旧 Player は vertical_physics_enabled_ が既定 false で、
        // 重力もジャンプも動いていなかった。挙動を変えないよう既定は false のまま。
        // Inspector から true にすれば重力とジャンプが有効になる。
        bool vertical_physics = false;

        // 移動に使う Collider の collider_key。0 は未設定。
        // Inspector では番号ではなく Collider 名の一覧から選ぶ。
        int primary_collider_key = 0;

    private:
        // 形状から「移動判定に使う球」を求める。
        //
        // 問い合わせ窓口が球のスイープしか持たないため、
        // 形状ごとに安全側の球へ落として使う。
        //   Sphere  … そのまま
        //   Capsule … 半径はそのまま。接地は下側の半球、壁は中央の球を使う
        //   Box     … 内接球（最小の半辺長）。角は拾えないが、
        //              すり抜けは起きない側の近似になる
        struct MotionSphere
        {
            // Owner のワールド位置からの相対。
            DirectX::XMFLOAT3 ground_offset{ 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 wall_offset{ 0.0f, 0.0f, 0.0f };
            float radius = 0.38f;
            float skin_width = 0.015f;
            float walkable_normal_y = 0.25f;

            // Layer / Mask と「自分自身を除外する」指定をまとめて持つ。
            Scene::CollisionQueryFilter filter;

            bool valid = false;
        };
        MotionSphere BuildMotionSphere() const;

        void ResolveGround(const MotionSphere& shape);
        void ResolveWalls(const MotionSphere& shape,
            const DirectX::XMFLOAT3& previous_position);

        // 実行時のみの状態。保存しない。
        DirectX::XMFLOAT3 velocity_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 ground_normal_{ 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT3 pending_move_{ 0.0f, 0.0f, 0.0f };
        float pending_speed_multiplier_ = 1.0f;
        DirectX::XMFLOAT3 last_move_direction_{ 0.0f, 0.0f, 0.0f };
        Scene::CollisionSourceInfo last_ground_source_;
        Scene::CollisionSourceInfo last_wall_source_;

        // Collision Event 配送用の記録。移動の計算には使わない。
        DirectX::XMFLOAT3 last_ground_point_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 last_wall_point_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 last_wall_normal_{ 0.0f, 1.0f, 0.0f };
        bool has_wall_contact_ = false;
        float ground_height_ = 0.0f;
        bool grounded_ = true;
        bool jump_requested_ = false;
        bool has_ground_ = false;
    };
}
