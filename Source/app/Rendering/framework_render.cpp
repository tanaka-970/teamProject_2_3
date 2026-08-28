#include "framework.h"
#include "skinned_mesh.h"
#include "gltf_model.h"
#include "../../../RePlayEngine/Components/Core/TransformComponent.h"
#include "../../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../../RePlayEngine/Components/Rendering/ParticleEmitterComponent.h"
#include "../../../RePlayEngine/Components/Rendering/PostProcessVolumeComponent.h"
#include "../../../RePlayEngine/Components/Rendering/ScreenEffectStackComponent.h"
#include "../../../RePlayEngine/Components/Rendering/ModelEffectStackComponent.h"
#include "../../../RePlayEngine/Rendering/Shaders/BuiltInShaders.h"
#include "../../../RePlayEngine/Rendering/ShaderStack/BuiltInShaderLayers.h"
#include "../../../RePlayEngine/Rendering/Materials/ShaderLayerBinding.h"
#include "../../../RePlayEngine/UI/UILayout.h"
#include "../../../RePlayEngine/Components/UI/UIEffectStackComponent.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// 分割一覧（framework_render.cpp）:
//   framework_dx12_ui.cpp             … DX12 UI の提出と合成
// 関数へ再分割せず本文を連続断片のまま include するため、描画順は変更しない。

namespace
{
    using ReplayEngine::Components::ParticleEmitterComponent;
    using ReplayEngine::Components::PostProcessVolumeComponent;
    using ReplayEngine::Components::ScreenEffectStackComponent;
    using ReplayEngine::Components::ModelEffectStackComponent;
    using ReplayEngine::Components::TransformComponent;

    float clamp_finite(float value, float fallback, float low, float high) noexcept
    {
        if (!std::isfinite(value)) return fallback;
        return (std::min)((std::max)(value, low), high);
    }


    DirectX::XMFLOAT4 clamp_color(const DirectX::XMFLOAT4& value) noexcept
    {
        return {
            clamp_finite(value.x, 1.0f, 0.0f, 8.0f),
            clamp_finite(value.y, 1.0f, 0.0f, 8.0f),
            clamp_finite(value.z, 1.0f, 0.0f, 8.0f),
            clamp_finite(value.w, 1.0f, 0.0f, 1.0f)
        };
    }

    DirectX::XMFLOAT3 normalize_or_up(const DirectX::XMFLOAT3& value) noexcept
    {
        const DirectX::XMVECTOR vector = DirectX::XMLoadFloat3(&value);
        const float length_sq = DirectX::XMVectorGetX(
            DirectX::XMVector3LengthSq(vector));
        if (!std::isfinite(length_sq) || length_sq <= 1.0e-8f)
        {
            return { 0.0f, 1.0f, 0.0f };
        }

        DirectX::XMFLOAT3 normalized{};
        DirectX::XMStoreFloat3(&normalized,
            DirectX::XMVector3Normalize(vector));
        return normalized;
    }

    ReplayEngine::Rendering::RenderStats::Phase profiler_phase_for_dx12_pass(
        ReplayEngine::Rendering::DX12::D3D12GpuPass pass) noexcept
    {
        using Pass = ReplayEngine::Rendering::DX12::D3D12GpuPass;
        using Phase = ReplayEngine::Rendering::RenderStats::Phase;
        switch (pass)
        {
        case Pass::RuntimeUI:
        case Pass::UIEffect:
        case Pass::UIPreview:
            return Phase::GameUI;
        case Pass::ImGui:
            return Phase::EditorUI;
        default:
            return Phase::Scene3D;
        }
    }

    const char* editor_log_level_for_dx12_message(D3D12_MESSAGE_SEVERITY severity) noexcept
    {
        switch (severity)
        {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        case D3D12_MESSAGE_SEVERITY_ERROR:
            return "Error";
        case D3D12_MESSAGE_SEVERITY_WARNING:
            return "Warning";
        default:
            return "Info";
        }
    }

    struct ParticleEmitterSelection
    {
        const ReplayEngine::Core::GameObject* object = nullptr;
        const ParticleEmitterComponent* component = nullptr;

        bool Valid() const noexcept { return object != nullptr && component != nullptr; }
    };

    ParticleEmitterSelection select_particle_emitter(
        const ReplayEngine::Scene::Scene& scene)
    {
        ParticleEmitterSelection best{};
        for (std::size_t object_index = 0; object_index < scene.GameObjectCount();
            ++object_index)
        {
            const ReplayEngine::Core::GameObject* object =
                scene.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy()) continue;

            for (std::size_t component_index = 0;
                component_index < object->ComponentCount(); ++component_index)
            {
                const auto* emitter = dynamic_cast<const ParticleEmitterComponent*>(
                    object->ComponentAt(component_index));
                if (emitter == nullptr || (!emitter->emitting && !emitter->HasPendingRequest()) ||
                    !emitter->ActiveInHierarchy())
                {
                    continue;
                }
                if (!best.Valid() || emitter->priority > best.component->priority)
                {
                    best = { object, emitter };
                }
            }
        }
        return best;
    }

    // 読み込んだFBXの軸方向を、エンジンの左手座標系へ合わせる。
    DirectX::XMMATRIX fbx_coordinate_transform()
    {
        const DirectX::XMFLOAT4X4 coordinate_system_transform
        {
            -1, 0,  0, 0,
             0, 0, -1, 0,
             0, 1,  0, 0,
             0, 0,  0, 1
        };
        return DirectX::XMLoadFloat4x4(&coordinate_system_transform);
    }
}

// エディタのデバッグ用メッシュ (static_meshes[0]) を置くワールド行列。
//
// かつてここには「ゲーム実行中は旧 Player の Transform を使う」という分岐があり、
// それが旧 Player を画面へ出す固定行列だった。その分岐は撤去した。
// GameObject の描画行列は SceneRenderCollector が RenderItem::world として作る。
void framework::store_debug_mesh_world(DirectX::XMFLOAT4X4& world) const
{
    DirectX::XMMATRIX C = fbx_coordinate_transform();

    DirectX::XMMATRIX S = DirectX::XMMatrixScaling(scaling.x, scaling.y, scaling.z);
    DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(rotation.x),
        DirectX::XMConvertToRadians(rotation.y),
        DirectX::XMConvertToRadians(rotation.z));
    DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(translation.x, translation.y, translation.z);
    DirectX::XMStoreFloat4x4(&world, C * S * R * T);
}

ReplayEngine::Rendering::ShaderLightingModel framework::deferred_lighting_model(
    int shading) const
{
    using ReplayEngine::Rendering::ShaderLightingModel;

    ShaderLightingModel model = ShaderLightingModel::Pbr;
    if (ReplayEngine::Rendering::BuiltInShaders::
        TryGetLightingModelFromShadingModel(shading, model))
    {
        return model;
    }

    // 旧データの不明値は従来どおり Unlit へ落とす。
    // MaterialにShader GUIDがある場合は resolve_render_item_material() が
    // Catalogの replay_lighting で上書きする。
    return ShaderLightingModel::Unlit;
}


void framework::render(float elapsed_time)
{
    if (dx12_framework_active)
    {
        apply_pending_resize();
        if (ui_preview_runtime_requested && show_ui_preview_panel &&
            ui_preview_runtime_width > 0 && ui_preview_runtime_height > 0)
        {
            dx12_device_context.EnsureUIPreviewTarget(
                static_cast<std::uint32_t>(ui_preview_runtime_width),
                static_cast<std::uint32_t>(ui_preview_runtime_height));
        }
        shadow_stats.Reset();
        shadow_stats.directional_preview_light = directional_light_is_preview;
        bool dx12_capture_requested = false;
        const float clear_color[4] =
        {
            background_color.x, background_color.y, background_color.z, background_color.w
        };
        bool ok = dx12_device_context.BeginFrame(clear_color);
        if (ok)
        {
            ReplayEngine::Rendering::DX12::D3D12FrameConstants constants{};
            const DirectX::XMMATRIX view = viewport_view_matrix();
            const DirectX::XMMATRIX projection = viewport_projection_matrix();
            const DirectX::XMMATRIX view_projection = view * projection;
            DirectX::XMStoreFloat4x4(&constants.view, view);
            DirectX::XMStoreFloat4x4(&constants.projection, projection);
            DirectX::XMStoreFloat4x4(&constants.view_projection, view_projection);
            DirectX::XMStoreFloat4x4(&constants.inv_view,
                DirectX::XMMatrixInverse(nullptr, view));
            DirectX::XMStoreFloat4x4(&constants.inv_projection,
                DirectX::XMMatrixInverse(nullptr, projection));
            DirectX::XMStoreFloat4x4(&constants.inv_view_projection,
                DirectX::XMMatrixInverse(nullptr, view_projection));
            if (!previous_view_projection_valid)
            {
                DirectX::XMStoreFloat4x4(&previous_view_projection, view_projection);
                previous_view_projection_valid = true;
            }
            constants.prev_view_projection = previous_view_projection;
            const DirectX::XMFLOAT3 eye = viewport_eye_position();
            constants.camera_position = { eye.x, eye.y, eye.z, 1.0f };
            const float viewport_width = static_cast<float>(
                (std::max)(SCREEN_WIDTH, static_cast<LONG>(1)));
            const float viewport_height = static_cast<float>(
                (std::max)(SCREEN_HEIGHT, static_cast<LONG>(1)));
            constants.screen_size = { viewport_width, viewport_height,
                1.0f / viewport_width, 1.0f / viewport_height };
            const DirectX::XMFLOAT4X4& projection_values = constants.projection;
            const float tan_half_fov_y = projection_values._22 != 0.0f
                ? 1.0f / projection_values._22 : 1.0f;
            const float aspect = projection_values._11 != 0.0f
                ? projection_values._22 / projection_values._11 : 1.0f;
            const float near_plane = projection_values._33 != 0.0f
                ? -projection_values._43 / projection_values._33 : 0.1f;
            const float far_plane = (projection_values._33 - 1.0f) != 0.0f
                ? -projection_values._43 / (projection_values._33 - 1.0f) : 10000.0f;
            constants.camera_planes = { near_plane, far_plane, tan_half_fov_y, aspect };
            constants.jitter = { taa_jitter_ndc.x, taa_jitter_ndc.y,
                previous_taa_jitter_ndc.x, previous_taa_jitter_ndc.y };
            if (!golden_capture_pending())
                shader_composer_time += (std::max)(0.0f, elapsed_time);
            constants.time_parameters =
            {
                static_cast<float>(frame_index), elapsed_time, shader_composer_time, 0.0f
            };

            // DX11 render_setup.inl を通らない DX12 path でも、既存 CSM の CPU cascade
            // calculation を同じ値で更新する。GPU buffer/resource は DX12 backend が持つ。
            csm.constants.params.w =
                (enable_dynamic_shadows && csm_enabled_setting && directional_shadow_enabled)
                ? 1.0f : 0.0f;
            DirectX::XMFLOAT4X4 shadow_view{}, shadow_projection{};
            DirectX::XMStoreFloat4x4(&shadow_view, view);
            DirectX::XMStoreFloat4x4(&shadow_projection, projection);
            csm.update_cascades(light_direction, shadow_view, shadow_projection, 30.0f);

            ReplayEngine::Rendering::DX12::D3D12StaticSceneSubmission static_scene;
            const ReplayEngine::Rendering::PostProcessPass::Settings& post_settings =
                post_process.GetSettings();
            static_scene.post_process.exposure = clamp_finite(post_settings.exposure,
                0.619f, 0.01f, 8.0f);
            static_scene.post_process.bloom_intensity = clamp_finite(post_settings.bloom_intensity,
                0.25f, 0.0f, 8.0f);
            static_scene.post_process.bloom_threshold = clamp_finite(luminance_threshold,
                1.0f, 0.0f, 8.0f);
            static_scene.post_process.vignette_strength = clamp_finite(post_settings.vignette_strength,
                0.138f, 0.0f, 1.0f);
            static_scene.post_process.fxaa_enable = clamp_finite(post_settings.fxaa_enable,
                1.0f, 0.0f, 1.0f);
            static_scene.post_process.taa_blend = clamp_finite(taa_pass.blend, 0.88f, 0.0f, 0.99f);
            static_scene.post_process.ssao_strength = clamp_finite(ssao_pass.intensity, 1.0f, 0.0f, 4.0f);
            static_scene.post_process.ssr_strength = clamp_finite(ssr_pass.intensity, 1.0f, 0.0f, 4.0f);
            const float ssao_fade_start = clamp_finite(ssao_pass.fade_start,
                60.0f, 1.0f, 400.0f);
            const float ssao_fade_end = (std::max)(ssao_fade_start + 0.001f,
                clamp_finite(ssao_pass.fade_end, 140.0f, 2.0f, 800.0f));
            static_scene.post_process.ssao_params0 = {
                clamp_finite(ssao_pass.radius, 0.75f, 0.05f, 4.0f),
                clamp_finite(ssao_pass.power, 1.6f, 0.5f, 4.0f),
                clamp_finite(ssao_pass.thin_occluder, 1.0f, 0.0f, 1.0f),
                clamp_finite(ssao_pass.normal_bias, 0.35f, 0.0f, 2.0f) };
            static_scene.post_process.ssao_params1 = {
                static_cast<float>((std::max)(1, (std::min)(8, ssao_pass.slice_count))),
                static_cast<float>((std::max)(2, (std::min)(12, ssao_pass.step_count))),
                ssao_fade_start, ssao_fade_end };
            static_scene.post_process.ssao_params2 = {
                ssao_pass.blur_enabled ? 1.0f : 0.0f,
                clamp_finite(ssao_pass.blur_sharpness, 1.0f, 0.0f, 1.0f), 0.0f, 0.0f };
            static_scene.post_process.ssr_params0 = {
                clamp_finite(ssr_pass.max_distance, 40.0f, 1.0f, 200.0f),
                clamp_finite(ssr_pass.thickness, 0.4f, 0.01f, 2.0f),
                clamp_finite(ssr_pass.stride, 3.0f, 1.0f, 16.0f),
                static_cast<float>((std::max)(4, (std::min)(64, ssr_pass.max_step))) };
            static_scene.post_process.ssr_params1 = {
                static_cast<float>((std::max)(0, (std::min)(8, ssr_pass.refine_step))),
                clamp_finite(ssr_pass.max_roughness, 0.65f, 0.05f, 1.0f),
                clamp_finite(ssr_pass.edge_fade, 0.12f, 0.01f, 0.4f),
                clamp_finite(ssr_pass.ray_bias, 1.0f, 0.0f, 4.0f) };
            static_scene.post_process.ssr_params2 = {
                clamp_finite(ssr_pass.resolve_radius, 12.0f, 0.0f, 40.0f),
                static_cast<float>((std::max)(1, (std::min)(16, ssr_pass.resolve_tap_count))),
                0.0f, 0.0f };
            static_scene.post_process.taa_params0 = {
                clamp_finite(taa_pass.variance_gamma, 1.0f, 0.25f, 3.0f),
                clamp_finite(taa_pass.sharpness, 0.35f, 0.0f, 1.0f),
                clamp_finite(taa_pass.max_velocity, 48.0f, 4.0f, 200.0f),
                0.0f };
            static_scene.post_process.color_filter = clamp_color(post_settings.color_filter);
            static_scene.post_process.render_output = profile_benchmark_mode
                ? profile_benchmark_render_output
                : static_cast<std::uint32_t>(render_graph.OutputIndex());
            static_scene.post_process.deferred_debug_mode = static_cast<std::uint32_t>(
                render_graph.DeferredDebugMode());
            static_scene.post_process.bloom_enabled = enable_bloom_shader;
            static_scene.post_process.vignette_enabled = enable_vignette_shader;
            static_scene.post_process.fxaa_enabled = enable_fxaa_shader;
            static_scene.post_process.taa_enabled = enable_taa && taa_pass.enabled;
            static_scene.post_process.ssao_enabled = enable_ssao && ssao_pass.enabled;
            static_scene.post_process.ssr_enabled = enable_ssr && ssr_pass.enabled;
            const auto volume_selection =
                ReplayEngine::Components::ResolvePostProcessVolumeSelection(active_object_scene());
            if (volume_selection.Valid())
            {
                const PostProcessVolumeComponent& volume = *volume_selection.component;
                static_scene.post_process.exposure = clamp_finite(volume.exposure,
                    static_scene.post_process.exposure, 0.01f, 8.0f);
                static_scene.post_process.bloom_threshold = clamp_finite(volume.bloom_threshold,
                    static_scene.post_process.bloom_threshold, 0.0f, 8.0f);
                static_scene.post_process.bloom_intensity = clamp_finite(volume.bloom_intensity,
                    static_scene.post_process.bloom_intensity, 0.0f, 8.0f);
                static_scene.post_process.vignette_strength = clamp_finite(volume.vignette_intensity,
                    static_scene.post_process.vignette_strength, 0.0f, 1.0f);
                static_scene.post_process.color_filter = clamp_color(volume.color_filter);
                static_scene.post_process.bloom_enabled = volume.bloom_enabled;
                static_scene.post_process.vignette_enabled = volume.vignette_enabled;
                static_scene.post_process.ssao_params0.x = clamp_finite(volume.ssao_radius,
                    static_scene.post_process.ssao_params0.x, 0.05f, 4.0f);
                static_scene.post_process.ssao_strength = clamp_finite(volume.ssao_intensity,
                    static_scene.post_process.ssao_strength, 0.0f, 4.0f);
                static_scene.post_process.ssr_strength = clamp_finite(volume.ssr_intensity,
                    static_scene.post_process.ssr_strength, 0.0f, 4.0f);
                static_scene.post_process.taa_enabled = volume.taa_enabled;
                static_scene.post_process.ssao_enabled = volume.ssao_enabled;
                static_scene.post_process.ssr_enabled = volume.ssr_enabled;
                static_scene.post_process.fxaa_enabled = enable_fxaa_shader;
            }
            for (const std::string& item : screen_space_overrides)
            {
                const std::size_t separator = item.find('=');
                if (separator == std::string::npos) continue;
                const std::string key = item.substr(0, separator);
                const std::string value = item.substr(separator + 1);
                try
                {
                    if (key == "ssao.enabled") static_scene.post_process.ssao_enabled = std::stoi(value) != 0;
                    else if (key == "ssao.radius") static_scene.post_process.ssao_params0.x = std::stof(value);
                    else if (key == "ssao.intensity") static_scene.post_process.ssao_strength = std::stof(value);
                    else if (key == "ssao.power") static_scene.post_process.ssao_params0.y = std::stof(value);
                    else if (key == "ssao.thin_occluder") static_scene.post_process.ssao_params0.z = std::stof(value);
                    else if (key == "ssao.slice_count") static_scene.post_process.ssao_params1.x = std::stof(value);
                    else if (key == "ssao.step_count") static_scene.post_process.ssao_params1.y = std::stof(value);
                    else if (key == "ssao.fade_start") static_scene.post_process.ssao_params1.z = std::stof(value);
                    else if (key == "ssao.fade_end") static_scene.post_process.ssao_params1.w = (std::max)(
                        static_scene.post_process.ssao_params1.z + 0.001f, std::stof(value));
                    else if (key == "ssao.normal_bias") static_scene.post_process.ssao_params0.w = std::stof(value);
                    else if (key == "ssao.blur_sharpness") static_scene.post_process.ssao_params2.y = std::stof(value);
                    else if (key == "ssao.blur_enabled") static_scene.post_process.ssao_params2.x =
                        std::stoi(value) != 0 ? 1.0f : 0.0f;
                    else if (key == "ssr.enabled") static_scene.post_process.ssr_enabled = std::stoi(value) != 0;
                    else if (key == "ssr.max_distance") static_scene.post_process.ssr_params0.x = std::stof(value);
                    else if (key == "ssr.thickness") static_scene.post_process.ssr_params0.y = std::stof(value);
                    else if (key == "ssr.stride") static_scene.post_process.ssr_params0.z = std::stof(value);
                    else if (key == "ssr.max_step") static_scene.post_process.ssr_params0.w = std::stof(value);
                    else if (key == "ssr.refine_step") static_scene.post_process.ssr_params1.x = std::stof(value);
                    else if (key == "ssr.max_roughness") static_scene.post_process.ssr_params1.y = std::stof(value);
                    else if (key == "ssr.edge_fade") static_scene.post_process.ssr_params1.z = std::stof(value);
                    else if (key == "ssr.ray_bias") static_scene.post_process.ssr_params1.w = std::stof(value);
                    else if (key == "ssr.resolve_radius") static_scene.post_process.ssr_params2.x = std::stof(value);
                    else if (key == "ssr.resolve_tap_count") static_scene.post_process.ssr_params2.y = std::stof(value);
                    else if (key == "taa.enabled") static_scene.post_process.taa_enabled = std::stoi(value) != 0;
                    else if (key == "taa.blend") static_scene.post_process.taa_blend = std::stof(value);
                    else if (key == "taa.variance_gamma") static_scene.post_process.taa_params0.x = std::stof(value);
                    else if (key == "taa.sharpness") static_scene.post_process.taa_params0.y = std::stof(value);
                    else if (key == "taa.max_velocity") static_scene.post_process.taa_params0.z = std::stof(value);
                }
                catch (...)
                {
                }
            }
            static_scene.background_color = {
                clear_color[0], clear_color[1], clear_color[2], clear_color[3] };
            const bool upload_ok =
                dx12_device_context.SubmitFrameConstants(constants) &&
                dx12_device_context.SubmitRenderItems(object_render_items);
            ReplayEngine::Rendering::DX12::D3D12SceneEffectSubmission scene_effects;
            const bool scene_effects_ok = build_dx12_scene_effects(scene_effects);
            dx12_device_context.SetSceneEffects(std::move(scene_effects));
            const bool static_scene_ok = upload_ok && scene_effects_ok &&
                build_dx12_static_scene(static_scene, elapsed_time) &&
                dx12_device_context.DrawScene3D(static_scene);
            ReplayEngine::Rendering::DX12::D3D12UIFrame ui_frame;
            bool ui_ok = false;
            if (upload_ok)
            {
                // DX12でも既存ProfilerのCPUカウンタへRuntime UIの提出量を記録する。
                // GPU timestampはD3D11専用のため、ここではDraw/Vertexだけを共有する。
                ReplayEngine::Rendering::Stats().BeginPhase(
                    ReplayEngine::Rendering::RenderStats::Phase::GameUI);
                {
                    REPLAY_PROFILE_SCOPE("UI/BuildEditorFrame");
                    ui_ok = build_dx12_ui(ui_frame);
                }
                if (ui_ok && scene_manager.IsExclusive())
                {
                    ui_frame = {};
                    REPLAY_PROFILE_SCOPE("UI/BuildRuntimeFrame");
                    ui_ok = scene_manager.BuildRuntimeUI(ui_frame, viewport_width, viewport_height);
                }
                if (ui_ok)
                {
                    ReplayEngine::Rendering::Stats().SetUICounters(
                        ui_frame.draw_commands, ui_frame.vertex_count,
                        ui_frame.texture_count, ui_frame.mask_depth,
                        ui_frame.clipped_commands);
                    for (const auto& batch : ui_frame.batches)
                    {
                        ReplayEngine::Rendering::Stats().CountDraw(
                            static_cast<std::uint32_t>(batch.vertices.size()));
                    }
                    {
                        REPLAY_PROFILE_SCOPE("UI/SubmitRuntimeFrame");
                        ui_ok = dx12_device_context.DrawRuntimeUI(ui_frame);
                    }

                    if (editor_mode && show_ui_preview_panel &&
                        ui_preview_runtime_requested &&
                        ui_preview_runtime_width > 0 && ui_preview_runtime_height > 0)
                    {
                        ReplayEngine::Scene::Scene* preview_scene =
                            object_editor_context.GetScene();
                        if (preview_scene != nullptr)
                        {
                            ReplayEngine::Rendering::DX12::D3D12UIFrame preview_frame;
                            object_ui_viewport preview_viewport{};
                            preview_viewport.width = static_cast<float>(
                                ui_preview_runtime_width);
                            preview_viewport.height = static_cast<float>(
                                ui_preview_runtime_height);
                            preview_viewport.logical_width = preview_viewport.width;
                            preview_viewport.logical_height = preview_viewport.height;
                            const bool preview_built = build_dx12_ui_for_scene(
                                preview_frame, *preview_scene,
                                static_cast<std::uint32_t>(ui_preview_runtime_width),
                                static_cast<std::uint32_t>(ui_preview_runtime_height),
                                preview_viewport);
                            if (preview_built)
                                ui_ok = dx12_device_context.DrawRuntimeUIPreview(preview_frame) && ui_ok;
                        }
                    }
                }
                ReplayEngine::Rendering::Stats().EndPhase(
                    ReplayEngine::Rendering::RenderStats::Phase::GameUI);
            }
#ifdef USE_IMGUI
            bool imgui_ok = true;
            if (imgui_frame_active)
            {
                imgui_frame_active = false;
                ImGui::Render();
                imgui_ok = dx12_device_context.DrawImGui(ImGui::GetDrawData());
            }
#else
            const bool imgui_ok = true;
#endif
            if (upload_ok)
            {
                // DX12はD3D11の影提出関数を通らないため、実際に提出したSceneから診断値を作る。
                shadow_stats.directional_light_present =
                    static_scene.directional_light.enabled;
                shadow_stats.directional_shadow_rendered =
                    static_scene.directional_shadow.enabled;
                for (const auto& draw : static_scene.draws)
                {
                    if (!draw.cast_shadow)
                    {
                        ++shadow_stats.skipped_cast_shadow;
                        continue;
                    }
                    if (draw.mesh_key.rfind("builtin:", 0) == 0)
                        ++shadow_stats.primitive_casters;
                    else
                        ++shadow_stats.static_casters;
                }
                for (const auto& draw : static_scene.skinned_draws)
                {
                    if (draw.surface.cast_shadow)
                        ++shadow_stats.skinned_casters;
                    else
                        ++shadow_stats.skipped_cast_shadow;
                }
                if (static_scene.local_shadows.enabled)
                {
                    int used_local_slices = 0;
                    for (std::uint32_t slice = 0;
                        slice < ReplayEngine::Rendering::DX12::D3D12LocalShadowSubmission::SliceCount;
                        ++slice)
                    {
                        if ((static_scene.local_shadows.used_slice_mask &
                            (1u << slice)) != 0)
                            ++used_local_slices;
                    }
                    for (const auto& light : static_scene.point_lights)
                    {
                        const bool valid_range = light.cast_shadows &&
                            light.shadow_slice >= 0 && light.shadow_slice + 5 <
                            static_cast<std::int32_t>(
                                ReplayEngine::Rendering::DX12::D3D12LocalShadowSubmission::SliceCount);
                        bool all_faces_present = valid_range;
                        if (valid_range)
                        {
                            for (std::int32_t face = 0; face < 6; ++face)
                            {
                                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(
                                    light.shadow_slice + face);
                                all_faces_present = all_faces_present &&
                                    (static_scene.local_shadows.used_slice_mask & bit) != 0;
                            }
                        }
                        if (all_faces_present) ++shadow_stats.point_shadow_lights;
                    }
                    for (const auto& light : static_scene.spot_lights)
                    {
                        if (light.cast_shadows && light.shadow_slice >= 0 &&
                            light.shadow_slice < static_cast<std::int32_t>(
                                ReplayEngine::Rendering::DX12::D3D12LocalShadowSubmission::SliceCount) &&
                            (static_scene.local_shadows.used_slice_mask &
                                (1u << static_cast<std::uint32_t>(light.shadow_slice))) != 0)
                            ++shadow_stats.spot_shadow_lights;
                    }
                    const int casters = shadow_stats.primitive_casters +
                        shadow_stats.static_casters + shadow_stats.skinned_casters;
                        const int passes = used_local_slices +
                        (shadow_stats.directional_shadow_rendered ?
                            static_cast<int>(ReplayEngine::Rendering::DX12::
                                D3D12DirectionalShadowSubmission::CascadeCount) : 0);
                    shadow_stats.shadow_draw_calls = casters * passes;
                }
                else if (shadow_stats.directional_shadow_rendered)
                {
                    const int casters = shadow_stats.primitive_casters +
                        shadow_stats.static_casters + shadow_stats.skinned_casters;
                    shadow_stats.shadow_draw_calls = casters * static_cast<int>(
                        ReplayEngine::Rendering::DX12::D3D12DirectionalShadowSubmission::CascadeCount);
                }
            }
            std::uint64_t component_count = 0;
            const auto& runtime_scene = active_object_scene();
            for (std::size_t object_index = 0;
                object_index < runtime_scene.GameObjectCount(); ++object_index)
            {
                const auto* object = runtime_scene.GameObjectAt(object_index);
                if (object != nullptr) component_count += object->ComponentCount();
            }
            ReplayEngine::Rendering::Stats().SetSceneCounters(
                runtime_scene.GameObjectCount(), component_count,
                static_scene.draws.size() + static_scene.skinned_draws.size(),
                static_scene.draws.size() + static_scene.skinned_draws.size());
            for (const auto& draw : static_scene.draws)
            {
                ReplayEngine::Rendering::Stats().CountDrawIndexed(
                    draw.index_count != 0 ? draw.index_count : 3u);
            }
            for (const auto& draw : static_scene.skinned_draws)
            {
                ReplayEngine::Rendering::Stats().CountDrawIndexed(
                    draw.surface.index_count != 0 ? draw.surface.index_count : 3u);
            }
            dx12_capture_requested = prepare_dx12_golden_capture();
            // 一時的な Upload 領域不足や Asset 提出失敗が起きても、フレームを完了して
            // Fence を打つ。次の BeginFrame に開いた Command List を持ち越さない。
            const bool end_ok = dx12_device_context.EndFrame();
            if (end_ok && dx12_capture_requested) tick_golden_capture();
            ReplayEngine::Rendering::Stats().EndFrame();
            const auto& gpu_timing = dx12_device_context.GpuTiming();
            std::vector<ReplayEngine::Rendering::RenderStats::ExternalGpuPassTiming> profiler_gpu_timings;
            profiler_gpu_timings.reserve(ReplayEngine::Rendering::DX12::D3D12GpuPassCount);
            for (std::size_t pass_index = 0;
                pass_index < ReplayEngine::Rendering::DX12::D3D12GpuPassCount; ++pass_index)
            {
                const auto pass = static_cast<ReplayEngine::Rendering::DX12::D3D12GpuPass>(pass_index);
                profiler_gpu_timings.push_back({
                    ReplayEngine::Rendering::DX12::D3D12GpuPassName(pass),
                    profiler_phase_for_dx12_pass(pass),
                    gpu_timing.milliseconds[pass_index],
                    gpu_timing.valid[pass_index] });
            }
            ReplayEngine::Rendering::Stats().ApplyExternalGpuTiming(
                gpu_timing.frame_id, profiler_gpu_timings);
            std::vector<ReplayEngine::Rendering::DX12::D3D12DebugMessage> dx12_messages;
            dx12_device_context.ConsumeDebugMessages(dx12_messages);
            for (const auto& message : dx12_messages)
            {
                std::string text = "DX12: " + message.text;
                if (message.repeat_count > 1)
                    text += " (x" + std::to_string(message.repeat_count) + ")";
                push_editor_log(editor_log_level_for_dx12_message(message.severity), text);
            }
            ok = upload_ok && static_scene_ok && ui_ok && imgui_ok && end_ok;
        }

        if (!ok && !dx12_framework_render_error_reported)
        {
            dx12_framework_render_error_reported = true;
            push_editor_log("Error",
                "DX12 framework frame の記録または Present に失敗しました");
            OutputDebugStringA("[DX12] framework frame failed.\n");
        }
        if (ok)
        {
            dx12_framework_render_error_reported = false;
            const DirectX::XMMATRIX current_view_projection =
                viewport_view_matrix() * viewport_projection_matrix();
            DirectX::XMStoreFloat4x4(&previous_view_projection, current_view_projection);
            previous_view_projection_valid = true;
            previous_taa_jitter_ndc = taa_jitter_ndc;
        }

    // Phase 2 では TAA/PostFX を移行していないが、旧 Present 経路と
    // エンジンの時間フレーム番号の意味をそろえる。
        if (!golden_capture_pending()) ++frame_index;
        return;
    }

    // 製品の描画経路はDX12に統一する。旧D3D11描画断片はここから取り込まない。
    // 旧経路を残したままでは、将来の変更で意図しないフォールバックが入り込む。あと、わかりにくい
}
