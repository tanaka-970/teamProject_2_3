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
        bool SetScene(std::unique_ptr<IScene> scene)
        {
            if (!scene || !scene->Initialize()) return false;
            current_scene_ = std::move(scene);
            return true;
        }

        bool QueueScene(std::unique_ptr<IScene> scene)
        {
            if (!scene || !scene->Initialize()) return false;
            queued_scene_ = std::move(scene);
            return true;
        }

        void QueueSceneFactory(std::function<std::unique_ptr<IScene>()> factory)
        {
            if (factory) queued_factories_.push_back(std::move(factory));
        }

        void Clear() noexcept
        {
            current_scene_.reset();
            queued_scene_.reset();
            queued_factories_.clear();
        }

        bool HasScene() const noexcept { return current_scene_ != nullptr; }
        bool IsExclusive() const noexcept
        {
            return current_scene_ && current_scene_->RenderMode() == SceneRenderMode::Exclusive;
        }

        IScene* CurrentScene() noexcept { return current_scene_.get(); }
        const IScene* CurrentScene() const noexcept { return current_scene_.get(); }

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
                    if (next && next->Initialize()) current_scene_ = std::move(next);
                }
            }
        }

        void Render(const RenderContext& context)
        {
            if (current_scene_) current_scene_->Render(context);
        }

        bool BuildRuntimeUI(Rendering::DX12::D3D12UIFrame& frame,
            float width, float height)
        {
            return current_scene_ ? current_scene_->BuildRuntimeUI(frame, width, height) : true;
        }

        bool OnKeyDown(WPARAM key)
        {
            return current_scene_ && current_scene_->OnKeyDown(key);
        }

    private:
        std::unique_ptr<IScene> current_scene_;
        std::unique_ptr<IScene> queued_scene_;
        std::deque<std::function<std::unique_ptr<IScene>()>> queued_factories_;
    };
}
