#pragma once

#include "IScene.h"

#include <memory>
#include <functional>
#include <deque>

namespace ReplayEngine::Scene
{
    class SceneManager final
    {
    public:
        bool SetScene(std::unique_ptr<IScene> scene, ID3D11Device* device)
        {
            if (!scene || !scene->Initialize(device)) return false;
            current_scene_ = std::move(scene);
            device_ = device;
            return true;
        }

        bool QueueScene(std::unique_ptr<IScene> scene, ID3D11Device* device)
        {
            if (!scene || !scene->Initialize(device)) return false;
            queued_scene_ = std::move(scene);
            device_ = device;
            return true;
        }

        void QueueSceneFactory(std::function<std::unique_ptr<IScene>()> factory)
        {
            if (factory) queued_factories_.push_back(std::move(factory));
        }

        void Clear() noexcept { current_scene_.reset(); }
        bool HasScene() const noexcept { return current_scene_ != nullptr; }
        bool IsExclusive() const noexcept
        {
            return current_scene_ && current_scene_->RenderMode() == SceneRenderMode::Exclusive;
        }

        void Update(float elapsed_time)
        {
            if (!current_scene_) return;
            current_scene_->Update(elapsed_time);
            if (current_scene_->IsFinished())
            {
                current_scene_ = std::move(queued_scene_);
                if (!current_scene_ && !queued_factories_.empty())
                {
                    auto factory = std::move(queued_factories_.front());
                    queued_factories_.pop_front();
                    auto next = factory();
                    if (next && next->Initialize(device_)) current_scene_ = std::move(next);
                }
            }
        }

        void Render(const RenderContext& context)
        {
            if (current_scene_) current_scene_->Render(context);
        }

        bool OnKeyDown(WPARAM key)
        {
            return current_scene_ && current_scene_->OnKeyDown(key);
        }

    private:
        ID3D11Device* device_ = nullptr;
        std::unique_ptr<IScene> current_scene_;
        std::unique_ptr<IScene> queued_scene_;
        std::deque<std::function<std::unique_ptr<IScene>()>> queued_factories_;
    };
}
