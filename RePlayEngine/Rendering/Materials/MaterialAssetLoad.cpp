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
    namespace Detail
    {
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
                // 上限を直値で持つと組み込みシェーダを足すたびに読めなくなる。
                // FlatFill(5) を足したときに実際に踏んだので表と連動させる。
                BuiltInShaders::FromShadingModel(value.shading_model).IsValid()))
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
    }

    namespace
    {
        using Detail::Finite;

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
        if (version < 5)
            loaded.properties.Set("prop.NormalizedRamp",
                Reflection::PropertyValue::MakeBool(false));
        if (!Finite(loaded))
        {
            error = "Materialに範囲外または非有限の値があります";
            return false;
        }
        material = std::move(loaded);
        return true;
    }
}
