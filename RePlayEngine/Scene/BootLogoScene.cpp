#include "BootLogoScene.h"

namespace ReplayEngine::Scene
{
    bool BootLogoScene::Initialize(ID3D11Device* device)
    {
        return logo_.Initialize(device);
    }

    void BootLogoScene::Update(float elapsed_time)
    {
        logo_.Update(elapsed_time);
    }

    void BootLogoScene::Render(const RenderContext& context)
    {
        logo_.Render(context.device_context, context.width, context.height);
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
