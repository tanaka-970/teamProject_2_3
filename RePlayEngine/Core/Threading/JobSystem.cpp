#include "JobSystem.h"

#include <algorithm>

namespace ReplayEngine::Core::Threading
{
    thread_local const JobSystem* JobSystem::current_worker_system_ = nullptr;

    JobSystem::JobSystem(std::size_t worker_count)
        : worker_count_(worker_count)
    {
        if (worker_count_ <= 1) return;
        workers_.reserve(worker_count_);
        for (std::size_t index = 0; index < worker_count_; ++index)
            workers_.emplace_back(&JobSystem::WorkerMain, this);
    }

    JobSystem::~JobSystem()
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stopping_ = true;
        }
        queue_condition_.notify_all();
        for (std::thread& worker : workers_)
            if (worker.joinable()) worker.join();
    }

    std::size_t JobSystem::DefaultWorkerCount() noexcept
    {
        const unsigned hardware = std::thread::hardware_concurrency();
        if (hardware <= 2) return hardware > 1 ? 1u : 0u;
        return (std::min)(std::size_t{ 12 }, static_cast<std::size_t>(hardware - 1));
    }

    void JobSystem::Run(std::size_t job_count, const IndexedJob& job)
    {
        if (job_count == 0 || !job) return;
        if (!ParallelExecutionEnabled() || current_worker_system_ == this)
        {
            for (std::size_t index = 0; index < job_count; ++index) job(index);
            return;
        }

        auto batch = std::make_shared<Batch>(job_count, job);
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stopping_) return;
            for (std::size_t index = 0; index < job_count; ++index)
                queue_.push_back({ batch, index });
        }
        queue_condition_.notify_all();

        std::unique_lock<std::mutex> completion_lock(batch->completion_mutex);
        batch->completion_condition.wait(completion_lock, [&batch]
        {
            return batch->remaining.load(std::memory_order_acquire) == 0;
        });
        if (batch->exception) std::rethrow_exception(batch->exception);
    }

    void JobSystem::WorkerMain()
    {
        current_worker_system_ = this;
        for (;;)
        {
            WorkItem item{};
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_condition_.wait(lock, [this]
                {
                    return stopping_ || !queue_.empty();
                });
                if (stopping_ && queue_.empty()) break;
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            Execute(std::move(item));
        }
        current_worker_system_ = nullptr;
    }

    void JobSystem::Execute(WorkItem item) noexcept
    {
        if (!item.batch) return;
        try
        {
            item.batch->job(item.index);
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(item.batch->completion_mutex);
            if (item.index < item.batch->exception_index)
            {
                item.batch->exception_index = item.index;
                item.batch->exception = std::current_exception();
            }
        }

        if (item.batch->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            std::lock_guard<std::mutex> lock(item.batch->completion_mutex);
            item.batch->completion_condition.notify_all();
        }
    }
}
