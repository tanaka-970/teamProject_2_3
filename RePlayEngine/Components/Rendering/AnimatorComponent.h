#pragma once

#include "../../Object/Component/Component.h"

namespace ReplayEngine::Components
{
    class CharacterMotorComponent;

    // アニメーションの状態と時間だけを管理する。
    //
    // 持たないもの:
    //   GPU メッシュ、ボーン行列、頂点バッファ、Shader。
    //   ここが持つのは「どのクリップを、どこまで再生したか」という数値だけ。
    //   実際の描画は SkinnedMeshRendererComponent が提出し、既存 Renderer が行う。
    //
    // 状態の決め方:
    //   CharacterMotorComponent の公開状態（接地しているか / 水平速度）を読んで、
    //   Idle / Walk / Jump のどれかを選ぶ。
    //   Motor が無い場合は Idle のまま時間だけ進める。
    //
    //   商用エンジンのようなステートマシンは作らない。
    //   旧 Player と同じ 3 状態が正しく切り替わることを目標にしている。
    class AnimatorComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(AnimatorComponent)

    public:
        enum class State
        {
            Idle = 0,
            Walk,
            Jump,
        };

        AnimatorComponent() = default;

        void OnStart() override;
        void OnUpdate(float delta_time) override;
        void OnDisable() override;

        // ---- 読み取り API ---------------------------------------------------

        State CurrentState() const noexcept { return state_; }

        // 実際に再生すべきクリップ番号。割り当てが無ければ -1。
        int CurrentClip() const noexcept { return current_clip_; }

        float AnimationTime() const noexcept { return animation_time_; }

        // ---- 保存される設定 -------------------------------------------------
        // クリップ番号は skinned_mesh の animation_clips の添字。
        // -1 なら「その状態では切り替えない」を意味する（旧 Player と同じ）。

        int idle_clip = -1;
        int walk_clip = -1;
        int jump_clip = -1;

        float playback_speed = 1.0f;
        bool loop = true;

        // 水平速度がこの値を超えたら Walk とみなす。旧 Player は 0.2f。
        float walk_speed_threshold = 0.2f;

        // false にすると時間を進めない。ポーズやデバッグ用。
        bool playing = true;

    private:
        CharacterMotorComponent* FindMotor() const;
        int ClipForState(State state) const noexcept;

        // 実行時のみの状態。保存しない。
        State state_ = State::Idle;
        int current_clip_ = -1;
        float animation_time_ = 0.0f;
    };
}
