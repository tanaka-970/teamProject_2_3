// PostProcess/UI/Editor、統計集計、Temporal 履歴、Golden Capture、Present。
// framework::render() 本文の連続断片。外部から直接 include しない。

post_process.Execute(immediate_context.Get(), *bit_block_transfer,
                post_source, bloom_srv, viewport.Width, viewport.Height,
                enable_bloom_shader, enable_vignette_shader, enable_fxaa_shader);
            }
        }
        else
        {
            ID3D11ShaderResourceView* selected =
                output == ReplayEngine::Rendering::RenderOutput::Bloom && bloom_srv
                ? bloom_srv
                : framebuffers[0]->shader_resource_views[0].Get();
            bit_block_transfer->blit(immediate_context.Get(), &selected, 0, 1);
        }
    }

    // 次フレームで描画先に戻すフレームバッファはSRVから外しておく。
    // ドライバー任せの競合解消を避けるために明示的に解除する。
    ID3D11ShaderResourceView* null_post_srvs[2]{};
    immediate_context->PSSetShaderResources(0, _countof(null_post_srvs), null_post_srvs);
    ReplayEngine::Rendering::Stats().CountStateSet(ReplayEngine::Rendering::RenderStats::StateKind::ShaderResource, false);
    }

    ReplayEngine::Rendering::Stats().EndPhase(
        ReplayEngine::Rendering::RenderStats::Phase::Scene3D, immediate_context.Get());

    render_camera_override = nullptr;
    render_matrix_override_active = false;
    render_camera_aspect = 0.0f;
    ReplayEngine::Rendering::Stats().BeginPhase(
        ReplayEngine::Rendering::RenderStats::Phase::GameUI, immediate_context.Get());
    const object_ui_viewport ui_target = object_ui_viewport_target();
    D3D11_VIEWPORT ui_viewport = viewport;
    ui_viewport.TopLeftX = ui_target.left;
    ui_viewport.TopLeftY = ui_target.top;
    ui_viewport.Width = ui_target.width;
    ui_viewport.Height = ui_target.height;
    // Editor では Scene View の矩形へ、実行時は従来どおりウィンドウ全体へ描く。
    immediate_context->RSSetViewports(1, &ui_viewport);

    const float ui_logical_width = (std::max)(1.0f, ui_target.logical_width);
    const float ui_logical_height = (std::max)(1.0f, ui_target.logical_height);
    ReplayEngine::UI::UILayout::Resolve(active_object_scene(),
        ui_logical_width, ui_logical_height);
    ReplayEngine::UI::UIRenderer::RenderStates ui_states{};
    ui_states.depth_disabled =
        depth_stencil_states[(size_t)DEPTH_STATE::ZT_OFF_ZW_OFF].Get();
    ui_states.depth_enabled =
        depth_stencil_states[(size_t)DEPTH_STATE::ZT_ON_ZW_OFF].Get();
    DirectX::XMStoreFloat4x4(&ui_states.world_view_projection,
        viewport_view_matrix() * viewport_projection_matrix());
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
    ui_states.focus_outline_enabled = project_settings.FocusOutlineEnabled();
    ui_states.focus_outline_color = project_settings.FocusOutlineColor();
    ui_states.focus_outline_width = project_settings.FocusOutlineWidth();
    ui_states.focus_corner_radius = project_settings.FocusCornerRadius();
    ui_states.scissor_offset_x = ui_target.left;
    ui_states.scissor_offset_y = ui_target.top;
    ui_states.viewport_scale_x = ui_target.width / ui_logical_width;
    ui_states.viewport_scale_y = ui_target.height / ui_logical_height;
    ui_states.scissor_bounds_enabled = true;
    ui_states.scissor_bounds.left = 0;
    ui_states.scissor_bounds.top = 0;
    ui_states.scissor_bounds.right = static_cast<LONG>((std::max)(
        1.0f, static_cast<float>(client_width)));
    ui_states.scissor_bounds.bottom = static_cast<LONG>((std::max)(
        1.0f, static_cast<float>(client_height)));
    {
        REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "UIRenderer");
    ui_renderer.Render(immediate_context.Get(), active_object_scene(),
        &asset_database, &shader_library.Catalog(), ui_font_atlas,
        ui_logical_width, ui_logical_height, shader_composer_time, ui_states);
    }
    // UI 用の viewport override は UIRenderer の間だけ。ImGui と次フレームは client 全体へ戻す。
    immediate_context->RSSetViewports(1, &viewport);
    ReplayEngine::Rendering::Stats().EndPhase(
        ReplayEngine::Rendering::RenderStats::Phase::GameUI, immediate_context.Get());

    ReplayEngine::Rendering::Stats().BeginPhase(
        ReplayEngine::Rendering::RenderStats::Phase::EditorUI, immediate_context.Get());
#ifdef USE_IMGUI
    // update()でNewFrameを通したフレームだけ描く。ロード完了フレームのように
    // 途中でeditor_modeが立った場合は次フレームからUIを出す。
    if (imgui_frame_active)
    {
        imgui_frame_active = false;
        // エディタUIはポスト処理後に描き、ゲーム画面の色補正から除外する。
        ImGui::Render();
        {
            REPLAY_PROFILE_GPU_SCOPE(immediate_context.Get(), "ImGuiRender");
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        if (editor_hide_requested)
        {
            editor_hide_requested = false;
            editor_mode = false;
            edit_mode_active = false;
            SetFocus(hwnd);
        }
    }
#endif
    ReplayEngine::Rendering::Stats().EndPhase(
        ReplayEngine::Rendering::RenderStats::Phase::EditorUI, immediate_context.Get());

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

    if (ReplayEngine::Rendering::Stats().Enabled() && !ReplayEngine::Rendering::Stats().Paused())
    {
        // Object/Component/asset resident の全走査を毎フレーム行うと、
        // 「計測のための計測」が CPU を食う。カリング値だけは毎フレーム、
        // 重いメタデータとメモリ内訳は約30フレームごとに更新する。
        static std::uint32_t profile_metadata_countdown = 0;
        static std::uint64_t profile_objects = 0;
        static std::uint64_t profile_components = 0;
        static std::uint64_t profile_effect_stacks = 0;

        const ReplayEngine::Scene::Scene& profile_scene = active_object_scene();
        if (profile_metadata_countdown == 0)
        {
            profile_metadata_countdown = 30u;
            profile_objects = profile_scene.GameObjectCount();
            profile_components = 0;
            profile_effect_stacks = 0;
            for (std::size_t profile_index = 0;
                profile_index < profile_scene.GameObjectCount(); ++profile_index)
            {
                const ReplayEngine::Core::GameObject* profile_object =
                    profile_scene.GameObjectAt(profile_index);
                if (profile_object == nullptr) continue;
                profile_components += profile_object->ComponentCount();
                for (std::size_t component_index = 0;
                    component_index < profile_object->ComponentCount(); ++component_index)
                {
                    const ReplayEngine::Core::Component* component =
                        profile_object->ComponentAt(component_index);
                    if (component == nullptr) continue;
                    if (component->TypeID() ==
                            ReplayEngine::Components::UIEffectStackComponent::StaticTypeID() ||
                        component->TypeID() ==
                            ReplayEngine::Components::ModelEffectStackComponent::StaticTypeID() ||
                        component->TypeID() ==
                            ReplayEngine::Components::ScreenEffectStackComponent::StaticTypeID())
                    {
                        ++profile_effect_stacks;
                    }
                }
            }

            std::uint32_t duplicate_assets = 0;
            std::unordered_set<std::string> profile_guids;
            for (const auto& record : asset_database.Records())
            {
                if (!record.guid.empty() &&
                    !profile_guids.insert(record.guid).second)
                {
                    ++duplicate_assets;
                }
            }
            ReplayEngine::Rendering::Stats().SetDuplicateAssetGuids(
                duplicate_assets,
                static_cast<std::uint32_t>(
                    shader_library.Catalog().DuplicateIdCount()));

            // 同じ Asset GUID が複数のGPU Texture実体として常駐していないかを
            // major texture owner 間で直接比較する。単なる参照数ではなく、
            // GUIDが同じなのに ID3D11Resource* が異なる場合だけ重複と数える。
            std::vector<std::pair<std::string, const void*>> resident_textures;
            ui_renderer.AppendResidentTextureIdentities(resident_textures);
            material_gpu_binder.AppendResidentTextureIdentities(resident_textures);
            std::unordered_map<std::string, const void*> first_texture_resource;
            std::unordered_set<std::string> duplicate_texture_guids;
            for (const auto& resident : resident_textures)
            {
                const auto inserted = first_texture_resource.emplace(
                    resident.first, resident.second);
                if (!inserted.second && inserted.first->second != resident.second)
                    duplicate_texture_guids.insert(resident.first);
            }
            ReplayEngine::Rendering::Stats().SetResidentTextureDuplicates(
                static_cast<std::uint32_t>(resident_textures.size()),
                static_cast<std::uint32_t>(duplicate_texture_guids.size()));

            std::uint64_t tracked_buffers = ui_renderer.TrackedBufferBytes() +
                scene_effect_chain.AllocatedBufferBytes() +
                material_gpu_binder.TrackedBufferBytes();
            for (const auto& buffer : constant_buffers)
                tracked_buffers += buffer_byte_width(buffer.Get());
            tracked_buffers += buffer_byte_width(material_override_cb.Get());
            tracked_buffers += buffer_byte_width(shader_layer_cb.Get());
            tracked_buffers += buffer_byte_width(character_material_cb.Get());
            tracked_buffers += buffer_byte_width(frame_constants_cb.Get());

            const std::uint64_t render_target_bytes =
                ui_renderer.RenderTargetPoolBytes() +
                scene_effect_targets.AllocatedBytes();
            ReplayEngine::Rendering::Stats().SetEngineMemoryBytes(
                texture_cache_resident_bytes() + material_gpu_binder.TrackedTextureBytes(),
                tracked_buffers, render_target_bytes);
        }
        else
        {
            --profile_metadata_countdown;
        }

        const auto& profile_culling = ReplayEngine::Rendering::Culling();
        ReplayEngine::Rendering::Stats().SetSceneCounters(
            profile_objects, profile_components, profile_culling.tested,
            profile_culling.tested >= profile_culling.culled
                ? profile_culling.tested - profile_culling.culled : 0u,
            profile_effect_stacks);
    }

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
