#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"
#include "../../Reflection/Property/PropertyValue.h"
#include "../../Motion/MotionAsset.h"

#include <string>
#include <vector>

namespace ReplayEngine::Components
{
    class MotionPlayerComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(MotionPlayerComponent)

    public:
        enum PlayState : int
        {
            Stopped = 0,
            Playing = 1,
            Paused = 2,
        };

        enum WrapMode : int
        {
            Once = 0,
            Loop = 1,
            PingPong = 2,
            ClampForever = 3,
        };

        struct SnapshotValue
        {
            Motion::MotionBinding binding;
            Reflection::PropertyValue value;
        };

        MotionPlayerComponent() = default;

        void OnRuntimeAwake() override;

        bool ShouldContribute() const noexcept;
        void ResetPlayback() noexcept;
        void Advance(float duration, float delta_time) noexcept;

        void Play() noexcept;
        void PlayFrom(float seconds) noexcept;
        void Pause() noexcept;
        void Resume() noexcept;
        void Stop() noexcept;
        void StopAndKeep() noexcept;

        bool NeedsSnapshot() const noexcept;
        void StoreSnapshot(std::vector<SnapshotValue> values);
        const Reflection::PropertyValue* SnapshotFor(
            const Motion::MotionBinding& binding) const noexcept;
        float BlendInAlpha() const noexcept;
        bool HasStopRestoreRequest() const noexcept;
        const std::vector<SnapshotValue>& SnapshotValues() const noexcept;
        void ConsumeStopRestoreRequest() noexcept;

        Reflection::AssetReference motion;
        std::string key;
        bool play_on_start = true;
        bool loop = false;
        int wrap_mode = Once;
        bool auto_stop_on_end = false;
        float blend_in_seconds = 0.0f;
        float speed = 1.0f;
        float weight = 1.0f;
        int state = Stopped;

        float time = 0.0f;

    private:
        int EffectiveWrapMode() const noexcept;

        std::vector<SnapshotValue> snapshot_values_;
        bool snapshot_valid_ = false;
        bool stop_restore_requested_ = false;
        float blend_in_elapsed_ = 0.0f;
        int ping_pong_direction_ = 1;
    };
}
