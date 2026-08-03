#include "SceneRenderCollector.h"

#include "IRenderSubmitter.h"
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

            for (std::size_t slot = 0; slot < object->ComponentCount(); ++slot)
            {
                const Core::Component* component = object->ComponentAt(slot);
                if (component == nullptr || component->PendingDestroy()) continue;

                // 描画 Component の型ごとの分岐はここに書かない。
                // IRenderSubmitter を実装しているかどうかだけを見る。
                // 新しい描画 Component が増えても、このファイルは変更不要。
                const auto* submitter = dynamic_cast<const IRenderSubmitter*>(component);
                if (submitter == nullptr) continue;

                RenderItem item;
                if (!submitter->BuildRenderItem(*object, item)) continue;

                // Asset 未指定のものは提出しない（描画側で毎回弾かなくて済むように）。
                if (item.mesh_asset.empty()) continue;

                output.Add(std::move(item));
            }
        }
    }
}
