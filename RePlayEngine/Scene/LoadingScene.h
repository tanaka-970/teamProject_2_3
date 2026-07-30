#pragma once

#include "IScene.h"

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

class sprite;

namespace ReplayEngine::Scene
{
    class LoadingScene final : public IScene
    {
    public:
        using Task = std::function<bool()>;

        LoadingScene();
        ~LoadingScene() override;

        void AddTask(std::string name, Task task);
        bool Initialize(ID3D11Device* device) override;
        void Update(float elapsed_time) override;
        void Render(const RenderContext& context) override;
        bool IsFinished() const noexcept override;
        SceneRenderMode RenderMode() const noexcept override { return SceneRenderMode::Exclusive; }

        float Progress() const noexcept;
        bool Succeeded() const noexcept { return succeeded_; }

    private:
        struct Entry { std::string name; Task task; };
        void StartNextTask();

        std::vector<Entry> tasks_;
        std::future<bool> active_task_;
        std::unique_ptr<sprite> solid_;
        std::unique_ptr<sprite> star_;
        size_t completed_tasks_ = 0;
        float time_ = 0.0f;
        float spinner_ = 0.0f;
        bool task_running_ = false;
        bool initialized_ = false;
        bool succeeded_ = true;
    };
}
