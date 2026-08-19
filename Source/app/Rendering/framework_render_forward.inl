// Outline、TAA、Forward fallback、Landscape/Skinned/Material Layer。
// framework::render() 本文の連続断片。外部から直接 include しない。

        // GameObject の輪郭線は Renderer Component の outline プロパティが決める。
        const bool draw_deferred_outline = render_graph.DeferredDebugMode() == 0 &&
            enable_outline_shader &&
            outline_per_static[0] &&
            !shader_layers_static[0].Contains(ReplayEngine::Rendering::ShaderLayerType::Outline);
        if (draw_deferred_outline)
        {
            // Deferred照明結果へ、同じDepthを使って追加パスを重ねる。
            immediate_context->OMSetRenderTargets(1, deferred.lit_rtv.GetAddressOf(), deferred.depth_dsv.Get());
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::RenderTarget, false);
            immediate_context->OMSetDepthStencilState(
                depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get(), 0);
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
            immediate_context->OMSetBlendState(blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
            if (enable_static_meshes && static_meshes[0] && outline_per_static[0] &&
                !shader_layers_static[0].Contains(ReplayEngine::Rendering::ShaderLayerType::Outline))
            {
                toon.bind_outline_pass(immediate_context.Get(), false);
                static_meshes[0]->render(immediate_context.Get(), world, material_color,
                    toon.outline_ps(), toon.static_outline_vs_.Get(),
                    toon.static_outline_il_.Get(), true);
            }
            immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
        }

        // シェーダーレイヤー(追加パス)はブレンドをALPHA/ADD/MULTIPLYへ変えるため、
        // 後続のフルスクリーンパスへ持ち越さないよう必ず不透明へ戻す。
        // ここを忘れると、TAAが加算合成になって履歴が累積し、
        // シーンビューが固まったように見える。
        immediate_context->OMSetBlendState(
            blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
        immediate_context->OMSetDepthStencilState(
            depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
        immediate_context->RSSetState(
            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);

        // TAAはトーンマップ前のHDRで解く。明部のエイリアスも正しく平均化され、
        // かつジッター済みの複数フレームが実質的なスーパーサンプリングになる。
        ID3D11ShaderResourceView* lit_srv = deferred.lit_srv.Get();
        if (enable_taa && taa_pass.Initialized() && render_graph.DeferredDebugMode() == 0)
        {
            REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "TAA");
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

        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::RenderTarget, false);
        immediate_context->RSSetViewports(1, &framebuffers[0]->viewport);
        immediate_context->OMSetDepthStencilState(depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get(), 0);
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::DepthStencil, false);
        immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
        // 後段のエフェクトを共通化するため、照明結果を通常の中間バッファへ戻す。
        bit_block_transfer->blit(immediate_context.Get(), &lit_srv, 0, 1);
    }
    else
    {
        // Forward経路はオブジェクトごとにシェーダーを選び、その場で最終色まで計算する。
        immediate_context->RSSetState(rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);

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

            // Deferredを使えない環境でも、同じGLB Assetを既存ローダーから描く。
            // 通常のDeferred経路は draw_object_scene_meshes() 側で処理する。
            if (gltf_model* gltf = resolve_object_gltf(item.mesh_asset))
            {
                if (item.double_sided)
                    immediate_context->RSSetState(
                        rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
                    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
                gltf->render(immediate_context.Get(), item.world, item.legacy_tint,
                    static_forward_shader(item.shading_model), false, false);
                if (item.double_sided)
                    immediate_context->RSSetState(
                        rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
                    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
                continue;
            }

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
                ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);

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
                        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
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
                        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
                        immediate_context->RSSetState(
                            rasterizer_states[(size_t)RASTER_STATE::CULL_NONE].Get());
                        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);

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
                                        ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
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
                    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
                    immediate_context->RSSetState(rasterizer_states[(size_t)(
                        layer.type == ReplayEngine::Rendering::ShaderLayerType::Wireframe
                            ? RASTER_STATE::WIREFRAME_CULL_NONE : RASTER_STATE::CULL_NONE)].Get());
                    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);

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
                ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
                toon.bind_outline_pass(immediate_context.Get(), true);
                scene_mesh->render(immediate_context.Get(), item.world, item.tint,
                    item_keyframe, toon.outline_ps(), toon.skinned_outline_vs_.Get(),
                    toon.skinned_outline_il_.Get(), true, false);
            }

            immediate_context->OMSetBlendState(
                blend_states[(size_t)BLEND_STATE::NONE].Get(), nullptr, 0xFFFFFFFF);

            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Blend, false);
            if (item.double_sided || item.material_binding.layers != nullptr || item.outline)
                immediate_context->RSSetState(
                    rasterizer_states[(size_t)RASTER_STATE::SOLID].Get());
                ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
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
            ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::Rasterizer, false);
        }
    }

    // RenderingLayerMask は選択 layer の Renderer だけを透明な一時RTへ再描画し、
