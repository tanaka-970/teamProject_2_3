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
#include "../../RePlayEngine/Components/UI/UIEffectStackComponent.h"
#include "../../RePlayEngine/Components/UI/UISpriteAnimatorComponent.h"
#include "../../RePlayEngine/Components/UI/UITextComponent.h"
#include "../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/MaterialOverrideDynamicProperties.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../RePlayEngine/Rendering/Shaders/BuiltInShaders.h"
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
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>


ReplayEngine::Rendering::RenderItem framework::resolve_render_item_material(
    const ReplayEngine::Rendering::RenderItem& source)
{
    using namespace ReplayEngine::Rendering;

    RenderItem item = source;
    item.legacy_tint = source.tint;
    item.lighting_model = deferred_lighting_model(source.shading_model);

    const MaterialAsset* material = resolve_object_material(source.material_asset);
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

void framework::draw_object_scene_meshes(ID3D11PixelShader* override_pixel_shader,
    bool gbuffer_pass, bool depth_only, ReplayEngine::Core::ObjectID only_owner,
    bool skip_model_effect_owners, std::uint32_t rendering_layer_mask)
{
    if (object_render_items.Empty()) return;

    for (const ReplayEngine::Rendering::RenderItem& source_item : object_render_items.Items())
    {
        if (only_owner.Valid() && source_item.owner != only_owner) continue;
        if (rendering_layer_mask != 0xFFFFFFFFu)
        {
            const int layer = (std::max)(0, (std::min)(31, source_item.rendering_layer));
            if ((rendering_layer_mask & (1u << static_cast<unsigned int>(layer))) == 0u) continue;
        }
        if (skip_model_effect_owners && !only_owner.Valid())
        {
            const ReplayEngine::Core::GameObject* owner =
                active_object_scene().FindGameObjectByID(source_item.owner);
            const auto* model_effect = owner != nullptr
                ? owner->GetComponent<ReplayEngine::Components::ModelEffectStackComponent>()
                : nullptr;
            if (model_effect != nullptr && model_effect->ActiveInHierarchy() &&
                model_effect->enabled && model_effect->HasActiveEffects(&asset_database))
            {
                continue;
            }
        }

        const ReplayEngine::Rendering::RenderItem item =
            resolve_render_item_material(source_item);
        // Asset 未指定・解決不可・読み込み失敗のいずれでも nullptr が返る。
        // その場合はこの GameObject を描かずに次へ進むだけで、実行は継続する。
        if (item.mesh_asset.empty()) continue;

        // Engine 内蔵 Primitive も通常の MeshRendererComponent から提出される。
        // 特殊な Primitive GameObject は作らず、asset id だけ builtin:* を使う。
        if (item.mesh_asset.rfind("builtin:", 0) == 0)
        {
            static_mesh* primitive = resolve_builtin_primitive_mesh(item.mesh_asset);
            if (primitive == nullptr) continue;

            if (item.double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

            if (depth_only)
            {
                primitive->render(immediate_context.Get(), item.world, item.tint,
                    nullptr, nullptr, nullptr, false, false);
            }
            else if (gbuffer_pass)
            {
                if (item.material_binding.usable_shader)
                {
                    material_gpu_binder.BindGBufferTextures(device.Get(), immediate_context.Get(),
                        asset_database, item.material_binding);
                }
                else
                {
                    material_gpu_binder.UnbindTextures(immediate_context.Get());
                }
                bind_gbuffer_material(item.lighting_model,
                    false, item.pixelate_enabled, item.pixelate_size,
                    item.pixelate_strength, item.metallic, item.roughness,
                    item.ambient_occlusion, item.emissive_strength,
                    item.material_base_color, item.emissive_color,
                    item.material_binding.usable_shader
                        ? item.material_binding.TextureSemanticMask() : 0u,
                    item.receive_shadow);
                primitive->render(immediate_context.Get(), item.world, item.tint,
                    static_mesh_gbuffer_ps.Get(), nullptr, nullptr, true, true);
                material_gpu_binder.UnbindTextures(immediate_context.Get());
            }
            else
            {
                primitive->render(immediate_context.Get(), item.world, item.tint,
                    override_pixel_shader != nullptr ? override_pixel_shader :
                        static_forward_shader(item.shading_model));
            }

            if (item.double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
            continue;
        }

        // Skin/Animationを持たないglTFは従来の軽い静的経路を維持する。
        // 持つものだけ既存skinned_mesh/Animator経路へ送る。
        gltf_model* gltf = resolve_object_gltf(item.mesh_asset);
        if (gltf != nullptr && !gltf->HasSkins() && !gltf->HasAnimations())
        {
            if (item.double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

            if (depth_only)
            {
                gltf->render(immediate_context.Get(), item.world, item.tint,
                    nullptr, false, true);
            }
            else if (gbuffer_pass)
            {
                // Unity/Unrealと同じ優先順位: GLB内蔵Materialを既定にし、
                // RePlay Materialが明示指定された場合だけ外部Materialで上書きする。
                // 仮Materialのwhite BaseMap(t40)でGLBのBaseColor(t0)を隠さない。
                const bool use_external_material = !source_item.material_asset.empty() &&
                    item.material_binding.usable_shader;
                if (use_external_material)
                {
                    material_gpu_binder.BindGBufferTextures(device.Get(), immediate_context.Get(),
                        asset_database, item.material_binding);
                }
                else
                {
                    material_gpu_binder.UnbindTextures(immediate_context.Get());
                }
                bind_gbuffer_material(item.lighting_model,
                    false, item.pixelate_enabled, item.pixelate_size,
                    item.pixelate_strength, item.metallic, item.roughness,
                    item.ambient_occlusion, item.emissive_strength,
                    item.material_base_color, item.emissive_color,
                    use_external_material
                        ? item.material_binding.TextureSemanticMask() : 0u,
                    item.receive_shadow);
                gltf->render(immediate_context.Get(), item.world, item.tint,
                    static_mesh_gbuffer_ps.Get(), true, false);
                material_gpu_binder.UnbindTextures(immediate_context.Get());
            }
            else
            {
                gltf->render(immediate_context.Get(), item.world, item.legacy_tint,
                    override_pixel_shader != nullptr ? override_pixel_shader :
                        static_forward_shader(item.shading_model), false, false);
            }

            if (item.double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
            continue;
        }

        skinned_mesh* mesh = resolve_object_mesh(item.mesh_asset);
        if (mesh == nullptr) continue;
        const bool use_embedded_gltf_materials = mesh->IsGltf() &&
            source_item.material_asset.empty();
        const bool draw_double_sided = item.double_sided ||
            (use_embedded_gltf_materials && mesh->HasDoubleSidedMaterials());

        // Animator の current / previous clip と blend factor は RenderItem だけを
        // 介して Renderer へ渡す。Motion Runtime とは混ぜず、既存の
        // skinned_mesh::blend_animations() で姿勢を作る。
        skinned_mesh::animation::keyframe blended_keyframe;
        const skinned_mesh::animation::keyframe* keyframe =
            resolve_render_item_keyframe(*mesh, item, blended_keyframe);

        if (depth_only)
        {
            // 深度プリパス。ピクセルシェーダーを外し、モーションベクターも書かない。
            //
            // 【ここを通さないと何も見えない】
            //   本描画は DepthFunc=EQUAL で走る。プリパスで深度を書いていない
            //   メッシュは深度比較に必ず失敗し、画面から丸ごと消える。
            //   GBuffer へ出すものは、例外なくここでも描くこと。
            if (draw_double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
            mesh->render(immediate_context.Get(), item.world, item.tint,
                keyframe, nullptr, nullptr, nullptr, false, false,
                use_embedded_gltf_materials);
            if (draw_double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
            continue;
        }

        // GBuffer パスでは Component が指定した描画方式を材質定数へ流す。
        if (gbuffer_pass)
        {
            if (item.material_binding.usable_shader)
            {
                material_gpu_binder.BindGBufferTextures(device.Get(), immediate_context.Get(),
                    asset_database, item.material_binding);
            }
            else
            {
                material_gpu_binder.UnbindTextures(immediate_context.Get());
            }

            bind_gbuffer_material(item.lighting_model,
                false, item.pixelate_enabled, item.pixelate_size,
                item.pixelate_strength, item.metallic, item.roughness,
                item.ambient_occlusion, item.emissive_strength,
                item.material_base_color,
                item.emissive_color,
                item.material_binding.usable_shader
                    ? item.material_binding.TextureSemanticMask() : 0u,
                item.receive_shadow);
        }

        // 最後の引数がモーションベクター出力。GBuffer パスだけで真にする
        // （複数回渡すと前フレーム姿勢が壊れる）。
        if (draw_double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        mesh->render(immediate_context.Get(), item.world, item.tint,
            keyframe, override_pixel_shader, nullptr, nullptr, true, gbuffer_pass,
            use_embedded_gltf_materials);
        if (gbuffer_pass)
            material_gpu_binder.UnbindTextures(immediate_context.Get());
        if (draw_double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
    }
}


namespace
{
    // ローカルAABBを外接球にして影ボリュームと判定する。届かない物体は描かない。
    bool shadow_volume_intersects(const DirectX::XMFLOAT3& local_minimum,
        const DirectX::XMFLOAT3& local_maximum,
        const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT3& volume_center, float volume_radius,
        float extrusion)
    {
        // 影ボリュームがまだ作られていないフレームは捨てない（安全側）。
        if (!(volume_radius > 0.0f)) return true;
        if (local_minimum.x > local_maximum.x) return true; // 未設定のAABB

        const DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&world);
        DirectX::XMFLOAT3 minimum{ FLT_MAX, FLT_MAX, FLT_MAX };
        DirectX::XMFLOAT3 maximum{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (int corner = 0; corner < 8; ++corner)
        {
            const DirectX::XMVECTOR local = DirectX::XMVectorSet(
                (corner & 1) ? local_maximum.x : local_minimum.x,
                (corner & 2) ? local_maximum.y : local_minimum.y,
                (corner & 4) ? local_maximum.z : local_minimum.z, 1.0f);
            DirectX::XMFLOAT3 transformed{};
            DirectX::XMStoreFloat3(&transformed,
                DirectX::XMVector3TransformCoord(local, matrix));
            minimum.x = (std::min)(minimum.x, transformed.x);
            minimum.y = (std::min)(minimum.y, transformed.y);
            minimum.z = (std::min)(minimum.z, transformed.z);
            maximum.x = (std::max)(maximum.x, transformed.x);
            maximum.y = (std::max)(maximum.y, transformed.y);
            maximum.z = (std::max)(maximum.z, transformed.z);
        }

        const DirectX::XMFLOAT3 center{
            (minimum.x + maximum.x) * 0.5f,
            (minimum.y + maximum.y) * 0.5f,
            (minimum.z + maximum.z) * 0.5f };
        const DirectX::XMFLOAT3 extent{
            (maximum.x - minimum.x) * 0.5f,
            (maximum.y - minimum.y) * 0.5f,
            (maximum.z - minimum.z) * 0.5f };
        const float object_radius = std::sqrt(
            extent.x * extent.x + extent.y * extent.y + extent.z * extent.z);

        const float dx = center.x - volume_center.x;
        const float dy = center.y - volume_center.y;
        const float dz = center.z - volume_center.z;
        const float distance_squared = dx * dx + dy * dy + dz * dz;
        const float reach = volume_radius + object_radius +
            (std::max)(extrusion, 0.0f);
        return distance_squared <= reach * reach;
    }
}


framework::shadow_material_state framework::resolve_shadow_material_state(
    const ReplayEngine::Rendering::RenderItem& source)
{
    using ReplayEngine::Rendering::MaterialAlphaMode;
    shadow_material_state state{};

    const ReplayEngine::Rendering::MaterialAsset* material =
        resolve_object_material(source.material_asset);

    state.double_sided = source.double_sided ||
        (material != nullptr && material->double_sided) ||
        (source.material_override && source.override_material_double_sided);

    if (material != nullptr && material->alpha_mode != MaterialAlphaMode::Opaque)
    {
        // Material Asset がアルファ抜きを宣言している。BaseMap は t40 側。
        state.alpha_mode = 1;
        state.alpha_cutoff = material->alpha_mode == MaterialAlphaMode::Mask
            ? material->alpha_cutoff : 0.01f;
        state.uses_replay_base_map = !material->base_color_texture.empty();
    }
    return state;
}

void framework::bind_shadow_alpha_constants(const shadow_material_state& state)
{
    if (shadow_alpha_cb == nullptr) return;
    shadow_alpha_constants constants{};
    constants.params = {
        static_cast<float>(state.alpha_mode),
        state.alpha_cutoff,
        state.uses_replay_base_map ? 1.0f : 0.0f,
        0.0f };
    immediate_context->UpdateSubresource(shadow_alpha_cb.Get(), 0, nullptr, &constants, 0, 0);
    immediate_context->PSSetConstantBuffers(7, 1, shadow_alpha_cb.GetAddressOf());
}

void framework::set_shadow_cull_state(bool double_sided, const DirectX::XMFLOAT4X4& world)
{
    if (double_sided)
    {
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        return;
    }

    // 3x3 の行列式が負なら鏡像。FBX 座標変換のように反転が入ると巻き順も
    // 裏返るため、そのまま裏面カリングすると影の深度が反対側の面になる。
    const float determinant =
        world._11 * (world._22 * world._33 - world._23 * world._32) -
        world._12 * (world._21 * world._33 - world._23 * world._31) +
        world._13 * (world._21 * world._32 - world._22 * world._31);
    immediate_context->RSSetState(determinant < 0.0f
        ? rasterizer_states[(size_t)RASTER_STATE::CULL_FRONT].Get()
        : rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
}


// ライト視点の影深度パスへ Scene の全キャスターを提出する。Directional / Point / Spot 共通。
void framework::draw_shadow_caster_meshes(
    ID3D11VertexShader* static_caster_vs, ID3D11InputLayout* static_caster_il,
    ID3D11VertexShader* skinned_caster_vs, ID3D11InputLayout* skinned_caster_il,
    const DirectX::XMFLOAT3& volume_center, float volume_radius,
    float volume_extrusion)
{
    // 影マップは深度テストと書き込みの両方が要るのでここで明示する。
    immediate_context->OMSetDepthStencilState(
        depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_ON].Get(), 0);
    ReplayEngine::Rendering::Stats().CountStateSet(
        ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
    immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
    ReplayEngine::Rendering::Stats().CountStateSet(
        ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);

    // gltf_model の視錐台カリングはカメラ基準なので影パスの間だけ止める。
    auto& culling = ReplayEngine::Rendering::Culling();
    const bool culling_was_enabled = culling.enabled;
    culling.enabled = false;

    const float extrusion = volume_extrusion;

    for (const ReplayEngine::Rendering::RenderItem& item : object_render_items.Items())
    {
        if (!item.cast_shadow)
        {
            ++shadow_stats.skipped_cast_shadow;
            continue;
        }
        if (item.mesh_asset.empty()) continue;

        const shadow_material_state material_state = resolve_shadow_material_state(item);

        // Engine 内蔵 Primitive。Cube / Sphere などもここを通って影を落とす。
        if (item.mesh_asset.rfind("builtin:", 0) == 0)
        {
            if (static_caster_vs == nullptr) continue;
            static_mesh* primitive = resolve_builtin_primitive_mesh(item.mesh_asset);
            if (primitive == nullptr) continue;
            if (!shadow_volume_intersects(primitive->bounding_box[0],
                primitive->bounding_box[1], item.world,
                volume_center, volume_radius, extrusion))
            {
                ++shadow_stats.culled_casters;
                continue;
            }

            const bool alpha_clip = material_state.alpha_mode != 0 &&
                shadow_caster_alpha_ps != nullptr;
            if (alpha_clip)
            {
                bind_shadow_alpha_constants(material_state);
                if (material_state.uses_replay_base_map)
                {
                    // material_binding は材質解決後の RenderItem にしか入らない。
                    // アルファ抜きのときだけ解決する。
                    const ReplayEngine::Rendering::RenderItem resolved =
                        resolve_render_item_material(item);
                    material_gpu_binder.BindGBufferTextures(device.Get(),
                        immediate_context.Get(), asset_database, resolved.material_binding);
                }
            }
            set_shadow_cull_state(material_state.double_sided, item.world);
            primitive->render(immediate_context.Get(), item.world, item.tint,
                alpha_clip ? shadow_caster_alpha_ps.Get() : nullptr,
                static_caster_vs, static_caster_il, alpha_clip, false);
            if (alpha_clip && material_state.uses_replay_base_map)
                material_gpu_binder.UnbindTextures(immediate_context.Get());
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());

            ++shadow_stats.primitive_casters;
            ++shadow_stats.shadow_draw_calls;
            continue;
        }

        // Skin も Animation も持たない glTF は静的キャスターとして描く。
        gltf_model* gltf = resolve_object_gltf(item.mesh_asset);
        if (gltf != nullptr && !gltf->HasSkins() && !gltf->HasAnimations())
        {
            if (static_caster_vs == nullptr) continue;

            DirectX::XMFLOAT3 local_minimum{}, local_maximum{};
            if (gltf->ComputeBounds(local_minimum, local_maximum) &&
                !shadow_volume_intersects(local_minimum, local_maximum, item.world,
                    volume_center, volume_radius, extrusion))
            {
                ++shadow_stats.culled_casters;
                continue;
            }

            // Material Asset の指定が無ければ glTF 内蔵の alphaMode をそのまま使う。
            const bool alpha_clip = shadow_caster_alpha_ps != nullptr &&
                (material_state.alpha_mode != 0 || gltf->HasAlphaMaskMaterials());
            if (alpha_clip && material_state.uses_replay_base_map)
            {
                const ReplayEngine::Rendering::RenderItem resolved =
                    resolve_render_item_material(item);
                material_gpu_binder.BindGBufferTextures(device.Get(),
                    immediate_context.Get(), asset_database, resolved.material_binding);
            }
            set_shadow_cull_state(material_state.double_sided, item.world);
            gltf->render_shadow(immediate_context.Get(), item.world,
                static_caster_vs, static_caster_il,
                alpha_clip ? shadow_caster_alpha_ps.Get() : nullptr,
                alpha_clip ? shadow_alpha_cb.Get() : nullptr,
                material_state.alpha_mode != 0 ? material_state.alpha_mode : -1,
                material_state.alpha_cutoff,
                material_state.uses_replay_base_map);
            if (alpha_clip && material_state.uses_replay_base_map)
                material_gpu_binder.UnbindTextures(immediate_context.Get());
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());

            ++shadow_stats.static_casters;
            ++shadow_stats.shadow_draw_calls;
            continue;
        }

        // Skinned Mesh。bounding_box が当てにならないので影ボリュームのカリングは掛けない。
        skinned_mesh* mesh = resolve_object_mesh(item.mesh_asset);
        if (mesh == nullptr) { ++shadow_stats.skinned_unresolved; continue; }
        if (skinned_caster_vs == nullptr) continue;

        skinned_mesh::animation::keyframe blended_keyframe;
        const skinned_mesh::animation::keyframe* keyframe =
            resolve_render_item_keyframe(*mesh, item, blended_keyframe);

        const bool use_embedded_gltf_materials = mesh->IsGltf() &&
            item.material_asset.empty();
        const bool draw_double_sided = material_state.double_sided ||
            (use_embedded_gltf_materials && mesh->HasDoubleSidedMaterials());

        // 内蔵材質側の alpha_mode は subset ごとに b0 へ載るので、PS 内で見る。
        shadow_material_state skinned_alpha = material_state;
        if (skinned_alpha.alpha_mode == 0 && use_embedded_gltf_materials &&
            mesh->HasAlphaMaskMaterials())
        {
            skinned_alpha.alpha_mode = 2;
            skinned_alpha.uses_replay_base_map = false;
        }
        const bool alpha_clip = skinned_alpha.alpha_mode != 0 &&
            shadow_caster_alpha_skinned_ps != nullptr;
        if (alpha_clip)
        {
            bind_shadow_alpha_constants(skinned_alpha);
            if (skinned_alpha.uses_replay_base_map)
            {
                const ReplayEngine::Rendering::RenderItem resolved =
                    resolve_render_item_material(item);
                material_gpu_binder.BindGBufferTextures(device.Get(),
                    immediate_context.Get(), asset_database, resolved.material_binding);
            }
        }

        set_shadow_cull_state(draw_double_sided, item.world);
        // write_motion_vectors は必ず false。影パスで進めると TAA が尾を引く。
        mesh->render(immediate_context.Get(), item.world, item.tint,
            keyframe, alpha_clip ? shadow_caster_alpha_skinned_ps.Get() : nullptr,
            skinned_caster_vs, skinned_caster_il,
            alpha_clip, false, use_embedded_gltf_materials);
        if (alpha_clip && skinned_alpha.uses_replay_base_map)
            material_gpu_binder.UnbindTextures(immediate_context.Get());
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());

        ++shadow_stats.skinned_casters;
        ++shadow_stats.shadow_draw_calls;
    }

    // Landscape も Mesh と同じ影パスへ入れる。専用の影処理は作らない。
    if (static_caster_vs != nullptr)
    {
        ReplayEngine::Scene::Scene& scene = active_object_scene();
        for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
        {
            ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy() ||
                !object->ActiveInHierarchy()) continue;

            auto* renderer =
                object->GetComponent<ReplayEngine::Components::LandscapeRendererComponent>();
            if (renderer == nullptr || !renderer->visible ||
                !renderer->ActiveInHierarchy()) continue;
            if (!renderer->cast_shadow)
            {
                ++shadow_stats.skipped_cast_shadow;
                continue;
            }

            static_mesh* landscape_mesh = resolve_landscape_gpu_mesh(*object);
            if (landscape_mesh == nullptr) continue;

            const DirectX::XMFLOAT4X4 world = object->GetTransform().WorldMatrixFloat4x4();
            if (!shadow_volume_intersects(landscape_mesh->bounding_box[0],
                landscape_mesh->bounding_box[1], world,
                volume_center, volume_radius, extrusion))
            {
                ++shadow_stats.culled_casters;
                continue;
            }

            set_shadow_cull_state(renderer->double_sided, world);
            landscape_mesh->render(immediate_context.Get(), world, renderer->tint,
                nullptr, static_caster_vs, static_caster_il, false, false);
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());

            ++shadow_stats.landscape_casters;
            ++shadow_stats.shadow_draw_calls;
        }
    }

    // 影パスで貼った Pixel Shader / 材質定数を次のパスへ持ち越さない。
    immediate_context->PSSetShader(nullptr, nullptr, 0);
    culling.enabled = culling_was_enabled;
}


static_mesh* framework::resolve_landscape_gpu_mesh(
    const ReplayEngine::Core::GameObject& object)
{
    const auto* landscape =
        object.GetComponent<ReplayEngine::Components::LandscapeComponent>();
    if (landscape == nullptr) return nullptr;

    const auto& data = landscape->Data();
    if (!data.Valid()) return nullptr;

    const std::uint64_t cache_key = object.ID().Value();
    landscape_gpu_cache_entry& cache = landscape_gpu_mesh_cache[cache_key];
    if (cache.revision != data.Revision() || cache.mesh == nullptr)
    {
        std::vector<static_mesh::vertex> vertices;
        vertices.reserve(data.VertexCount());
        for (const ReplayEngine::Landscape::LandscapeVertex& source : data.Vertices())
        {
            static_mesh::vertex vertex{};
            vertex.position = source.position;
            vertex.normal = source.normal;
            vertex.texcoord = source.uv;
            vertices.push_back(vertex);
        }

        bool gpu_ready = false;
        if (cache.mesh == nullptr)
        {
            cache.mesh = std::make_unique<static_mesh>(device.Get(), vertices, data.Indices());
            gpu_ready = cache.mesh != nullptr && cache.mesh->is_loaded();
        }
        else
        {
            // Sculpt 中は geometry だけ変わるので vertex/index buffer だけ更新する。
            gpu_ready = cache.mesh->update_procedural_geometry(
                device.Get(), vertices, data.Indices());
        }

        if (gpu_ready) cache.revision = data.Revision();
    }
    if (cache.mesh == nullptr || !cache.mesh->is_loaded()) return nullptr;
    return cache.mesh.get();
}


void framework::draw_landscape_scene_meshes(bool gbuffer_pass, bool depth_only)
{
    ReplayEngine::Scene::Scene& scene = active_object_scene();
    for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;

        auto* renderer = object->GetComponent<ReplayEngine::Components::LandscapeRendererComponent>();
        if (renderer == nullptr || !renderer->visible || !renderer->ActiveInHierarchy()) continue;

        static_mesh* landscape_mesh = resolve_landscape_gpu_mesh(*object);
        if (landscape_mesh == nullptr) continue;

        if (renderer->double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

        const DirectX::XMFLOAT4X4 world = object->GetTransform().WorldMatrixFloat4x4();
        if (depth_only)
        {
            landscape_mesh->render(immediate_context.Get(), world, renderer->tint,
                nullptr, nullptr, nullptr, false, false);
        }
        else if (gbuffer_pass)
        {
            // Landscape はまず標準PBR surfaceとしてGBufferへ出す。
            // Material Component連携はこの任意Mesh基盤の上へ後から追加できる。
            bind_gbuffer_material(
                ReplayEngine::Rendering::ShaderLightingModel::Pbr,
                false, false, 1.0f, 0.0f,
                0.0f, 0.75f, 1.0f, 0.0f,
                renderer->tint, { 0.0f, 0.0f, 0.0f }, 0u,
                renderer->receive_shadow);
            landscape_mesh->render(immediate_context.Get(), world, renderer->tint,
                static_mesh_gbuffer_ps.Get(), nullptr, nullptr, true, true);
        }
        else
        {
            landscape_mesh->render(immediate_context.Get(), world, renderer->tint,
                static_forward_shader(SHADING_MODEL_PBR));
        }

        if (renderer->double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
    }
}
