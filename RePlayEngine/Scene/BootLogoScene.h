#pragma once

#include "IScene.h"
#include "../Presentation/BootLogoComponent.h"

namespace ReplayEngine::Scene
{
    // 起動ロゴの出し方。Scene=Project設定のシーン（使えなければ既定ロゴへ）、BuiltIn=既定ロゴ固定、None=出さない。
    enum class BootLogoMode { Scene, BuiltIn, None };
    inline constexpr BootLogoMode kBootLogoMode = BootLogoMode::Scene;

    class BootLogoScene final : public IScene
    {
    public:
        bool Initialize() override;
        void Update(float elapsed_time) override;
        bool BuildRuntimeUI(Rendering::DX12::D3D12UIFrame& frame,
            float width, float height) override;
        bool OnKeyDown(WPARAM key) override;
        bool IsFinished() const noexcept override;
        SceneRenderMode RenderMode() const noexcept override { return SceneRenderMode::Exclusive; }

    private:
        Presentation::BootLogoComponent logo_;
    };

    class BootLogoAssetScene final : public IScene
    {
    public:
        // Scene からモーションの尺を測れなかったときだけ使う既定値。
        static constexpr float default_duration = 3.87f;

        explicit BootLogoAssetScene(float duration) noexcept;
        bool Initialize() override;
        void Update(float elapsed_time) override;
        bool IsFinished() const noexcept override;
        SceneRenderMode RenderMode() const noexcept override { return SceneRenderMode::Exclusive; }

    private:
        float duration_ = default_duration;
        float time_ = 0.0f;
    };
}
