#include "AssetCache.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ReplayEngine::Assets
{
    namespace
    {
        constexpr std::uint32_t cache_magic = 0x43415052;
        constexpr std::uint32_t cache_container_version = 1;

        // ヘッダーに種類と元データの指紋を持たせ、古いキャッシュの誤読を防ぐ。
        struct CacheHeader
        {
            std::uint32_t magic = cache_magic;
            std::uint32_t container_version = cache_container_version;
            std::uint32_t asset_kind = 0;
            std::uint32_t format_version = 0;
            std::uint64_t source_fingerprint = 0;
            std::uint64_t payload_size = 0;
        };

        const char* KindFolder(AssetKind kind)
        {
            switch (kind)
            {
            case AssetKind::Model: return "models";
            case AssetKind::Image: return "images";
            case AssetKind::Audio: return "audio";
            case AssetKind::Shader: return "shaders";
            case AssetKind::Scene: return "scenes";
            case AssetKind::Material: return "materials";
            default: return "unknown";
            }
        }

        bool ReadFile(const std::filesystem::path& path,
            std::vector<std::uint8_t>& bytes, std::string& error)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                error = "キャッシュ対象ファイルを開けません";
                return false;
            }
            const std::streamoff size = stream.tellg();
            if (size < 0)
            {
                error = "ファイルサイズを取得できません";
                return false;
            }
            bytes.resize(static_cast<std::size_t>(size));
            stream.seekg(0, std::ios::beg);
            if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), size))
            {
                error = "ファイルを最後まで読み込めません";
                return false;
            }
            return true;
        }

        std::uint64_t Fingerprint(const std::vector<std::uint8_t>& bytes)
        {
            // 内容ベースのハッシュにより、更新日時に依存せず変更を検出する。
            std::uint64_t hash = 1469598103934665603ull;
            for (std::uint8_t byte : bytes)
            {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            return hash;
        }
    }

    AssetCache::AssetCache(std::filesystem::path root) : root_(std::move(root)) {}

    bool AssetCache::Describe(const std::filesystem::path& source, AssetKind kind,
        std::uint32_t format_version, AssetCacheEntry& entry, std::string& error) const
    {
        std::vector<std::uint8_t> source_bytes;
        if (!ReadFile(source, source_bytes, error)) return false;

        entry.source_path = source;
        entry.source_fingerprint = Fingerprint(source_bytes);
        entry.format_version = format_version;
        entry.kind = kind;

        // 変換形式の更新時は別ファイルになるようバージョンを名前に含める。
        std::ostringstream name;
        name << std::hex << std::setfill('0') << std::setw(16) << entry.source_fingerprint
            << "_v" << std::dec << format_version << ".replaycache";
        entry.cache_path = root_ / KindFolder(kind) / name.str();
        return true;
    }

    bool AssetCache::Store(const std::filesystem::path& source, AssetKind kind,
        std::uint32_t format_version, const std::vector<std::uint8_t>& payload,
        AssetCacheEntry& entry, std::string& error) const
    {
        if (!Describe(source, kind, format_version, entry, error)) return false;
        std::error_code filesystem_error;
        std::filesystem::create_directories(entry.cache_path.parent_path(), filesystem_error);
        if (filesystem_error)
        {
            error = "キャッシュフォルダを作成できません";
            return false;
        }

        // 検証情報をペイロードより先に書き、読み込み時の判定を軽くする。
        CacheHeader header{};
        header.asset_kind = static_cast<std::uint32_t>(kind);
        header.format_version = format_version;
        header.source_fingerprint = entry.source_fingerprint;
        header.payload_size = payload.size();

        std::ofstream stream(entry.cache_path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "キャッシュファイルを作成できません";
            return false;
        }
        stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!payload.empty())
            stream.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        if (!stream)
        {
            error = "キャッシュの書き込みに失敗しました";
            return false;
        }
        return true;
    }

    bool AssetCache::Load(const std::filesystem::path& source, AssetKind kind,
        std::uint32_t format_version, std::vector<std::uint8_t>& payload,
        AssetCacheEntry& entry, std::string& error) const
    {
        if (!Describe(source, kind, format_version, entry, error)) return false;
        std::ifstream stream(entry.cache_path, std::ios::binary);
        if (!stream) return false;

        // ペイロードを返す前に種類、形式、元ファイルの一致をまとめて検証する。
        CacheHeader header{};
        stream.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!stream || header.magic != cache_magic ||
            header.container_version != cache_container_version ||
            header.asset_kind != static_cast<std::uint32_t>(kind) ||
            header.format_version != format_version ||
            header.source_fingerprint != entry.source_fingerprint)
        {
            error = "キャッシュの形式または元ファイルの指紋が一致しません";
            return false;
        }

        payload.resize(static_cast<std::size_t>(header.payload_size));
        if (!payload.empty())
            stream.read(reinterpret_cast<char*>(payload.data()), payload.size());
        if (!stream)
        {
            error = "キャッシュの読み込みに失敗しました";
            payload.clear();
            return false;
        }
        return true;
    }

    bool AssetCache::StoreSourceFile(const std::filesystem::path& source, AssetKind kind,
        AssetCacheEntry& entry, std::string& error) const
    {
        std::vector<std::uint8_t> bytes;
        if (!ReadFile(source, bytes, error)) return false;
        return Store(source, kind, 0, bytes, entry, error);
    }
}
