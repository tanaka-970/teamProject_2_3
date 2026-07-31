#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class SphereColliderComponent;

    // キャラクターの移動・重力・ジャンプ・接地を担当する。
    //
    // 再利用可能であることが最重要:
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
    class CharacterMotorComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(CharacterMotorComponent)

    public:
        CharacterMotorComponent() = default;

        void OnStart() override;
        void OnFixedUpdate(float fixed_delta_time) override;
        void OnDisable() override;

        // ---- 移動要求（外から呼ぶ API）--------------------------------------

        // ワールド空間の水平移動方向。長さ 0〜1。次の FixedUpdate で消費される。
        void Move(const DirectX::XMFLOAT3& world_direction) noexcept;

        // ジャンプ要求。次の FixedUpdate で 1 回だけ消費される。
        // 可変フレームで複数回呼ばれても 1 回にまとまり、
        // FixedUpdate が 1 フレームに複数回走っても重複実行されない。
        void RequestJump() noexcept { jump_requested_ = true; }

        // 位置を直接指定する（テレポート）。速度は保持したままにする。
        void Teleport(const DirectX::XMFLOAT3& world_position);

        // ---- 公開状態（Animator などが読む）--------------------------------

        bool Grounded() const noexcept { return grounded_; }
        const DirectX::XMFLOAT3& Velocity() const noexcept { return velocity_; }
        const DirectX::XMFLOAT3& GroundNormal() const noexcept { return ground_normal_; }

        // 水平速度の大きさ。Animator が Idle / Walk を判定するのに使う。
        float PlanarSpeed() const noexcept;

        // 直近に受け取った移動方向。Controller が向きを決めるのに使う。
        const DirectX::XMFLOAT3& LastMoveDirection() const noexcept { return last_move_direction_; }

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

        // 重力と接地を有効にするか。
        // 旧 Player は vertical_physics_enabled_ が既定 false で、
        // 重力もジャンプも動いていなかった。挙動を変えないよう既定は false のまま。
        // Inspector から true にすれば重力とジャンプが有効になる。
        bool vertical_physics = false;

    private:
        SphereColliderComponent* FindCollider() const;
        void ResolveGround(const DirectX::XMFLOAT3& position, float radius,
            float walkable_normal_y);
        void ResolveWalls(const DirectX::XMFLOAT3& previous_position);

        // 実行時のみの状態。保存しない。
        DirectX::XMFLOAT3 velocity_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 ground_normal_{ 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT3 pending_move_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 last_move_direction_{ 0.0f, 0.0f, 0.0f };
        float ground_height_ = 0.0f;
        bool grounded_ = true;
        bool jump_requested_ = false;
        bool has_ground_ = false;
    };
}
