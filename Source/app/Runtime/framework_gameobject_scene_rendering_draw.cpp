// GameObject / Component 基盤のうち「Object / Landscape 描画」を持つ。
// 描画パス、Material binding、Depth/GBuffer 分岐は関数本体のまま移動している。
#include "framework.h"
#include "../../../RePlayEngine/Components/Rendering/ModelEffectStackComponent.h"

#include "gltf_model.h"
#include "skinned_mesh.h"

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
#include "../../RePlayEngine/Components/Motion/MotionPlayerComponent.h"
#include "../../RePlayEngine/Components/Core/PropertyLinkComponent.h"
#include "../../RePlayEngine/Components/Editor/EditorNoteComponent.h"
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/MaterialOverrideDynamicProperties.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/LineRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/TrailComponent.h"
#include "../../RePlayEngine/Components/Rendering/ParticleEmitterComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../RePlayEngine/Rendering/Shaders/BuiltInShaders.h"
#include "../../RePlayEngine/Rendering/Shaders/ShaderConstantPacker.h"
#include "../../RePlayEngine/Rendering/ShaderStack/BuiltInShaderLayers.h"
#include "../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../RePlayEngine/Project/ProjectSettingsSerializer.h"
#include "../../RePlayEngine/Rendering/Adapter/SceneRenderCollector.h"
#include "../../RePlayEngine/Motion/MotionBindingResolver.h"
#include "../../RePlayEngine/Motion/MotionEvaluator.h"
#include "../../RePlayEngine/UI/UILayout.h"
#include "../../RePlayEngine/Runtime/Events/EventBus.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypeCatalog.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypes.h"
#include "../../game/Behaviours/ValidationBehaviours.h"

#include <commdlg.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>


ReplayEngine::Rendering::RenderItem framework::resolve_render_item_material(
    const ReplayEngine::Rendering::RenderItem& source,
    const std::string* material_asset_override, bool apply_material_motion)
{
    using namespace ReplayEngine::Rendering;

    RenderItem item;
    if (material_asset_override == nullptr)
    {
        item = source;
    }
    else
    {
        item.cast_shadow = source.cast_shadow;
        item.receive_shadow = source.receive_shadow;
        // Slot 経路は source 全体をコピーしないので、Object の tint だけ引き継ぐ。
        item.tint = source.tint;
    }
    item.legacy_tint = source.tint;
    item.lighting_model = deferred_lighting_model(source.shading_model);

    const std::string& material_asset = material_asset_override != nullptr
        ? *material_asset_override : source.material_asset;
    item.material_asset = material_asset;
    const MaterialAsset* material = resolve_object_material(material_asset);
    const bool has_material_asset = material != nullptr;
    MaterialAsset fallback_material;
    if (!has_material_asset)
    {
        // Material Asset が無い Renderer でも、Renderer 側の描画方式と
        // material.* Motion は同じ解決経路へ流す。
        fallback_material.shading_model = source.shading_model;
        material = &fallback_material;
    }

    // 旧 .cso fallback では従来どおり Material の base_color を頂点 tint に使う。
    item.legacy_tint = has_material_asset
        ? (source.material_override ? source.tint : material->base_color)
        : source.tint;

    // Catalog shader では BaseColor は b9 から渡す。pin.color は Renderer 側の
    // 追加 tint にだけ使い、同じ色を二重に掛けない。
    item.tint = has_material_asset && !source.material_override
        ? DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f } : source.tint;

    item.shading_model = material->shading_model; // fallback のためだけに保持
    // material_override は従来どおり「Renderer tint が Material BaseColor を置換」。
    // Catalog shader は BaseColor を b9 から読むため、置換時は b9 側を白にする。
    item.material_base_color = source.material_override
        ? source.override_material_base_color : material->base_color;
    item.metallic = source.material_override
        ? source.override_material_metallic : material->metallic;
    item.roughness = source.material_override
        ? source.override_material_roughness : material->roughness;
    item.ambient_occlusion = source.material_override
        ? source.override_material_ambient_occlusion : material->ambient_occlusion;
    item.emissive_color = source.material_override
        ? source.override_material_emissive_color : material->emissive;
    item.emissive_strength = source.material_override
        ? source.override_material_emissive_strength : material->emissive_strength;
    item.double_sided = source.double_sided || material->double_sided ||
        (source.material_override && source.override_material_double_sided);
    item.outline = has_material_asset
        ? material->layers.Contains(BuiltInShaderLayers::Outline) : source.outline;
    item.pixelate_size = material->pixelate_grid;
    item.pixelate_strength = material->pixelate_strength;
    // 不透明度は legacy 欄が無いので Property から直接読む。
    if (const auto* opacity = material->properties.Find("prop.PixelateOpacity"))
        item.pixelate_opacity = opacity->AsFloat(item.pixelate_opacity);

    // 現在の GameObject mesh は静的提出も skinned_mesh renderer を通る。
    // Vertex Shader の VS_OUT と一致させるため Catalog 側も Skinned 変種を使う。
    // 真の static_mesh 経路を RenderItem へ接続した時点で source.skinned 分岐へ戻す。
    const ShaderVariant variant = ShaderVariant::Skinned;
    MaterialAsset binding_material = *material;
    if (source.material_override)
    {
        binding_material.base_color = source.override_material_base_color;
        binding_material.metallic = source.override_material_metallic;
        binding_material.roughness = source.override_material_roughness;
        binding_material.ambient_occlusion = source.override_material_ambient_occlusion;
        binding_material.emissive = source.override_material_emissive_color;
        binding_material.emissive_strength = source.override_material_emissive_strength;
        binding_material.double_sided = source.override_material_double_sided;
        binding_material.properties.Set("prop.BaseColor",
            ReplayEngine::Reflection::PropertyValue::MakeColor(
                source.override_material_base_color));
        binding_material.properties.Set("prop.Metallic",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_metallic));
        binding_material.properties.Set("prop.Roughness",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_roughness));
        binding_material.properties.Set("prop.AmbientOcclusion",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_ambient_occlusion));
        binding_material.properties.Set("prop.Emissive",
            ReplayEngine::Reflection::PropertyValue::MakeVector3(
                source.override_material_emissive_color));
        binding_material.properties.Set("prop.EmissiveStrength",
            ReplayEngine::Reflection::PropertyValue::MakeFloat(
                source.override_material_emissive_strength));
        binding_material.properties.Set("prop.DoubleSided",
            ReplayEngine::Reflection::PropertyValue::MakeBool(
                source.override_material_double_sided));
    }

    // Motion の material.* は Renderer の永続 material_override とは別物。
    // Asset を直接変更せず、この draw 用コピーにだけ重ねる。停止した次フレームには
    // PrepareMaterialMotionProperties が active mask / bag を消すため完全に元へ戻る。
    if (apply_material_motion)
    {
        using namespace ReplayEngine::Components;
        const std::uint32_t motion_mask = source.material_motion_fixed_mask;
        if ((motion_mask & MaterialMotionBaseColor) != 0)
        {
            item.material_base_color = source.override_material_base_color;
            binding_material.base_color = source.override_material_base_color;
            binding_material.properties.Set("prop.BaseColor",
                ReplayEngine::Reflection::PropertyValue::MakeColor(
                    source.override_material_base_color));
        }
        if ((motion_mask & MaterialMotionMetallic) != 0)
        {
            item.metallic = source.override_material_metallic;
            binding_material.metallic = source.override_material_metallic;
            binding_material.properties.Set("prop.Metallic",
                ReplayEngine::Reflection::PropertyValue::MakeFloat(
                    source.override_material_metallic));
        }
        if ((motion_mask & MaterialMotionRoughness) != 0)
        {
            item.roughness = source.override_material_roughness;
            binding_material.roughness = source.override_material_roughness;
            binding_material.properties.Set("prop.Roughness",
                ReplayEngine::Reflection::PropertyValue::MakeFloat(
                    source.override_material_roughness));
        }
        if ((motion_mask & MaterialMotionAmbientOcclusion) != 0)
        {
            item.ambient_occlusion = source.override_material_ambient_occlusion;
            binding_material.ambient_occlusion = source.override_material_ambient_occlusion;
            binding_material.properties.Set("prop.AmbientOcclusion",
                ReplayEngine::Reflection::PropertyValue::MakeFloat(
                    source.override_material_ambient_occlusion));
        }
        if ((motion_mask & MaterialMotionEmissiveColor) != 0)
        {
            item.emissive_color = source.override_material_emissive_color;
            binding_material.emissive = source.override_material_emissive_color;
            binding_material.properties.Set("prop.Emissive",
                ReplayEngine::Reflection::PropertyValue::MakeVector3(
                    source.override_material_emissive_color));
        }
        if ((motion_mask & MaterialMotionEmissiveStrength) != 0)
        {
            item.emissive_strength = source.override_material_emissive_strength;
            binding_material.emissive_strength = source.override_material_emissive_strength;
            binding_material.properties.Set("prop.EmissiveStrength",
                ReplayEngine::Reflection::PropertyValue::MakeFloat(
                    source.override_material_emissive_strength));
        }
        if ((motion_mask & MaterialMotionDoubleSided) != 0)
        {
            item.double_sided = source.double_sided || source.override_material_double_sided;
            binding_material.double_sided = source.override_material_double_sided;
            binding_material.properties.Set("prop.DoubleSided",
                ReplayEngine::Reflection::PropertyValue::MakeBool(
                    source.override_material_double_sided));
        }
        for (const ReplayEngine::Reflection::PropertyBag::Entry& entry :
            source.material_motion_properties.Entries())
        {
            binding_material.properties.Set(entry.name, entry.value);
        }

    }

    // Material Asset が無い場合は、Builtin Primitive の旧 shading_model を
    // そのまま使う。仮 Material を Shader Catalog へ解決すると、正常な
    // Primitive まで「欠落 Shader」と誤判定してマゼンタへ落ちる。
    if (!has_material_asset)
    {
        item.material_binding = {};
        item.pixelate_enabled = source.pixelate_enabled;
        return item;
    }

    const bool resolved = MaterialBindingResolver::Resolve(binding_material,
        shader_library.Catalog(), variant, item.material_binding);
    // binding_material は一時コピーなので、LayerStack の借用先だけ元Assetへ戻す。
    item.material_binding.layers = has_material_asset ? &material->layers : nullptr;

    if (resolved && item.material_binding.usable_shader)
    {
        item.lighting_model = item.material_binding.lighting_model;
        if (item.material_binding.requested_shader.IsValid())
            object_shader_lighting_failures.erase(
                item.material_binding.requested_shader.ToString());
    }
    else
    {
        item.lighting_model = deferred_lighting_model(material->shading_model);
    }

    if (item.material_binding.missing_shader)
    {
        // Deferred は generated b9 ではなく固定 GBuffer bridge を使うため、
        // Missing Shader のマゼンタをこちらにも明示的に反映する。
        item.material_base_color = DirectX::XMFLOAT4{ 1.0f, 0.0f, 1.0f, 1.0f };
        item.tint = DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f };
        item.emissive_color = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        item.emissive_strength = 0.0f;

        const std::string key = material->shader_guid.empty()
            ? std::string("legacy:") + std::to_string(material->shading_model)
            : material->shader_guid;
        if (object_shader_lighting_failures.insert(key).second)
        {
            push_editor_log("Error",
                "Material Shader を解決できないため Unlit/Magenta へフォールバック: " +
                key + " / " + item.material_binding.diagnostic);
        }
    }

    // Pixelate は surface shader と layer の両方から有効になる。
    item.pixelate_enabled = item.material_binding.shader == BuiltInShaders::Pixelate;
    if (!has_material_asset) return item;

    for (const ShaderLayer& layer : material->layers.Layers())
    {
        if (!layer.enabled) continue;
        if (layer.Is(BuiltInShaderLayers::Pixelate))
        {
            item.pixelate_enabled = true;
            item.pixelate_size = layer.parameter;
            item.pixelate_strength = layer.strength;
        }
        else if (layer.Is(BuiltInShaderLayers::Outline))
        {
            item.outline = true;
        }
    }
    return item;
}
bool framework::build_dx12_static_scene(
    ReplayEngine::Rendering::DX12::D3D12StaticSceneSubmission& submission,
    const ReplayEngine::Scene::Scene& scene,
    const ReplayEngine::Rendering::RenderItemList& render_items,
    float elapsed_time, dx12_scene_build_options options)
{
    using namespace ReplayEngine::Rendering;
    using namespace ReplayEngine::Rendering::DX12;

    // 呼出し側がフレームごとに選択した背景色とポストプロセス設定は、
    // Scene内の描画項目を集め直しても失ってはいけない。
    // 全体を初期化すると背景が常に既定の黒へ戻り、Inspectorを動かしても反映されない。
    const D3D12PostProcessSubmission selected_post_process = submission.post_process;
    const DirectX::XMFLOAT4 selected_background_color = submission.background_color;
    submission = {};
    submission.post_process = selected_post_process;
    submission.background_color = selected_background_color;
    constexpr std::uint32_t kNormalMapSemantic = 1u << 1;
    constexpr std::uint32_t kEmissiveMapSemantic = 1u << 4;
        // glTFはOcclusion/Roughness/Metalnessを1枚のTextureのR/G/Bへ格納する。
    constexpr std::uint32_t kPackedOrmMapSemantic = 1u << 6;

    std::unordered_set<std::string> mesh_source_keys;
    std::unordered_set<std::string> texture_source_keys;
    std::unordered_set<std::string> shader_source_keys;

    const auto multiply_color = [](const DirectX::XMFLOAT4& a,
        const DirectX::XMFLOAT4& b) noexcept
    {
        return DirectX::XMFLOAT4{ a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w };
    };
    const auto multiply_world = [](const DirectX::XMFLOAT4X4& local,
        const DirectX::XMFLOAT4X4& world) noexcept
    {
        DirectX::XMFLOAT4X4 result{};
        DirectX::XMStoreFloat4x4(&result,
            DirectX::XMLoadFloat4x4(&local) * DirectX::XMLoadFloat4x4(&world));
        return result;
    };
    const auto alpha_mode = [](int mode) noexcept
    {
        if (mode == 1) return D3D12StaticAlphaMode::Mask;
        if (mode == 2) return D3D12StaticAlphaMode::Blend;
        return D3D12StaticAlphaMode::Opaque;
    };
    const auto material_alpha_mode = [](MaterialAlphaMode mode) noexcept
    {
        switch (mode)
        {
        case MaterialAlphaMode::Mask: return D3D12StaticAlphaMode::Mask;
        case MaterialAlphaMode::Blend: return D3D12StaticAlphaMode::Blend;
        default: return D3D12StaticAlphaMode::Opaque;
        }
    };

    const auto add_texture = [this, &submission, &texture_source_keys](
        const std::filesystem::path& input_path) -> std::string
    {
        if (input_path.empty()) return {};
        std::filesystem::path resolved = input_path;
        if (resolved.is_relative())
        {
            std::error_code ec;
            if (!std::filesystem::exists(resolved, ec) || ec)
                resolved = content_path(resolved);
        }
        resolved = resolved.lexically_normal();
        const std::string key = resolved.generic_string();
        if (key.empty()) return {};
        if (!dx12_device_context.HasStaticTexture(key) &&
            texture_source_keys.insert(key).second)
        {
            D3D12StaticTextureSource source;
            source.key = key;
            source.source_path = resolved;
            submission.texture_sources.push_back(std::move(source));
        }
        return key;
    };

    const auto add_material_texture = [&add_texture](
        D3D12StaticDrawItem& draw, std::uint32_t slot,
        const std::filesystem::path& input_path, std::uint32_t semantic_bit)
    {
        const std::string key = add_texture(input_path);
        if (key.empty()) return;
        D3D12StaticMaterialTexture mapped;
        mapped.slot = slot;
        mapped.texture_key = key;
        draw.material_textures.push_back(std::move(mapped));
        draw.material_texture_semantic_mask |= semantic_bit;
    };

    const auto add_asset_texture = [this, &add_texture](
        const std::string& asset_guid) -> std::string
    {
        if (asset_guid.empty()) return {};
        const ReplayEngine::Assets::AssetRecord* record =
            asset_database.FindByGuid(asset_guid);
        if (record == nullptr) return {};
        return add_texture(content_path(record->source_path));
    };

    struct BaseTextureBinding final
    {
        std::string asset_guid;
        std::string fallback_key{ "__dx12_white" };
    };
    const auto fallback_texture_key = [](const std::string& fallback) -> std::string
    {
        if (fallback == "black") return "__dx12_black";
        if (fallback == "gray") return "__dx12_gray";
        if (fallback == "bump") return "__dx12_bump";
        return "__dx12_white";
    };
    const auto has_material_texture = [](const RenderItem& item,
        const MaterialAsset& material, const char* property_name,
        const std::string& legacy_texture) noexcept
    {
        if (!legacy_texture.empty()) return true;
        for (const ResolvedMaterialTexture& texture : item.material_binding.textures)
            if (texture.property_name == property_name && !texture.asset_guid.empty())
                return true;
        return false;
    };
    const auto set_material_texture = [&add_texture](D3D12StaticDrawItem& draw,
        std::uint32_t slot, const std::filesystem::path& path,
        std::uint32_t semantic_bit) -> std::string
    {
        const std::string key = add_texture(path);
        if (key.empty()) return {};
        for (D3D12StaticMaterialTexture& mapped : draw.material_textures)
        {
            if (mapped.slot != slot) continue;
            mapped.texture_key = key;
            draw.material_texture_semantic_mask |= semantic_bit;
            return key;
        }
        D3D12StaticMaterialTexture mapped;
        mapped.slot = slot;
        mapped.texture_key = key;
        draw.material_textures.push_back(std::move(mapped));
        draw.material_texture_semantic_mask |= semantic_bit;
        return key;
    };
    const auto preserve_embedded_material_textures = [
        &has_material_texture, &set_material_texture, &kPackedOrmMapSemantic](D3D12StaticDrawItem& draw,
        const RenderItem& material_item, const MaterialAsset& material,
        const std::filesystem::path& base_path,
        const std::filesystem::path& normal_path,
        const std::filesystem::path& orm_path,
        const std::filesystem::path& emissive_path)
    {
        if (!has_material_texture(material_item, material, "BaseMap",
            material.base_color_texture) && !base_path.empty())
        {
            const std::string key = set_material_texture(draw, 40u, base_path,
                ResolvedMaterialBinding::BaseMapSemantic);
            if (!key.empty()) draw.base_color_texture_key = key;
        }

        if (!has_material_texture(material_item, material, "NormalMap",
            material.normal_texture) && !normal_path.empty())
            set_material_texture(draw, 41u, normal_path,
                ResolvedMaterialBinding::NormalMapSemantic);

        const bool has_metallic = has_material_texture(material_item, material,
            "MetallicMap", material.metallic_texture);
        const bool has_roughness = has_material_texture(material_item, material,
            "RoughnessMap", material.roughness_texture);
        const bool has_occlusion = has_material_texture(material_item, material,
            "OcclusionMap", material.ambient_occlusion_texture) ||
            has_material_texture(material_item, material, "AmbientOcclusionMap",
                material.ambient_occlusion_texture);
        if (!has_metallic && !has_roughness && !has_occlusion && !orm_path.empty())
        {
            set_material_texture(draw, 42u, orm_path,
                ResolvedMaterialBinding::MetallicMapSemantic);
            draw.material_texture_semantic_mask &=
                ~(ResolvedMaterialBinding::MetallicMapSemantic |
                    ResolvedMaterialBinding::RoughnessMapSemantic |
                    ResolvedMaterialBinding::OcclusionMapSemantic);
            draw.material_texture_semantic_mask |= kPackedOrmMapSemantic;
        }

        if (!has_material_texture(material_item, material, "EmissiveMap",
            material.emissive_texture) && !emissive_path.empty())
            set_material_texture(draw, 44u, emissive_path,
                ResolvedMaterialBinding::EmissiveMapSemantic);
    };
    const auto base_texture_binding = [&fallback_texture_key](const RenderItem& item) -> BaseTextureBinding
    {
        for (const ResolvedMaterialTexture& texture : item.material_binding.textures)
        {
            if (texture.property_name != "BaseMap") continue;
            BaseTextureBinding binding;
            binding.asset_guid = texture.asset_guid;
            binding.fallback_key = fallback_texture_key(texture.default_texture);
            return binding;
        }
        return {};
    };

    const auto fill_external_material = [this, &submission, &shader_source_keys,
        &material_alpha_mode, &multiply_color, &add_asset_texture,
        &base_texture_binding, &fallback_texture_key](
        const RenderItem& source_item, const RenderItem& item,
        D3D12StaticDrawItem& draw, const MaterialAsset* resolved_material = nullptr) -> bool
    {
        const MaterialAsset* material = resolved_material != nullptr
            ? resolved_material : resolve_object_material(source_item.material_asset);
        const bool external = material != nullptr;
        draw.base_color = multiply_color(item.material_base_color, item.tint);
        draw.vertex_tint = item.tint;
        draw.emissive = item.emissive_color;
        draw.emissive_strength = item.emissive_strength;
        draw.metallic = item.metallic;
        draw.roughness = item.roughness;
        draw.ambient_occlusion = item.ambient_occlusion;
        draw.double_sided = item.double_sided;
        draw.lighting_model = static_cast<std::int32_t>(item.lighting_model);
        draw.cast_shadow = item.cast_shadow;
        draw.receive_shadow = item.receive_shadow;
        if (!external) return false;

        draw.alpha_mode = material_alpha_mode(material->alpha_mode);
        draw.alpha_cutoff = material->alpha_cutoff;
        const BaseTextureBinding binding = base_texture_binding(item);
        const bool flat_fill = item.material_binding.shader == BuiltInShaders::FlatFill;
        // BuiltIn は自前 PSO を持てないので、固有表現は builtin_params で運ぶ。
        // x=効果ID（1=Pixelate）、y/z/w=その効果の引数。増やすときは ID を足す。
        const bool is_toon = item.material_binding.shader == BuiltInShaders::Toon;
        if (is_toon)
        {
            const auto property_float = [material](const char* name, float fallback)
            {
                const auto* value = material->properties.Find(name);
                return value != nullptr ? value->AsFloat(fallback) : fallback;
            };
            const auto property_color = [material](const char* name,
                const DirectX::XMFLOAT4& fallback)
            {
                const auto* value = material->properties.Find(name);
                return value != nullptr ? value->AsVector4() : fallback;
            };
            const float steps = property_float("prop.StepCount", 3.0f);
            draw.builtin_params = { 2.0f, std::clamp(steps, 1.0f, 8.0f), 0.0f, 0.0f };
            const DirectX::XMFLOAT4 shadow_tint =
                property_color("prop.ShadowTint", { 0.0f, 0.0f, 0.0f, 1.0f });
            const DirectX::XMFLOAT4 rim_color =
                property_color("prop.RimColor", { 0.0f, 0.0f, 0.0f, 1.0f });
            const DirectX::XMFLOAT4 specular_tint =
                property_color("prop.SpecularTint", { 0.0f, 0.0f, 0.0f, 1.0f });
            draw.builtin_params1 = { std::clamp(shadow_tint.x, 0.0f, 1.0f),
                std::clamp(shadow_tint.y, 0.0f, 1.0f), std::clamp(shadow_tint.z, 0.0f, 1.0f),
                std::clamp(property_float("prop.RimPower", 2.0f), 0.0f, 8.0f) };
            draw.builtin_params2 = { std::clamp(rim_color.x, 0.0f, 1.0f),
                std::clamp(rim_color.y, 0.0f, 1.0f), std::clamp(rim_color.z, 0.0f, 1.0f),
                std::clamp(property_float("prop.SpecularPower", 32.0f), 1.0f, 128.0f) };
            draw.builtin_params3 = { std::clamp(specular_tint.x, 0.0f, 1.0f),
                std::clamp(specular_tint.y, 0.0f, 1.0f),
                std::clamp(specular_tint.z, 0.0f, 1.0f), 0.0f };
        }
        else if (item.material_binding.shader == BuiltInShaders::Pixelate)
        {
            draw.builtin_params = { 1.0f,
                (std::max)(1.0f, item.pixelate_size),
                std::clamp(item.pixelate_strength, 0.0f, 1.0f),
                std::clamp(item.pixelate_opacity, 0.0f, 1.0f) };
            const auto* use_gbuffer = material->properties.Find("prop.UseGBufferColor");
            draw.builtin_params1.x = use_gbuffer != nullptr && use_gbuffer->AsBool(false)
                ? 1.0f : 0.0f;
        }

        if (flat_fill)
        {
            // FlatFill だけ既存の 1x1 白テクスチャを使い、共用 Bridge は変更しない。
            draw.base_color_texture_key = "__dx12_white";
            // BuiltIn は自前 PSO を持たず必ず Bridge を通るため、cbuffer の BaseColor が
            // 効かない。Bridge が使う base_color へ Material の色を入れる。
            draw.base_color = multiply_color(material->base_color, item.tint);
        }
        else
        {
            std::string texture_guid = binding.asset_guid;
            if (texture_guid.empty()) texture_guid = material->base_color_texture;
            draw.base_color_texture_key = add_asset_texture(texture_guid);
            if (draw.base_color_texture_key.empty())
                draw.base_color_texture_key = binding.fallback_key;
        }

        // 既存の MaterialBindingResolver を b9 の Byte と t40 以降の Slot の正本として使う。
        // DX12 Backend はこの解決済み Packet だけを利用する。
        draw.material_constants = item.material_binding.constants;
        draw.material_texture_semantic_mask = item.material_binding.TextureSemanticMask();
        for (const ResolvedMaterialTexture& texture : item.material_binding.textures)
        {
            D3D12StaticMaterialTexture mapped;
            mapped.slot = texture.slot;
            // BuiltIn Toon の RampMap だけ宣言順(t41)ではなく Bridge の固定 slot へ載せ替える。
            std::uint32_t bridge_slot = 0;
            if (is_toon && texture.property_name == "RampMap" &&
                ResolvedMaterialBinding::TryGetGBufferBridgeSlot(
                    texture.property_name, bridge_slot))
                mapped.slot = bridge_slot;
            mapped.texture_key = add_asset_texture(texture.asset_guid);
            if (mapped.texture_key.empty())
                mapped.texture_key = fallback_texture_key(texture.default_texture);
            draw.material_textures.push_back(std::move(mapped));
        }

        // BuiltInのPBR/Toon/FBX/Pixelate/UnlitはまだDeferred/Light/Shadowの
        // Global Resource Setに依存するため、Phase 2 Bridgeで意図的に描画する。
        // 安定した b0/b1/b4/b9、t0/t40 以降、s0..s2 の ABI だけに依存する
        // Project/Composer Surface Shaderは実際のSM6 Custom Pixel Shaderとして実行できる。
        const ShaderCatalog::Entry* shader_entry =
            shader_library.Catalog().Find(item.material_binding.shader);
        if (shader_entry != nullptr && shader_entry->info.domain == ShaderDomain::Surface &&
            shader_entry->schema && item.material_binding.usable_shader)
        {
            const std::string generic = shader_entry->info.source_path.generic_string();
            const bool built_in = generic.find("/BuiltIn/") != std::string::npos ||
                generic.find("\\BuiltIn\\") != std::string::npos;
            if (!built_in)
            {
                draw.shader_key = item.material_binding.shader.ToString();
                if (!draw.shader_key.empty() &&
                    !dx12_device_context.HasStaticShader(draw.shader_key) &&
                    shader_source_keys.insert(draw.shader_key).second)
                {
                    D3D12StaticShaderSource shader;
                    shader.key = draw.shader_key;
                    shader.source_path = shader_entry->info.source_path;
                    if (shader.source_path.is_relative())
                        shader.source_path = std::filesystem::current_path() /
                            shader.source_path;
                    shader.source_path = shader.source_path.lexically_normal();
                    shader.generated_declaration =
                        ShaderConstantPacker::GenerateHlslDeclaration(*shader_entry->schema);
                    submission.shader_sources.push_back(std::move(shader));
                }
            }
        }
        return true;
    };

    const auto resolve_material_slot = [this](const RenderItem& source_item,
        std::size_t subset_index, const std::string*& out_asset) -> const MaterialAsset*
    {
        out_asset = nullptr;
        if (source_item.material_slot_assets == nullptr ||
            subset_index >= static_cast<std::size_t>(source_item.material_slot_count))
            return nullptr;
        const std::string* candidate = source_item.material_slot_assets[subset_index];
        if (candidate == nullptr || candidate->empty()) return nullptr;
        const MaterialAsset* material = resolve_object_material(*candidate);
        if (material == nullptr) return nullptr;
        out_asset = candidate;
        return material;
    };

    const auto make_mesh_source = [&submission, &mesh_source_keys](
        const std::string& key, auto vertex_begin, auto vertex_end,
        const std::vector<std::uint32_t>& indices)
    {
        if (!mesh_source_keys.insert(key).second) return;
        D3D12StaticMeshSource source;
        source.key = key;
        try
        {
            source.vertices.reserve(static_cast<std::size_t>(
                std::distance(vertex_begin, vertex_end)));
            for (auto it = vertex_begin; it != vertex_end; ++it)
            {
                D3D12StaticVertex vertex;
                vertex.position = it->position;
                vertex.normal = it->normal;
                vertex.texcoord = it->texcoord;
                source.vertices.push_back(vertex);
            }
            source.indices = indices;
            submission.mesh_sources.push_back(std::move(source));
        }
        catch (...)
        {
            mesh_source_keys.erase(key);
        }
    };

    std::unordered_set<std::string> skinned_mesh_source_keys;
    const auto make_skinned_mesh_source = [&submission, &skinned_mesh_source_keys](
        const std::string& key, const skinned_mesh::mesh& mesh)
    {
        if (!skinned_mesh_source_keys.insert(key).second) return;
        D3D12SkinnedMeshSource source;
        source.key = key;
        try
        {
            source.vertices.reserve(mesh.vertices.size());
            for (const skinned_mesh::vertex& input : mesh.vertices)
            {
                D3D12SkinnedVertex vertex;
                vertex.position = input.position;
                vertex.normal = input.normal;
                vertex.tangent = input.tangent;
                vertex.texcoord = input.texcoord;
                vertex.bone_weights = { input.bone_weights[0], input.bone_weights[1],
                    input.bone_weights[2], input.bone_weights[3] };
                for (std::uint32_t influence = 0; influence < 4; ++influence)
                    vertex.bone_indices[influence] = input.bone_indices[influence];
                vertex.morph_position = input.morph_position;
                vertex.morph_normal = input.morph_normal;
                source.vertices.push_back(vertex);
            }
            source.indices = mesh.indices;
            submission.skinned_mesh_sources.push_back(std::move(source));
        }
        catch (...)
        {
            skinned_mesh_source_keys.erase(key);
        }
    };

    if (options.include_auxiliary_geometry)
    {
    const auto normalize_or_fallback = [](const DirectX::XMVECTOR& value,
        const DirectX::XMVECTOR& fallback) noexcept
    {
        const float length = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(value));
        return length > 1.0e-8f ? DirectX::XMVector3Normalize(value) : fallback;
    };
    DirectX::XMFLOAT4X4 inverse_view{};
    DirectX::XMStoreFloat4x4(&inverse_view,
        DirectX::XMMatrixInverse(nullptr, viewport_view_matrix()));
    const DirectX::XMVECTOR camera_right = normalize_or_fallback(
        DirectX::XMVectorSet(inverse_view._11, inverse_view._12, inverse_view._13, 0.0f),
        DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
    const DirectX::XMVECTOR camera_up = normalize_or_fallback(
        DirectX::XMVectorSet(inverse_view._21, inverse_view._22, inverse_view._23, 0.0f),
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    const DirectX::XMVECTOR camera_forward = normalize_or_fallback(
        DirectX::XMVectorSet(inverse_view._31, inverse_view._32, inverse_view._33, 0.0f),
        DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));

    const auto add_ribbon = [&submission, &normalize_or_fallback, camera_right,
        camera_up, camera_forward](const std::string& key,
        const std::vector<DirectX::XMFLOAT3>& points,
        const std::vector<float>& point_alpha,
        const ReplayEngine::Rendering::LineStrokeStyle& style) -> bool
    {
        if (key.empty() || points.size() < 2) return false;
        D3D12StaticMeshSource source;
        source.key = key;
        source.replace_existing = true;
        try
        {
            source.vertices.reserve((points.size() - 1) * 4);
            source.indices.reserve((points.size() - 1) * 6);
            const float width_start = (std::max)(0.0f, style.width_start);
            const float width_end = (std::max)(0.0f, style.width_end);
            for (std::size_t index = 0; index + 1 < points.size(); ++index)
            {
                const float t0 = static_cast<float>(index) /
                    static_cast<float>(points.size() - 1);
                const float t1 = static_cast<float>(index + 1) /
                    static_cast<float>(points.size() - 1);
                const DirectX::XMVECTOR p0 = DirectX::XMLoadFloat3(&points[index]);
                const DirectX::XMVECTOR p1 = DirectX::XMLoadFloat3(&points[index + 1]);
                const DirectX::XMVECTOR tangent = normalize_or_fallback(
                    DirectX::XMVectorSubtract(p1, p0),
                    DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
                const DirectX::XMVECTOR side = normalize_or_fallback(
                    DirectX::XMVector3Cross(tangent, camera_forward), camera_right);
                const float half_width0 = width_start * 0.5f * (1.0f - t0) +
                    width_end * 0.5f * t0;
                const float half_width1 = width_start * 0.5f * (1.0f - t1) +
                    width_end * 0.5f * t1;
                const DirectX::XMVECTOR left0 = DirectX::XMVectorSubtract(p0,
                    DirectX::XMVectorScale(side, half_width0));
                const DirectX::XMVECTOR right0 = DirectX::XMVectorAdd(p0,
                    DirectX::XMVectorScale(side, half_width0));
                const DirectX::XMVECTOR left1 = DirectX::XMVectorSubtract(p1,
                    DirectX::XMVectorScale(side, half_width1));
                const DirectX::XMVECTOR right1 = DirectX::XMVectorAdd(p1,
                    DirectX::XMVectorScale(side, half_width1));
                D3D12StaticVertex vertices[4]{};
                DirectX::XMStoreFloat3(&vertices[0].position, left0);
                DirectX::XMStoreFloat3(&vertices[1].position, right0);
                DirectX::XMStoreFloat3(&vertices[2].position, right1);
                DirectX::XMStoreFloat3(&vertices[3].position, left1);
                for (D3D12StaticVertex& vertex : vertices)
                {
                    DirectX::XMStoreFloat3(&vertex.normal, camera_up);
                    vertex.texcoord = { t0, 0.0f };
                }
                vertices[2].texcoord = { t1, 1.0f };
                vertices[3].texcoord = { t1, 0.0f };
                const std::uint32_t base = static_cast<std::uint32_t>(source.vertices.size());
                source.vertices.insert(source.vertices.end(), std::begin(vertices), std::end(vertices));
                source.indices.insert(source.indices.end(),
                    { base, base + 1, base + 2, base, base + 2, base + 3 });
            }
        }
        catch (...)
        {
            return false;
        }
        if (source.vertices.empty() || source.indices.empty()) return false;
        submission.mesh_sources.push_back(std::move(source));
        return true;
    };

    const auto add_ribbon_draw = [&submission](const std::string& key,
        const std::string& motion_key, const ReplayEngine::Rendering::LineStrokeStyle& style)
    {
        D3D12StaticDrawItem draw;
        draw.mesh_key = key;
        draw.motion_key = motion_key;
        draw.alpha_mode = D3D12StaticAlphaMode::Blend;
        draw.base_color = style.fill_color;
        draw.cast_shadow = false;
        draw.receive_shadow = false;
        draw.double_sided = true;
        submission.draws.push_back(std::move(draw));
    };

    // LineRenderer/TrailもCPU側の点列を正本にし、DX12では一時Ribbon Meshへ変換する。
    // Frame slotごとに置換するので、毎フレームの軌跡更新で古いGPU Resourceを蓄積しない。
    {
        std::vector<DirectX::XMFLOAT3> path;
        std::vector<float> alpha;
        for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
        {
            const ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;
            const DirectX::XMFLOAT4X4 world = object->GetTransform().WorldMatrixFloat4x4();
            for (std::size_t component_index = 0; component_index < object->ComponentCount();
                ++component_index)
            {
                const auto* component = object->ComponentAt(component_index);
                const std::string key_prefix = "ribbon:" +
                    std::to_string(object->ID().Value()) + ":" +
                    std::to_string(component_index) + ":slot:" +
                    std::to_string(dx12_device_context.FrameIndex());
                if (const auto* line = dynamic_cast<const ReplayEngine::Components::LineRendererComponent*>(component))
                {
                    if (!line->ActiveInHierarchy() || line->points.size() < 2) continue;
                    path = ReplayEngine::Rendering::BuildCatmullRomLinePath(
                        line->points, line->smoothing, line->closed);
                    for (DirectX::XMFLOAT3& point : path)
                    {
                        DirectX::XMStoreFloat3(&point,
                            DirectX::XMVector3TransformCoord(
                                DirectX::XMLoadFloat3(&point), DirectX::XMLoadFloat4x4(&world)));
                    }
                    const auto style = line->StrokeStyle();
                    if (add_ribbon(key_prefix, path, {}, style))
                        add_ribbon_draw(key_prefix, key_prefix, style);
                    continue;
                }
                const auto* trail = dynamic_cast<const ReplayEngine::Components::TrailComponent*>(component);
                if (trail == nullptr || !trail->ActiveInHierarchy()) continue;
                trail->RuntimePath(path, alpha);
                if (path.size() < 2) continue;
                if (!trail->world_space)
                {
                    DirectX::XMFLOAT4X4 parent_world{};
                    const auto* parent = object->Parent();
                    if (parent != nullptr) parent_world = parent->GetTransform().WorldMatrixFloat4x4();
                    else DirectX::XMStoreFloat4x4(&parent_world, DirectX::XMMatrixIdentity());
                    for (DirectX::XMFLOAT3& point : path)
                        DirectX::XMStoreFloat3(&point,
                            DirectX::XMVector3TransformCoord(
                                DirectX::XMLoadFloat3(&point), DirectX::XMLoadFloat4x4(&parent_world)));
                }
                const auto style = trail->StrokeStyle();
                if (add_ribbon(key_prefix, path, alpha, style))
                    add_ribbon_draw(key_prefix, key_prefix, style);
            }
        }
    }

    // ParticleEmitterもComponentの値を正本にし、DX12の透明Forwardへ提出する。
    // D3D11専用のStructuredBufferへ依存せず、寿命のあるCPU状態をビルボードへ
    // 変換することで、テクスチャ・Blend・深度の既存Material契約を再利用する。
    {
        using ReplayEngine::Components::ParticleEmitterComponent;
        using Particle = framework::dx12_particle_instance;
        const float delta = (std::max)(0.0f, (std::min)(elapsed_time, 0.1f));
        const DirectX::XMVECTOR world_up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        const DirectX::XMVECTOR camera_forward_world =
            DirectX::XMVector3Normalize(camera_forward);

        struct ParticleBucket final
        {
            D3D12StaticMeshSource source;
            DirectX::XMFLOAT4 color_sum{};
            std::size_t count = 0;
        };

        const auto next_random = [](std::uint32_t& state) noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<float>(state & 0x00FFFFFFu) /
                static_cast<float>(0x01000000u);
        };
        const auto clamp_color = [](const DirectX::XMFLOAT4& value) noexcept
        {
            return DirectX::XMFLOAT4{
                (std::max)(0.0f, (std::min)(1.0f, value.x)),
                (std::max)(0.0f, (std::min)(1.0f, value.y)),
                (std::max)(0.0f, (std::min)(1.0f, value.z)),
                (std::max)(0.0f, (std::min)(1.0f, value.w)) };
        };
        const auto clamp_value = [](float value, float fallback,
            float minimum, float maximum) noexcept
        {
            if (!std::isfinite(value)) value = fallback;
            return (std::max)(minimum, (std::min)(maximum, value));
        };
        const auto lerp_color = [&clamp_color](const DirectX::XMFLOAT4& a,
            const DirectX::XMFLOAT4& b, float t) noexcept
        {
            return clamp_color(DirectX::XMFLOAT4{
                a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
        };

        for (std::size_t object_index = 0; object_index < scene.GameObjectCount();
            ++object_index)
        {
            const ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy() ||
                !object->ActiveInHierarchy()) continue;
            const ParticleEmitterComponent* emitter = nullptr;
            for (std::size_t component_index = 0; component_index < object->ComponentCount();
                ++component_index)
            {
                emitter = dynamic_cast<const ParticleEmitterComponent*>(
                    object->ComponentAt(component_index));
                if (emitter != nullptr && emitter->ActiveInHierarchy()) break;
                emitter = nullptr;
            }
            if (emitter == nullptr || (!emitter->emitting && !emitter->HasPendingRequest()))
                continue;

            const ReplayEngine::Core::ObjectID object_id = object->ID();
            std::vector<Particle>& state = dx12_particle_states[object_id];
            float& spawn_remainder = dx12_particle_spawn_remainders[object_id];
            if (emitter->ConsumeClearRequest())
            {
                state.clear();
                spawn_remainder = 0.0f;
            }

            const int max_particles = (std::max)(1, (std::min)(emitter->max_particles, 10000));
            const DirectX::XMFLOAT4X4 object_world = object->GetTransform().WorldMatrixFloat4x4();
            const DirectX::XMMATRIX object_matrix = DirectX::XMLoadFloat4x4(&object_world);
            const DirectX::XMVECTOR origin = DirectX::XMVector3TransformCoord(
                DirectX::XMVectorZero(), object_matrix);
            const DirectX::XMVECTOR direction_input = DirectX::XMVector3TransformNormal(
                DirectX::XMLoadFloat3(&emitter->direction), object_matrix);
            const DirectX::XMVECTOR direction = normalize_or_fallback(
                direction_input, world_up);
            const DirectX::XMVECTOR basis_seed =
                std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(direction, world_up))) < 0.98f
                ? world_up : DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
            const DirectX::XMVECTOR basis_right = normalize_or_fallback(
                DirectX::XMVector3Cross(basis_seed, direction),
                DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
            const DirectX::XMVECTOR basis_up = DirectX::XMVector3Cross(direction, basis_right);
            const float lifetime = clamp_value(emitter->lifetime, 1.5f, 0.01f, 60.0f);
            const float start_speed = clamp_value(emitter->start_speed, 2.0f, 0.0f, 200.0f);
            const float gravity = clamp_value(emitter->gravity, 1.8f, -100.0f, 100.0f);
            const float drag = clamp_value(emitter->drag, 0.5f, 0.0f, 20.0f);
            const float start_size = clamp_value(emitter->start_size, 0.1f, 0.001f, 100.0f);
            const float end_size = clamp_value(emitter->end_size, 0.02f, 0.001f, 100.0f);
            const float cone = clamp_value(emitter->cone_angle, 0.4f, 0.0f, 3.14159f);

            for (Particle& particle : state)
            {
                particle.age += delta;
                if (particle.age >= particle.life) continue;
                particle.velocity.y -= gravity * delta;
                particle.velocity.x *= std::exp(-drag * delta);
                particle.velocity.y *= std::exp(-drag * delta);
                particle.velocity.z *= std::exp(-drag * delta);
                particle.position.x += particle.velocity.x * delta;
                particle.position.y += particle.velocity.y * delta;
                particle.position.z += particle.velocity.z * delta;
                const float life_t = (std::max)(0.0f, (std::min)(1.0f,
                    particle.age / (std::max)(particle.life, 0.001f)));
                particle.size = start_size + (end_size - start_size) * life_t;
                particle.color = lerp_color(emitter->start_color, emitter->end_color, life_t);
                particle.rotation += delta * 1.7f;
            }
            state.erase(std::remove_if(state.begin(), state.end(),
                [](const Particle& particle) { return particle.age >= particle.life; }), state.end());

            const float spawn_rate = emitter->emitting
                ? clamp_value(emitter->spawn_rate, 200.0f, 0.0f, 20000.0f) : 0.0f;
            spawn_remainder += spawn_rate * delta;
            int spawn_count = static_cast<int>(spawn_remainder);
            spawn_remainder -= static_cast<float>(spawn_count);
            spawn_count += (std::max)(0, emitter->ConsumeBurst());
            spawn_count = (std::min)(spawn_count,
                max_particles - static_cast<int>(state.size()));

            std::uint32_t random_state = static_cast<std::uint32_t>(
                object_id.Value() ^ (static_cast<std::uint64_t>(frame_index) * 747796405ull));
            for (int spawn_index = 0; spawn_index < spawn_count; ++spawn_index)
            {
                const float radius = std::sqrt(next_random(random_state));
                const float azimuth = next_random(random_state) * DirectX::XM_2PI;
                const float angle = radius * cone;
                const float sin_angle = std::sin(angle);
                const DirectX::XMVECTOR cone_direction = DirectX::XMVectorAdd(
                    DirectX::XMVectorScale(direction, std::cos(angle)),
                    DirectX::XMVectorAdd(
                        DirectX::XMVectorScale(basis_right, std::cos(azimuth) * sin_angle),
                        DirectX::XMVectorScale(basis_up, std::sin(azimuth) * sin_angle)));
                const DirectX::XMVECTOR spawn_direction = DirectX::XMVector3Normalize(
                    cone_direction);
                const float particle_life = lifetime *
                    (0.75f + next_random(random_state) * 0.5f);
                const float speed = start_speed * (0.75f + next_random(random_state) * 0.5f);
                Particle particle;
                DirectX::XMStoreFloat3(&particle.position, origin);
                DirectX::XMStoreFloat3(&particle.velocity,
                    DirectX::XMVectorScale(spawn_direction, speed));
                particle.color = clamp_color(emitter->start_color);
                particle.life = particle_life;
                particle.size = start_size;
                particle.rotation = next_random(random_state) * DirectX::XM_2PI;
                state.push_back(particle);
            }
            if (state.size() > static_cast<std::size_t>(max_particles))
                state.resize(static_cast<std::size_t>(max_particles));
            if (state.empty()) continue;

            ParticleBucket buckets[8]{};
            for (std::size_t particle_index = 0; particle_index < state.size(); ++particle_index)
            {
                const Particle& particle = state[particle_index];
                const float life_t = particle.life > 0.0f ? particle.age / particle.life : 1.0f;
                const std::size_t bucket_index = (std::min)(static_cast<std::size_t>(7),
                    static_cast<std::size_t>((std::max)(0.0f, life_t) * 8.0f));
                ParticleBucket& bucket = buckets[bucket_index];
                if (bucket.source.key.empty())
                    bucket.source.key = "particle:" + std::to_string(object_id.Value()) + ":slot:" +
                        std::to_string(dx12_device_context.FrameIndex()) + ":bucket:" +
                        std::to_string(bucket_index);
                bucket.source.replace_existing = true;
                DirectX::XMVECTOR center = DirectX::XMLoadFloat3(&particle.position);
                const DirectX::XMVECTOR right = DirectX::XMVectorScale(camera_right, particle.size);
                const DirectX::XMVECTOR up = DirectX::XMVectorScale(camera_up, particle.size);
                const DirectX::XMVECTOR corners[4] = {
                    DirectX::XMVectorAdd(DirectX::XMVectorSubtract(center, right), up),
                    DirectX::XMVectorAdd(DirectX::XMVectorAdd(center, right), up),
                    DirectX::XMVectorSubtract(DirectX::XMVectorAdd(center, right), up),
                    DirectX::XMVectorSubtract(DirectX::XMVectorSubtract(center, right), up) };
                const std::uint32_t base = static_cast<std::uint32_t>(bucket.source.vertices.size());
                for (std::uint32_t corner = 0; corner < 4; ++corner)
                {
                    D3D12StaticVertex vertex;
                    DirectX::XMStoreFloat3(&vertex.position, corners[corner]);
                    DirectX::XMStoreFloat3(&vertex.normal, camera_forward_world);
                    vertex.texcoord = { (corner == 1 || corner == 2) ? 1.0f : 0.0f,
                        (corner >= 2) ? 1.0f : 0.0f };
                    bucket.source.vertices.push_back(vertex);
                }
                bucket.source.indices.insert(bucket.source.indices.end(),
                    { base, base + 1, base + 2, base, base + 2, base + 3 });
                bucket.color_sum.x += particle.color.x;
                bucket.color_sum.y += particle.color.y;
                bucket.color_sum.z += particle.color.z;
                bucket.color_sum.w += particle.color.w;
                ++bucket.count;
            }

            std::string sprite_texture_key = add_asset_texture(emitter->sprite.guid);
            if (sprite_texture_key.empty()) sprite_texture_key = "__dx12_white";
            for (std::size_t bucket_index = 0; bucket_index < 8; ++bucket_index)
            {
                ParticleBucket& bucket = buckets[bucket_index];
                if (bucket.count == 0 || bucket.source.vertices.empty()) continue;
                const float inverse_count = 1.0f / static_cast<float>(bucket.count);
                D3D12StaticDrawItem draw;
                draw.mesh_key = bucket.source.key;
                draw.motion_key = bucket.source.key;
                draw.base_color = {
                    bucket.color_sum.x * inverse_count,
                    bucket.color_sum.y * inverse_count,
                    bucket.color_sum.z * inverse_count,
                    bucket.color_sum.w * inverse_count };
                draw.base_color_texture_key = sprite_texture_key;
                draw.alpha_mode = D3D12StaticAlphaMode::Blend;
                draw.lighting_model = static_cast<std::int32_t>(
                    ReplayEngine::Rendering::ShaderLightingModel::Unlit);
                draw.roughness = 1.0f;
                draw.cast_shadow = false;
                draw.receive_shadow = false;
                draw.double_sided = true;
                submission.mesh_sources.push_back(std::move(bucket.source));
                submission.draws.push_back(std::move(draw));
            }
        }
    }

    }

    for (const RenderItem& source_item : render_items.Items())
    {
        if (source_item.mesh_asset.empty()) continue;

        const RenderItem item = resolve_render_item_material(source_item);
        if (source_item.skinned)
        {
            skinned_mesh* mesh_asset = resolve_object_mesh(item.mesh_asset);
            if (mesh_asset == nullptr) continue;
            std::filesystem::path model_source;
            std::string model_reason;
            (void)resolve_model_source(item.mesh_asset, model_source, model_reason);

            skinned_mesh::animation::keyframe blended_keyframe;
            const skinned_mesh::animation::keyframe* keyframe =
                resolve_render_item_keyframe(*mesh_asset, item, blended_keyframe);

            // Slot 番号は Object 全体を通した番号にする。glTF は 1 primitive が
            // 1 mesh + subset 1 個で入るため、mesh 内の subset 番号だと常に 0 になる。
            std::size_t material_slot_cursor = 0;
            for (std::size_t mesh_index = 0; mesh_index < mesh_asset->meshes.size(); ++mesh_index)
            {
                const skinned_mesh::mesh& mesh = mesh_asset->meshes[mesh_index];
                if (mesh.vertices.empty() || mesh.indices.empty()) continue;
                const std::string mesh_key = item.mesh_asset + "#skinned:" +
                    std::to_string(mesh_index);
                D3D12MeshLocalBounds cached_bounds;
                if (!dx12_device_context.HasSkinnedMesh(mesh_key) ||
                    !dx12_device_context.GetSkinnedMeshLocalBounds(mesh_key, cached_bounds))
                    make_skinned_mesh_source(mesh_key, mesh);

                DirectX::XMFLOAT4X4 mesh_world =
                    multiply_world(mesh.default_global_transform, item.world);
                float morph_weight = mesh.default_morph_weight;
                std::size_t palette_size = (std::max)(static_cast<std::size_t>(1),
                    mesh.bind_pose.bones.size());
                for (const skinned_mesh::vertex& vertex : mesh.vertices)
                {
                    for (std::uint32_t influence = 0; influence < 4; ++influence)
                        palette_size = (std::max)(palette_size,
                            static_cast<std::size_t>(vertex.bone_indices[influence]) + 1u);
                }
                std::vector<DirectX::XMFLOAT4X4> palette(palette_size,
                    DirectX::XMFLOAT4X4{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 });

                if (keyframe != nullptr && mesh.node_index >= 0 &&
                    static_cast<std::size_t>(mesh.node_index) < keyframe->nodes.size())
                {
                    const skinned_mesh::animation::keyframe::node& mesh_node =
                        keyframe->nodes[static_cast<std::size_t>(mesh.node_index)];
                    mesh_world = multiply_world(mesh_node.global_transform, item.world);
                    morph_weight = mesh_node.morph_weight;
                    const DirectX::XMMATRIX inverse_mesh = DirectX::XMMatrixInverse(nullptr,
                        DirectX::XMLoadFloat4x4(&mesh_node.global_transform));
                    for (std::size_t bone_index = 0; bone_index < mesh.bind_pose.bones.size();
                        ++bone_index)
                    {
                        const skeleton::bone& bone = mesh.bind_pose.bones[bone_index];
                        if (bone.node_index < 0 ||
                            static_cast<std::size_t>(bone.node_index) >= keyframe->nodes.size())
                            continue;
                        const auto& bone_node =
                            keyframe->nodes[static_cast<std::size_t>(bone.node_index)];
                        DirectX::XMStoreFloat4x4(&palette[bone_index],
                            DirectX::XMLoadFloat4x4(&bone.offset_transform) *
                            DirectX::XMLoadFloat4x4(&bone_node.global_transform) * inverse_mesh);
                    }
                }

                const auto append_skinned_draw = [&](std::uint32_t start, std::uint32_t count,
                    std::uint64_t material_id, std::size_t subset_index)
                {
                    const std::string* slot_asset = nullptr;
                    const MaterialAsset* slot_material =
                        resolve_material_slot(source_item, subset_index, slot_asset);
                    RenderItem slot_item;
                    const RenderItem* material_item = &item;
                    if (slot_material != nullptr && slot_asset != nullptr)
                    {
                        slot_item = resolve_render_item_material(source_item, slot_asset, false);
                        material_item = &slot_item;
                    }
                    D3D12SkinnedDrawItem skinned_draw;
                    D3D12StaticDrawItem& draw = skinned_draw.surface;
                    draw.mesh_key = mesh_key;
                    draw.owner_id = source_item.owner.Value();
                    draw.rendering_layer = static_cast<std::uint32_t>((std::max)(0,
                        (std::min)(31, item.rendering_layer)));
                    draw.start_index = start;
                    draw.index_count = count;
                    draw.world = mesh_world;
                    const bool external = fill_external_material(
                        source_item, *material_item, draw, slot_material);
                    if (external)
                    {
                        const MaterialAsset* material = slot_material != nullptr
                            ? slot_material : resolve_object_material(source_item.material_asset);
                        if (material != nullptr)
                        {
                            const auto embedded_texture_path =
                                [&mesh_asset, material_id, &model_source](std::size_t slot)
                            {
                                const auto embedded_material = mesh_asset->materials.find(material_id);
                                if (embedded_material == mesh_asset->materials.end()) return std::filesystem::path{};
                                std::filesystem::path path(embedded_material->second.texture_filenames[slot]);
                                if (!path.empty() && path.is_relative() &&
                                    !model_source.empty())
                                    path = model_source.parent_path() / path;
                                return path;
                            };
                            preserve_embedded_material_textures(draw, *material_item, *material,
                                embedded_texture_path(0), embedded_texture_path(1),
                                embedded_texture_path(2), embedded_texture_path(3));
                        }
                    }
                    if (!external)
                    {
                        const auto material_it = mesh_asset->materials.find(material_id);
                        if (material_it != mesh_asset->materials.end())
                        {
                            const skinned_mesh::material& material = material_it->second;
                            draw.base_color = multiply_color(material.Kd, source_item.tint);
                            std::filesystem::path texture_path(material.texture_filenames[0]);
                            if (!texture_path.empty() && texture_path.is_relative() &&
                                !model_source.empty())
                                texture_path = model_source.parent_path() / texture_path;
                            draw.base_color_texture_key = add_texture(texture_path);
                            const auto embedded_texture_path =
                                [&material, &model_source](std::size_t slot)
                            {
                                std::filesystem::path path(material.texture_filenames[slot]);
                                if (!path.empty() && path.is_relative() &&
                                    !model_source.empty())
                                    path = model_source.parent_path() / path;
                                return path;
                            };
                            add_material_texture(draw, 41u, embedded_texture_path(1),
                                kNormalMapSemantic);
                            add_material_texture(draw, 42u, embedded_texture_path(2),
                                kPackedOrmMapSemantic);
                            add_material_texture(draw, 44u, embedded_texture_path(3),
                                kEmissiveMapSemantic);
                            if (const skinned_mesh::gltf_material_info* gltf_material =
                                mesh_asset->GltfMaterial(material_id))
                            {
                                draw.metallic = gltf_material->metallic;
                                draw.roughness = gltf_material->roughness;
                                draw.ambient_occlusion = gltf_material->occlusion;
                                draw.emissive = gltf_material->emissive;
                                draw.emissive_strength = gltf_material->emissive_strength;
                                draw.alpha_mode = alpha_mode(gltf_material->alpha_mode);
                                draw.alpha_cutoff = gltf_material->alpha_cutoff;
                                draw.double_sided = gltf_material->double_sided || item.double_sided;
                                if (gltf_material->unlit) draw.lighting_model = 2;
                            }
                        }
                        else
                        {
                            draw.base_color = source_item.tint;
                            draw.double_sided = item.double_sided;
                        }
                    }
                    skinned_draw.motion_key = std::to_string(source_item.owner.Value()) + ":" +
                        mesh_key + ":" + std::to_string(start);
                    skinned_draw.bone_palette = palette;
                    skinned_draw.morph_weight = morph_weight;
                    submission.skinned_draws.push_back(std::move(skinned_draw));
                };

                if (mesh.subsets.empty())
                    append_skinned_draw(0, static_cast<std::uint32_t>(mesh.indices.size()), 0,
                        material_slot_cursor++);
                else
                {
                    for (std::size_t subset_index = 0; subset_index < mesh.subsets.size();
                        ++subset_index)
                    {
                        const skinned_mesh::mesh::subset& subset = mesh.subsets[subset_index];
                        append_skinned_draw(subset.start_index_location, subset.index_count,
                            subset.material_unique_id, material_slot_cursor++);
                    }
                }
            }
            continue;
        }
        if (item.mesh_asset.rfind("builtin:", 0) == 0)
        {
            const std::string* slot_asset = nullptr;
            const MaterialAsset* slot_material = resolve_material_slot(source_item, 0, slot_asset);
            RenderItem slot_item;
            const RenderItem* material_item = &item;
            if (slot_material != nullptr && slot_asset != nullptr)
            {
                slot_item = resolve_render_item_material(source_item, slot_asset, false);
                material_item = &slot_item;
            }
            if (dx12_framework_active)
            {
                std::vector<static_mesh::vertex> cpu_vertices;
                std::vector<std::uint32_t> cpu_indices;
                if (!build_builtin_primitive_cpu(item.mesh_asset, cpu_vertices, cpu_indices) ||
                    cpu_vertices.empty() || cpu_indices.empty())
                    continue;
                if (!dx12_device_context.HasStaticMesh(item.mesh_asset))
                {
                    D3D12StaticMeshSource source;
                    source.key = item.mesh_asset;
                    source.vertices.reserve(cpu_vertices.size());
                    for (const static_mesh::vertex& input : cpu_vertices)
                    {
                        D3D12StaticVertex vertex;
                        vertex.position = input.position;
                        vertex.normal = input.normal;
                        vertex.texcoord = input.texcoord;
                        source.vertices.push_back(vertex);
                    }
                    source.indices = cpu_indices;
                    submission.mesh_sources.push_back(std::move(source));
                }
                D3D12StaticDrawItem draw;
                draw.mesh_key = item.mesh_asset;
                draw.owner_id = source_item.owner.Value();
                draw.rendering_layer = static_cast<std::uint32_t>((std::max)(0,
                    (std::min)(31, item.rendering_layer)));
                draw.motion_key = std::to_string(source_item.owner.Value()) + ":" + draw.mesh_key;
                draw.world = item.world;
                (void)fill_external_material(source_item, *material_item, draw, slot_material);
                submission.draws.push_back(std::move(draw));
                continue;
            }
            static_mesh* primitive = resolve_builtin_primitive_mesh(item.mesh_asset);
            if (primitive == nullptr || primitive->cpu_vertices().empty() ||
                primitive->cpu_indices().empty())
                continue;
            if (!dx12_device_context.HasStaticMesh(item.mesh_asset))
                make_mesh_source(item.mesh_asset, primitive->cpu_vertices().begin(),
                    primitive->cpu_vertices().end(), primitive->cpu_indices());

            D3D12StaticDrawItem draw;
            draw.mesh_key = item.mesh_asset;
            draw.owner_id = source_item.owner.Value();
            draw.rendering_layer = static_cast<std::uint32_t>((std::max)(0,
                (std::min)(31, item.rendering_layer)));
            draw.motion_key = std::to_string(source_item.owner.Value()) + ":" + draw.mesh_key;
            draw.world = item.world;
            (void)fill_external_material(source_item, *material_item, draw, slot_material);
            submission.draws.push_back(std::move(draw));
            continue;
        }

        gltf_model* gltf = resolve_object_gltf(item.mesh_asset);
        if (gltf != nullptr && !gltf->HasSkins() && !gltf->HasAnimations())
        {
            bool geometry_missing = false;
            for (std::size_t primitive_index = 0;
                primitive_index < gltf->StaticPrimitiveCount(); ++primitive_index)
            {
                const std::string key = item.mesh_asset + "#gltf:" +
                    std::to_string(primitive_index);
                if (!dx12_device_context.HasStaticMesh(key))
                {
                    geometry_missing = true;
                    break;
                }
            }
            std::vector<gltf_model::StaticPrimitiveExport> exported;
            if (geometry_missing)
            {
                if (!gltf->ExportStaticPrimitives(exported)) continue;
                for (std::size_t primitive_index = 0;
                    primitive_index < exported.size(); ++primitive_index)
                {
                    const std::string key = item.mesh_asset + "#gltf:" +
                        std::to_string(primitive_index);
                    if (!dx12_device_context.HasStaticMesh(key))
                        make_mesh_source(key, exported[primitive_index].vertices.begin(),
                            exported[primitive_index].vertices.end(),
                            exported[primitive_index].indices);
                }
            }

            for (std::size_t primitive_index = 0;
                primitive_index < gltf->StaticPrimitiveCount(); ++primitive_index)
            {
                gltf_model::StaticPrimitiveInfo info;
                if (!gltf->StaticPrimitiveInfoAt(primitive_index, info)) continue;
                D3D12StaticDrawItem draw;
                draw.mesh_key = item.mesh_asset + "#gltf:" +
                    std::to_string(primitive_index);
                draw.owner_id = source_item.owner.Value();
                draw.rendering_layer = static_cast<std::uint32_t>((std::max)(0,
                    (std::min)(31, item.rendering_layer)));
                draw.motion_key = std::to_string(source_item.owner.Value()) + ":" + draw.mesh_key;
                draw.world = multiply_world(info.node_transform, item.world);
                const bool external = fill_external_material(source_item, item, draw);
                if (external)
                {
                    const MaterialAsset* material = resolve_object_material(
                        source_item.material_asset);
                    if (material != nullptr)
                        preserve_embedded_material_textures(draw, item, *material,
                            info.embedded_base_color_texture,
                            info.embedded_normal_texture,
                            info.embedded_orm_texture, {});
                }
                if (!external)
                {
                    draw.base_color = multiply_color(info.embedded_base_color,
                        source_item.tint);
                    draw.base_color_texture_key = add_texture(
                        info.embedded_base_color_texture);
                    add_material_texture(draw, 41u, info.embedded_normal_texture,
                        kNormalMapSemantic);
                    add_material_texture(draw, 42u, info.embedded_orm_texture,
                        kPackedOrmMapSemantic);
                    if (!info.embedded_orm_texture.empty())
                    {
                        draw.metallic = 1.0f;
                        draw.roughness = 1.0f;
                        draw.ambient_occlusion = 1.0f;
                    }
                    draw.alpha_mode = alpha_mode(info.alpha_mode);
                    draw.alpha_cutoff = info.alpha_cutoff;
                    draw.double_sided = item.double_sided;
                }
                submission.draws.push_back(std::move(draw));
            }
            continue;
        }

        // FBX/cereal と Bind Pose の GLB は内部的には skinned_mesh で表現されるが、
        // MeshRenderer の Submission（skinned=false）は有効な Static Geometry として扱う。
        skinned_mesh* mesh_asset = resolve_object_mesh(item.mesh_asset);
        if (mesh_asset == nullptr) continue;
        std::filesystem::path model_source;
        std::string model_reason;
        (void)resolve_model_source(item.mesh_asset, model_source, model_reason);

        // Slot 番号は Object 全体を通した番号にする（skinned 側と同じ理由）。
        std::size_t material_slot_cursor = 0;
        for (std::size_t mesh_index = 0; mesh_index < mesh_asset->meshes.size(); ++mesh_index)
        {
            const skinned_mesh::mesh& mesh = mesh_asset->meshes[mesh_index];
            if (mesh.vertices.empty() || mesh.indices.empty()) continue;
            const std::string mesh_key = item.mesh_asset + "#mesh:" +
                std::to_string(mesh_index);
            if (!dx12_device_context.HasStaticMesh(mesh_key))
                make_mesh_source(mesh_key, mesh.vertices.begin(), mesh.vertices.end(),
                    mesh.indices);

            const DirectX::XMFLOAT4X4 mesh_world =
                multiply_world(mesh.default_global_transform, item.world);
            const auto append_draw = [&](std::uint32_t start, std::uint32_t count,
                std::uint64_t material_id, std::size_t subset_index)
            {
                const std::string* slot_asset = nullptr;
                const MaterialAsset* slot_material =
                    resolve_material_slot(source_item, subset_index, slot_asset);
                RenderItem slot_item;
                const RenderItem* material_item = &item;
                if (slot_material != nullptr && slot_asset != nullptr)
                {
                    slot_item = resolve_render_item_material(source_item, slot_asset, false);
                    material_item = &slot_item;
                }
                D3D12StaticDrawItem draw;
                draw.mesh_key = mesh_key;
                draw.owner_id = source_item.owner.Value();
                draw.rendering_layer = static_cast<std::uint32_t>((std::max)(0,
                    (std::min)(31, item.rendering_layer)));
                draw.motion_key = std::to_string(source_item.owner.Value()) + ":" + mesh_key +
                    ":" + std::to_string(start);
                draw.start_index = start;
                draw.index_count = count;
                draw.world = mesh_world;
                const bool external = fill_external_material(
                    source_item, *material_item, draw, slot_material);
                if (external)
                {
                    const MaterialAsset* material = slot_material != nullptr
                        ? slot_material : resolve_object_material(source_item.material_asset);
                    if (material != nullptr)
                    {
                        const auto embedded_texture_path =
                            [&mesh_asset, material_id, &model_source](std::size_t slot)
                        {
                            const auto embedded_material = mesh_asset->materials.find(material_id);
                            if (embedded_material == mesh_asset->materials.end()) return std::filesystem::path{};
                            std::filesystem::path path(embedded_material->second.texture_filenames[slot]);
                            if (!path.empty() && path.is_relative() &&
                                !model_source.empty())
                                path = model_source.parent_path() / path;
                            return path;
                        };
                        preserve_embedded_material_textures(draw, *material_item, *material,
                            embedded_texture_path(0), embedded_texture_path(1),
                            embedded_texture_path(2), embedded_texture_path(3));
                    }
                }
                if (!external)
                {
                    const auto material_it = mesh_asset->materials.find(material_id);
                    if (material_it != mesh_asset->materials.end())
                    {
                        const skinned_mesh::material& material = material_it->second;
                        draw.base_color = multiply_color(material.Kd, source_item.tint);
                        std::filesystem::path texture_path(material.texture_filenames[0]);
                        if (!texture_path.empty() && texture_path.is_relative() &&
                            !model_source.empty())
                            texture_path = model_source.parent_path() / texture_path;
                        draw.base_color_texture_key = add_texture(texture_path);
                        const auto embedded_texture_path =
                            [&material, &model_source](std::size_t slot)
                        {
                            std::filesystem::path path(material.texture_filenames[slot]);
                            if (!path.empty() && path.is_relative() &&
                                !model_source.empty())
                                path = model_source.parent_path() / path;
                            return path;
                        };
                        add_material_texture(draw, 41u, embedded_texture_path(1),
                            kNormalMapSemantic);
                        add_material_texture(draw, 42u, embedded_texture_path(2),
                            kPackedOrmMapSemantic);
                        add_material_texture(draw, 44u, embedded_texture_path(3),
                            kEmissiveMapSemantic);
                        if (const skinned_mesh::gltf_material_info* gltf_material =
                            mesh_asset->GltfMaterial(material_id))
                        {
                            draw.metallic = gltf_material->metallic;
                            draw.roughness = gltf_material->roughness;
                            draw.ambient_occlusion = gltf_material->occlusion;
                            draw.emissive = gltf_material->emissive;
                            draw.emissive_strength = gltf_material->emissive_strength;
                            draw.alpha_mode = alpha_mode(gltf_material->alpha_mode);
                            draw.alpha_cutoff = gltf_material->alpha_cutoff;
                            draw.double_sided = gltf_material->double_sided ||
                                item.double_sided;
                        }
                    }
                    else
                    {
                        draw.base_color = source_item.tint;
                        draw.double_sided = item.double_sided;
                    }
                }
                submission.draws.push_back(std::move(draw));
            };

            if (mesh.subsets.empty())
            {
                append_draw(0, static_cast<std::uint32_t>(mesh.indices.size()), 0,
                    material_slot_cursor++);
            }
            else
            {
                for (std::size_t subset_index = 0; subset_index < mesh.subsets.size();
                    ++subset_index)
                {
                    const skinned_mesh::mesh::subset& subset = mesh.subsets[subset_index];
                    append_draw(subset.start_index_location, subset.index_count,
                        subset.material_unique_id, material_slot_cursor++);
                }
            }
        }
    }

    if (options.include_auxiliary_geometry && editor_mode && !object_scene_play_mode)
    {
        using ReplayEngine::Components::EditorNoteComponent;
        using ReplayEngine::Components::UITextComponent;
        constexpr float world_units_per_pixel = 0.01f;
        for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
        {
            const ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy())
                continue;
            for (const EditorNoteComponent* note : object->GetComponents<EditorNoteComponent>())
            {
                if (note == nullptr || !note->ActiveInHierarchy() || !note->show_in_viewport ||
                    note->mode != EditorNoteComponent::World || note->text.empty() ||
                    (note->completed && note->hide_when_completed))
                    continue;

                UITextComponent layout;
                layout.text = note->text;
                layout.font_size = 64.0f;
                layout.horizontal_align = UITextComponent::Left;
                layout.vertical_align = UITextComponent::Top;
                layout.word_wrap = false;
                ui_font_atlas.BuildGlyphs(layout, 16384.0f, 4096.0f, &asset_database);
                if (layout.Glyphs().empty()) continue;

                std::string atlas_face_key;
                std::uint64_t atlas_revision = 0;
                if (!ui_font_atlas.ActiveAtlasRevision(atlas_face_key, atlas_revision)) continue;
                const std::string texture_key = "editor-note-font:" + atlas_face_key + ":" +
                    std::to_string(atlas_revision);
                if (!dx12_device_context.HasStaticTexture(texture_key) &&
                    texture_source_keys.insert(texture_key).second)
                {
                    D3D12StaticTextureSource texture;
                    texture.key = texture_key;
                    std::string copied_face_key;
                    std::uint64_t copied_revision = 0;
                    if (!ui_font_atlas.CopyActiveAtlas(copied_face_key, texture.rgba,
                        texture.width, texture.height, copied_revision) ||
                        copied_face_key != atlas_face_key || copied_revision != atlas_revision)
                        continue;
                    submission.texture_sources.push_back(std::move(texture));
                }

                float minimum_x = (std::numeric_limits<float>::max)();
                float minimum_y = (std::numeric_limits<float>::max)();
                float maximum_x = (std::numeric_limits<float>::lowest)();
                float maximum_y = (std::numeric_limits<float>::lowest)();
                for (const UITextComponent::GlyphQuad& glyph : layout.Glyphs())
                {
                    minimum_x = (std::min)(minimum_x, glyph.position.x);
                    maximum_x = (std::max)(maximum_x, glyph.position.x + glyph.size.x);
                    minimum_y = (std::min)(minimum_y, -glyph.position.y - glyph.size.y);
                    maximum_y = (std::max)(maximum_y, -glyph.position.y);
                }
                float align_x = -minimum_x;
                if (note->horizontal_align == EditorNoteComponent::Center)
                    align_x = -(minimum_x + maximum_x) * 0.5f;
                else if (note->horizontal_align == EditorNoteComponent::Right)
                    align_x = -maximum_x;
                float align_y = -maximum_y;
                if (note->vertical_align == EditorNoteComponent::Middle)
                    align_y = -(minimum_y + maximum_y) * 0.5f;
                else if (note->vertical_align == EditorNoteComponent::Bottom)
                    align_y = -minimum_y;

                std::size_t mesh_signature = std::hash<std::string>{}(note->text);
                mesh_signature ^= static_cast<std::size_t>(note->horizontal_align + 17) +
                    (mesh_signature << 6) + (mesh_signature >> 2);
                mesh_signature ^= static_cast<std::size_t>(note->vertical_align + 31) +
                    (mesh_signature << 6) + (mesh_signature >> 2);
                mesh_signature ^= static_cast<std::size_t>(atlas_revision) +
                    (mesh_signature << 6) + (mesh_signature >> 2);
                const std::string mesh_key = "editor-note:" +
                    std::to_string(object->ID().Value()) + ":" +
                    std::to_string(note->StableID()) + ":" + std::to_string(mesh_signature);
                if (!dx12_device_context.HasStaticMesh(mesh_key) &&
                    mesh_source_keys.insert(mesh_key).second)
                {
                    D3D12StaticMeshSource mesh;
                    mesh.key = mesh_key;
                    mesh.vertices.reserve(layout.Glyphs().size() * 4);
                    mesh.indices.reserve(layout.Glyphs().size() * 6);
                    for (const UITextComponent::GlyphQuad& glyph : layout.Glyphs())
                    {
                        const float left = (glyph.position.x + align_x) * world_units_per_pixel;
                        const float right = (glyph.position.x + glyph.size.x + align_x) *
                            world_units_per_pixel;
                        const float top = (-glyph.position.y + align_y) * world_units_per_pixel;
                        const float bottom = (-glyph.position.y - glyph.size.y + align_y) *
                            world_units_per_pixel;
                        const float uv_left = glyph.uv.x;
                        const float uv_top = glyph.uv.y;
                        const float uv_right = glyph.uv.x + glyph.uv.z;
                        const float uv_bottom = glyph.uv.y + glyph.uv.w;
                        const std::uint32_t base = static_cast<std::uint32_t>(
                            mesh.vertices.size());
                        D3D12StaticVertex vertices[4]{};
                        vertices[0].position = { left, top, 0.0f };
                        vertices[1].position = { right, top, 0.0f };
                        vertices[2].position = { right, bottom, 0.0f };
                        vertices[3].position = { left, bottom, 0.0f };
                        vertices[0].texcoord = { uv_left, uv_top };
                        vertices[1].texcoord = { uv_right, uv_top };
                        vertices[2].texcoord = { uv_right, uv_bottom };
                        vertices[3].texcoord = { uv_left, uv_bottom };
                        for (D3D12StaticVertex& vertex : vertices)
                            vertex.normal = { 0.0f, 0.0f, 1.0f };
                        mesh.vertices.insert(mesh.vertices.end(), std::begin(vertices),
                            std::end(vertices));
                        mesh.indices.insert(mesh.indices.end(),
                            { base, base + 1, base + 2, base, base + 2, base + 3 });
                    }
                    submission.mesh_sources.push_back(std::move(mesh));
                }

                D3D12StaticDrawItem draw;
                draw.mesh_key = mesh_key;
                draw.owner_id = object->ID().Value();
                draw.motion_key = mesh_key;
                const DirectX::XMFLOAT4X4 owner_world =
                    object->GetTransform().WorldMatrixFloat4x4();
                const float note_scale = (std::max)(0.01f, note->text_scale);
                DirectX::XMStoreFloat4x4(&draw.world,
                    DirectX::XMMatrixScaling(note_scale, note_scale, note_scale) *
                    DirectX::XMMatrixTranslation(note->offset.x, note->offset.y, note->offset.z) *
                    DirectX::XMLoadFloat4x4(&owner_world));
                draw.base_color = note->color;
                if (note->completed) draw.base_color.w *= 0.45f;
                draw.base_color_texture_key = texture_key;
                draw.alpha_mode = D3D12StaticAlphaMode::Blend;
                draw.lighting_model = static_cast<std::int32_t>(ShaderLightingModel::Unlit);
                draw.double_sided = true;
                draw.cast_shadow = false;
                draw.receive_shadow = false;
                submission.draws.push_back(std::move(draw));
            }
        }
    }

    // Landscapeは既存LandscapeDataの頂点/Indexを正本として、そのままStatic Mesh提出へ変換する。
    // D3D11専用のstatic_meshキャッシュをDX12から参照しないため、編集後のRevisionもキーへ含める。
    if (options.include_auxiliary_geometry)
    {
        for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
        {
            const ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;
            const auto* landscape = object->GetComponent<ReplayEngine::Components::LandscapeComponent>();
            const auto* renderer = object->GetComponent<ReplayEngine::Components::LandscapeRendererComponent>();
            if (landscape == nullptr || renderer == nullptr || !renderer->visible ||
                !renderer->ActiveInHierarchy() || !landscape->Data().Valid()) continue;

            const auto& data = landscape->Data();
            const std::string mesh_key = "landscape:" +
                std::to_string(object->ID().Value()) + ":" + std::to_string(data.Revision());
            if (!dx12_device_context.HasStaticMesh(mesh_key))
            {
                D3D12StaticMeshSource source;
                source.key = mesh_key;
                source.vertices.reserve(data.Vertices().size());
                for (const auto& input : data.Vertices())
                {
                    D3D12StaticVertex vertex;
                    vertex.position = input.position;
                    vertex.normal = input.normal;
                    vertex.texcoord = input.uv;
                    source.vertices.push_back(vertex);
                }
                source.indices = data.Indices();
                submission.mesh_sources.push_back(std::move(source));
            }

            D3D12StaticDrawItem draw;
            draw.mesh_key = mesh_key;
            draw.owner_id = object->ID().Value();
            draw.rendering_layer = 0;
            draw.motion_key = std::to_string(object->ID().Value()) + ":" + mesh_key;
            draw.world = object->GetTransform().WorldMatrixFloat4x4();
            draw.base_color = renderer->tint;
            draw.double_sided = renderer->double_sided;
            draw.cast_shadow = renderer->cast_shadow;
            draw.receive_shadow = renderer->receive_shadow;
            draw.lighting_model = static_cast<std::int32_t>(ShaderLightingModel::Pbr);
            submission.draws.push_back(std::move(draw));
        }
    }

    if (options.include_active_lighting)
    {
        submission.directional_light.enabled = directional_light_present;
        submission.directional_light.direction = { light_direction.x, light_direction.y, light_direction.z };
        submission.directional_light.color = { pbr.light.directional_color.x,
            pbr.light.directional_color.y, pbr.light.directional_color.z };
        submission.directional_light.intensity = pbr.light.directional_color.w;
        submission.directional_light.cast_shadows = directional_shadow_enabled;
        submission.directional_light.shadow_strength = pbr.light.shadow_params.x;

        const int point_count = (std::max)(0, (std::min)(lights_manager::POINT_LIGHT_MAX,
            lights.data.light_counts.x));
        submission.point_lights.reserve(static_cast<std::size_t>(point_count));
        for (int index = 0; index < point_count; ++index)
        {
            const lights_manager::point_light& source = lights.data.point_lights[index];
            D3D12PointLightSubmission light;
            light.position = { source.position.x, source.position.y, source.position.z };
            light.range = source.position.w;
            light.color = { source.color.x, source.color.y, source.color.z };
            light.intensity = source.color.w;
            light.cast_shadows = source.shadow.x >= 0.0f;
            light.shadow_strength = source.shadow.y;
            light.shadow_slice = static_cast<std::int32_t>(source.shadow.x);
            submission.point_lights.push_back(light);
        }

        const int spot_count = (std::max)(0, (std::min)(lights_manager::SPOT_LIGHT_MAX,
            lights.data.light_counts.y));
        submission.spot_lights.reserve(static_cast<std::size_t>(spot_count));
        for (int index = 0; index < spot_count; ++index)
        {
            const lights_manager::spot_light& source = lights.data.spot_lights[index];
            D3D12SpotLightSubmission light;
            light.position = { source.position.x, source.position.y, source.position.z };
            light.range = source.position.w;
            light.direction = { source.direction.x, source.direction.y, source.direction.z };
            light.inner_cos = source.direction.w;
            light.color = { source.color.x, source.color.y, source.color.z };
            light.outer_cos = source.color.w;
            light.intensity = source.params.x;
            light.cast_shadows = source.params.y >= 0.0f;
            light.shadow_strength = source.params.z;
            light.shadow_slice = static_cast<std::int32_t>(source.params.y);
            submission.spot_lights.push_back(light);
        }

        // ShadowのProjection/AllocationにおけるCPU正本は既存CSM/LocalShadowAtlas。
        // DX12側で別のCascade分割やPoint Face選択を作らず、確定済み値だけを提出する。
        submission.directional_shadow.enabled =
            csm.constants.params.w > 0.5f && submission.directional_light.cast_shadows;
        submission.directional_shadow.resolution = csm_renderer::SHADOW_MAP_SIZE;
        for (std::uint32_t cascade = 0;
            cascade < D3D12DirectionalShadowSubmission::CascadeCount; ++cascade)
            submission.directional_shadow.view_projection[cascade] =
                csm.constants.view_projection[cascade];
        submission.directional_shadow.split_distances = csm.constants.split_distances;
        submission.directional_shadow.params = csm.constants.params;
        submission.directional_shadow.params2 = csm.constants.params2;
        submission.directional_shadow.params3 = csm.constants.params3;
        submission.directional_shadow.texel_world = csm.constants.texel_world;

        submission.local_shadows.enabled = enable_dynamic_shadows && local_shadows.enabled;
        submission.local_shadows.resolution = local_shadows.resolution_setting;
        for (std::uint32_t slice = 0;
            slice < D3D12LocalShadowSubmission::SliceCount; ++slice)
        {
            ReplayEngine::Rendering::LocalShadowAtlas::Slice source{};
            if (!local_shadows.TryGetSlice(slice, source)) continue;
            submission.local_shadows.slices[slice].view_projection = source.view_projection;
            submission.local_shadows.slices[slice].params = source.params;
            submission.local_shadows.used_slice_mask |= (1u << slice);
        }
    }
    return true;
}

bool framework::build_dx12_lighting_for_scene(
    ReplayEngine::Rendering::DX12::D3D12StaticSceneSubmission& submission,
    const ReplayEngine::Scene::Scene& scene) const
{
    using ReplayEngine::Components::DirectionalLightComponent;
    using ReplayEngine::Components::PointLightComponent;
    using ReplayEngine::Components::SpotLightComponent;
    using namespace ReplayEngine::Rendering::DX12;
    using namespace DirectX;

    submission.directional_light = {};
    submission.point_lights.clear();
    submission.spot_lights.clear();
    submission.directional_shadow = {};
    submission.local_shadows = {};
    bool directional_found = false;
    submission.point_lights.reserve(lights_manager::POINT_LIGHT_MAX);
    submission.spot_lights.reserve(lights_manager::SPOT_LIGHT_MAX);

    for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
    {
        const ReplayEngine::Core::GameObject* object = scene.GameObjectAt(index);
        if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;

        if (!directional_found)
        {
            const DirectionalLightComponent* light = object->GetComponent<DirectionalLightComponent>();
            if (light != nullptr && light->ActiveInHierarchy())
            {
                const XMFLOAT4 rotation = object->GetTransform().WorldRotationQuaternion();
                XMVECTOR direction = XMVector3Rotate(XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f),
                    XMLoadFloat4(&rotation));
                direction = XMVector3Normalize(direction);
                XMStoreFloat3(&submission.directional_light.direction, direction);
                submission.directional_light.enabled = true;
                submission.directional_light.color = {
                    light->color.x, light->color.y, light->color.z };
                submission.directional_light.intensity = (std::max)(0.0f, light->intensity);
                submission.directional_light.cast_shadows = false;
                submission.directional_light.shadow_strength =
                    (std::max)(0.0f, (std::min)(1.0f, light->shadow_strength));
                directional_found = true;
            }
        }

        if (submission.point_lights.size() <
            static_cast<std::size_t>(lights_manager::POINT_LIGHT_MAX))
        {
            const PointLightComponent* light = object->GetComponent<PointLightComponent>();
            if (light != nullptr && light->ActiveInHierarchy())
            {
                D3D12PointLightSubmission output{};
                output.position = object->GetTransform().WorldPosition();
                output.range = (std::max)(0.01f, light->range);
                output.color = { light->color.x, light->color.y, light->color.z };
                output.intensity = (std::max)(0.0f, light->intensity);
                output.cast_shadows = false;
                output.shadow_strength =
                    (std::max)(0.0f, (std::min)(1.0f, light->shadow_strength));
                output.shadow_slice = -1;
                submission.point_lights.push_back(output);
            }
        }

        if (submission.spot_lights.size() <
            static_cast<std::size_t>(lights_manager::SPOT_LIGHT_MAX))
        {
            const SpotLightComponent* light = object->GetComponent<SpotLightComponent>();
            if (light != nullptr && light->ActiveInHierarchy())
            {
                const XMFLOAT4 rotation = object->GetTransform().WorldRotationQuaternion();
                XMVECTOR direction = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
                    XMLoadFloat4(&rotation));
                direction = XMVector3Normalize(direction);
                D3D12SpotLightSubmission output{};
                output.position = object->GetTransform().WorldPosition();
                output.range = (std::max)(0.01f, light->range);
                XMStoreFloat3(&output.direction, direction);
                const float outer = (std::max)(0.1f,
                    (std::min)(179.0f, light->outer_angle_degrees));
                const float inner = (std::max)(0.1f,
                    (std::min)(outer, light->inner_angle_degrees));
                output.inner_cos = std::cos(XMConvertToRadians(inner));
                output.outer_cos = std::cos(XMConvertToRadians(outer));
                output.color = { light->color.x, light->color.y, light->color.z };
                output.intensity = (std::max)(0.0f, light->intensity);
                output.cast_shadows = false;
                output.shadow_strength =
                    (std::max)(0.0f, (std::min)(1.0f, light->shadow_strength));
                output.shadow_slice = -1;
                submission.spot_lights.push_back(output);
            }
        }
    }
    return true;
}

bool framework::prewarm_loading_scene_gpu_resources()
{
    if (!dx12_device_context.IsInitialized() || object_loading_scene == nullptr) return true;

    ReplayEngine::Rendering::RenderItemList render_items;
    ReplayEngine::Rendering::SceneRenderCollector::Collect(*object_loading_scene, render_items);
    ReplayEngine::Rendering::DX12::D3D12StaticSceneSubmission submission;
    dx12_scene_build_options options;
    options.include_auxiliary_geometry = false;
    options.include_active_lighting = false;
    if (!build_dx12_static_scene(submission, *object_loading_scene, render_items, 0.0f, options))
        return false;

    const std::size_t skinned_render_item_count = static_cast<std::size_t>(std::count_if(
        render_items.Items().begin(), render_items.Items().end(),
        [](const ReplayEngine::Rendering::RenderItem& item) noexcept { return item.skinned; }));
    const std::size_t static_source_count = submission.mesh_sources.size();
    const std::size_t skinned_source_count = submission.skinned_mesh_sources.size();
    const std::size_t static_draw_count = submission.draws.size();
    const std::size_t skinned_draw_count = submission.skinned_draws.size();
    const std::size_t texture_candidate_count = submission.texture_sources.size();
    std::size_t skinned_component_count = 0;
    for (std::size_t object_index = 0; object_index < object_loading_scene->GameObjectCount();
        ++object_index)
    {
        const ReplayEngine::Core::GameObject* object =
            object_loading_scene->GameObjectAt(object_index);
        if (object == nullptr) continue;
        for (std::size_t component_index = 0; component_index < object->ComponentCount();
            ++component_index)
        {
            const ReplayEngine::Core::Component* component = object->ComponentAt(component_index);
            if (component == nullptr) continue;
            const bool is_skinned = dynamic_cast<const
                ReplayEngine::Components::SkinnedMeshRendererComponent*>(component) != nullptr;
            if (is_skinned) ++skinned_component_count;
            std::fprintf(stderr,
                "[Loading3D] component: object=%llu component=%zu type=\"%s\" "
                "is_skinned_renderer=%d active=%d\n",
                static_cast<unsigned long long>(object->ID().Value()), component_index,
                component->TypeName(), is_skinned ? 1 : 0,
                component->ActiveInHierarchy() ? 1 : 0);
        }
    }
    for (std::size_t index = 0; index < render_items.Items().size(); ++index)
    {
        const ReplayEngine::Rendering::RenderItem& item = render_items.Items()[index];
        std::fprintf(stderr,
            "[Loading3D] item[%zu]: owner=%llu skinned=%d mesh=\"%s\"\n",
            index, static_cast<unsigned long long>(item.owner.Value()),
            item.skinned ? 1 : 0, item.mesh_asset.c_str());
    }
    submission.texture_sources.clear();
    submission.shader_sources.clear();
    const auto before = dx12_device_context.CaptureScene3DState();
    const auto begin = std::chrono::steady_clock::now();
    const bool ok = dx12_device_context.PreloadScene3DResources(submission, false);
    const double elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    const auto after = dx12_device_context.CaptureScene3DState();
    std::fprintf(stderr,
        "[Loading3D] GPU preload %s: %.3f ms, static +%zu, skinned +%zu, texture +%zu\n",
        ok ? "OK" : "FAILED", elapsed_ms,
        after.static_mesh_cache_size - before.static_mesh_cache_size,
        after.skinned_mesh_cache_size - before.skinned_mesh_cache_size,
        after.texture_cache_size - before.texture_cache_size);
    std::fprintf(stderr,
        "[Loading3D] preload inputs: items %zu (skinned %zu), components skinned %zu, "
        "sources static %zu skinned %zu, "
        "draws static %zu skinned %zu, texture candidates %zu (disabled)\n",
        render_items.Size(), skinned_render_item_count, skinned_component_count, static_source_count,
        skinned_source_count, static_draw_count, skinned_draw_count,
        texture_candidate_count);
    return ok;
}
