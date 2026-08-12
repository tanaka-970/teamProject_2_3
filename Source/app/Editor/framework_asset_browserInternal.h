#pragma once

// これは framework_asset_browser の分割内部で共有する実装であり、外部から使うものではない。

#include <filesystem>
#include <string>

namespace framework_asset_browser::Detail
{
    inline std::wstring LowerExtension(const std::filesystem::path& path)
    {
        std::wstring extension = path.extension().wstring();
        for (wchar_t& character : extension)
        {
            character = static_cast<wchar_t>(towlower(character));
        }
        return extension;
    }
}
