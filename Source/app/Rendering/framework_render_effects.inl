// RenderingLayerMask、Model/Screen Effect、透明 VFX、PostProcess 前半。
// framework::render() 本文の連続断片。外部から直接 include しない。

    // 同じ EffectChain を通して元Sceneへ alpha 合成する。通常Sceneの描画を消さないため
    // target未指定(WholeScreen)の既存Sceneには一切コストを足さない。
    if (camera_pass.screen_effect != nullptr &&
        camera_pass.screen_effect->target_mode == ScreenEffectStackComponent::RenderingLayerMask &&
        camera_pass.screen_effect->HasActiveEffects(&asset_database))
    {
        const std::uint32_t layer_width = static_cast<std::uint32_t>(
            (std::max)(1.0f, framebuffers[0]->viewport.Width));
        const std::uint32_t layer_height = static_cast<std::uint32_t>(
            (std::max)(1.0f, framebuffers[0]->viewport.Height));
        ReplayEngine::UI::UIRenderTarget* layer_source = scene_effect_targets.Acquire(
            layer_width, layer_height, DXGI_FORMAT_R16G16B16A16_FLOAT);
        if (layer_source != nullptr)
        {
            ID3D11ShaderResourceView* null_srvs[8]{};
            immediate_context->PSSetShaderResources(0, 8, null_srvs);
            immediate_context->OMSetRenderTargets(1, layer_source->rtv.GetAddressOf(),
                framebuffers[0]->depth_stencil_view.Get());
            const FLOAT transparent[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            immediate_context->ClearRenderTargetView(layer_source->rtv.Get(), transparent);
            immediate_context->RSSetViewports(1, &camera_output_viewport);
            immediate_context->OMSetBlendState(
                blend_states[(size_t)BLEND_STATE::ALPHA].Get(), nullptr, 0xFFFFFFFF);
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);
            const int target_layer = (std::max)(0, (std::min)(31,
                camera_pass.screen_effect->target_rendering_layer));
            const std::uint32_t target_layer_mask =
                1u << static_cast<unsigned int>(target_layer);
            draw_object_scene_meshes(nullptr, false, false,
                ReplayEngine::Core::ObjectID::Invalid(), false, target_layer_mask);
            immediate_context->OMSetRenderTargets(0, nullptr, nullptr);

            ReplayEngine::UI::UIRenderTarget* layer_effected = apply_scene_effect_chain(
                layer_source->srv.Get(),
                camera_pass.screen_effect->EffectiveEffects(&asset_database),
                layer_width, layer_height, DXGI_FORMAT_R16G16B16A16_FLOAT,
                shader_composer_time, static_cast<std::uint64_t>(
                    reinterpret_cast<std::uintptr_t>(camera_pass.screen_effect)),
                &camera_pass.screen_effect->effect_region);

            immediate_context->OMSetRenderTargets(1,
                framebuffers[0]->render_target_view.GetAddressOf(),
                framebuffers[0]->depth_stencil_view.Get());
            D3D11_VIEWPORT full_layer_viewport = framebuffers[0]->viewport;
            immediate_context->RSSetViewports(1, &full_layer_viewport);
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
            immediate_context->OMSetBlendState(
                blend_states[(size_t)BLEND_STATE::ALPHA].Get(), nullptr, 0xFFFFFFFF);
            ID3D11ShaderResourceView* selected_layer = layer_effected != nullptr
                ? layer_effected->srv.Get() : layer_source->srv.Get();
            bit_block_transfer->blit(immediate_context.Get(), &selected_layer, 0, 1);
            ID3D11ShaderResourceView* null_layer = nullptr;
            immediate_context->PSSetShaderResources(0, 1, &null_layer);
            immediate_context->RSSetViewports(1, &camera_output_viewport);
        }
    }

    // Model Effect は通常の scene 描画後、transparent VFX より前へ合成する。
    // これにより effect 無しモデルは従来経路のまま、line/particle は後から正しく重なる。
    draw_model_effect_stacks(camera_output_viewport);

    // Component 版の 3D ライン / 軌跡は、透明 3D エフェクトと同じく
    // 深度を読むが書かない Forward 合成へ置く。
    draw_line_strokes();

    if (particles_this_frame)
    {
        REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "Particles");
        // 半透明エフェクトは深度テストを行うが、後続を遮らないよう深度を書き込まない。
        immediate_context->OMSetBlendState(
            blend_states[(size_t)particle_blend_state].Get(), nullptr, 0xFFFFFFFF);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
        particles.render(immediate_context.Get());
    }

    if (enable_trail)
    {
        REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "Trails");
        immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::ALPHA].Get(), nullptr, 0xFFFFFFFF);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
        test_trail.render(immediate_context.Get());
    }

    pbr.unbind_pbr_resources(immediate_context.Get());
    csm.unbind_resources(immediate_context.Get());
    toon.unbind_resources(immediate_context.Get());
    framebuffers[0]->deactivate(immediate_context.Get());

    immediate_context->OMSetDepthStencilState(depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);

    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
    immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
    immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);

    // RenderGraphの表示先に応じて、Bloom中間結果または最終合成結果を選ぶ。
    const auto output = render_graph.Output();
    ID3D11ShaderResourceView* bloom_srv = nullptr;
    if (enable_luminance_shader &&
        (enable_bloom_shader || output == ReplayEngine::Rendering::RenderOutput::Bloom))
    {
        REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "Bloom");
        bloom_pass.threshold = luminance_threshold;
        bloom_srv = bloom_pass.Execute(immediate_context.Get(), *bit_block_transfer,
            framebuffers[0]->shader_resource_views[0].Get());
    }

    const ScreenEffectStackComponent* screen_effect = camera_pass.screen_effect;
    const bool final_output =
        output == ReplayEngine::Rendering::RenderOutput::Final && enable_final_pass_shader;
    const bool has_screen_effect = final_output && screen_effect != nullptr &&
        screen_effect->target_mode == ScreenEffectStackComponent::WholeScreen &&
        screen_effect->HasActiveEffects(&asset_database);
    const bool before_post_effect = has_screen_effect &&
        screen_effect->apply_stage == ScreenEffectStackComponent::BeforePostProcess;
    const bool after_post_effect = has_screen_effect &&
        screen_effect->apply_stage == ScreenEffectStackComponent::AfterPostProcess;

    ID3D11ShaderResourceView* post_source =
        framebuffers[0]->shader_resource_views[0].Get();
    ReplayEngine::UI::UIRenderTarget* before_result = nullptr;
    const std::uint32_t effect_width = static_cast<std::uint32_t>(
        (std::max)(1.0f, camera_output_viewport.Width));
    const std::uint32_t effect_height = static_cast<std::uint32_t>(
        (std::max)(1.0f, camera_output_viewport.Height));

    if (before_post_effect)
    {
        // Effect は Camera viewport の画素だけへ掛ける。ただし既存 PostProcess は
        // full framebuffer SRV を入力にする設計なので、split viewport では
        // 1) Camera 矩形を crop -> 2) Effect -> 3) full-size HDR copy へ差し戻す。
        // これで Bloom/FXAA/Vignette を含む既存 PostProcess の入力座標系を変えない。
        const std::uint32_t framebuffer_width = static_cast<std::uint32_t>(
            (std::max)(1.0f, framebuffers[0]->viewport.Width));
        const std::uint32_t framebuffer_height = static_cast<std::uint32_t>(
            (std::max)(1.0f, framebuffers[0]->viewport.Height));
        const UINT left = static_cast<UINT>((std::max)(0.0f,
            std::floor(camera_output_viewport.TopLeftX)));
        const UINT top = static_cast<UINT>((std::max)(0.0f,
            std::floor(camera_output_viewport.TopLeftY)));
        const bool full_viewport = left == 0u && top == 0u &&
            effect_width == framebuffer_width && effect_height == framebuffer_height;

        ID3D11ShaderResourceView* effect_source = post_source;
        ReplayEngine::UI::UIRenderTarget* camera_source = nullptr;
        bool source_ready = full_viewport;
        if (!full_viewport)
        {
            camera_source = scene_effect_targets.Acquire(effect_width, effect_height,
                DXGI_FORMAT_R16G16B16A16_FLOAT);
            if (camera_source != nullptr)
            {
                Microsoft::WRL::ComPtr<ID3D11Resource> source_resource;
                framebuffers[0]->shader_resource_views[0]->GetResource(
                    source_resource.GetAddressOf());
                if (source_resource != nullptr)
                {
                    D3D11_BOX box{};
                    box.left = left;
                    box.top = top;
                    box.front = 0;
                    box.right = (std::min)(framebuffer_width, left + effect_width);
                    box.bottom = (std::min)(framebuffer_height, top + effect_height);
                    box.back = 1;
                    if (box.right > box.left && box.bottom > box.top &&
                        box.right - box.left == effect_width &&
                        box.bottom - box.top == effect_height)
                    {
                        immediate_context->CopySubresourceRegion(
                            camera_source->texture.Get(), 0, 0, 0, 0,
                            source_resource.Get(), 0, &box);
                        effect_source = camera_source->srv.Get();
                        source_ready = true;
                    }
                }
            }
        }

        if (source_ready)
        {
            before_result = apply_scene_effect_chain(effect_source, screen_effect->EffectiveEffects(&asset_database),
                effect_width, effect_height, DXGI_FORMAT_R16G16B16A16_FLOAT,
                shader_composer_time, static_cast<std::uint64_t>(
                    reinterpret_cast<std::uintptr_t>(screen_effect)),
                &screen_effect->effect_region);
            if (before_result != nullptr)
            {
                if (full_viewport)
                {
                    post_source = before_result->srv.Get();
                }
                else
                {
                    ReplayEngine::UI::UIRenderTarget* merged_source =
                        scene_effect_targets.Acquire(framebuffer_width, framebuffer_height,
                            DXGI_FORMAT_R16G16B16A16_FLOAT);
                    Microsoft::WRL::ComPtr<ID3D11Resource> original_resource;
                    framebuffers[0]->shader_resource_views[0]->GetResource(
                        original_resource.GetAddressOf());
                    if (merged_source != nullptr && original_resource != nullptr)
                    {
                        immediate_context->CopyResource(merged_source->texture.Get(),
                            original_resource.Get());
                        immediate_context->CopySubresourceRegion(
                            merged_source->texture.Get(), 0, left, top, 0,
                            before_result->texture.Get(), 0, nullptr);
                        post_source = merged_source->srv.Get();
                    }
                    // merge RT を確保できなければ、元の full framebuffer を使い
                    // Effect だけ安全に諦める。座標系を壊した入力は PostProcess へ渡さない。
                }
            }
        }
    }

    if (after_post_effect)
    {
        // Tone map 済みの結果を一度 LDR RT へ受け、その Camera の Effect を掛けてから
        // backbuffer へ戻す。UI はこの camera loop の後なので巻き込まれない。
        ReplayEngine::UI::UIRenderTarget* post_target =
            scene_effect_targets.Acquire(effect_width, effect_height,
                DXGI_FORMAT_R8G8B8A8_UNORM);
        if (post_target != nullptr)
        {
            ID3D11ShaderResourceView* null_srvs[2]{};
            immediate_context->PSSetShaderResources(0, 2, null_srvs);
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::ShaderResource, false);
            immediate_context->OMSetRenderTargets(1, post_target->rtv.GetAddressOf(), nullptr);
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::RenderTarget, false);
            D3D11_VIEWPORT post_viewport{};
            post_viewport.Width = static_cast<float>(effect_width);
            post_viewport.Height = static_cast<float>(effect_height);
            post_viewport.MinDepth = 0.0f;
            post_viewport.MaxDepth = 1.0f;
            immediate_context->RSSetViewports(1, &post_viewport);
            const FLOAT clear_post[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            immediate_context->ClearRenderTargetView(post_target->rtv.Get(), clear_post);

            {
                REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "PostProcess");
post_process.Execute(immediate_context.Get(), *bit_block_transfer,
                post_source, bloom_srv, viewport.Width, viewport.Height,
                enable_bloom_shader, enable_vignette_shader, enable_fxaa_shader);
            }

            ID3D11ShaderResourceView* null_post_source = nullptr;
            immediate_context->PSSetShaderResources(0, 1, &null_post_source);
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::ShaderResource, false);
            ReplayEngine::UI::UIRenderTarget* effected = apply_scene_effect_chain(
                post_target->srv.Get(), screen_effect->EffectiveEffects(&asset_database),
                effect_width, effect_height, DXGI_FORMAT_R8G8B8A8_UNORM,
                shader_composer_time, static_cast<std::uint64_t>(
                    reinterpret_cast<std::uintptr_t>(screen_effect)),
                &screen_effect->effect_region);

            immediate_context->OMSetRenderTargets(1, render_target_view.GetAddressOf(), nullptr);

            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::RenderTarget, false);
            immediate_context->RSSetViewports(1, &camera_output_viewport);
            ID3D11ShaderResourceView* selected =
                effected != nullptr ? effected->srv.Get() : post_target->srv.Get();
            bit_block_transfer->blit(immediate_context.Get(), &selected, 0, 1);
        }
        else
        {
            // RT 枯渇時も描画を失わない。Effect だけ諦めて既存 PostProcess を通す。
            immediate_context->OMSetRenderTargets(1, render_target_view.GetAddressOf(), nullptr);
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::RenderTarget, false);
            immediate_context->RSSetViewports(1, &camera_output_viewport);
            {
                REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "PostProcess");
post_process.Execute(immediate_context.Get(), *bit_block_transfer,
                post_source, bloom_srv, viewport.Width, viewport.Height,
                enable_bloom_shader, enable_vignette_shader, enable_fxaa_shader);
            }
        }
    }
    else
    {
        immediate_context->OMSetRenderTargets(1, render_target_view.GetAddressOf(), nullptr);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::RenderTarget, false);
        immediate_context->RSSetViewports(1, &camera_output_viewport);

        if (final_output)
        {
            // Effect が空/無効ならこの従来分岐をそのまま通る。
            {
                REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "PostProcess");
