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

    BootLogoAssetScene::BootLogoAssetScene(float duration) noexcept
        : duration_(duration > 0.0f ? duration : default_duration)
    {
    }

    bool BootLogoAssetScene::Initialize()
    {
        time_ = 0.0f;
        return true;
    }

    void BootLogoAssetScene::Update(float elapsed_time)
    {
        if (elapsed_time > 0.0f) time_ += elapsed_time;
    }

    bool BootLogoAssetScene::IsFinished() const noexcept
    {
        return time_ >= duration_;
    }
}
