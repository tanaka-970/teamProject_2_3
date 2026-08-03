#include "ScriptError.h"

#include <algorithm>

namespace ReplayEngine::Scripting
{
    const char* ToString(ScriptErrorKind kind) noexcept
    {
        switch (kind)
        {
        case ScriptErrorKind::Load:       return "Load Error";
        case ScriptErrorKind::Runtime:    return "Runtime Error";
        case ScriptErrorKind::Field:      return "Field Error";
        case ScriptErrorKind::Unresolved: return "Unresolved";
        }
        return "Error";
    }

    std::string ScriptErrorRecord::Describe() const
    {
        std::string text = ToString(kind);
        text += " [";
        text += ToString(language);
        text += "] ";

        if (!script_name.empty()) text += script_name;
        else if (script_type.IsValid()) text += script_type.ToString();
        else text += "(script 未設定)";

        if (!class_name.empty())
        {
            text += " (";
            text += class_name;
            text += ")";
        }

        if (!object_name.empty())
        {
            text += " on \"";
            text += object_name;
            text += "\"";
        }
        if (object.Valid())
        {
            text += " #";
            text += object.ToString();
        }

        if (!callback.empty())
        {
            text += " :: ";
            text += callback;
        }

        if (!file.empty())
        {
            text += " @ ";
            text += file;
            if (line > 0)
            {
                text += ":";
                text += std::to_string(line);
            }
        }

        if (!message.empty())
        {
            text += " — ";
            text += message;
        }
        return text;
    }

    std::string ScriptErrorRecord::Key() const
    {
        // 同じ「場所」の繰り返しだけを畳む。
        //
        // ObjectID まで含めるのは、100 体の GameObject が同じスクリプトで
        // それぞれ落ちている状況を 1 件へ畳んでしまわないため。
        // どれが落ちているのか分からなくなる方が困る。
        std::string key = script_type.ToString();
        key += '|';
        key += std::to_string(static_cast<int>(kind));
        key += '|';
        key += object.ToString();
        key += '|';
        key += std::to_string(component);
        key += '|';
        key += callback;
        key += '|';
        key += file;
        key += '|';
        key += std::to_string(line);
        key += '|';
        key += message;
        return key;
    }

    ScriptErrorLog::Entry* ScriptErrorLog::Find(const std::string& key) noexcept
    {
        for (std::size_t index = 0; index < keys_.size(); ++index)
        {
            if (keys_[index] == key) return &entries_[index];
        }
        return nullptr;
    }

    bool ScriptErrorLog::Record(const ScriptErrorRecord& record, double now_seconds)
    {
        ++total_count_;

        const std::string key = record.Key();

        if (Entry* existing = Find(key))
        {
            ++existing->occurrence_count;
            existing->last_time = now_seconds;

            // 記録は常に最新へ差し替える。
            // 同じ Key でも stack trace が増えている場合があるため。
            existing->record = record;

            latest_index_ = static_cast<std::size_t>(existing - entries_.data());
            has_latest_ = true;

            if (existing->occurrence_count <= verbatim_limit)
            {
                ++existing->emitted_count;
                existing->last_emit_time = now_seconds;
                return true;
            }

            // 抑制へ入ったあとは間隔を空けてまとめて出す。
            if (now_seconds - existing->last_emit_time >= aggregate_interval)
            {
                ++existing->emitted_count;
                existing->last_emit_time = now_seconds;
                return true;
            }
            return false;
        }

        Entry entry;
        entry.record = record;
        entry.occurrence_count = 1;
        entry.emitted_count = 1;
        entry.last_emit_time = now_seconds;
        entry.first_time = now_seconds;
        entry.last_time = now_seconds;

        entries_.push_back(std::move(entry));
        keys_.push_back(key);

        latest_index_ = entries_.size() - 1;
        has_latest_ = true;
        return true;
    }

    std::vector<std::string> ScriptErrorLog::BuildSummary() const
    {
        std::vector<std::string> lines;
        lines.reserve(entries_.size());

        for (const Entry& entry : entries_)
        {
            std::string line = entry.record.Describe();
            line += "  （合計 ";
            line += std::to_string(entry.occurrence_count);
            line += " 回 / 出力 ";
            line += std::to_string(entry.emitted_count);
            line += " 回）";
            lines.push_back(std::move(line));
        }
        return lines;
    }

    const ScriptErrorRecord* ScriptErrorLog::Latest() const noexcept
    {
        if (!has_latest_ || latest_index_ >= entries_.size()) return nullptr;
        return &entries_[latest_index_].record;
    }

    void ScriptErrorLog::Clear() noexcept
    {
        entries_.clear();
        keys_.clear();
        total_count_ = 0;
        latest_index_ = 0;
        has_latest_ = false;
    }
}
