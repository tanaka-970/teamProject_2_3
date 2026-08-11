#include "framework.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"
#include "gltf_model.h"
#include "../../../RePlayEngine/Components/Core/TransformComponent.h"
#include "../../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../../RePlayEngine/Components/Rendering/ParticleEmitterComponent.h"
#include "../../../RePlayEngine/Components/Rendering/PostProcessVolumeComponent.h"
#include "../../../RePlayEngine/Rendering/Shaders/BuiltInShaders.h"
#include "../../../RePlayEngine/Rendering/ShaderStack/BuiltInShaderLayers.h"
#include "../../../RePlayEngine/Rendering/Materials/ShaderLayerBinding.h"
#include "../../../RePlayEngine/UI/UILayout.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    using ReplayEngine::Components::ParticleEmitterComponent;
    using ReplayEngine::Components::PostProcessVolumeComponent;
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
                if (emitter == nullptr || !emitter->emitting ||
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

void framework::update_frame_constants(const DirectX::XMMATRIX& view,
    const DirectX::XMMATRIX& projection, float elapsed_time, bool advance_effect_time)
{
    if (!frame_constants_cb) return;

    const DirectX::XMMATRIX view_projection = view * projection;
    DirectX::XMStoreFloat4x4(&frame_constants.view, view);
    DirectX::XMStoreFloat4x4(&frame_constants.projection, projection);
    DirectX::XMStoreFloat4x4(&frame_constants.view_projection, view_projection);
    DirectX::XMStoreFloat4x4(&frame_constants.inv_view,
        DirectX::XMMatrixInverse(nullptr, view));
    DirectX::XMStoreFloat4x4(&frame_constants.inv_projection,
        DirectX::XMMatrixInverse(nullptr, projection));
    DirectX::XMStoreFloat4x4(&frame_constants.inv_view_projection,
        DirectX::XMMatrixInverse(nullptr, view_projection));

    // 初回フレームは前フレームが無いので、今フレームで埋めて再投影を無効化する。
    if (!previous_view_projection_valid)
    {
        DirectX::XMStoreFloat4x4(&previous_view_projection, view_projection);
        previous_view_projection_valid = true;
    }
    frame_constants.prev_view_projection = previous_view_projection;

    const DirectX::XMFLOAT4X4& p = frame_constants.projection;
    // projection._22 = 1/tan(fovY/2)、_11 = 1/(tan(fovY/2)*aspect)。
    const float tan_half_fov_y = p._22 != 0.0f ? 1.0f / p._22 : 1.0f;
    const float aspect = p._11 != 0.0f ? p._22 / p._11 : 1.0f;
    // LH透視射影は _33 = far/(far-near)、_43 = -near*far/(far-near)。
    const float near_plane = p._33 != 0.0f ? -p._43 / p._33 : 0.1f;
    const float far_plane = (p._33 - 1.0f) != 0.0f ? p._43 / (p._33 - 1.0f) : 10000.0f;

    // CameraComponent / 補助 View / 従来 Camera のどれを描いていても、
    // view/projection と同じ窓口から Eye を取る。ここだけ旧 Gameplay Camera を
    // 直接読むと、分割 Viewport で鏡面・SSAO の視点だけ別 Camera になる。
    const DirectX::XMFLOAT3 frame_eye = viewport_eye_position();
    frame_constants.camera_position =
        { frame_eye.x, frame_eye.y, frame_eye.z, 1.0f };
    const float width = static_cast<float>(SCREEN_WIDTH);
    const float height = static_cast<float>(SCREEN_HEIGHT);
    frame_constants.screen_size = { width, height, 1.0f / width, 1.0f / height };
    frame_constants.camera_planes = { near_plane, far_plane, tan_half_fov_y, aspect };
    // z is accumulated effect/composer time. Golden capture passes elapsed_time=0,
    // so visual regression remains deterministic instead of advancing while capturing.
    if (advance_effect_time) shader_composer_time += (std::max)(0.0f, elapsed_time);
    frame_constants.frame_params = { static_cast<float>(frame_index), elapsed_time, shader_composer_time, 0.0f };

    // TAAのジッター量(NDC)。射影行列へ加算済みの値をそのまま共有し、
    // モーションベクター側で打ち消せるようにしておく。
    frame_constants.jitter = { taa_jitter_ndc.x, taa_jitter_ndc.y,
        previous_taa_jitter_ndc.x, previous_taa_jitter_ndc.y };

    // メッシュ側がモーションベクターを書くために必要なフレーム共通の情報。
    motion_vectors::FrameContext& motion_frame = motion_vectors::Frame();
    motion_frame.previous_view_projection = previous_view_projection;
    motion_frame.current_jitter = taa_jitter_ndc;
    motion_frame.previous_jitter = previous_taa_jitter_ndc;
    motion_frame.enabled = previous_view_projection_valid;
    motion_frame.frame_id = frame_index + 1; // 0は「未描画」を表すため使わない

    immediate_context->UpdateSubresource(frame_constants_cb.Get(), 0, nullptr,
        &frame_constants, 0, 0);
    // b4はこのフレーム定数の専用スロット。他のパスが上書きしないため、
    // フレーム先頭で一度貼れば SSAO/SSR/TAA/タイルド照明すべてから読める。
    ID3D11Buffer* buffers[1]{ frame_constants_cb.Get() };
    immediate_context->PSSetConstantBuffers(4, 1, buffers);
    immediate_context->CSSetConstantBuffers(4, 1, buffers);
}

// 【マテリアルが唯一の真実】
//
// 以前はここでグローバルフラグ（use_pbr_skin / enable_toon_shader など）を
// 見て、false なら nullptr を返したり SHADING_MODEL_UNLIT へ降格させていた。
//
// その結果、
//   「描画確認」タブのチェックを外す
//     -> 全マテリアルの指定が無視されて Unlit になる
//     -> 画面にもログにも理由が出ない
// という状態になっていた。マテリアルでトゥーンを選んだのに
// 反映されない原因がこれ。
//
// Unity ではマテリアルが指定した絵柄が必ず使われる。
// グローバルなスイッチがマテリアルを黙って上書きすることはない。
// ここもそれに合わせ、**指定された絵柄をそのまま返す**。
//
// フラグは描画を止める役目をやめ、診断表示だけに使う
// （framework_editor.cpp の draw_runtime_mode_banner で警告を出す）。

ID3D11PixelShader* framework::skinned_forward_shader(int shading) const
{
    // nullptrは各メッシュが持つ標準ピクセルシェーダーを使う指定になる。
    switch (shading)
    {
    case SHADING_MODEL_PBR:      return pbr.skinned_mesh_ps();
    case SHADING_MODEL_TOON:     return toon.skinned_mesh_ps();
    case SHADING_MODEL_UNLIT:    return skinned_mesh_unlit_ps.Get();
    case SHADING_MODEL_PIXELATE: return object_pixelate_ps.Get();
    default:                     return nullptr;
    }
}

ID3D11PixelShader* framework::static_forward_shader(int shading) const
{
    switch (shading)
    {
    case SHADING_MODEL_PBR:      return pbr.static_mesh_ps();
    case SHADING_MODEL_TOON:     return toon.static_mesh_ps();
    case SHADING_MODEL_UNLIT:    return static_mesh_unlit_ps.Get();
    case SHADING_MODEL_PIXELATE: return object_pixelate_ps.Get();
    default:                     return nullptr;
    }
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

void framework::bind_gbuffer_material(
    ReplayEngine::Rendering::ShaderLightingModel lighting_model,
    bool stage_surface, bool pixelate_enabled,
    float pixelate_size, float pixelate_strength, float metallic, float roughness,
    float ambient_occlusion, float emissive_strength,
    const DirectX::XMFLOAT4& base_color_factor,
    const DirectX::XMFLOAT3& emissive_color,
    std::uint32_t texture_mask)
{
    material_override_constants constants{};
    constants.base_color_factor = base_color_factor;
    constants.emissive_factor = DirectX::XMFLOAT4{
        emissive_color.x, emissive_color.y, emissive_color.z, emissive_strength };
    constants.mat_params = stage_surface
        ? DirectX::XMFLOAT4{ 0.0f, 0.88f, 1.0f, 0.0f }
        : DirectX::XMFLOAT4{ metallic, roughness, ambient_occlusion, 0.0f };
    constants.lighting_model = static_cast<unsigned int>(lighting_model);
    constants.texture_contrast = stage_surface ? stage_texture_contrast : 1.0f;
    constants.pixelate_size = pixelate_enabled ? pixelate_size : 0.0f;
    constants.pixelate_strength = pixelate_enabled ? pixelate_strength : 0.0f;
    constants.texture_mask = texture_mask;

    immediate_context->UpdateSubresource(material_override_cb.Get(), 0,
        nullptr, &constants, 0, 0);
    immediate_context->PSSetConstantBuffers(9, 1,
        material_override_cb.GetAddressOf());
}

void framework::render(float elapsed_time)
{
    apply_pending_resize();
    if (!render_target_view || !depth_stencil_view || !framebuffers[0]) return;

    // 描画統計の計測開始。CPUカウンタを0に戻し、GPUクエリを開く。
    ReplayEngine::Rendering::Stats().BeginFrame(immediate_context.Get());

    // 前フレームのRTV/SRV参照を先に外し、同じリソースを入出力へ同時設定する競合を防ぐ。
    ID3D11RenderTargetView* null_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    immediate_context->OMSetRenderTargets(_countof(null_rtvs), null_rtvs, 0);
    ID3D11ShaderResourceView* null_srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    immediate_context->VSSetShaderResources(0, _countof(null_srvs), null_srvs);
    immediate_context->PSSetShaderResources(0, _countof(null_srvs), null_srvs);

    FLOAT clear_color[]{ background_color.x, background_color.y, background_color.z, background_color.w };
    immediate_context->ClearRenderTargetView(render_target_view.Get(), clear_color);
    immediate_context->ClearDepthStencilView(depth_stencil_view.Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    immediate_context->PSSetSamplers(0, 1, sampler_states[(size_t)SAMPLER_STATE::POINT].GetAddressOf());
    immediate_context->PSSetSamplers(1, 1, sampler_states[(size_t)SAMPLER_STATE::LINEAR].GetAddressOf());
    immediate_context->PSSetSamplers(2, 1, sampler_states[(size_t)SAMPLER_STATE::ANISOTROPIC].GetAddressOf());

    D3D11_VIEWPORT viewport;
    UINT num_viewports{ 1 };
    immediate_context->RSGetViewports(&num_viewports, &viewport);

    if (scene_manager.IsExclusive())
    {
#ifdef USE_IMGUI
        // 排他シーン中はエディタUIを出さない。NewFrame済みなら破棄して対を保つ。
        if (imgui_frame_active)
        {
            imgui_frame_active = false;
            ImGui::EndFrame();
        }
#endif
        // 起動ロゴやロード画面はゲーム側の描画パイプラインを通さず、画面全体へ直接描く。
        immediate_context->OMSetRenderTargets(1, render_target_view.GetAddressOf(), nullptr);
        immediate_context->OMSetBlendState(
            blend_states[(size_t)BLEND_STATE::ALPHA].Get(), nullptr, 0xFFFFFFFF);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        scene_manager.Render({ immediate_context.Get(), viewport.Width, viewport.Height });
        // 早期returnでもクエリを閉じる。開いたままにすると次フレームで
        // 二重Beginになりクエリが壊れる。
        ReplayEngine::Rendering::Stats().EndFrame(immediate_context.Get());
        swap_chain->Present(0, 0);
        return;
    }

    // Scene View / Game View の行列はここ 1 か所からしか取らない。
    //
    //   Edit Mode        -> 編集カメラ (EditorViewportCamera)
    //   Play / 通常実行   -> Runtime Camera (SceneGame が持つ Camera)
    //
    // Picking・Gizmo・Collider Debug Draw もすべて同じ窓口を通るので、
    // 「見えている位置」と「拾える位置」と「線の位置」がずれない。
    const ReplayEngine::Rendering::PostProcessPass::Settings original_post_settings =
        post_process.GetSettings();
    const bool original_enable_bloom_shader = enable_bloom_shader;
    const bool original_enable_vignette_shader = enable_vignette_shader;
    const bool original_enable_ssao = enable_ssao;
    const bool original_enable_ssr = enable_ssr;
    const bool original_enable_taa = enable_taa;
    const float original_luminance_threshold = luminance_threshold;
    const float original_ssao_radius = ssao_pass.radius;
    const float original_ssao_intensity = ssao_pass.intensity;
    const bool original_ssao_pass_enabled = ssao_pass.enabled;
    const float original_ssr_intensity = ssr_pass.intensity;
    const bool original_ssr_pass_enabled = ssr_pass.enabled;
    const bool original_taa_pass_enabled = taa_pass.enabled;

    const auto volume_selection =
        ReplayEngine::Components::ResolvePostProcessVolumeSelection(
            active_object_scene());
    if (volume_selection.Valid())
    {
        const PostProcessVolumeComponent& volume = *volume_selection.component;
        auto& settings = post_process.GetSettings();
        settings.exposure = clamp_finite(volume.exposure,
            original_post_settings.exposure, 0.01f, 8.0f);
        settings.bloom_intensity = clamp_finite(volume.bloom_intensity,
            original_post_settings.bloom_intensity, 0.0f, 8.0f);
        settings.vignette_strength = clamp_finite(volume.vignette_intensity,
            original_post_settings.vignette_strength, 0.0f, 1.0f);
        settings.color_filter = clamp_color(volume.color_filter);

        enable_bloom_shader = volume.bloom_enabled;
        enable_vignette_shader = volume.vignette_enabled;
        enable_ssao = volume.ssao_enabled;
        enable_ssr = volume.ssr_enabled;
        enable_taa = volume.taa_enabled;
        luminance_threshold = clamp_finite(volume.bloom_threshold,
            original_luminance_threshold, 0.0f, 16.0f);

        ssao_pass.enabled = volume.ssao_enabled;
        ssao_pass.radius = clamp_finite(volume.ssao_radius,
            original_ssao_radius, 0.01f, 16.0f);
        ssao_pass.intensity = clamp_finite(volume.ssao_intensity,
            original_ssao_intensity, 0.0f, 8.0f);
        ssr_pass.enabled = volume.ssr_enabled;
        ssr_pass.intensity = clamp_finite(volume.ssr_intensity,
            original_ssr_intensity, 0.0f, 8.0f);
        taa_pass.enabled = volume.taa_enabled;
    }

    struct CameraRenderPass
    {
        const ReplayEngine::Components::CameraComponent* camera = nullptr;
        bool matrix_override = false;
        DirectX::XMFLOAT4X4 view{};
        DirectX::XMFLOAT4X4 projection{};
        DirectX::XMFLOAT3 eye{ 0.0f, 0.0f, 0.0f };
        D3D11_VIEWPORT output{};
    };

    auto make_output_viewport = [&](const DirectX::XMFLOAT4& rect)
    {
        D3D11_VIEWPORT output = viewport;
        const float x = clamp_finite(rect.x, 0.0f, 0.0f, 1.0f);
        const float y = clamp_finite(rect.y, 0.0f, 0.0f, 1.0f);
        const float width = (std::min)(
            clamp_finite(rect.z, 1.0f, 0.01f, 1.0f), 1.0f - x);
        const float height = (std::min)(
            clamp_finite(rect.w, 1.0f, 0.01f, 1.0f), 1.0f - y);
        output.TopLeftX = viewport.TopLeftX + x * viewport.Width;
        output.TopLeftY = viewport.TopLeftY + y * viewport.Height;
        output.Width = (std::max)(1.0f, width * viewport.Width);
        output.Height = (std::max)(1.0f, height * viewport.Height);
        return output;
    };

    std::vector<CameraRenderPass> camera_render_passes;
    if (using_editor_camera())
    {
        // Scene View 本体は従来どおり全面を使う。Picking/Gizmo は client 全体を
        // 前提にしているため、4 分割へ置き換えると編集座標がずれる。補助 View は
        // 右側へ重ねるだけにし、既存 Scene View の操作基盤を変えない。
        CameraRenderPass main_pass{};
        main_pass.output = viewport;
        camera_render_passes.push_back(main_pass);

        if (editor_auxiliary_views)
        {
            const DirectX::XMFLOAT3 center = editor_camera.OrbitPivot();
            const float distance = (std::max)(editor_camera.OrbitDistance(), 1.0f);
            const float ortho_height = (std::max)(1.0f, distance * 1.5f);

            auto add_auxiliary = [&](const DirectX::XMFLOAT3& eye,
                const DirectX::XMFLOAT3& up, const DirectX::XMFLOAT4& rect)
            {
                CameraRenderPass pass{};
                pass.matrix_override = true;
                pass.eye = eye;
                pass.output = make_output_viewport(rect);
                const float aspect = pass.output.Width / pass.output.Height;
                const DirectX::XMMATRIX view_matrix = DirectX::XMMatrixLookAtLH(
                    DirectX::XMLoadFloat3(&eye), DirectX::XMLoadFloat3(&center),
                    DirectX::XMLoadFloat3(&up));
                const DirectX::XMMATRIX projection_matrix =
                    DirectX::XMMatrixOrthographicLH(ortho_height * aspect,
                        ortho_height, 0.05f, 10000.0f);
                DirectX::XMStoreFloat4x4(&pass.view, view_matrix);
                DirectX::XMStoreFloat4x4(&pass.projection, projection_matrix);
                camera_render_passes.push_back(pass);
            };

            add_auxiliary({ center.x, center.y, center.z - distance },
                { 0.0f, 1.0f, 0.0f }, { 0.72f, 0.02f, 0.26f, 0.28f });
            add_auxiliary({ center.x - distance, center.y, center.z },
                { 0.0f, 1.0f, 0.0f }, { 0.72f, 0.35f, 0.26f, 0.28f });
            add_auxiliary({ center.x, center.y + distance, center.z },
                { 0.0f, 0.0f, 1.0f }, { 0.72f, 0.68f, 0.26f, 0.28f });
        }
    }
    else
    {
        // viewport_enabled を使っている Scene だけ複数 Camera へ切り替える。
        // 旧 Scene は値を持たない = false なので、これまで通り「priority 最大の
        // Camera 1 台を全画面」で描く経路へそのまま落ちる。
        const ReplayEngine::Scene::Scene& scene_for_cameras = active_object_scene();
        for (std::size_t object_index = 0;
            object_index < scene_for_cameras.GameObjectCount(); ++object_index)
        {
            const ReplayEngine::Core::GameObject* object =
                scene_for_cameras.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy()) continue;
            const auto* camera = object->GetComponent<
                ReplayEngine::Components::CameraComponent>();
            if (camera == nullptr || !camera->ActiveInHierarchy() ||
                !camera->viewport_enabled) continue;

            CameraRenderPass pass{};
            pass.camera = camera;
            pass.output = make_output_viewport(camera->viewport_rect);
            camera_render_passes.push_back(pass);
        }

        std::stable_sort(camera_render_passes.begin(), camera_render_passes.end(),
            [](const CameraRenderPass& lhs, const CameraRenderPass& rhs)
            {
                return lhs.camera->priority < rhs.camera->priority;
            });

        if (camera_render_passes.empty())
        {
            CameraRenderPass legacy_pass{};
            legacy_pass.output = viewport;
            camera_render_passes.push_back(legacy_pass);
        }
    }

    const bool multiple_camera_passes = camera_render_passes.size() > 1;
    if (multiple_camera_passes)
    {
        // Temporal 履歴は Camera ごとに保持する必要がある。共有履歴のまま TAA を
        // 回すと別 Camera の前フレームを再投影するため、複数 View 中だけ無効化する。
        // 将来 Camera ごとの履歴を持たせる場合はここを拡張点にする。
        enable_taa = false;
        taa_pass.enabled = false;
        previous_view_projection_valid = false;
    }

    for (std::size_t camera_pass_index = 0;
        camera_pass_index < camera_render_passes.size(); ++camera_pass_index)
    {
        const CameraRenderPass& camera_pass = camera_render_passes[camera_pass_index];
        render_camera_override = camera_pass.camera;
        render_matrix_override_active = camera_pass.matrix_override;
        if (camera_pass.matrix_override)
        {
            render_view_override = camera_pass.view;
            render_projection_override = camera_pass.projection;
            render_eye_override = camera_pass.eye;
        }
        render_camera_aspect = camera_pass.output.Width / camera_pass.output.Height;
        const D3D11_VIEWPORT camera_output_viewport = camera_pass.output;

        // 複数 Camera では前の Camera の履歴を次の Camera へ渡さない。
        if (multiple_camera_passes) previous_view_projection_valid = false;

        const DirectX::XMMATRIX V = viewport_view_matrix();
        const DirectX::XMMATRIX P = viewport_projection_matrix();

    // 以降の全描画パスが共有するカメラと主光源の定数を一度だけ更新する。
    scene_constants scene{};
    DirectX::XMStoreFloat4x4(&scene.view_projection, V * P);
    scene.light_direction = light_direction;
    {
        const DirectX::XMFLOAT3 eye = viewport_eye_position();
        scene.camera_position = { eye.x, eye.y, eye.z, 1.0f };
    }
    immediate_context->UpdateSubresource(constant_buffers[0].Get(), 0, 0, &scene, 0, 0);
    immediate_context->VSSetConstantBuffers(1, 1, constant_buffers[0].GetAddressOf());
    immediate_context->PSSetConstantBuffers(1, 1, constant_buffers[0].GetAddressOf());

    // SSAO/SSR/TAAが参照するカメラ行列群をb4へ載せる。
    update_frame_constants(V, P, elapsed_time, camera_pass_index == 0);

    // 視錐台カリング用の平面をこのフレームのビュー射影から作る。
    // 各メッシュは描画直前にこれを参照して画面外のプリミティブを捨てる。
    {
        auto& culling = ReplayEngine::Rendering::Culling();
        culling.BeginFrame();
        culling.frustum.BuildFromViewProjection(scene.view_projection);
        // 自動LODは画面上の投影サイズで段を決めるので、行列と画面高さを渡す。
        DirectX::XMStoreFloat4x4(&culling.view_projection, P);
        culling.screen_height = camera_output_viewport.Height;
        culling.camera_position = { scene.camera_position.x,
            scene.camera_position.y, scene.camera_position.z };
    }

    const float original_pbr_shadow_enable = pbr.light.shadow_params.w;
    const bool pbr_shadow_enabled =
        enable_pbr_shadow_shader && original_pbr_shadow_enable > 0.5f;
    if (!pbr_shadow_enabled)
    {
        pbr.light.shadow_params.w = 0.0f;
    }
    pbr.update_light_vp(light_direction, DirectX::XMFLOAT3(0, 0, 0), 20.0f);
    pbr.update_constants(immediate_context.Get());
    pbr.light.shadow_params.w = original_pbr_shadow_enable;
    {
        DirectX::XMFLOAT4X4 V4, P4;
        DirectX::XMStoreFloat4x4(&V4, V);
        DirectX::XMStoreFloat4x4(&P4, P);
        csm.update_cascades(light_direction, V4, P4, 30.0f);
        csm.update_constants(immediate_context.Get());
    }
    toon.update_constants(immediate_context.Get());
    lights.update_constants(immediate_context.Get());

    // 描画より先に時間依存のGPUパーティクルと軌跡を進める。
    bool particles_this_frame = enable_particles;
    BLEND_STATE particle_blend_state = BLEND_STATE::ADD;
    const ParticleEmitterSelection emitter_selection =
        select_particle_emitter(active_object_scene());
    if (emitter_selection.Valid())
    {
        const ParticleEmitterComponent& emitter = *emitter_selection.component;
        DirectX::XMFLOAT3 origin{ 0.0f, 0.0f, 0.0f };
        if (const TransformComponent* transform =
            emitter_selection.object->GetComponent<TransformComponent>())
        {
            origin = transform->Position();
        }
        const DirectX::XMFLOAT3 direction = normalize_or_up(emitter.direction);

        particles.active_count = static_cast<UINT>((std::max)(1,
            (std::min)(emitter.max_particles,
                static_cast<int>(particle_system::MAX_COUNT))));
        particles.constants.spawn_origin = {
            origin.x, origin.y, origin.z,
            clamp_finite(emitter.spawn_rate, 0.0f, 0.0f, 20000.0f)
        };
        particles.constants.spawn_direction = {
            direction.x, direction.y, direction.z,
            clamp_finite(emitter.cone_angle, 0.0f, 0.0f, 3.14159f)
        };
        const float lifetime = clamp_finite(emitter.lifetime, 1.0f, 0.01f, 60.0f);
        const float speed = clamp_finite(emitter.start_speed, 0.0f, 0.0f, 200.0f);
        particles.constants.spawn_params = { speed, speed, lifetime, lifetime };
        particles.constants.spawn_color = clamp_color(emitter.start_color);
        particles.constants.end_color = clamp_color(emitter.end_color);
        particles.constants.spawn_scalar = {
            clamp_finite(emitter.start_size, 0.1f, 0.001f, 100.0f),
            0.0f,
            clamp_finite(emitter.gravity, 0.0f, -100.0f, 100.0f),
            clamp_finite(emitter.drag, 0.0f, 0.0f, 20.0f)
        };
        particles.constants.end_scalar = {
            clamp_finite(emitter.end_size, 0.02f, 0.001f, 100.0f),
            0.0f, 0.0f, 0.0f
        };

        switch (emitter.blend_mode)
        {
        case 1: particle_blend_state = BLEND_STATE::ADD; break;
        case 2: particle_blend_state = BLEND_STATE::MULTIPLY; break;
        case 3: particle_blend_state = BLEND_STATE::SCREEN; break;
        default: particle_blend_state = BLEND_STATE::ALPHA; break;
        }
        particles_this_frame = true;
    }

    if (camera_pass_index == 0 && particles_this_frame)
        particles.simulate(immediate_context.Get(), elapsed_time);
    if (camera_pass_index == 0 && enable_trail)
        test_trail.update(elapsed_time);

    // アニメーション付きモデルは例外なく
    // SkinnedMeshRendererComponent + AnimatorComponent が提出し、
    // draw_object_scene_meshes() / RenderItem 経由でのみ描かれる。
    // Shadow / CSM / GBuffer / Forward / Outline のどのパスにも
    // Player 専用の分岐は残っていない。

    if (csm.constants.params.w > 0.5f && enable_static_meshes && static_meshes[0])
    {
        // カスケードシャドウ用深度を先に作る。終了時に元のRTVとViewportを復元する。
        D3D11_VIEWPORT main_vp = viewport;
        csm.shadow_begin(immediate_context.Get());

        DirectX::XMFLOAT4X4 world;
        store_debug_mesh_world(world);
        static_meshes[0]->render(immediate_context.Get(), world, material_color,
            nullptr,
            csm.caster_static_vs.Get(),
            csm.caster_static_il.Get(),
            false);

        csm.shadow_end(immediate_context.Get(),
            render_target_view.Get(), depth_stencil_view.Get(), main_vp);
    }

    if (pbr_shadow_enabled && enable_static_meshes && static_meshes[0])
    {
        // PBR固有のシャドウマップはCSMと別リソースなので、必要な場合だけ生成する。
        D3D11_VIEWPORT main_vp = viewport;
        pbr.shadow_begin(immediate_context.Get());

        DirectX::XMFLOAT4X4 world;
        store_debug_mesh_world(world);
        static_meshes[0]->render(immediate_context.Get(), world, material_color,
            nullptr,
            pbr.shadow_caster_static_vs.Get(),
            pbr.shadow_caster_static_il.Get(),
            false);

        pbr.shadow_end(immediate_context.Get(),
            render_target_view.Get(), depth_stencil_view.Get(), main_vp);
    }

    // 3Dシーンは中間フレームバッファへ描き、最後にポスト処理してバックバッファへ出力する。
    framebuffers[0]->clear(immediate_context.Get(),
        background_color.x, background_color.y, background_color.z, background_color.w);
    framebuffers[0]->activate(immediate_context.Get());

    immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::ALPHA].Get(), nullptr, 0xFFFFFFFF);
    immediate_context->OMSetDepthStencilState(depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_ON].Get(), 0);
    immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());

    // 背景画像は任意アセット。読み込めていない場合は sprite_batches[0] が空になる。
    if (draw_background_image && sprite_batches[0])
    {
        // 背景画像は深度を書かず、3Dオブジェクトより先に中間バッファへ敷く。
        sprite_batches[0]->begin(immediate_context.Get());
        sprite_batches[0]->render(immediate_context.Get(), 0, 0,
            static_cast<float>(client_width), static_cast<float>(client_height), 1, 1, 1, 1, 0);
        sprite_batches[0]->end(immediate_context.Get());
    }

    immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);

    pbr.bind_pbr_resources(immediate_context.Get());
    csm.bind_resources(immediate_context.Get());
    toon.bind_resources(immediate_context.Get());
    immediate_context->PSSetShaderResources(1, 1, dummy_normal_srv.GetAddressOf());

    const auto make_base_pixelate_layer = [](float grid, float strength)
    {
        ReplayEngine::Rendering::ShaderLayer layer{};
        layer.type = ReplayEngine::Rendering::ShaderLayerType::Pixelate;
        layer.opacity = 1.0f;
        layer.strength = strength;
        layer.parameter = grid;
        return layer;
    };
    // Pixelate の固定設定はエディタのデバッグ用静的メッシュだけに使う。
    const auto static_pixelate_layer = make_base_pixelate_layer(
        pixelate_grid_per_static[0], pixelate_strength_per_static[0]);
    const auto find_pixelate_layer = [](const ReplayEngine::Rendering::ShaderLayerStack& stack)
        -> const ReplayEngine::Rendering::ShaderLayer*
    {
        for (const auto& layer : stack.Layers())
        {
            if (layer.enabled &&
                layer.type == ReplayEngine::Rendering::ShaderLayerType::Pixelate)
                return &layer;
        }
        return nullptr;
    };
    const auto* static_added_pixelate = find_pixelate_layer(shader_layers_static[0]);
    const bool static_uses_pixelate = shading_per_static[0] == SHADING_MODEL_PIXELATE ||
        static_added_pixelate != nullptr;
    const auto& static_pixelate_settings = static_added_pixelate
        ? *static_added_pixelate : static_pixelate_layer;

    // 通常描画はDeferredへ統一する。Forward+は将来別経路として追加する。
    const bool deferred_active = deferred.initialized;
    if (deferred_active)
    {
        DirectX::XMFLOAT4X4 world;
        store_debug_mesh_world(world);

        // --- 深度プリパス -------------------------------------------------
        // 深度だけを先に埋めてから、G-Buffer本描画をDepthFunc=EQUALで走らせる。
        // 柱が重なるシーンではG-Buffer PS(テクスチャ3枚サンプル)の実行回数が
        // 手前の1回だけになる。頂点処理は2回になるので、LODとの併用が前提。
        const bool use_depth_prepass = enable_depth_prepass && deferred.depth_equal_state;
        if (use_depth_prepass)
        {
            deferred.depth_prepass_begin(immediate_context.Get());
            immediate_context->OMSetBlendState(
                blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_ON].Get(), 0);
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

            // 深度だけなのでピクセルシェーダーは外す(bind_pixel_shader=false)。
            // モーションベクターもここでは書かない。
            //
            // キャラクターは GameObject の提出リスト経由でのみ描かれる。
            if (enable_static_meshes && static_meshes[0])
                static_meshes[0]->render(immediate_context.Get(), world, material_color,
                    nullptr, nullptr, nullptr, false, false);
            // GameObject の提出リストもここで深度を書く。
            //
            // 【必須】本描画は DepthFunc=EQUAL なので、プリパスで深度を書かなかった
            // メッシュは比較に失敗して画面から丸ごと消える。
            // キャラクターはこの経路でしか描かれないため、ここを外すと何も映らない。
            draw_landscape_scene_meshes(false, true);
            draw_object_scene_meshes(nullptr, false, true);

            // 深度プリパスの描画数は統計へ混ぜない(同じ形状を二重に数えないため)。
        }

        // Deferred経路は材質情報をGBufferへ集約し、照明パスで一括して色を決定する。
        FLOAT deferred_clear[]{ background_color.x, background_color.y, background_color.z, background_color.w };
        deferred.gbuffer_begin(immediate_context.Get(), deferred_clear, !use_depth_prepass);
        immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
        // プリパス済みなら EQUAL 比較で最前面だけを通す。
        immediate_context->OMSetDepthStencilState(use_depth_prepass
            ? deferred.depth_equal_state.Get()
            : depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_ON].Get(), 0);
        immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

        if (enable_static_meshes && static_meshes[0])
        {
            bind_gbuffer_material(
                deferred_lighting_model(shading_per_static[0]),
                false, static_uses_pixelate,
                static_pixelate_settings.parameter,
                static_pixelate_settings.strength);
            static_meshes[0]->render(immediate_context.Get(), world, material_color,
                                     static_mesh_gbuffer_ps.Get(),
                                     nullptr, nullptr, true, true);
        }

        // GameObject / Component 基盤が提出した描画対象を GBuffer へ描く。
        // 提出リストは update_object_scene() が毎フレーム作り直す。
        //
        // Stage/Characterを含むScene描画はこの提出リスト1本だけ。
        draw_landscape_scene_meshes(true, false);
        draw_object_scene_meshes(skinned_mesh_gbuffer_ps.Get(), true);

        deferred.gbuffer_end(immediate_context.Get());

        // 照明の前にSSAOを解く。G-Bufferの深度と法線だけで完結するパス。
        ID3D11ShaderResourceView* ambient_occlusion = nullptr;
        if (enable_ssao && ssao_pass.Initialized())
        {
            ssao_pass.enabled = true;
            ambient_occlusion = ssao_pass.Execute(immediate_context.Get(),
                *bit_block_transfer, deferred.depth_srv.Get(),
                deferred.gbuffer_srv[2].Get());
        }

        // SSRも照明前に解き、PBRの鏡面項へ差し込む。反射源は前フレームの
        // ライティング結果なので、この時点で参照しても自己参照にならない。
        ID3D11ShaderResourceView* screen_reflection = nullptr;
        if (enable_ssr && ssr_pass.Initialized())
        {
            ssr_pass.enabled = true;
            screen_reflection = ssr_pass.Execute(immediate_context.Get(),
                *bit_block_transfer, deferred.depth_srv.Get(),
                deferred.gbuffer_srv[2].Get(), deferred.gbuffer_srv[3].Get());
        }

        // スクリーン空間パスがb1を触るため、共有シーン定数だけ貼り直す。
        immediate_context->PSSetConstantBuffers(1, 1, constant_buffers[0].GetAddressOf());

        // GBufferをSRVへ切り替えた後、ライト計算結果をDeferred側の出力へ書く。
        // タイルド版が有効なときはコンピュートシェーダーへ差し替える。
        // デバッグ表示中はPS版のみが対応しているのでそちらを使う。
        // Object/primitive draws leave their own culling and depth states behind.
        // The classic deferred pass renders a fullscreen strip, so inheriting a
        // mesh cull state can reject the entire lighting pass and leave only clear.
        immediate_context->OMSetBlendState(
            blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

        const bool use_tiled = tiled_deferred.enabled && tiled_deferred.Initialized() &&
            render_graph.DeferredDebugMode() == 0;
        if (use_tiled)
        {
            // 定数バッファ配列(b10)の点光源/スポットをStructuredBufferへ移す。
            // CS側ではb10を貼らないため、二重計上にはならない。
            tiled_deferred.ClearLights();
            for (int i = 0; i < lights.data.light_counts.x && i < lights_manager::POINT_LIGHT_MAX; ++i)
            {
                const auto& point = lights.data.point_lights[i];
                tiled_deferred.AddPointLight(
                    { point.position.x, point.position.y, point.position.z },
                    point.position.w,
                    { point.color.x, point.color.y, point.color.z }, point.color.w);
            }
            for (int i = 0; i < lights.data.light_counts.y && i < lights_manager::SPOT_LIGHT_MAX; ++i)
            {
                const auto& spot = lights.data.spot_lights[i];
                tiled_deferred.AddSpotLight(
                    { spot.position.x, spot.position.y, spot.position.z },
                    spot.position.w,
                    { spot.direction.x, spot.direction.y, spot.direction.z },
                    spot.direction.w, spot.color.w,
                    { spot.color.x, spot.color.y, spot.color.z }, spot.params.x);
            }

            // CSはPSとスロットが独立しているので、必要なものを貼り直す。
            immediate_context->CSSetConstantBuffers(1, 1, constant_buffers[0].GetAddressOf());
            immediate_context->CSSetConstantBuffers(4, 1, frame_constants_cb.GetAddressOf());
            immediate_context->CSSetSamplers(0, 1,
                sampler_states[(size_t)SAMPLER_STATE::POINT].GetAddressOf());
            immediate_context->CSSetSamplers(1, 1,
                sampler_states[(size_t)SAMPLER_STATE::LINEAR].GetAddressOf());
            immediate_context->CSSetSamplers(2, 1,
                sampler_states[(size_t)SAMPLER_STATE::ANISOTROPIC].GetAddressOf());
            pbr.bind_compute_resources(immediate_context.Get());
            csm.bind_compute_resources(immediate_context.Get());

            ID3D11ShaderResourceView* gbuffer_views[4]{
                deferred.gbuffer_srv[0].Get(), deferred.gbuffer_srv[1].Get(),
                deferred.gbuffer_srv[2].Get(), deferred.gbuffer_srv[3].Get() };
            tiled_deferred.Dispatch(immediate_context.Get(), deferred.lit_uav.Get(),
                gbuffer_views, deferred.depth_srv.Get(),
                ambient_occlusion, screen_reflection);

            pbr.unbind_compute_resources(immediate_context.Get());
            csm.unbind_compute_resources(immediate_context.Get());
        }
        else
        {
            deferred.lighting_pass(immediate_context.Get(), scene.view_projection,
                                   background_color, render_graph.DeferredDebugMode(),
                                   ambient_occlusion, screen_reflection);
        }

        // 次フレームのSSR用に、照明直後のHDRカラーを履歴として確保する。
        if (enable_ssr && ssr_pass.Initialized() && render_graph.DeferredDebugMode() == 0)
            ssr_pass.CaptureHistory(immediate_context.Get(), deferred.lit_tex.Get());

        bool object_layers_present = false;
        for (const ReplayEngine::Rendering::RenderItem& source_item : object_render_items.Items())
        {
            const ReplayEngine::Rendering::RenderItem item =
                resolve_render_item_material(source_item);
            if (item.outline || (item.material_binding.layers != nullptr &&
                item.material_binding.layers->HasEnabledLayers()))
            {
                object_layers_present = true;
                break;
            }
        }

        const bool draw_shader_layers = render_graph.DeferredDebugMode() == 0 &&
            (shader_layers_static[0].HasEnabledLayers() ||
                shading_per_static[0] == SHADING_MODEL_PIXELATE ||
                object_layers_present);
        if (draw_shader_layers)
        {
            // Surfaceとは別の材質パスをDeferred照明結果へ順番どおりに合成する。
            immediate_context->OMSetRenderTargets(1, deferred.lit_rtv.GetAddressOf(), deferred.depth_dsv.Get());
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);

            const auto prepare_layer = [this](const ReplayEngine::Rendering::ShaderLayer& layer,
                const ReplayEngine::Rendering::CharacterMaterialProfile& profile)
            {
                BLEND_STATE blend = BLEND_STATE::ALPHA;
                if (layer.blend == ReplayEngine::Rendering::ShaderLayerBlend::Additive)
                    blend = BLEND_STATE::ADD;
                else if (layer.blend == ReplayEngine::Rendering::ShaderLayerBlend::Multiply)
                    blend = BLEND_STATE::MULTIPLY;
                immediate_context->OMSetBlendState(blend_states[(size_t)blend].Get(), nullptr, 0xFFFFFFFF);
                const bool wireframe = layer.type == ReplayEngine::Rendering::ShaderLayerType::Wireframe;
                immediate_context->RSSetState(rasterizer_states[(size_t)(wireframe
                    ? RASTER_STATE::WIREFRAME_CULL_NONE : RASTER_STATE::CULL_NONE)].Get());
                if (layer.type == ReplayEngine::Rendering::ShaderLayerType::StylizedCharacter)
                {
                    const auto constants =
                        ReplayEngine::Rendering::CharacterMaterialGpuData::FromProfile(profile);
                    immediate_context->UpdateSubresource(
                        character_material_cb.Get(), 0, nullptr, &constants, 0, 0);
                    immediate_context->PSSetConstantBuffers(
                        11, 1, character_material_cb.GetAddressOf());
                }
            };
            const auto layer_color = [](const ReplayEngine::Rendering::ShaderLayer& layer)
            {
                DirectX::XMFLOAT4 color = layer.tint;
                color.w *= layer.opacity;
                return color;
            };
            const auto static_pixel_shader = [this](const ReplayEngine::Rendering::ShaderLayer& layer)
                -> ID3D11PixelShader*
            {
                using ReplayEngine::Rendering::ShaderLayerType;
                switch (layer.type)
                {
                case ShaderLayerType::Pbr:       return pbr.static_mesh_ps();
                case ShaderLayerType::Toon:      return toon.static_mesh_ps();
                case ShaderLayerType::Pixelate: return object_pixelate_ps.Get();
                case ShaderLayerType::StylizedCharacter: return static_stylized_character_ps.Get();
                default:                        return static_mesh_unlit_ps.Get();
                }
            };

            // キャラクターの追加パスは Renderer Component の設定で決まる。
            if (enable_static_meshes && static_meshes[0])
            {
                for (const auto& layer : shader_layers_static[0].Layers())
                {
                    if (!layer.enabled) continue;
                    if (layer.type == ReplayEngine::Rendering::ShaderLayerType::Pixelate) continue;
                    if (layer.Is(ReplayEngine::Rendering::BuiltInShaderLayers::Outline))
                    {
                        const auto saved_outline = toon.outline;
                        toon.outline.outline_color = layer.tint;
                        toon.outline.outline_params.x = layer.parameter;
                        toon.update_constants(immediate_context.Get());
                        immediate_context->OMSetBlendState(
                            blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
                        toon.bind_outline_pass(immediate_context.Get(), false);
                        static_meshes[0]->render(immediate_context.Get(), world, material_color,
                            toon.outline_ps(), toon.static_outline_vs_.Get(),
                            toon.static_outline_il_.Get(), true);
                        toon.outline = saved_outline;
                        toon.update_constants(immediate_context.Get());
                        immediate_context->RSSetState(
                            rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
                        continue;
                    }
                    prepare_layer(layer, character_profiles_static[0]);
                    const auto color = layer_color(layer);
                    static_meshes[0]->render(immediate_context.Get(), world, color,
                        static_pixel_shader(layer));
                }
            }
            // GameObject Material 固有のLayerStack。同じ順序で追加パスを描く。
            const ReplayEngine::Rendering::CharacterMaterialProfile default_profile{};
            for (const ReplayEngine::Rendering::RenderItem& source_item : object_render_items.Items())
            {
                const ReplayEngine::Rendering::RenderItem item =
                    resolve_render_item_material(source_item);
                if (item.mesh_asset.empty()) continue;
                skinned_mesh* mesh = resolve_object_mesh(item.mesh_asset);
                if (mesh == nullptr) continue;
                skinned_mesh::animation::keyframe blended_keyframe;
                const auto* keyframe =
                    resolve_render_item_keyframe(*mesh, item, blended_keyframe);

                bool outline_drawn = false;
                if (item.material_binding.layers != nullptr)
                {
                    for (const auto& layer : item.material_binding.layers->Layers())
                    {
                        if (!layer.enabled || layer.Is(ReplayEngine::Rendering::BuiltInShaderLayers::Pixelate))
                            continue; // PixelateはGBufferへ設定済み
                        if (layer.Is(ReplayEngine::Rendering::BuiltInShaderLayers::Outline))
                        {
                            const auto saved_outline = toon.outline;
                            toon.outline.outline_color = layer.tint;
                            toon.outline.outline_params.x = layer.parameter;
                            toon.update_constants(immediate_context.Get());
                            immediate_context->OMSetBlendState(
                                blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
                            toon.bind_outline_pass(immediate_context.Get(), true);
                            mesh->render(immediate_context.Get(), item.world, layer.tint,
                                keyframe, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                                toon.skinned_outline_il_.Get(), true, false);
                            toon.outline = saved_outline;
                            toon.update_constants(immediate_context.Get());
                            outline_drawn = true;
                            continue;
                        }

                        // Built-in 7種は見た目を変えない旧専用パス。
                        // それ以外は Layer Shader Asset を Catalog から直接描く。
                        if (!ReplayEngine::Rendering::BuiltInShaderLayers::IsBuiltIn(
                            layer.EffectiveShader()))
                        {
                            prepare_layer(layer, default_profile);
                            ReplayEngine::Rendering::ResolvedMaterialBinding layer_binding;
                            if (ReplayEngine::Rendering::ShaderLayerBindingResolver::Resolve(
                                layer, shader_library.Catalog(),
                                ReplayEngine::Rendering::ShaderVariant::Skinned, layer_binding))
                            {
                                const auto* layer_entry = shader_library.Catalog().Find(
                                    layer_binding.shader);
                                ID3D11PixelShader* catalog_layer =
                                    material_gpu_binder.ResolvePixelShader(
                                        device.Get(), shader_library.Catalog(), layer_binding);
                                if (catalog_layer != nullptr &&
                                    material_gpu_binder.Bind(device.Get(), immediate_context.Get(),
                                        asset_database, layer_binding))
                                {
                                    const DirectX::XMFLOAT4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
                                    mesh->render(immediate_context.Get(), item.world, white,
                                        keyframe, catalog_layer, nullptr, nullptr, true, false);

                                    // Shader-owned pass は Layer の直後、宣言順で固定実行する。
                                    // Material Editor から順番を変えるのは Layer だけ。
                                    if (layer_entry != nullptr)
                                    {
                                        for (std::size_t pass_index = 0;
                                            pass_index < layer_entry->passes.size(); ++pass_index)
                                        {
                                            const auto& pass = layer_entry->passes[pass_index];
                                            BLEND_STATE pass_blend = BLEND_STATE::ALPHA;
                                            switch (pass.info.blend)
                                            {
                                            case ReplayEngine::Rendering::ShaderPassBlend::Inherit:
                                                if (layer.blend == ReplayEngine::Rendering::ShaderLayerBlend::Additive)
                                                    pass_blend = BLEND_STATE::ADD;
                                                else if (layer.blend == ReplayEngine::Rendering::ShaderLayerBlend::Multiply)
                                                    pass_blend = BLEND_STATE::MULTIPLY;
                                                break;
                                            case ReplayEngine::Rendering::ShaderPassBlend::Additive:
                                                pass_blend = BLEND_STATE::ADD; break;
                                            case ReplayEngine::Rendering::ShaderPassBlend::Multiply:
                                                pass_blend = BLEND_STATE::MULTIPLY; break;
                                            case ReplayEngine::Rendering::ShaderPassBlend::Alpha:
                                            default: pass_blend = BLEND_STATE::ALPHA; break;
                                            }
                                            immediate_context->OMSetBlendState(
                                                blend_states[(size_t)pass_blend].Get(), nullptr, 0xFFFFFFFF);
                                            if (ID3D11PixelShader* pass_ps =
                                                material_gpu_binder.ResolvePassPixelShader(
                                                    device.Get(), shader_library.Catalog(),
                                                    layer_binding, pass_index))
                                            {
                                                mesh->render(immediate_context.Get(), item.world, white,
                                                    keyframe, pass_ps, nullptr, nullptr, true, false);
                                            }
                                        }
                                    }
                                }
                                material_gpu_binder.Unbind(immediate_context.Get());
                            }
                            continue;
                        }

                        prepare_layer(layer, default_profile);
                        ID3D11PixelShader* layer_ps = skinned_mesh_unlit_ps.Get();
                        using ReplayEngine::Rendering::ShaderLayerType;
                        switch (layer.type)
                        {
                        case ShaderLayerType::Pbr: layer_ps = pbr.skinned_mesh_ps(); break;
                        case ShaderLayerType::Toon: layer_ps = toon.skinned_mesh_ps(); break;
                        case ShaderLayerType::StylizedCharacter:
                            layer_ps = skinned_stylized_character_ps.Get(); break;
                        default: break;
                        }
                        mesh->render(immediate_context.Get(), item.world, layer_color(layer),
                            keyframe, layer_ps, nullptr, nullptr, true, false);
                    }
                }

                if (item.outline && !outline_drawn)
                {
                    immediate_context->OMSetBlendState(
                        blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
                    toon.bind_outline_pass(immediate_context.Get(), true);
                    mesh->render(immediate_context.Get(), item.world, item.tint,
                        keyframe, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                        toon.skinned_outline_il_.Get(), true, false);
                }
            }

            immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
            immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
        }

        // 輪郭線の追加パス。旧 Player 用の分岐は無い。
        // GameObject の輪郭線は Renderer Component の outline プロパティが決める。
        const bool draw_deferred_outline = render_graph.DeferredDebugMode() == 0 &&
            enable_outline_shader &&
            outline_per_static[0] &&
            !shader_layers_static[0].Contains(ReplayEngine::Rendering::ShaderLayerType::Outline);
        if (draw_deferred_outline)
        {
            // Deferred照明結果へ、同じDepthを使って追加パスを重ねる。
            immediate_context->OMSetRenderTargets(1, deferred.lit_rtv.GetAddressOf(), deferred.depth_dsv.Get());
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);
            immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
            if (enable_static_meshes && static_meshes[0] && outline_per_static[0] &&
                !shader_layers_static[0].Contains(ReplayEngine::Rendering::ShaderLayerType::Outline))
            {
                toon.bind_outline_pass(immediate_context.Get(), false);
                static_meshes[0]->render(immediate_context.Get(), world, material_color,
                    toon.outline_ps(), toon.static_outline_vs_.Get(),
                    toon.static_outline_il_.Get(), true);
            }
            immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
        }

        // シェーダーレイヤー(追加パス)はブレンドをALPHA/ADD/MULTIPLYへ変えるため、
        // 後続のフルスクリーンパスへ持ち越さないよう必ず不透明へ戻す。
        // ここを忘れると、TAAが加算合成になって履歴が累積し、
        // シーンビューが固まったように見える。
        immediate_context->OMSetBlendState(
            blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

        // TAAはトーンマップ前のHDRで解く。明部のエイリアスも正しく平均化され、
        // かつジッター済みの複数フレームが実質的なスーパーサンプリングになる。
        ID3D11ShaderResourceView* lit_srv = deferred.lit_srv.Get();
        if (enable_taa && taa_pass.Initialized() && render_graph.DeferredDebugMode() == 0)
        {
            taa_pass.enabled = true;
            ID3D11ShaderResourceView* resolved = taa_pass.Execute(
                immediate_context.Get(), *bit_block_transfer, lit_srv,
                deferred.depth_srv.Get(),
                deferred.gbuffer_srv[deferred_renderer::GBUFFER_VELOCITY_INDEX].Get());
            if (resolved) lit_srv = resolved;
        }
        else if (taa_pass.Initialized())
        {
            taa_pass.InvalidateHistory();
        }

        immediate_context->OMSetRenderTargets(1,
            framebuffers[0]->render_target_view.GetAddressOf(),
            framebuffers[0]->depth_stencil_view.Get());
        immediate_context->RSSetViewports(1, &framebuffers[0]->viewport);
        immediate_context->OMSetDepthStencilState(depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        // 後段のエフェクトを共通化するため、照明結果を通常の中間バッファへ戻す。
        bit_block_transfer->blit(immediate_context.Get(), &lit_srv, 0, 1);
    }
    else
    {
        // Forward経路はオブジェクトごとにシェーダーを選び、その場で最終色まで計算する。
        immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

        const auto bind_pixelate_settings = [this](const ReplayEngine::Rendering::ShaderLayer& layer)
        {
            const auto constants = ReplayEngine::Rendering::ShaderLayerGpuData::FromLayer(layer);
            immediate_context->UpdateSubresource(
                shader_layer_cb.Get(), 0, nullptr, &constants, 0, 0);
            immediate_context->PSSetConstantBuffers(10, 1, shader_layer_cb.GetAddressOf());
        };

        DirectX::XMFLOAT4X4 world;
        store_debug_mesh_world(world);

        if (enable_static_meshes && static_meshes[0])
        {
            if (shading_per_static[0] == SHADING_MODEL_PIXELATE)
                bind_pixelate_settings(static_pixelate_layer);
            static_meshes[0]->render(immediate_context.Get(), world, material_color,
                                     static_forward_shader(shading_per_static[0]));
        }

        // Landscape procedural mesh もForward経路へ出す。
        draw_landscape_scene_meshes(false, false);

        // GameObject / Component 基盤の描画（Forward 経路）。
        // Component ごとの描画方式を反映するため、Shader は 1 件ずつ選び直す。
        // Asset 未指定・解決不可・読み込み失敗の GameObject は静かに飛ばす。
        for (const ReplayEngine::Rendering::RenderItem& scene_item : object_render_items.Items())
        {
            const ReplayEngine::Rendering::RenderItem item =
                resolve_render_item_material(scene_item);
            if (item.mesh_asset.empty()) continue;
            skinned_mesh* scene_mesh = resolve_object_mesh(item.mesh_asset);
            if (scene_mesh == nullptr) continue;

            // Animator が決めたクリップと時刻から姿勢を求める。
            // クリップ長を知っているのは Renderer 側なので、ループ処理もここで解決する。
            skinned_mesh::animation::keyframe blended_keyframe;
            const skinned_mesh::animation::keyframe* item_keyframe =
                resolve_render_item_keyframe(*scene_mesh, item, blended_keyframe);

            ID3D11PixelShader* catalog_shader = nullptr;
            bool catalog_bound = false;
            if (item.material_binding.usable_shader)
            {
                catalog_shader = material_gpu_binder.ResolvePixelShader(
                    device.Get(), shader_library.Catalog(), item.material_binding);
                if (catalog_shader != nullptr)
                {
                    catalog_bound = material_gpu_binder.Bind(device.Get(),
                        immediate_context.Get(), asset_database,
                        item.material_binding);
                    // b9 の生成に失敗した状態で Catalog PS を使うと、旧 draw の
                    // 定数を読んでしまう。完全に bind できない場合は旧 .cso へ戻す。
                    if (!catalog_bound)
                    {
                        material_gpu_binder.Unbind(immediate_context.Get());
                        catalog_shader = nullptr;
                    }
                }
            }

            if (item.double_sided)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

            // Catalog shader を作れた場合だけ新経路へ切り替える。
            // Bind に一部失敗しても既定Textureへ落として描画は続ける。
            scene_mesh->render(immediate_context.Get(), item.world,
                catalog_shader != nullptr ? item.tint : item.legacy_tint,
                item_keyframe,
                catalog_shader != nullptr
                    ? catalog_shader : skinned_forward_shader(item.shading_model));

            if (catalog_shader != nullptr || catalog_bound)
                material_gpu_binder.Unbind(immediate_context.Get());

            // Forward時もMaterial固有Layerを宣言順で描く。
            bool outline_drawn = false;
            if (item.material_binding.layers != nullptr)
            {
                for (const auto& layer : item.material_binding.layers->Layers())
                {
                    if (!layer.enabled) continue;
                    if (layer.Is(ReplayEngine::Rendering::BuiltInShaderLayers::Outline))
                    {
                        const auto saved_outline = toon.outline;
                        toon.outline.outline_color = layer.tint;
                        toon.outline.outline_params.x = layer.parameter;
                        toon.update_constants(immediate_context.Get());
                        immediate_context->OMSetBlendState(
                            blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
                        toon.bind_outline_pass(immediate_context.Get(), true);
                        scene_mesh->render(immediate_context.Get(), item.world, layer.tint,
                            item_keyframe, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                            toon.skinned_outline_il_.Get(), true, false);
                        toon.outline = saved_outline;
                        toon.update_constants(immediate_context.Get());
                        outline_drawn = true;
                        continue;
                    }

                    if (!ReplayEngine::Rendering::BuiltInShaderLayers::IsBuiltIn(
                        layer.EffectiveShader()))
                    {
                        BLEND_STATE custom_blend = BLEND_STATE::ALPHA;
                        if (layer.blend == ReplayEngine::Rendering::ShaderLayerBlend::Additive)
                            custom_blend = BLEND_STATE::ADD;
                        else if (layer.blend == ReplayEngine::Rendering::ShaderLayerBlend::Multiply)
                            custom_blend = BLEND_STATE::MULTIPLY;
                        immediate_context->OMSetBlendState(
                            blend_states[(size_t)custom_blend].Get(), nullptr, 0xFFFFFFFF);
                        immediate_context->RSSetState(
                            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

                        ReplayEngine::Rendering::ResolvedMaterialBinding layer_binding;
                        if (ReplayEngine::Rendering::ShaderLayerBindingResolver::Resolve(
                            layer, shader_library.Catalog(),
                            ReplayEngine::Rendering::ShaderVariant::Skinned, layer_binding))
                        {
                            const auto* layer_entry = shader_library.Catalog().Find(
                                layer_binding.shader);
                            ID3D11PixelShader* catalog_layer =
                                material_gpu_binder.ResolvePixelShader(
                                    device.Get(), shader_library.Catalog(), layer_binding);
                            if (catalog_layer != nullptr &&
                                material_gpu_binder.Bind(device.Get(), immediate_context.Get(),
                                    asset_database, layer_binding))
                            {
                                const DirectX::XMFLOAT4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
                                scene_mesh->render(immediate_context.Get(), item.world, white,
                                    item_keyframe, catalog_layer, nullptr, nullptr, true, false);

                                if (layer_entry != nullptr)
                                {
                                    for (std::size_t pass_index = 0;
                                        pass_index < layer_entry->passes.size(); ++pass_index)
                                    {
                                        const auto& pass = layer_entry->passes[pass_index];
                                        BLEND_STATE pass_blend = custom_blend;
                                        if (pass.info.blend == ReplayEngine::Rendering::ShaderPassBlend::Alpha)
                                            pass_blend = BLEND_STATE::ALPHA;
                                        else if (pass.info.blend == ReplayEngine::Rendering::ShaderPassBlend::Additive)
                                            pass_blend = BLEND_STATE::ADD;
                                        else if (pass.info.blend == ReplayEngine::Rendering::ShaderPassBlend::Multiply)
                                            pass_blend = BLEND_STATE::MULTIPLY;
                                        immediate_context->OMSetBlendState(
                                            blend_states[(size_t)pass_blend].Get(), nullptr, 0xFFFFFFFF);
                                        if (ID3D11PixelShader* pass_ps =
                                            material_gpu_binder.ResolvePassPixelShader(
                                                device.Get(), shader_library.Catalog(),
                                                layer_binding, pass_index))
                                        {
                                            scene_mesh->render(immediate_context.Get(), item.world, white,
                                                item_keyframe, pass_ps, nullptr, nullptr, true, false);
                                        }
                                    }
                                }
                            }
                            material_gpu_binder.Unbind(immediate_context.Get());
                        }
                        continue;
                    }

                    BLEND_STATE blend = BLEND_STATE::ALPHA;
                    if (layer.blend == ReplayEngine::Rendering::ShaderLayerBlend::Additive)
                        blend = BLEND_STATE::ADD;
                    else if (layer.blend == ReplayEngine::Rendering::ShaderLayerBlend::Multiply)
                        blend = BLEND_STATE::MULTIPLY;
                    immediate_context->OMSetBlendState(
                        blend_states[(size_t)blend].Get(), nullptr, 0xFFFFFFFF);
                    immediate_context->RSSetState(rasterizer_states[(size_t)(
                        layer.type == ReplayEngine::Rendering::ShaderLayerType::Wireframe
                            ? RASTER_STATE::WIREFRAME_CULL_NONE : RASTER_STATE::CULL_NONE)].Get());

                    ID3D11PixelShader* layer_ps = skinned_mesh_unlit_ps.Get();
                    using ReplayEngine::Rendering::ShaderLayerType;
                    switch (layer.type)
                    {
                    case ShaderLayerType::Pbr: layer_ps = pbr.skinned_mesh_ps(); break;
                    case ShaderLayerType::Toon: layer_ps = toon.skinned_mesh_ps(); break;
                    case ShaderLayerType::Pixelate:
                        bind_pixelate_settings(layer); layer_ps = object_pixelate_ps.Get(); break;
                    case ShaderLayerType::StylizedCharacter:
                        layer_ps = skinned_stylized_character_ps.Get(); break;
                    default: break;
                    }
                    DirectX::XMFLOAT4 layer_tint = layer.tint;
                    layer_tint.w *= layer.opacity;
                    scene_mesh->render(immediate_context.Get(), item.world, layer_tint,
                        item_keyframe, layer_ps, nullptr, nullptr, true, false);
                }
            }
            if (item.outline && !outline_drawn)
            {
                immediate_context->OMSetBlendState(
                    blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
                toon.bind_outline_pass(immediate_context.Get(), true);
                scene_mesh->render(immediate_context.Get(), item.world, item.tint,
                    item_keyframe, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                    toon.skinned_outline_il_.Get(), true, false);
            }

            immediate_context->OMSetBlendState(
                blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
            if (item.double_sided || item.material_binding.layers != nullptr || item.outline)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
        }

        // アウトラインは表面描画後に背面を膨らませて重ねる。
        // 旧 Player 専用のアウトライン描画は撤去した。
        if (enable_static_meshes && static_meshes[0] &&
            enable_outline_shader && outline_per_static[0])
        {
            toon.bind_outline_pass(immediate_context.Get(), false);
            static_meshes[0]->render(immediate_context.Get(), world, material_color,
                                     toon.outline_ps(),
                                     toon.static_outline_vs_.Get(),
                                     toon.static_outline_il_.Get(),
                                     true);
            immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
        }
    }

    if (particles_this_frame)
    {
        // 半透明エフェクトは深度テストを行うが、後続を遮らないよう深度を書き込まない。
        immediate_context->OMSetBlendState(
            blend_states[(size_t)particle_blend_state].Get(), nullptr, 0xFFFFFFFF);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);
        particles.render(immediate_context.Get());
    }

    if (enable_trail)
    {
        immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::ALPHA].Get(), nullptr, 0xFFFFFFFF);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);
        test_trail.render(immediate_context.Get());
    }

    pbr.unbind_pbr_resources(immediate_context.Get());
    csm.unbind_resources(immediate_context.Get());
    toon.unbind_resources(immediate_context.Get());
    framebuffers[0]->deactivate(immediate_context.Get());

    immediate_context->OMSetDepthStencilState(depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
    immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
    immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);

    // RenderGraphの表示先に応じて、Bloom中間結果または最終合成結果を選ぶ。
    const auto output = render_graph.Output();
    ID3D11ShaderResourceView* bloom_srv = nullptr;
    if (enable_luminance_shader &&
        (enable_bloom_shader || output == ReplayEngine::Rendering::RenderOutput::Bloom))
    {
        bloom_pass.threshold = luminance_threshold;
        bloom_srv = bloom_pass.Execute(immediate_context.Get(), *bit_block_transfer,
            framebuffers[0]->shader_resource_views[0].Get());
    }

    immediate_context->OMSetRenderTargets(1, render_target_view.GetAddressOf(), nullptr);
    immediate_context->RSSetViewports(1, &camera_output_viewport);

    if (output == ReplayEngine::Rendering::RenderOutput::Final && enable_final_pass_shader)
    {
        // FinalではBloom、ビネット、FXAAをまとめてバックバッファへ合成する。
        post_process.Execute(immediate_context.Get(), *bit_block_transfer,
            framebuffers[0]->shader_resource_views[0].Get(),
            bloom_srv,
            viewport.Width, viewport.Height, enable_bloom_shader,
            enable_vignette_shader, enable_fxaa_shader);
    }
    else
    {
        ID3D11ShaderResourceView* selected =
            output == ReplayEngine::Rendering::RenderOutput::Bloom && bloom_srv
            ? bloom_srv
            : framebuffers[0]->shader_resource_views[0].Get();
        bit_block_transfer->blit(immediate_context.Get(), &selected, 0, 1);
    }

    // 次フレームで描画先に戻すフレームバッファはSRVから外しておく。
    // ドライバー任せの競合解消を避けるために明示的に解除する。
    ID3D11ShaderResourceView* null_post_srvs[2]{};
    immediate_context->PSSetShaderResources(0, _countof(null_post_srvs), null_post_srvs);
    }

    render_camera_override = nullptr;
    render_matrix_override_active = false;
    render_camera_aspect = 0.0f;
    // 最後の Camera が小さい Viewport でも、UI/ImGui は従来どおり画面全体へ描く。
    immediate_context->RSSetViewports(1, &viewport);

    ReplayEngine::UI::UILayout::Resolve(active_object_scene(),
        viewport.Width, viewport.Height);
    ReplayEngine::UI::UIRenderer::RenderStates ui_states{};
    ui_states.depth_disabled =
        depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get();
    ui_states.rasterizer =
        rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get();
    ui_states.rasterizer_scissor =
        rasterizer_states[(size_t)RASTER_STATE::SCISSOR].Get();
    ui_states.blend_none =
        blend_states[(size_t)BLEND_STATE::NONE].Get();
    ui_states.blend_alpha =
        blend_states[(size_t)BLEND_STATE::ALPHA].Get();
    ui_states.blend_add =
        blend_states[(size_t)BLEND_STATE::ADD].Get();
    ui_states.blend_multiply =
        blend_states[(size_t)BLEND_STATE::MULTIPLY].Get();
    ui_states.blend_screen =
        blend_states[(size_t)BLEND_STATE::SCREEN].Get();
    ui_states.blend_premultiplied =
        blend_states[(size_t)BLEND_STATE::PREMULTIPLIED].Get();
    ui_states.sampler =
        sampler_states[(size_t)SAMPLER_STATE::LINEAR].Get();
    ui_renderer.Render(immediate_context.Get(), active_object_scene(),
        &asset_database, &shader_library.Catalog(), ui_font_atlas,
        viewport.Width, viewport.Height, ui_states);

#ifdef USE_IMGUI
    // update()でNewFrameを通したフレームだけ描く。ロード完了フレームのように
    // 途中でeditor_modeが立った場合は次フレームからUIを出す。
    if (imgui_frame_active)
    {
        imgui_frame_active = false;
        // エディタUIはポスト処理後に描き、ゲーム画面の色補正から除外する。
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (editor_hide_requested)
        {
            editor_hide_requested = false;
            editor_mode = false;
            edit_mode_active = false;
            SetFocus(hwnd);
        }
    }
#endif

    // GPUクエリを閉じて、揃った計測結果を回収する。
    post_process.GetSettings() = original_post_settings;
    enable_bloom_shader = original_enable_bloom_shader;
    enable_vignette_shader = original_enable_vignette_shader;
    enable_ssao = original_enable_ssao;
    enable_ssr = original_enable_ssr;
    enable_taa = original_enable_taa;
    luminance_threshold = original_luminance_threshold;
    ssao_pass.radius = original_ssao_radius;
    ssao_pass.intensity = original_ssao_intensity;
    ssao_pass.enabled = original_ssao_pass_enabled;
    ssr_pass.intensity = original_ssr_intensity;
    ssr_pass.enabled = original_ssr_pass_enabled;
    taa_pass.enabled = original_taa_pass_enabled;

    ReplayEngine::Rendering::Stats().EndFrame(immediate_context.Get());

    // 次フレームの再投影用に今フレームのビュー射影を残し、
    // ジッター/ノイズ列を進めるためのフレーム番号を更新する。
    if (multiple_camera_passes)
    {
        // Camera ごとの履歴をまだ永続保持していないため、次フレームも必ず
        // 履歴無しから始める。単一 Camera の既存 TAA 経路は従来どおり。
        previous_view_projection_valid = false;
    }
    else
    {
        previous_view_projection = frame_constants.view_projection;
        previous_view_projection_valid = true;
    }

    // 基準画像を撮る間はフレーム番号も止める。
    //
    // frame_index は SSAO / SSR / TAA の時間ノイズの種になっている。
    // 進めたまま撮ると、止めているつもりでもノイズだけが毎回変わる。
    if (!golden_capture_pending()) ++frame_index;

    // Present の直前で撮ること。
    // Present のあとはバックバッファの中身が保証されない（DISCARD）。
    tick_golden_capture();

    swap_chain->Present(0, 0);
}
