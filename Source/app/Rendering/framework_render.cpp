#include "framework.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"
#include "gltf_model.h"

namespace
{
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

void framework::store_object_world(DirectX::XMFLOAT4X4& world) const
{
    DirectX::XMMATRIX C = fbx_coordinate_transform();

    if (enable_scene_game && game_scene)
    {
        // 実行中はプレイヤーが持つTransformを優先し、FBXの座標変換だけを前段に加える。
        DirectX::XMMATRIX W = DirectX::XMLoadFloat4x4(&game_scene->Gameplay().GetPlayer().GetTransform());
        DirectX::XMStoreFloat4x4(&world, C * W);
        return;
    }

    DirectX::XMMATRIX S = DirectX::XMMatrixScaling(scaling.x, scaling.y, scaling.z);
    DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYaw(
        DirectX::XMConvertToRadians(rotation.x),
        DirectX::XMConvertToRadians(rotation.y),
        DirectX::XMConvertToRadians(rotation.z));
    DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(translation.x, translation.y, translation.z);
    DirectX::XMStoreFloat4x4(&world, C * S * R * T);
}

void framework::update_frame_constants(const DirectX::XMMATRIX& view,
    const DirectX::XMMATRIX& projection, float elapsed_time)
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

    frame_constants.camera_position = enable_scene_game && game_scene
        ? DirectX::XMFLOAT4(game_scene->Gameplay().GetCamera().GetEye().x,
            game_scene->Gameplay().GetCamera().GetEye().y,
            game_scene->Gameplay().GetCamera().GetEye().z, 1.0f)
        : camera_position;
    const float width = static_cast<float>(SCREEN_WIDTH);
    const float height = static_cast<float>(SCREEN_HEIGHT);
    frame_constants.screen_size = { width, height, 1.0f / width, 1.0f / height };
    frame_constants.camera_planes = { near_plane, far_plane, tan_half_fov_y, aspect };
    frame_constants.frame_params = { static_cast<float>(frame_index), elapsed_time, 0.0f, 0.0f };

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

ID3D11PixelShader* framework::skinned_forward_shader(int shading) const
{
    // nullptrは各メッシュが持つ標準ピクセルシェーダーを使う指定になる。
    switch (shading)
    {
    case SHADING_MODEL_PBR:   return use_pbr_skin ? pbr.skinned_mesh_ps() : nullptr;
    case SHADING_MODEL_TOON:  return enable_toon_shader ? toon.skinned_mesh_ps() : nullptr;
    case SHADING_MODEL_UNLIT: return enable_unlit_shader ? skinned_mesh_unlit_ps.Get() : nullptr;
    case SHADING_MODEL_PIXELATE: return object_pixelate_ps.Get();
    default:                  return nullptr;
    }
}

ID3D11PixelShader* framework::static_forward_shader(int shading) const
{
    switch (shading)
    {
    case SHADING_MODEL_PBR:   return use_pbr_skin ? pbr.static_mesh_ps() : nullptr;
    case SHADING_MODEL_TOON:  return enable_toon_shader ? toon.static_mesh_ps() : nullptr;
    case SHADING_MODEL_UNLIT: return enable_unlit_shader ? static_mesh_unlit_ps.Get() : nullptr;
    case SHADING_MODEL_PIXELATE: return object_pixelate_ps.Get();
    default:                  return nullptr;
    }
}

unsigned int framework::deferred_shading_model(int shading) const
{
    // 無効化された描画方式がGBufferへ残らないよう、安全な方式へ置き換える。
    switch (shading)
    {
    case SHADING_MODEL_PBR:   return use_pbr_skin ? SHADING_MODEL_PBR : SHADING_MODEL_UNLIT;
    case SHADING_MODEL_TOON:  return enable_toon_shader ? SHADING_MODEL_TOON : SHADING_MODEL_UNLIT;
    case SHADING_MODEL_UNLIT: return enable_unlit_shader ? SHADING_MODEL_UNLIT : SHADING_MODEL_FBX_DEFAULT;
    case SHADING_MODEL_PIXELATE: return SHADING_MODEL_PBR;
    default:                  return SHADING_MODEL_UNLIT;
    }
}

void framework::bind_gbuffer_material(unsigned int shading, bool stage_surface,
    float pixelate_size, float pixelate_strength)
{
    material_override_constants constants{};
    constants.mat_params = stage_surface
        ? DirectX::XMFLOAT4{ 0.0f, 0.88f, 1.0f, 0.0f }
        : DirectX::XMFLOAT4{ 0.0f, 0.55f, 1.0f, 0.0f };
    constants.shading_model = shading;
    constants.texture_contrast = stage_surface ? stage_texture_contrast : 1.0f;
    constants.pixelate_size = pixelate_size;
    constants.pixelate_strength = pixelate_strength;
    immediate_context->UpdateSubresource(material_override_cb.Get(), 0, nullptr, &constants, 0, 0);
    immediate_context->PSSetConstantBuffers(9, 1, material_override_cb.GetAddressOf());
}

void framework::render(float elapsed_time)
{
    apply_pending_resize();
    if (!render_target_view || !depth_stencil_view || !framebuffers[0]) return;

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
        swap_chain->Present(0, 0);
        return;
    }

    float aspect = viewport.Width / viewport.Height;

    DirectX::XMMATRIX P;
    DirectX::XMMATRIX V;

    if (enable_scene_game && game_scene)
    {
        // ゲーム実行中のカメラはSceneGameが所有し、エディタ用カメラと混在させない。
        const Camera& cam = game_scene->Gameplay().GetCamera();
        V = DirectX::XMLoadFloat4x4(&cam.GetView());
        P = DirectX::XMLoadFloat4x4(&cam.GetProjection());
    }
    else
    {
        P = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(30.0f), aspect, 0.1f, 10000.0f);
        DirectX::XMVECTOR eye   = DirectX::XMLoadFloat4(&camera_position);
        DirectX::XMVECTOR focus = DirectX::XMVectorSet(0, 0, 0, 1);
        DirectX::XMVECTOR up    = DirectX::XMVectorSet(0, 1, 0, 0);
        V = DirectX::XMMatrixLookAtLH(eye, focus, up);
    }

    // TAAのサブピクセルジッターを射影行列へ入れる。以降の全パスがこのPを使うので、
    // G-Buffer/影/前方描画すべてが同じ標本位置になる。
    previous_taa_jitter_ndc = taa_jitter_ndc;
    taa_jitter_ndc = { 0.0f, 0.0f };
    if (enable_taa && taa_pass.Initialized() && taa_pass.enabled &&
        render_graph.DeferredDebugMode() == 0)
    {
        const DirectX::XMFLOAT2 pixel_offset = taa_pass.CurrentJitter(frame_index);
        // ピクセル単位のオフセットをNDCへ。NDCは幅2なので 2/解像度 を掛ける。
        taa_jitter_ndc = {
            pixel_offset.x * 2.0f / static_cast<float>(SCREEN_WIDTH),
            pixel_offset.y * 2.0f / static_cast<float>(SCREEN_HEIGHT) };

        // row_major・mul(v, P)の規約では clip.x += jitter.x * clip.w になるよう
        // _31/_32 へ加算する(clip.w = view z)。
        DirectX::XMFLOAT4X4 jittered;
        DirectX::XMStoreFloat4x4(&jittered, P);
        jittered._31 += taa_jitter_ndc.x;
        jittered._32 += taa_jitter_ndc.y;
        P = DirectX::XMLoadFloat4x4(&jittered);
    }
    taa_pass.SetJitter(taa_jitter_ndc);

    // 以降の全描画パスが共有するカメラと主光源の定数を一度だけ更新する。
    scene_constants scene{};
    DirectX::XMStoreFloat4x4(&scene.view_projection, V * P);
    scene.light_direction = light_direction;
    if (enable_scene_game && game_scene)
    {
        const DirectX::XMFLOAT3& eyeP = game_scene->Gameplay().GetCamera().GetEye();
        scene.camera_position = { eyeP.x, eyeP.y, eyeP.z, 1.0f };
    }
    else
    {
        scene.camera_position = camera_position;
    }
    immediate_context->UpdateSubresource(constant_buffers[0].Get(), 0, 0, &scene, 0, 0);
    immediate_context->VSSetConstantBuffers(1, 1, constant_buffers[0].GetAddressOf());
    immediate_context->PSSetConstantBuffers(1, 1, constant_buffers[0].GetAddressOf());

    // SSAO/SSR/TAAが参照するカメラ行列群をb9へ載せる。
    update_frame_constants(V, P, elapsed_time);

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
    if (enable_particles) particles.simulate(immediate_context.Get(), elapsed_time);
    if (enable_trail)     test_trail.update(elapsed_time);

    const skinned_mesh::animation::keyframe* active_keyframe = nullptr;
    if (skinned_meshes[0] && !skinned_meshes[0]->animation_clips.empty())
    {
        const int clip_count = static_cast<int>(skinned_meshes[0]->animation_clips.size());

        // Playerが明示した場合だけクリップを切り替える。-1ならエディタで選んだ値を維持する。
        if (enable_scene_game && game_scene)
        {
            int desired = game_scene->Gameplay().GetPlayer().GetAnimationClipIndex();
            if (desired >= 0)
            {
                if (desired >= clip_count) desired = clip_count - 1;
                if (desired != animation_clip_index)
                {
                    animation_clip_index = desired;
                    animation_tick = 0.0f;
                }
            }
        }

        if (animation_clip_index < 0) animation_clip_index = 0;
        if (animation_clip_index >= clip_count) animation_clip_index = clip_count - 1;

        skinned_mesh::animation& anim = skinned_meshes[0]->animation_clips.at(animation_clip_index);
        if (!anim.sequence.empty())
        {
            const float sampling_rate = anim.sampling_rate > 0.0f ? anim.sampling_rate : 60.0f;
            const float duration = static_cast<float>(anim.sequence.size()) / sampling_rate;
            if (animate_model)
            {
                animation_tick += elapsed_time * animation_speed;
            }
            if (animation_loop && duration > 0.0f)
            {
                while (animation_tick >= duration) animation_tick -= duration;
                while (animation_tick < 0.0f) animation_tick += duration;
            }
            else
            {
                if (animation_tick < 0.0f) animation_tick = 0.0f;
                if (animation_tick > duration) animation_tick = duration;
            }

            int frame = static_cast<int>(animation_tick * sampling_rate);
            if (frame >= static_cast<int>(anim.sequence.size()))
            {
                frame = static_cast<int>(anim.sequence.size()) - 1;
            }
            active_keyframe = &anim.sequence.at(frame);
        }
    }

    if (csm.constants.params.w > 0.5f &&
        (skinned_meshes[0] || (enable_static_meshes && static_meshes[0])))
    {
        // カスケードシャドウ用深度を先に作る。終了時に元のRTVとViewportを復元する。
        D3D11_VIEWPORT main_vp = viewport;
        csm.shadow_begin(immediate_context.Get());

        DirectX::XMFLOAT4X4 world;
        store_object_world(world);
        if (skinned_meshes[0])
        {
            skinned_meshes[0]->render(immediate_context.Get(), world, material_color, active_keyframe,
                nullptr,
                csm.caster_skinned_vs.Get(),
                csm.caster_skinned_il.Get(),
                false);
        }
        if (enable_static_meshes && static_meshes[0])
        {
            static_meshes[0]->render(immediate_context.Get(), world, material_color,
                nullptr,
                csm.caster_static_vs.Get(),
                csm.caster_static_il.Get(),
                false);
        }

        csm.shadow_end(immediate_context.Get(),
            render_target_view.Get(), depth_stencil_view.Get(), main_vp);
    }

    if (pbr_shadow_enabled && (skinned_meshes[0] || (enable_static_meshes && static_meshes[0])))
    {
        // PBR固有のシャドウマップはCSMと別リソースなので、必要な場合だけ生成する。
        D3D11_VIEWPORT main_vp = viewport;
        pbr.shadow_begin(immediate_context.Get());

        DirectX::XMFLOAT4X4 world;
        store_object_world(world);
        if (skinned_meshes[0])
        {
            skinned_meshes[0]->render(immediate_context.Get(), world, material_color, active_keyframe,
                nullptr,
                pbr.shadow_caster_skinned_vs.Get(),
                pbr.shadow_caster_skinned_il.Get(),
                false);
        }
        if (enable_static_meshes && static_meshes[0])
        {
            static_meshes[0]->render(immediate_context.Get(), world, material_color,
                nullptr,
                pbr.shadow_caster_static_vs.Get(),
                pbr.shadow_caster_static_il.Get(),
                false);
        }

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

    if (draw_background_image)
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
    const auto player_pixelate_layer = make_base_pixelate_layer(
        pixelate_grid_per_skinned[0], pixelate_strength_per_skinned[0]);
    const auto static_pixelate_layer = make_base_pixelate_layer(
        pixelate_grid_per_static[0], pixelate_strength_per_static[0]);
    const auto stage_pixelate_layer = make_base_pixelate_layer(
        stage_pixelate_grid, stage_pixelate_strength);
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
    const auto* player_added_pixelate = find_pixelate_layer(shader_layers_skinned[0]);
    const auto* static_added_pixelate = find_pixelate_layer(shader_layers_static[0]);
    const auto* stage_added_pixelate = find_pixelate_layer(stage_shader_layers);
    const bool player_uses_pixelate = shading_per_skinned[0] == SHADING_MODEL_PIXELATE ||
        player_added_pixelate != nullptr;
    const bool static_uses_pixelate = shading_per_static[0] == SHADING_MODEL_PIXELATE ||
        static_added_pixelate != nullptr;
    const bool stage_uses_pixelate =
        (enable_stage_shader && shading_per_stage == SHADING_MODEL_PIXELATE) ||
        stage_added_pixelate != nullptr;
    const auto& player_pixelate_settings = player_added_pixelate
        ? *player_added_pixelate : player_pixelate_layer;
    const auto& static_pixelate_settings = static_added_pixelate
        ? *static_added_pixelate : static_pixelate_layer;
    const auto& stage_pixelate_settings = stage_added_pixelate
        ? *stage_added_pixelate : stage_pixelate_layer;

    // 通常描画はDeferredへ統一する。Forward+は将来別経路として追加する。
    const bool deferred_active = deferred.initialized;
    if (deferred_active)
    {
        // Deferred経路は材質情報をGBufferへ集約し、照明パスで一括して色を決定する。
        FLOAT deferred_clear[]{ background_color.x, background_color.y, background_color.z, background_color.w };
        deferred.gbuffer_begin(immediate_context.Get(), deferred_clear);
        immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
        immediate_context->OMSetDepthStencilState(depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_ON].Get(), 0);
        immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());

        DirectX::XMFLOAT4X4 world;
        store_object_world(world);
        if (skinned_meshes[0])
        {
            bind_gbuffer_material(player_uses_pixelate ? SHADING_MODEL_PIXELATE
                : deferred_shading_model(shading_per_skinned[0]), false,
                player_pixelate_settings.parameter, player_pixelate_settings.strength);
            // 最後の引数がモーションベクター出力の指定。G-Bufferパスだけで真にする。
            skinned_meshes[0]->render(immediate_context.Get(), world, material_color,
                                      active_keyframe, skinned_mesh_gbuffer_ps.Get(),
                                      nullptr, nullptr, true, true);
        }
        if (enable_static_meshes && static_meshes[0])
        {
            bind_gbuffer_material(static_uses_pixelate ? SHADING_MODEL_PIXELATE
                : deferred_shading_model(shading_per_static[0]), false,
                static_pixelate_settings.parameter, static_pixelate_settings.strength);
            static_meshes[0]->render(immediate_context.Get(), world, material_color,
                                     static_mesh_gbuffer_ps.Get(),
                                     nullptr, nullptr, true, true);
        }

        if (enable_scene_game && game_scene && enable_stage_render)
        {
            DirectX::XMFLOAT4X4 stage_world;
            DirectX::XMStoreFloat4x4(&stage_world,
                DirectX::XMLoadFloat4x4(&game_scene->Gameplay().GetStage().GetTransform()));
            skinned_mesh* stage_model = game_scene->Gameplay().GetStage().GetModel();
            if (stage_model)
            {
                bind_gbuffer_material(stage_uses_pixelate ? SHADING_MODEL_PIXELATE
                    : deferred_shading_model(shading_per_stage), true,
                    stage_pixelate_settings.parameter, stage_pixelate_settings.strength);
                stage_model->render(immediate_context.Get(), stage_world, material_color,
                    nullptr, skinned_mesh_gbuffer_ps.Get(), nullptr, nullptr, true, true);
            }
            else if (stage_gltf_model)
            {
                bind_gbuffer_material(stage_uses_pixelate ? SHADING_MODEL_PIXELATE
                    : deferred_shading_model(shading_per_stage), true,
                    stage_pixelate_settings.parameter, stage_pixelate_settings.strength);
                stage_gltf_model->render(immediate_context.Get(), stage_world, material_color,
                    static_mesh_gbuffer_ps.Get(), true);
            }
        }

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
        deferred.lighting_pass(immediate_context.Get(), scene.view_projection,
                               background_color, render_graph.DeferredDebugMode(),
                               ambient_occlusion, screen_reflection);

        // 次フレームのSSR用に、照明直後のHDRカラーを履歴として確保する。
        if (enable_ssr && ssr_pass.Initialized() && render_graph.DeferredDebugMode() == 0)
            ssr_pass.CaptureHistory(immediate_context.Get(), deferred.lit_tex.Get());

        const bool draw_shader_layers = render_graph.DeferredDebugMode() == 0 &&
            (shader_layers_skinned[0].HasEnabledLayers() ||
                shader_layers_static[0].HasEnabledLayers() ||
                stage_shader_layers.HasEnabledLayers() ||
                shading_per_skinned[0] == SHADING_MODEL_PIXELATE ||
                shading_per_static[0] == SHADING_MODEL_PIXELATE ||
                (enable_stage_shader && shading_per_stage == SHADING_MODEL_PIXELATE));
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
            const auto skinned_pixel_shader = [this](const ReplayEngine::Rendering::ShaderLayer& layer,
                bool stage) -> ID3D11PixelShader*
            {
                using ReplayEngine::Rendering::ShaderLayerType;
                switch (layer.type)
                {
                case ShaderLayerType::Pbr:
                    return stage ? pbr.skinned_mesh_stage_ps() : pbr.skinned_mesh_ps();
                case ShaderLayerType::Toon:      return toon.skinned_mesh_ps();
                case ShaderLayerType::Pixelate: return object_pixelate_ps.Get();
                case ShaderLayerType::StylizedCharacter: return skinned_stylized_character_ps.Get();
                default:                        return skinned_mesh_unlit_ps.Get();
                }
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

            if (skinned_meshes[0])
            {
                for (const auto& layer : shader_layers_skinned[0].Layers())
                {
                    if (!layer.enabled) continue;
                    if (layer.type == ReplayEngine::Rendering::ShaderLayerType::Pixelate) continue;
                    if (layer.type == ReplayEngine::Rendering::ShaderLayerType::Outline)
                    {
                        immediate_context->OMSetBlendState(
                            blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
                        toon.bind_outline_pass(immediate_context.Get(), true);
                        skinned_meshes[0]->render(immediate_context.Get(), world, material_color,
                            active_keyframe, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                            toon.skinned_outline_il_.Get(), true);
                        immediate_context->RSSetState(
                            rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
                        continue;
                    }
                    prepare_layer(layer, character_profiles_skinned[0]);
                    const auto color = layer_color(layer);
                    skinned_meshes[0]->render(immediate_context.Get(), world, color,
                        active_keyframe, skinned_pixel_shader(layer, false));
                }
            }
            if (enable_static_meshes && static_meshes[0])
            {
                for (const auto& layer : shader_layers_static[0].Layers())
                {
                    if (!layer.enabled) continue;
                    if (layer.type == ReplayEngine::Rendering::ShaderLayerType::Pixelate) continue;
                    if (layer.type == ReplayEngine::Rendering::ShaderLayerType::Outline)
                    {
                        immediate_context->OMSetBlendState(
                            blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
                        toon.bind_outline_pass(immediate_context.Get(), false);
                        static_meshes[0]->render(immediate_context.Get(), world, material_color,
                            toon.outline_ps(), toon.static_outline_vs_.Get(),
                            toon.static_outline_il_.Get(), true);
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
            if (enable_scene_game && game_scene && enable_stage_render)
            {
                DirectX::XMFLOAT4X4 stage_world;
                DirectX::XMStoreFloat4x4(&stage_world,
                    DirectX::XMLoadFloat4x4(&game_scene->Gameplay().GetStage().GetTransform()));
                for (const auto& layer : stage_shader_layers.Layers())
                {
                    if (!layer.enabled) continue;
                    if (layer.type == ReplayEngine::Rendering::ShaderLayerType::Pixelate) continue;
                    if (layer.type == ReplayEngine::Rendering::ShaderLayerType::Outline)
                    {
                        if (skinned_mesh* stage_model = game_scene->Gameplay().GetStage().GetModel())
                        {
                            immediate_context->OMSetBlendState(
                                blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
                            toon.bind_outline_pass(immediate_context.Get(), true);
                            stage_model->render(immediate_context.Get(), stage_world, material_color,
                                nullptr, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                                toon.skinned_outline_il_.Get(), true);
                            immediate_context->RSSetState(
                                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
                        }
                        continue;
                    }
                    prepare_layer(layer, stage_character_profile);
                    const auto color = layer_color(layer);
                    if (skinned_mesh* stage_model = game_scene->Gameplay().GetStage().GetModel())
                        stage_model->render(immediate_context.Get(), stage_world, color, nullptr,
                            skinned_pixel_shader(layer, true));
                    else if (stage_gltf_model)
                        stage_gltf_model->render(immediate_context.Get(), stage_world, color,
                            static_pixel_shader(layer));
                }
            }
            immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
            immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
        }

        const bool draw_deferred_outline = render_graph.DeferredDebugMode() == 0 &&
            enable_outline_shader &&
            ((outline_per_skinned[0] &&
                !shader_layers_skinned[0].Contains(ReplayEngine::Rendering::ShaderLayerType::Outline)) ||
             (outline_per_static[0] &&
                !shader_layers_static[0].Contains(ReplayEngine::Rendering::ShaderLayerType::Outline)) ||
             (outline_per_stage &&
                !stage_shader_layers.Contains(ReplayEngine::Rendering::ShaderLayerType::Outline)));
        if (draw_deferred_outline)
        {
            // Deferred照明結果へ、同じDepthを使って追加パスを重ねる。
            immediate_context->OMSetRenderTargets(1, deferred.lit_rtv.GetAddressOf(), deferred.depth_dsv.Get());
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);
            immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
            if (skinned_meshes[0] && outline_per_skinned[0] &&
                !shader_layers_skinned[0].Contains(ReplayEngine::Rendering::ShaderLayerType::Outline))
            {
                toon.bind_outline_pass(immediate_context.Get(), true);
                skinned_meshes[0]->render(immediate_context.Get(), world, material_color,
                    active_keyframe, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                    toon.skinned_outline_il_.Get(), true);
            }
            if (enable_static_meshes && static_meshes[0] && outline_per_static[0] &&
                !shader_layers_static[0].Contains(ReplayEngine::Rendering::ShaderLayerType::Outline))
            {
                toon.bind_outline_pass(immediate_context.Get(), false);
                static_meshes[0]->render(immediate_context.Get(), world, material_color,
                    toon.outline_ps(), toon.static_outline_vs_.Get(),
                    toon.static_outline_il_.Get(), true);
            }
            if (enable_scene_game && game_scene && enable_stage_render && outline_per_stage &&
                !stage_shader_layers.Contains(ReplayEngine::Rendering::ShaderLayerType::Outline))
            {
                if (skinned_mesh* stage_model = game_scene->Gameplay().GetStage().GetModel())
                {
                    DirectX::XMFLOAT4X4 stage_world;
                    DirectX::XMStoreFloat4x4(&stage_world,
                        DirectX::XMLoadFloat4x4(&game_scene->Gameplay().GetStage().GetTransform()));
                    toon.bind_outline_pass(immediate_context.Get(), true);
                    stage_model->render(immediate_context.Get(), stage_world, material_color,
                        nullptr, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                        toon.skinned_outline_il_.Get(), true);
                }
            }
            immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
        }

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
        store_object_world(world);

        if (skinned_meshes[0])
        {
            if (shading_per_skinned[0] == SHADING_MODEL_PIXELATE)
                bind_pixelate_settings(player_pixelate_layer);
            skinned_meshes[0]->render(immediate_context.Get(), world, material_color,
                                      active_keyframe, skinned_forward_shader(shading_per_skinned[0]));
        }

        if (enable_scene_game && game_scene && enable_stage_render)
        {
            DirectX::XMFLOAT4X4 stage_world;
            DirectX::XMStoreFloat4x4(&stage_world,
                DirectX::XMLoadFloat4x4(&game_scene->Gameplay().GetStage().GetTransform()));
            skinned_mesh* stage_model = game_scene->Gameplay().GetStage().GetModel();
            if (stage_model)
            {
                if (enable_stage_shader && shading_per_stage == SHADING_MODEL_PIXELATE)
                    bind_pixelate_settings(stage_pixelate_layer);
                // ステージ用シェーダーは独立して切り替え、無効時はFBX標準PSを使う。
                ID3D11PixelShader* stage_ps = enable_stage_shader
                    ? (shading_per_stage == SHADING_MODEL_PBR && use_pbr_skin
                        ? pbr.skinned_mesh_stage_ps()
                        : skinned_forward_shader(shading_per_stage))
                    : nullptr;
                ID3D11SamplerState* stage_sampler = sampler_states[(size_t)(stage_texture_wrap
                    ? SAMPLER_STATE::ANISOTROPIC : SAMPLER_STATE::ANISOTROPIC_CLAMP)].Get();
                immediate_context->PSSetSamplers(2, 1, &stage_sampler);
                stage_model->render(immediate_context.Get(), stage_world, material_color,
                                    nullptr, stage_ps);
                immediate_context->PSSetSamplers(2, 1,
                    sampler_states[(size_t)SAMPLER_STATE::ANISOTROPIC].GetAddressOf());
            }
            else if (stage_gltf_model)
            {
                if (enable_stage_shader && shading_per_stage == SHADING_MODEL_PIXELATE)
                    bind_pixelate_settings(stage_pixelate_layer);
                ID3D11PixelShader* stage_ps = nullptr;
                if (enable_stage_shader && shading_per_stage == SHADING_MODEL_TOON)
                    stage_ps = toon.static_mesh_ps();
                else if (enable_stage_shader && shading_per_stage == SHADING_MODEL_UNLIT)
                    stage_ps = static_mesh_unlit_ps.Get();
                else if (enable_stage_shader && shading_per_stage == SHADING_MODEL_PIXELATE)
                    stage_ps = object_pixelate_ps.Get();
                stage_gltf_model->render(immediate_context.Get(), stage_world, material_color, stage_ps);
            }
        }

        if (enable_static_meshes && static_meshes[0])
        {
            if (shading_per_static[0] == SHADING_MODEL_PIXELATE)
                bind_pixelate_settings(static_pixelate_layer);
            static_meshes[0]->render(immediate_context.Get(), world, material_color,
                                     static_forward_shader(shading_per_static[0]));
        }

        if (skinned_meshes[0] && enable_outline_shader && outline_per_skinned[0])
        {
            // アウトラインは表面描画後に背面を膨らませて重ねる。
            toon.bind_outline_pass(immediate_context.Get(), true);
            skinned_meshes[0]->render(immediate_context.Get(), world, material_color,
                                      active_keyframe,
                                      toon.outline_ps(),
                                      toon.skinned_outline_vs_.Get(),
                                      toon.skinned_outline_il_.Get(),
                                      true);
            immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
        }
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
        if (enable_scene_game && game_scene && enable_stage_render &&
            enable_outline_shader && outline_per_stage)
        {
            skinned_mesh* stage_model = game_scene->Gameplay().GetStage().GetModel();
            if (stage_model)
            {
                DirectX::XMFLOAT4X4 stage_world;
                DirectX::XMStoreFloat4x4(&stage_world,
                    DirectX::XMLoadFloat4x4(&game_scene->Gameplay().GetStage().GetTransform()));
                toon.bind_outline_pass(immediate_context.Get(), true);
                stage_model->render(immediate_context.Get(), stage_world, material_color,
                    nullptr, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                    toon.skinned_outline_il_.Get(), true);
                immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
            }
        }
    }

    if (enable_particles)
    {
        // 半透明エフェクトは深度テストを行うが、後続を遮らないよう深度を書き込まない。
        immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::ADD].Get(), nullptr, 0xFFFFFFFF);
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
    immediate_context->RSSetViewports(1, &viewport);

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

    // 次フレームの再投影用に今フレームのビュー射影を残し、
    // ジッター/ノイズ列を進めるためのフレーム番号を更新する。
    previous_view_projection = frame_constants.view_projection;
    previous_view_projection_valid = true;
    ++frame_index;

    swap_chain->Present(0, 0);
}
