#include "ShaderLayerBinding.h"

#include "../Shaders/ShaderCatalog.h"
#include "../Shaders/ShaderConstantPacker.h"

#include <algorithm>

namespace ReplayEngine::Rendering
{
    namespace
    {
        struct LayerLookupContext final
        {
            const Reflection::PropertyBag* bag = nullptr;
            const ShaderPropertySchema* schema = nullptr;
        };

        bool ConvertConstant(const Reflection::PropertyValue& value,
            ShaderPropertyKind kind, DirectX::XMFLOAT4& out)
        {
            using Reflection::PropertyType;
            out = {};
            switch (kind)
            {
            case ShaderPropertyKind::Float:
            case ShaderPropertyKind::Range:
                if (value.Type() != PropertyType::Float) return false;
                out.x = value.AsFloat();
                return true;
            case ShaderPropertyKind::Toggle:
                if (value.Type() == PropertyType::Bool)
                {
                    out.x = value.AsBool() ? 1.0f : 0.0f;
                    return true;
                }
                if (value.Type() == PropertyType::Float)
                {
                    out.x = value.AsFloat();
                    return true;
                }
                return false;
            case ShaderPropertyKind::Enum:
                if (value.Type() == PropertyType::Enum || value.Type() == PropertyType::Int)
                {
                    out.x = static_cast<float>(value.AsInt());
                    return true;
                }
                if (value.Type() == PropertyType::Float)
                {
                    out.x = value.AsFloat();
                    return true;
                }
                return false;
            case ShaderPropertyKind::Float2:
                if (value.Type() != PropertyType::Vector2) return false;
                {
                    const auto v = value.AsVector2();
                    out = { v.x, v.y, 0.0f, 0.0f };
                }
                return true;
            case ShaderPropertyKind::Float3:
                if (value.Type() != PropertyType::Vector3) return false;
                {
                    const auto v = value.AsVector3();
                    out = { v.x, v.y, v.z, 0.0f };
                }
                return true;
            case ShaderPropertyKind::Float4:
                if (value.Type() != PropertyType::Vector4) return false;
                out = value.AsVector4();
                return true;
            case ShaderPropertyKind::Color:
                if (value.Type() != PropertyType::Color && value.Type() != PropertyType::Vector4)
                    return false;
                out = value.AsVector4();
                return true;
            case ShaderPropertyKind::Texture:
            default:
                return false;
            }
        }

        bool Lookup(const std::string& saved_name, DirectX::XMFLOAT4& out, void* user)
        {
            const auto* context = static_cast<const LayerLookupContext*>(user);
            if (context == nullptr || context->bag == nullptr || context->schema == nullptr)
                return false;
            const ShaderProperty* property = context->schema->FindBySavedName(saved_name);
            if (property == nullptr || property->kind == ShaderPropertyKind::Texture)
                return false;
            const Reflection::PropertyValue* value = context->bag->Find(saved_name);
            return value != nullptr && ConvertConstant(*value, property->kind, out);
        }

        bool ReadStringProperty(const Reflection::PropertyBag& bag,
            const std::string& saved_name, std::string& out)
        {
            const Reflection::PropertyValue* value = bag.Find(saved_name);
            if (value == nullptr) return false;
            using Reflection::PropertyType;
            if (value->Type() != PropertyType::AssetReference &&
                value->Type() != PropertyType::AssetPath &&
                value->Type() != PropertyType::String)
                return false;
            out = value->AsString();
            return true;
        }

        void BuildTextures(const Reflection::PropertyBag& bag,
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
                ReadStringProperty(bag, property.SavedName(), texture.asset_guid);
                out.push_back(std::move(texture));
            }
            std::sort(out.begin(), out.end(),
                [](const ResolvedMaterialTexture& a, const ResolvedMaterialTexture& b)
                {
                    return a.slot < b.slot;
                });
        }
    }

    bool ShaderLayerBindingResolver::Resolve(const ShaderLayer& layer,
        const ShaderCatalog& catalog, ShaderVariant variant,
        ResolvedMaterialBinding& out)
    {
        out = {};
        const ShaderID requested = layer.EffectiveShader();
        out.requested_shader = requested;
        out.shader = requested;
        out.variant = variant;

        if (!requested.IsValid())
        {
            out.missing_shader = true;
            out.diagnostic = "Layer ShaderGUID がありません";
            return false;
        }

        const ShaderCatalog::Entry* entry = catalog.Find(requested);
        if (entry == nullptr)
        {
            out.missing_shader = true;
            out.diagnostic = "Layer Shader が Catalog にありません: " + requested.ToString();
            return false;
        }
        if (entry->info.domain != ShaderDomain::Layer)
        {
            out.diagnostic = "Layer に surface/postprocess Shader は使用できません";
            return false;
        }
        if (!entry->schema)
        {
            out.diagnostic = "Layer Shader Schema がありません";
            return false;
        }
        if (!entry->UsesVariant(variant))
        {
            out.diagnostic = std::string("Layer Shader が ") + ToString(variant) +
                " 変種を持っていません";
            return false;
        }

        const ShaderCatalog::VariantResult& compiled = entry->At(variant);
        if (!compiled.bytecode)
        {
            out.diagnostic = std::string("Layer Shader の ") + ToString(variant) +
                " bytecode がありません";
            return false;
        }

        out.schema = entry->schema;
        out.usable_shader = true;
        out.lighting_model = entry->info.lighting_model;

        LayerLookupContext context{ &layer.properties, entry->schema.get() };
        ShaderConstantPacker::Pack(*entry->schema, &Lookup, &context, out.constants);
        BuildTextures(layer.properties, *entry->schema, out.textures);
        return true;
    }
}
