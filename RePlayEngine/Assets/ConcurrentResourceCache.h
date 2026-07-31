#pragma once

#include <algorithm>
#include <condition_variable>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ReplayEngine::Assets
{
    template<class Resource>
    class ConcurrentResourceCache final
    {
    public:
        template<class Factory>
        std::shared_ptr<Resource> Load(const std::filesystem::path& path, Factory&& factory)
        {
            const std::wstring key = MakeKey(path);
            std::shared_ptr<PendingLoad> pending;
            bool should_load = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (const auto cached = resources_.find(key); cached != resources_.end())
                    return cached->second;
                if (const auto active = pending_.find(key); active != pending_.end())
                {
                    pending = active->second;
                    pending->ready.wait(lock, [&pending] { return pending->completed; });
                    if (pending->error) std::rethrow_exception(pending->error);
                    return pending->resource;
                }
                pending = std::make_shared<PendingLoad>();
                pending_[key] = pending;
                should_load = true;
            }

            std::shared_ptr<Resource> resource;
            std::exception_ptr error;
            if (should_load)
            {
                try { resource = factory(); }
                catch (...) { error = std::current_exception(); }
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending->resource = resource;
                pending->error = error;
                pending->completed = true;
                if (resource) resources_[key] = resource;
                pending_.erase(key);
            }
            pending->ready.notify_all();
            if (error) std::rethrow_exception(error);
            return resource;
        }

        size_t CachedCount() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return resources_.size();
        }

        void Clear()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            resources_.clear();
        }

    private:
        struct PendingLoad
        {
            std::condition_variable ready;
            std::shared_ptr<Resource> resource;
            std::exception_ptr error;
            bool completed = false;
        };

        static std::wstring MakeKey(const std::filesystem::path& path)
        {
            std::error_code error;
            auto normalized = std::filesystem::absolute(path, error);
            if (error) normalized = path;
            std::wstring key = normalized.lexically_normal().generic_wstring();
            std::transform(key.begin(), key.end(), key.begin(),
                [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
            return key;
        }

        mutable std::mutex mutex_;
        std::map<std::wstring, std::shared_ptr<Resource>> resources_;
        std::map<std::wstring, std::shared_ptr<PendingLoad>> pending_;
    };
}
