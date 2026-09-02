#pragma once

#include "AssetCache.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Assets
{
    struct AssetRecord
    {
        std::string guid;
        std::string display_name;
        std::filesystem::path source_path;
        std::filesystem::path cache_path;
        AssetKind kind = AssetKind::Unknown;
    };

    class AssetDatabase final
    {
    public:
        explicit AssetDatabase(std::filesystem::path database_path =
            std::filesystem::path("resources") / "AssetDatabase.replaydb");

        bool Load(std::string& error);
        bool Save(std::string& error) const;
        const AssetRecord& Register(const std::filesystem::path& source,
            AssetKind kind, const std::filesystem::path& cache = {});
        bool Remove(const std::string& guid);

        // Project Browser の rename / move は Asset の同一性を変えない。
        // source_path だけを差し替え、既存 GUID をそのまま維持する。
        // directory move では old_root 以下の全 record を相対位置ごと new_root へ移す。
        bool RelocatePath(const std::filesystem::path& old_source,
            const std::filesystem::path& new_source, bool update_display_name = true);
        std::size_t RelocateTree(const std::filesystem::path& old_root,
            const std::filesystem::path& new_root);

        // source_path がファイルではなくフォルダになっている record を挙げる。
        // 過去の RelocateTree が配下をまとめて潰した壊れ方を Editor から見えるようにする。
        std::vector<const AssetRecord*> FindFolderSourcePaths() const;

        const AssetRecord* FindByGuid(const std::string& guid) const noexcept;
        const AssetRecord* FindByPath(const std::filesystem::path& path) const noexcept;
        bool HasPathGuidReservation(const std::filesystem::path& path) const;
        const std::vector<AssetRecord>& Records() const noexcept { return records_; }

        static std::filesystem::path NormalizeProjectPath(const std::filesystem::path& path);

    private:
        static std::string MakeGuid(const std::filesystem::path& normalized_path);

        std::filesystem::path database_path_;
        std::vector<AssetRecord> records_;
    };
}
