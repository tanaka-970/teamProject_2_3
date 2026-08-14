#include "ShaderEditorValidation.h"

#include "BuiltInShaders.h"
#include "ShaderAssetFactory.h"
#include "ShaderConstantPacker.h"
#include "ShaderLibrary.h"
#include "ShaderSource.h"
#include "../Materials/MaterialAsset.h"
#include "../Materials/MaterialSchema.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace ReplayEngine::Rendering::Validation
{
    namespace
    {
        class Check final
        {
        public:
            void Expect(bool value, const char* text)
            {
                ++total_;
                if (value) return;
                ++failed_;
                std::fprintf(stderr, "  [FAIL %d] %s\n", 1500 + total_ - 1, text);
            }

            int Report() const
            {
                std::fprintf(stderr, "shader-editor %s: %d checks, %d failed\n",
                    failed_ == 0 ? "OK" : "FAILED", total_, failed_);
                return failed_ == 0 ? 0 : 1500;
            }

        private:
            int total_ = 0;
            int failed_ = 0;
        };

        float ReadFloat(const MaterialAsset& material, const char* name,
            float fallback = -999.0f)
        {
            const Reflection::PropertyValue* value = material.properties.Find(name);
            return value == nullptr ? fallback : value->AsFloat(fallback);
        }

        std::string ReadText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return {};
            return std::string(std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
        }
    }

    int RunShaderEditorValidation()
    {
        Check check;

        // ---- 実プロジェクトの Shader Catalog --------------------------------
        ShaderLibrary library;
        const auto report = library.ScanAll(std::filesystem::current_path());
        check.Expect(report.registered >= 5, "built-in 5 shader が Catalog にある");

        const ShaderCatalog::Entry* pbr = library.Catalog().Find(BuiltInShaders::Pbr);
        const ShaderCatalog::Entry* toon = library.Catalog().Find(BuiltInShaders::Toon);
        const ShaderCatalog::Entry* unlit = library.Catalog().Find(BuiltInShaders::Unlit);
        const ShaderCatalog::Entry* pixelate = library.Catalog().Find(BuiltInShaders::Pixelate);
        check.Expect(pbr != nullptr && pbr->schema != nullptr, "PBR が Picker 候補になる");
        check.Expect(toon != nullptr && toon->schema != nullptr, "Toon が Picker 候補になる");
        check.Expect(unlit != nullptr && unlit->schema != nullptr, "Unlit が Picker 候補になる");
        check.Expect(pixelate != nullptr && pixelate->schema != nullptr,
            "Pixelate が Picker 候補になる");

        if (pbr && pbr->schema)
        {
            const ShaderProperty* roughness = pbr->schema->FindByName("Roughness");
            check.Expect(roughness != nullptr && roughness->category == "Surface",
                "Property category を HLSL から読む");
        }
        if (pixelate && pixelate->schema)
        {
            const ShaderProperty* size = pixelate->schema->FindByName("PixelSize");
            check.Expect(size != nullptr && size->minimum == 1.0f &&
                size->maximum == 64.0f, "Pixelate range schema を保持する");
        }

        // ---- Shader 切替 / Property 保持 ------------------------------------
        MaterialAsset material;
        check.Expect(pbr && MaterialSchema::SelectShader(material, *pbr),
            "Material へ PBR を選択できる");
        check.Expect(material.shader_guid == BuiltInShaders::Pbr.ToString() &&
            material.shading_model == 1, "PBR 選択を GUID と legacy field へ同期する");
        check.Expect(material.properties.Find("prop.BaseColor") != nullptr,
            "PBR default property を自動生成する");
        check.Expect(material.properties.Find("prop.BaseMap") != nullptr &&
            material.properties.Find("prop.BaseMap")->Type() ==
                Reflection::PropertyType::AssetReference,
            "Texture property は AssetGUID 型で保存する");

        material.properties.Set("prop.BaseColor",
            Reflection::PropertyValue::MakeColor(DirectX::XMFLOAT4{ 0.2f, 0.3f, 0.4f, 1.0f }));
        material.properties.Set("prop.Roughness",
            Reflection::PropertyValue::MakeFloat(0.13f));
        material.properties.Set("prop.BaseMap",
            Reflection::PropertyValue::MakeAssetReference(
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
        material.properties.Set("prop.UnknownFromOldShader",
            Reflection::PropertyValue::MakeString("keep-me"));

        check.Expect(toon && MaterialSchema::SelectShader(material, *toon),
            "Material へ Toon を選択できる");
        check.Expect(material.shader_guid == BuiltInShaders::Toon.ToString() &&
            material.shading_model == 2, "Toon を GUID Picker で選択できる");
        check.Expect(material.properties.Find("prop.RampMap") != nullptr,
            "Toon 固有 property を default から追加する");
        check.Expect(material.properties.Find("prop.Roughness") != nullptr &&
            ReadFloat(material, "prop.Roughness") > 0.12f &&
            ReadFloat(material, "prop.Roughness") < 0.14f,
            "Shader 切替で旧 Shader 固有値を捨てない");
        check.Expect(material.properties.Find("prop.UnknownFromOldShader") != nullptr &&
            material.properties.Find("prop.UnknownFromOldShader")->AsString() == "keep-me",
            "未知 Property を保持する");

        check.Expect(pixelate && MaterialSchema::SelectShader(material, *pixelate),
            "Material へ Pixelate を選択できる");
        check.Expect(material.shader_guid == BuiltInShaders::Pixelate.ToString() &&
            material.shading_model == 4, "Pixelate を GUID Picker で選択できる");
        check.Expect(ReadFloat(material, "prop.PixelSize") > 5.99f &&
            ReadFloat(material, "prop.PixelSize") < 6.01f,
            "Pixelate default property を Schema から生成する");

        check.Expect(pbr && MaterialSchema::SelectShader(material, *pbr),
            "PBR へ戻せる");
        check.Expect(ReadFloat(material, "prop.Roughness") > 0.12f &&
            ReadFloat(material, "prop.Roughness") < 0.14f,
            "PBR -> Toon -> Pixelate -> PBR で値が復元される");

        // ---- 選択/Texture/未知値の Save / Restart 相当 ----------------------
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "replay_shader_editor_validation";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        std::string error;

        const fs::path pbr_roundtrip_path = dir / "pbr_roundtrip.replaymaterial";
        check.Expect(MaterialAsset::Save(material, pbr_roundtrip_path, error),
            "選択 Shader/Property/Texture を Material へ保存できる");
        MaterialAsset pbr_roundtrip;
        check.Expect(MaterialAsset::Load(pbr_roundtrip_path, pbr_roundtrip, error),
            "保存 Material を再起動相当で読み直せる");
        check.Expect(pbr_roundtrip.shader_guid == BuiltInShaders::Pbr.ToString(),
            "Shader Picker の選択を Save/Reload で保持する");
        check.Expect(ReadFloat(pbr_roundtrip, "prop.Roughness") > 0.12f &&
            ReadFloat(pbr_roundtrip, "prop.Roughness") < 0.14f,
            "Shader Property 値を Save/Reload で保持する");
        const Reflection::PropertyValue* roundtrip_texture =
            pbr_roundtrip.properties.Find("prop.BaseMap");
        check.Expect(roundtrip_texture != nullptr &&
            roundtrip_texture->Type() == Reflection::PropertyType::AssetReference &&
            roundtrip_texture->AsString() == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "Texture AssetGUID を Save/Reload で保持する");
        check.Expect(pbr_roundtrip.properties.Find("prop.UnknownFromOldShader") != nullptr,
            "未知 Property も Save/Reload で保持する");

        // ---- Missing Shader / Save / Reload --------------------------------
        const std::string missing_guid = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
        material.shader_guid = missing_guid;
        material.properties.Set("prop.MissingShaderValue",
            Reflection::PropertyValue::MakeFloat(7.25f));
        const std::size_t before_missing = material.properties.Size();
        check.Expect(!MaterialSchema::SelectShader(material, library.Catalog(), missing_guid),
            "Missing Shader を別 Shader へ勝手に丸めない");
        check.Expect(material.shader_guid == missing_guid &&
            material.properties.Size() == before_missing,
            "Missing Shader で Material data を変更しない");

        const fs::path material_path = dir / "missing.replaymaterial";
        check.Expect(MaterialAsset::Save(material, material_path, error),
            "Missing Shader Material を保存できる");
        MaterialAsset reloaded;
        check.Expect(MaterialAsset::Load(material_path, reloaded, error),
            "Missing Shader Material を再読込できる");
        check.Expect(reloaded.shader_guid == missing_guid,
            "Missing Shader GUID を Save/Reload で保持する");
        check.Expect(reloaded.properties.Find("prop.MissingShaderValue") != nullptr &&
            reloaded.properties.Find("prop.MissingShaderValue")->AsFloat() > 7.24f,
            "Missing Shader の Property を Save/Reload で保持する");

        // ---- Shader Asset 作成 ---------------------------------------------
        const fs::path custom_path = dir / "CustomSurface.hlsl";
        ShaderID custom_id;
        check.Expect(ShaderAssetFactory::CreateSurfaceShader(custom_path,
            "Custom Surface", "Project/Test", custom_id, error),
            "Surface Shader Asset を Atomic Create できる");
        check.Expect(custom_id.IsValid() && fs::exists(custom_path),
            "作成 Shader に固定 GUID が入る");
        const std::string custom_source = ReadText(custom_path);
        check.Expect(custom_source.find("#define REPLAY_MATERIAL_PROPERTIES 1") !=
            std::string::npos,
            "作成 Shader が b9/t40+ Material Property 経路を使う");

        bool needs_guid = true;
        const ShaderSource::ParseResult parsed =
            ShaderSource::ParseFile(custom_path, needs_guid);
        check.Expect(parsed.succeeded && !needs_guid && parsed.info.id == custom_id,
            "作成 Shader Asset を再解析できる");
        check.Expect(parsed.info.domain == ShaderDomain::Surface &&
            parsed.info.category == "Project/Test",
            "作成 Shader の domain/category を保持する");
        check.Expect(parsed.info.properties.size() == 4,
            "作成 Shader に Schema-driven property が入る");
        if (!parsed.info.properties.empty())
        {
            check.Expect(parsed.info.properties.front().category == "Surface",
                "作成 Shader の Property category を保持する");
        }

        // Custom Shader を Catalog へ入れれば Editor C++ の変更なしに選べる。
        ShaderSourceInfo custom_info = parsed.info;
        std::uint32_t buffer_size = 0;
        ShaderConstantPacker::AssignOffsets(custom_info.properties, buffer_size);
        ShaderCatalog::Entry custom_entry;
        custom_entry.info = custom_info;
        custom_entry.schema = std::make_shared<ShaderPropertySchema>(
            custom_info.id, custom_info.properties, 1);
        library.Catalog().Register(std::move(custom_entry));
        const ShaderCatalog::Entry* custom = library.Catalog().Find(custom_id);
        check.Expect(custom != nullptr && custom->schema != nullptr,
            "Custom Shader を Catalog/Pickable に追加できる");

        MaterialAsset custom_material;
        check.Expect(custom && MaterialSchema::SelectShader(custom_material, *custom),
            "Custom Shader を Material へ選択できる");
        check.Expect(custom_material.shader_guid == custom_id.ToString() &&
            custom_material.properties.Find("prop.BaseColor") != nullptr,
            "Custom Shader も Built-in と同じ Material 経路を使う");
        custom_material.properties.Set("prop.CustomRetained",
            Reflection::PropertyValue::MakeFloat(3.5f));
        const fs::path custom_material_path = dir / "custom.replaymaterial";
        check.Expect(MaterialAsset::Save(custom_material, custom_material_path, error),
            "Custom Shader Material を保存できる");
        MaterialAsset custom_reloaded;
        check.Expect(MaterialAsset::Load(custom_material_path, custom_reloaded, error) &&
            custom_reloaded.shader_guid == custom_id.ToString() &&
            custom_reloaded.properties.Find("prop.CustomRetained") != nullptr,
            "Custom Shader GUID/Property を Save/Reload で保持する");

        // Tooltip の parser も今後の直感的 UI 用に固定する。
        bool tooltip_needs_guid = true;
        const auto tooltip_parse = ShaderSource::ParseText(
            "#pragma replay_guid \"11111111111111111111111111111111\"\n"
            "#pragma replay_domain surface\n"
            "#pragma replay_lighting unlit\n"
            "#pragma property range Power \"Power\" 0..8 = 2 category \"Rim\" tooltip \"Edge intensity\"\n",
            "tooltip_test.hlsl", tooltip_needs_guid);
        check.Expect(tooltip_parse.succeeded &&
            tooltip_parse.info.properties.size() == 1 &&
            tooltip_parse.info.properties[0].category == "Rim" &&
            tooltip_parse.info.properties[0].tooltip == "Edge intensity",
            "Property category/tooltip metadata を解析できる");

        fs::remove_all(dir, ec);
        return check.Report();
    }
}
