#include "SceneRenderCollector.h"

#include "../../Components/Rendering/MeshRendererComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

namespace ReplayEngine::Rendering
{
    void SceneRenderCollector::Collect(const Scene::Scene& scene, RenderItemList& output)
    {
        output.Clear();

        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            const Core::GameObject* object = scene.GameObjectAt(index);
            if (object == nullptr) continue;
            if (object->PendingDestroy()) continue;
            if (!object->ActiveInHierarchy()) continue;

            // ワールド行列は 1 度だけ計算して、この GameObject の全 Renderer で使い回す。
            DirectX::XMFLOAT4X4 world{};
            bool world_ready = false;

            for (std::size_t slot = 0; slot < object->ComponentCount(); ++slot)
            {
                const Core::Component* component = object->ComponentAt(slot);
                if (component == nullptr) continue;

                const auto* renderer =
                    dynamic_cast<const Components::MeshRendererComponent*>(component);
                if (renderer == nullptr) continue;
                if (!renderer->ShouldRender()) continue;

                if (!world_ready)
                {
                    world = object->GetTransform().WorldMatrixFloat4x4();
                    world_ready = true;
                }

                RenderItem item;
                item.owner = object->ID();
                item.mesh_asset = renderer->mesh_asset;
                item.material_asset = renderer->material_asset;
                item.world = world;
                item.tint = renderer->tint;
                item.shading_model = renderer->shading_model;
                item.outline = renderer->outline;
                item.cast_shadow = renderer->cast_shadow;

                output.Add(std::move(item));
            }
        }
    }
}
