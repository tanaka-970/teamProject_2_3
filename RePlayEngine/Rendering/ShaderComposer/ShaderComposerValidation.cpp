#include "ShaderComposerValidation.h"

#include "ShaderComposerAsset.h"
#include "ShaderComposerGenerator.h"
#include "../Shaders/ShaderCompiler.h"
#include "../Shaders/ShaderConstantPacker.h"
#include "../Shaders/ShaderSource.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace ReplayEngine::Rendering::Validation
{
    namespace
    {
        struct Check final
        {
            int code = 1800;
            int failure = 0;
            void Expect(bool condition, const char* text)
            {
                ++code;
                if (condition)
                    std::cout << "[PASS " << code << "] " << text << '\n';
                else if (failure == 0)
                {
                    failure = code;
                    std::cerr << "[FAIL " << code << "] " << text << '\n';
                }
            }
        };

        bool CompileGenerated(const ShaderComposerAsset& graph,
            const std::string& source, ShaderVariant variant, std::string& error)
        {
            bool needs_guid = false;
            const auto parsed = ShaderSource::ParseText(source,
                graph.generated_hlsl, needs_guid);
            if (!parsed.succeeded || needs_guid)
            {
                error = "generated pragma parse failed";
                return false;
            }
            std::vector<ShaderProperty> properties = parsed.info.properties;
            std::uint32_t size = 0;
            ShaderConstantPacker::AssignOffsets(properties, size);
            ShaderPropertySchema schema(parsed.info.id, std::move(properties), 1);
            const std::string declaration = ShaderConstantPacker::GenerateHlslDeclaration(schema);
            std::string combined = declaration + "\n#line 1 \"composer_generated\"\n" + source;

            ShaderCompiler::Options options = ShaderCompiler::DefaultOptions(false);
            options.defines.emplace_back(shader_variant_define,
                variant == ShaderVariant::Skinned ? "1" : "0");
            ShaderBytecode bytecode;
            const ShaderCompileResult compiled = ShaderCompiler::CompileSource(
                combined, graph.generated_hlsl, "main", "ps_6_0", options, bytecode);
            if (!compiled.succeeded)
            {
                error = compiled.Summary();
                return false;
            }
            return bytecode != nullptr;
        }

        bool CompileGeneratedStandalone(const ShaderComposerAsset& graph,
            const std::string& source, ShaderVariant variant, std::string& error)
        {
            ShaderCompiler::Options options = ShaderCompiler::DefaultOptions(false);
            options.defines.emplace_back(shader_variant_define,
                variant == ShaderVariant::Skinned ? "1" : "0");
            ShaderBytecode bytecode;
            const ShaderCompileResult compiled = ShaderCompiler::CompileSource(
                source, graph.generated_hlsl, "main", "ps_6_0", options, bytecode);
            if (!compiled.succeeded)
            {
                error = compiled.Summary();
                return false;
            }
            return bytecode != nullptr;
        }

        ShaderComposerNode* FindFirst(ShaderComposerAsset& graph, ShaderComposerNodeKind kind)
        {
            for (ShaderComposerNode& node : graph.nodes) if (node.kind == kind) return &node;
            return nullptr;
        }
    }

    int RunShaderComposerValidation()
    {
        Check check;
        const std::filesystem::path folder = std::filesystem::path("Saved") / "Validation" / "ShaderComposer";
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
        std::filesystem::create_directories(folder, ec);
        check.Expect(!ec, "validation folder を作成できる");

        const std::filesystem::path graph_path = folder / "Surface.replayshadergraph";
        ShaderComposerAsset surface = ShaderComposerAsset::CreateDefault(
            ShaderDomain::Surface, "ComposerValidationSurface",
            std::filesystem::path("Shader") / "Materials" / "Generated" / "ComposerValidationSurface.hlsl");
        check.Expect(surface.shader_id.IsValid(), "Composer は固定 ShaderGUID を持つ");
        check.Expect(surface.nodes.size() >= 5 && !surface.connections.empty(),
            "default surface graph は接続済み node を持つ");

        const std::uint64_t time_id = surface.AddNode(ShaderComposerNodeKind::Time, 20, 420).id;
        const std::uint64_t fresnel_id = surface.AddNode(ShaderComposerNodeKind::Fresnel, 240, 420).id;
        ShaderComposerNode& fresnel_power_node = surface.AddNode(ShaderComposerNodeKind::FloatProperty, 20, 540);
        const std::uint64_t fresnel_power_id = fresnel_power_node.id;
        fresnel_power_node.name = "FresnelPower";
        fresnel_power_node.display_name = "Fresnel Power";
        fresnel_power_node.value = 2.0f;
        fresnel_power_node.minimum = 0.1f;
        fresnel_power_node.maximum = 8.0f;
        ShaderComposerNode& emission_color_node = surface.AddNode(ShaderComposerNodeKind::ColorProperty, 240, 540);
        const std::uint64_t emission_color_id = emission_color_node.id;
        emission_color_node.name = "EmissionColor";
        emission_color_node.display_name = "Emission Color";
        emission_color_node.category = "Emission";
        const std::uint64_t emission_mul_id = surface.AddNode(ShaderComposerNodeKind::Multiply, 500, 480).id;
        ShaderComposerNode* output = FindFirst(surface, ShaderComposerNodeKind::SurfaceOutput);
        check.Expect(output != nullptr, "surface output が存在する");
        if (output)
        {
            surface.Connect(fresnel_power_id, fresnel_id, 2);
            surface.Connect(fresnel_id, emission_mul_id, 0);
            surface.Connect(emission_color_id, emission_mul_id, 1);
            surface.Connect(emission_mul_id, output->id, 1);
            surface.Connect(time_id, output->id, 2);
        }

        std::string error;
        check.Expect(ShaderComposerAsset::Save(surface, graph_path, error), "graph を保存できる");
        ShaderComposerAsset loaded;
        check.Expect(ShaderComposerAsset::Load(graph_path, loaded, error), "graph を再読込できる");
        check.Expect(loaded.shader_id == surface.shader_id &&
            loaded.nodes.size() == surface.nodes.size() &&
            loaded.connections.size() == surface.connections.size(),
            "保存/再読込で GUID / node / connection が一致する");

        const ShaderComposerGenerateResult generated = ShaderComposerGenerator::Generate(loaded);
        check.Expect(generated.succeeded && generated.diagnostics.empty(), "surface HLSL を生成できる");
        check.Expect(generated.hlsl.find("#pragma replay_domain   surface") != std::string::npos &&
            generated.hlsl.find("EmissionColor") != std::string::npos &&
            generated.hlsl.find("FresnelPower") != std::string::npos &&
            generated.hlsl.find("frame_params.z") != std::string::npos,
            "generated HLSL に domain / 公開 Property / accumulated Time が入る");
        check.Expect(generated.hlsl.find("#include \"../../static_mesh.hlsli\"") != std::string::npos &&
            generated.hlsl.find("#include \"../../frame_common.hlsli\"") != std::string::npos &&
            generated.hlsl.find("REPLAY_MATERIAL_SCHEMA_INJECTED") != std::string::npos,
            "generated HLSL は Visual Studio から単体で解決できる include/schema fallback を持つ");

        bool needs_guid = false;
        const auto parsed = ShaderSource::ParseText(generated.hlsl, loaded.generated_hlsl, needs_guid);
        check.Expect(parsed.succeeded && !needs_guid && parsed.info.id == loaded.shader_id,
            "generated HLSL は通常 ShaderAsset として parse できる");
        bool found_emission = false;
        for (const ShaderProperty& property : parsed.info.properties)
            if (property.name == "EmissionColor") found_emission = true;
        check.Expect(found_emission, "Composer Property は Shader Schema へ現れる");

        std::string compile_error;
        check.Expect(CompileGenerated(loaded, generated.hlsl, ShaderVariant::Static, compile_error),
            "generated surface Static variant が compile できる");
        if (!compile_error.empty()) std::cerr << compile_error << '\n';
        compile_error.clear();
        check.Expect(CompileGeneratedStandalone(loaded, generated.hlsl, ShaderVariant::Static, compile_error),
            "generated surface は schema injection なしでも standalone compile できる");
        if (!compile_error.empty()) std::cerr << compile_error << '\n';
        compile_error.clear();
        check.Expect(CompileGenerated(loaded, generated.hlsl, ShaderVariant::Skinned, compile_error),
            "generated surface Skinned variant が compile できる");
        if (!compile_error.empty()) std::cerr << compile_error << '\n';

        ShaderComposerAsset layer = ShaderComposerAsset::CreateDefault(
            ShaderDomain::Layer, "ComposerValidationLayer",
            std::filesystem::path("Shader") / "Layers" / "Generated" / "ComposerValidationLayer.hlsl");
        const ShaderComposerGenerateResult layer_generated = ShaderComposerGenerator::Generate(layer);
        check.Expect(layer_generated.succeeded &&
            layer_generated.hlsl.find("#pragma replay_domain   layer") != std::string::npos,
            "Layer Graph も同じ Generator から作れる");
        compile_error.clear();
        check.Expect(CompileGenerated(layer, layer_generated.hlsl, ShaderVariant::Static, compile_error),
            "generated Layer Static variant が compile できる");
        if (!compile_error.empty()) std::cerr << compile_error << '\n';
        compile_error.clear();
        check.Expect(CompileGenerated(layer, layer_generated.hlsl, ShaderVariant::Skinned, compile_error),
            "generated Layer Skinned variant が compile できる");
        if (!compile_error.empty()) std::cerr << compile_error << '\n';

        // Cycle must fail instead of recursing forever.
        ShaderComposerAsset cyclic = ShaderComposerAsset::CreateDefault(
            ShaderDomain::Surface, "Cycle", "Shader/Materials/Generated/Cycle.hlsl");
        const std::uint64_t a_id = cyclic.AddNode(ShaderComposerNodeKind::Add, 0, 0).id;
        const std::uint64_t b_id = cyclic.AddNode(ShaderComposerNodeKind::Multiply, 0, 0).id;
        ShaderComposerNode* cyclic_output = FindFirst(cyclic, ShaderComposerNodeKind::SurfaceOutput);
        cyclic.Connect(a_id, b_id, 0);
        cyclic.Connect(b_id, a_id, 0);
        if (cyclic_output) cyclic.Connect(a_id, cyclic_output->id, 0);
        const auto cycle_result = ShaderComposerGenerator::Generate(cyclic);
        check.Expect(!cycle_result.succeeded && !cycle_result.diagnostics.empty(),
            "cycle graph は明示エラーになり無限再帰しない");

        // Duplicate exposed property name must fail to prevent an invalid cbuffer.
        ShaderComposerAsset duplicate = ShaderComposerAsset::CreateDefault(
            ShaderDomain::Surface, "Duplicate", "Shader/Materials/Generated/Duplicate.hlsl");
        ShaderComposerNode& duplicate_property = duplicate.AddNode(ShaderComposerNodeKind::FloatProperty, 0, 0);
        duplicate_property.name = "BaseMap"; // existing texture property name
        const auto duplicate_result = ShaderComposerGenerator::Generate(duplicate);
        check.Expect(!duplicate_result.succeeded, "公開 Property 名の重複を生成前に検出する");

        std::filesystem::remove_all(folder, ec);
        return check.failure;
    }
}
