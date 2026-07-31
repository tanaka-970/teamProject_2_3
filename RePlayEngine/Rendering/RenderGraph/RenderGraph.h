#pragma once

#include <cstddef>

namespace ReplayEngine::Rendering
{
    enum class RenderOutput
    {
        Final,
        SceneColor,
        Bloom,
        DeferredLit,
        GBufferBaseColor,
        GBufferNormal,
        GBufferMaterial,
        Depth,
        AmbientOcclusion,
        ScreenReflection,
        ShadowVisibility,
        Count
    };

// 出力先の流れを明示する小規模な描画グラフ管理器。
// 個々のDirect3D資源は各描画パスが引き続き所有する。
    class RenderGraph final
    {
    public:
        RenderOutput Output() const noexcept { return output_; }
        int OutputIndex() const noexcept { return static_cast<int>(output_); }

        void SetOutput(int index) noexcept
        {
            const int count = static_cast<int>(RenderOutput::Count);
            output_ = static_cast<RenderOutput>((index >= 0 && index < count) ? index : 0);
        }

        void CycleOutput() noexcept
        {
            SetOutput((OutputIndex() + 1) % static_cast<int>(RenderOutput::Count));
        }

        bool RequiresDeferred() const noexcept
        {
            return output_ >= RenderOutput::DeferredLit;
        }

        int DeferredDebugMode() const noexcept
        {
            switch (output_)
            {
            case RenderOutput::GBufferBaseColor: return 1;
            case RenderOutput::GBufferNormal: return 2;
            case RenderOutput::GBufferMaterial: return 3;
            case RenderOutput::Depth: return 4;
            case RenderOutput::AmbientOcclusion: return 5;
            case RenderOutput::ScreenReflection: return 6;
            case RenderOutput::ShadowVisibility: return 7;
            default: return 0;
            }
        }

        static constexpr const char* Names() noexcept
        {
            return "Final\0HDR Scene\0Bloom\0Deferred Lit\0GBuffer Base Color\0GBuffer Normal\0"
                   "GBuffer Material\0Depth\0SSAO\0SSR\0Shadow Visibility\0";
        }

    private:
        RenderOutput output_ = RenderOutput::Final;
    };
}
