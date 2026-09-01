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
            return u8"最終合成\0HDRシーン\0ブルーム\0Deferred照明\0GBuffer ベースカラー\0GBuffer 法線\0"
                   u8"GBuffer マテリアル\0深度\0SSAO\0SSR\0影の可視性\0";
        }

        static const char* Name(int index) noexcept
        {
            const int count = static_cast<int>(RenderOutput::Count);
            if (index < 0 || index >= count) return Names();
            const char* name = Names();
            for (int i = 0; i < index; ++i)
            {
                while (*name != '\0') ++name;
                ++name;
            }
            return name;
        }

    private:
        RenderOutput output_ = RenderOutput::Final;
    };
}
