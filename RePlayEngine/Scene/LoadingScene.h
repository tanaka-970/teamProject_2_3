#pragma once

#include "IScene.h"

#include <atomic>
#include <functional>
#include <future>
#include <string>
#include <vector>

namespace ReplayEngine::Scene
{
    class LoadingScene final : public IScene
    {
    public:
        using Task = std::function<bool()>;

        LoadingScene();
        ~LoadingScene() override;

        void AddTask(std::string name, Task task);
        bool Initialize() override;
        void Update(float elapsed_time) override;
        bool BuildRuntimeUI(Rendering::DX12::D3D12UIFrame& frame,
            float width, float height) override;
        bool IsFinished() const noexcept override;
        SceneRenderMode RenderMode() const noexcept override { return SceneRenderMode::Exclusive; }

        float Progress() const noexcept;
        bool Succeeded() const noexcept { return succeeded_; }

    private:
        struct Entry { std::string name; Task task; };
        void StartTasks();

        std::vector<Entry> tasks_;
        std::future<bool> loader_;
        std::atomic<size_t> completed_tasks_{ 0 };
        std::atomic<bool> failed_{ false };
        float spinner_ = 0.0f;
        bool task_running_ = false;
        bool initialized_ = false;
        bool succeeded_ = true;
    };
}
