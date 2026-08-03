#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    class CharacterMotorComponent;
    class PlayerInputComponent;

    // 入力と移動をつなぐ橋渡し。
    //
    // やること:
    //   PlayerInputComponent から入力を読む
    //   カメラ基準の方向をワールド方向へ変換する
    //   CharacterMotorComponent へ移動要求とジャンプ要求を渡す
    //   進行方向へ向きを回す
    //   移動状態を公開する（Animator が読む）
    //
    // やらないこと:
    //   速度を自分で積分しない（Motor の仕事）
    //   衝突計算をしない（PhysicsQueryService と Motor の仕事）
    //   Animator のクリップ番号を直接いじらない（Animator が自分で決める）
    //   HP を管理しない（HealthComponent の仕事）
    //   GPU に触らない
    //
    // カメラ依存について:
    //   既存 Camera クラスを include しない。
    //   Scene の ICameraBasisProvider から forward / right を受け取るだけ。
    //   これにより、カメラ実装を差し替えてもこの Component は変更不要。
    class PlayerControllerComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(PlayerControllerComponent)

    public:
        PlayerControllerComponent() = default;

        void OnStart() override;
        void OnUpdate(float delta_time) override;

        // 必要な Component が揃っているか。Inspector の警告表示に使う。
        bool HasRequiredComponents() const;

        // 不足している Component の説明。揃っていれば空文字。
        const char* MissingRequirementText() const;

        // ---- 保存される設定 -------------------------------------------------

        // 旋回速度（度/秒）。旧 Player の turn_speed = 720 度/秒 と同じ。
        float turn_speed_degrees = 720.0f;

        // ダッシュ時の移動速度倍率。1.0 で無効。
        float dash_multiplier = 1.0f;

        // カメラ基準で移動するか。false ならワールド軸基準。
        bool camera_relative = true;

        // 進行方向へ自動で向きを変えるか。
        bool rotate_towards_movement = true;

    private:
        CharacterMotorComponent* FindMotor() const;
        PlayerInputComponent* FindInput() const;

        // カメラ基準（無ければワールド軸）で入力方向をワールドへ変換する。
        DirectX::XMFLOAT3 ResolveWorldDirection(float input_x, float input_y) const;

        void RotateTowards(const DirectX::XMFLOAT3& direction, float delta_time);

        // 必要 Component 不足の警告を一度だけ出すためのフラグ。
        bool requirement_warning_logged_ = false;
    };
}
