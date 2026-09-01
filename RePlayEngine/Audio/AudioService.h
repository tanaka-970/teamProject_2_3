#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <string>

namespace ReplayEngine::Audio
{
    enum class AudioSpatialMode
    {
        TwoD = 0,
        ThreeD = 1
    };

    struct AudioVoiceHandle final
    {
        std::uint64_t value = 0;

        bool Valid() const noexcept { return value != 0; }
        void Reset() noexcept { value = 0; }
    };

    struct AudioPlaybackParams final
    {
        std::string clip_path;
        bool loop = false;
        float volume = 1.0f;
        float pitch = 1.0f;
        AudioSpatialMode spatial_mode = AudioSpatialMode::TwoD;
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        float min_distance = 1.0f;
        float max_distance = 30.0f;
    };

    class IAudioPlaybackService
    {
    public:
        virtual ~IAudioPlaybackService() = default;

        // 既存のモック/Component向け実装を壊さない既定値。
        // AudioSystemはXAudio2のsilent modeを正しく反映してoverrideする。
        virtual bool Available() const noexcept { return true; }

        virtual AudioVoiceHandle Play(const AudioPlaybackParams& params) = 0;
        virtual void Stop(AudioVoiceHandle handle) noexcept = 0;
        virtual void UpdateVoice(AudioVoiceHandle handle,
            const AudioPlaybackParams& params) noexcept = 0;
        virtual bool IsPlaying(AudioVoiceHandle handle) const noexcept
        {
            return handle.Valid();
        }
    };
}
