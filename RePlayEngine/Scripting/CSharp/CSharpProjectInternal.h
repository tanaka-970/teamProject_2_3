#pragma once

#include "CSharpProject.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Scripting::CSharp::Detail
{
    // これは CSharpProject の分割内部で共有する実装であり、外部から使うものではない。
    std::filesystem::path NormalizeRoot(std::filesystem::path root);
    std::string ReadAllText(const std::filesystem::path& path);
    bool WriteTextIfChanged(const std::filesystem::path& path,
        const std::string& text, std::string& error);
    bool SourceTreeIsNewer(const std::filesystem::path& source_root,
        const std::filesystem::path& output,
        const std::vector<std::filesystem::path>& dependencies = {});
    std::wstring ToWide(const std::string& text);
    std::string FromWide(const std::wstring& text);
    std::wstring Quote(const std::filesystem::path& path);
    std::wstring QuoteText(const std::wstring& text);
    CSharpBuildResult RunDotnet(const std::wstring& arguments,
        const std::filesystem::path& expected_assembly);
    void ParseDiagnostics(CSharpBuildResult& result);
}
