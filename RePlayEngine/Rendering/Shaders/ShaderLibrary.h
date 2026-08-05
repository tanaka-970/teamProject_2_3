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

            std::string Summary() const;
        };

        void SetLogSink(LogSink sink) { log_ = std::move(sink); }

        // 走査してカタログを作り直す。
        //
        // コンパイルはしない。ここは「何があるか」を集めるだけ。
        // 実際のコンパイルはフェーズ 4 以降で ShaderProgram が担う。
        ScanReport ScanAll(const std::filesystem::path& project_root);

        ShaderCatalog& Catalog() noexcept { return catalog_; }
        const ShaderCatalog& Catalog() const noexcept { return catalog_; }

        const ScanReport& LastReport() const noexcept { return last_report_; }

        // 走査対象のフォルダ。project_root からの相対。
        static std::vector<std::pair<std::filesystem::path, ShaderDomain>>
            ScanFolders();

    private:
        void Log(const char* severity, const std::string& message,
            const std::filesystem::path& file = {}, int line = 0) const;

        ShaderCatalog catalog_;
        ScanReport last_report_;
        LogSink log_;
    };
}
