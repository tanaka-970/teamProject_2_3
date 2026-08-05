#include "ShaderLibrary.h"

#include "ShaderConstantPacker.h"
#include "ShaderSource.h"

#include "ShaderCompiler.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <system_error>

namespace ReplayEngine::Rendering
{
    std::string ShaderLibrary::ScanReport::Summary() const
    {
        std::ostringstream stream;
        stream << "シェーダ走査: " << scanned << " 枚見つかり "
               << registered << " 枚を登録";
        if (compiled != 0)       stream << " / コンパイル成功 " << compiled;
        if (compile_failed != 0) stream << " / コンパイル失敗 " << compile_failed;
        if (assigned_guids != 0) stream << " / GUID 採番 " << assigned_guids;
        if (parse_issues != 0)   stream << " / 書式エラー " << parse_issues;
        if (duplicate_ids != 0)  stream << " / GUID 重複 " << duplicate_ids;
        if (failed != 0)         stream << " / 読み込み失敗 " << failed;
        return stream.str();
    }

    std::vector<std::pair<std::filesystem::path, ShaderDomain>>
        ShaderLibrary::ScanFolders()
    {
        return {
            { std::filesystem::path("Shader") / "Materials",   ShaderDomain::Surface },
            { std::filesystem::path("Shader") / "Layers",      ShaderDomain::Layer },
            { std::filesystem::path("Shader") / "PostProcess", ShaderDomain::PostProcess },
        };
    }

    void ShaderLibrary::Log(const char* severity, const std::string& message,
        const std::filesystem::path& file, int line) const
    {
        if (!log_) return;
        log_(severity, message, file, line);
    }

    ShaderLibrary::ScanReport ShaderLibrary::ScanAll(
        const std::filesystem::path& project_root)
    {
        catalog_.Clear();
        ScanReport report;

        for (const auto& pair : ScanFolders())
        {
            const std::filesystem::path folder = project_root / pair.first;
            const ShaderDomain folder_domain = pair.second;

            std::error_code error;
            if (!std::filesystem::exists(folder, error) || error)
            {
                // フォルダが無いのは異常ではない。
                // まだ 1 枚も置いていないだけ。黙って飛ばす。
                error.clear();
                continue;
            }

            std::filesystem::recursive_directory_iterator iterator(folder,
                std::filesystem::directory_options::skip_permission_denied, error);
            if (error)
            {
                ++report.failed;
                Log("Warning", "フォルダを走査できません: " +
                    folder.generic_u8string(), folder);
                continue;
            }

            for (const std::filesystem::directory_entry& entry : iterator)
            {
                std::error_code entry_error;
                if (!entry.is_regular_file(entry_error) || entry_error) continue;

                std::string extension = entry.path().extension().u8string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (extension != ".hlsl") continue;

                ++report.scanned;

                bool needs_guid = true;
                ShaderSource::ParseResult parsed =
                    ShaderSource::ParseFile(entry.path(), needs_guid);

                if (!parsed.succeeded)
                {
                    ++report.failed;
                    Log("Error", "シェーダを読めません: " +
                        entry.path().generic_u8string(), entry.path());
                    continue;
                }

                // 書式エラーは黙って捨てない。
                //
                // 捨てると「#pragma を書いたのに欄が出ない」の原因が
                // 一切分からなくなる。1 件ずつ行番号付きで出す。
                for (const ShaderSource::ParseIssue& issue : parsed.issues)
                {
                    ++report.parse_issues;
                    Log("Warning", issue.message, entry.path(), issue.line);
                }

                if (needs_guid)
                {
                    ShaderID assigned;
                    std::string assign_error;
                    if (!ShaderSource::AssignGuid(entry.path(), assigned, assign_error))
                    {
                        ++report.failed;
                        Log("Error", "GUID を採番できません: " + assign_error,
                            entry.path());
                        continue;
                    }
                    ++report.assigned_guids;
                    parsed.info.id = assigned;
                    Log("Info", "GUID を採番しました: " + assigned.ToString(),
                        entry.path());
                }

                if (!parsed.info.id.IsValid())
                {
                    ++report.failed;
                    Log("Error", "GUID が無効です", entry.path());
                    continue;
                }

                // フォルダとファイルの宣言が食い違ったらフォルダを優先する。
                //
                // Shader/Layers に置いてあるのに domain surface と
                // 書いてあったら、ほぼ書き間違い。置き場所の方が意図に近い。
                // ただし黙って直さず、必ず知らせる。
                if (parsed.info.domain != folder_domain)
                {
                    Log("Warning",
                        std::string("replay_domain がフォルダと食い違います。") +
                        "宣言=" + ToString(parsed.info.domain) +
                        " フォルダ=" + ToString(folder_domain) +
                        " / フォルダ側を採用します",
                        entry.path());
                    parsed.info.domain = folder_domain;
                }

                // 定数バッファの割り当てをここで済ませる。
                //
                // Schema を作る前に確定させないと、
                // ConstantBufferSize が 0 のまま固定されてしまう。
                std::uint32_t buffer_size = 0;
                ShaderConstantPacker::AssignOffsets(parsed.info.properties, buffer_size);

                ShaderCatalog::Entry catalog_entry;
                catalog_entry.info = parsed.info;
                catalog_entry.schema = std::make_shared<ShaderPropertySchema>(
                    parsed.info.id, parsed.info.properties, 1);

                // 変種のコンパイル結果は既定で「まだ試していない」。
                // このあと CompileAll がまとめて埋める。

                std::error_code time_error;
                catalog_entry.source_write_time =
                    std::filesystem::last_write_time(entry.path(), time_error);

                catalog_.Register(std::move(catalog_entry));
                ++report.registered;
            }
        }

        report.duplicate_ids = catalog_.DuplicateIdCount();
        if (report.duplicate_ids != 0)
        {
            // コピペして #pragma replay_guid を消し忘れた状態。
            // 放置すると片方のマテリアルがもう片方のシェーダを引く。
            Log("Error", "GUID が重複しているシェーダがあります (" +
                std::to_string(report.duplicate_ids) +
                " 件)。コピーしたシェーダの replay_guid を消してください");
        }

        // 走査したらそのままコンパイルする。
        // 分けると「走査済みだがコンパイルしていない」中途半端な状態が生まれる。
        const ScanReport compile_report = CompileAll(false);
        report.compiled = compile_report.compiled;
        report.compile_failed = compile_report.compile_failed;

        last_report_ = report;
        Log("Info", report.Summary());
        return report;
    }

    namespace
    {
        // ドメインごとの入口関数と対象プロファイル。
        //
        // まだ ReplaySurface / ReplayLayer の土台（.hlsli）を作っていないので、
        // 当面は main を直接見る。フェーズ 4 で土台を入れたら差し替える。
        struct DomainEntryPoint final
        {
            const char* entry = "main";
            const char* target = "ps_5_0";
        };

        DomainEntryPoint EntryPointFor(ShaderDomain domain) noexcept
        {
            switch (domain)
            {
            case ShaderDomain::Surface:     return { "main", "ps_5_0" };
            case ShaderDomain::Layer:       return { "main", "ps_5_0" };
            case ShaderDomain::PostProcess: return { "main", "ps_5_0" };
            default:                        return { "main", "ps_5_0" };
            }
        }

        std::string ReadAllText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return std::string();
            return std::string((std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char>());
        }
    }

    bool ShaderLibrary::CompileEntry(ShaderCatalog::Entry& entry, bool debug_build)
    {
        bool all_ok = true;
        for (int index = 0; index < shader_variant_count; ++index)
        {
            const ShaderVariant variant = static_cast<ShaderVariant>(index);
            if (!entry.UsesVariant(variant)) continue;
            if (!CompileVariant(entry, variant, debug_build)) all_ok = false;
        }
        return all_ok;
    }

    bool ShaderLibrary::CompileVariant(ShaderCatalog::Entry& entry,
        ShaderVariant variant, bool debug_build)
    {
        ShaderCatalog::VariantResult& result = entry.At(variant);

        const std::string source = ReadAllText(entry.info.source_path);
        if (source.empty())
        {
            result.compiled = false;
            result.diagnostics.clear();
            ShaderDiagnostic item;
            item.severity = ShaderDiagnostic::Severity::Error;
            item.file = entry.info.source_path;
            item.message = "ソースを読めません";
            result.diagnostics.push_back(item);
            return false;
        }

        // cbuffer を自動生成してソースの先頭へ差し込む。
        //
        // 人に書かせない理由:
        //   HLSL の 16 バイト境界規則を踏み外すと値が 1 つずれる。
        //   ずれてもエラーにならず、絵が「なんとなく変」になるだけなので
        //   原因の特定が非常に難しい。宣言 1 か所から作れば食い違わない。
        std::string declaration;
        if (entry.schema)
        {
            declaration = ShaderConstantPacker::GenerateHlslDeclaration(*entry.schema);
        }

        // #line で行番号を元ソースへ戻す。
        std::ostringstream combined;

        // 生成した側で出たエラーが、人が書いた側の行を指さないようにする。
        // 名前を分けておけば「これは自分が書いた行ではない」と分かる。
        combined << "#line 1 \"REPLAY_GENERATED\"\n";
        combined << declaration;

        // ここから先は人が書いた .hlsl の 1 行目。
        //
        // これが無いと、差し込んだ cbuffer のぶんだけ行番号がずれて、
        // 「エラーの行をクリックしたら別の行へ飛ぶ」ことになる。
        combined << "#line 1 \"" << entry.info.source_path.generic_u8string() << "\"\n";
        combined << source;

        ShaderCompiler::Options options = ShaderCompiler::DefaultOptions(debug_build);

        // 変種は define で切り替える。
        //
        // ファイルを 2 つに分けない理由は、分けた瞬間に片方だけ直す事故が
        // 起きるから。実際 static_mesh_*_ps.hlsl と skinned_mesh_*_ps.hlsl は
        // 接線の作り方以外ほぼ同じ内容が二重に書かれている。
        options.defines.emplace_back(shader_variant_define,
            variant == ShaderVariant::Skinned ? "1" : "0");

        const DomainEntryPoint entry_point = EntryPointFor(entry.info.domain);

        // 失敗時に bytecode を触らない。
        // 直前に成功したものが残り、描画が続けられる。
        Microsoft::WRL::ComPtr<ID3DBlob> bytecode = result.bytecode;

        const ShaderCompileResult compiled = ShaderCompiler::CompileSource(
            combined.str(), entry.info.source_path,
            entry_point.entry, entry_point.target, options, bytecode);

        result.diagnostics = compiled.diagnostics;

        // どの変種で出た診断か分かるようにしておく。
        //
        // Static は通って Skinned だけ落ちることがあり、
        // そのとき「どちらの話か」が分からないと直しようがない。
        if (!result.diagnostics.empty())
        {
            const std::string tag =
                std::string("[") + ToString(variant) + "] ";
            for (ShaderDiagnostic& item : result.diagnostics)
            {
                item.message = tag + item.message;
            }
        }

        if (compiled.succeeded)
        {
            result.bytecode = bytecode;
            result.compiled = true;
            result.ever_compiled = true;
            return true;
        }

        // 【失敗しても消さない】
        //   compiled は false にするが、bytecode と schema は残す。
        //   Material が参照している ShaderID も生きたままなので、
        //   構文エラーを直せば設定を失わずに戻ってこられる。
        result.compiled = false;
        return false;
    }

    ShaderLibrary::ScanReport ShaderLibrary::CompileAll(bool debug_build)
    {
        ScanReport report;

        for (ShaderCatalog::Entry& entry : catalog_.AllMutable())
        {
            const bool ok = CompileEntry(entry, debug_build);

            // 診断は成功・失敗にかかわらず全部出す。
            // 警告を握り潰すと後で効いてくる。
            for (int index = 0; index < shader_variant_count; ++index)
            {
                const ShaderVariant variant = static_cast<ShaderVariant>(index);
                if (!entry.UsesVariant(variant)) continue;
                for (const ShaderDiagnostic& item : entry.At(variant).diagnostics)
                {
                    const char* severity =
                        item.severity == ShaderDiagnostic::Severity::Error
                        ? "Error" : "Warning";
                    Log(severity,
                        (item.code.empty() ? std::string() : item.code + ": ") +
                        item.message, item.file, item.line);
                }
            }

            if (ok)
            {
                ++report.compiled;
                continue;
            }

            ++report.compile_failed;

            if (entry.EverCompiled())
            {
                Log("Warning",
                    "コンパイルに失敗しました。直前に成功したものを使い続けます: " +
                    entry.info.DisplayName(), entry.info.source_path);
            }
        }

        // 画面に出ている集計を合わせる。
        //
        // ここを更新しないと「コンパイルし直したのに表示が前のまま」になり、
        // 直ったかどうかが画面から判断できなくなる。
        last_report_.compiled = report.compiled;
        last_report_.compile_failed = report.compile_failed;
        return report;
    }

    bool ShaderLibrary::CompileOne(ShaderID id, bool debug_build)
    {
        ShaderCatalog::Entry* entry = catalog_.FindMutable(id);
        if (entry == nullptr) return false;

        const bool ok = CompileEntry(*entry, debug_build);
        for (int index = 0; index < shader_variant_count; ++index)
        {
            const ShaderVariant variant = static_cast<ShaderVariant>(index);
            if (!entry->UsesVariant(variant)) continue;
            for (const ShaderDiagnostic& item : entry->At(variant).diagnostics)
            {
                const char* severity =
                    item.severity == ShaderDiagnostic::Severity::Error
                    ? "Error" : "Warning";
                Log(severity,
                    (item.code.empty() ? std::string() : item.code + ": ") +
                    item.message, item.file, item.line);
            }
        }
        return ok;
    }

    std::size_t ShaderLibrary::PollSourceChanges(bool debug_build)
    {
        std::size_t recompiled = 0;

        for (ShaderCatalog::Entry& entry : catalog_.AllMutable())
        {
            std::error_code error;
            const std::filesystem::file_time_type now =
                std::filesystem::last_write_time(entry.info.source_path, error);
            if (error) continue;
            if (now == entry.source_write_time) continue;

            entry.source_write_time = now;

            // 宣言が変わっているかもしれないので読み直す。
            //
            // property を足したのに Schema が古いままだと、
            // 「書いたのに欄が増えない」ことになる。
            bool needs_guid = false;
            ShaderSource::ParseResult parsed =
                ShaderSource::ParseFile(entry.info.source_path, needs_guid);
            // ID が変わっていたら差し替えない。
            //
            // index_ は ID 文字列で引いているので、ここで info を丸ごと
            // 入れ替えると目録から引けない項目ができる。
            // GUID を書き換えるのは走査し直すべき事態なので、知らせて止める。
            const bool id_changed =
                parsed.succeeded && parsed.info.id != entry.info.id;
            if (id_changed)
            {
                Log("Warning",
                    "replay_guid が変わりました。再走査してください: " +
                    entry.info.DisplayName(), entry.info.source_path);
            }

            if (parsed.succeeded && !needs_guid && !id_changed)
            {
                for (const ShaderSource::ParseIssue& issue : parsed.issues)
                {
                    Log("Warning", issue.message, entry.info.source_path, issue.line);
                }

                std::uint32_t buffer_size = 0;
                ShaderConstantPacker::AssignOffsets(parsed.info.properties, buffer_size);

                // Revision を上げる。
                //
                // ScriptFieldSchema と同じ考え方。Material 側は
                // 「自分が持っている値の Revision」と比べて、
                // 増えた項目に既定値を入れ、消えた項目の値は捨てずに残す。
                const std::uint32_t revision =
                    entry.schema ? entry.schema->Revision() + 1 : 1;

                entry.info = parsed.info;
                entry.schema = std::make_shared<ShaderPropertySchema>(
                    parsed.info.id, parsed.info.properties, revision);
            }

            Log("Info", "シェーダの変更を検出しました: " + entry.info.DisplayName(),
                entry.info.source_path);

            CompileOne(entry.info.id, debug_build);
            ++recompiled;
        }

        if (recompiled != 0)
        {
            // 1 枚だけ直しても、画面の集計は全体で数え直す。
            std::size_t ok = 0;
            std::size_t ng = 0;
            for (const ShaderCatalog::Entry& entry : catalog_.All())
            {
                if (entry.AllCompiled()) ++ok; else ++ng;
            }
            last_report_.compiled = ok;
            last_report_.compile_failed = ng;
        }
        return recompiled;
    }
}
