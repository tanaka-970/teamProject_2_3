#pragma once

#include "../../Scene/SceneDocument.h"

#include <DirectXMath.h>

namespace ReplayEngine::Editor
{
    class ViewportPicker final
    {
    public:
        static Scene::EntityId Pick(const Scene::SceneDocument& scene,
            const DirectX::XMFLOAT3& ray_origin,
            const DirectX::XMFLOAT3& ray_direction) noexcept;
    };
}
