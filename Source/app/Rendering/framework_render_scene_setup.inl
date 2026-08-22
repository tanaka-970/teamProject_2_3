// Particle/Shadow/背景/Background Effect、Depth Prepass と GBuffer 準備。
// framework::render() 本文の連続断片。外部から直接 include しない。

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
    if (camera_pass_index == 0)
        update_line_trails(elapsed_time);

    // アニメーション付きモデルは例外なく
    // SkinnedMeshRendererComponent + AnimatorComponent が提出し、
    // draw_object_scene_meshes() / RenderItem 経由でのみ描かれる。
    // Shadow / CSM / GBuffer / Forward / Outline のどのパスにも
    // Player 専用の分岐は残っていない。

    // 影の診断値はフレームごとに作り直す。
    if (camera_pass_index == 0) shadow_stats.Reset();
    shadow_stats.directional_light_present = directional_light_present;
    shadow_stats.directional_preview_light = directional_light_is_preview;

    if (csm.constants.params.w > 0.5f)
    {
        REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "CSM Shadow");
        // カスケードシャドウ用深度を先に作る。終了時に元のRTVとViewportを復元する。
        D3D11_VIEWPORT main_vp = viewport;
        csm.shadow_begin(immediate_context.Get());

        // Scene の全キャスターを提出する。姿勢はこのフレームのものが入る。
        draw_shadow_caster_meshes(
            csm.caster_static_vs.Get(), csm.caster_static_il.Get(),
            csm.caster_skinned_vs.Get(), csm.caster_skinned_il.Get(),
            csm.shadow_volume_center, csm.shadow_volume_radius,
            csm.caster_extrusion);

        // エディタのデバッグ用静的メッシュ。互換のために残しているだけ。
        if (enable_static_meshes && static_meshes[0])
        {
            DirectX::XMFLOAT4X4 world;
            store_debug_mesh_world(world);
            static_meshes[0]->render(immediate_context.Get(), world, material_color,
                nullptr,
                csm.caster_static_vs.Get(),
                csm.caster_static_il.Get(),
                false);
            ++shadow_stats.shadow_draw_calls;
        }

        csm.shadow_end(immediate_context.Get(),
            render_target_view.Get(), depth_stencil_view.Get(), main_vp);
        shadow_stats.directional_shadow_rendered = true;
    }

    if (pbr_shadow_enabled && enable_dynamic_shadows)
    {
        REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "PBR Shadow");
        // PBR固有のシャドウマップはCSMと別リソースなので、必要な場合だけ生成する。
        // CSM が無効な構成でも影が消えないよう、こちらにも Scene 全体を流す。
        D3D11_VIEWPORT main_vp = viewport;
        pbr.shadow_begin(immediate_context.Get());

        draw_shadow_caster_meshes(
            pbr.shadow_caster_static_vs.Get(), pbr.shadow_caster_static_il.Get(),
            pbr.shadow_caster_skinned_vs.Get(), pbr.shadow_caster_skinned_il.Get(),
            csm.shadow_volume_center, csm.shadow_volume_radius,
            csm.caster_extrusion);

        if (enable_static_meshes && static_meshes[0])
        {
            DirectX::XMFLOAT4X4 world;
            store_debug_mesh_world(world);
            static_meshes[0]->render(immediate_context.Get(), world, material_color,
                nullptr,
                pbr.shadow_caster_static_vs.Get(),
                pbr.shadow_caster_static_il.Get(),
                false);
        }

        pbr.shadow_end(immediate_context.Get(),
            render_target_view.Get(), depth_stencil_view.Get(), main_vp);
    }

    // Point / Spot の影マップ。カメラに依存しないので 1 回だけ作る。
    if (camera_pass_index == 0 && enable_dynamic_shadows &&
        local_shadows.enabled && local_shadows.AtlasReady() &&
        !local_shadow_requests.empty())
    {
        REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "Local Shadow");
        const D3D11_VIEWPORT main_vp = viewport;
        local_shadows.UploadSlices(immediate_context.Get());

        for (const local_shadow_request& request : local_shadow_requests)
        {
            local_shadows.BeginLight(immediate_context.Get(),
                request.base_slice, request.slice_count);
            // 影ボリュームはライトの到達距離。範囲外の物体は影マップへ書けない。
            draw_shadow_caster_meshes(
                csm.caster_static_vs.Get(), csm.caster_static_il.Get(),
                csm.caster_skinned_vs.Get(), csm.caster_skinned_il.Get(),
                request.position, request.range, 0.0f);

            if (request.point) ++shadow_stats.point_shadow_lights;
            else ++shadow_stats.spot_shadow_lights;
        }

        local_shadows.End(immediate_context.Get(),
            render_target_view.Get(), depth_stencil_view.Get(), main_vp);
    }

    // 3Dシーンは中間フレームバッファへ描き、最後にポスト処理してバックバッファへ出力する。
    framebuffers[0]->clear(immediate_context.Get(),
        background_color.x, background_color.y, background_color.z, background_color.w);
    framebuffers[0]->activate(immediate_context.Get());

    immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::ALPHA].Get(), nullptr, 0xFFFFFFFF);

    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
    immediate_context->OMSetDepthStencilState(depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_ON].Get(), 0);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
    immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);

    // 背景画像は任意アセット。読み込めていない場合は sprite_batches[0] が空になる。
    if (draw_background_image && sprite_batches[0])
    {
        // 背景画像は深度を書かず、3Dオブジェクトより先に中間バッファへ敷く。
        sprite_batches[0]->begin(immediate_context.Get());
        sprite_batches[0]->render(immediate_context.Get(), 0, 0,
            static_cast<float>(client_width), static_cast<float>(client_height), 1, 1, 1, 1, 0);
        sprite_batches[0]->end(immediate_context.Get());
    }

    // BackgroundOnly は geometry を描く前の framebuffer にだけ EffectChain を掛ける。
    // 既存 WholeScreen (target_mode=0) はこの分岐へ入らず従来経路を維持する。
    if (camera_pass.screen_effect != nullptr &&
        camera_pass.screen_effect->target_mode == ScreenEffectStackComponent::BackgroundOnly &&
        camera_pass.screen_effect->HasActiveEffects(&asset_database))
    {
        const std::uint32_t background_width = static_cast<std::uint32_t>(
            (std::max)(1.0f, framebuffers[0]->viewport.Width));
        const std::uint32_t background_height = static_cast<std::uint32_t>(
            (std::max)(1.0f, framebuffers[0]->viewport.Height));
        // framebuffer::deactivate() は activate() 時の backbuffer cache を解放するため、
        // mid-frame では使わない。OM だけ外して最後の通常 deactivate() を温存する。
        immediate_context->OMSetRenderTargets(0, nullptr, nullptr);
        ReplayEngine::UI::UIRenderTarget* effected_background = apply_scene_effect_chain(
            framebuffers[0]->shader_resource_views[0].Get(),
            camera_pass.screen_effect->EffectiveEffects(&asset_database),
            background_width, background_height, DXGI_FORMAT_R16G16B16A16_FLOAT,
            shader_composer_time, static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(camera_pass.screen_effect)),
            &camera_pass.screen_effect->effect_region);
        immediate_context->OMSetRenderTargets(1,
            framebuffers[0]->render_target_view.GetAddressOf(),
            framebuffers[0]->depth_stencil_view.Get());
        immediate_context->RSSetViewports(1, &camera_output_viewport);
        if (effected_background != nullptr)
        {
            immediate_context->OMSetBlendState(
                blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
            D3D11_VIEWPORT background_viewport = framebuffers[0]->viewport;
            immediate_context->RSSetViewports(1, &background_viewport);
            ID3D11ShaderResourceView* source = effected_background->srv.Get();
            bit_block_transfer->blit(immediate_context.Get(), &source, 0, 1);
            ID3D11ShaderResourceView* null_source = nullptr;
            immediate_context->PSSetShaderResources(0, 1, &null_source);
            immediate_context->RSSetViewports(1, &camera_output_viewport);
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_ON].Get(), 0);
        }
    }

    immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);

    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);

    pbr.bind_pbr_resources(immediate_context.Get());
    csm.bind_resources(immediate_context.Get());
    // Point / Spot の影マップ。前方描画の Toon / PBR もこれを読む。
    local_shadows.BindResources(immediate_context.Get());
    toon.bind_resources(immediate_context.Get());
    immediate_context->PSSetShaderResources(1, 1, dummy_normal_srv.GetAddressOf());
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::ShaderResource, false);

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
            REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "DepthPrepass");
            deferred.depth_prepass_begin(immediate_context.Get());
            immediate_context->OMSetBlendState(
                blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_ON].Get(), 0);
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
            // GBuffer と同じカリングにすること。ここだけ両面で深度を書くと
            // 本描画の EQUAL 比較と食い違い、面が消える。
            immediate_context->RSSetState(
                rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);

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
        {
        REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "GBuffer");
        FLOAT deferred_clear[]{ background_color.x, background_color.y, background_color.z, background_color.w };
        deferred.gbuffer_begin(immediate_context.Get(), deferred_clear, !use_depth_prepass);
        immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
        // プリパス済みなら EQUAL 比較で最前面だけを通す。
        immediate_context->OMSetDepthStencilState(use_depth_prepass
            ? deferred.depth_equal_state.Get()
            : depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_ON].Get(), 0);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
        // 既定は背面カリング。両面が要るマテリアルは item.double_sided を見て
        // 描画直前に CULL_NONE へ切り替え、終わったらここへ戻る。
        // 既定を CULL_NONE にすると、最初の double_sided が現れるまでの
        // メッシュだけ両面で描かれ、描画順で結果が変わってしまう。
        immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);

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
        }

        // 照明の前にSSAOを解く。G-Bufferの深度と法線だけで完結するパス。
        ID3D11ShaderResourceView* ambient_occlusion = nullptr;
        if (enable_ssao && ssao_pass.Initialized())
        {
            REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "SSAO");
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
            REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "SSR");
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
