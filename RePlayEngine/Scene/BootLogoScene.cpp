#include "BootLogoScene.h"

namespace ReplayEngine::Scene
{
    bool BootLogoScene::Initialize()
    {
        return logo_.Initialize();
    }

    void BootLogoScene::Update(float elapsed_time)
    {
        logo_.Update(elapsed_time);
    }

    bool BootLogoScene::BuildRuntimeUI(Rendering::DX12::D3D12UIFrame& frame,
        float width, float height)
    {
        return logo_.BuildRuntimeUI(frame, width, height);
    }

    bool BootLogoScene::OnKeyDown(WPARAM key)
    {
        if (key != VK_RETURN && key != VK_SPACE && key != VK_ESCAPE) return false;
        logo_.RequestSkip();
        return true;
    }

    bool BootLogoScene::IsFinished() const noexcept
    {
        return logo_.IsFinished();
    }
}
