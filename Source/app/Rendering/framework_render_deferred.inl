// Deferred Lighting、Shader Layer/Material 追加パス。
// framework::render() 本文の連続断片。外部から直接 include しない。

        // Object/primitive draws leave their own culling and depth states behind.
        // The classic deferred pass renders a fullscreen strip, so inheriting a
        // mesh cull state can reject the entire lighting pass and leave only clear.
        immediate_context->OMSetBlendState(
            blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);

        {
        REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "DeferredLighting");
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
                    { point.color.x, point.color.y, point.color.z }, point.color.w,
                    // 影スロットは PS 版と同じものを渡す。落とすと影の有無が変わる。
                    static_cast<int>(point.shadow.x), point.shadow.y);
            }
            for (int i = 0; i < lights.data.light_counts.y && i < lights_manager::SPOT_LIGHT_MAX; ++i)
            {
                const auto& spot = lights.data.spot_lights[i];
                tiled_deferred.AddSpotLight(
                    { spot.position.x, spot.position.y, spot.position.z },
                    spot.position.w,
                    { spot.direction.x, spot.direction.y, spot.direction.z },
                    spot.direction.w, spot.color.w,
                    { spot.color.x, spot.color.y, spot.color.z }, spot.params.x,
                    static_cast<int>(spot.params.y), spot.params.z);
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
            local_shadows.BindComputeResources(immediate_context.Get());

            ID3D11ShaderResourceView* gbuffer_views[4]{
                deferred.gbuffer_srv[0].Get(), deferred.gbuffer_srv[1].Get(),
                deferred.gbuffer_srv[2].Get(), deferred.gbuffer_srv[3].Get() };
            tiled_deferred.Dispatch(immediate_context.Get(), deferred.lit_uav.Get(),
                gbuffer_views, deferred.depth_srv.Get(),
                ambient_occlusion, screen_reflection);

            pbr.unbind_compute_resources(immediate_context.Get());
            csm.unbind_compute_resources(immediate_context.Get());
            local_shadows.UnbindComputeResources(immediate_context.Get());
        }
        else
        {
            deferred.lighting_pass(immediate_context.Get(), scene.view_projection,
                                   background_color, render_graph.DeferredDebugMode(),
                                   ambient_occlusion, screen_reflection);
        }
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
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::RenderTarget, false);
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);

            const auto prepare_layer = [this](const ReplayEngine::Rendering::ShaderLayer& layer,
                const ReplayEngine::Rendering::CharacterMaterialProfile& profile)
            {
                BLEND_STATE blend = BLEND_STATE::ALPHA;
                if (layer.blend == ReplayEngine::Rendering::ShaderLayerBlend::Additive)
                    blend = BLEND_STATE::ADD;
                else if (layer.blend == ReplayEngine::Rendering::ShaderLayerBlend::Multiply)
                    blend = BLEND_STATE::MULTIPLY;
                immediate_context->OMSetBlendState(blend_states[(size_t)blend].Get(), nullptr, 0xFFFFFFFF);
                ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
                const bool wireframe = layer.type == ReplayEngine::Rendering::ShaderLayerType::Wireframe;
                immediate_context->RSSetState(rasterizer_states[(size_t)(wireframe
                    ? RASTER_STATE::WIREFRAME_CULL_NONE : RASTER_STATE::CULL_NONE)].Get());
                ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
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
                        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
                        toon.bind_outline_pass(immediate_context.Get(), false);
                        static_meshes[0]->render(immediate_context.Get(), world, material_color,
                            toon.outline_ps(), toon.static_outline_vs_.Get(),
                            toon.static_outline_il_.Get(), true);
                        toon.outline = saved_outline;
                        toon.update_constants(immediate_context.Get());
                        immediate_context->RSSetState(
                            rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
                        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
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
                            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
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
                                            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
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
                    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
                    toon.bind_outline_pass(immediate_context.Get(), true);
                    mesh->render(immediate_context.Get(), item.world, item.tint,
                        keyframe, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                        toon.skinned_outline_il_.Get(), true, false);
                }
            }

            immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());

            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
            immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
        }

        // 輪郭線の追加パス。旧 Player 用の分岐は無い。
