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
        struct Value final
        {
            ShaderComposerValueType type = ShaderComposerValueType::Invalid;
            std::string expression;
            bool valid = false;
        };

        int Components(ShaderComposerValueType type)
        {
            switch (type)
            {
            case ShaderComposerValueType::Float: return 1;
            case ShaderComposerValueType::Float2: return 2;
            case ShaderComposerValueType::Float3: return 3;
            case ShaderComposerValueType::Float4: return 4;
            default: return 0;
            }
        }

        const char* TypeName(ShaderComposerValueType type)
        {
            switch (type)
            {
            case ShaderComposerValueType::Float: return "float";
            case ShaderComposerValueType::Float2: return "float2";
            case ShaderComposerValueType::Float3: return "float3";
            case ShaderComposerValueType::Float4: return "float4";
            default: return "float";
            }
        }

        std::string FloatLiteral(float value)
        {
            if (!std::isfinite(value)) value = 0.0f;
            std::ostringstream stream;
            stream << std::setprecision(9) << value;
            std::string text = stream.str();
            if (text.find_first_of(".eE") == std::string::npos) text += ".0";
            return text + "f";
        }

        std::string Escape(const std::string& text)
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

        bool IsIdentifier(const std::string& name)
        {
            if (name.empty()) return false;
            const unsigned char first = static_cast<unsigned char>(name[0]);
            if (!(std::isalpha(first) || name[0] == '_')) return false;
            for (char c : name)
            {
                const unsigned char u = static_cast<unsigned char>(c);
                if (!(std::isalnum(u) || c == '_')) return false;
            }
            return true;
        }

        std::string Convert(const Value& value, ShaderComposerValueType target)
        {
            if (!value.valid) return {};
            if (value.type == target) return value.expression;
            const int source = Components(value.type);
            const int destination = Components(target);
            if (source == 1 && destination > 1)
            {
                // FXC (D3DCompile / Shader Model 5) では float4(x) のような
                // 1 引数 vector constructor を scalar splat として扱えない。
                // 明示的に必要な成分数だけ複製して、DX11/FXC でも確実に通す。
                std::ostringstream stream;
                stream << TypeName(target) << '(';
                for (int component = 0; component < destination; ++component)
                {
                    if (component != 0) stream << ", ";
                    stream << value.expression;
                }
                stream << ')';
                return stream.str();
            }
            if (destination == 1)
                return "(" + value.expression + ").x";
            if (source == 4 && destination == 3) return "(" + value.expression + ").xyz";
            if (source >= 3 && destination == 2) return "(" + value.expression + ").xy";
            if (source == 2 && destination == 3) return "float3(" + value.expression + ", 0.0f)";
            if (source == 2 && destination == 4) return "float4(" + value.expression + ", 0.0f, 1.0f)";
            if (source == 3 && destination == 4) return "float4(" + value.expression + ", 1.0f)";
            return {};
        }

        ShaderComposerValueType MergeTypes(ShaderComposerValueType a,
            ShaderComposerValueType b)
        {
            if (a == ShaderComposerValueType::Invalid) return b;
            if (b == ShaderComposerValueType::Invalid) return a;
            if (a == b) return a;
            if (a == ShaderComposerValueType::Float) return b;
            if (b == ShaderComposerValueType::Float) return a;
            return ShaderComposerValueType::Invalid;
        }

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

        class Generator final
        {
        public:
            explicit Generator(const ShaderComposerAsset& source)
                : asset(source)
            {
                for (const ShaderComposerConnection& edge : asset.connections)
                {
                    const std::uint64_t key = (edge.to_node << 16) ^ edge.to_pin;
                    inputs[key] = &edge;
                }
            }

            ShaderComposerGenerateResult Run()
            {
                if (!asset.shader_id.IsValid())
                    AddError(0, "ShaderGUID がありません");
                if (asset.domain != ShaderDomain::Surface &&
                    asset.domain != ShaderDomain::Layer &&
                    asset.domain != ShaderDomain::PostProcess)
                    AddError(0, "Composer v1 は Surface / Layer のみ対応です");
                if (asset.lighting_model != ShaderLightingModel::Unlit)
                    AddError(0, "Composer v1 の custom output は Unlit のみ対応です。PBR/Toon graph output は次世代で追加します");

                std::unordered_set<std::string> property_names;
                for (const ShaderComposerNode& node : asset.nodes)
                {
                    if (node.kind == ShaderComposerNodeKind::FloatProperty ||
                        node.kind == ShaderComposerNodeKind::ColorProperty ||
                        node.kind == ShaderComposerNodeKind::TextureProperty)
                    {
                        if (!IsIdentifier(node.name))
                            AddError(node.id, "Property name は HLSL identifier が必要です: " + node.name);
                        else if (!property_names.insert(node.name).second)
                            AddError(node.id, "Property name が重複しています: " + node.name);
                    }
                }

                const ShaderComposerNodeKind required = asset.domain == ShaderDomain::Layer
                    ? ShaderComposerNodeKind::LayerOutput : ShaderComposerNodeKind::SurfaceOutput;
                const ShaderComposerNode* output = nullptr;
                for (const ShaderComposerNode& node : asset.nodes)
                {
                    if (node.kind != required) continue;
                    if (output != nullptr)
                    {
                        AddError(node.id, "Output node は graph に 1 つだけ置けます");
                    }
                    else output = &node;
                }
                if (output == nullptr) AddError(0, "Output node がありません");
                if (!diagnostics.empty()) return Finish(false, {});

                std::ostringstream header;
                header << "// Generated by RePlayEngine Shader Composer. DO NOT EDIT BY HAND.\n"
                    << "// Source graph is the .replayshadergraph asset.\n\n"
                    // Generated HLSL is also opened directly in Visual Studio.  Runtime
                    // compilation prepends the schema declaration, but IntelliSense does not.
                    // Disable warnings for our custom pragmas in the standalone file too.
                    << "#pragma warning(disable: 3568)\n"
                    << "#pragma replay_guid     \"" << asset.shader_id.ToString() << "\"\n"
                    << "#pragma replay_name     \"" << Escape(asset.display_name) << "\"\n"
                    << "#pragma replay_category \"" << Escape(asset.category) << "\"\n"
                    << "#pragma replay_domain   " << ToString(asset.domain) << "\n"
                    << "#pragma replay_lighting " << ToString(asset.lighting_model) << "\n";

                for (const ShaderComposerNode& node : asset.nodes)
                {
                    const std::string display = Escape(node.display_name.empty() ? node.name : node.display_name);
                    const std::string category = Escape(node.category.empty() ? "Composer" : node.category);
                    const std::string tooltip = Escape(node.tooltip);
                    if (node.kind == ShaderComposerNodeKind::FloatProperty)
                    {
                        header << "#pragma property range " << node.name << " \"" << display << "\" "
                            << FloatLiteral(node.minimum) << ".." << FloatLiteral(node.maximum)
                            << " = " << FloatLiteral(node.value) << " category \"" << category << "\"";
                        if (!tooltip.empty()) header << " tooltip \"" << tooltip << "\"";
                        header << "\n";
                    }
                    else if (node.kind == ShaderComposerNodeKind::ColorProperty)
                    {
                        header << "#pragma property color " << node.name << " \"" << display << "\" = ("
                            << FloatLiteral(node.color.x) << ", " << FloatLiteral(node.color.y) << ", "
                            << FloatLiteral(node.color.z) << ", " << FloatLiteral(node.color.w)
                            << ") category \"" << category << "\"";
                        if (!tooltip.empty()) header << " tooltip \"" << tooltip << "\"";
                        header << "\n";
                    }
                    else if (node.kind == ShaderComposerNodeKind::TextureProperty)
                    {
                        header << "#pragma property texture " << node.name << " \"" << display
                            << "\" default " << (node.default_texture.empty() ? "white" : node.default_texture)
                            << " category \"" << category << "\"";
                        if (!tooltip.empty()) header << " tooltip \"" << tooltip << "\"";
                        header << "\n";
                    }
                }

                if (asset.domain == ShaderDomain::PostProcess)
                {
                    header << "\nTexture2D source_texture : register(t0);\n"
                        << "SamplerState source_sampler : register(s0);\n"
                        << "SamplerState replay_composer_sampler : register(s0);\n"
                        << "cbuffer UIEffectConstants : register(b0)\n{\n"
                        << "    float4 effect_color;\n"
                        << "    float4 effect_params0;\n"
                        << "    float4 effect_params1;\n"
                        << "    float4 effect_params2;\n"
                        << "    float4 target_size;\n"
                        << "};\n"
                        << "struct VSOutput\n{\n"
                        << "    float4 position : SV_POSITION;\n"
                        << "    float2 texcoord : TEXCOORD0;\n"
                        << "};\n"
                        << "#define frame_params float4(0.0f, 0.0f, effect_params1.w, 0.0f)\n";

                    const char* float_slots[] = {
                        "effect_params0.x", "effect_params0.y",
                        "effect_params0.z", "effect_params0.w",
                        "effect_params1.x", "effect_params1.y",
                        "effect_params1.z", "effect_params1.w"
                    };
                    int float_slot = 0;
                    for (const ShaderComposerNode& node : asset.nodes)
                    {
                        if (node.kind == ShaderComposerNodeKind::FloatProperty)
                        {
                            const int slot = float_slot++;
                            header << "#define " << node.name << ' '
                                << (slot < static_cast<int>(sizeof(float_slots) / sizeof(float_slots[0]))
                                    ? float_slots[slot] : "0.0f") << "\n";
                        }
                        else if (node.kind == ShaderComposerNodeKind::ColorProperty)
                        {
                            header << "#define " << node.name << " effect_color\n";
                        }
                        else if (node.kind == ShaderComposerNodeKind::TextureProperty)
                        {
                            header << "#define " << node.name << " source_texture\n";
                        }
                    }

                    header << "\nfloat replay_composer_noise(float2 p)\n{\n"
                        << "    p = frac(p * float2(123.34f, 456.21f));\n"
                        << "    p += dot(p, p + 45.32f);\n"
                        << "    return frac(p.x * p.y);\n}\n\n";
                }
                else
                {
                    // ShaderLibrary prepends the canonical b9/t40+ declaration at runtime.
                    // A generated file opened (or compiled) by itself has no such prefix, so emit
                    // an equivalent fallback block.  The runtime prefix defines the guard below
                    // and therefore remains the single source of truth during actual rendering.
                    header << "\n#ifndef REPLAY_MATERIAL_SCHEMA_INJECTED\n";
                    bool has_constant_property = false;
                    for (const ShaderComposerNode& node : asset.nodes)
                    {
                        if (node.kind == ShaderComposerNodeKind::FloatProperty ||
                            node.kind == ShaderComposerNodeKind::ColorProperty)
                        {
                            has_constant_property = true;
                            break;
                        }
                    }
                    header << "cbuffer REPLAY_MATERIAL_CB : register(b"
                        << ShaderConstantPacker::material_constant_register << ")\n{\n";
                    if (!has_constant_property)
                        header << "    float4 _replay_unused;\n";
                    for (const ShaderComposerNode& node : asset.nodes)
                    {
                        if (node.kind == ShaderComposerNodeKind::FloatProperty)
                            header << "    float " << node.name << ";\n";
                        else if (node.kind == ShaderComposerNodeKind::ColorProperty)
                            header << "    float4 " << node.name << ";\n";
                    }
                    header << "};\n";
                    std::uint32_t texture_slot = ShaderConstantPacker::material_texture_base_slot;
                    for (const ShaderComposerNode& node : asset.nodes)
                    {
                        if (node.kind != ShaderComposerNodeKind::TextureProperty) continue;
                        header << "Texture2D " << node.name << " : register(t"
                            << texture_slot++ << ");\n";
                    }
                    header << "#endif // REPLAY_MATERIAL_SCHEMA_INJECTED\n\n";

                    // Composer outputs live under Shader/Materials/Generated or
                    // Shader/Layers/Generated.  Relative includes make the generated source
                    // self-contained for Visual Studio/FXC instead of relying only on the
                    // engine's custom runtime include search paths.
                    header << "#if REPLAY_SKINNED\n"
                        << "#include \"../../skinned_mesh.hlsli\"\n"
                        << "#else\n"
                        << "#include \"../../static_mesh.hlsli\"\n"
                        << "#endif\n"
                        << "#include \"../../frame_common.hlsli\"\n\n"
                        << "SamplerState replay_composer_sampler : register(s1);\n"
                        << "float replay_composer_noise(float2 p)\n{\n"
                        << "    p = frac(p * float2(123.34f, 456.21f));\n"
                        << "    p += dot(p, p + 45.32f);\n"
                        << "    return frac(p.x * p.y);\n}\n\n";
                }

                statements.str(std::string());
                statements.clear();
                if (asset.domain == ShaderDomain::PostProcess)
                {
                    Value base = Input(*output, 0, {
                        ShaderComposerValueType::Float4,
                        "source_texture.Sample(source_sampler, pin.texcoord)", true });
                    Value emission = Input(*output, 1, {
                        ShaderComposerValueType::Float3, "float3(0,0,0)", true });
                    Value opacity = Input(*output, 2, {
                        ShaderComposerValueType::Float, "1.0f", true });
                    const std::string base4 = Convert(base, ShaderComposerValueType::Float4);
                    const std::string emission3 = Convert(emission, ShaderComposerValueType::Float3);
                    const std::string opacity1 = Convert(opacity, ShaderComposerValueType::Float);
                    if (base4.empty()) AddError(output->id, "Base Color input 縺ｮ蝙九ｒ float4 縺ｸ螟画鋤縺ｧ縺阪∪縺帙ｓ");
                    if (emission3.empty()) AddError(output->id, "Emission input 縺ｮ蝙九ｒ float3 縺ｸ螟画鋤縺ｧ縺阪∪縺帙ｓ");
                    if (opacity1.empty()) AddError(output->id, "Opacity input 縺ｮ蝙九ｒ float 縺ｸ螟画鋤縺ｧ縺阪∪縺帙ｓ");
                    if (!diagnostics.empty()) return Finish(false, {});

                    header << "float4 main(VSOutput pin) : SV_TARGET\n{\n";
                    header << statements.str();
                    header << "    float4 replay_base = " << base4 << ";\n"
                        << "    float3 replay_emission = " << emission3 << ";\n"
                        << "    float replay_opacity = saturate(" << opacity1 << ");\n"
                        << "    replay_base.rgb += replay_emission;\n"
                        << "    replay_base.a *= replay_opacity;\n"
                        << "    return replay_base;\n}\n";
                }
                else if (asset.domain == ShaderDomain::Surface)
                {
                    Value base = Input(*output, 0, { ShaderComposerValueType::Float4, "float4(1,1,1,1)", true });
                    Value emission = Input(*output, 1, { ShaderComposerValueType::Float3, "float3(0,0,0)", true });
                    Value opacity = Input(*output, 2, { ShaderComposerValueType::Float, "1.0f", true });
                    const std::string base4 = Convert(base, ShaderComposerValueType::Float4);
                    const std::string emission3 = Convert(emission, ShaderComposerValueType::Float3);
                    const std::string opacity1 = Convert(opacity, ShaderComposerValueType::Float);
                    if (base4.empty()) AddError(output->id, "Base Color input の型を float4 へ変換できません");
                    if (emission3.empty()) AddError(output->id, "Emission input の型を float3 へ変換できません");
                    if (opacity1.empty()) AddError(output->id, "Opacity input の型を float へ変換できません");
                    if (!diagnostics.empty()) return Finish(false, {});

                    header << "float4 main(VS_OUT pin) : SV_TARGET\n{\n";
                    header << statements.str();
                    header << "    float4 replay_base = " << base4 << ";\n"
                        << "    float3 replay_emission = " << emission3 << ";\n"
                        << "    float replay_opacity = saturate(" << opacity1 << ");\n"
                        << "    replay_base.rgb += replay_emission;\n"
                        << "    replay_base.a *= replay_opacity;\n"
                        << "    return replay_base * pin.color;\n}\n";
                }
                else
                {
                    Value color = Input(*output, 0, { ShaderComposerValueType::Float4, "float4(1,1,1,1)", true });
                    const std::string color4 = Convert(color, ShaderComposerValueType::Float4);
                    if (color4.empty()) AddError(output->id, "Layer Color input の型を float4 へ変換できません");
                    if (!diagnostics.empty()) return Finish(false, {});

                    header << "float4 main(VS_OUT pin) : SV_TARGET\n{\n";
                    header << statements.str();
                    header << "    return " << color4 << " * pin.color;\n}\n";
                }
                return Finish(true, header.str());
            }

        private:
            Value Input(const ShaderComposerNode& node, std::uint32_t pin,
                const Value& fallback)
            {
                const std::uint64_t key = (node.id << 16) ^ pin;
                const auto found = inputs.find(key);
                if (found == inputs.end()) return fallback;
                return Build(found->second->from_node);
            }

            Value Build(std::uint64_t id)
            {
                const auto memo_found = memo.find(id);
                if (memo_found != memo.end()) return memo_found->second;
                if (!visiting.insert(id).second)
                {
                    AddError(id, "Graph に cycle があります");
                    return {};
                }
                const ShaderComposerNode* node = asset.FindNode(id);
                if (node == nullptr)
                {
                    AddError(id, "Connection source node がありません");
                    visiting.erase(id);
                    return {};
                }

                Value result;
                const std::string var = "replay_n" + std::to_string(node->id);
                auto emit = [&](ShaderComposerValueType type, const std::string& expression)
                {
                    statements << "    " << TypeName(type) << ' ' << var << " = " << expression << ";\n";
                    result = { type, var, true };
                };

                switch (node->kind)
                {
                case ShaderComposerNodeKind::UV:
                    emit(ShaderComposerValueType::Float2, "pin.texcoord"); break;
                case ShaderComposerNodeKind::Time:
                    emit(ShaderComposerValueType::Float, "frame_params.z"); break;
                case ShaderComposerNodeKind::Normal:
                    emit(ShaderComposerValueType::Float3, "normalize(pin.world_normal.xyz)"); break;
                case ShaderComposerNodeKind::ViewDirection:
                    emit(ShaderComposerValueType::Float3,
                        "normalize(camera_position.xyz - pin.world_position.xyz)"); break;
                case ShaderComposerNodeKind::Float:
                    emit(ShaderComposerValueType::Float, FloatLiteral(node->value)); break;
                case ShaderComposerNodeKind::Color:
                    emit(ShaderComposerValueType::Float4, "float4(" + FloatLiteral(node->color.x) + "," +
                        FloatLiteral(node->color.y) + "," + FloatLiteral(node->color.z) + "," + FloatLiteral(node->color.w) + ")"); break;
                case ShaderComposerNodeKind::FloatProperty:
                    emit(ShaderComposerValueType::Float, node->name); break;
                case ShaderComposerNodeKind::ColorProperty:
                    emit(ShaderComposerValueType::Float4, node->name); break;
                case ShaderComposerNodeKind::TextureProperty:
                {
                    Value uv = Input(*node, 0, { ShaderComposerValueType::Float2, "pin.texcoord", true });
                    const std::string uv2 = Convert(uv, ShaderComposerValueType::Float2);
                    if (uv2.empty()) AddError(node->id, "Texture UV input は float2 に変換できません");
                    else emit(ShaderComposerValueType::Float4,
                        node->name + ".Sample(replay_composer_sampler, " + uv2 + ")");
                    break;
                }
                case ShaderComposerNodeKind::Add:
                case ShaderComposerNodeKind::Subtract:
                case ShaderComposerNodeKind::Multiply:
                case ShaderComposerNodeKind::Divide:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    Value b = Input(*node, 1, { ShaderComposerValueType::Float, "0.0f", true });
                    ShaderComposerValueType type = MergeTypes(a.type, b.type);
                    if (type == ShaderComposerValueType::Invalid)
                    {
                        AddError(node->id, "Math node は同じ vector size または scalar と vector を接続してください");
                        break;
                    }
                    const std::string ax = Convert(a, type), bx = Convert(b, type);
                    const char* op = node->kind == ShaderComposerNodeKind::Add ? "+" :
                        node->kind == ShaderComposerNodeKind::Subtract ? "-" :
                        node->kind == ShaderComposerNodeKind::Multiply ? "*" : "/";
                    const std::string right = node->kind == ShaderComposerNodeKind::Divide
                        ? "(" + bx + " + 1.0e-6f)" : bx;
                    emit(type, "(" + ax + " " + op + " " + right + ")");
                    break;
                }
                case ShaderComposerNodeKind::Lerp:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    Value b = Input(*node, 1, { ShaderComposerValueType::Float, "1.0f", true });
                    Value t = Input(*node, 2, { ShaderComposerValueType::Float, "0.5f", true });
                    ShaderComposerValueType type = MergeTypes(a.type, b.type);
                    if (type == ShaderComposerValueType::Invalid) { AddError(node->id, "Lerp A/B type が互換ではありません"); break; }
                    const std::string tx = t.type == ShaderComposerValueType::Float
                        ? t.expression : Convert(t, type);
                    if (tx.empty()) { AddError(node->id, "Lerp T type が互換ではありません"); break; }
                    emit(type, "lerp(" + Convert(a, type) + ", " + Convert(b, type) + ", " + tx + ")");
                    break;
                }
                case ShaderComposerNodeKind::Saturate:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "0.0f", true });
                    emit(a.type, "saturate(" + a.expression + ")"); break;
                }
                case ShaderComposerNodeKind::Power:
                {
                    Value a = Input(*node, 0, { ShaderComposerValueType::Float, "1.0f", true });
                    Value b = Input(*node, 1, { ShaderComposerValueType::Float, "2.0f", true });
                    ShaderComposerValueType type = a.type;
                    const std::string bx = b.type == ShaderComposerValueType::Float
                        ? b.expression : Convert(b, type);
                    if (bx.empty()) { AddError(node->id, "Power exponent type が互換ではありません"); break; }
                    emit(type, "pow(max(" + a.expression + ", 0.0f), " + bx + ")"); break;
                }
                case ShaderComposerNodeKind::Fresnel:
                {
                    Value n = Input(*node, 0, { ShaderComposerValueType::Float3, "normalize(pin.world_normal.xyz)", true });
                    Value v = Input(*node, 1, { ShaderComposerValueType::Float3, "normalize(camera_position.xyz - pin.world_position.xyz)", true });
                    Value p = Input(*node, 2, { ShaderComposerValueType::Float, FloatLiteral(node->value), true });
                    const std::string n3 = Convert(n, ShaderComposerValueType::Float3);
                    const std::string v3 = Convert(v, ShaderComposerValueType::Float3);
                    const std::string p1 = Convert(p, ShaderComposerValueType::Float);
                    if (n3.empty() || v3.empty() || p1.empty()) { AddError(node->id, "Fresnel input type が不正です"); break; }
                    emit(ShaderComposerValueType::Float,
                        "pow(saturate(1.0f - dot(normalize(" + n3 + "), normalize(" + v3 + "))), max(" + p1 + ", 0.0001f))");
                    break;
                }
                case ShaderComposerNodeKind::UVScroll:
                {
                    Value uv = Input(*node, 0, { ShaderComposerValueType::Float2, "pin.texcoord", true });
                    Value speed = Input(*node, 1, { ShaderComposerValueType::Float2,
                        "float2(" + FloatLiteral(node->vector2.x) + "," + FloatLiteral(node->vector2.y) + ")", true });
                    Value time = Input(*node, 2, { ShaderComposerValueType::Float, "frame_params.z", true });
                    const std::string uv2 = Convert(uv, ShaderComposerValueType::Float2);
                    const std::string speed2 = Convert(speed, ShaderComposerValueType::Float2);
                    const std::string time1 = Convert(time, ShaderComposerValueType::Float);
                    if (uv2.empty() || speed2.empty() || time1.empty()) { AddError(node->id, "UV Scroll input type が不正です"); break; }
                    emit(ShaderComposerValueType::Float2, "(" + uv2 + " + " + speed2 + " * " + time1 + ")");
                    break;
                }
                case ShaderComposerNodeKind::Noise:
                {
                    Value uv = Input(*node, 0, { ShaderComposerValueType::Float2, "pin.texcoord", true });
                    Value scale = Input(*node, 1, { ShaderComposerValueType::Float, FloatLiteral(node->value), true });
                    const std::string uv2 = Convert(uv, ShaderComposerValueType::Float2);
                    const std::string scale1 = Convert(scale, ShaderComposerValueType::Float);
                    if (uv2.empty() || scale1.empty()) { AddError(node->id, "Noise input type が不正です"); break; }
                    emit(ShaderComposerValueType::Float, "replay_composer_noise(" + uv2 + " * " + scale1 + ")");
                    break;
                }
                case ShaderComposerNodeKind::Dissolve:
                {
                    Value value = Input(*node, 0, { ShaderComposerValueType::Float, "1.0f", true });
                    Value threshold = Input(*node, 1, { ShaderComposerValueType::Float, FloatLiteral(node->value), true });
                    Value edge = Input(*node, 2, { ShaderComposerValueType::Float, FloatLiteral(node->minimum), true });
                    const std::string v = Convert(value, ShaderComposerValueType::Float);
                    const std::string t = Convert(threshold, ShaderComposerValueType::Float);
                    const std::string e = Convert(edge, ShaderComposerValueType::Float);
                    if (v.empty() || t.empty() || e.empty()) { AddError(node->id, "Dissolve input type が不正です"); break; }
                    emit(ShaderComposerValueType::Float,
                        "smoothstep(" + t + " - max(" + e + ", 1.0e-5f), " + t + " + max(" + e + ", 1.0e-5f), " + v + ")");
                    break;
                }
                case ShaderComposerNodeKind::SurfaceOutput:
                case ShaderComposerNodeKind::LayerOutput:
                    AddError(node->id, "Output node を別 node の入力へ接続できません"); break;
                default:
                    AddError(node->id, "未対応 node kind"); break;
                }

                visiting.erase(id);
                memo[id] = result;
                return result;
            }

            void AddError(std::uint64_t id, std::string message)
            {
                diagnostics.push_back({ id, std::move(message) });
            }

            ShaderComposerGenerateResult Finish(bool ok, std::string hlsl)
            {
                ShaderComposerGenerateResult result;
                result.succeeded = ok && diagnostics.empty();
                result.hlsl = std::move(hlsl);
                result.diagnostics = std::move(diagnostics);
                return result;
            }

            const ShaderComposerAsset& asset;
            std::unordered_map<std::uint64_t, const ShaderComposerConnection*> inputs;
            std::unordered_map<std::uint64_t, Value> memo;
            std::unordered_set<std::uint64_t> visiting;
            std::ostringstream statements;
            std::vector<ShaderComposerDiagnostic> diagnostics;
        };
    }

    ShaderComposerGenerateResult ShaderComposerGenerator::Generate(
        const ShaderComposerAsset& asset)
    {
        Generator generator(asset);
        return generator.Run();
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
