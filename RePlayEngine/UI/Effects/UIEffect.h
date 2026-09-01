#pragma once

#include "../../Reflection/Property/PropertyBag.h"

#include <DirectXMath.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace ReplayEngine::UI
{
    enum class UIEffectKind : int
    {
        Blur = 0,
        Glow = 1,
        ColorAdjust = 2,
        Noise = 3,
        Shake = 4,
        Mask = 5,
        Wipe = 6,
        Dissolve = 7,
        Distortion = 8,
        ChromaticAberration = 9,
        Kuwahara = 10,
        Halftone = 11,
        DirectionalBlur = 12,
        RadialBlur = 13,
        RotationalBlur = 14,
        Vignette = 15,
        LightStreaks = 16,
        LensDistortion = 17,
        Posterize = 18,
        Threshold = 19,
        ColorRamp = 20,
        Levels = 21,
        Temperature = 22,
        EdgeDetect = 23,
        Outline = 24,
        LongShadow = 25,
        CrossHatch = 26,
        BrushStroke = 27,
        Mosaic = 28,
        Crystallize = 29,
        StainedGlass = 30,
        Twirl = 31,
        Spherize = 32,
        Ripple = 33,
        PolarCoordinates = 34,
        Scanlines = 35,
        CRT = 36,
        Glitch = 37,
        Dither = 38,
        VHS = 39,
        Letterbox = 40,
        Waveform = 41,
        DisplacementMap = 42,
        TurbulentDisplace = 43,
        FractalNoise = 44,
        MotionBlur = 45,
        Echo = 46,
        DropShadow = 47,
        InnerShadow = 48,
        LUT = 49,
        ToneCurve = 50,
        MatteComposite = 51,
        MatteMorphology = 52,
        BevelEmboss = 53,
        Kaleidoscope = 54,
        PageCurl = 55,
        AsciiLedMatrix = 56,
        FeedbackZoom = 57,
        LiquidGlass = 58,
        LightSweep = 59,
        Shockwave = 60,
        PixelSort = 61,

        // 動的な演出系。既存 Scene の値を変えないため、必ず末尾へ追加する。
        Hologram = 62,
        IridescentFoil = 63,
        RadarSweep = 64,
        EnergyPulse = 65,
        CircuitFlow = 66,
        HeatHaze = 67,
        WaterCaustics = 68,
        VoronoiShatter = 69,
        InkBleed = 70,
        BurnReveal = 71,
        PortalVortex = 72,
        FrostCrack = 73,
        Count = 74,

        // 新規 kind は Count の直前へ追加する。既存 Scene の enum 値を変えない。
    };

    inline const char* UIEffectKindName(UIEffectKind kind) noexcept
    {
        static constexpr std::array<const char*,
            static_cast<std::size_t>(UIEffectKind::Count)> names{
            "Blur", "Glow", "ColorAdjust", "Noise", "Shake", "Mask", "Wipe",
            "Dissolve", "Distortion", "ChromaticAberration", "Kuwahara", "Halftone",
            "DirectionalBlur", "RadialBlur", "RotationalBlur", "Vignette",
            "LightStreaks", "LensDistortion", "Posterize", "Threshold", "ColorRamp",
            "Levels", "Temperature", "EdgeDetect", "Outline", "LongShadow",
            "CrossHatch", "BrushStroke", "Mosaic", "Crystallize", "StainedGlass",
            "Twirl", "Spherize", "Ripple", "PolarCoordinates", "Scanlines", "CRT",
            "Glitch", "Dither", "VHS", "Letterbox", "Waveform", "DisplacementMap",
            "TurbulentDisplace", "FractalNoise", "MotionBlur", "Echo", "DropShadow",
            "InnerShadow", "LUT", "ToneCurve", "MatteComposite", "MatteMorphology",
            "BevelEmboss", "Kaleidoscope", "PageCurl", "AsciiLedMatrix", "FeedbackZoom",
            "LiquidGlass", "LightSweep", "Shockwave", "PixelSort", "Hologram",
            "IridescentFoil", "RadarSweep", "EnergyPulse", "CircuitFlow", "HeatHaze",
            "WaterCaustics", "VoronoiShatter", "InkBleed", "BurnReveal", "PortalVortex",
            "FrostCrack"
        };
        const int index = static_cast<int>(kind);
        if (index < 0 || index >= static_cast<int>(names.size())) return "";
        return names[static_cast<std::size_t>(index)];
    }

    // Effect Stack 全体へ掛ける共通の適用範囲。
    // TextureMask は白黒画像を指定することで、矩形/円形では表せない
    // 投げ縄・ロゴ形状・手描き領域にも対応する。
    enum class UIEffectRegionShape : int
    {
        Rectangle = 0,
        Ellipse = 1,
        TextureMask = 2,
        Freeform = 3,
    };

    enum class UIEffectRegionScope : int
    {
        AllEffects = 0,
        SelectedEffects = 1,
    };

    struct UIEffectRegionData
    {
        bool enabled = false;
        int shape = static_cast<int>(UIEffectRegionShape::Rectangle);
        int scope = static_cast<int>(UIEffectRegionScope::AllEffects);
        bool invert = false;
        DirectX::XMFLOAT2 center{ 0.5f, 0.5f };
        DirectX::XMFLOAT2 size{ 0.5f, 0.5f };
        float rotation = 0.0f;
        float feather = 0.0f;
        float strength = 1.0f;
        std::string mask;
        std::vector<DirectX::XMFLOAT2> path_points;
        bool path_closed = true;
    };

    struct UIEffectRegion final : UIEffectRegionData
    {
        static constexpr int MaxAdditionalCount = 7;
        // 先頭の範囲は既存 Scene の effect_region として保持し、ここへ
        // 追加範囲を積む。描画時は全範囲の union として合成する。
        std::vector<UIEffectRegionData> additional;
    };

    inline void EnsureUIEffectRegionPath(UIEffectRegionData& region)
    {
        if (region.path_points.size() >= 3) return;
        const float half_x = (std::max)(0.001f, region.size.x);
        const float half_y = (std::max)(0.001f, region.size.y);
        region.path_points = {
            { region.center.x - half_x, region.center.y - half_y },
            { region.center.x + half_x, region.center.y - half_y },
            { region.center.x + half_x, region.center.y + half_y },
            { region.center.x - half_x, region.center.y + half_y } };
    }

    // Inspector で M マークを出す対象。値をキーフレームで動かせるだけの
    // Effect ではなく、現在時刻を参照して自律的に変化するものを示す。
    inline bool IsTimeDrivenEffect(UIEffectKind kind) noexcept
    {
        switch (kind)
        {
        case UIEffectKind::Noise:
        case UIEffectKind::Shake:
        case UIEffectKind::Distortion:
        case UIEffectKind::Ripple:
        case UIEffectKind::Scanlines:
        case UIEffectKind::CRT:
        case UIEffectKind::Glitch:
        case UIEffectKind::VHS:
        case UIEffectKind::Waveform:
        case UIEffectKind::TurbulentDisplace:
        case UIEffectKind::FractalNoise:
        case UIEffectKind::MotionBlur:
        case UIEffectKind::Echo:
        case UIEffectKind::FeedbackZoom:
        case UIEffectKind::LightSweep:
        case UIEffectKind::Shockwave:
        case UIEffectKind::PixelSort:
        case UIEffectKind::Hologram:
        case UIEffectKind::IridescentFoil:
        case UIEffectKind::RadarSweep:
        case UIEffectKind::EnergyPulse:
        case UIEffectKind::CircuitFlow:
        case UIEffectKind::HeatHaze:
        case UIEffectKind::WaterCaustics:
        case UIEffectKind::VoronoiShatter:
        case UIEffectKind::InkBleed:
        case UIEffectKind::BurnReveal:
        case UIEffectKind::PortalVortex:
        case UIEffectKind::FrostCrack:
            return true;
        default:
            return false;
        }
    }

    inline bool EffectSpreadsPixels(UIEffectKind kind) noexcept
    {
        switch (kind)
        {
        case UIEffectKind::Blur:
        case UIEffectKind::Glow:
        case UIEffectKind::Shake:
        case UIEffectKind::Mask:
        case UIEffectKind::Wipe:
        case UIEffectKind::Dissolve:
        case UIEffectKind::Distortion:
        case UIEffectKind::ChromaticAberration:
        case UIEffectKind::Kuwahara:
        case UIEffectKind::Halftone:
        case UIEffectKind::DirectionalBlur:
        case UIEffectKind::RadialBlur:
        case UIEffectKind::RotationalBlur:
        case UIEffectKind::LightStreaks:
        case UIEffectKind::LensDistortion:
        case UIEffectKind::EdgeDetect:
        case UIEffectKind::Outline:
        case UIEffectKind::LongShadow:
        case UIEffectKind::BrushStroke:
        case UIEffectKind::Mosaic:
        case UIEffectKind::Crystallize:
        case UIEffectKind::StainedGlass:
        case UIEffectKind::Twirl:
        case UIEffectKind::Spherize:
        case UIEffectKind::Ripple:
        case UIEffectKind::PolarCoordinates:
        case UIEffectKind::CRT:
        case UIEffectKind::Glitch:
        case UIEffectKind::VHS:
        case UIEffectKind::Waveform:
        case UIEffectKind::DisplacementMap:
        case UIEffectKind::TurbulentDisplace:
        case UIEffectKind::MotionBlur:
        case UIEffectKind::Echo:
        case UIEffectKind::DropShadow:
        case UIEffectKind::InnerShadow:
        case UIEffectKind::MatteComposite:
        case UIEffectKind::MatteMorphology:
        case UIEffectKind::BevelEmboss:
        case UIEffectKind::Kaleidoscope:
        case UIEffectKind::PageCurl:
        case UIEffectKind::AsciiLedMatrix:
        case UIEffectKind::FeedbackZoom:
        case UIEffectKind::LiquidGlass:
        case UIEffectKind::Shockwave:
        case UIEffectKind::PixelSort:
        case UIEffectKind::Hologram:
        case UIEffectKind::IridescentFoil:
        case UIEffectKind::EnergyPulse:
        case UIEffectKind::HeatHaze:
        case UIEffectKind::WaterCaustics:
        case UIEffectKind::VoronoiShatter:
        case UIEffectKind::InkBleed:
        case UIEffectKind::BurnReveal:
        case UIEffectKind::PortalVortex:
        case UIEffectKind::FrostCrack:
            return true;
        default:
            return false;
        }
    }

    class UIEffect final
    {
    public:
        bool enabled = true;
        // 範囲制限が SelectedEffects のとき、この Effect を対象にするか。
        // AllEffects では無視されるため、既存 Scene の挙動は変わらない。
        bool region_enabled = true;
        int kind = static_cast<int>(UIEffectKind::Blur);
        float radius = 8.0f;
        float intensity = 1.0f;
        // 赤 0.2126・青 0.0722 は既定 0.5 では構造的に光らないため、新規 Effect はまず光る値にする。
        // 一部だけを光らせたい場合は、利用側でしきい値を上げる。
        float threshold = 0.0f;
        float amount = 1.0f;
        float angle = 0.0f;
        float progress = 0.0f;
        float softness = 0.0f;
        float speed = 0.0f;
        float seed = 0.0f;
        DirectX::XMFLOAT2 direction{ 1.0f, -1.0f };
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 color_3{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 color_4{ 1.0f, 1.0f, 1.0f, 1.0f };
        float color_stop_2 = 0.333333f;
        float color_stop_3 = 0.666667f;
        float color_stop_4 = 1.0f;
        std::string mask;
        std::string custom_shader;

        // BrushStroke だけが使う atlas 制御。false の間は mask 全体を従来どおり
        // 1 枚の筆跡として読むため、既存 Scene の見た目を変えない。
        bool brush_atlas_enabled = false;
        // 独立ストローク描画は現在調整中のため明示的な opt-in とする。
        // false は既存のアトラスフィルター経路を使う。
        bool brush_instanced_renderer_enabled = false;
        int brush_pattern_mode = 0;
        int brush_pattern_index = 0;
        std::array<float, 16> brush_pattern_weights{};

        // Shader Composer の #pragma property 値。未知項目も PropertyBag のまま保持する。
        Reflection::PropertyBag custom_parameters;
        int waveform = 0;

        DirectX::XMFLOAT4 ExpandBounds(float target_width,
            float target_height) const noexcept;
    };
}
