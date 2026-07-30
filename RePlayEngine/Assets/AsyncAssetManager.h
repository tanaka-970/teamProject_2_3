#pragma once

#include "AssetCache.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ReplayEngine::Assets
{
    struct AsyncAssetResult
    {
        std::uint64_t ticket = 0;
        std::filesystem::path path;
        AssetKind kind = AssetKind::Unknown;
        std::vector<std::uint8_t> bytes;
        std::string error;
        bool Succeeded() const noexcept { return error.empty(); }
    };

    class AsyncAssetManager final
    {
    public:
        using Completion = std::function<void(AsyncAssetResult&&)>;
        using Work = std::function<void(AsyncAssetResult&)>;

        AsyncAssetManager();
        ~AsyncAssetManager();
        AsyncAssetManager(const AsyncAssetManager&) = delete;
        AsyncAssetManager& operator=(const AsyncAssetManager&) = delete;

        std::uint64_t QueueFile(const std::filesystem::path& path,
            AssetKind kind, Completion completion);
        std::uint64_t QueueTask(const std::filesystem::path& path,
            AssetKind kind, Work work, Completion completion);
        void PumpMainThread();
        void CancelPending();

        bool Busy() const noexcept { return pending_count_.load() != 0; }
        float Progress() const noexcept;
        std::uint64_t CompletedCount() const noexcept { return completed_count_.load(); }
        std::uint64_t TotalCount() const noexcept { return total_count_.load(); }

    private:
        struct Job
        {
            std::uint64_t ticket = 0;
            std::filesystem::path path;
            AssetKind kind = AssetKind::Unknown;
            Work work;
            Completion completion;
        };
        struct CompletedJob
        {
            AsyncAssetResult result;
            Completion completion;
        };

        void WorkerMain();

        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<Job> jobs_;
        std::deque<CompletedJob> completed_;
        std::vector<std::thread> workers_;
        bool stopping_ = false;
        std::atomic<std::uint64_t> next_ticket_{ 1 };
        std::atomic<std::uint64_t> total_count_{ 0 };
        std::atomic<std::uint64_t> completed_count_{ 0 };
        std::atomic<std::uint64_t> pending_count_{ 0 };
    };
}
