#include "ShaderAssetFactory.h"

#include "ShaderSource.h"

#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace ReplayEngine::Rendering
{
    namespace
    {
        std::string EscapePragmaString(const std::string& text)
        {
            std::string out;
            out.reserve(text.size());
            for (char c : text)
            {
                if (c == '\\' || c == '"') out.push_back('\\');
                if (c == '\r' || c == '\n') out.push_back(' ');
                else out.push_back(c);
            }
            return out;
        }

        bool ReplaceAtomic(const std::filesystem::path& temporary,
            const std::filesystem::path& destination, std::string& error)
        {
#ifdef _WIN32
            if (MoveFileExW(temporary.c_str(), destination.c_str(),
                MOVEFILE_WRITE_THROUGH) != FALSE)
            {
                return true;
            }
#endif
            std::error_code ec;
            std::filesystem::rename(temporary, destination, ec);
            if (!ec) return true;

            std::filesystem::remove(temporary, ec);
            error = "Shader Asset を確定できません: " +
                destination.generic_u8string();
            return false;
        }
    }

    bool ShaderAssetFactory::CreateSurfaceShader(
        const std::filesystem::path& path, const std::string& display_name,
        const std::string& category, ShaderID& out_id, std::string& error)
    {
        error.clear();
        out_id = {};

        if (path.empty())
        {
            error = "Shader Asset の保存先が空です";
            return false;
        }
        if (std::filesystem::exists(path))
        {
            error = "同名の Shader Asset が既にあります: " + path.generic_u8string();
            return false;
        }

        std::error_code ec;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "Shader Asset の保存フォルダを作成できません";
            return false;
        }

        const ShaderID id = ShaderSource::GenerateID();
        const std::string safe_name = EscapePragmaString(
            display_name.empty() ? path.stem().u8string() : display_name);
        const std::string safe_category = EscapePragmaString(
            category.empty() ? std::string("Project") : category);

        std::ostringstream source;
        source << "// RePlayEngine Surface Shader\n"
               << "// #pragma property を追加すると Material Inspector へ自動表示されます。\n\n"
               << "#pragma replay_guid     \"" << id.ToString() << "\"\n"
               << "#pragma replay_name     \"" << safe_name << "\"\n"
               << "#pragma replay_category \"" << safe_category << "\"\n"
               << "#pragma replay_domain   surface\n"
               << "#pragma replay_lighting unlit\n\n"
               << "#pragma property color   BaseColor   \"Base Color\" = (1, 1, 1, 1) category \"Surface\"\n"
               << "#pragma property texture BaseMap     \"Base Map\" default white category \"Surface\"\n"
               << "#pragma property range   AlphaCutoff \"Alpha Cutoff\" 0..1 = 0.5 category \"Rendering\"\n"
               << "#pragma property toggle  DoubleSided \"Double Sided\" = false category \"Rendering\"\n\n"
               << "// 最初は既存 Unlit 実装を利用する安全なテンプレート。\n"
               << "// 独自 HLSL へ発展させても replay_* / property 宣言はそのまま使えます。\n"
               << "#define REPLAY_MATERIAL_PROPERTIES 1\n"
               << "#if REPLAY_SKINNED\n"
               << "#include \"skinned_mesh_unlit_ps.hlsl\"\n"
               << "#else\n"
               << "#include \"static_mesh_unlit_ps.hlsl\"\n"
               << "#endif\n";

        std::filesystem::path temporary = path;
        temporary += L".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "Shader Asset 一時ファイルを作成できません";
                return false;
            }
            const std::string text = source.str();
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.flush();
            if (!stream)
            {
                stream.close();
                std::filesystem::remove(temporary, ec);
                error = "Shader Asset 一時ファイルへ書き込めません";
                return false;
            }
        }

        if (!ReplaceAtomic(temporary, path, error)) return false;
        out_id = id;
        return true;
    }
    bool ShaderAssetFactory::CreateLayerShader(
        const std::filesystem::path& path, const std::string& display_name,
        const std::string& category, ShaderID& out_id, std::string& error)
    {
        error.clear();
        out_id = {};
        if (path.empty())
        {
            error = "Layer Shader Asset の保存先が空です";
            return false;
        }
        if (std::filesystem::exists(path))
        {
            error = "同名の Layer Shader Asset が既にあります: " + path.generic_u8string();
            return false;
        }

        std::error_code ec;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "Layer Shader Asset の保存フォルダを作成できません";
            return false;
        }

        const ShaderID id = ShaderSource::GenerateID();
        const std::string safe_name = EscapePragmaString(
            display_name.empty() ? path.stem().u8string() : display_name);
        const std::string safe_category = EscapePragmaString(
            category.empty() ? std::string("Project") : category);

        std::ostringstream source;
        source << "// RePlayEngine Layer Shader\n"
               << "// Material > Shader Stack > Add Layer へ自動表示されます。\n"
               << "// property は b9 / t40+ と Inspector / Save の共通定義です。\n\n"
               << "#pragma replay_guid     \"" << id.ToString() << "\"\n"
               << "#pragma replay_name     \"" << safe_name << "\"\n"
               << "#pragma replay_category \"" << safe_category << "\"\n"
               << "#pragma replay_domain   layer\n"
               << "#pragma replay_lighting unlit\n\n"
               << "#pragma property color   Tint     \"Tint\" = (1, 1, 1, 1) category \"Layer\"\n"
               << "#pragma property texture BaseMap  \"Base Map\" default white category \"Layer\"\n"
               << "#pragma property range   Strength \"Strength\" 0..8 = 1 category \"Layer\"\n"
               << "#pragma property range   Opacity  \"Opacity\" 0..1 = 0.5 category \"Layer\"\n\n"
               << "struct REPLAY_LAYER_INPUT\n"
               << "{\n"
               << "    float4 position : SV_POSITION;\n"
               << "    float4 color : COLOR;\n"
               << "    float2 texcoord : TEXCOORD;\n"
               << "};\n"
               << "SamplerState replay_layer_sampler : register(s1);\n\n"
               << "float4 main(REPLAY_LAYER_INPUT pin) : SV_TARGET\n"
               << "{\n"
               << "    float4 color = BaseMap.Sample(replay_layer_sampler, pin.texcoord);\n"
               << "    color *= Tint * pin.color;\n"
               << "    color.rgb *= Strength;\n"
               << "    color.a *= Opacity;\n"
               << "    return color;\n"
               << "}\n";

        std::filesystem::path temporary = path;
        temporary += L".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "Layer Shader Asset 一時ファイルを作成できません";
                return false;
            }
            const std::string text = source.str();
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.flush();
            if (!stream)
            {
                stream.close();
                std::filesystem::remove(temporary, ec);
                error = "Layer Shader Asset 一時ファイルへ書き込めません";
                return false;
            }
        }
        if (!ReplaceAtomic(temporary, path, error)) return false;
        out_id = id;
        return true;
    }

}
