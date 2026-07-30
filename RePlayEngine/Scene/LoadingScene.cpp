#include "LoadingScene.h"

#include "../../Source/mesh/sprite.h"

#include <algorithm>
#include <chrono>
#include <thread>
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
        StartTasks();
        return initialized_;
    }

    void LoadingScene::StartTasks()
    {
        if (task_running_ || tasks_.empty()) return;
        const unsigned hardware = std::thread::hardware_concurrency();
        const size_t available = hardware > 1 ? hardware - 1 : 1;
        const size_t worker_count = (std::min)(tasks_.size(),
            (std::max)(size_t{ 2 }, (std::min)(size_t{ 12 }, available)));

        next_task_.store(0);
        completed_tasks_.store(0);
        task_running_ = true;
        workers_.reserve(worker_count);
        for (size_t worker_index = 0; worker_index < worker_count; ++worker_index)
        {
            workers_.push_back(std::async(std::launch::async, [this]()
            {
                const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                bool worker_succeeded = true;
                for (;;)
                {
                    const size_t index = next_task_.fetch_add(1);
                    if (index >= tasks_.size()) break;
                    bool task_succeeded = false;
                    try { task_succeeded = tasks_[index].task(); }
                    catch (...) { task_succeeded = false; }
                    worker_succeeded = task_succeeded && worker_succeeded;
                    completed_tasks_.fetch_add(1);
                }
                if (SUCCEEDED(com)) CoUninitialize();
                return worker_succeeded;
            }));
        }
    }

    void LoadingScene::Update(float elapsed_time)
    {
        spinner_ += elapsed_time * 120.0f;
        if (!task_running_) return;
        for (auto& worker : workers_)
            if (worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        for (auto& worker : workers_) succeeded_ = worker.get() && succeeded_;
        workers_.clear();
        task_running_ = false;
    }

    float LoadingScene::Progress() const noexcept
    {
        if (tasks_.empty()) return 1.0f;
        return static_cast<float>(completed_tasks_.load()) / static_cast<float>(tasks_.size());
    }

    bool LoadingScene::IsFinished() const noexcept
    {
        return initialized_ && !task_running_ && completed_tasks_.load() >= tasks_.size();
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
