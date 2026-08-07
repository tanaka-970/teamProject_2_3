#pragma once

#include "ShaderAsset.h"
#include "ShaderDiagnostic.h"

#include <d3d11.h>
#include <wrl.h>

#include <filesystem>
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
        // 1 つの変種（Static / Skinned）のコンパイル結果。
        struct VariantResult final
        {
            // 直近のコンパイルが成功したか。
            //
            // 【失敗しても Entry を消さないこと】
            //   消すと Material が参照している ShaderID が引けなくなり、
            //   Missing Shader 扱いになってプロパティ値が失われる。
            //   構文エラーを 1 つ書いただけで設定が飛ぶのは論外。
            //   コンパイルが失敗しても、宣言と値は保持し続ける。
            bool compiled = false;

            // 一度でもコンパイルに成功したか。
            // 「まだ試していない」と「試して失敗した」を区別する。
            bool ever_compiled = false;

            // 成功したバイトコード。失敗時は前回のものを保持する。
            Microsoft::WRL::ComPtr<ID3DBlob> bytecode;

            // 直近のコンパイルで出た診断。成功時も警告が入る。
            std::vector<ShaderDiagnostic> diagnostics;

            std::size_t ErrorCount() const noexcept;
            const ShaderDiagnostic* FirstError() const noexcept;
        };

        struct PassResult final
        {
            ShaderPassInfo info;
            VariantResult variants[shader_variant_count];

            VariantResult& At(ShaderVariant variant) noexcept
            {
                return variants[static_cast<int>(variant)];
            }
            const VariantResult& At(ShaderVariant variant) const noexcept
            {
                return variants[static_cast<int>(variant)];
            }
        };

        struct Entry final
        {
            ShaderSourceInfo info;
            ShaderPropertySchemaRef schema;

            // Shader-owned additional passes. info.passes と同じ宣言順。
            // Material の Layer 順序とは独立しており、Editor から並べ替えない。
            std::vector<PassResult> passes;

            // Static / Skinned それぞれの結果。
            // 使わない変種（layer の Skinned など）は触られない。
            VariantResult variants[shader_variant_count];

            // 最後に読んだソースの更新時刻。保存検出に使う。
            std::filesystem::file_time_type source_write_time{};

            VariantResult& At(ShaderVariant variant) noexcept
            {
                return variants[static_cast<int>(variant)];
            }
            const VariantResult& At(ShaderVariant variant) const noexcept
            {
                return variants[static_cast<int>(variant)];
            }

            bool UsesVariant(ShaderVariant variant) const noexcept
            {
                return ShaderDomainUsesVariant(info.domain, variant);
            }

            // 使う変種が全部通ったか。1 つでも落ちていれば false。
            //
            // 片方だけ通った状態を「成功」と呼ばないこと。
            // スキンだけ落ちていると、キャラだけ描けない状態になる。
            bool AllCompiled() const noexcept;

            // 一度でも通ったことがあるか（使う変種すべてについて）。
            bool EverCompiled() const noexcept;

            // 使う変種のエラー合計。
            std::size_t ErrorCount() const noexcept;
            const ShaderDiagnostic* FirstError() const noexcept;
        };

        // 登録または更新。既存の ID なら中身を差し替える。
        void Register(Entry entry);

        // 見つからなければ nullptr。
        const Entry* Find(ShaderID id) const noexcept;

        // 書き換え用。ShaderLibrary がコンパイル結果を入れるのに使う。
        Entry* FindMutable(ShaderID id) noexcept;

        std::vector<Entry>& AllMutable() noexcept { return entries_; }

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
