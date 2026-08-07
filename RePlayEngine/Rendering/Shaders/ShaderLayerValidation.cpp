#include "ShaderLayerValidation.h"

#include "ShaderAssetFactory.h"
#include "ShaderLibrary.h"
#include "../Materials/MaterialAsset.h"
#include "../ShaderStack/BuiltInShaderLayers.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace ReplayEngine::Rendering::Validation
{
    namespace
    {
        class Checker final
        {
        public:
            explicit Checker(int first_code) : next_code_(first_code) {}

            void Expect(bool condition, const std::string& what)
            {
                const int code = next_code_++;
                ++total_;
                if (condition) return;
                ++failures_;
                if (first_failure_ == 0) first_failure_ = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what.c_str());
            }

            int Report() const
            {
                if (first_failure_ == 0)
                {
                    std::fprintf(stderr, "shader-layer OK: %d checks passed\n", total_);
                    return 0;
                }
                std::fprintf(stderr,
                    "shader-layer FAILED: %d/%d checks failed (first=%d)\n",
                    failures_, total_, first_failure_);
                return first_failure_;
            }

        private:
            int next_code_ = 1600;
            int first_failure_ = 0;
            int total_ = 0;
            int failures_ = 0;
        };

        std::size_t CountLayer(const ShaderLayerStack& stack, ShaderID id)
        {
            std::size_t count = 0;
            for (const ShaderLayer& layer : stack.Layers())
                if (layer.Is(id)) ++count;
            return count;
        }

        void WriteLegacyV3(const std::filesystem::path& path)
        {
            std::ofstream s(path, std::ios::binary | std::ios::trunc);
            s << "REPLAY_MATERIAL 3\n"
              << "BASE_COLOR 1 1 1 1\n"
              << "BASE_COLOR_TEXTURE \"\"\n"
              << "NORMAL_TEXTURE \"\"\n"
              << "METALLIC 0\n"
              << "METALLIC_TEXTURE \"\"\n"
              << "ROUGHNESS 0.5\n"
              << "ROUGHNESS_TEXTURE \"\"\n"
              << "EMISSIVE 0 0 0 0\n"
              << "EMISSIVE_TEXTURE \"\"\n"
              << "AMBIENT_OCCLUSION 1\n"
              << "AMBIENT_OCCLUSION_TEXTURE \"\"\n"
              << "ALPHA 0 0.5\n"
              << "DOUBLE_SIDED 0\n"
              << "SHADING_MODEL 1\n"
              << "SHADER_GUID \"00000000000000000000000000000002\"\n"
              << "PROPERTY_COUNT 0\n"
              << "PIXELATE 6 1\n"
              << "OUTLINE_PASS 1\n"
              << "LAYER_COUNT 2\n"
              << "LAYER 0 0 1 0.5 1 6 1 1 1 1\n"
              << "LAYER 3 0 1 0.4 0.8 9 1 1 1 1\n"
              << "END_MATERIAL\n";
        }
    }

    int RunShaderLayerValidation()
    {
        Checker check(1600);
        namespace fs = std::filesystem;

        // ---- 固定 GUID / legacy map ---------------------------------------
        check.Expect(BuiltInShaderLayers::All().size() == 7,
            "built-in layer は 7 種");
        std::set<std::string> ids;
        for (const BuiltInShaderLayers::Definition& definition :
            BuiltInShaderLayers::All())
        {
            check.Expect(definition.id.IsValid(),
                std::string(definition.display_name) + ": GUID が有効");
            check.Expect(ids.insert(definition.id.ToString()).second,
                std::string(definition.display_name) + ": GUID が重複しない");
            check.Expect(BuiltInShaderLayers::FromLegacyType(definition.legacy_type) ==
                definition.id,
                std::string(definition.display_name) + ": legacy enum -> GUID");
            std::uint32_t legacy = 99;
            check.Expect(BuiltInShaderLayers::TryGetLegacyType(definition.id, legacy) &&
                legacy == definition.legacy_type,
                std::string(definition.display_name) + ": GUID -> legacy enum");
        }
        check.Expect(!BuiltInShaderLayers::FromLegacyType(99).IsValid(),
            "unknown legacy layer を勝手に丸めない");

        // ---- 実 Shader/Layers を走査 / compile -----------------------------
        ShaderLibrary library;
        const ShaderLibrary::ScanReport scan = library.ScanAll(fs::current_path());
        check.Expect(scan.duplicate_ids == 0, "Layer Shader GUID に重複がない");
        check.Expect(scan.compile_failed == 0,
            "Project の Shader Catalog が Layer を含めて全部 compile できる");

        for (const BuiltInShaderLayers::Definition& definition :
            BuiltInShaderLayers::All())
        {
            const ShaderCatalog::Entry* entry = library.Catalog().Find(definition.id);
            const std::string name = definition.display_name;
            check.Expect(entry != nullptr, name + ": Catalog に載る");
            if (entry == nullptr) continue;
            check.Expect(entry->info.domain == ShaderDomain::Layer,
                name + ": domain=layer");
            check.Expect(entry->At(ShaderVariant::Static).compiled,
                name + ": Static compile");
            check.Expect(entry->At(ShaderVariant::Skinned).compiled,
                name + ": Skinned compile");
        }

        // ---- v4: GUID + PropertyBag + 順序 + Missing を保存 ----------------
        const fs::path dir = fs::temp_directory_path() /
            "replay_shader_layer_validation";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        std::string error;

        MaterialAsset material;
        ShaderLayer& first = material.layers.Add(BuiltInShaderLayers::Pixelate);
        first.blend = ShaderLayerBlend::Multiply;
        first.enabled = true;
        first.properties.Set("prop.PixelSize",
            Reflection::PropertyValue::MakeFloat(11.0f));
        first.properties.Set("prop.UnknownFutureLayerValue",
            Reflection::PropertyValue::MakeString("keep-layer-data"));
        first.SyncPropertiesToLegacyFields();
        const std::uint64_t first_id = first.id;

        ShaderID custom;
        check.Expect(ShaderID::TryParse(
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa10", custom) && custom.IsValid(),
            "custom layer test GUID を作れる");
        ShaderLayer& second = material.layers.Add(custom);
        second.blend = ShaderLayerBlend::Additive;
        second.enabled = false;
        second.properties.Set("prop.Strength",
            Reflection::PropertyValue::MakeFloat(3.5f));
        second.properties.Set("prop.Texture",
            Reflection::PropertyValue::MakeAssetReference(
                "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
        const std::uint64_t second_id = second.id;

        const fs::path v4 = dir / "stack_v4.replaymaterial";
        check.Expect(MaterialAsset::Save(material, v4, error),
            "Material v4 Layer Stack を保存できる: " + error);
        MaterialAsset roundtrip;
        check.Expect(MaterialAsset::Load(v4, roundtrip, error),
            "Material v4 Layer Stack を再読込できる: " + error);
        check.Expect(roundtrip.layers.Layers().size() == 2,
            "Layer 数を保存・復元する");
        if (roundtrip.layers.Layers().size() == 2)
        {
            const ShaderLayer& a = roundtrip.layers.Layers()[0];
            const ShaderLayer& b = roundtrip.layers.Layers()[1];
            check.Expect(a.EffectiveShader() == BuiltInShaderLayers::Pixelate &&
                b.EffectiveShader() == custom,
                "Layer GUID と順序を保存・復元する");
            check.Expect(a.id == first_id && b.id == second_id && a.id != b.id,
                "Layer persistent ID を Save/Reload で維持する");
            check.Expect(a.blend == ShaderLayerBlend::Multiply &&
                b.blend == ShaderLayerBlend::Additive && !b.enabled,
                "Layer blend/enabled を保存・復元する");
            const auto* unknown = a.properties.Find("prop.UnknownFutureLayerValue");
            check.Expect(unknown != nullptr && unknown->AsString() == "keep-layer-data",
                "未知 Layer Property を捨てない");
            const auto* strength = b.properties.Find("prop.Strength");
            check.Expect(strength != nullptr && strength->AsFloat() > 3.49f,
                "Missing/custom Layer の PropertyBag を保持する");
        }

        // ---- v3 migration / outline bool -> Outline Layer -----------------
        const fs::path legacy = dir / "legacy_v3.replaymaterial";
        WriteLegacyV3(legacy);
        MaterialAsset migrated;
        check.Expect(MaterialAsset::Load(legacy, migrated, error),
            "Material v3 layer を読める: " + error);
        check.Expect(migrated.layers.Layers().size() == 3,
            "v3 2 layers + outline_pass を 3 GUID layers へ移行する");
        if (migrated.layers.Layers().size() >= 2)
        {
            check.Expect(migrated.layers.Layers()[0].EffectiveShader() ==
                BuiltInShaderLayers::Pbr,
                "legacy PBR layer -> fixed GUID");
            check.Expect(migrated.layers.Layers()[1].EffectiveShader() ==
                BuiltInShaderLayers::Pixelate,
                "legacy Pixelate layer -> fixed GUID");
        }
        check.Expect(CountLayer(migrated.layers, BuiltInShaderLayers::Outline) == 1,
            "outline_pass=true は Outline Layer 1 枚へだけ移行する");

        // ---- 64 layers ------------------------------------------------------
        MaterialAsset sixty_four;
        for (std::size_t i = 0; i < ShaderLayerStack::MaxLayers; ++i)
        {
            ShaderLayer& layer = sixty_four.layers.Add(BuiltInShaderLayers::Unlit);
            layer.properties.Set("prop.Sequence",
                Reflection::PropertyValue::MakeFloat(static_cast<float>(i)));
        }
        check.Expect(sixty_four.layers.Layers().size() == ShaderLayerStack::MaxLayers &&
            !sixty_four.layers.CanAdd(), "64 Layer を保持し上限で止まる");
        const fs::path max_path = dir / "max_layers.replaymaterial";
        check.Expect(MaterialAsset::Save(sixty_four, max_path, error),
            "64 Layer Material を保存できる: " + error);
        MaterialAsset max_roundtrip;
        check.Expect(MaterialAsset::Load(max_path, max_roundtrip, error) &&
            max_roundtrip.layers.Layers().size() == ShaderLayerStack::MaxLayers,
            "64 Layer Material を再読込できる: " + error);

        // ---- 新規 Layer Shader Asset 作成 + Catalog ------------------------
        const fs::path custom_root = dir / "CustomProject";
        const fs::path custom_path = custom_root / "Shader" / "Layers" /
            "Project" / "ValidationLayer.hlsl";
        ShaderID created_id;
        check.Expect(ShaderAssetFactory::CreateLayerShader(custom_path,
            "Validation Layer", "Project/Validation", created_id, error),
            "Layer Shader Asset を C++ 変更なしで作れる: " + error);
        ShaderLibrary custom_library;
        const auto custom_report = custom_library.ScanAll(custom_root);
        const ShaderCatalog::Entry* created = custom_library.Catalog().Find(created_id);
        check.Expect(custom_report.compile_failed == 0 && created != nullptr,
            "自作 Layer Shader が走査・compile・Catalog 登録される");
        if (created != nullptr)
        {
            check.Expect(created->info.domain == ShaderDomain::Layer,
                "自作 Layer Shader は domain=layer");
            check.Expect(created->schema != nullptr &&
                created->schema->FindByName("Strength") != nullptr,
                "自作 Layer Schema を自動生成する");
        }

        // BOM 付きファイルも generated cbuffer の後ろで壊れない回帰。
        const fs::path bom_path = custom_root / "Shader" / "Layers" /
            "Project" / "BomLayer.hlsl";
        {
            std::ofstream out(bom_path, std::ios::binary | std::ios::trunc);
            const unsigned char bom[3]{ 0xEF, 0xBB, 0xBF };
            out.write(reinterpret_cast<const char*>(bom), 3);
            out << "#pragma replay_guid \"cccccccccccccccccccccccccccccc10\"\n"
                << "#pragma replay_name \"BOM Layer\"\n"
                << "#pragma replay_domain layer\n"
                << "#pragma replay_lighting unlit\n"
                << "struct I{float4 p:SV_POSITION;float4 c:COLOR;float2 uv:TEXCOORD;};\n"
                << "float4 main(I x):SV_TARGET{return x.c;}\n";
        }
        ShaderLibrary bom_library;
        const auto bom_report = bom_library.ScanAll(custom_root);
        check.Expect(bom_report.compile_failed == 0,
            "UTF-8 BOM 付き Shader も runtime compile できる");

        fs::remove_all(dir, ec);
        return check.Report();
    }
}
