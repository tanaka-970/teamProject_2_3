#include "FileEditHistory.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace ReplayEngine::Editor
{
    std::filesystem::path FileEditHistory::Normalize(const std::filesystem::path& path)
    {
        std::error_code error;
        std::filesystem::path absolute = std::filesystem::absolute(path, error);
        if (error) absolute = path;
        error.clear();
        const auto canonical = std::filesystem::weakly_canonical(absolute, error);
        return error ? absolute.lexically_normal() : canonical.lexically_normal();
    }

    bool FileEditHistory::ReadFile(const std::filesystem::path& path,
        std::vector<std::uint8_t>& bytes, std::string& error)
    {
        bytes.clear();
        std::error_code filesystem_error;
        if (!std::filesystem::exists(path, filesystem_error) || filesystem_error)
        {
            error = "Undo 対象ファイルが見つかりません: " + path.generic_u8string();
            return false;
        }
        const std::uintmax_t size = std::filesystem::file_size(path, filesystem_error);
        if (filesystem_error || size > maximum_snapshot_bytes)
        {
            error = size > maximum_snapshot_bytes
                ? "Undo snapshot 上限(4 MiB)を超えています。"
                : "Undo 対象ファイルのサイズを取得できません。";
            return false;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Undo 対象ファイルを開けません: " + path.generic_u8string();
            return false;
        }
        bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        if (!stream.eof() && stream.fail())
        {
            error = "Undo snapshot の読み込みに失敗しました。";
            bytes.clear();
            return false;
        }
        error.clear();
        return true;
    }

    bool FileEditHistory::WriteFileAtomic(const std::filesystem::path& path,
        const std::vector<std::uint8_t>& bytes, std::string& error)
    {
        std::error_code filesystem_error;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "Undo 復元先を作成できません。";
                return false;
            }
        }
        const std::filesystem::path temporary = path.string() + ".undo.tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "Undo 一時ファイルを作成できません。";
                return false;
            }
            if (!bytes.empty())
                stream.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            stream.close();
            if (!stream)
            {
                error = "Undo 一時ファイルの書き込みに失敗しました。";
                return false;
            }
        }
        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            filesystem_error.clear();
            std::filesystem::copy_file(temporary, path,
                std::filesystem::copy_options::overwrite_existing, filesystem_error);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            if (filesystem_error)
            {
                error = "Undo ファイルを差し替えられません。";
                return false;
            }
        }
        error.clear();
        return true;
    }

    bool FileEditHistory::MovePath(const std::filesystem::path& from,
        const std::filesystem::path& to, std::string& error)
    {
        std::error_code filesystem_error;
        if (!std::filesystem::exists(from, filesystem_error) || filesystem_error)
        {
            error = "移動元が見つかりません: " + from.generic_u8string();
            return false;
        }
        filesystem_error.clear();
        if (std::filesystem::exists(to, filesystem_error) && !filesystem_error)
        {
            error = "移動先が既に存在します: " + to.generic_u8string();
            return false;
        }
        if (!to.parent_path().empty())
        {
            filesystem_error.clear();
            std::filesystem::create_directories(to.parent_path(), filesystem_error);
            if (filesystem_error)
            {
                error = "移動先フォルダを作成できません: " + to.parent_path().generic_u8string();
                return false;
            }
        }
        filesystem_error.clear();
        std::filesystem::rename(from, to, filesystem_error);
        if (filesystem_error)
        {
            error = "ファイル/フォルダを移動できません: " + filesystem_error.message();
            return false;
        }
        error.clear();
        return true;
    }

    std::filesystem::path FileEditHistory::UniqueStashPath(
        const std::filesystem::path& root, const std::filesystem::path& original)
    {
        const auto ticks = static_cast<unsigned long long>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::filesystem::path folder = Normalize(root);
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        const std::string stem = original.filename().u8string().empty()
            ? "entry" : original.filename().u8string();
        for (unsigned int suffix = 0; suffix < 10000; ++suffix)
        {
            std::ostringstream name;
            name << ticks << "_" << suffix << "_" << stem;
            const std::filesystem::path candidate = folder / std::filesystem::u8path(name.str());
            error.clear();
            if (!std::filesystem::exists(candidate, error) || error) return candidate;
        }
        return folder / ("fallback_" + stem);
    }

    bool FileEditHistory::Begin(const std::filesystem::path& path, std::string label,
        std::string& error)
    {
        const std::filesystem::path normalized = Normalize(path);
        if (transaction_.active)
        {
            if (transaction_.path == normalized) return true;
            error = "別のファイル編集トランザクションが進行中です。";
            return false;
        }
        Transaction next;
        next.path = normalized;
        next.label = std::move(label);
        if (!ReadFile(next.path, next.before, error)) return false;
        next.active = true;
        transaction_ = std::move(next);
        return true;
    }

    bool FileEditHistory::Push(Entry entry)
    {
        if (entry.kind == EntryKind::FileContent && entry.before == entry.after) return false;
        if (cursor_ < entries_.size()) entries_.erase(entries_.begin() +
            static_cast<std::ptrdiff_t>(cursor_), entries_.end());
        entries_.push_back(std::move(entry));
        cursor_ = entries_.size();
        if (entries_.size() > maximum_entries)
        {
            const std::size_t remove = entries_.size() - maximum_entries;
            entries_.erase(entries_.begin(), entries_.begin() +
                static_cast<std::ptrdiff_t>(remove));
            cursor_ = entries_.size();
        }
        return true;
    }

    bool FileEditHistory::Commit(std::string& error)
    {
        if (!transaction_.active) return false;
        std::vector<std::uint8_t> after;
        if (!ReadFile(transaction_.path, after, error)) return false;
        Entry entry;
        entry.kind = EntryKind::FileContent;
        entry.path = transaction_.path;
        entry.label = transaction_.label;
        entry.before = std::move(transaction_.before);
        entry.after = std::move(after);
        transaction_ = {};
        error.clear();
        return Push(std::move(entry));
    }

    void FileEditHistory::Cancel() noexcept
    {
        transaction_ = {};
    }

    bool FileEditHistory::RecordSavedChange(const std::filesystem::path& path,
        std::string label, const std::vector<std::uint8_t>& before, std::string& error)
    {
        Entry entry;
        entry.kind = EntryKind::FileContent;
        entry.path = Normalize(path);
        entry.label = std::move(label);
        entry.before = before;
        if (!ReadFile(entry.path, entry.after, error)) return false;
        error.clear();
        return Push(std::move(entry));
    }

    bool FileEditHistory::RecordPathMove(const std::filesystem::path& from,
        const std::filesystem::path& to, std::string label, std::string& error)
    {
        Entry entry;
        entry.kind = EntryKind::PathMove;
        entry.path = Normalize(from);
        entry.secondary_path = Normalize(to);
        entry.label = std::move(label);
        std::error_code filesystem_error;
        if (!std::filesystem::exists(entry.secondary_path, filesystem_error) || filesystem_error)
        {
            error = "履歴へ記録する移動先が見つかりません: " + entry.secondary_path.generic_u8string();
            return false;
        }
        error.clear();
        return Push(std::move(entry));
    }

    bool FileEditHistory::RecordPathCreated(const std::filesystem::path& path,
        const std::filesystem::path& stash_root, std::string label, std::string& error)
    {
        Entry entry;
        entry.kind = EntryKind::PathCreate;
        entry.path = Normalize(path);
        entry.secondary_path = UniqueStashPath(stash_root, entry.path);
        entry.label = std::move(label);
        std::error_code filesystem_error;
        if (!std::filesystem::exists(entry.path, filesystem_error) || filesystem_error)
        {
            error = "履歴へ記録する作成物が見つかりません: " + entry.path.generic_u8string();
            return false;
        }
        error.clear();
        return Push(std::move(entry));
    }

    bool FileEditHistory::Undo(std::filesystem::path& restored_path,
        std::string& label, std::string& error)
    {
        if (transaction_.active)
        {
            error = "編集中の操作を確定してから Undo してください。";
            return false;
        }
        if (!CanUndo()) return false;
        Entry& entry = entries_[cursor_ - 1];
        last_applied_ = {};
        if (entry.kind == EntryKind::FileContent)
        {
            if (!WriteFileAtomic(entry.path, entry.before, error)) return false;
            restored_path = entry.path;
            last_applied_ = { AppliedKind::FileContent, entry.path, entry.path, entry.label };
        }
        else if (entry.kind == EntryKind::PathMove)
        {
            if (!MovePath(entry.secondary_path, entry.path, error)) return false;
            restored_path = entry.path;
            last_applied_ = { AppliedKind::PathMove, entry.secondary_path, entry.path, entry.label };
        }
        else
        {
            if (!MovePath(entry.path, entry.secondary_path, error)) return false;
            restored_path = entry.secondary_path;
            last_applied_ = { AppliedKind::PathCreate, entry.path, entry.secondary_path, entry.label };
        }
        --cursor_;
        label = entry.label;
        return true;
    }

    bool FileEditHistory::Redo(std::filesystem::path& restored_path,
        std::string& label, std::string& error)
    {
        if (transaction_.active)
        {
            error = "編集中の操作を確定してから Redo してください。";
            return false;
        }
        if (!CanRedo()) return false;
        Entry& entry = entries_[cursor_];
        last_applied_ = {};
        if (entry.kind == EntryKind::FileContent)
        {
            if (!WriteFileAtomic(entry.path, entry.after, error)) return false;
            restored_path = entry.path;
            last_applied_ = { AppliedKind::FileContent, entry.path, entry.path, entry.label };
        }
        else if (entry.kind == EntryKind::PathMove)
        {
            if (!MovePath(entry.path, entry.secondary_path, error)) return false;
            restored_path = entry.secondary_path;
            last_applied_ = { AppliedKind::PathMove, entry.path, entry.secondary_path, entry.label };
        }
        else
        {
            if (!MovePath(entry.secondary_path, entry.path, error)) return false;
            restored_path = entry.path;
            last_applied_ = { AppliedKind::PathCreate, entry.secondary_path, entry.path, entry.label };
        }
        ++cursor_;
        label = entry.label;
        return true;
    }

    void FileEditHistory::Clear() noexcept
    {
        entries_.clear();
        cursor_ = 0;
        transaction_ = {};
        last_applied_ = {};
    }
}
