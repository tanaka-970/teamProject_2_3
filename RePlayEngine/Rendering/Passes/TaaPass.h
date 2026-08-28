#pragma once

#include <DirectXMath.h>
#include <cstdint>

namespace ReplayEngine::Rendering
{
    class TaaPass final
    {
    public:
        static constexpr std::uint32_t kJitterSampleCount = 8;
        bool Initialized() const noexcept { return true; }
        DirectX::XMFLOAT2 CurrentJitter(std::uint32_t frame_index) const noexcept
        {
            const auto halton = [](std::uint32_t index, std::uint32_t base) noexcept
            {
                float result = 0.0f;
                float fraction = 1.0f;
                while (index != 0)
                {
                    fraction /= static_cast<float>(base);
                    result += fraction * static_cast<float>(index % base);
                    index /= base;
                }
                return result;
            };
            const std::uint32_t sample = frame_index % kJitterSampleCount + 1u;
            return { halton(sample, 2u) - 0.5f, halton(sample, 3u) - 0.5f };
        }
        DirectX::XMFLOAT2 PreviousJitter() const noexcept { return previous_jitter_; }
        void SetJitter(const DirectX::XMFLOAT2& jitter) noexcept
        {
            previous_jitter_ = current_jitter_;
            current_jitter_ = jitter;
        }
        void InvalidateHistory() noexcept { history_valid_ = false; }
        bool HistoryValid() const noexcept { return history_valid_; }
        void MarkHistoryValid() noexcept { history_valid_ = true; }
        float blend = 0.88f;
        float variance_gamma = 1.0f;
        float sharpness = 0.35f;
        float max_velocity = 48.0f;
        bool enabled = true;
    private:
        DirectX::XMFLOAT2 current_jitter_{ 0.0f, 0.0f };
        DirectX::XMFLOAT2 previous_jitter_{ 0.0f, 0.0f };
        bool history_valid_ = false;
    };
}
