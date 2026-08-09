#pragma once

#include "AudioService.h"

#include <wrl/client.h>
#include <xaudio2.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Audio
{
    class AudioSystem final : public IAudioPlaybackService
    {
    public:
        AudioSystem() = default;
        ~AudioSystem();

        AudioSystem(const AudioSystem&) = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;

        bool Initialize();
        void Shutdown() noexcept;

        AudioVoiceHandle Play(const AudioPlaybackParams& params) override;
        void Stop(AudioVoiceHandle handle) noexcept override;
        void UpdateVoice(AudioVoiceHandle handle,
            const AudioPlaybackParams& params) noexcept override;

        void StopAll() noexcept;
        void UpdateFromScene(const Scene::Scene& scene) noexcept;

        bool SilentMode() const noexcept { return silent_mode_; }
        std::size_t ActiveVoiceCount() const noexcept { return voices_.size(); }
        std::size_t MaxConcurrentVoices() const noexcept { return max_concurrent_voices_; }

    private:
        struct AudioClip final
        {
            WAVEFORMATEX format{};
            std::vector<std::uint8_t> data;
            std::string path;
        };

        struct ActiveVoice final
        {
            AudioVoiceHandle handle;
            IXAudio2SourceVoice* voice = nullptr;
            std::shared_ptr<AudioClip> clip;
            AudioPlaybackParams params;
        };

        std::shared_ptr<AudioClip> LoadClip(const std::string& path);
        void CollectFinishedVoices() noexcept;
        void DestroyVoice(ActiveVoice& active) noexcept;
        void ApplyVoiceSpatialization(ActiveVoice& active) noexcept;
        void SetMonoPan(ActiveVoice& active, float pan) noexcept;
        void EnterSilentMode(const char* reason, HRESULT result = S_OK) noexcept;
        void LogWarningOnce(const std::string& message) noexcept;
        AudioVoiceHandle NextHandle() noexcept;

        Microsoft::WRL::ComPtr<IXAudio2> xaudio_;
        IXAudio2MasteringVoice* mastering_voice_ = nullptr;
        UINT32 mastering_channels_ = 2;

        std::unordered_map<std::string, std::shared_ptr<AudioClip>> clip_cache_;
        std::unordered_set<std::string> failed_clip_warnings_;
        std::vector<ActiveVoice> voices_;

        DirectX::XMFLOAT3 listener_position_{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 listener_forward_{ 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 listener_right_{ 1.0f, 0.0f, 0.0f };
        bool listener_valid_ = false;

        bool silent_mode_ = true;
        bool voice_limit_warning_logged_ = false;
        std::uint64_t next_voice_handle_ = 1;

        // 上限超過時は新しい再生要求を落とす。既に鳴っている音を奪わない方針。
        std::size_t max_concurrent_voices_ = 32;
    };
}
