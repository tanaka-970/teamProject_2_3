#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/PropertyDesc.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ReplayEngine::Components
{
    class CharacterMotorComponent;

    // スケルタルアニメーション専用の State Machine。
    //
    // Motion Runtime は Component Property を動かす別系統なので、ここへ統合しない。
    // 既存 skinned_mesh の clip/keyframe/blend_animations を使い、Animator は
    // 「どのクリップを何秒で、どの状態からどれだけブレンドするか」だけを管理する。
    class AnimatorComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(AnimatorComponent)

    public:
        // 旧 Scene / Prefab と C++ 呼び出し側の互換性だけのために残す。
        // states が 1 件以上ある場合、新しい State Machine の判定には使わない。
        enum class State
        {
            Idle = 0,
            Walk,
            Jump,
        };

        // 末尾追加を守る。Scene へ整数として保存される。
        enum class TransitionCondition
        {
            Always = 0,
            Grounded,
            Airborne,
            PlanarSpeedGreater,
            PlanarSpeedLessEqual,
            VerticalSpeedGreater,
            VerticalSpeedLessEqual,
        };

        struct AnimationState
        {
            std::string name;
            int clip = -1;
            bool loop = true;
            float speed = 1.0f;
        };

        struct AnimationTransition
        {
            // from が空または "*" なら全状態から遷移できる。
            std::string from;
            std::string to;
            TransitionCondition condition = TransitionCondition::Always;
            float threshold = 0.0f;
            float blend_time = 0.15f;
        };

        AnimatorComponent() = default;

        void OnStart() override;
        void OnUpdate(float delta_time) override;
        void OnDisable() override;
        void OnPropertyChanged(const char* property_name) override;

        const std::vector<Reflection::PropertyDesc>*
            DynamicProperties() const noexcept override;

        // ---- 読み取り API ---------------------------------------------------

        State CurrentState() const noexcept { return legacy_state_; }
        const std::string& CurrentStateName() const noexcept { return current_state_name_; }

        int CurrentClip() const noexcept { return current_clip_; }
        float AnimationTime() const noexcept { return animation_time_; }
        bool CurrentLoop() const noexcept { return current_loop_; }

        int PreviousClip() const noexcept { return previous_clip_; }
        float PreviousAnimationTime() const noexcept { return previous_animation_time_; }
        bool PreviousLoop() const noexcept { return previous_loop_; }

        // 0=遷移元、1=遷移先。遷移していなければ常に 1。
        float BlendFactor() const noexcept;
        bool IsBlending() const noexcept
        {
            return previous_clip_ >= 0 && transition_duration_ > 0.0f &&
                transition_time_ < transition_duration_;
        }

        int StateCount() const noexcept { return static_cast<int>(states.size()); }
        void SetStateCount(int count);
        int TransitionCount() const noexcept { return static_cast<int>(transitions.size()); }
        void SetTransitionCount(int count);

        // ---- 新しい data-driven 設定 ----------------------------------------

        std::string default_state;
        std::vector<AnimationState> states;
        std::vector<AnimationTransition> transitions;

        // ---- 旧 3 状態設定 ---------------------------------------------------
        // states が空の Scene だけで使う。既存 Scene を読み直したときの挙動を変えない。

        int idle_clip = -1;
        int walk_clip = -1;
        int jump_clip = -1;

        float playback_speed = 1.0f;
        bool loop = true;
        float walk_speed_threshold = 0.2f;
        bool playing = true;

    private:
        CharacterMotorComponent* FindMotor() const;
        int ClipForLegacyState(State state) const noexcept;

        int FindStateIndex(const std::string& name) const noexcept;
        bool TransitionMatches(const AnimationTransition& transition,
            const CharacterMotorComponent* motor) const noexcept;
        void EnterState(int state_index, float blend_time);
        void ResetRuntimeState();
        void RebuildDynamicProperties() const;

        // 実行時のみ。Scene には保存しない。
        State legacy_state_ = State::Idle;
        int current_state_index_ = -1;
        std::string current_state_name_;

        int current_clip_ = -1;
        float animation_time_ = 0.0f;
        bool current_loop_ = true;
        float current_speed_ = 1.0f;

        int previous_clip_ = -1;
        float previous_animation_time_ = 0.0f;
        bool previous_loop_ = true;
        float previous_speed_ = 1.0f;
        float transition_time_ = 0.0f;
        float transition_duration_ = 0.0f;

        mutable std::vector<Reflection::PropertyDesc> dynamic_properties_;
    };
}
