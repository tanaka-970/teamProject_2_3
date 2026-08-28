#pragma once

#include "ScriptFieldSchema.h"
#include "ScriptLanguage.h"
#include "ScriptTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ReplayEngine::Scripting
{
    // スクリプト型 1 つ分の目録。
    //
    // ComponentTypeInfo に相当するが、ComponentRegistry へは登録しない。
    // GameObject が所有する実体は常に ScriptComponent 1 種のままにするため。
    //
    // Add Component へ「RotatingObject」「EnemyBrain」と並べるのは
    // Phase 6 で AddComponentPanel がこの目録も列挙する形で行う。
    // ComponentRegistry 側には手を入れない。
    struct ScriptTypeDescriptor final
    {
        ScriptTypeID type_id;
        ScriptLanguage language = ScriptLanguage::Lua;

        // 保存・照合に使う識別名。"RotatingObject"
        std::string script_name;

        // Add Component と Inspector ヘッダーに出す名前。"Rotating Object"
        // 空なら script_name を整形して使う。
        std::string display_name;

        std::string asset_guid;

        // C# の完全修飾クラス名。Lua では空。
        std::string class_name;

        // Add Component のカテゴリ。既定は "Scripts/Lua" / "Scripts/C#"。
        std::string category;

        // 現在の状態。Error のときも schema は「最後に成功したもの」を保つ。
        ScriptStatus status = ScriptStatus::Unresolved;
        std::string last_error;

        ScriptFieldSchemaRef schema;

        const std::string& DisplayName() const noexcept
        {
            return display_name.empty() ? script_name : display_name;
        }

        std::string ResolvedCategory() const;
    };

    // ScriptTypeID -> ScriptTypeDescriptor の目録。
    //
    // ScriptRuntime が 1 つ所有する。静的な表にしないのは、
    // 中身が「今どのスクリプトが読めているか」という可変の状態であり、
    // ComponentRegistry のような不変の型情報とは性質が違うため。
    //
    // Play セッションをまたいで生存する。Schema のキャッシュがここにあるので、
    // Play / Stop を繰り返しても Lua の読み直しが起きない。
    class ScriptTypeCatalog final
    {
    public:
        // 登録または更新。既存の type_id なら中身を差し替える。
        // Schema の差し替えは ScriptRuntime の同期点からのみ呼ぶこと。
        void Register(ScriptTypeDescriptor descriptor);

        // Schema だけを差し替え、revision を 1 進める。
        // 型が未登録なら false。
        bool ReplaceSchema(ScriptTypeID type_id, ScriptFieldSchemaRef schema,
            ScriptStatus status, std::string last_error = std::string());

        bool Remove(ScriptTypeID type_id) noexcept;
        std::size_t RemoveLanguage(ScriptLanguage language) noexcept;
        void Clear() noexcept;

        const ScriptTypeDescriptor* Find(ScriptTypeID type_id) const noexcept;

        // Add Component 用。登録順のまま返す。
        const std::vector<ScriptTypeDescriptor>& All() const noexcept { return entries_; }

        std::size_t Count() const noexcept { return entries_.size(); }

        // 引けなければ空の shared_ptr。呼び出し側は「未解決」として扱う。
        ScriptFieldSchemaRef FindSchema(ScriptTypeID type_id) const;

        std::string FindDisplayName(ScriptTypeID type_id) const;

        // 次に発行する Schema の revision。差し替えのたびに増える。
        std::uint32_t NextRevision() noexcept { return ++revision_counter_; }

    private:
        ScriptTypeDescriptor* FindMutable(ScriptTypeID type_id) noexcept;

        std::vector<ScriptTypeDescriptor> entries_;
        std::uint32_t revision_counter_ = 0;
    };
}
