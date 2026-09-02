#include "MaterialBinding.h"

#include "../Shaders/BuiltInShaders.h"
#include "../Shaders/ShaderCatalog.h"
#include "../Shaders/ShaderConstantPacker.h"

#include <algorithm>
#include <utility>

namespace ReplayEngine::Rendering
{
    namespace
    {
        struct LookupContext final
        {
            const Reflection::PropertyBag* bag = nullptr;
            const ShaderPropertySchema* schema = nullptr;
        };

        bool ReadStringProperty(const Reflection::PropertyBag& bag,
            const std::string& saved_name, std::string& out)
        {
            const Reflection::PropertyValue* value = bag.Find(saved_name);
            if (value == nullptr) return false;

            using Reflection::PropertyType;
            switch (value->Type())
            {
            case PropertyType::AssetReference:
            case PropertyType::AssetPath:
            case PropertyType::String:
                out = value->AsString();
                return true;
            default:
                return false;
            }
        }

        bool ConvertConstant(const Reflection::PropertyValue& value,
            ShaderPropertyKind kind, DirectX::XMFLOAT4& out)
        {
            out = {};
            switch (kind)
            {
            case ShaderPropertyKind::Float:
            case ShaderPropertyKind::Range:
                if (value.Type() != Reflection::PropertyType::Float) return false;
                out.x = value.AsFloat();
                return true;

            case ShaderPropertyKind::Toggle:
                if (value.Type() == Reflection::PropertyType::Bool)
                {
                    out.x = value.AsBool() ? 1.0f : 0.0f;
                    return true;
                }
                if (value.Type() == Reflection::PropertyType::Float)
                {
                    out.x = value.AsFloat();
                    return true;
                }
                return false;

            case ShaderPropertyKind::Enum:
                if (value.Type() == Reflection::PropertyType::Enum ||
                    value.Type() == Reflection::PropertyType::Int)
                {
                    out.x = static_cast<float>(value.AsInt());
                    return true;
                }
                if (value.Type() == Reflection::PropertyType::Float)
                {
                    out.x = value.AsFloat();
                    return true;
                }
                return false;

            case ShaderPropertyKind::Float2:
                if (value.Type() != Reflection::PropertyType::Vector2) return false;
                {
                    const DirectX::XMFLOAT2 v = value.AsVector2();
                    out = { v.x, v.y, 0.0f, 0.0f };
                }
                return true;

            case ShaderPropertyKind::Float3:
                if (value.Type() != Reflection::PropertyType::Vector3) return false;
                {
                    const DirectX::XMFLOAT3 v = value.AsVector3();
                    out = { v.x, v.y, v.z, 0.0f };
                }
                return true;

            case ShaderPropertyKind::Float4:
                if (value.Type() != Reflection::PropertyType::Vector4) return false;
                out = value.AsVector4();
                return true;

            case ShaderPropertyKind::Color:
                if (value.Type() != Reflection::PropertyType::Color) return false;
                out = value.AsVector4();
                return true;

            case ShaderPropertyKind::Texture:
                return false;
            default:
                return false;
            }
        }

        bool VariantHasUsableBytecode(const ShaderCatalog::Entry& entry,
            ShaderVariant variant) noexcept
        {
            if (!entry.UsesVariant(variant)) return false;
            const ShaderCatalog::VariantResult& result = entry.At(variant);
            // 直近のコンパイルが失敗していても、bytecode は最後に成功したものを
            // 保持する契約。compiled ではなく bytecode の有無で描画可否を決める。
            return result.bytecode != nullptr;
        }
    }

    bool ResolvedMaterialBinding::TryGetGBufferBridgeSlot(
        const std::string& property_name, std::uint32_t& out_slot) noexcept
    {
        if (property_name == "BaseMap") out_slot = 40;
        else if (property_name == "NormalMap") out_slot = 41;
        else if (property_name == "MetallicMap") out_slot = 42;
        else if (property_name == "RoughnessMap") out_slot = 43;
        else if (property_name == "EmissiveMap") out_slot = 44;
        else if (property_name == "OcclusionMap" ||
            property_name == "AmbientOcclusionMap") out_slot = 45;
        else if (property_name == "RampMap") out_slot = 46;
        else return false;
        return true;
    }

    std::uint32_t ResolvedMaterialBinding::TextureSemanticMask() const noexcept
    {
        std::uint32_t mask = 0;
        for (const ResolvedMaterialTexture& texture : textures)
        {
            if (texture.property_name == "BaseMap")
                mask |= BaseMapSemantic;
            else if (texture.property_name == "NormalMap")
                mask |= NormalMapSemantic;
            else if (texture.property_name == "MetallicMap")
                mask |= MetallicMapSemantic;
            else if (texture.property_name == "RoughnessMap")
                mask |= RoughnessMapSemantic;
            else if (texture.property_name == "EmissiveMap")
                mask |= EmissiveMapSemantic;
            else if (texture.property_name == "OcclusionMap" ||
                texture.property_name == "AmbientOcclusionMap")
                mask |= OcclusionMapSemantic;
            else if (texture.property_name == "RampMap")
                mask |= RampMapSemantic;
        }
        return mask;
    }

    bool MaterialBindingResolver::LookupConstant(const std::string& saved_name,
        DirectX::XMFLOAT4& out, void* user)
    {
        const auto* context = static_cast<const LookupContext*>(user);
        if (context == nullptr || context->bag == nullptr || context->schema == nullptr)
            return false;

        const ShaderProperty* property = context->schema->FindBySavedName(saved_name);
        if (property == nullptr || property->kind == ShaderPropertyKind::Texture)
            return false;

        const Reflection::PropertyValue* value = context->bag->Find(saved_name);
        if (value == nullptr) return false;
        return ConvertConstant(*value, property->kind, out);
    }

    void MaterialBindingResolver::BuildTextures(const MaterialAsset& material,
        const ShaderPropertySchema& schema,
        std::vector<ResolvedMaterialTexture>& out)
    {
        out.clear();
        out.reserve(schema.TextureCount());

        for (const ShaderProperty& property : schema.Properties())
        {
            if (property.kind != ShaderPropertyKind::Texture) continue;

            ResolvedMaterialTexture texture;
            texture.property_name = property.name;
            texture.slot = property.texture_slot;
            texture.default_texture = property.default_texture.empty()
                ? std::string("white") : property.default_texture;

            ReadStringProperty(material.properties, property.SavedName(),
                texture.asset_guid);

            // Material v3 初期実装には OcclusionMap と
            // AmbientOcclusionMap の表記揺れがあった。保存値を捨てず、
            // 新しい Schema 名から旧保存名も 1 回だけ救済する。
            if (texture.asset_guid.empty() && property.name == "OcclusionMap")
            {
                ReadStringProperty(material.properties,
                    "prop.AmbientOcclusionMap", texture.asset_guid);
            }

            out.push_back(std::move(texture));
        }

        std::sort(out.begin(), out.end(),
            [](const ResolvedMaterialTexture& a,
                const ResolvedMaterialTexture& b)
            {
                return a.slot < b.slot;
            });
    }

    bool MaterialBindingResolver::ResolveEntry(const MaterialAsset& material,
        const ShaderCatalog& catalog, ShaderID requested, ShaderVariant variant,
        ResolvedMaterialBinding& out)
    {
        const ShaderCatalog::Entry* entry = catalog.Find(requested);
        if (entry == nullptr)
        {
            MakeMissingFallback(material, catalog, variant, requested,
                "Shader CatalogにGUIDがありません", out);
            return out.usable_shader;
        }
        if (entry->info.domain != ShaderDomain::Surface)
        {
            MakeMissingFallback(material, catalog, variant, requested,
                "Materialにはsurface domainのShaderだけを設定できます", out);
            return out.usable_shader;
        }
        if (!entry->schema)
        {
            MakeMissingFallback(material, catalog, variant, requested,
                "Shader Schemaがありません", out);
            return out.usable_shader;
        }
        if (!VariantHasUsableBytecode(*entry, variant))
        {
            MakeMissingFallback(material, catalog, variant, requested,
                std::string(ToString(variant)) + "変種に成功済みbytecodeがありません",
                out);
            return out.usable_shader;
        }

        out.requested_shader = requested;
        out.shader = requested;
        out.variant = variant;
        out.schema = entry->schema;
        out.layers = &material.layers;
        out.lighting_model = entry->info.lighting_model;
        out.missing_shader = false;
        out.usable_shader = true;
        out.diagnostic.clear();

        LookupContext context{ &material.properties, entry->schema.get() };
        ShaderConstantPacker::Pack(*entry->schema,
            &MaterialBindingResolver::LookupConstant, &context, out.constants);
        BuildTextures(material, *entry->schema, out.textures);
        return true;
    }

    void MaterialBindingResolver::MakeMissingFallback(
        const MaterialAsset& material, const ShaderCatalog& catalog,
        ShaderVariant variant, ShaderID requested, const std::string& reason,
        ResolvedMaterialBinding& out)
    {
        out = {};
        out.requested_shader = requested;
        out.variant = variant;
        out.missing_shader = true;
        out.diagnostic = reason;
        out.layers = &material.layers;
        out.lighting_model = ShaderLightingModel::Unlit;

        const ShaderCatalog::Entry* fallback = catalog.Find(BuiltInShaders::Unlit);
        if (fallback == nullptr || !fallback->schema ||
            !VariantHasUsableBytecode(*fallback, variant))
        {
            out.usable_shader = false;
            out.diagnostic += "; Unlit fallbackも利用できません";
            return;
        }

        // 元の Material は一切書き換えない。描画用の一時 PropertyBag だけへ
        // magenta を入れ、Shader が戻れば元の値でそのまま復帰できるようにする。
        MaterialAsset fallback_material = material;
        fallback_material.properties.Set("prop.BaseColor",
            Reflection::PropertyValue::MakeColor(
                DirectX::XMFLOAT4{ 1.0f, 0.0f, 1.0f, 1.0f }));
        fallback_material.properties.Set("prop.BaseMap",
            Reflection::PropertyValue::MakeAssetReference(std::string()));

        out.shader = BuiltInShaders::Unlit;
        out.schema = fallback->schema;
        out.usable_shader = true;

        LookupContext context{ &fallback_material.properties, fallback->schema.get() };
        ShaderConstantPacker::Pack(*fallback->schema,
            &MaterialBindingResolver::LookupConstant, &context, out.constants);
        BuildTextures(fallback_material, *fallback->schema, out.textures);
    }

    bool MaterialBindingResolver::Resolve(const MaterialAsset& material,
        const ShaderCatalog& catalog, ShaderVariant variant,
        ResolvedMaterialBinding& out)
    {
        out = {};

        ShaderID requested;
        if (!material.shader_guid.empty())
        {
            if (!ShaderID::TryParse(material.shader_guid, requested))
            {
                MakeMissingFallback(material, catalog, variant, requested,
                    "Shader GUIDを解析できません: " + material.shader_guid, out);
                return out.usable_shader;
            }
        }
        else
        {
            requested = BuiltInShaders::FromShadingModel(material.shading_model);
            if (!requested.IsValid())
            {
                MakeMissingFallback(material, catalog, variant, requested,
                    "旧shading_modelに対応する組み込みShaderがありません", out);
                return out.usable_shader;
            }
        }

        return ResolveEntry(material, catalog, requested, variant, out);
    }
}
