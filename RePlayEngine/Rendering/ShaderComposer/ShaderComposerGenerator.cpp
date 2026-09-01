// Shader Composer 生成のうち「入力検証」と「HLSL 全体の組み立て」だけを持つ。
//
//   ShaderComposerGenerator.cpp          … 入力検証と HLSL 全体の組み立て（このファイル）
//   ShaderComposerGeneratorInternal.h    … 分割内部の Generator と Value の宣言
//   ShaderComposerGeneratorNodes.cpp     … Node ごとの式生成
//   ShaderComposerGeneratorFile.cpp      … 生成 HLSL の Atomic Save

#include "ShaderComposerGenerator.h"
#include "ShaderComposerGeneratorInternal.h"
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
    namespace Detail
    {
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
                // 旧 Shader Model 5 コンパイラ では float4(x) のような
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

        Generator::Generator(const ShaderComposerAsset& source)
                : asset(source)
            {
                for (const ShaderComposerConnection& edge : asset.connections)
                {
                    const std::uint64_t key = (edge.to_node << 16) ^ edge.to_pin;
                    inputs[key] = &edge;
                }
            }

        ShaderComposerGenerateResult Generator::Run()
            {
                if (!asset.shader_id.IsValid())
                    AddError(0, "ShaderGUID がありません");
                if (asset.domain != ShaderDomain::Surface &&
                    asset.domain != ShaderDomain::Layer &&
                    asset.domain != ShaderDomain::PostProcess)
                    AddError(0, "Composer domain は Surface / Layer / PostProcess に対応します");
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

                    // UI Effect も Surface と同じ #pragma property -> b9/t40+ を正本にする。
                    // 旧実装の「先頭8 floatだけ b0 へ詰める」方式では Property 数に上限があり、
                    // Color/Texture が全項目で同じ値へ潰れていた。Runtime では ShaderLibrary が
                    // canonical declaration を前置するため、ここは単体コンパイル用 fallback だけ。
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

                    header << "float replay_composer_noise(float2 p)\n{\n"
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
                    if (base4.empty()) AddError(output->id, "Base Color input の型を float4 へ変換できません");
                    if (emission3.empty()) AddError(output->id, "Emission input の型を float3 へ変換できません");
                    if (opacity1.empty()) AddError(output->id, "Opacity input の型を float へ変換できません");
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

        void Generator::AddError(std::uint64_t id, std::string message)
            {
                diagnostics.push_back({ id, std::move(message) });
            }

        ShaderComposerGenerateResult Generator::Finish(bool ok, std::string hlsl)
            {
                ShaderComposerGenerateResult result;
                result.succeeded = ok && diagnostics.empty();
                result.hlsl = std::move(hlsl);
                result.diagnostics = std::move(diagnostics);
                return result;
            }
    }

    using Detail::Generator;

    ShaderComposerGenerateResult ShaderComposerGenerator::Generate(
        const ShaderComposerAsset& asset)
    {
        Generator generator(asset);
        return generator.Run();
    }
}
