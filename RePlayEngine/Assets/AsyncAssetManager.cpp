#include "AsyncAssetManager.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <windows.h>

namespace ReplayEngine::Assets
{
    AsyncAssetManager::AsyncAssetManager()
    {
        const unsigned hardware = std::thread::hardware_concurrency();
        const size_t available = hardware > 1 ? hardware - 1 : 1;
        const size_t worker_count = (std::max)(size_t{ 2 },
            (std::min)(size_t{ 8 }, available));
        workers_.reserve(worker_count);
        for (size_t index = 0; index < worker_count; ++index)
            workers_.emplace_back(&AsyncAssetManager::WorkerMain, this);
    }

    AsyncAssetManager::~AsyncAssetManager()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_)
            if (worker.joinable()) worker.join();
    }

    std::uint64_t AsyncAssetManager::QueueFile(const std::filesystem::path& path,
        AssetKind kind, Completion completion)
    {
        return QueueTask(path, kind, [](AsyncAssetResult& result)
        {
            std::ifstream stream(result.path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                result.error = "アセットファイルを開けません";
                return;
            }
            const std::streamoff size = stream.tellg();
            if (size < 0)
            {
                result.error = "アセットサイズを取得できません";
                return;
            }
            result.bytes.resize(static_cast<std::size_t>(size));
            stream.seekg(0, std::ios::beg);
            if (!result.bytes.empty() &&
                !stream.read(reinterpret_cast<char*>(result.bytes.data()), size))
            {
                result.bytes.clear();
                result.error = "アセットを最後まで読み込めません";
            }
        }, std::move(completion));
    }

    std::uint64_t AsyncAssetManager::QueueTask(const std::filesystem::path& path,
        AssetKind kind, Work work, Completion completion)
    {
        if (pending_count_.load() == 0)
        {
            // 新しいバッチの進捗が前回の完了数を引き継がないようにする。
            total_count_.store(0);
            completed_count_.store(0);
        }
        Job job{};
        const std::uint64_t ticket = next_ticket_.fetch_add(1);
        job.ticket = ticket;
        job.path = path;
        job.kind = kind;
        job.work = std::move(work);
        job.completion = std::move(completion);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.push_back(std::move(job));
        }
        total_count_.fetch_add(1);
        pending_count_.fetch_add(1);
        condition_.notify_one();
        return ticket;
    }

    void AsyncAssetManager::PumpMainThread()
    {
        std::deque<CompletedJob> ready;
        {
            // コールバック実行中にワーカースレッドを待たせないようキューだけ交換する。
            std::lock_guard<std::mutex> lock(mutex_);
            ready.swap(completed_);
        }
        for (CompletedJob& job : ready)
        {
            if (job.completion) job.completion(std::move(job.result));
            completed_count_.fetch_add(1);
            pending_count_.fetch_sub(1);
        }
    }

    void AsyncAssetManager::CancelPending()
    {
        // 実行中のジョブは止めず、まだ取得されていないジョブだけを破棄する。
        std::lock_guard<std::mutex> lock(mutex_);
        pending_count_.fetch_sub(static_cast<std::uint64_t>(jobs_.size()));
        jobs_.clear();
    }

    float AsyncAssetManager::Progress() const noexcept
    {
        const std::uint64_t total = total_count_.load();
        if (total == 0) return 1.0f;
        return static_cast<float>(completed_count_.load()) / static_cast<float>(total);
    }

    void AsyncAssetManager::WorkerMain()
    {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;)
        {
            Job job{};
            {
                // 停止要求か新規ジョブが届くまでワーカースレッドを休止する。
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_ && jobs_.empty()) break;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }

            AsyncAssetResult result{};
            result.ticket = job.ticket;
            result.path = job.path;
            result.kind = job.kind;
            try
            {
                if (job.work) job.work(result);
                else result.error = "非同期処理が設定されていません";
            }
            catch (const std::exception& exception)
            {
                result.error = exception.what();
            }
            catch (...)
            {
                result.error = "非同期処理で不明なエラーが発生しました";
            }
            {
                // GPU生成を含む完了処理はPumpMainThreadまで遅延させる。
                std::lock_guard<std::mutex> lock(mutex_);
                completed_.push_back({ std::move(result), std::move(job.completion) });
            }
        }
        if (SUCCEEDED(com)) CoUninitialize();
    }
}
