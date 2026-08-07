#include "ShaderPassValidation.h"

#include "ShaderLibrary.h"
#include "ShaderSource.h"
#include "../ShaderStack/ShaderExecutionPlan.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace ReplayEngine::Rendering::Validation
{
    namespace
    {
        class Checker final
        {
        public:
            explicit Checker(int first) : next_(first) {}
            void Expect(bool ok, const std::string& what)
            {
                const int code = next_++;
                ++total_;
                if (ok) return;
                ++failed_;
                if (first_ == 0) first_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what.c_str());
            }
            int Report() const
            {
                if (first_ == 0)
                {
                    std::fprintf(stderr, "shader-pass OK: %d checks passed\n", total_);
                    return 0;
                }
                std::fprintf(stderr,
                    "shader-pass FAILED: %d/%d checks failed (first=%d)\n",
                    failed_, total_, first_);
                return first_;
            }
        private:
            int next_ = 1700;
            int first_ = 0;
            int total_ = 0;
            int failed_ = 0;
        };

        const char* MultiPassSource = R"HLSL(
#pragma replay_guid "dddddddddddddddddddddddddddddd10"
#pragma replay_name "Validation Multi Pass"
#pragma replay_domain layer
#pragma replay_lighting unlit
#pragma property color Tint "Tint" = (1,1,1,1)
#pragma replay_pass "Glow" GlowPass additive
#pragma replay_pass "Mask" MaskPass multiply
struct I { float4 p:SV_POSITION; float4 c:COLOR; float2 uv:TEXCOORD; };
float4 main(I x):SV_TARGET { return x.c * Tint; }
float4 GlowPass(I x):SV_TARGET { return float4(Tint.rgb * 0.25, 0.25); }
float4 MaskPass(I x):SV_TARGET { return float4(0.75,0.75,0.75,1); }
)HLSL";
    }

    int RunShaderPassValidation()
    {
        Checker check(1700);
        namespace fs = std::filesystem;

        bool needs_guid = false;
        const auto parsed = ShaderSource::ParseText(MultiPassSource,
            "ValidationMultiPass.hlsl", needs_guid);
        check.Expect(parsed.succeeded && !needs_guid,
            "multi-pass shader pragma を解析できる");
        check.Expect(parsed.info.passes.size() == 2,
            "Shader が複数 pass を宣言できる");
        if (parsed.info.passes.size() == 2)
        {
            check.Expect(parsed.info.passes[0].name == "Glow" &&
                parsed.info.passes[0].entry_point == "GlowPass" &&
                parsed.info.passes[0].blend == ShaderPassBlend::Additive,
                "1st pass の名前/entry/blend を保持する");
            check.Expect(parsed.info.passes[1].name == "Mask" &&
                parsed.info.passes[1].entry_point == "MaskPass" &&
                parsed.info.passes[1].blend == ShaderPassBlend::Multiply,
                "pass の宣言順を保持する");
        }

        bool bad_needs_guid = false;
        const auto bad = ShaderSource::ParseText(
            "#pragma replay_guid \"eeeeeeeeeeeeeeeeeeeeeeeeeeeeee10\"\n"
            "#pragma replay_domain layer\n"
            "#pragma replay_pass \"Bad\" BadPass mystery\n",
            "BadPass.hlsl", bad_needs_guid);
        bool fatal = false;
        for (const auto& issue : bad.issues) if (issue.fatal) fatal = true;
        check.Expect(fatal, "unknown pass blend を勝手に丸めない");

        const fs::path root = fs::temp_directory_path() /
            "replay_shader_pass_validation";
        std::error_code ec;
        fs::remove_all(root, ec);
        const fs::path file = root / "Shader" / "Layers" / "MultiPass.hlsl";
        fs::create_directories(file.parent_path(), ec);
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << MultiPassSource;
        }

        ShaderLibrary library;
        const auto report = library.ScanAll(root);
        ShaderID id;
        ShaderID::TryParse("dddddddddddddddddddddddddddddd10", id);
        const ShaderCatalog::Entry* entry = library.Catalog().Find(id);
        check.Expect(report.compile_failed == 0 && entry != nullptr,
            "base + additional passes を全部 compile できる");
        if (entry != nullptr)
        {
            check.Expect(entry->passes.size() == 2,
                "Catalog に pass を宣言順で保持する");
            if (entry->passes.size() == 2)
            {
                check.Expect(entry->passes[0].info.entry_point == "GlowPass" &&
                    entry->passes[1].info.entry_point == "MaskPass",
                    "Catalog の pass 順序が固定される");
                check.Expect(entry->passes[0].At(ShaderVariant::Static).compiled &&
                    entry->passes[0].At(ShaderVariant::Skinned).compiled &&
                    entry->passes[1].At(ShaderVariant::Static).compiled &&
                    entry->passes[1].At(ShaderVariant::Skinned).compiled,
                    "各 pass を Static/Skinned で compile する");
                check.Expect(entry->passes[0].At(ShaderVariant::Static).bytecode != nullptr &&
                    entry->passes[1].At(ShaderVariant::Static).bytecode != nullptr,
                    "pass bytecode を Catalog が保持する");
            }
        }

        ShaderLayerStack layers;
        ShaderLayer& a = layers.Add(id);
        a.blend = ShaderLayerBlend::Alpha;
        const std::uint64_t a_id = a.id;

        // ShaderLayerStack は std::vector を使うため、次の Add() で
        // 以前に取得した参照 a が無効化される可能性がある。
        // Validation では参照を保持せず、永続 ID を値として退避して比較する。
        ShaderLayer& b = layers.Add(id);
        b.blend = ShaderLayerBlend::Additive;
        b.enabled = true;
        const std::uint64_t b_id = b.id;

        const auto plan = ShaderExecutionPlan::Build(layers, library.Catalog());
        check.Expect(plan.size() == 6,
            "2 layers x (main + 2 passes) を展開できる");
        if (plan.size() == 6)
        {
            check.Expect(plan[0].kind == ShaderExecutionStepKind::LayerMain &&
                plan[1].kind == ShaderExecutionStepKind::ShaderPass &&
                plan[2].kind == ShaderExecutionStepKind::ShaderPass &&
                plan[0].layer_id == a_id && plan[1].layer_id == a_id &&
                plan[2].layer_id == a_id,
                "Layer -> その Shader passes の順で展開する");
            check.Expect(plan[3].layer_id == b_id &&
                plan[4].layer_id == b_id && plan[5].layer_id == b_id,
                "次 Layer へ移ってからその passes を展開する");
            check.Expect(plan[1].entry_point == "GlowPass" &&
                plan[2].entry_point == "MaskPass",
                "pass 宣言順を Material から変更しない");
            check.Expect(plan[1].blend == ShaderLayerBlend::Additive &&
                plan[2].blend == ShaderLayerBlend::Multiply,
                "pass 固有 blend が Layer blend より優先される");
        }

        fs::remove_all(root, ec);
        return check.Report();
    }
}
