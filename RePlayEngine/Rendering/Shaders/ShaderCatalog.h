#pragma once

#include "ShaderAsset.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Rendering
{
    // ShaderID からシェーダを引く目録。
    //
    // ScriptTypeCatalog と同じ役割。
    //   ・実行中に中身が変わる（保存で再コンパイルされる）ので静的な表にしない
    //   ・ShaderLibrary が 1 つ所有する
    //   ・Inspector のドロップダウンはここから作る
    //
    // バイトコードはまだ持たない（フェーズ 4 以降で ShaderProgram を足す）。
    // 今はメタデータの目録として機能する。
    class ShaderCatalog final
    {
    public:
        struct Entry final
        {
            ShaderSourceInfo info;
            ShaderPropertySchemaRef schema;

            // 直近のコンパイル結果。
            // 失敗していても Entry は消さない。値を保持し続けるため。
            bool compiled = false;
            std::string last_error;
        };

        // 登録または更新。既存の ID なら中身を差し替える。
        void Register(Entry entry);

        // 見つからなければ nullptr。
        const Entry* Find(ShaderID id) const noexcept;

        ShaderPropertySchemaRef FindSchema(ShaderID id) const noexcept;

        const std::vector<Entry>& All() const noexcept { return entries_; }

        std::size_t Count() const noexcept { return entries_.size(); }

        void Clear() noexcept;

        // Inspector のドロップダウン用。"Lit/Standard" のような
        // MenuPath を集めて重複を除き、並べ替えて返す。
        std::vector<std::string> MenuPaths() const;

        // 同じ GUID が 2 回登録されようとした件数。
        //
        // 0 でないときはコピペでシェーダを作って GUID を消し忘れている。
        // 起動時にログへ出して気付けるようにすること。
        std::size_t DuplicateIdCount() const noexcept { return duplicate_ids_; }

    private:
        std::vector<Entry> entries_;
        std::unordered_map<std::string, std::size_t> index_;
        std::size_t duplicate_ids_ = 0;
    };
}
