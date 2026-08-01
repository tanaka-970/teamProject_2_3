#pragma once

#include "../../Scene/SceneDocument.h"
#include "../../Core/ObjectID/ObjectID.h"

#include <DirectXMath.h>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Editor
{
    class ViewportPicker final
    {
    public:
        static Scene::EntityId Pick(const Scene::SceneDocument& scene,
            const DirectX::XMFLOAT3& ray_origin,
            const DirectX::XMFLOAT3& ray_direction) noexcept;

        static Core::ObjectID Pick(const Scene::Scene& scene,
            const DirectX::XMFLOAT3& ray_origin,
            const DirectX::XMFLOAT3& ray_direction) noexcept;
    };
}
