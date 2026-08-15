// GameObject / Component 基盤のうち「Object / Landscape 描画」を持つ。
// 描画パス、Material binding、Depth/GBuffer 分岐は関数本体のまま移動している。
#include "framework.h"

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
    bool gbuffer_pass, bool depth_only)
{
    if (object_render_items.Empty()) return;

    for (const ReplayEngine::Rendering::RenderItem& source_item : object_render_items.Items())
    {
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
                        ? item.material_binding.TextureSemanticMask() : 0u);
                primitive->render(immediate_context.Get(), item.world, item.tint,
                    static_mesh_gbuffer_ps.Get(), nullptr, nullptr, true, true);
                material_gpu_binder.UnbindTextures(immediate_context.Get());
            }
            else
            {
                primitive->render(immediate_context.Get(), item.world, item.tint,
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
                        ? item.material_binding.TextureSemanticMask() : 0u);
                gltf->render(immediate_context.Get(), item.world, item.tint,
                    static_mesh_gbuffer_ps.Get(), true, false);
                material_gpu_binder.UnbindTextures(immediate_context.Get());
            }
            else
            {
                gltf->render(immediate_context.Get(), item.world, item.legacy_tint,
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
                    ? item.material_binding.TextureSemanticMask() : 0u);
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


void framework::draw_landscape_scene_meshes(bool gbuffer_pass, bool depth_only)
{
    ReplayEngine::Scene::Scene& scene = active_object_scene();
    for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
    {
        ReplayEngine::Core::GameObject* object = scene.GameObjectAt(object_index);
        if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;

        auto* landscape = object->GetComponent<ReplayEngine::Components::LandscapeComponent>();
        auto* renderer = object->GetComponent<ReplayEngine::Components::LandscapeRendererComponent>();
        if (landscape == nullptr || renderer == nullptr || !renderer->visible ||
            !renderer->ActiveInHierarchy() || !landscape->Data().Valid()) continue;

        const auto& data = landscape->Data();
        const std::uint64_t cache_key = object->ID().Value();
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
                // Sculpt / Topology edit では geometry だけが変わる。
                // static_mesh を丸ごと再構築すると CSO/Texture まで毎回作り直すため、
                // vertex/index buffer だけ更新する。
                gpu_ready = cache.mesh->update_procedural_geometry(
                    device.Get(), vertices, data.Indices());
            }

            if (gpu_ready) cache.revision = data.Revision();
        }
        if (cache.mesh == nullptr || !cache.mesh->is_loaded()) continue;

        if (renderer->double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

        const DirectX::XMFLOAT4X4 world = object->GetTransform().WorldMatrixFloat4x4();
        if (depth_only)
        {
            cache.mesh->render(immediate_context.Get(), world, renderer->tint,
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
                renderer->tint, { 0.0f, 0.0f, 0.0f }, 0u);
            cache.mesh->render(immediate_context.Get(), world, renderer->tint,
                static_mesh_gbuffer_ps.Get(), nullptr, nullptr, true, true);
        }
        else
        {
            cache.mesh->render(immediate_context.Get(), world, renderer->tint,
                static_forward_shader(SHADING_MODEL_PBR));
        }

        if (renderer->double_sided)
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
    }
}
