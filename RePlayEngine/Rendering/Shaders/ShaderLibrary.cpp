#include "ShaderLibrary.h"

#include "ShaderConstantPacker.h"
#include "ShaderSource.h"

#include <algorithm>
#include <cctype>
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

                // まだコンパイルしない。フェーズ 4 以降で行う。
                catalog_entry.compiled = false;

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

        last_report_ = report;
        Log("Info", report.Summary());
        return report;
    }
}
