// render() の backbuffer 初期化、Camera pass 構築、frame constants 更新。
// framework::render() 本文の連続断片。外部から直接 include しない。

    apply_pending_resize();
    if (!render_target_view || !depth_stencil_view || !framebuffers[0])
    {
        ReplayEngine::Rendering::Stats().EndFrame(immediate_context.Get());
        return;
    }

    ReplayEngine::Rendering::Stats().BeginPhase(
        ReplayEngine::Rendering::RenderStats::Phase::Scene3D, immediate_context.Get());

    // 前フレームのRTV/SRV参照を先に外し、同じリソースを入出力へ同時設定する競合を防ぐ。
    ID3D11RenderTargetView* null_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    immediate_context->OMSetRenderTargets(_countof(null_rtvs), null_rtvs, 0);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::RenderTarget, false);
    ID3D11ShaderResourceView* null_srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    immediate_context->VSSetShaderResources(0, _countof(null_srvs), null_srvs);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::ShaderResource, false);
    immediate_context->PSSetShaderResources(0, _countof(null_srvs), null_srvs);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::ShaderResource, false);

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
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::RenderTarget, false);
        immediate_context->OMSetBlendState(
            blend_states[(size_t)BLEND_STATE::ALPHA].Get(), nullptr, 0xFFFFFFFF);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
        immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
        scene_manager.Render({ immediate_context.Get(), viewport.Width, viewport.Height });
        // 早期returnでも Phase / Frame Query を閉じる。開いたままにすると次フレームで
        // 二重Beginになりクエリが壊れる。
        ReplayEngine::Rendering::Stats().EndPhase(
            ReplayEngine::Rendering::RenderStats::Phase::Scene3D, immediate_context.Get());
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
        const ScreenEffectStackComponent* screen_effect = nullptr;
        bool matrix_override = false;
        DirectX::XMFLOAT4X4 view{};
        DirectX::XMFLOAT4X4 projection{};
        DirectX::XMFLOAT3 eye{ 0.0f, 0.0f, 0.0f };
        D3D11_VIEWPORT output{};
    };

    const auto screen_effect_for_object = [](const ReplayEngine::Core::GameObject* object)
        -> const ScreenEffectStackComponent*
    {
        const ScreenEffectStackComponent* effect = object != nullptr
            ? object->GetComponent<ScreenEffectStackComponent>() : nullptr;
        return effect != nullptr && effect->ActiveInHierarchy() && effect->enabled
            ? effect : nullptr;
    };

    const auto active_camera_selection =
        ReplayEngine::Components::ResolveActiveCameraSelection(active_object_scene());
    const ScreenEffectStackComponent* active_camera_screen_effect =
        active_camera_selection.Valid()
            ? screen_effect_for_object(active_camera_selection.object) : nullptr;

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
        main_pass.screen_effect = active_camera_screen_effect;
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
                pass.screen_effect = active_camera_screen_effect;
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
            pass.screen_effect = screen_effect_for_object(object);
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
            legacy_pass.screen_effect = active_camera_screen_effect;
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

    begin_scene_effect_frame();

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
        // 【影を出すかどうかの正本】
        //   Light Component の Cast Shadows と全体設定を、ここで 1 回だけ
        //   掛け合わせる。以降のパスもシェーダーも csm_params.w だけを見る。
        //   個別の場所で「影を出す条件」を再発明しないこと。
        csm.constants.params.w =
            (enable_dynamic_shadows && csm_enabled_setting && directional_shadow_enabled)
            ? 1.0f : 0.0f;

        DirectX::XMFLOAT4X4 V4, P4;
        DirectX::XMStoreFloat4x4(&V4, V);
        DirectX::XMStoreFloat4x4(&P4, P);
        csm.update_cascades(light_direction, V4, P4, 30.0f);
        csm.update_constants(immediate_context.Get());
    }
    toon.update_constants(immediate_context.Get());
    lights.update_constants(immediate_context.Get());

    // 描画より先に時間依存のGPUパーティクルと軌跡を進める。
