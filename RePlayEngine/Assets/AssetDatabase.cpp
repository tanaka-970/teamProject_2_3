#include "AssetDatabase.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace ReplayEngine::Assets
{
    namespace
    {
        std::uint64_t Hash(std::string_view text, std::uint64_t seed)
        {
            std::uint64_t value = seed;
            for (const unsigned char character : text)
            {
                value ^= character;
                value *= 1099511628211ull;
            }
            return value;
        }

        std::string PathKey(const std::filesystem::path& path)
        {
            // Windows上のパス比較で大文字小文字の違いを同一視する。
            std::string key = path.generic_u8string();
            std::transform(key.begin(), key.end(), key.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return key;
        }

        bool PathInsideOrEqual(const std::filesystem::path& value,
            const std::filesystem::path& root, std::filesystem::path& relative)
        {
            relative.clear();
            // 同一パスは relative を空のまま「root 自身」として返す。
            if (PathKey(value) == PathKey(root)) return true;

            std::error_code error;
            const std::filesystem::path computed =
                std::filesystem::relative(value, root, error);
            // 差分を出せないときは配下と見なさない。空の relative で畳むと
            // root 配下の record が全部 root 自身へ潰れる。
            if (error || computed.empty() || computed == ".") return false;
            const auto first = computed.begin();
            if (first == computed.end() || first->generic_u8string() == "..") return false;
            relative = computed;
            return true;
        }
    }

    AssetDatabase::AssetDatabase(std::filesystem::path database_path)
        : database_path_(std::move(database_path))
    {
    }
    //

    void AssetDatabase::InvalidateMissingFiles() noexcept
    {
        missing_files_cached_ = false;
    }

    std::filesystem::path AssetDatabase::NormalizeProjectPath(const std::filesystem::path& path)
    {
        // プロジェクト内のアセットは移動可能性を保つため相対パスで記録する。
        std::error_code error;
        const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
        if (error) return path.lexically_normal();
        const auto project = std::filesystem::current_path(error).lexically_normal();
        if (error) return absolute;
        const auto relative = std::filesystem::relative(absolute, project, error);
        if (!error && !relative.empty())
        {
            const std::string relative_text = relative.generic_u8string();
            if (relative_text != ".." && relative_text.rfind("../", 0) != 0)
                return relative;
        }
        return absolute;
    }

    std::string AssetDatabase::MakeGuid(const std::filesystem::path& normalized_path)
    {
        // 同じ正規化パスから常に同じGUIDを生成して参照を安定させる。
        const std::string key = PathKey(normalized_path);
        const std::uint64_t high = Hash(key, 1469598103934665603ull);
        const std::uint64_t low = Hash(key, 1099511628211ull ^ 0x9e3779b97f4a7c15ull);
        std::ostringstream stream;
        stream << std::hex << std::setfill('0') << std::setw(16) << high << std::setw(16) << low;
        return stream.str();
    }

    bool AssetDatabase::Load(std::string& error)
    {
        records_.clear();
        missing_guids_.clear();
        InvalidateMissingFiles();
        if (!std::filesystem::exists(database_path_)) return true;
        std::ifstream stream(database_path_, std::ios::binary);
        if (!stream)
        {
            error = "AssetDatabaseを開けません";
            return false;
        }
        std::string magic;
        int version = 0;
        std::size_t count = 0;
        if (!(stream >> magic >> version >> count) || magic != "REPLAY_ASSET_DB" ||
            version < 1 || version > 2)
        {
            error = "AssetDatabaseの形式が不正です";
            return false;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            AssetRecord record{};
            std::string source;
            std::string cache;
            std::uint32_t kind = 0;
            if (!(stream >> std::quoted(record.guid) >> kind))
            {
                error = "AssetDatabaseの項目を読み取れません";
                records_.clear();
                return false;
            }
            if (version >= 2 && !(stream >> std::quoted(record.display_name)))
            {
                error = "AssetDatabaseの表示名を読み取れません";
                records_.clear();
                return false;
            }
            if (!(stream >> std::quoted(source) >> std::quoted(cache)))
            {
                error = "AssetDatabaseの項目を読み取れません";
                records_.clear();
                return false;
            }
            record.kind = static_cast<AssetKind>(kind);
            record.source_path = std::filesystem::u8path(source);
            record.cache_path = std::filesystem::u8path(cache);
            if (record.display_name.empty())
                record.display_name = record.source_path.stem().u8string();
            records_.push_back(std::move(record));
        }
        return true;
    }

    bool AssetDatabase::Save(std::string& error) const
    {
        std::error_code filesystem_error;
        std::filesystem::create_directories(database_path_.parent_path(), filesystem_error);
        if (filesystem_error)
        {
            error = "AssetDatabase保存フォルダーを作成できません";
            return false;
        }
        std::ofstream stream(database_path_, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "AssetDatabaseを書き込めません";
            return false;
        }
        stream << "REPLAY_ASSET_DB 2 " << records_.size() << '\n';
        for (const AssetRecord& record : records_)
            stream << std::quoted(record.guid) << ' ' << static_cast<std::uint32_t>(record.kind)
                << ' ' << std::quoted(record.display_name)
                << ' ' << std::quoted(record.source_path.generic_u8string())
                << ' ' << std::quoted(record.cache_path.generic_u8string()) << '\n';
        return static_cast<bool>(stream);
    }

    const AssetRecord& AssetDatabase::Register(const std::filesystem::path& source,
        AssetKind kind, const std::filesystem::path& cache)
    {
        const auto normalized = NormalizeProjectPath(source);
        if (const AssetRecord* found = FindByPath(normalized))
        {
            // 再登録時はGUIDを維持し、既存シーンからの参照切れを防ぐ。
            AssetRecord* mutable_record = &records_[static_cast<std::size_t>(found - records_.data())];
            mutable_record->kind = kind;
            if (mutable_record->display_name.empty())
                mutable_record->display_name = normalized.stem().u8string();
            if (!cache.empty()) mutable_record->cache_path = NormalizeProjectPath(cache);
            missing_guids_.erase(mutable_record->guid);
            InvalidateMissingFiles();
            return *mutable_record;
        }
        AssetRecord record{};
        record.guid = MakeGuid(normalized);
        // Rename/Move は既存 GUID を維持するため、元パスが後から再利用されると
        // path-derived GUID が移動済みAssetの GUID と衝突し得る。
        // 既存 record は一切変更せず、新規登録側だけ deterministic salt を足して回避する。
        if (FindByGuid(record.guid) != nullptr)
        {
            const std::string base = normalized.generic_u8string();
            for (std::uint32_t suffix = 1; suffix < 1000000u; ++suffix)
            {
                const std::filesystem::path salted = std::filesystem::u8path(
                    base + "#replay-guid-" + std::to_string(suffix));
                record.guid = MakeGuid(salted);
                if (FindByGuid(record.guid) == nullptr) break;
            }
        }
        record.display_name = normalized.stem().u8string();
        record.source_path = normalized;
        record.cache_path = cache.empty() ? std::filesystem::path{} : NormalizeProjectPath(cache);
        record.kind = kind;
        missing_guids_.erase(record.guid);
        records_.push_back(std::move(record));
        InvalidateMissingFiles();
        return records_.back();
    }

    bool AssetDatabase::Remove(const std::string& guid)
    {
        const auto found = std::remove_if(records_.begin(), records_.end(),
            [&guid](const AssetRecord& record) { return record.guid == guid; });
        if (found == records_.end()) return false;
        records_.erase(found, records_.end());
        missing_guids_.erase(guid);
        InvalidateMissingFiles();
        return true;
    }

    bool AssetDatabase::RelocatePath(const std::filesystem::path& old_source,
        const std::filesystem::path& new_source, bool update_display_name)
    {
        const std::filesystem::path old_normalized = NormalizeProjectPath(old_source);
        const std::filesystem::path new_normalized = NormalizeProjectPath(new_source);
        const std::string old_key = PathKey(old_normalized);
        const auto found = std::find_if(records_.begin(), records_.end(),
            [&old_key](const AssetRecord& record)
            { return PathKey(record.source_path) == old_key; });
        if (found == records_.end()) return false;

        // 同じ行先を別 GUID が既に使っている場合は、参照を壊すので拒否する。
        const std::string new_key = PathKey(new_normalized);
        const auto collision = std::find_if(records_.begin(), records_.end(),
            [&new_key, &found](const AssetRecord& record)
            { return &record != &*found && PathKey(record.source_path) == new_key; });
        if (collision != records_.end()) return false;

        found->source_path = new_normalized;
        if (update_display_name) found->display_name = new_normalized.stem().u8string();
        missing_guids_.erase(found->guid);
        InvalidateMissingFiles();
        return true;
    }

    std::size_t AssetDatabase::RelocateTree(const std::filesystem::path& old_root,
        const std::filesystem::path& new_root)
    {
        const std::filesystem::path old_normalized = NormalizeProjectPath(old_root);
        const std::filesystem::path new_normalized = NormalizeProjectPath(new_root);
        if (PathKey(old_normalized) == PathKey(new_normalized)) return 0;

        // 移動先が既に埋まっている record は書き換えない。2 件が同じ source_path
        // へ着地すると、どちらのファイル名だったかを後から復元できなくなる。
        std::vector<std::string> destination_keys;
        destination_keys.reserve(records_.size());
        for (const AssetRecord& record : records_)
        {
            std::filesystem::path relative;
            if (PathInsideOrEqual(record.source_path, old_normalized, relative)) continue;
            destination_keys.push_back(PathKey(record.source_path));
        }

        std::size_t changed = 0;
        for (AssetRecord& record : records_)
        {
            std::filesystem::path relative;
            if (!PathInsideOrEqual(record.source_path, old_normalized, relative)) continue;

            const std::filesystem::path destination = relative.empty()
                ? new_normalized
                : (new_normalized / relative).lexically_normal();
            const std::string destination_key = PathKey(destination);
            if (std::find(destination_keys.begin(), destination_keys.end(),
                destination_key) != destination_keys.end())
            {
                continue;
            }

            record.source_path = destination;
            record.display_name = record.source_path.stem().u8string();
            missing_guids_.erase(record.guid);
            destination_keys.push_back(destination_key);
            ++changed;
        }
        if (changed > 0) InvalidateMissingFiles();
        return changed;
    }

    std::vector<const AssetRecord*> AssetDatabase::FindFolderSourcePaths() const
    {
        // source_path が拡張子を持たない record。フォルダへ潰れた壊れ方の目印。
        std::vector<const AssetRecord*> broken;
        for (const AssetRecord& record : records_)
        {
            if (record.source_path.empty()) continue;
            if (!record.source_path.extension().empty()) continue;
            broken.push_back(&record);
        }
        return broken;
    }

    const AssetRecord* AssetDatabase::FindByGuid(const std::string& guid) const noexcept
    {
        const auto found = std::find_if(records_.begin(), records_.end(),
            [&guid](const AssetRecord& record) { return record.guid == guid; });
        return found == records_.end() ? nullptr : &*found;
    }

    const AssetRecord* AssetDatabase::FindByPath(const std::filesystem::path& path) const noexcept
    {
        const std::string key = PathKey(NormalizeProjectPath(path));
        const auto found = std::find_if(records_.begin(), records_.end(),
            [&key](const AssetRecord& record) { return PathKey(record.source_path) == key; });
        return found == records_.end() ? nullptr : &*found;
    }

    bool AssetDatabase::HasPathGuidReservation(const std::filesystem::path& path) const
    {
        const std::filesystem::path normalized = NormalizeProjectPath(path);
        if (FindByPath(normalized) != nullptr) return false;
        return FindByGuid(MakeGuid(normalized)) != nullptr;
    }
    //指定されたパスにGUIDの予約がされているかどうかを確認する
    void AssetDatabase::RefreshMissingFiles(const std::filesystem::path& project_root)
    {
        missing_guids_.clear();

        std::error_code root_error;
        const std::filesystem::path root = std::filesystem::absolute(
            project_root, root_error).lexically_normal();
        if (root_error)
        {
            missing_files_cached_ = true;
            return;
        }

        const std::filesystem::path project = root.parent_path();
        for (const AssetRecord& record : records_)
        {
            const std::filesystem::path source = record.source_path.is_absolute()
                ? record.source_path : project / record.source_path;
            std::filesystem::path relative;
            if (!PathInsideOrEqual(source.lexically_normal(), root, relative)) continue;

            std::error_code file_error;
            if (!std::filesystem::is_regular_file(source, file_error) || file_error)
                missing_guids_.insert(record.guid);
        }
        missing_files_cached_ = true;
    }

    bool AssetDatabase::IsMissing(const std::string& guid) const noexcept
    {
        return missing_guids_.find(guid) != missing_guids_.end();
    }
}
