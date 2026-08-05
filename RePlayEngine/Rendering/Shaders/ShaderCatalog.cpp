#include "ShaderCatalog.h"

#include <algorithm>

namespace ReplayEngine::Rendering
{
    void ShaderCatalog::Register(Entry entry)
    {
        if (!entry.info.id.IsValid())
        {
            // ID が無いものは目録に載せない。
            // 載せると引けない項目がドロップダウンに並ぶことになる。
            return;
        }

        const std::string key = entry.info.id.ToString();
        const auto found = index_.find(key);
        if (found != index_.end())
        {
            // 既に同じ ID がある。
            //
            // 同じファイルの再コンパイルなら正常な更新。
            // 別ファイルなら GUID の重複で、コピペして
            // #pragma replay_guid を消し忘れた状態。
            // 後者は必ず気付けるように数えておく。
            const Entry& existing = entries_[found->second];
            if (existing.info.source_path != entry.info.source_path)
            {
                ++duplicate_ids_;
            }
            entries_[found->second] = std::move(entry);
            return;
        }

        index_.emplace(key, entries_.size());
        entries_.push_back(std::move(entry));
    }

    const ShaderCatalog::Entry* ShaderCatalog::Find(ShaderID id) const noexcept
    {
        if (!id.IsValid()) return nullptr;
        const auto found = index_.find(id.ToString());
        if (found == index_.end()) return nullptr;
        if (found->second >= entries_.size()) return nullptr;
        return &entries_[found->second];
    }

    ShaderPropertySchemaRef ShaderCatalog::FindSchema(ShaderID id) const noexcept
    {
        const Entry* entry = Find(id);
        return entry != nullptr ? entry->schema : ShaderPropertySchemaRef{};
    }

    void ShaderCatalog::Clear() noexcept
    {
        entries_.clear();
        index_.clear();
        duplicate_ids_ = 0;
    }

    std::vector<std::string> ShaderCatalog::MenuPaths() const
    {
        std::vector<std::string> paths;
        paths.reserve(entries_.size());
        for (const Entry& entry : entries_)
        {
            paths.push_back(entry.info.MenuPath());
        }
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        return paths;
    }
}
