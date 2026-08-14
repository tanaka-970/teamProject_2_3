// Material Asset のうち、互換 Property 同期と replaymaterial への保存だけを持つ。
//
//   MaterialAsset.cpp      ... Property 同期と保存（このファイル）
//   MaterialAssetLoad.cpp  ... replaymaterial の読み込み
//   MaterialAssetInternal.h ... 分割した保存・読み込みだけが共有する検証宣言

#include "MaterialAsset.h"
#include "MaterialAssetInternal.h"
#include "../Shaders/BuiltInShaders.h"

#include <windows.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace ReplayEngine::Rendering
{
    namespace
    {
        using Detail::Finite;

        void SetLegacyProperties(MaterialAsset& value)
        {
            using Reflection::PropertyValue;
            value.properties.Set("prop.BaseColor", PropertyValue::MakeColor(value.base_color));
            value.properties.Set("prop.BaseMap", PropertyValue::MakeAssetReference(value.base_color_texture));
            value.properties.Set("prop.NormalMap", PropertyValue::MakeAssetReference(value.normal_texture));
            value.properties.Set("prop.Metallic", PropertyValue::MakeFloat(value.metallic));
            value.properties.Set("prop.MetallicMap", PropertyValue::MakeAssetReference(value.metallic_texture));
            value.properties.Set("prop.Roughness", PropertyValue::MakeFloat(value.roughness));
            value.properties.Set("prop.RoughnessMap", PropertyValue::MakeAssetReference(value.roughness_texture));
            value.properties.Set("prop.Emissive", PropertyValue::MakeVector3(value.emissive));
            value.properties.Set("prop.EmissiveStrength", PropertyValue::MakeFloat(value.emissive_strength));
            value.properties.Set("prop.EmissiveMap", PropertyValue::MakeAssetReference(value.emissive_texture));
            value.properties.Set("prop.AmbientOcclusion", PropertyValue::MakeFloat(value.ambient_occlusion));
            value.properties.Set("prop.OcclusionMap", PropertyValue::MakeAssetReference(value.ambient_occlusion_texture));
            value.properties.Set("prop.AlphaMode", PropertyValue::MakeEnum(static_cast<int>(value.alpha_mode)));
            value.properties.Set("prop.AlphaCutoff", PropertyValue::MakeFloat(value.alpha_cutoff));
            value.properties.Set("prop.DoubleSided", PropertyValue::MakeBool(value.double_sided));
            value.properties.Set("prop.PixelSize", PropertyValue::MakeFloat(value.pixelate_grid));
            value.properties.Set("prop.PixelateStrength", PropertyValue::MakeFloat(value.pixelate_strength));
        }

        template<class F>
        void ApplyIf(const Reflection::PropertyBag& bag, const char* name, F apply)
        {
            if (const Reflection::PropertyValue* value = bag.Find(name)) apply(*value);
        }

        bool WriteProperty(std::ostream& stream, const Reflection::PropertyBag::Entry& entry)
        {
            using Reflection::PropertyType;
            const auto& v = entry.value;
            stream << "PROPERTY " << std::quoted(entry.name) << ' '
                << Reflection::ToString(v.Type()) << ' ';
            switch (v.Type())
            {
            case PropertyType::Bool: stream << (v.AsBool() ? 1 : 0); break;
            case PropertyType::Int:
            case PropertyType::Enum:
            case PropertyType::CollisionLayer:
            case PropertyType::CollisionMask:
            case PropertyType::ColliderReference: stream << v.AsInt(); break;
            case PropertyType::Int64: stream << v.AsInt64(); break;
            case PropertyType::UInt64: stream << v.AsUInt64(); break;
            case PropertyType::Float: stream << v.AsFloat(); break;
            case PropertyType::Double: stream << v.AsDouble(); break;
            case PropertyType::String:
            case PropertyType::AssetPath:
            case PropertyType::AssetReference:
            case PropertyType::SceneReference: stream << std::quoted(v.AsString()); break;
            case PropertyType::Vector2: { const auto x=v.AsVector2(); stream<<x.x<<' '<<x.y; break; }
            case PropertyType::Vector3: { const auto x=v.AsVector3(); stream<<x.x<<' '<<x.y<<' '<<x.z; break; }
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
            case PropertyType::Color: { const auto x=v.AsVector4(); stream<<x.x<<' '<<x.y<<' '<<x.z<<' '<<x.w; break; }
            default: return false;
            }
            stream << '\n';
            return true;
        }

    }

    void MaterialAsset::SyncLegacyFieldsToProperties()
    {
        SetLegacyProperties(*this);
    }

    void MaterialAsset::SyncPropertiesToLegacyFields()
    {
        ApplyIf(properties, "prop.BaseColor", [this](const Reflection::PropertyValue& v){ base_color=v.AsVector4(); });
        ApplyIf(properties, "prop.BaseMap", [this](const Reflection::PropertyValue& v){ base_color_texture=v.AsString(); });
        ApplyIf(properties, "prop.NormalMap", [this](const Reflection::PropertyValue& v){ normal_texture=v.AsString(); });
        ApplyIf(properties, "prop.Metallic", [this](const Reflection::PropertyValue& v){ metallic=v.AsFloat(metallic); });
        ApplyIf(properties, "prop.MetallicMap", [this](const Reflection::PropertyValue& v){ metallic_texture=v.AsString(); });
        ApplyIf(properties, "prop.Roughness", [this](const Reflection::PropertyValue& v){ roughness=v.AsFloat(roughness); });
        ApplyIf(properties, "prop.RoughnessMap", [this](const Reflection::PropertyValue& v){ roughness_texture=v.AsString(); });
        ApplyIf(properties, "prop.Emissive", [this](const Reflection::PropertyValue& v){ emissive=v.AsVector3(); });
        ApplyIf(properties, "prop.EmissiveStrength", [this](const Reflection::PropertyValue& v){ emissive_strength=v.AsFloat(emissive_strength); });
        ApplyIf(properties, "prop.EmissiveMap", [this](const Reflection::PropertyValue& v){ emissive_texture=v.AsString(); });
        ApplyIf(properties, "prop.AmbientOcclusion", [this](const Reflection::PropertyValue& v){ ambient_occlusion=v.AsFloat(ambient_occlusion); });
        if (const Reflection::PropertyValue* value = properties.Find("prop.OcclusionMap"))
            ambient_occlusion_texture = value->AsString();
        else
            ApplyIf(properties, "prop.AmbientOcclusionMap", [this](const Reflection::PropertyValue& v){ ambient_occlusion_texture=v.AsString(); });
        ApplyIf(properties, "prop.AlphaMode", [this](const Reflection::PropertyValue& v){ alpha_mode=static_cast<MaterialAlphaMode>(v.AsInt(static_cast<int>(alpha_mode))); });
        ApplyIf(properties, "prop.AlphaCutoff", [this](const Reflection::PropertyValue& v){ alpha_cutoff=v.AsFloat(alpha_cutoff); });
        ApplyIf(properties, "prop.DoubleSided", [this](const Reflection::PropertyValue& v){ double_sided=v.AsBool(double_sided); });
        ApplyIf(properties, "prop.PixelSize", [this](const Reflection::PropertyValue& v){ pixelate_grid=v.AsFloat(pixelate_grid); });
        ApplyIf(properties, "prop.PixelateStrength", [this](const Reflection::PropertyValue& v){ pixelate_strength=v.AsFloat(pixelate_strength); });
    }

    bool MaterialAsset::Save(const MaterialAsset& material,
        const std::filesystem::path& path, std::string& error)
    {
        error.clear();
        if (path.empty())
        {
            error = "Materialの保存先が空です";
            return false;
        }
        if (!Finite(material))
        {
            error = "Materialに範囲外または非有限の値があります";
            return false;
        }

        std::error_code filesystem_error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error)
        {
            error = "Material保存フォルダーを作成できません";
            return false;
        }

        std::filesystem::path temporary = path;
        temporary += L".tmp";
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "Material一時ファイルを作成できません";
            return false;
        }
        MaterialAsset normalized = material;
        if (normalized.shader_guid.empty())
            normalized.shader_guid = BuiltInShaders::FromShadingModel(normalized.shading_model).ToString();

        // v3 以降は PropertyBag が surface の正本。
        // v4 では Layer も ShaderGUID + PropertyBag が正本になる。
        // 互換 bridge は既存 7 layer の見た目を移行中も変えないためだけに同期する。
        normalized.SyncPropertiesToLegacyFields();
        normalized.SyncLegacyFieldsToProperties();
        for (ShaderLayer& layer : normalized.layers.Layers())
        {
            if (!layer.shader.IsValid()) layer.shader = layer.EffectiveShader();
            layer.SyncPropertiesToLegacyFields();
            layer.SyncLegacyFieldsToProperties();
        }

        stream << std::setprecision(std::numeric_limits<float>::max_digits10);
        stream << "REPLAY_MATERIAL " << current_version << '\n';
        stream << "BASE_COLOR " << normalized.base_color.x << ' ' << normalized.base_color.y
            << ' ' << normalized.base_color.z << ' ' << normalized.base_color.w << '\n';
        stream << "BASE_COLOR_TEXTURE " << std::quoted(normalized.base_color_texture) << '\n';
        stream << "NORMAL_TEXTURE " << std::quoted(normalized.normal_texture) << '\n';
        stream << "METALLIC " << normalized.metallic << '\n';
        stream << "METALLIC_TEXTURE " << std::quoted(normalized.metallic_texture) << '\n';
        stream << "ROUGHNESS " << normalized.roughness << '\n';
        stream << "ROUGHNESS_TEXTURE " << std::quoted(normalized.roughness_texture) << '\n';
        stream << "EMISSIVE " << normalized.emissive.x << ' ' << normalized.emissive.y
            << ' ' << normalized.emissive.z << ' ' << normalized.emissive_strength << '\n';
        stream << "EMISSIVE_TEXTURE " << std::quoted(normalized.emissive_texture) << '\n';
        stream << "AMBIENT_OCCLUSION " << normalized.ambient_occlusion << '\n';
        stream << "AMBIENT_OCCLUSION_TEXTURE "
            << std::quoted(normalized.ambient_occlusion_texture) << '\n';
        stream << "ALPHA " << static_cast<int>(normalized.alpha_mode) << ' '
            << normalized.alpha_cutoff << '\n';
        stream << "DOUBLE_SIDED " << (normalized.double_sided ? 1 : 0) << '\n';
        stream << "SHADING_MODEL " << normalized.shading_model << '\n';
        stream << "SHADER_GUID " << std::quoted(normalized.shader_guid) << '\n';
        stream << "PROPERTY_COUNT " << normalized.properties.Size() << '\n';
        for (const Reflection::PropertyBag::Entry& entry : normalized.properties.Entries())
        {
            if (!WriteProperty(stream, entry))
            {
                stream.close();
                std::filesystem::remove(temporary, filesystem_error);
                error = "Material property has unsupported serialization type: " + entry.name;
                return false;
            }
        }

        // ---- v4 Layer Asset ---------------------------------------------
        // Layer の種類は enum ではなく ShaderGUID。PropertyBag も各 Layer 自身が持つ。
        // legacy_type / 固定値は旧レンダラとの bridge で、新規 Layer の追加条件ではない。
        stream << "PIXELATE " << normalized.pixelate_grid << ' '
            << normalized.pixelate_strength << '\n';
        // v2/v3 reader compatibility token。正本は LayerStack。
        stream << "OUTLINE_PASS "
            << (normalized.layers.Contains(BuiltInShaderLayers::Outline) ? 1 : 0) << '\n';
        stream << "LAYER_COUNT " << normalized.layers.Layers().size() << '\n';
        for (const ShaderLayer& layer : normalized.layers.Layers())
        {
            int legacy_type = -1;
            std::uint32_t legacy = 0;
            if (BuiltInShaderLayers::TryGetLegacyType(layer.EffectiveShader(), legacy))
                legacy_type = static_cast<int>(legacy);

            stream << "LAYER4 " << std::quoted(layer.EffectiveShader().ToString()) << ' '
                << layer.id << ' ' << legacy_type << ' '
                << static_cast<int>(layer.blend) << ' '
                << (layer.enabled ? 1 : 0) << ' '
                << layer.opacity << ' ' << layer.strength << ' ' << layer.parameter << ' '
                << layer.tint.x << ' ' << layer.tint.y << ' '
                << layer.tint.z << ' ' << layer.tint.w << '\n';
            stream << "LAYER_PROPERTY_COUNT " << layer.properties.Size() << '\n';
            for (const Reflection::PropertyBag::Entry& entry : layer.properties.Entries())
            {
                if (!WriteProperty(stream, entry))
                {
                    stream.close();
                    std::filesystem::remove(temporary, filesystem_error);
                    error = "Layer property has unsupported serialization type: " + entry.name;
                    return false;
                }
            }
        }
        stream << "END_MATERIAL\n";
        stream.flush();
        if (!stream)
        {
            stream.close();
            std::filesystem::remove(temporary, filesystem_error);
            error = "Materialを書き込み中に失敗しました";
            return false;
        }
        stream.close();

        if (std::filesystem::exists(path, filesystem_error) && !filesystem_error)
        {
            std::filesystem::path backup = path;
            backup += L".bak";
            std::filesystem::copy_file(path, backup,
                std::filesystem::copy_options::overwrite_existing, filesystem_error);
            if (filesystem_error)
            {
                std::filesystem::remove(temporary, filesystem_error);
                error = "Materialバックアップを作成できません";
                return false;
            }
        }

        if (!MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::filesystem::remove(temporary, filesystem_error);
            error = "Material一時ファイルを置換できません";
            return false;
        }
        return true;
    }

}
