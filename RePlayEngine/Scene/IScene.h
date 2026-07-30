#pragma once

#include <d3d11.h>
#include <windows.h>

namespace ReplayEngine::Scene
{
    enum class SceneRenderMode
    {
        World,
        Exclusive
    };

    struct RenderContext
    {
        ID3D11DeviceContext* device_context = nullptr;
        float width = 0.0f;
        float height = 0.0f;
    };

    class IScene
    {
    public:
        virtual ~IScene() = default;

        virtual bool Initialize(ID3D11Device* device) = 0;
        virtual void Update(float elapsed_time) = 0;
        virtual void Render(const RenderContext& context) = 0;
        virtual bool OnKeyDown(WPARAM) { return false; }
        virtual bool IsFinished() const noexcept = 0;
        virtual SceneRenderMode RenderMode() const noexcept { return SceneRenderMode::World; }
    };
}
