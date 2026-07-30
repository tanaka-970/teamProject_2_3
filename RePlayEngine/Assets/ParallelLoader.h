#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>
#include <objbase.h>

// ロード専用の小さな並列実行ヘルパー。
// job_count 個の仕事を worker_count 本のスレッドへ動的に分配し、全完了を待つ。
// - 各ワーカーはテクスチャ読込(WIC/DDS=COM)に備えて CoInitializeEx を呼ぶ。
// - ID3D11Device::Create系はスレッドセーフなのでモデル/テクスチャ生成に使える。
//   即時コンテキスト(ID3D11DeviceContext)は絶対に触らないこと。
// - job 内の例外は呼び出し側で吸収しておくこと(ここでは捕捉しない)。
namespace ReplayEngine::Assets
{
    namespace ParallelLoader
    {
        // 実マシンのコア数に合わせた既定ワーカー数(1本はメインスレッド用に残す)。
        // 固定値だとコア数の多いマシンで並列余地を捨てるため、ロード系はこれを使う。
        // 上限12はスレッド起動コストとI/O飽和のバランス。
        inline int DefaultWorkerCount() noexcept
        {
            const unsigned hardware = std::thread::hardware_concurrency();
            const int available = hardware > 1 ? static_cast<int>(hardware) - 1 : 1;
            return (std::max)(2, (std::min)(12, available));
        }

        inline void Run(std::size_t job_count, int worker_count,
            const std::function<void(std::size_t)>& job)
        {
            if (job_count == 0 || !job) return;

            const int thread_count = (std::max)(1,
                (std::min)(worker_count, static_cast<int>(job_count)));
            if (thread_count <= 1)
            {
                for (std::size_t index = 0; index < job_count; ++index) job(index);
                return;
            }

            // 先着順に添字を取り合わせることで、重い仕事が偏っても手が空かない。
            std::atomic<std::size_t> next_job{ 0 };
            auto worker = [&]()
            {
                const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                for (;;)
                {
                    const std::size_t index = next_job.fetch_add(1);
                    if (index >= job_count) break;
                    job(index);
                }
                if (SUCCEEDED(com)) ::CoUninitialize();
            };

            std::vector<std::thread> threads;
            threads.reserve(static_cast<std::size_t>(thread_count));
            for (int i = 0; i < thread_count; ++i) threads.emplace_back(worker);
            for (std::thread& thread : threads) thread.join();
        }

        inline void Run(std::size_t job_count, const std::function<void(std::size_t)>& job)
        {
            Run(job_count, DefaultWorkerCount(), job);
        }
    }
}
