#pragma once

#include "CSharpScriptBackend.h"
#include "CSharpProjectInternal.h"

#include "../Core/ScriptValue.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Runtime/Core/RuntimeResult.h"
#include "../../Runtime/Events/EventBus.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <deque>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ReplayEngine::Scripting::CSharp::Detail
{
        inline std::string ToUtf8(const std::wstring& text)
        {
            if (text.empty()) return std::string();
            const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0) return std::string();
            std::string result(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                result.data(), size, nullptr, nullptr);
            return result;
        }

        inline std::vector<char> Utf8CString(const std::filesystem::path& path)
        {
            std::string text = path.generic_u8string();
            text.push_back('\0');
            return std::vector<char>(text.begin(), text.end());
        }

        inline std::vector<char> Utf8CString(const std::string& text)
        {
            std::vector<char> result(text.begin(), text.end());
            result.push_back('\0');
            return result;
        }

        inline std::filesystem::path FindHostFxr()
        {
            std::vector<std::filesystem::path> roots;
#ifdef _WIN32
            wchar_t buffer[MAX_PATH]{};
            const DWORD env_size = GetEnvironmentVariableW(L"DOTNET_ROOT",
                buffer, static_cast<DWORD>(std::size(buffer)));
            if (env_size > 0 && env_size < std::size(buffer))
            {
                roots.emplace_back(buffer);
            }
            roots.emplace_back(L"C:\\Program Files\\dotnet");
            roots.emplace_back(L"C:\\Program Files (x86)\\dotnet");
#endif

            std::filesystem::path best;
            for (const std::filesystem::path& root : roots)
            {
                const std::filesystem::path fxr = root / "host" / "fxr";
                std::error_code error;
                if (!std::filesystem::exists(fxr, error) || error) continue;

                for (std::filesystem::directory_iterator it(fxr, error), end;
                    !error && it != end; it.increment(error))
                {
                    if (!it->is_directory()) continue;
                    const std::filesystem::path candidate =
                        it->path() / "hostfxr.dll";
                    if (!std::filesystem::exists(candidate, error) || error) continue;
                    if (best.empty() || it->path().filename().wstring() >
                        best.parent_path().filename().wstring())
                    {
                        best = candidate;
                    }
                }
            }
            return best;
        }

        inline std::string ReadOutputBuffer(const std::array<char, text_buffer_size>& buffer)
        {
            return std::string(buffer.data());
        }

        inline int HexValue(char c) noexcept
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        inline std::string Unescape(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '%' && index + 2 < text.size())
                {
                    const int high = HexValue(text[index + 1]);
                    const int low = HexValue(text[index + 2]);
                    if (high >= 0 && low >= 0)
                    {
                        result.push_back(static_cast<char>((high << 4) | low));
                        index += 2;
                        continue;
                    }
                }
                result.push_back(text[index]);
            }
            return result;
        }

        inline std::vector<std::string> Split(std::string_view text, char separator)
        {
            std::vector<std::string> result;
            std::size_t start = 0;
            while (start <= text.size())
            {
                const std::size_t next = text.find(separator, start);
                const std::size_t count = next == std::string_view::npos
                    ? text.size() - start
                    : next - start;
                result.emplace_back(text.substr(start, count));
                if (next == std::string_view::npos) break;
                start = next + 1;
            }
            return result;
        }

        inline bool MapManagedFieldType(const std::string& text, ScriptValueType& out)
        {
            using Reflection::PropertyType;
            if (text == "bool") { out = PropertyType::Bool; return true; }
            if (text == "int") { out = PropertyType::Int; return true; }
            if (text == "int64") { out = PropertyType::Int64; return true; }
            if (text == "uint64") { out = PropertyType::UInt64; return true; }
            if (text == "float") { out = PropertyType::Float; return true; }
            if (text == "double") { out = PropertyType::Double; return true; }
            if (text == "string") { out = PropertyType::String; return true; }
            if (text == "vector2" || text == "vec2") { out = PropertyType::Vector2; return true; }
            if (text == "vector3" || text == "vec3") { out = PropertyType::Vector3; return true; }
            if (text == "vector4" || text == "vec4") { out = PropertyType::Vector4; return true; }
            if (text == "quaternion" || text == "quat") { out = PropertyType::Quaternion; return true; }
            if (text == "color") { out = PropertyType::Color; return true; }
            if (text == "object") { out = PropertyType::ObjectReference; return true; }
            if (text == "component") { out = PropertyType::ComponentReference; return true; }
            if (text == "asset") { out = PropertyType::AssetReference; return true; }
            if (text == "enum") { out = PropertyType::Enum; return true; }
            return false;
        }

        inline double ParseDouble(const std::string& text, double fallback = 0.0)
        {
            char* end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            return end != text.c_str() ? value : fallback;
        }

        inline std::uint64_t ParseUInt64(const std::string& text,
            std::uint64_t fallback = 0)
        {
            char* end = nullptr;
            const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
            return end != text.c_str() ? static_cast<std::uint64_t>(value) : fallback;
        }

        inline std::int64_t ParseInt64(const std::string& text,
            std::int64_t fallback = 0)
        {
            char* end = nullptr;
            const long long value = std::strtoll(text.c_str(), &end, 10);
            return end != text.c_str() ? static_cast<std::int64_t>(value) : fallback;
        }

        inline ScriptValue ParseValue(ScriptValueType type, const std::string& text)
        {
            using Reflection::ComponentReference;
            using Reflection::PropertyType;

            const std::vector<std::string> parts = Split(text, ',');
            const auto part = [&parts](std::size_t index) -> std::string
            {
                return index < parts.size() ? parts[index] : std::string();
            };
            const auto f = [&part](std::size_t index) -> float
            {
                return static_cast<float>(ParseDouble(part(index)));
            };

            switch (type)
            {
            case PropertyType::Bool:
            {
                std::string lower = text;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return ScriptValue::MakeBool(lower == "true" || lower == "1");
            }
            case PropertyType::Int:
                return ScriptValue::MakeInt(static_cast<int>(ParseInt64(text)));
            case PropertyType::Int64:
                return ScriptValue::MakeInt64(ParseInt64(text));
            case PropertyType::UInt64:
                return ScriptValue::MakeUInt64(ParseUInt64(text));
            case PropertyType::Float:
                return ScriptValue::MakeFloat(static_cast<float>(ParseDouble(text)));
            case PropertyType::Double:
                return ScriptValue::MakeDouble(ParseDouble(text));
            case PropertyType::String:
                return ScriptValue::MakeString(text);
            case PropertyType::Vector2:
                return ScriptValue::MakeVector2({ f(0), f(1) });
            case PropertyType::Vector3:
                return ScriptValue::MakeVector3({ f(0), f(1), f(2) });
            case PropertyType::Vector4:
                return ScriptValue::MakeVector4({ f(0), f(1), f(2), f(3) });
            case PropertyType::Quaternion:
                return ScriptValue::MakeQuaternion({ f(0), f(1), f(2), f(3) });
            case PropertyType::Color:
                return ScriptValue::MakeColor({ f(0), f(1), f(2), f(3) });
            case PropertyType::Enum:
                return ScriptValue::MakeEnum(static_cast<int>(ParseInt64(text)));
            case PropertyType::AssetReference:
                return ScriptValue::MakeAssetReference(text);
            case PropertyType::ObjectReference:
                return ScriptValue::MakeObjectReference(Core::ObjectID(ParseUInt64(text)));
            case PropertyType::ComponentReference:
            {
                ComponentReference reference;
                reference.owner = Core::ObjectID(ParseUInt64(part(0)));
                reference.component =
                    static_cast<Core::ComponentStableID>(ParseUInt64(part(1)));
                return ScriptValue::MakeComponentReference(reference);
            }
            default:
                return ScriptFieldSchema::MakeTypeDefault(type);
            }
        }

        inline std::string FloatText(float value)
        {
            std::ostringstream stream;
            stream.precision(9);
            stream << value;
            return stream.str();
        }

        inline std::string DoubleText(double value)
        {
            std::ostringstream stream;
            stream.precision(17);
            stream << value;
            return stream.str();
        }

        inline std::string ValueToText(const ScriptValue& value)
        {
            using Reflection::PropertyType;

            switch (value.Type())
            {
            case PropertyType::Bool:
                return value.AsBool() ? "true" : "false";
            case PropertyType::Int:
                return std::to_string(value.AsInt());
            case PropertyType::Int64:
                return std::to_string(value.AsInt64());
            case PropertyType::UInt64:
                return std::to_string(value.AsUInt64());
            case PropertyType::Float:
                return FloatText(value.AsFloat());
            case PropertyType::Double:
                return DoubleText(value.AsDouble());
            case PropertyType::String:
                return value.AsString();
            case PropertyType::Vector2:
            {
                const DirectX::XMFLOAT2 v = value.AsVector2();
                return FloatText(v.x) + "," + FloatText(v.y);
            }
            case PropertyType::Vector3:
            {
                const DirectX::XMFLOAT3 v = value.AsVector3();
                return FloatText(v.x) + "," + FloatText(v.y) + "," + FloatText(v.z);
            }
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
            case PropertyType::Color:
            {
                const DirectX::XMFLOAT4 v = value.AsVector4();
                return FloatText(v.x) + "," + FloatText(v.y) + "," +
                    FloatText(v.z) + "," + FloatText(v.w);
            }
            case PropertyType::ObjectReference:
                return std::to_string(value.AsObjectReference().Value());
            case PropertyType::ComponentReference:
            {
                const Reflection::ComponentReference reference =
                    value.AsComponentReference();
                return std::to_string(reference.owner.Value()) + "," +
                    std::to_string(reference.component);
            }
            default:
                return std::string();
            }
        }

        inline bool ParseSchemaText(ScriptTypeID type_id, std::uint32_t revision,
            const std::string& text, ScriptFieldSchemaRef& out_schema,
            std::unordered_map<std::string, ScriptValueType>& out_field_types,
            std::string& error)
        {
            std::vector<ScriptFieldDefinition> fields;
            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line))
            {
                if (line.empty()) continue;
                const std::vector<std::string> parts = Split(line, '\t');
                if (parts.size() < 6 || parts[0] != "FIELD") continue;

                ScriptValueType type = Reflection::PropertyType::Float;
                if (!MapManagedFieldType(parts[2], type))
                {
                    error = "Unsupported C# field type: " + parts[2];
                    return false;
                }

                ScriptFieldDefinition definition =
                    ScriptFieldDefinition::Make(Unescape(parts[1]), type);
                definition.display_name = Unescape(parts[3]);
                definition.tooltip = Unescape(parts[4]);
                definition.default_value = ParseValue(type, Unescape(parts[5]));

                // 7 列目以降は後から足した追加情報。無い古い形式でもそのまま読める。
                if (parts.size() > 6)
                {
                    const std::string flags = Unescape(parts[6]);
                    if (flags.find('h') != std::string::npos)
                        definition.visible_in_inspector = false;
                    if (flags.find('r') != std::string::npos)
                        definition.read_only = true;
                }
                if (parts.size() > 8 && !parts[7].empty() && !parts[8].empty())
                {
                    definition.has_range = true;
                    definition.minimum = ParseDouble(parts[7]);
                    definition.maximum = ParseDouble(parts[8]);
                }
                if (parts.size() > 9) definition.asset_type = Unescape(parts[9]);
                if (parts.size() > 10) definition.category = Unescape(parts[10]);
                if (parts.size() > 11)
                {
                    const std::string labels = Unescape(parts[11]);
                    if (!labels.empty()) definition.enum_labels = Split(labels, ',');
                }

                out_field_types[definition.SavedName()] = type;
                fields.push_back(std::move(definition));
            }

            std::vector<std::string> rejected;
            out_schema = ScriptFieldSchema::Build(type_id, revision,
                std::move(fields), &rejected);
            if (!rejected.empty())
            {
                error = rejected.front();
            }
            return true;
        }

        inline void* ProcAddress(void* module, const char* name)
        {
#ifdef _WIN32
            return reinterpret_cast<void*>(GetProcAddress(
                static_cast<HMODULE>(module), name));
#else
            (void)module;
            (void)name;
            return nullptr;
#endif
        }

        inline bool FileExists(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && !error;
        }

        inline std::filesystem::path ManagedApiAssemblyPathForMode(
            const std::filesystem::path& root, bool packaged_mode)
        {
            if (!packaged_mode) return CSharpProject::ManagedApiAssemblyPath(root);
            const std::filesystem::path release =
                CSharpProject::ManagedApiAssemblyPath(root, "Release");
            if (FileExists(release)) return release;
            const std::filesystem::path debug =
                CSharpProject::ManagedApiAssemblyPath(root, "Debug");
            return FileExists(debug) ? debug : release;
        }

        inline std::filesystem::path ManagedApiRuntimeConfigPathForMode(
            const std::filesystem::path& root, bool packaged_mode)
        {
            if (!packaged_mode) return CSharpProject::ManagedApiRuntimeConfigPath(root);
            const std::filesystem::path release =
                CSharpProject::ManagedApiRuntimeConfigPath(root, "Release");
            if (FileExists(release)) return release;
            const std::filesystem::path debug =
                CSharpProject::ManagedApiRuntimeConfigPath(root, "Debug");
            return FileExists(debug) ? debug : release;
        }

        inline std::filesystem::path GameScriptsAssemblyPathForMode(
            const std::filesystem::path& root, bool packaged_mode)
        {
            if (!packaged_mode) return CSharpProject::GameScriptsAssemblyPath(root);
            const std::filesystem::path release =
                CSharpProject::GameScriptsAssemblyPath(root, "Release");
            if (FileExists(release)) return release;
            const std::filesystem::path debug =
                CSharpProject::GameScriptsAssemblyPath(root, "Debug");
            return FileExists(debug) ? debug : release;
        }
}
