// Shader Source のうち、#pragma replay_* と property 宣言の解析だけを持つ。
//
//   ShaderSource.cpp      ... pragma / property parser（このファイル）
//   ShaderSourceFile.cpp  ... source file 読み込みと GUID 付与

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

    ShaderSource::ParseResult ShaderSource::ParseText(const std::string& text,
        const std::filesystem::path& source_path, bool& out_needs_guid)
    {
        ParseResult result;
        result.info.source_path = source_path;
        out_needs_guid = true;

        std::istringstream stream(text);
        std::string raw;
        int line_number = 0;
        bool lighting_directive_seen = false;

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

            if (directive == "replay_lighting")
            {
                if (lighting_directive_seen)
                {
                    result.info.lighting_model_valid = false;
                    result.issues.push_back({ line_number,
                        "replay_lighting が重複しています", true });
                    continue;
                }
                lighting_directive_seen = true;

                if (tokens.size() < 3)
                {
                    result.info.lighting_model_valid = false;
                    result.issues.push_back({ line_number,
                        "replay_lighting に値がありません", true });
                    continue;
                }

                ShaderLightingModel model = ShaderLightingModel::Pbr;
                if (!TryParseShaderLightingModel(tokens[2], model))
                {
                    // 既定値は PBR だが、不明な名前を PBR として通してはいけない。
                    // valid=false を残し、ShaderLibrary が Catalog 登録を止める。
                    result.info.lighting_model_valid = false;
                    result.issues.push_back({ line_number,
                        "replay_lighting が不明です: " + tokens[2], true });
                    continue;
                }

                result.info.lighting_model = model;
                result.info.lighting_model_valid = true;
                continue;
            }

            if (directive == "replay_pass")
            {
                // #pragma replay_pass "Display Name" EntryPoint [inherit|alpha|additive|multiply]
                // 宣言順をそのまま固定描画順として保存する。
                if (tokens.size() < 4)
                {
                    result.issues.push_back({ line_number,
                        "replay_pass は表示名と entry point が必要です", true });
                    continue;
                }

                ShaderPassInfo pass;
                pass.name = tokens[2];
                pass.entry_point = tokens[3];
                if (pass.name.empty() || pass.entry_point.empty() || pass.entry_point == "main")
                {
                    result.issues.push_back({ line_number,
                        "replay_pass の名前/entry point が不正です（main は base pass 専用）", true });
                    continue;
                }
                if (tokens.size() >= 5 &&
                    !TryParseShaderPassBlend(tokens[4], pass.blend))
                {
                    result.issues.push_back({ line_number,
                        "replay_pass blend が不明です: " + tokens[4], true });
                    continue;
                }

                bool duplicate = false;
                for (const ShaderPassInfo& existing : result.info.passes)
                {
                    if (existing.name == pass.name || existing.entry_point == pass.entry_point)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate)
                {
                    result.issues.push_back({ line_number,
                        "replay_pass の名前または entry point が重複しています", true });
                    continue;
                }
                result.info.passes.push_back(std::move(pass));
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
                if (token == "category")
                {
                    if (index + 1 < tokens.size())
                    {
                        property.category = tokens[index + 1];
                        ++index;
                    }
                    else
                    {
                        result.issues.push_back({ line_number,
                            "property category に値がありません" });
                    }
                    continue;
                }
                if (token == "tooltip")
                {
                    if (index + 1 < tokens.size())
                    {
                        property.tooltip = tokens[index + 1];
                        ++index;
                    }
                    else
                    {
                        result.issues.push_back({ line_number,
                            "property tooltip に値がありません" });
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

}
