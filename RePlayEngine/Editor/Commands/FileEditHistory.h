#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Editor
{
    // Scene 以外の Project Asset / ProjectSettings 用 Undo。
    //
    // SceneEditHistory と混ぜない理由:
    //   Scene は SceneData snapshot、外部 Asset はファイルそのものが正本で、
    //   復元方法と寿命が異なる。ここでは byte snapshot だけを扱い、復元後の
    //   Runtime cache invalidation は framework 側が担当する。
    class FileEditHistory final
    {
    public:
        static constexpr std::size_t maximum_entries = 128;
        static constexpr std::uintmax_t maximum_snapshot_bytes = 4u * 1024u * 1024u;

        bool Begin(const std::filesystem::path& path, std::string label,
            std::string& error);
        bool Commit(std::string& error);
        void Cancel() noexcept;

        // 呼び出し側が「保存前 bytes」をすでに持っている場合の即時操作用。
        bool RecordSavedChange(const std::filesystem::path& path, std::string label,
            const std::vector<std::uint8_t>& before, std::string& error);

        bool Undo(std::filesystem::path& restored_path, std::string& label,
            std::string& error);
        bool Redo(std::filesystem::path& restored_path, std::string& label,
            std::string& error);

        bool CanUndo() const noexcept { return cursor_ > 0; }
        bool CanRedo() const noexcept { return cursor_ < entries_.size(); }
        bool InTransaction() const noexcept { return transaction_.active; }
        const std::filesystem::path& TransactionPath() const noexcept
        {
            return transaction_.path;
        }
        void Clear() noexcept;

        static bool ReadFile(const std::filesystem::path& path,
            std::vector<std::uint8_t>& bytes, std::string& error);

    private:
        struct Entry final
        {
            std::filesystem::path path;
            std::string label;
            std::vector<std::uint8_t> before;
            std::vector<std::uint8_t> after;
        };
        struct Transaction final
        {
            bool active = false;
            std::filesystem::path path;
            std::string label;
            std::vector<std::uint8_t> before;
        };

        static std::filesystem::path Normalize(const std::filesystem::path& path);
        static bool WriteFileAtomic(const std::filesystem::path& path,
            const std::vector<std::uint8_t>& bytes, std::string& error);
        bool Push(Entry entry);

        std::vector<Entry> entries_;
        std::size_t cursor_ = 0;
        Transaction transaction_;
    };
}
