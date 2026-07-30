#include "BootLogoComponent.h"

#include "../../Source/mesh/sprite.h"

#include <algorithm>
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
}

namespace ReplayEngine::Presentation
{
    BootLogoComponent::BootLogoComponent() = default;
    BootLogoComponent::~BootLogoComponent() = default;

    bool BootLogoComponent::Initialize(ID3D11Device* device)
    {
        if (!device) return false;

        solid_ = std::make_unique<sprite>(device, nullptr, "sprite_solid_ps.cso");
        constexpr const char* masked_shader = "sprite_masked_ps.cso";
        star_ = std::make_unique<sprite>(device,
            L"resources\\RePlayEngine\\BootLogo\\BootStar.png", masked_shader);
        note_ = std::make_unique<sprite>(device,
            L"resources\\RePlayEngine\\BootLogo\\BootNote.png", masked_shader);
        mark_ = std::make_unique<sprite>(device,
            L"resources\\RePlayEngine\\BootLogo\\EngineLogoMark.png", masked_shader);
        word_ = std::make_unique<sprite>(device,
            L"resources\\RePlayEngine\\BootLogo\\EngineLogoText.png", masked_shader);
        initialized_ = solid_->valid() && star_->valid() && note_->valid()
            && mark_->valid() && word_->valid();
        Reset();
        return initialized_;
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
        if (skip_requested_ && time_ >= kSkipEnableTime && time_ < kHoldEnd)
        {
            time_ = kHoldEnd;
        }
        time_ = (std::min)(time_, kDuration);
        skip_requested_ = false;
    }

    void BootLogoComponent::DrawBar(ID3D11DeviceContext* context,
        float center_x, float center_y, float width, float height, float angle_degrees,
        float r, float g, float b, float a) const
    {
        if (!solid_ || a <= 0.003f) return;
        solid_->render(context, center_x - width * 0.5f, center_y - height * 0.5f,
            width, height, r, g, b, a, angle_degrees);
    }

    void BootLogoComponent::Render(ID3D11DeviceContext* context,
        float screen_width, float screen_height) const
    {
        if (!IsActive() || !context || screen_width <= 0.0f || screen_height <= 0.0f) return;

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

        DrawBar(context, center_x, center_y, screen_width, screen_height, 0.0f,
            kBgR, kBgG, kBgB, 1.0f);

        if (time_ <= kFlowEnd)
        {
            const float progress = Segment(time_, 0.0f, kFlowEnd);
            const float zoom = time_ < kCutTime
                ? 1.0f + 7.0f * std::pow(Segment(time_, 0.22f, kCutTime), 2.6f)
                : 1.0f;
            for (const auto& streak : kFlowStreaks)
            {
                const float along = streak.start + (2600.0f - streak.start) * progress;
                const float lane = -297.0f + (streak.lane + 297.0f) * zoom;
                const float zoomed_along = -1060.0f + (along + 1060.0f) * zoom;
                float x = 0.0f, y = 0.0f;
                AxisPoint(lane, zoomed_along, x, y);
                float r = 0.0f, g = 0.0f, b = 0.0f;
                ToneColor(streak.tone, r, g, b);
                DrawBar(context, x, y, S(streak.length * zoom), S(streak.thickness * zoom),
                    kAxisAngle, r, g, b, streak.alpha);
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
                mark_->render(context, X(505.0f) - mark_size * 0.5f + slide_x,
                    Y(520.0f) - mark_size * 0.5f + slide_y, mark_size, mark_size,
                    1, 1, 1, 1, 0);

                const float word_width = X(880.0f);
                const float aspect = word_->texture_height() > 0.0f
                    ? word_->texture_width() / word_->texture_height() : 7.3f;
                const float word_height = word_width / aspect;
                word_->render(context, X(725.0f) + slide_x, Y(520.0f) - word_height * 0.5f + slide_y,
                    word_width, word_height, 1, 1, 1, 1, 0);

                const float decoration_alpha = Segment(time_, 0.95f, 1.17f);
                const float note_size = S(110.0f);
                note_->render(context, X(1435.0f) + slide_x, Y(190.0f) + slide_y,
                    note_size, note_size, kPaleR, kPaleG, kPaleB, 0.8f * decoration_alpha, -14.0f);
            }
        }

    // 右上の星を中央へ移動して拡大し、そのままワイプとして使う。
    // 白背景でも見えるようチーム版の淡い青を保ち、黒フェードは挟まない。
        if (time_ >= 0.95f && time_ <= kStarFillTime)
        {
            const float decoration_alpha = Segment(time_, 0.95f, 1.17f);
            const float to_center = Segment(time_, kHoldEnd, kSlideOutEnd);
            const float grow = std::pow(Segment(time_, kSlideOutEnd, kStarFillTime), 6.0f);
            const float star_x = X(1517.0f) + (center_x - X(1517.0f)) * to_center;
            const float star_y = Y(255.0f) + (center_y - Y(255.0f)) * to_center;
            const float base_size = S(150.0f + 150.0f * to_center);
            const float size = base_size + S(4900.0f) * grow;
            const float alpha = decoration_alpha * (0.85f + 0.15f * to_center);
            star_->render(context, star_x - size * 0.5f, star_y - size * 0.5f,
                size, size, kPaleR, kPaleG, kPaleB, alpha, 0);
        }

    }
}
