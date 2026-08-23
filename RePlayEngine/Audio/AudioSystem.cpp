#include "AudioSystem.h"

#include "../Components/Audio/AudioListenerComponent.h"
#include "../Scene/Runtime/Scene.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

using namespace DirectX;

namespace ReplayEngine::Audio
{
    namespace
    {
        constexpr std::uint32_t MakeFourCC(char a, char b, char c, char d) noexcept
        {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
        }

        std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes,
            std::size_t offset) noexcept
        {
            return static_cast<std::uint16_t>(
                bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8u));
        }

        std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes,
            std::size_t offset) noexcept
        {
            return static_cast<std::uint32_t>(bytes[offset]) |
                (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
                (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
                (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
        }

        float ClampVolume(float value) noexcept
        {
            if (!std::isfinite(value)) return 1.0f;
            return (std::max)(0.0f, (std::min)(4.0f, value));
        }

        float ClampPitch(float value) noexcept
        {
            if (!std::isfinite(value)) return 1.0f;
            return (std::max)(0.25f, (std::min)(4.0f, value));
        }

        float ClampDistance(float value, float fallback) noexcept
        {
            if (!std::isfinite(value)) return fallback;
            return (std::max)(0.0f, value);
        }

        XMFLOAT3 NormalizeOr(const XMFLOAT3& value, const XMFLOAT3& fallback) noexcept
        {
            XMVECTOR v = XMLoadFloat3(&value);
            const float length_sq = XMVectorGetX(XMVector3LengthSq(v));
            if (!std::isfinite(length_sq) || length_sq <= 1.0e-8f) return fallback;

            v = XMVector3Normalize(v);
            XMFLOAT3 result{};
            XMStoreFloat3(&result, v);
            if (!std::isfinite(result.x) || !std::isfinite(result.y) ||
                !std::isfinite(result.z))
            {
                return fallback;
            }
            return result;
        }

        std::string CacheKeyForPath(const std::string& path)
        {
            std::error_code error;
            std::filesystem::path file_path(path);
            std::filesystem::path absolute =
                std::filesystem::absolute(file_path, error);
            if (error) absolute = file_path;
            return absolute.lexically_normal().generic_string();
        }

        std::string HResultText(HRESULT result)
        {
            std::ostringstream output;
            output << "0x" << std::hex << std::uppercase
                << static_cast<unsigned long>(result);
            return output.str();
        }
    }

    AudioSystem::~AudioSystem()
    {
        Shutdown();
    }

    bool AudioSystem::Initialize()
    {
        Shutdown();

        HRESULT result = XAudio2Create(xaudio_.GetAddressOf(), 0,
            XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(result))
        {
            EnterSilentMode("XAudio2Create failed", result);
            return false;
        }

        result = xaudio_->CreateMasteringVoice(&mastering_voice_);
        if (FAILED(result))
        {
            EnterSilentMode("CreateMasteringVoice failed", result);
            return false;
        }

        XAUDIO2_VOICE_DETAILS details{};
        mastering_voice_->GetVoiceDetails(&details);
        mastering_channels_ = (std::max)(1u, details.InputChannels);
        silent_mode_ = false;
        return true;
    }

    void AudioSystem::Shutdown() noexcept
    {
        StopAll();
        if (mastering_voice_ != nullptr)
        {
            mastering_voice_->DestroyVoice();
            mastering_voice_ = nullptr;
        }
        xaudio_.Reset();
        clip_cache_.clear();
        failed_clip_warnings_.clear();
        listener_valid_ = false;
        silent_mode_ = true;
    }

    AudioVoiceHandle AudioSystem::Play(const AudioPlaybackParams& params)
    {
        if (silent_mode_ || xaudio_ == nullptr || mastering_voice_ == nullptr)
        {
            return {};
        }
        if (params.clip_path.empty()) return {};

        CollectFinishedVoices();
        if (voices_.size() >= max_concurrent_voices_)
        {
            if (!voice_limit_warning_logged_)
            {
                voice_limit_warning_logged_ = true;
                LogWarningOnce("[Audio] Voice limit reached; newest playback request was dropped.\n");
            }
            return {};
        }

        std::shared_ptr<AudioClip> clip = LoadClip(params.clip_path);
        if (!clip) return {};

        IXAudio2SourceVoice* source_voice = nullptr;
        HRESULT result = xaudio_->CreateSourceVoice(&source_voice, &clip->format);
        if (FAILED(result) || source_voice == nullptr)
        {
            LogWarningOnce("[Audio] CreateSourceVoice failed: " + HResultText(result) + "\n");
            return {};
        }

        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(clip->data.size());
        buffer.pAudioData = clip->data.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.LoopCount = params.loop ? XAUDIO2_LOOP_INFINITE : 0;

        result = source_voice->SubmitSourceBuffer(&buffer);
        if (FAILED(result))
        {
            source_voice->DestroyVoice();
            LogWarningOnce("[Audio] SubmitSourceBuffer failed: " + HResultText(result) + "\n");
            return {};
        }

        ActiveVoice active{};
        active.handle = NextHandle();
        active.voice = source_voice;
        active.clip = std::move(clip);
        active.params = params;
        active.params.volume = ClampVolume(params.volume);
        active.params.pitch = ClampPitch(params.pitch);
        active.params.min_distance = ClampDistance(params.min_distance, 1.0f);
        active.params.max_distance = ClampDistance(params.max_distance, 30.0f);

        ApplyVoiceSpatialization(active);
        result = source_voice->Start();
        if (FAILED(result))
        {
            source_voice->DestroyVoice();
            LogWarningOnce("[Audio] SourceVoice start failed: " + HResultText(result) + "\n");
            return {};
        }

        const AudioVoiceHandle handle = active.handle;
        voices_.push_back(std::move(active));
        return handle;
    }

    void AudioSystem::Stop(AudioVoiceHandle handle) noexcept
    {
        if (!handle.Valid()) return;

        for (auto it = voices_.begin(); it != voices_.end(); ++it)
        {
            if (it->handle.value != handle.value) continue;
            DestroyVoice(*it);
            voices_.erase(it);
            return;
        }
    }

    void AudioSystem::UpdateVoice(AudioVoiceHandle handle,
        const AudioPlaybackParams& params) noexcept
    {
        if (!handle.Valid()) return;

        for (ActiveVoice& active : voices_)
        {
            if (active.handle.value != handle.value) continue;
            active.params = params;
            active.params.volume = ClampVolume(params.volume);
            active.params.pitch = ClampPitch(params.pitch);
            active.params.min_distance = ClampDistance(params.min_distance, 1.0f);
            active.params.max_distance = ClampDistance(params.max_distance, 30.0f);
            ApplyVoiceSpatialization(active);
            return;
        }
    }

    bool AudioSystem::IsPlaying(AudioVoiceHandle handle) const noexcept
    {
        if (!handle.Valid()) return false;
        return std::any_of(voices_.begin(), voices_.end(),
            [handle](const ActiveVoice& active)
            {
                return active.handle.value == handle.value;
            });
    }

    void AudioSystem::StopAll() noexcept
    {
        for (ActiveVoice& active : voices_)
        {
            DestroyVoice(active);
        }
        voices_.clear();
    }

    void AudioSystem::UpdateFromScene(const Scene::Scene& scene) noexcept
    {
        const Components::AudioListenerSelection selection =
            Components::ResolveAudioListenerSelection(scene);
        listener_valid_ = selection.Valid();
        if (listener_valid_)
        {
            listener_position_ = selection.component->Position();
            listener_forward_ =
                NormalizeOr(selection.component->Forward(), { 0.0f, 0.0f, 1.0f });
            const XMFLOAT3 up =
                NormalizeOr(selection.component->Up(), { 0.0f, 1.0f, 0.0f });
            XMVECTOR right = XMVector3Cross(XMLoadFloat3(&up),
                XMLoadFloat3(&listener_forward_));
            XMFLOAT3 right_value{ 1.0f, 0.0f, 0.0f };
            XMStoreFloat3(&right_value, XMVector3Normalize(right));
            listener_right_ = NormalizeOr(right_value, { 1.0f, 0.0f, 0.0f });
        }

        CollectFinishedVoices();
        for (ActiveVoice& active : voices_)
        {
            ApplyVoiceSpatialization(active);
        }
    }

    std::shared_ptr<AudioSystem::AudioClip> AudioSystem::LoadClip(
        const std::string& path)
    {
        const std::string key = CacheKeyForPath(path);
        const auto found = clip_cache_.find(key);
        if (found != clip_cache_.end()) return found->second;

        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            if (failed_clip_warnings_.insert(key).second)
                LogWarningOnce("[Audio] WAV open failed: " + path + "\n");
            return nullptr;
        }

        std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        if (bytes.size() < 12 ||
            ReadU32(bytes, 0) != MakeFourCC('R', 'I', 'F', 'F') ||
            ReadU32(bytes, 8) != MakeFourCC('W', 'A', 'V', 'E'))
        {
            if (failed_clip_warnings_.insert(key).second)
                LogWarningOnce("[Audio] Unsupported WAV header: " + path + "\n");
            return nullptr;
        }

        WAVEFORMATEX format{};
        std::size_t data_offset = 0;
        std::size_t data_size = 0;
        bool found_format = false;
        bool found_data = false;

        std::size_t offset = 12;
        while (offset + 8 <= bytes.size())
        {
            const std::uint32_t chunk_id = ReadU32(bytes, offset);
            const std::uint32_t chunk_size = ReadU32(bytes, offset + 4);
            const std::size_t chunk_data = offset + 8;
            const std::size_t chunk_end = chunk_data + chunk_size;
            if (chunk_end > bytes.size()) break;

            if (chunk_id == MakeFourCC('f', 'm', 't', ' ') && chunk_size >= 16)
            {
                format.wFormatTag = ReadU16(bytes, chunk_data);
                format.nChannels = ReadU16(bytes, chunk_data + 2);
                format.nSamplesPerSec = ReadU32(bytes, chunk_data + 4);
                format.nAvgBytesPerSec = ReadU32(bytes, chunk_data + 8);
                format.nBlockAlign = ReadU16(bytes, chunk_data + 12);
                format.wBitsPerSample = ReadU16(bytes, chunk_data + 14);
                format.cbSize = 0;
                found_format = true;
            }
            else if (chunk_id == MakeFourCC('d', 'a', 't', 'a'))
            {
                data_offset = chunk_data;
                data_size = chunk_size;
                found_data = true;
            }

            offset = chunk_end + (chunk_size & 1u);
        }

        const bool valid_pcm =
            found_format &&
            format.wFormatTag == WAVE_FORMAT_PCM &&
            format.nChannels > 0 &&
            format.nSamplesPerSec > 0 &&
            format.nBlockAlign > 0 &&
            format.wBitsPerSample > 0 &&
            found_data &&
            data_size > 0 &&
            data_size <= UINT32_MAX;
        if (!valid_pcm)
        {
            if (failed_clip_warnings_.insert(key).second)
                LogWarningOnce("[Audio] Only PCM .wav files are supported: " + path + "\n");
            return nullptr;
        }

        auto clip = std::make_shared<AudioClip>();
        clip->format = format;
        clip->path = key;
        clip->data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(data_offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(data_offset + data_size));
        clip_cache_.emplace(key, clip);
        return clip;
    }

    void AudioSystem::CollectFinishedVoices() noexcept
    {
        for (auto it = voices_.begin(); it != voices_.end();)
        {
            if (it->voice == nullptr)
            {
                it = voices_.erase(it);
                continue;
            }

            XAUDIO2_VOICE_STATE state{};
            it->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (!it->params.loop && state.BuffersQueued == 0)
            {
                DestroyVoice(*it);
                it = voices_.erase(it);
                continue;
            }
            ++it;
        }
    }

    void AudioSystem::DestroyVoice(ActiveVoice& active) noexcept
    {
        if (active.voice == nullptr) return;
        active.voice->Stop(0);
        active.voice->FlushSourceBuffers();
        active.voice->DestroyVoice();
        active.voice = nullptr;
        active.handle.Reset();
    }

    void AudioSystem::ApplyVoiceSpatialization(ActiveVoice& active) noexcept
    {
        if (active.voice == nullptr || !active.clip) return;

        active.voice->SetFrequencyRatio(ClampPitch(active.params.pitch));

        float volume = ClampVolume(active.params.volume);
        float pan = 0.0f;
        const bool spatial =
            active.params.spatial_mode == AudioSpatialMode::ThreeD &&
            listener_valid_;
        if (spatial)
        {
            const XMVECTOR source = XMLoadFloat3(&active.params.position);
            const XMVECTOR listener = XMLoadFloat3(&listener_position_);
            const XMVECTOR offset = source - listener;
            const float distance = XMVectorGetX(XMVector3Length(offset));
            const float min_distance = (std::max)(0.0f, active.params.min_distance);
            const float max_distance = (std::max)(min_distance + 0.001f,
                active.params.max_distance);
            float attenuation = 1.0f;
            if (distance > min_distance)
            {
                attenuation = 1.0f -
                    ((distance - min_distance) / (max_distance - min_distance));
                attenuation = (std::max)(0.0f, (std::min)(1.0f, attenuation));
            }
            volume *= attenuation;

            if (distance > 1.0e-4f)
            {
                XMVECTOR direction = XMVector3Normalize(offset);
                pan = XMVectorGetX(XMVector3Dot(direction,
                    XMLoadFloat3(&listener_right_)));
                pan = (std::max)(-1.0f, (std::min)(1.0f, pan));
            }
        }

        active.voice->SetVolume(volume);
        SetMonoPan(active, pan);
    }

    void AudioSystem::SetMonoPan(ActiveVoice& active, float pan) noexcept
    {
        if (active.voice == nullptr || !active.clip || mastering_voice_ == nullptr)
            return;
        if (active.clip->format.nChannels != 1 || mastering_channels_ < 2)
            return;

        pan = (std::max)(-1.0f, (std::min)(1.0f, pan));
        std::vector<float> matrix(mastering_channels_, 0.0f);
        matrix[0] = pan <= 0.0f ? 1.0f : 1.0f - pan;
        matrix[1] = pan >= 0.0f ? 1.0f : 1.0f + pan;
        active.voice->SetOutputMatrix(mastering_voice_, 1, mastering_channels_,
            matrix.data());
    }

    void AudioSystem::EnterSilentMode(const char* reason, HRESULT result) noexcept
    {
        StopAll();
        if (mastering_voice_ != nullptr)
        {
            mastering_voice_->DestroyVoice();
            mastering_voice_ = nullptr;
        }
        xaudio_.Reset();
        silent_mode_ = true;

        std::string message = "[Audio] Silent mode: ";
        message += reason != nullptr ? reason : "unknown";
        if (FAILED(result)) message += " (" + HResultText(result) + ")";
        message += "\n";
        LogWarningOnce(message);
    }

    void AudioSystem::LogWarningOnce(const std::string& message) noexcept
    {
        static std::unordered_set<std::string> logged_messages;
        try
        {
            if (logged_messages.insert(message).second)
            {
                OutputDebugStringA(message.c_str());
            }
        }
        catch (...)
        {
            OutputDebugStringA(message.c_str());
        }
    }

    AudioVoiceHandle AudioSystem::NextHandle() noexcept
    {
        AudioVoiceHandle handle{ next_voice_handle_++ };
        if (next_voice_handle_ == 0) next_voice_handle_ = 1;
        if (!handle.Valid()) handle.value = next_voice_handle_++;
        return handle;
    }
}
