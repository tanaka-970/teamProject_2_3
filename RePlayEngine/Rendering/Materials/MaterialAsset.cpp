#include "MaterialAsset.h"
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
        bool Expect(std::istream& stream, const char* expected, std::string& error)
        {
            std::string token;
            if (!(stream >> token) || token != expected)
            {
                error = std::string("Materialの項目が不正です: ") + expected;
                return false;
            }
            return true;
        }

        bool Finite(const MaterialAsset& value) noexcept
        {
            const float values[]{ value.base_color.x, value.base_color.y,
                value.base_color.z, value.base_color.w, value.metallic, value.roughness,
                value.emissive.x, value.emissive.y, value.emissive.z,
                value.emissive_strength, value.ambient_occlusion, value.alpha_cutoff };
            for (const float item : values) if (!std::isfinite(item)) return false;
            if (!(value.metallic >= 0.0f && value.metallic <= 1.0f &&
                value.roughness >= 0.0f && value.roughness <= 1.0f &&
                value.ambient_occlusion >= 0.0f && value.ambient_occlusion <= 1.0f &&
                value.alpha_cutoff >= 0.0f && value.alpha_cutoff <= 1.0f &&
                value.emissive_strength >= 0.0f &&
                value.shading_model >= 0 && value.shading_model <= 4))
                return false;

            for (const Reflection::PropertyBag::Entry& entry : value.properties.Entries())
                if (!entry.value.IsFinite()) return false;

            std::unordered_set<std::uint64_t> layer_ids;
            for (const ShaderLayer& layer : value.layers.Layers())
            {
                if (layer.id == 0 ||
                    layer.id == (std::numeric_limits<std::uint64_t>::max)() ||
                    !layer_ids.insert(layer.id).second ||
                    !layer.EffectiveShader().IsValid())
                    return false;
                const float layer_values[]{ layer.opacity, layer.strength, layer.parameter,
                    layer.tint.x, layer.tint.y, layer.tint.z, layer.tint.w };
                for (const float item : layer_values) if (!std::isfinite(item)) return false;
                for (const Reflection::PropertyBag::Entry& entry : layer.properties.Entries())
                    if (!entry.value.IsFinite()) return false;
            }
            return true;
        }


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

        bool ReadProperty(std::istream& stream, Reflection::PropertyBag& bag, std::string& error)
        {
            using namespace Reflection;
            std::string name, type_text;
            if (!(stream >> std::quoted(name) >> type_text)) { error="Material property header is invalid"; return false; }
            PropertyType type{};
            if (!TryParsePropertyType(type_text, type)) { error="Material property type is unknown: "+type_text; return false; }
            PropertyValue value;
            switch(type)
            {
            case PropertyType::Bool: { int x; if(!(stream>>x)) return false; value=PropertyValue::MakeBool(x!=0); break; }
            case PropertyType::Int: { int x; if(!(stream>>x)) return false; value=PropertyValue::MakeInt(x); break; }
            case PropertyType::Enum: { int x; if(!(stream>>x)) return false; value=PropertyValue::MakeEnum(x); break; }
            case PropertyType::CollisionLayer: { int x; if(!(stream>>x)) return false; value=PropertyValue::MakeCollisionLayer(x); break; }
            case PropertyType::CollisionMask: { int x; if(!(stream>>x)) return false; value=PropertyValue::MakeCollisionMask(x); break; }
            case PropertyType::ColliderReference: { int x; if(!(stream>>x)) return false; value=PropertyValue::MakeColliderReference(x); break; }
            case PropertyType::Int64: { std::int64_t x; if(!(stream>>x)) return false; value=PropertyValue::MakeInt64(x); break; }
            case PropertyType::UInt64: { std::uint64_t x; if(!(stream>>x)) return false; value=PropertyValue::MakeUInt64(x); break; }
            case PropertyType::Float: { float x; if(!(stream>>x)) return false; value=PropertyValue::MakeFloat(x); break; }
            case PropertyType::Double: { double x; if(!(stream>>x)) return false; value=PropertyValue::MakeDouble(x); break; }
            case PropertyType::String: { std::string x; if(!(stream>>std::quoted(x))) return false; value=PropertyValue::MakeString(x); break; }
            case PropertyType::AssetPath: { std::string x; if(!(stream>>std::quoted(x))) return false; value=PropertyValue::MakeAssetPath(x); break; }
            case PropertyType::AssetReference: { std::string x; if(!(stream>>std::quoted(x))) return false; value=PropertyValue::MakeAssetReference(x); break; }
            case PropertyType::SceneReference: { std::string x; if(!(stream>>std::quoted(x))) return false; value=PropertyValue::MakeSceneReference(x); break; }
            case PropertyType::Vector2: { DirectX::XMFLOAT2 x; if(!(stream>>x.x>>x.y)) return false; value=PropertyValue::MakeVector2(x); break; }
            case PropertyType::Vector3: { DirectX::XMFLOAT3 x; if(!(stream>>x.x>>x.y>>x.z)) return false; value=PropertyValue::MakeVector3(x); break; }
            case PropertyType::Vector4: { DirectX::XMFLOAT4 x; if(!(stream>>x.x>>x.y>>x.z>>x.w)) return false; value=PropertyValue::MakeVector4(x); break; }
            case PropertyType::Quaternion: { DirectX::XMFLOAT4 x; if(!(stream>>x.x>>x.y>>x.z>>x.w)) return false; value=PropertyValue::MakeQuaternion(x); break; }
            case PropertyType::Color: { DirectX::XMFLOAT4 x; if(!(stream>>x.x>>x.y>>x.z>>x.w)) return false; value=PropertyValue::MakeColor(x); break; }
            default: error="Material property type is not supported in v3"; return false;
            }
            bag.Set(std::move(name), std::move(value));
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

    bool MaterialAsset::Load(const std::filesystem::path& path,
        MaterialAsset& material, std::string& error)
    {
        error.clear();
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            error = "Materialを開けません";
            return false;
        }

        std::string magic;
        int version = 0;
        MaterialAsset loaded;
        int alpha = 0;
        int double_sided_value = 0;
        bool legacy_outline_pass = false;
        // version 1 も読む。層構造が無いだけで、それ以外は同じ並び。
        // 古い .replaymaterial を開けなくすると、
        // 既に作ってあるアセットが全部失われる。
        if (!(stream >> magic >> version) || magic != "REPLAY_MATERIAL" ||
            version < 1 || version > current_version ||
            !Expect(stream, "BASE_COLOR", error) ||
            !(stream >> loaded.base_color.x >> loaded.base_color.y >>
                loaded.base_color.z >> loaded.base_color.w) ||
            !Expect(stream, "BASE_COLOR_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.base_color_texture)) ||
            !Expect(stream, "NORMAL_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.normal_texture)) ||
            !Expect(stream, "METALLIC", error) || !(stream >> loaded.metallic) ||
            !Expect(stream, "METALLIC_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.metallic_texture)) ||
            !Expect(stream, "ROUGHNESS", error) || !(stream >> loaded.roughness) ||
            !Expect(stream, "ROUGHNESS_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.roughness_texture)) ||
            !Expect(stream, "EMISSIVE", error) ||
            !(stream >> loaded.emissive.x >> loaded.emissive.y >>
                loaded.emissive.z >> loaded.emissive_strength) ||
            !Expect(stream, "EMISSIVE_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.emissive_texture)) ||
            !Expect(stream, "AMBIENT_OCCLUSION", error) ||
            !(stream >> loaded.ambient_occlusion) ||
            !Expect(stream, "AMBIENT_OCCLUSION_TEXTURE", error) ||
            !(stream >> std::quoted(loaded.ambient_occlusion_texture)) ||
            !Expect(stream, "ALPHA", error) ||
            !(stream >> alpha >> loaded.alpha_cutoff) ||
            !Expect(stream, "DOUBLE_SIDED", error) || !(stream >> double_sided_value) ||
            !Expect(stream, "SHADING_MODEL", error) || !(stream >> loaded.shading_model))
        {
            if (error.empty()) error = "Materialの内容を読み取れません";
            return false;
        }

        // ---- version 3 の追加分 -------------------------------------------
        if (version >= 3)
        {
            std::size_t property_count = 0;
            if (!Expect(stream, "SHADER_GUID", error) ||
                !(stream >> std::quoted(loaded.shader_guid)) ||
                !Expect(stream, "PROPERTY_COUNT", error) || !(stream >> property_count))
            {
                if (error.empty()) error = "Material v3 header could not be read";
                return false;
            }
            if (property_count > 4096) { error = "Material property count exceeds limit"; return false; }
            for (std::size_t index=0; index<property_count; ++index)
            {
                if (!Expect(stream, "PROPERTY", error) || !ReadProperty(stream, loaded.properties, error))
                {
                    if (error.empty()) error = "Material property could not be read";
                    return false;
                }
            }
        }
        else
        {
            const ShaderID migrated = BuiltInShaders::FromShadingModel(loaded.shading_model);
            if (!migrated.IsValid())
            {
                error = "Unknown legacy shading_model: " + std::to_string(loaded.shading_model);
                return false;
            }
            loaded.shader_guid = migrated.ToString();
            loaded.SyncLegacyFieldsToProperties();
        }

        // ---- version 2 の追加分 -------------------------------------------
        if (version >= 2)
        {
            int outline_value = 0;
            std::size_t layer_count = 0;
            if (!Expect(stream, "PIXELATE", error) ||
                !(stream >> loaded.pixelate_grid >> loaded.pixelate_strength) ||
                !Expect(stream, "OUTLINE_PASS", error) || !(stream >> outline_value) ||
                !Expect(stream, "LAYER_COUNT", error) || !(stream >> layer_count))
            {
                if (error.empty()) error = "Materialの層構造を読み取れません";
                return false;
            }
            legacy_outline_pass = outline_value != 0;

            if (layer_count > ShaderLayerStack::MaxLayers)
            {
                error = "Materialの層数が上限を超えています";
                return false;
            }

            for (std::size_t index = 0; index < layer_count; ++index)
            {
                int blend = 0;
                int enabled = 0;
                ShaderLayer source{};

                if (version >= 4)
                {
                    std::string shader_guid;
                    std::uint64_t persistent_id = 0;
                    int legacy_type = -1;
                    if (!Expect(stream, "LAYER4", error) ||
                        !(stream >> std::quoted(shader_guid) >> persistent_id >> legacy_type >> blend >> enabled >>
                            source.opacity >> source.strength >> source.parameter >>
                            source.tint.x >> source.tint.y >> source.tint.z >> source.tint.w))
                    {
                        if (error.empty()) error = "Material v4 layer header could not be read";
                        return false;
                    }
                    if (blend < static_cast<int>(ShaderLayerBlend::Alpha) ||
                        blend > static_cast<int>(ShaderLayerBlend::Multiply))
                    {
                        error = "Material layer blend is invalid";
                        return false;
                    }

                    ShaderID layer_shader;
                    if (!ShaderID::TryParse(shader_guid, layer_shader) || !layer_shader.IsValid())
                    {
                        error = "Material layer ShaderGUID is invalid: " + shader_guid;
                        return false;
                    }
                    if (persistent_id == 0 ||
                        persistent_id == (std::numeric_limits<std::uint64_t>::max)() ||
                        loaded.layers.ContainsID(persistent_id))
                    {
                        error = "Material layer persistent ID is invalid or duplicated";
                        return false;
                    }
                    ShaderLayer& added = loaded.layers.AddWithID(layer_shader, persistent_id);
                    added.blend = static_cast<ShaderLayerBlend>(blend);
                    added.enabled = enabled != 0;
                    added.opacity = source.opacity;
                    added.strength = source.strength;
                    added.parameter = source.parameter;
                    added.tint = source.tint;
                    if (legacy_type >= 0 && legacy_type <=
                        static_cast<int>(ShaderLayerType::StylizedCharacter))
                    {
                        // GUID が正本だが、旧特殊パスの見た目維持に使う。
                        added.type = static_cast<ShaderLayerType>(legacy_type);
                    }

                    std::size_t property_count = 0;
                    if (!Expect(stream, "LAYER_PROPERTY_COUNT", error) ||
                        !(stream >> property_count) || property_count > 4096)
                    {
                        if (error.empty()) error = "Material layer property count is invalid";
                        return false;
                    }
                    added.properties.Clear();
                    for (std::size_t property_index = 0;
                        property_index < property_count; ++property_index)
                    {
                        if (!Expect(stream, "PROPERTY", error) ||
                            !ReadProperty(stream, added.properties, error))
                        {
                            if (error.empty()) error = "Material layer property could not be read";
                            return false;
                        }
                    }
                    added.SyncPropertiesToLegacyFields();
                }
                else
                {
                    int type = 0;
                    if (!Expect(stream, "LAYER", error) ||
                        !(stream >> type >> blend >> enabled >> source.opacity >>
                            source.strength >> source.parameter >>
                            source.tint.x >> source.tint.y >> source.tint.z >> source.tint.w))
                    {
                        if (error.empty()) error = "Materialの層を読み取れません";
                        return false;
                    }
                    if (type < static_cast<int>(ShaderLayerType::Pbr) ||
                        type > static_cast<int>(ShaderLayerType::StylizedCharacter) ||
                        blend < static_cast<int>(ShaderLayerBlend::Alpha) ||
                        blend > static_cast<int>(ShaderLayerBlend::Multiply))
                    {
                        error = "Materialの層の列挙値が不正です";
                        return false;
                    }

                    ShaderLayer& added =
                        loaded.layers.Add(static_cast<ShaderLayerType>(type));
                    added.blend = static_cast<ShaderLayerBlend>(blend);
                    added.enabled = enabled != 0;
                    added.opacity = source.opacity;
                    added.strength = source.strength;
                    added.parameter = source.parameter;
                    added.tint = source.tint;
                    added.SyncLegacyFieldsToProperties();
                }
            }
        }

        // v2/v3 の outline_pass は Shader-owned bool だった。v4 では Outline Layer
        // へ 1 回だけ移行し、以後の正本を LayerStack にする。
        if (legacy_outline_pass &&
            !loaded.layers.Contains(BuiltInShaderLayers::Outline) &&
            loaded.layers.CanAdd())
        {
            ShaderLayer& outline = loaded.layers.Add(BuiltInShaderLayers::Outline);
            outline.SyncLegacyFieldsToProperties();
        }

        if (!Expect(stream, "END_MATERIAL", error))
        {
            if (error.empty()) error = "Materialの終端が見つかりません";
            return false;
        }
        if (alpha < static_cast<int>(MaterialAlphaMode::Opaque) ||
            alpha > static_cast<int>(MaterialAlphaMode::Blend) ||
            (double_sided_value != 0 && double_sided_value != 1))
        {
            error = "Materialの列挙値が不正です";
            return false;
        }
        loaded.alpha_mode = static_cast<MaterialAlphaMode>(alpha);
        loaded.double_sided = double_sided_value != 0;
        if (version >= 3) loaded.SyncPropertiesToLegacyFields();
        else loaded.SyncLegacyFieldsToProperties();
        if (!Finite(loaded))
        {
            error = "Materialに範囲外または非有限の値があります";
            return false;
        }
        material = std::move(loaded);
        return true;
    }
}
