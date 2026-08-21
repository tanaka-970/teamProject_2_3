#pragma once

#include "../../Reflection/Property/PropertyBag.h"

#include <DirectXMath.h>

#include <array>
#include <string>

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

        // 新規 kind は必ず末尾へ追加する。既存 Scene の enum 値を変えない。
    };

    class UIEffect final
    {
    public:
        bool enabled = true;
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
