#include "ShaderComposerGenerator.h"
#include "../Shaders/ShaderConstantPacker.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace ReplayEngine::Rendering
{
    namespace
    {
        bool ReplaceAtomic(const std::filesystem::path& temporary,
            const std::filesystem::path& destination, std::string& error)
        {
#ifdef _WIN32
            if (MoveFileExW(temporary.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE)
                return true;
#endif
            std::error_code ec;
            std::filesystem::remove(destination, ec);
            ec.clear();
            std::filesystem::rename(temporary, destination, ec);
            if (!ec) return true;
            std::filesystem::remove(temporary, ec);
            error = "generated HLSL を確定できません: " + destination.generic_u8string();
            return false;
        }
    }

    bool ShaderComposerGenerator::GenerateToFile(const ShaderComposerAsset& asset,
        const std::filesystem::path& project_root, std::string& error)
    {
        error.clear();
        const ShaderComposerGenerateResult generated = Generate(asset);
        if (!generated.succeeded)
        {
            error = "Shader Composer generation failed";
            if (!generated.diagnostics.empty())
            {
                error += " / node " + std::to_string(generated.diagnostics.front().node_id) +
                    ": " + generated.diagnostics.front().message;
            }
            return false;
        }
        if (asset.generated_hlsl.empty())
        {
            error = "generated HLSL path が空です";
            return false;
        }

        const std::filesystem::path destination = asset.generated_hlsl.is_absolute()
            ? asset.generated_hlsl : project_root / asset.generated_hlsl;
        std::error_code ec;
        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) { error = "generated HLSL folder を作成できません"; return false; }

        std::filesystem::path temporary = destination;
        temporary += L".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) { error = "generated HLSL 一時ファイルを作成できません"; return false; }
        stream.write(generated.hlsl.data(), static_cast<std::streamsize>(generated.hlsl.size()));
        stream.flush();
        if (!stream)
        {
            stream.close(); std::filesystem::remove(temporary, ec);
            error = "generated HLSL の書き込みに失敗しました"; return false;
        }
        stream.close();
        return ReplaceAtomic(temporary, destination, error);
    }
}
