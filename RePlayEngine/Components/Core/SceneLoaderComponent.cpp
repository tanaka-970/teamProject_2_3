#include "SceneLoaderComponent.h"

#include "../../Runtime/Scene/RuntimeSceneService.h"
#include "../../Runtime/Scene/SceneFlowService.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Services/SceneServices.h"

namespace ReplayEngine::Components
{
    void SceneLoaderComponent::OnUpdate(float /*delta_time*/)
    {
        Scene::Scene* scene = GetScene();
        if (scene == nullptr)
        {
            progress = 0.0f;
            is_loading = false;
            state = static_cast<int>(Runtime::SceneLoadState::Failed);
            return;
        }

        const Scene::ILoadingProgressProvider* loading_progress =
            scene->Services().LoadingProgress();
        if (loading_progress != nullptr)
        {
            progress = loading_progress->Progress();
            is_loading = loading_progress->IsLoading();
            state = is_loading ? static_cast<int>(Runtime::SceneLoadState::Loading)
                               : static_cast<int>(Runtime::SceneLoadState::Idle);
            return;
        }

        Runtime::RuntimeSceneService* runtime_scene = scene->Services().RuntimeScene();
        Runtime::SceneFlowService* scene_flow = scene->Services().SceneFlow();

        if (runtime_scene != nullptr)
        {
            progress = runtime_scene->Progress();
            is_loading = runtime_scene->IsBusy() ||
                (scene_flow != nullptr && scene_flow->TransitionInProgress());
            state = static_cast<int>(runtime_scene->State());
            return;
        }

        progress = (scene_flow != nullptr && scene_flow->TransitionInProgress()) ? 0.5f : 1.0f;
        is_loading = scene_flow != nullptr && scene_flow->TransitionInProgress();
        state = is_loading ? static_cast<int>(Runtime::SceneLoadState::Loading)
                           : static_cast<int>(Runtime::SceneLoadState::Idle);
    }
}
