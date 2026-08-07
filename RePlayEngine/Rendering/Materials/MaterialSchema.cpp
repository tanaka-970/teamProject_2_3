#include "MaterialSchema.h"

#include "../Shaders/BuiltInShaders.h"
#include "../Shaders/ShaderCatalog.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ReplayEngine::Rendering
{
    namespace
    {
        bool NormalizeValue(const Reflection::PropertyValue& source,
            const ShaderProperty& property, Reflection::PropertyValue& out)
        {
            using Reflection::PropertyType;

            switch (property.kind)
            {
            case ShaderPropertyKind::Float:
            case ShaderPropertyKind::Range:
            {
                if (source.Type() == PropertyType::Float)
                {
                    out = source;
                    return true;
                }
                Reflection::PropertyValue converted;
                if (source.ConvertTo(PropertyType::Float, converted))
                {
                    out = std::move(converted);
                    return true;
                }
                return false;
            }

            case ShaderPropertyKind::Float2:
            {
                if (source.Type() == PropertyType::Vector2)
                {
                    out = source;
                    return true;
                }
                Reflection::PropertyValue converted;
                if (source.ConvertTo(PropertyType::Vector2, converted))
                {
                    out = std::move(converted);
                    return true;
                }
                return false;
            }

            case ShaderPropertyKind::Float3:
            {
                if (source.Type() == PropertyType::Vector3)
                {
                    out = source;
                    return true;
                }
                Reflection::PropertyValue converted;
                if (source.ConvertTo(PropertyType::Vector3, converted))
                {
                    out = std::move(converted);
                    return true;
                }
                return false;
            }

            case ShaderPropertyKind::Float4:
            {
                if (source.Type() == PropertyType::Vector4)
                {
                    out = source;
                    return true;
                }
                Reflection::PropertyValue converted;
                if (source.ConvertTo(PropertyType::Vector4, converted))
                {
                    out = std::move(converted);
                    return true;
                }
                return false;
            }

            case ShaderPropertyKind::Color:
            {
                if (source.Type() == PropertyType::Color)
                {
                    out = source;
                    return true;
                }
                if (source.Type() == PropertyType::Vector4)
                {
                    out = Reflection::PropertyValue::MakeColor(source.AsVector4());
                    return true;
                }
                return false;
            }

            case ShaderPropertyKind::Texture:
            {
                if (source.Type() == PropertyType::AssetReference ||
                    source.Type() == PropertyType::AssetPath ||
                    source.Type() == PropertyType::String)
                {
                    out = Reflection::PropertyValue::MakeAssetReference(source.AsString());
                    return true;
                }
                return false;
            }

            case ShaderPropertyKind::Toggle:
            {
                if (source.Type() == PropertyType::Bool)
                {
                    out = source;
                    return true;
                }
                if (source.Type() == PropertyType::Float)
                {
                    out = Reflection::PropertyValue::MakeBool(source.AsFloat() != 0.0f);
                    return true;
                }
                if (source.Type() == PropertyType::Int ||
                    source.Type() == PropertyType::Enum)
                {
                    out = Reflection::PropertyValue::MakeBool(source.AsInt() != 0);
                    return true;
                }
                return false;
            }

            case ShaderPropertyKind::Enum:
            {
                int value = 0;
                if (source.Type() == PropertyType::Enum ||
                    source.Type() == PropertyType::Int)
                {
                    value = source.AsInt();
                }
                else if (source.Type() == PropertyType::Float)
                {
                    value = static_cast<int>(std::lround(source.AsFloat()));
                }
                else
                {
                    return false;
                }

                if (!property.enum_names.empty())
                {
                    value = (std::max)(0, (std::min)(value,
                        static_cast<int>(property.enum_names.size()) - 1));
                }
                out = Reflection::PropertyValue::MakeEnum(value);
                return true;
            }

            default:
                return false;
            }
        }

        void SyncLegacyBuiltInShader(MaterialAsset& material, ShaderID id)
        {
            for (const BuiltInShaders::Definition& definition : BuiltInShaders::All())
            {
                if (definition.id == id)
                {
                    material.shading_model = definition.shading_model;
                    return;
                }
            }
        }
    }

    Reflection::PropertyType MaterialSchema::PropertyTypeFor(
        ShaderPropertyKind kind) noexcept
    {
        using Reflection::PropertyType;
        switch (kind)
        {
        case ShaderPropertyKind::Float:
        case ShaderPropertyKind::Range:   return PropertyType::Float;
        case ShaderPropertyKind::Float2:  return PropertyType::Vector2;
        case ShaderPropertyKind::Float3:  return PropertyType::Vector3;
        case ShaderPropertyKind::Float4:  return PropertyType::Vector4;
        case ShaderPropertyKind::Color:   return PropertyType::Color;
        case ShaderPropertyKind::Texture: return PropertyType::AssetReference;
        case ShaderPropertyKind::Toggle:  return PropertyType::Bool;
        case ShaderPropertyKind::Enum:    return PropertyType::Enum;
        default:                          return PropertyType::Float;
        }
    }

    Reflection::PropertyValue MaterialSchema::DefaultValueFor(
        const ShaderProperty& property)
    {
        using Reflection::PropertyValue;
        switch (property.kind)
        {
        case ShaderPropertyKind::Float:
        case ShaderPropertyKind::Range:
            return PropertyValue::MakeFloat(property.default_value.x);
        case ShaderPropertyKind::Float2:
            return PropertyValue::MakeVector2({
                property.default_value.x, property.default_value.y });
        case ShaderPropertyKind::Float3:
            return PropertyValue::MakeVector3({
                property.default_value.x, property.default_value.y,
                property.default_value.z });
        case ShaderPropertyKind::Float4:
            return PropertyValue::MakeVector4(property.default_value);
        case ShaderPropertyKind::Color:
            return PropertyValue::MakeColor(property.default_value);
        case ShaderPropertyKind::Texture:
            // white / black / bump 等は GPU 側の fallback 名であり AssetGUID ではない。
            // Material には空 GUID を保存して既定 Texture を使う。
            return PropertyValue::MakeAssetReference(std::string());
        case ShaderPropertyKind::Toggle:
            return PropertyValue::MakeBool(property.default_value.x != 0.0f);
        case ShaderPropertyKind::Enum:
        {
            int value = static_cast<int>(std::lround(property.default_value.x));
            if (!property.enum_names.empty())
            {
                value = (std::max)(0, (std::min)(value,
                    static_cast<int>(property.enum_names.size()) - 1));
            }
            return PropertyValue::MakeEnum(value);
        }
        default:
            return PropertyValue::MakeFloat(0.0f);
        }
    }

    bool MaterialSchema::EnsureProperties(MaterialAsset& material,
        const ShaderPropertySchema& schema)
    {
        bool changed = false;
        for (const ShaderProperty& property : schema.Properties())
        {
            const std::string saved = property.SavedName();
            const Reflection::PropertyValue* existing = material.properties.Find(saved);
            if (existing == nullptr)
            {
                material.properties.Set(saved, DefaultValueFor(property));
                changed = true;
                continue;
            }

            Reflection::PropertyValue normalized;
            if (NormalizeValue(*existing, property, normalized) &&
                !Reflection::ValuesEqual(*existing, normalized))
            {
                material.properties.Set(saved, std::move(normalized));
                changed = true;
            }
        }

        if (changed) material.SyncPropertiesToLegacyFields();
        return changed;
    }

    bool MaterialSchema::SelectShader(MaterialAsset& material,
        const ShaderCatalog::Entry& entry)
    {
        if (!entry.info.id.IsValid() || entry.info.domain != ShaderDomain::Surface)
            return false;

        material.shader_guid = entry.info.id.ToString();
        SyncLegacyBuiltInShader(material, entry.info.id);

        if (entry.schema) EnsureProperties(material, *entry.schema);
        material.SyncPropertiesToLegacyFields();
        return true;
    }

    bool MaterialSchema::SelectShader(MaterialAsset& material,
        const ShaderCatalog& catalog, const std::string& shader_guid)
    {
        ShaderID id;
        if (!ShaderID::TryParse(shader_guid, id) || !id.IsValid()) return false;
        const ShaderCatalog::Entry* entry = catalog.Find(id);
        if (entry == nullptr) return false;
        return SelectShader(material, *entry);
    }
}
