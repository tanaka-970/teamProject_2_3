#pragma once

#include "ScriptLanguage.h"
#include "ScriptTypes.h"
#include "../../Core/ObjectID/ObjectID.h"
#include "../../Core/ObjectID/RuntimeIdentity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Scripting
{
    enum class ScriptErrorKind : std::int32_t
    {
        // 読み込み・構文の失敗。インスタンスが作れない。
        Load = 0,

        // Callback 実行中の失敗。インスタンスは生きている。
        Runtime,

        // Field の型が合わない・移送できないなど、値まわりの問題。
        Field,

        // 型が見つからない。データは保持されている。
        Unresolved,
    };

    const char* ToString(ScriptErrorKind kind) noexcept;

    // エラー 1 件分。指示書 14 章が要求する項目をすべて持つ。
    struct ScriptErrorRecord final
    {
        ScriptErrorKind kind = ScriptErrorKind::Runtime;
        ScriptLanguage language = ScriptLanguage::Lua;

        ScriptTypeID script_type;
        std::string script_name;      // "RotatingObject"
        std::string asset_guid;
        std::string class_name;       // C# のみ

        Core::ObjectID object;
        std::string object_name;
        Core::ComponentStableID component = Core::invalid_component_stable_id;

        // 空なら Callback 外（読み込み時など）。
        std::string callback;

        std::string file;
        int line = 0;

        std::string message;
        std::string stack_trace;

        // 人が読む 1 行。ログと Inspector の両方で使う。
        std::string Describe() const;

        // 同じエラーかどうかの判定に使う鍵。
        // 行番号とメッセージまで含めるので、同じ場所の繰り返しだけが畳まれる。
        std::string Key() const;
    };

    // 同じエラーを毎フレーム出し続けないための集約器。
    //
    // ---------------------------------------------------------------------
    // 【なぜ必要か】
    //
    //   OnUpdate の中で落ちるスクリプトは、1 秒間に 60 回・
    //   10 分放置すれば 36000 回まったく同じ行を出す。
    //   ログが流れて他の問題が見えなくなるうえ、
    //   文字列連結だけでフレーム時間を食う。
    //
    // 【方針（指示書 14 章）】
    //   最初の 5 回は出す
    //   それ以降は 1 秒ごとに「N 回発生」とまとめて出す
    //   停止時に総回数を出す
    //
    // ---------------------------------------------------------------------
    // 時間は外から渡す。ここで時計を持たないのは、
    // Validation が時間を進めて挙動を確かめられるようにするため。
    class ScriptErrorLog final
    {
    public:
        struct Entry
        {
            ScriptErrorRecord record;

            // 総発生回数。抑制されたぶんも数える。
            std::uint64_t occurrence_count = 0;

            // 実際に出力した回数。
            std::uint64_t emitted_count = 0;

            // 最後に出力した時刻（秒）。
            double last_emit_time = 0.0;

            // 最初と最後に発生した時刻（秒）。
            double first_time = 0.0;
            double last_time = 0.0;
        };

        // 最初にそのまま出す回数。
        static constexpr std::uint64_t verbatim_limit = 5;

        // 抑制へ入ったあとのまとめ出力の間隔（秒）。
        static constexpr double aggregate_interval = 1.0;

        // 1 件記録する。出力すべきなら true を返す。
        // 呼び出し側はそのときだけログを書く。
        bool Record(const ScriptErrorRecord& record, double now_seconds);

        // 抑制でまとめられた件数を含めた締めの一覧。Play 停止時に出す。
        std::vector<std::string> BuildSummary() const;

        const std::vector<Entry>& Entries() const noexcept { return entries_; }

        std::size_t UniqueCount() const noexcept { return entries_.size(); }
        std::uint64_t TotalCount() const noexcept { return total_count_; }

        // 直近のエラー。Inspector の Status 表示に使う。
        const ScriptErrorRecord* Latest() const noexcept;

        void Clear() noexcept;

    private:
        Entry* Find(const std::string& key) noexcept;

        std::vector<Entry> entries_;
        std::vector<std::string> keys_;   // entries_ と同じ並び

        std::uint64_t total_count_ = 0;
        std::size_t latest_index_ = 0;
        bool has_latest_ = false;
    };
}
