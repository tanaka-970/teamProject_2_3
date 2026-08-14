#include "ShaderRenderValidation.h"
#include "BuiltInShaders.h"
#include "ShaderLibrary.h"
#include "../Materials/MaterialBinding.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace ReplayEngine::Rendering::Validation
{
    namespace
    {
        class Check final
        {
        public:
            void Expect(bool value, const char* text)
            {
                ++total_; if (value) return; ++failed_;
                std::fprintf(stderr, "  [FAIL %d] %s\n", 1400 + total_ - 1, text);
            }
            int Report() const
            {
                std::fprintf(stderr, "shader-render %s: %d checks, %d failed\n",
                    failed_ == 0 ? "OK" : "FAILED", total_, failed_);
                return failed_ == 0 ? 0 : 1400;
            }
        private: int total_ = 0; int failed_ = 0;
        };

        float ReadFloat(const ResolvedMaterialBinding& binding,
            const char* property_name)
        {
            if (!binding.schema) return -9999.0f;
            const ShaderProperty* property = binding.schema->FindByName(property_name);
            if (!property || property->constant_size < 4 ||
                property->constant_offset + 4 > binding.constants.size()) return -9999.0f;
            float value = 0.0f;
            std::memcpy(&value, binding.constants.data() + property->constant_offset, 4);
            return value;
        }

        DirectX::XMFLOAT4 ReadFloat4(const ResolvedMaterialBinding& binding,
            const char* property_name)
        {
            DirectX::XMFLOAT4 value{ -9999.0f, -9999.0f, -9999.0f, -9999.0f };
            if (!binding.schema) return value;
            const ShaderProperty* property = binding.schema->FindByName(property_name);
            if (!property || property->constant_size < sizeof(value) ||
                property->constant_offset + sizeof(value) > binding.constants.size())
                return value;
            std::memcpy(&value,
                binding.constants.data() + property->constant_offset, sizeof(value));
            return value;
        }
    }

    int RunShaderRenderValidation()
    {
        Check check;
        ShaderLibrary library;
        const ShaderLibrary::ScanReport report =
            library.ScanAll(std::filesystem::current_path());
        check.Expect(report.compile_failed == 0, "実Shader Catalogを構築できる");

        MaterialAsset material;
        material.shader_guid = BuiltInShaders::Pbr.ToString();
        material.SyncLegacyFieldsToProperties();
        material.properties.Set("prop.Metallic",
            Reflection::PropertyValue::MakeFloat(0.37f));
        material.properties.Set("prop.Roughness",
            Reflection::PropertyValue::MakeFloat(0.63f));
        material.properties.Set("prop.BaseMap",
            Reflection::PropertyValue::MakeAssetReference("base-guid"));
        ShaderLayer& pixel = material.layers.Add(ShaderLayerType::Pixelate);
        pixel.parameter = 7.0f;
        material.layers.Add(ShaderLayerType::Outline).enabled = false;

        ResolvedMaterialBinding static_binding;
        check.Expect(MaterialBindingResolver::Resolve(material, library.Catalog(),
            ShaderVariant::Static, static_binding), "PBR Staticを解決できる");
        check.Expect(static_binding.shader == BuiltInShaders::Pbr,
            "MaterialのShaderIDがRender bindingへ届く");
        check.Expect(static_binding.variant == ShaderVariant::Static,
            "Static変種を選ぶ");
        check.Expect(static_binding.schema != nullptr, "Schemaを保持する");
        check.Expect(static_binding.usable_shader, "成功bytecodeを使用可能と判定する");
        check.Expect(static_binding.lighting_model == ShaderLightingModel::Pbr,
            "replay_lightingがbindingへ届く");
        check.Expect(std::fabs(ReadFloat(static_binding, "Metallic") - 0.37f) < 0.0001f,
            "PropertyBagのMetallicをb9 byte列へPackする");
        check.Expect(std::fabs(ReadFloat(static_binding, "Roughness") - 0.63f) < 0.0001f,
            "PropertyBagのRoughnessをb9 byte列へPackする");
        check.Expect(static_binding.layers == &material.layers,
            "LayerStackをコピーせず借用する");
        check.Expect(static_binding.layers && static_binding.layers->Layers().size() == 2,
            "Layer順序とdisabled状態を保持する");
        check.Expect(static_binding.layers &&
            static_binding.layers->Layers()[0].type == ShaderLayerType::Pixelate &&
            !static_binding.layers->Layers()[1].enabled,
            "Layerの並びとenabledを変更しない");

        ResolvedMaterialBinding skinned_binding;
        check.Expect(MaterialBindingResolver::Resolve(material, library.Catalog(),
            ShaderVariant::Skinned, skinned_binding), "PBR Skinnedを解決できる");
        check.Expect(skinned_binding.variant == ShaderVariant::Skinned,
            "Skinned変種を選ぶ");

        MaterialAsset missing = material;
        missing.shader_guid = "ffffffffffffffffffffffffffffffff";
        missing.properties.Set("prop.UnknownFutureValue",
            Reflection::PropertyValue::MakeString("keep"));
        ResolvedMaterialBinding fallback;
        check.Expect(MaterialBindingResolver::Resolve(missing, library.Catalog(),
            ShaderVariant::Static, fallback), "Missing Shaderでも描画fallbackを作れる");
        check.Expect(fallback.missing_shader, "Missing Shader状態を明示する");
        check.Expect(fallback.shader == BuiltInShaders::Unlit,
            "Missing ShaderはUnlitへfallbackする");
        check.Expect(fallback.lighting_model == ShaderLightingModel::Unlit,
            "Missing ShaderはUnlit照明になる");
        const DirectX::XMFLOAT4 missing_color = ReadFloat4(fallback, "BaseColor");
        check.Expect(std::fabs(missing_color.x - 1.0f) < 0.0001f &&
            std::fabs(missing_color.y) < 0.0001f &&
            std::fabs(missing_color.z - 1.0f) < 0.0001f &&
            std::fabs(missing_color.w - 1.0f) < 0.0001f,
            "Missing Shaderは実際のb9にもMagentaをPackする");
        check.Expect(missing.properties.Find("prop.UnknownFutureValue") != nullptr,
            "Missing fallbackで元PropertyBagを破壊しない");

        return check.Report();
    }
}
