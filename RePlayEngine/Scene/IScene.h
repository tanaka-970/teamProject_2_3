#pragma once

#include <windows.h>

namespace ReplayEngine::Rendering::DX12 { struct D3D12UIFrame; }

namespace ReplayEngine::Scene
{
    enum class SceneRenderMode
    {
        World,
        Exclusive
    };

    struct RenderContext
    {
        float width = 0.0f;
        float height = 0.0f;
    };

    class IScene
    {
    public:
        virtual ~IScene() = default;

        virtual bool Initialize() = 0;
        virtual void Update(float elapsed_time) = 0;
        virtual void Render(const RenderContext&) {}
        virtual bool BuildRuntimeUI(Rendering::DX12::D3D12UIFrame&, float, float) { return true; }
        virtual bool OnKeyDown(WPARAM) { return false; }
        virtual bool IsFinished() const noexcept = 0;
        virtual SceneRenderMode RenderMode() const noexcept { return SceneRenderMode::World; }
    };
}
