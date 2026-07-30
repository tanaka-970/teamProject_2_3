#pragma once

#include "IScene.h"
#include "../Presentation/BootLogoComponent.h"

namespace ReplayEngine::Scene
{
// シーンは演出コンポーネントを組み合わせる。
// フレームワークはIScene／SceneManagerだけを扱い、ロゴ固有処理を持たない。
    class BootLogoScene final : public IScene
    {
    public:
        bool Initialize(ID3D11Device* device) override;
        void Update(float elapsed_time) override;
        void Render(const RenderContext& context) override;
        bool OnKeyDown(WPARAM key) override;
        bool IsFinished() const noexcept override;
        SceneRenderMode RenderMode() const noexcept override { return SceneRenderMode::Exclusive; }

    private:
        Presentation::BootLogoComponent logo_;
    };
}
