#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ReplayEngine::Core::Threading
{
    class JobSystem final
    {
    public:
        using IndexedJob = std::function<void(std::size_t)>;

        explicit JobSystem(std::size_t worker_count = DefaultWorkerCount());
        ~JobSystem();
        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        static std::size_t DefaultWorkerCount() noexcept;
        std::size_t WorkerCount() const noexcept { return worker_count_; }
        bool ParallelExecutionEnabled() const noexcept { return worker_count_ > 1; }

        void Run(std::size_t job_count, const IndexedJob& job);

    private:
        struct Batch
        {
            explicit Batch(std::size_t count, IndexedJob work)
                : remaining(count), job(std::move(work))
            {
            }

            std::atomic<std::size_t> remaining{ 0 };
            IndexedJob job;
            std::mutex completion_mutex;
            std::condition_variable completion_condition;
            std::size_t exception_index = static_cast<std::size_t>(-1);
            std::exception_ptr exception;
        };

        struct WorkItem
        {
            std::shared_ptr<Batch> batch;
            std::size_t index = 0;
        };

        void WorkerMain();
        static void Execute(WorkItem item) noexcept;

        std::mutex queue_mutex_;
        std::condition_variable queue_condition_;
        std::deque<WorkItem> queue_;
        std::vector<std::thread> workers_;
        std::size_t worker_count_ = 0;
        bool stopping_ = false;
        static thread_local const JobSystem* current_worker_system_;
    };
}
