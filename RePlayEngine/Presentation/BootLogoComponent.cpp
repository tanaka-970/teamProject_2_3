#include "BootLogoComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    constexpr float kCutTime = 0.433f;
    constexpr float kFlowEnd = 0.60f;
    constexpr float kHoldEnd = 2.40f;
    constexpr float kSlideOutEnd = 2.97f;
    constexpr float kStarFillTime = 3.87f;
    constexpr float kSkipEnableTime = 0.20f;
    constexpr float kAxisAngle = -45.0f;
    constexpr float kDirX = 0.70711f;
    constexpr float kDirY = -0.70711f;
    constexpr float kPerpX = 0.70711f;
    constexpr float kPerpY = 0.70711f;
    constexpr float kBgR = 1.0f, kBgG = 1.0f, kBgB = 1.0f;
    constexpr float kNavyR = 0.016f, kNavyG = 0.077f, kNavyB = 0.180f;
    constexpr float kPaleR = 0.472f, kPaleG = 0.678f, kPaleB = 0.763f;
    constexpr float kBlueR = 0.094f, kBlueG = 0.467f, kBlueB = 0.949f;

    struct Streak
    {
        float lane;
        float start;
        float length;
        float thickness;
        int tone;
        float alpha;
    };

    constexpr Streak kFlowStreaks[] =
    {
        { -300.0f,  -600.0f, 1500.0f, 620.0f, 0, 0.75f },
        { -880.0f, -1500.0f,  900.0f, 120.0f, 1, 1.00f },
        {  560.0f, -2600.0f, 1300.0f, 200.0f, 1, 1.00f },
        {-1180.0f, -3400.0f, 1100.0f,  70.0f, 2, 0.95f },
        {  180.0f, -4200.0f, 1800.0f, 300.0f, 1, 1.00f },
        {  940.0f, -5000.0f, 1400.0f, 100.0f, 0, 0.85f },
        { -520.0f, -5800.0f, 2000.0f, 420.0f, 1, 1.00f },
        { 1240.0f, -6600.0f, 1200.0f,  60.0f, 2, 0.90f },
        { -960.0f, -7400.0f, 1600.0f, 240.0f, 0, 0.80f },
    };

    float Saturate(float value)
    {
        return (std::min)(1.0f, (std::max)(0.0f, value));
    }

    float Segment(float time, float start, float end)
    {
        return end > start ? Saturate((time - start) / (end - start)) : 1.0f;
    }

    void ToneColor(int tone, float& r, float& g, float& b)
    {
        r = kPaleR; g = kPaleG; b = kPaleB;
        if (tone == 1) { r = kNavyR; g = kNavyG; b = kNavyB; }
        if (tone == 2) { r = kBlueR; g = kBlueG; b = kBlueB; }
    }

    void EnsureTexture(ReplayEngine::Rendering::DX12::D3D12UIFrame& frame,
        const char* key, const wchar_t* path)
    {
        for (const auto& source : frame.texture_sources)
            if (source.key == key) return;
        ReplayEngine::Rendering::DX12::D3D12StaticTextureSource source{};
        source.key = key;
        source.source_path = path;
        frame.texture_sources.push_back(std::move(source));
    }

    void AddQuad(ReplayEngine::Rendering::DX12::D3D12UIFrame& frame,
        float center_x, float center_y, float width, float height, float angle_degrees,
        const DirectX::XMFLOAT4& color, const char* texture_key = nullptr)
    {
        if (width <= 0.0f || height <= 0.0f || color.w <= 0.003f) return;
        const float half_w = width * 0.5f;
        const float half_h = height * 0.5f;
        const float radians = DirectX::XMConvertToRadians(angle_degrees);
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        const auto point = [&](float x, float y)
        {
            return DirectX::XMFLOAT2{
                center_x + x * c - y * s,
                center_y + x * s + y * c };
        };
        const DirectX::XMFLOAT2 p0 = point(-half_w, -half_h);
        const DirectX::XMFLOAT2 p1 = point(-half_w,  half_h);
        const DirectX::XMFLOAT2 p2 = point( half_w,  half_h);
        const DirectX::XMFLOAT2 p3 = point( half_w, -half_h);
        ReplayEngine::Rendering::DX12::D3D12UIBatch batch{};
        if (texture_key != nullptr) batch.texture_key = texture_key;
        batch.vertices = {
            { p0, {0,0}, color, {0,0,1,1} }, { p1, {0,1}, color, {0,0,1,1} },
            { p2, {1,1}, color, {0,0,1,1} }, { p0, {0,0}, color, {0,0,1,1} },
            { p2, {1,1}, color, {0,0,1,1} }, { p3, {1,0}, color, {0,0,1,1} } };
        batch.constants.screen_size = { static_cast<float>(frame.target_width),
            static_cast<float>(frame.target_height), 0.0f, 0.0f };
        frame.vertex_count += static_cast<std::uint32_t>(batch.vertices.size());
        ++frame.draw_commands;
        frame.batches.push_back(std::move(batch));
    }
}

namespace ReplayEngine::Presentation
{
    BootLogoComponent::BootLogoComponent() = default;
    BootLogoComponent::~BootLogoComponent() = default;

    bool BootLogoComponent::Initialize()
    {
        initialized_ = true;
        Reset();
        return true;
    }

    void BootLogoComponent::Reset() noexcept
    {
        time_ = 0.0f;
        skip_requested_ = false;
    }

    void BootLogoComponent::Update(float elapsed_time)
    {
        if (!initialized_ || IsFinished()) return;
        const float safe_step = (std::min)(1.0f / 20.0f, (std::max)(0.0f, elapsed_time));
        time_ += safe_step;
        if (skip_requested_ && time_ >= kSkipEnableTime && time_ < kHoldEnd) time_ = kHoldEnd;
        time_ = (std::min)(time_, kDuration);
        skip_requested_ = false;
    }

    bool BootLogoComponent::BuildRuntimeUI(Rendering::DX12::D3D12UIFrame& frame,
        float screen_width, float screen_height) const
    {
        if (!IsActive() || screen_width <= 0.0f || screen_height <= 0.0f) return true;
        frame.target_width = static_cast<std::uint32_t>((std::max)(1.0f, screen_width));
        frame.target_height = static_cast<std::uint32_t>((std::max)(1.0f, screen_height));
        EnsureTexture(frame, "boot:star", L"resources\\RePlayEngine\\BootLogo\\BootStar.png");
        EnsureTexture(frame, "boot:note", L"resources\\RePlayEngine\\BootLogo\\BootNote.png");
        EnsureTexture(frame, "boot:mark", L"resources\\RePlayEngine\\BootLogo\\EngineLogoMark.png");
        EnsureTexture(frame, "boot:word", L"resources\\RePlayEngine\\BootLogo\\EngineLogoText.png");
        frame.texture_count = static_cast<std::uint32_t>(frame.texture_sources.size());

        const float scale_x = screen_width / 1920.0f;
        const float scale_y = screen_height / 1080.0f;
        const float scale = (std::min)(scale_x, scale_y);
        const float center_x = screen_width * 0.5f;
        const float center_y = screen_height * 0.5f;
        const auto X = [scale_x](float value) { return value * scale_x; };
        const auto Y = [scale_y](float value) { return value * scale_y; };
        const auto S = [scale](float value) { return value * scale; };
        const auto AxisPoint = [&](float lane, float along, float& x, float& y)
        {
            x = center_x + (kPerpX * lane + kDirX * along) * scale;
            y = center_y + (kPerpY * lane + kDirY * along) * scale;
        };

        AddQuad(frame, center_x, center_y, screen_width, screen_height, 0.0f,
            { kBgR, kBgG, kBgB, 1.0f });
        if (time_ <= kFlowEnd)
        {
            const float progress = Segment(time_, 0.0f, kFlowEnd);
            const float zoom = time_ < kCutTime
                ? 1.0f + 7.0f * std::pow(Segment(time_, 0.22f, kCutTime), 2.6f) : 1.0f;
            for (const auto& streak : kFlowStreaks)
            {
                const float along = streak.start + (2600.0f - streak.start) * progress;
                const float lane = -297.0f + (streak.lane + 297.0f) * zoom;
                const float zoomed_along = -1060.0f + (along + 1060.0f) * zoom;
                float x = 0.0f, y = 0.0f;
                AxisPoint(lane, zoomed_along, x, y);
                float r = 0.0f, g = 0.0f, b = 0.0f;
                ToneColor(streak.tone, r, g, b);
                AddQuad(frame, x, y, S(streak.length * zoom), S(streak.thickness * zoom),
                    kAxisAngle, { r, g, b, streak.alpha });
            }
        }

        if (time_ >= kCutTime)
        {
            const float slide_progress = Segment(time_, kHoldEnd, kSlideOutEnd);
            const float slide = -slide_progress * slide_progress * 3000.0f;
            const float slide_x = kDirX * slide * scale;
            const float slide_y = kDirY * slide * scale;
            if (slide_progress < 1.0f)
            {
                const float mark_size = S(380.0f);
                AddQuad(frame, X(505.0f) + slide_x, Y(520.0f) + slide_y,
                    mark_size, mark_size, 0.0f, {1,1,1,1}, "boot:mark");
                const float word_width = X(880.0f);
                const float word_height = word_width / 7.3f;
                AddQuad(frame, X(725.0f) + word_width * 0.5f + slide_x,
                    Y(520.0f) + slide_y, word_width, word_height, 0.0f,
                    {1,1,1,1}, "boot:word");
                const float alpha = Segment(time_, 0.95f, 1.17f) * 0.8f;
                const float note_size = S(110.0f);
                AddQuad(frame, X(1435.0f) + note_size * 0.5f + slide_x,
                    Y(190.0f) + note_size * 0.5f + slide_y, note_size, note_size, -14.0f,
                    {kPaleR,kPaleG,kPaleB,alpha}, "boot:note");
            }
        }

        if (time_ >= 0.95f && time_ <= kStarFillTime)
        {
            const float decoration_alpha = Segment(time_, 0.95f, 1.17f);
            const float to_center = Segment(time_, kHoldEnd, kSlideOutEnd);
            const float grow = std::pow(Segment(time_, kSlideOutEnd, kStarFillTime), 6.0f);
            const float star_x = X(1517.0f) + (center_x - X(1517.0f)) * to_center;
            const float star_y = Y(255.0f) + (center_y - Y(255.0f)) * to_center;
            const float size = S(150.0f + 150.0f * to_center) + S(4900.0f) * grow;
            const float alpha = decoration_alpha * (0.85f + 0.15f * to_center);
            AddQuad(frame, star_x, star_y, size, size, 0.0f,
                {kPaleR,kPaleG,kPaleB,alpha}, "boot:star");
        }
        return true;
    }
}
