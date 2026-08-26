#include "ShaderLibrary.h"

#include "ShaderConstantPacker.h"
#include "ShaderSource.h"

#include "ShaderCompiler.h"

#include <algorithm>
#include <cctype>
#include <cstring>
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
                bool has_fatal_parse_issue = false;
                for (const ShaderSource::ParseIssue& issue : parsed.issues)
                {
                    ++report.parse_issues;
                    if (issue.fatal) has_fatal_parse_issue = true;
                    Log(issue.fatal ? "Error" : "Warning",
                        issue.message, entry.path(), issue.line);
                }

                // replay_lighting の不明値など、意味を勝手に補えない宣言は
                // Catalog へ入れない。PBR へ黙って丸めると、指定したのに
                // 見た目が変わらない原因になる。
                if (has_fatal_parse_issue || !parsed.info.lighting_model_valid)
                {
                    ++report.failed;
                    Log("Error",
                        "致命的なシェーダ宣言エラーのため登録しません",
                        entry.path());
                    continue;
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
                catalog_entry.passes.reserve(parsed.info.passes.size());
                for (const ShaderPassInfo& pass_info : parsed.info.passes)
                {
                    ShaderCatalog::PassResult pass;
                    pass.info = pass_info;
                    catalog_entry.passes.push_back(std::move(pass));
                }

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
            const char* target = "ps_6_0";
        };

        DomainEntryPoint EntryPointFor(ShaderDomain domain) noexcept
        {
            switch (domain)
            {
            case ShaderDomain::Surface:     return { "main", "ps_6_0" };
            case ShaderDomain::Layer:       return { "main", "ps_6_0" };
            case ShaderDomain::PostProcess: return { "main", "ps_6_0" };
            default:                        return { "main", "ps_6_0" };
            }
        }

        std::string ReadAllText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return std::string();
            std::string text((std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char>());

            // Visual Studio の「UTF-8 with signature」で保存された HLSL も受ける。
            // ShaderLibrary は generated cbuffer をソース先頭へ差し込むため、
            // 元ファイルの BOM を残すと BOM がストリーム途中へ移動して DXC が
            // FbxDefault 等を失敗させる。Parser と Compiler の両方で正規化する。
            if (text.size() >= 3 &&
                static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB &&
                static_cast<unsigned char>(text[2]) == 0xBF)
            {
                text.erase(0, 3);
            }
            return text;
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

        // Shader-owned pass は Material Layer とは独立。
        // 宣言順を固定したまま、それぞれの entry point を同じ source/schema からコンパイルする。
        for (ShaderCatalog::PassResult& pass : entry.passes)
        {
            for (int index = 0; index < shader_variant_count; ++index)
            {
                const ShaderVariant variant = static_cast<ShaderVariant>(index);
                if (!entry.UsesVariant(variant)) continue;
                if (!CompilePassVariant(entry, pass, variant, debug_build)) all_ok = false;
            }
        }
        return all_ok;
    }

    bool ShaderLibrary::CompileVariant(ShaderCatalog::Entry& entry,
        ShaderVariant variant, bool debug_build)
    {
        const DomainEntryPoint domain_entry = EntryPointFor(entry.info.domain);
        return CompileVariantInto(entry, entry.At(variant), variant,
            domain_entry.entry, std::string(), debug_build);
    }

    bool ShaderLibrary::CompilePassVariant(ShaderCatalog::Entry& entry,
        ShaderCatalog::PassResult& pass, ShaderVariant variant, bool debug_build)
    {
        return CompileVariantInto(entry, pass.At(variant), variant,
            pass.info.entry_point.c_str(), "Pass " + pass.info.name + ": ", debug_build);
    }

    bool ShaderLibrary::CompileVariantInto(ShaderCatalog::Entry& entry,
        ShaderCatalog::VariantResult& result, ShaderVariant variant,
        const char* entry_point, const std::string& diagnostic_prefix,
        bool debug_build)
    {
        const std::string source = ReadAllText(entry.info.source_path);
        if (source.empty())
        {
            result.compiled = false;
            result.diagnostics.clear();
            ShaderDiagnostic item;
            item.severity = ShaderDiagnostic::Severity::Error;
            item.file = entry.info.source_path;
            item.message = diagnostic_prefix + "ソースを読めません";
            result.diagnostics.push_back(item);
            return false;
        }

        std::string declaration;
        if (entry.schema)
            declaration = ShaderConstantPacker::GenerateHlslDeclaration(*entry.schema);

        std::ostringstream combined;
        combined << "#line 1 \"REPLAY_GENERATED\"\n";
        combined << declaration;
        combined << "#line 1 \"" << entry.info.source_path.generic_u8string() << "\"\n";
        combined << source;

        ShaderCompiler::Options options = ShaderCompiler::DefaultOptions(debug_build);
        options.defines.emplace_back(shader_variant_define,
            variant == ShaderVariant::Skinned ? "1" : "0");

        const DomainEntryPoint domain_entry = EntryPointFor(entry.info.domain);
        ShaderBytecode bytecode;
        const ShaderCompileResult compiled = ShaderCompiler::CompileSource(
            combined.str(), entry.info.source_path,
            entry_point != nullptr && *entry_point != '\0' ? entry_point : domain_entry.entry,
            domain_entry.target, options, bytecode);

        result.diagnostics = compiled.diagnostics;
        if (!result.diagnostics.empty())
        {
            const std::string tag = diagnostic_prefix + "[" + ToString(variant) + "] ";
            for (ShaderDiagnostic& item : result.diagnostics)
                item.message = tag + item.message;
        }

        if (compiled.succeeded)
        {
            result.bytecode = bytecode;
            result.compiled = true;
            result.ever_compiled = true;
            return true;
        }

        // Compile failure は last successful bytecode を保持する。
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
            for (const ShaderCatalog::PassResult& pass : entry.passes)
            {
                for (int index = 0; index < shader_variant_count; ++index)
                {
                    const ShaderVariant variant = static_cast<ShaderVariant>(index);
                    if (!entry.UsesVariant(variant)) continue;
                    for (const ShaderDiagnostic& item : pass.At(variant).diagnostics)
                    {
                        const char* severity =
                            item.severity == ShaderDiagnostic::Severity::Error
                            ? "Error" : "Warning";
                        Log(severity,
                            (item.code.empty() ? std::string() : item.code + ": ") +
                            item.message, item.file, item.line);
                    }
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
        for (const ShaderCatalog::PassResult& pass : entry->passes)
        {
            for (int index = 0; index < shader_variant_count; ++index)
            {
                const ShaderVariant variant = static_cast<ShaderVariant>(index);
                if (!entry->UsesVariant(variant)) continue;
                for (const ShaderDiagnostic& item : pass.At(variant).diagnostics)
                {
                    const char* severity =
                        item.severity == ShaderDiagnostic::Severity::Error
                        ? "Error" : "Warning";
                    Log(severity,
                        (item.code.empty() ? std::string() : item.code + ": ") +
                        item.message, item.file, item.line);
                }
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

            bool has_fatal_parse_issue = false;
            if (!parsed.succeeded)
            {
                Log("Error", "シェーダを再解析できません。直前に成功した bytecode/schema を維持します",
                    entry.info.source_path);
                continue;
            }

            for (const ShaderSource::ParseIssue& issue : parsed.issues)
            {
                if (issue.fatal) has_fatal_parse_issue = true;
                Log(issue.fatal ? "Error" : "Warning", issue.message,
                    entry.info.source_path, issue.line);
            }

            // Hot Reload 中も ScanAll と同じ安全規則を使う。
            // 致命的な pragma / lighting 宣言が壊れた状態で新 source を compile すると、
            // HLSL 自体は通ってしまって古い schema と新しい bytecode が混ざる場合がある。
            // その状態を作らず、最後に成功した metadata + bytecode を丸ごと維持する。
            if (has_fatal_parse_issue || !parsed.info.lighting_model_valid)
            {
                Log("Error",
                    "致命的なシェーダ宣言エラーです。直前に成功した bytecode/schema を維持します",
                    entry.info.source_path);
                continue;
            }
            if (needs_guid)
            {
                Log("Error",
                    "replay_guid が消えています。Hot Reload では自動採番せず、直前の Shader を維持します",
                    entry.info.source_path);
                continue;
            }
            if (id_changed)
            {
                // 上で再走査を促すログを出している。古い ID の Entry へ新しい GUID の
                // bytecode を入れない。
                continue;
            }

            {
                std::uint32_t buffer_size = 0;
                ShaderConstantPacker::AssignOffsets(parsed.info.properties, buffer_size);

                // Revision を上げる。
                //
                // ScriptFieldSchema と同じ考え方。Material 側は
                // 「自分が持っている値の Revision」と比べて、
                // 増えた項目に既定値を入れ、消えた項目の値は捨てずに残す。
                const std::uint32_t revision =
                    entry.schema ? entry.schema->Revision() + 1 : 1;

                // Pass 宣言変更でも last-successful bytecode を可能な限り保持する。
                std::vector<ShaderCatalog::PassResult> replacement_passes;
                replacement_passes.reserve(parsed.info.passes.size());
                for (const ShaderPassInfo& pass_info : parsed.info.passes)
                {
                    auto found = std::find_if(entry.passes.begin(), entry.passes.end(),
                        [&pass_info](const ShaderCatalog::PassResult& old_pass)
                        { return old_pass.info.entry_point == pass_info.entry_point; });
                    ShaderCatalog::PassResult pass;
                    if (found != entry.passes.end()) pass = *found;
                    pass.info = pass_info;
                    replacement_passes.push_back(std::move(pass));
                }

                entry.info = parsed.info;
                entry.schema = std::make_shared<ShaderPropertySchema>(
                    parsed.info.id, parsed.info.properties, revision);
                entry.passes = std::move(replacement_passes);
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
