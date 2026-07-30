#include "LoadingScene.h"

#include "../../Source/mesh/sprite.h"

#include <algorithm>
#include <chrono>
#include <windows.h>

namespace ReplayEngine::Scene
{
    LoadingScene::LoadingScene() = default;
    LoadingScene::~LoadingScene() = default;

    void LoadingScene::AddTask(std::string name, Task task)
    {
        if (task) tasks_.push_back({ std::move(name), std::move(task) });
    }

    bool LoadingScene::Initialize(ID3D11Device* device)
    {
        if (!device) return false;
        solid_ = std::make_unique<sprite>(device, nullptr, "sprite_solid_ps.cso");
        star_ = std::make_unique<sprite>(device,
            L"resources\\RePlayEngine\\BootLogo\\BootStar.png", "sprite_masked_ps.cso");
        initialized_ = solid_ && solid_->valid();
        StartNextTask();
        return initialized_;
    }

    void LoadingScene::StartNextTask()
    {
        if (task_running_ || completed_tasks_ >= tasks_.size()) return;
        Task task = tasks_[completed_tasks_].task;
        task_running_ = true;
        active_task_ = std::async(std::launch::async, [task = std::move(task)]()
        {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            bool result = false;
            try { result = task(); }
            catch (...) { result = false; }
            if (SUCCEEDED(com)) CoUninitialize();
            return result;
        });
    }

    void LoadingScene::Update(float elapsed_time)
    {
        time_ += (std::max)(0.0f, elapsed_time);
        spinner_ += elapsed_time * 120.0f;
        if (!task_running_)
        {
            StartNextTask();
            return;
        }
        if (active_task_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        succeeded_ = active_task_.get() && succeeded_;
        task_running_ = false;
        ++completed_tasks_;
        StartNextTask();
    }

    float LoadingScene::Progress() const noexcept
    {
        if (tasks_.empty()) return 1.0f;
        return static_cast<float>(completed_tasks_) / static_cast<float>(tasks_.size());
    }

    bool LoadingScene::IsFinished() const noexcept
    {
        return initialized_ && !task_running_ && completed_tasks_ >= tasks_.size() && time_ >= 0.75f;
    }

    void LoadingScene::Render(const RenderContext& context)
    {
        if (!initialized_ || !context.device_context) return;
        const float w = context.width;
        const float h = context.height;
        solid_->render(context.device_context, 0, 0, w, h, 1.0f, 1.0f, 1.0f, 1.0f, 0);

        const float bar_w = w * 0.46f;
        const float bar_h = (std::max)(8.0f, h * 0.012f);
        const float x = (w - bar_w) * 0.5f;
        const float y = h * 0.68f;
        solid_->render(context.device_context, x, y, bar_w, bar_h,
            0.82f, 0.86f, 0.90f, 1.0f, 0);
        const float fill = bar_w * Progress();
        if (fill > 0.0f)
            solid_->render(context.device_context, x, y, fill, bar_h,
                0.20f, 0.56f, 0.92f, 1.0f, 0);

        if (star_ && star_->valid())
        {
            const float size = h * 0.12f;
            star_->render(context.device_context, w * 0.5f - size * 0.5f,
                h * 0.5f - size * 0.5f, size, size, 0.75f, 0.90f, 1.0f, 0.95f, spinner_);
        }
    }
}
