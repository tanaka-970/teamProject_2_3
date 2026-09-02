#include "UIEffect.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::UI
{
    DirectX::XMFLOAT4 UIEffect::ExpandBounds(float target_width,
        float target_height) const noexcept
    {
        if (!enabled) return { 0.0f, 0.0f, 0.0f, 0.0f };

        const UIEffectKind effect_kind = static_cast<UIEffectKind>(kind);
        const float safe_radius = (std::max)(0.0f, radius);
        const float safe_amount = std::fabs(amount);
        const float safe_width = (std::max)(1.0f, target_width);
        const float safe_height = (std::max)(1.0f, target_height);
        constexpr float margin = 2.0f;
        constexpr float pi = 3.14159265359f;
        const auto uniform = [](float value)
        {
            return DirectX::XMFLOAT4{ value, value, value, value };
        };
        const auto corner_radius = [&](float center_x, float center_y)
        {
            const float x = (std::max)(std::fabs(center_x),
                std::fabs(1.0f - center_x)) * safe_width;
            const float y = (std::max)(std::fabs(center_y),
                std::fabs(1.0f - center_y)) * safe_height;
            return std::sqrt(x * x + y * y);
        };
        const auto safe_center = [&]()
        {
            const bool valid = direction.x >= 0.0f && direction.x <= 1.0f &&
                direction.y >= 0.0f && direction.y <= 1.0f;
            return valid ? direction : DirectX::XMFLOAT2{ 0.5f, 0.5f };
        };
        switch (effect_kind)
        {
        case UIEffectKind::Blur:
        case UIEffectKind::Glow:
        case UIEffectKind::Outline:
        case UIEffectKind::Kuwahara:
        case UIEffectKind::LightStreaks:
            // HLSL の最遠サンプルは radius。丸め余裕を片側 2px 足す。
            return uniform(safe_radius + margin);
        case UIEffectKind::EdgeDetect:
            // Sobel は max(radius, 0.25) 離れた 3x3 を読む。
            return uniform((std::max)(safe_radius, 0.25f) + margin);
        case UIEffectKind::Shake:
        {
            // jitter の各軸は [-0.5, 0.5] * amount * intensity。
            const float expansion = 0.5f * std::fabs(amount * intensity) + margin;
            return uniform(expansion);
        }
        case UIEffectKind::Distortion:
        case UIEffectKind::ChromaticAberration:
        case UIEffectKind::DisplacementMap:
        case UIEffectKind::TurbulentDisplace:
        {
            // sin/cos または正規化した放射方向へ amount * intensity 動く。
            const float expansion = std::fabs(amount * intensity) + margin;
            return uniform(expansion);
        }
        case UIEffectKind::DirectionalBlur:
        case UIEffectKind::RadialBlur:
        case UIEffectKind::MotionBlur:
        {
            // 両 Shader とも t は [-0.5, 0.5]。Radial の distance_scale は最大 1。
            const float expansion = 0.5f * safe_amount + margin;
            return uniform(expansion);
        }
        case UIEffectKind::LongShadow:
        case UIEffectKind::Ripple:
        case UIEffectKind::Echo:
            return uniform(safe_amount + safe_radius + margin);
        case UIEffectKind::DropShadow:
            return uniform(safe_amount + safe_radius + margin);
        case UIEffectKind::BrushStroke:
        {
            // 楕円タップの長軸 radius と短軸 max(amount, 0.5) の大きい側が最大距離。
            const float expansion = (std::max)(safe_radius,
                (std::max)(safe_amount, 0.5f)) + margin;
            return uniform(expansion);
        }
        case UIEffectKind::RotationalBlur:
        {
            // HLSL は角度全幅の半分まで回す。最遠角の弦長を最大変位とする。
            const float radians = std::fabs(angle) * pi / 180.0f;
            const float chord_factor = 2.0f * std::sin((std::min)(
                radians * 0.25f, pi * 0.5f));
            const float expansion = corner_radius(direction.x, direction.y) *
                chord_factor + margin;
            return uniform(expansion);
        }
        case UIEffectKind::Twirl:
        {
            // aspect 補正後の radius は target 高さ基準。最大回転の弦長で包む。
            const float radians = std::fabs(angle) * pi / 180.0f;
            const float chord_factor = 2.0f * std::sin((std::min)(
                radians * 0.5f, pi * 0.5f));
            const float expansion = safe_radius * safe_height * chord_factor + margin;
            return uniform(expansion);
        }
        case UIEffectKind::Spherize:
        {
            // sample_uv は半径内を scale で割る。scale の最小値 0.05 まで含めて包む。
            const float spherize_amount = angle;
            const float displacement_scale = spherize_amount >= 0.0f
                ? spherize_amount / (1.0f + spherize_amount)
                : 1.0f / (std::max)(1.0f + spherize_amount, 0.05f) - 1.0f;
            const float expansion = safe_radius * safe_height *
                std::fabs(displacement_scale) + margin;
            return uniform(expansion);
        }
        case UIEffectKind::LensDistortion:
        {
            // distorted_uv - uv = centered * amount * |centered|^2。
            const DirectX::XMFLOAT2 center = safe_center();
            const float farthest = corner_radius(center.x, center.y);
            const float normalized_radius = farthest / safe_height;
            const float expansion = safe_amount * normalized_radius *
                normalized_radius * farthest + margin;
            return uniform(expansion);
        }
        case UIEffectKind::Waveform:
        {
            // 基本波は -1..1 へ正規化してあるので、変位の最大は 振幅 × (1 + うねり) で確定する。
            // 太さ変調は細らせる向きにしか効かないため、ここへ足す必要はない。
            const float wobble = (std::min)(1.0f, (std::max)(0.0f, progress));
            const float expansion = safe_amount * (1.0f + wobble) + margin;
            return uniform(expansion);
        }
        case UIEffectKind::Glitch:
        {
            // shifted_uv へ RGB の channel_shift がさらに加算される。
            const float expansion = safe_amount + std::fabs(intensity) + margin;
            return uniform(expansion);
        }
        case UIEffectKind::VHS:
        {
            // HLSL の行揺れは (Hash * 2 - 1) * radius なので最大 |radius|。
            // そこへ横にじみか RGB ずれの大きい側が重なる。
            const float expansion = safe_radius +
                (std::max)(safe_amount, std::fabs(threshold)) + margin;
            return uniform(expansion);
        }
        case UIEffectKind::Mosaic:
            // 出力位置は動かさず、同じセル内の色で置換するだけなので矩形外へ輪郭を押し出さない。
            return uniform(0.0f);
        case UIEffectKind::Crystallize:
        case UIEffectKind::StainedGlass:
            // Voronoi の中心色を現在の出力ピクセルへ塗る領域フィルタで、幾何学的な外向き変位はない。
            return uniform(0.0f);
        case UIEffectKind::ColorAdjust:
        case UIEffectKind::Noise:
        case UIEffectKind::Mask:
        case UIEffectKind::Wipe:
        case UIEffectKind::Bubble:
        case UIEffectKind::Dissolve:
        case UIEffectKind::Halftone:
        case UIEffectKind::Vignette:
        case UIEffectKind::Posterize:
        case UIEffectKind::Threshold:
        case UIEffectKind::ColorRamp:
        case UIEffectKind::Levels:
        case UIEffectKind::Temperature:
        case UIEffectKind::CrossHatch:
        case UIEffectKind::PolarCoordinates:
        case UIEffectKind::Scanlines:
        case UIEffectKind::CRT:
        case UIEffectKind::Dither:
        case UIEffectKind::Letterbox:
        case UIEffectKind::FractalNoise:
        case UIEffectKind::InnerShadow:
        case UIEffectKind::LUT:
        case UIEffectKind::ToneCurve:
        case UIEffectKind::MatteComposite:
        case UIEffectKind::BevelEmboss:
        case UIEffectKind::Kaleidoscope:
        case UIEffectKind::PageCurl:
        case UIEffectKind::AsciiLedMatrix:
        case UIEffectKind::FeedbackZoom:
        case UIEffectKind::LiquidGlass:
        case UIEffectKind::LightSweep:
        case UIEffectKind::PixelSort:
        case UIEffectKind::Hologram:
        case UIEffectKind::IridescentFoil:
        case UIEffectKind::RadarSweep:
        case UIEffectKind::EnergyPulse:
        case UIEffectKind::CircuitFlow:
        case UIEffectKind::HeatHaze:
        case UIEffectKind::WaterCaustics:
        case UIEffectKind::BurnReveal:
        case UIEffectKind::FrostCrack:
            // 現在の出力ピクセルの色・アルファだけを変え、矩形外へ輪郭を生成しない。
            return uniform(0.0f);
        case UIEffectKind::VoronoiShatter:
            // 破片は amount ピクセルまで元の輪郭外へ飛ぶ。
            return uniform(safe_amount + margin);
        case UIEffectKind::InkBleed:
            // HLSL は amount * 14px の近傍からアルファを引き延ばす。
            return uniform(safe_amount * 14.0f + margin);
        case UIEffectKind::PortalVortex:
        {
            // 渦のサンプル位置は局所 RT 内で完結するが、強い設定では端の
            // 近傍を読むため、半径相当の余白を確保しておく。
            return uniform(std::fabs(amount * intensity) + margin);
        }
        case UIEffectKind::Shockwave:
        {
            // 波面は中心から外へ amount * intensity だけサンプルをずらせる。
            return uniform(std::fabs(amount * intensity) + margin);
        }
        case UIEffectKind::MatteMorphology:
        {
            // 膨張・局所ホールフィル・エッジは半径ぶんだけ外側へ輪郭を作れる。
            // 収縮は入力矩形内だけで完結するが、余白を確保して全モードの切替を安全にする。
            return uniform(safe_radius + margin);
        }
        default:
            // 未知 kind は Shader を適用しないため、確保領域も増やさない。
            return uniform(0.0f);
        }
    }
}
