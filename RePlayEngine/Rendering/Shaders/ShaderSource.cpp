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
            return std::string((std::istreambuf_iterator<char>(stream)),
                std::istreambuf_iterator<char>());
        }

        void TrimInPlace(std::string& text)
        {
            const auto not_space = [](unsigned char c) { return !std::isspace(c); };
            text.erase(text.begin(),
                std::find_if(text.begin(), text.end(), not_space));
            text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(),
                text.end());
        }

        // 1 行を空白区切りのトークンへ分ける。
        // ただし "..." は 1 トークン、(...) と {...} も 1 トークンとして扱う。
        //
        // 自前で切るのは、表示名に空白が入る（"基本色 (メイン)"）ことと、
        // 既定値が (1, 1, 1, 1) のように空白を含むため。
        std::vector<std::string> Tokenize(const std::string& line)
        {
            std::vector<std::string> tokens;
            std::size_t index = 0;
            const std::size_t size = line.size();

            while (index < size)
            {
                while (index < size &&
                    std::isspace(static_cast<unsigned char>(line[index]))) ++index;
                if (index >= size) break;

                const char open = line[index];
                if (open == '"')
                {
                    const std::size_t end = line.find('"', index + 1);
                    if (end == std::string::npos)
                    {
                        tokens.push_back(line.substr(index + 1));
                        break;
                    }
                    tokens.push_back(line.substr(index + 1, end - index - 1));
                    index = end + 1;
                    continue;
                }
                if (open == '(' || open == '{')
                {
                    const char close = (open == '(') ? ')' : '}';
                    const std::size_t end = line.find(close, index + 1);
                    if (end == std::string::npos)
                    {
                        tokens.push_back(line.substr(index));
                        break;
                    }
                    // 括弧ごと入れる。呼び出し側が中身を見る。
                    tokens.push_back(line.substr(index, end - index + 1));
                    index = end + 1;
                    continue;
                }

                std::size_t end = index;
                while (end < size &&
                    !std::isspace(static_cast<unsigned char>(line[end]))) ++end;
                tokens.push_back(line.substr(index, end - index));
                index = end;
            }
            return tokens;
        }

        // "(1, 0.5, 0, 1)" や "1, 0.5" を float の並びへ。
        std::vector<float> ParseFloatList(std::string text)
        {
            if (!text.empty() && (text.front() == '(' || text.front() == '{'))
            {
                text = text.substr(1, text.size() >= 2 ? text.size() - 2 : 0);
            }
            for (char& c : text) if (c == ',') c = ' ';

            std::vector<float> values;
            std::istringstream stream(text);
            float value = 0.0f;
            while (stream >> value) values.push_back(value);
            return values;
        }

        // "{ Off, Front, Back }" を名前の並びへ。
        std::vector<std::string> ParseNameList(std::string text)
        {
            if (!text.empty() && (text.front() == '{' || text.front() == '('))
            {
                text = text.substr(1, text.size() >= 2 ? text.size() - 2 : 0);
            }
            for (char& c : text) if (c == ',') c = '\n';

            std::vector<std::string> names;
            std::istringstream stream(text);
            std::string name;
            while (std::getline(stream, name))
            {
                TrimInPlace(name);
                if (!name.empty()) names.push_back(name);
            }
            return names;
        }

        // "0..1" を最小・最大へ。
        bool ParseRange(const std::string& text, float& out_min, float& out_max)
        {
            const std::size_t dots = text.find("..");
            if (dots == std::string::npos) return false;
            try
            {
                out_min = std::stof(text.substr(0, dots));
                out_max = std::stof(text.substr(dots + 2));
            }
            catch (...)
            {
                return false;
            }
            return true;
        }

        void ApplyDefaultValue(ShaderProperty& property, const std::string& text)
        {
            if (property.kind == ShaderPropertyKind::Toggle)
            {
                const bool on = (text == "true" || text == "1" || text == "on");
                property.default_value = DirectX::XMFLOAT4{
                    on ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
                return;
            }
            if (property.kind == ShaderPropertyKind::Enum)
            {
                // 名前でも番号でも受ける。
                for (std::size_t index = 0; index < property.enum_names.size(); ++index)
                {
                    if (property.enum_names[index] == text)
                    {
                        property.default_value = DirectX::XMFLOAT4{
                            static_cast<float>(index), 0.0f, 0.0f, 0.0f };
                        return;
                    }
                }
                try
                {
                    property.default_value = DirectX::XMFLOAT4{
                        std::stof(text), 0.0f, 0.0f, 0.0f };
                }
                catch (...)
                {
                    property.default_value = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f };
                }
                return;
            }

            const std::vector<float> values = ParseFloatList(text);
            DirectX::XMFLOAT4 result{ 0.0f, 0.0f, 0.0f, 0.0f };
            if (values.size() > 0) result.x = values[0];
            if (values.size() > 1) result.y = values[1];
            if (values.size() > 2) result.z = values[2];
            if (values.size() > 3) result.w = values[3];

            // Color と Float4 は alpha の既定を 1 にする。
            // 3 要素しか書かれていないとき 0 だと透明になってしまう。
            if (values.size() == 3 &&
                (property.kind == ShaderPropertyKind::Color ||
                 property.kind == ShaderPropertyKind::Float4))
            {
                result.w = 1.0f;
            }
            property.default_value = result;
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

    ShaderSource::ParseResult ShaderSource::ParseText(const std::string& text,
        const std::filesystem::path& source_path, bool& out_needs_guid)
    {
        ParseResult result;
        result.info.source_path = source_path;
        out_needs_guid = true;

        std::istringstream stream(text);
        std::string raw;
        int line_number = 0;

        while (std::getline(stream, raw))
        {
            ++line_number;
            while (!raw.empty() && (raw.back() == '\r' || raw.back() == '\n'))
            {
                raw.pop_back();
            }

            std::string line = raw;
            TrimInPlace(line);
            if (line.rfind("#pragma", 0) != 0) continue;

            const std::vector<std::string> tokens = Tokenize(line);
            // tokens[0] == "#pragma"
            if (tokens.size() < 2) continue;
            const std::string& directive = tokens[1];

            if (directive == "replay_guid")
            {
                if (tokens.size() < 3)
                {
                    result.issues.push_back({ line_number, "replay_guid に値がありません" });
                    continue;
                }
                ShaderID parsed;
                if (!ShaderID::TryParse(tokens[2], parsed) || !parsed.IsValid())
                {
                    result.issues.push_back({ line_number,
                        "replay_guid を解析できません: " + tokens[2] });
                    continue;
                }
                result.info.id = parsed;
                out_needs_guid = false;
                continue;
            }

            if (directive == "replay_name")
            {
                if (tokens.size() >= 3) result.info.name = tokens[2];
                continue;
            }

            if (directive == "replay_category")
            {
                if (tokens.size() >= 3) result.info.category = tokens[2];
                continue;
            }

            if (directive == "replay_domain")
            {
                if (tokens.size() < 3) continue;
                ShaderDomain domain = ShaderDomain::Surface;
                if (!TryParseShaderDomain(tokens[2], domain))
                {
                    result.issues.push_back({ line_number,
                        "replay_domain が不明です: " + tokens[2] });
                    continue;
                }
                result.info.domain = domain;
                continue;
            }

            if (directive != "property") continue;

            // #pragma property <kind> <name> ["表示名"] [range] [{enum}] [= 既定]
            if (tokens.size() < 4)
            {
                result.issues.push_back({ line_number,
                    "property の書式が足りません（kind と name が必要）" });
                continue;
            }

            ShaderProperty property;
            if (!TryParseShaderPropertyKind(tokens[2], property.kind))
            {
                result.issues.push_back({ line_number,
                    "property の型が不明です: " + tokens[2] });
                continue;
            }
            property.name = tokens[3];
            if (property.name.empty())
            {
                result.issues.push_back({ line_number, "property の名前が空です" });
                continue;
            }

            // 4 番目以降は順不同で受ける。
            // 書き順を強制すると、書き間違えたときの原因が分かりにくい。
            for (std::size_t index = 4; index < tokens.size(); ++index)
            {
                const std::string& token = tokens[index];

                if (token == "=")
                {
                    if (index + 1 < tokens.size())
                    {
                        ApplyDefaultValue(property, tokens[index + 1]);
                        ++index;
                    }
                    continue;
                }
                if (token == "default")
                {
                    if (index + 1 < tokens.size())
                    {
                        property.default_texture = tokens[index + 1];
                        ++index;
                    }
                    continue;
                }
                if (!token.empty() && token.front() == '{')
                {
                    property.enum_names = ParseNameList(token);
                    continue;
                }
                if (token.find("..") != std::string::npos)
                {
                    float minimum = 0.0f;
                    float maximum = 1.0f;
                    if (ParseRange(token, minimum, maximum))
                    {
                        property.minimum = minimum;
                        property.maximum = maximum;
                    }
                    else
                    {
                        result.issues.push_back({ line_number,
                            "範囲指定を解析できません: " + token });
                    }
                    continue;
                }

                // 残りは表示名。最初の 1 つだけ採る。
                if (property.display_name.empty()) property.display_name = token;
            }

            // Texture の既定が未指定なら白にする。
            // 未指定のまま黒だと「テクスチャを付け忘れると真っ黒」になり、
            // 設定漏れなのか意図なのか区別できない。
            if (property.kind == ShaderPropertyKind::Texture &&
                property.default_texture.empty())
            {
                property.default_texture = "white";
            }

            // 同名を弾く。同名があると値の対応が壊れる。
            const auto duplicate = std::find_if(
                result.info.properties.begin(), result.info.properties.end(),
                [&property](const ShaderProperty& existing)
                {
                    return existing.name == property.name;
                });
            if (duplicate != result.info.properties.end())
            {
                result.issues.push_back({ line_number,
                    "property の名前が重複しています: " + property.name });
                continue;
            }

            result.info.properties.push_back(std::move(property));
        }

        result.succeeded = true;
        return result;
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
