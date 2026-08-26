#include "ShaderSource.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <system_error>

namespace ReplayEngine::Rendering
{
    namespace
    {
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

    ShaderID ShaderSource::GenerateID()
    {
        // CSharpProject::GenerateTypeGuid と同じ考え方。
        // 乱数だけだと同一プロセス内で近い値が出るので時刻を混ぜる。
        std::random_device device;
        const auto now = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());

        ShaderID id;
        id.high = (static_cast<std::uint64_t>(device()) << 32) ^ device() ^ now;
        id.low = (static_cast<std::uint64_t>(device()) << 32) ^ device() ^ (now >> 7);

        // 万一 0 になったら作り直す。0 は無効値。
        if (!id.IsValid()) id.high = 1;
        return id;
    }

    ShaderSource::ParseResult ShaderSource::ParseFile(
        const std::filesystem::path& path, bool& out_needs_guid)
    {
        const std::string text = ReadAllText(path);
        if (text.empty())
        {
            ParseResult result;
            result.info.source_path = path;
            result.succeeded = false;
            result.issues.push_back({ 0,
                "ファイルを読めないか空です: " + path.generic_u8string() });
            out_needs_guid = false;
            return result;
        }
        return ParseText(text, path, out_needs_guid);
    }

    bool ShaderSource::AssignGuid(const std::filesystem::path& path,
        ShaderID& out_id, std::string& error)
    {
        error.clear();

        const std::string original = ReadAllText(path);
        if (original.empty())
        {
            error = "ファイルを読めないか空です: " + path.generic_u8string();
            return false;
        }

        // 既に持っているなら振らない。
        //
        // 一度振った GUID を書き換えると、そのシェーダを使っている
        // 全マテリアルの参照が切れる。ここは二重の安全弁。
        bool needs_guid = true;
        const ParseResult existing = ParseText(original, path, needs_guid);
        if (!needs_guid && existing.info.id.IsValid())
        {
            out_id = existing.info.id;
            return true;
        }

        const ShaderID id = GenerateID();

        std::ostringstream rewritten;
        rewritten << "#pragma replay_guid \"" << id.ToString() << "\"\n";
        rewritten << original;

        // 一時ファイルへ書いてから差し替える。
        // 直接上書きすると、書き込み中に落ちたときソースが失われる。
        std::filesystem::path temporary = path;
        temporary += L".tmp";

        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                error = "一時ファイルを作成できません: " + temporary.generic_u8string();
                return false;
            }
            const std::string text = rewritten.str();
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!stream)
            {
                error = "一時ファイルへ書き込めません";
                return false;
            }
        }

        std::error_code filesystem_error;
        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            // rename が失敗する環境向けに copy + remove で再試行する。
            std::filesystem::copy_file(temporary, path,
                std::filesystem::copy_options::overwrite_existing, filesystem_error);
            if (filesystem_error)
            {
                error = "GUID を書き戻せません: " + path.generic_u8string();
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }

        out_id = id;
        return true;
    }
}
