#pragma once

#include "ShaderCatalog.h"
#include "ShaderCompiler.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering
{
    // .hlsl を走査して Catalog を埋める。
    //
    // 【走査するフォルダ】
    //   Shader/Materials/**    surface
    //   Shader/Layers/**       layer
    //   Shader/PostProcess/**  postprocess
    //
    // Shader/ 直下は走査しない。
    // あそこは既存のパスが直接 .cso で読んでいるファイルで、
    // #pragma も持っていない。混ぜると GUID が振られて差分が出る。
    class ShaderLibrary final
    {
    public:
        // ログの出し先。エンジンから Editor へ直接依存しないための口。
        //
        // 「エンジンは理由を知っているのに画面へ出さない」を避けるため、
        // 走査・採番・失敗のすべてをここへ流す。呼び出し側が
        // Console と Saved/Diagnostics の両方へ落とすこと。
        using LogSink = std::function<void(const std::string& severity,
            const std::string& message,
            const std::filesystem::path& file, int line)>;

        struct ScanReport final
        {
            std::size_t scanned = 0;        // 見つかった .hlsl の枚数
            std::size_t registered = 0;     // Catalog へ載った枚数
            std::size_t assigned_guids = 0; // 採番したもの
            std::size_t parse_issues = 0;   // pragma の書式エラー
            std::size_t duplicate_ids = 0;  // GUID の重複
            std::size_t failed = 0;         // 読めなかったもの
            std::size_t compiled = 0;       // コンパイルに成功した枚数
            std::size_t compile_failed = 0; // コンパイルに失敗した枚数

            std::string Summary() const;
        };

        void SetLogSink(LogSink sink) { log_ = std::move(sink); }

        // 走査してカタログを作り直す。
        //
        // 走査のあと、そのまま全部コンパイルする。
        // 分けても得が無く、分けると「走査したがコンパイルしていない」
        // 中途半端な状態が生まれて扱いが増える。
        ScanReport ScanAll(const std::filesystem::path& project_root);

        // カタログ内の全シェーダをコンパイルする。
        //
        // #pragma property から cbuffer を自動生成してソースの先頭へ差し込み、
        // D3DCompile へ渡す。人は cbuffer を書かない。
        //
        // 失敗しても Entry は消さず、直前に成功したバイトコードを保持する。
        // 構文エラーを 1 つ書いただけで Material の設定が飛ぶのを避けるため。
        ScanReport CompileAll(bool debug_build);

        // 1 枚だけコンパイルし直す。
        bool CompileOne(ShaderID id, bool debug_build);

        // 保存を検出したものだけコンパイルし直す。
        //
        // 戻り値は再コンパイルした枚数。
        // 毎フレーム呼ばず、1 秒程度の間隔で呼ぶこと
        // （poll_csharp_script_changes と同じ考え方）。
        std::size_t PollSourceChanges(bool debug_build);

        ShaderCatalog& Catalog() noexcept { return catalog_; }
        const ShaderCatalog& Catalog() const noexcept { return catalog_; }

        const ScanReport& LastReport() const noexcept { return last_report_; }

        // 走査対象のフォルダ。project_root からの相対。
        static std::vector<std::pair<std::filesystem::path, ShaderDomain>>
            ScanFolders();

    private:
        void Log(const char* severity, const std::string& message,
            const std::filesystem::path& file = {}, int line = 0) const;

        // 1 件をコンパイルして結果を Entry へ書き込む。ログは出さない。
        // 呼び出し側が「何件成功したか」をまとめてから出す。
        //
        // 使う変種（surface なら Static と Skinned）を全部コンパイルし、
        // 全部通ったときだけ true。片方だけ通った状態を成功と呼ばない。
        bool CompileEntry(ShaderCatalog::Entry& entry, bool debug_build);

        // 変種 1 つぶん。REPLAY_SKINNED を define して渡す。
        bool CompileVariant(ShaderCatalog::Entry& entry, ShaderVariant variant,
            bool debug_build);

        ShaderCatalog catalog_;
        ScanReport last_report_;
        LogSink log_;
    };
}
