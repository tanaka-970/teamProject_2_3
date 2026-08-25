#include "LoadingScene.h"

#include "../Assets/ParallelLoader.h"
#include "../Rendering/DX12/D3D12DeviceContext.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <windows.h>

namespace
{
    void AddRect(ReplayEngine::Rendering::DX12::D3D12UIFrame& frame,
        float x, float y, float width, float height, const DirectX::XMFLOAT4& color)
    {
        if (width <= 0.0f || height <= 0.0f) return;
        ReplayEngine::Rendering::DX12::D3D12UIBatch batch{};
        batch.vertices = {
            {{x,y},{0,0},color,{0,0,1,1}}, {{x,y+height},{0,1},color,{0,0,1,1}},
            {{x+width,y+height},{1,1},color,{0,0,1,1}}, {{x,y},{0,0},color,{0,0,1,1}},
            {{x+width,y+height},{1,1},color,{0,0,1,1}}, {{x+width,y},{1,0},color,{0,0,1,1}} };
        batch.constants.screen_size = { static_cast<float>(frame.target_width),
            static_cast<float>(frame.target_height), 0.0f, 0.0f };
        frame.vertex_count += static_cast<std::uint32_t>(batch.vertices.size());
        ++frame.draw_commands;
        frame.batches.push_back(std::move(batch));
    }
}

namespace ReplayEngine::Scene
{
    LoadingScene::LoadingScene() = default;
    LoadingScene::~LoadingScene()
    {
        if (loader_.valid())
        {
            try { loader_.get(); }
            catch (...) { failed_.store(true); }
        }
        task_running_ = false;
    }

    void LoadingScene::AddTask(std::string name, Task task)
    {
        if (task) tasks_.push_back({ std::move(name), std::move(task) });
    }

    bool LoadingScene::Initialize()
    {
        initialized_ = true;
        StartTasks();
        return true;
    }

    void LoadingScene::StartTasks()
    {
        if (task_running_ || tasks_.empty()) return;
        completed_tasks_.store(0);
        failed_.store(false);
        task_running_ = true;
        loader_ = std::async(std::launch::async, [this]()
        {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            Assets::ParallelLoader::Run(tasks_.size(), [this](size_t index)
            {
                bool task_succeeded = false;
                try { task_succeeded = tasks_[index].task(); }
                catch (...) { task_succeeded = false; }
                if (!task_succeeded) failed_.store(true);
                completed_tasks_.fetch_add(1);
            });
            if (SUCCEEDED(com)) CoUninitialize();
            return !failed_.load();
        });
    }

    void LoadingScene::Update(float elapsed_time)
    {
        spinner_ += elapsed_time * 120.0f;
        if (!task_running_) return;
        if (!loader_.valid()) { task_running_ = false; return; }
        if (loader_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        succeeded_ = loader_.get() && succeeded_;
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

    bool LoadingScene::BuildRuntimeUI(Rendering::DX12::D3D12UIFrame& frame,
        float width, float height)
    {
        if (!initialized_ || width <= 0.0f || height <= 0.0f) return true;
        frame.target_width = static_cast<std::uint32_t>((std::max)(1.0f, width));
        frame.target_height = static_cast<std::uint32_t>((std::max)(1.0f, height));
        AddRect(frame, 0, 0, width, height, {0.025f,0.032f,0.045f,1.0f});
        const float bar_w = width * 0.46f;
        const float bar_h = (std::max)(8.0f, height * 0.012f);
        const float x = (width - bar_w) * 0.5f;
        const float y = height * 0.68f;
        AddRect(frame, x, y, bar_w, bar_h, {0.15f,0.18f,0.23f,1.0f});
        const float fill = bar_w * Progress();
        if (fill > 0.0f) AddRect(frame, x, y, fill, bar_h, {0.20f,0.56f,0.92f,1.0f});
        const float spinner_size = (std::max)(8.0f, height * 0.025f);
        const float orbit = height * 0.045f;
        const float radians = DirectX::XMConvertToRadians(spinner_);
        const float sx = width * 0.5f + std::cos(radians) * orbit - spinner_size * 0.5f;
        const float sy = height * 0.5f + std::sin(radians) * orbit - spinner_size * 0.5f;
        AddRect(frame, sx, sy, spinner_size, spinner_size, {0.75f,0.90f,1.0f,0.95f});
        return true;
    }
}
