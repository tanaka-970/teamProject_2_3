#pragma once

#include "../../Object/Component/Component.h"

namespace ReplayEngine::Components
{
    // 入力の取得だけを担当する。
    //
    // 禁止していること:
    //   Transform を触らない / 速度を更新しない / 重力を計算しない /
    //   Collider へ触らない / Animator を変更しない / 地形や描画に依存しない。
    //   ここは「今フレームの入力値」を保持するだけの箱。
    //
    // フレーム入力と FixedUpdate 入力の扱い:
    //   軸入力（move_x / move_y）は連続値なので、OnUpdate で毎フレーム上書きする。
    //   FixedUpdate はその時点の値をそのまま読めばよい。
    //
    //   ジャンプのような「押した瞬間」の入力はラッチする。
    //     OnUpdate で押下を検出したら jump_latched_ を立てる
    //     消費側（PlayerController）が ConsumeJump() で 1 回だけ取り出す
    //   これにより、
    //     - 1 フレームに FixedUpdate が 0 回 -> 次フレームまで保持されて消えない
    //     - 1 フレームに FixedUpdate が 3 回 -> 最初の 1 回だけ消費される
    //   という挙動になる。
    class PlayerInputComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(PlayerInputComponent)

    public:
        PlayerInputComponent() = default;

        void OnUpdate(float delta_time) override;
        void OnDisable() override;

        // ---- 読み取り API ---------------------------------------------------

        float MoveX() const noexcept { return move_x_; }
        float MoveY() const noexcept { return move_y_; }
        bool Dashing() const noexcept { return dash_held_; }

        // ジャンプ押下を 1 回だけ取り出す。取り出すとフラグは下がる。
        bool ConsumeJump() noexcept;

        // 消費せずに状態だけ見る（Inspector 表示やデバッグ用）。
        bool JumpLatched() const noexcept { return jump_latched_; }

        // ---- 保存される設定 -------------------------------------------------

        // false の間は入力を一切拾わない。
        // Component 自体を無効にしても同じ結果になるが、
        // ムービー中だけ操作を止めるといった用途で使い分けられるようにしておく。
        bool input_enabled = true;

        // ローカルプレイヤー番号。将来の 2P 対応やデバイス割り当て用。
        // 現状は 0 のみを扱い、値は保存だけする。
        int local_player_slot = 0;

    private:
        // 実行時のみの状態。保存しない。
        float move_x_ = 0.0f;
        float move_y_ = 0.0f;
        bool dash_held_ = false;
        bool jump_latched_ = false;
    };
}
