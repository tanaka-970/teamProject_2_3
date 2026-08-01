#include "AnimatorComponent.h"

#include "../Gameplay/CharacterMotorComponent.h"
#include "../../Object/GameObject/GameObject.h"

namespace ReplayEngine::Components
{
    void AnimatorComponent::OnStart()
    {
        state_ = State::Idle;
        animation_time_ = 0.0f;
        current_clip_ = ClipForState(State::Idle);
    }

    void AnimatorComponent::OnDisable()
    {
        // 無効化しても現在のクリップは保持する。
        // 再有効化したときに姿勢が飛ばないようにするため。
        // 時間だけ止まる。
    }

    CharacterMotorComponent* AnimatorComponent::FindMotor() const
    {
        Core::GameObject* owner = Owner();
        return owner != nullptr ? owner->GetComponent<CharacterMotorComponent>() : nullptr;
    }

    int AnimatorComponent::ClipForState(State state) const noexcept
    {
        switch (state)
        {
        case State::Jump: return jump_clip;
        case State::Walk: return walk_clip;
        case State::Idle: break;
        }
        return idle_clip;
    }

    void AnimatorComponent::OnUpdate(float delta_time)
    {
        // Motor が無くても落ちない。その場合は Idle のまま時間だけ進める。
        const CharacterMotorComponent* motor = FindMotor();

        State desired = State::Idle;
        if (motor != nullptr && motor->ActiveInHierarchy())
        {
            if (!motor->Grounded()) desired = State::Jump;
            else if (motor->PlanarSpeed() > walk_speed_threshold) desired = State::Walk;
        }

        // 状態が変わったら時間を戻す。旧 Player の挙動と合わせる。
        if (desired != state_)
        {
            state_ = desired;
            animation_time_ = 0.0f;
        }

        // 割り当てが無い状態（-1）では、直前のクリップを維持する。
        // 旧 Player も -1 のときは framework 側の手動選択を残していた。
        const int clip = ClipForState(state_);
        if (clip >= 0 && clip != current_clip_)
        {
            current_clip_ = clip;
            animation_time_ = 0.0f;
        }
        else if (clip >= 0)
        {
            current_clip_ = clip;
        }

        if (playing) animation_time_ += delta_time * playback_speed;
        if (animation_time_ < 0.0f) animation_time_ = 0.0f;

        // ループ処理はクリップ長を知る Renderer 側が行う。
        // ここでは値が際限なく増えないよう、十分大きくなったら巻き戻すだけにする。
        if (loop && animation_time_ > 3600.0f) animation_time_ = 0.0f;
    }
}
