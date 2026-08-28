#pragma once

#include "IScene.h"
#include "../Presentation/BootLogoComponent.h"

namespace ReplayEngine::Scene
{
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
}
