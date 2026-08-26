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
        enum class AppliedKind
        {
            None,
            FileContent,
            PathMove,
            PathCreate,
        };

        struct AppliedChange final
        {
            AppliedKind kind = AppliedKind::None;
            std::filesystem::path from_path;
            std::filesystem::path to_path;
            std::string label;
        };

        static constexpr std::size_t maximum_entries = 128;
        static constexpr std::uintmax_t maximum_snapshot_bytes = 4u * 1024u * 1024u;

        bool Begin(const std::filesystem::path& path, std::string label,
            std::string& error);
        bool Commit(std::string& error);
        void Cancel() noexcept;

        // 呼び出し側が「保存前 bytes」をすでに持っている場合の即時操作用。
        bool RecordSavedChange(const std::filesystem::path& path, std::string label,
            const std::vector<std::uint8_t>& before, std::string& error);

        // 既に filesystem 上で完了した move/rename/delete-to-trash を履歴へ積む。
        // Undo/Redo は rename で往復するため、ファイルでもフォルダでも内容を失わない。
        bool RecordPathMove(const std::filesystem::path& from,
            const std::filesystem::path& to, std::string label, std::string& error);

        // 新規作成/複製した path。Undo 時は hidden stash へ退避し、Redo で戻す。
        bool RecordPathCreated(const std::filesystem::path& path,
            const std::filesystem::path& stash_root, std::string label, std::string& error);

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
        const AppliedChange& LastAppliedChange() const noexcept { return last_applied_; }
        void Clear() noexcept;

        static bool ReadFile(const std::filesystem::path& path,
            std::vector<std::uint8_t>& bytes, std::string& error);

    private:
        enum class EntryKind
        {
            FileContent,
            PathMove,
            PathCreate,
        };

        struct Entry final
        {
            EntryKind kind = EntryKind::FileContent;
            std::filesystem::path path;
            std::filesystem::path secondary_path;
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
        static bool MovePath(const std::filesystem::path& from,
            const std::filesystem::path& to, std::string& error);
        static std::filesystem::path UniqueStashPath(const std::filesystem::path& root,
            const std::filesystem::path& original);
        bool Push(Entry entry);

        std::vector<Entry> entries_;
        std::size_t cursor_ = 0;
        Transaction transaction_;
        AppliedChange last_applied_;
    };
}
