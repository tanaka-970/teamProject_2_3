// UI Render の描画先座標変換、Backdrop 捕捉、Effect 用 RT 準備。
// UIRenderer::Render() 本文の連続断片。外部から直接 include しない。

            float opacity, const D3D11_RECT* scissor)
        {
            UIInputFieldComponent* input = const_cast<Core::GameObject&>(object)
                .GetComponent<UIInputFieldComponent>();
            if (input == nullptr)
            {
                // Unity InputField と同様、表示 Text を子 GameObject に置く構成も許す。
                // text_target 参照先の UIText を描いている場合は、その InputField の
                // selection / caret をこの Text の矩形へ重ねる。
                Scene::Scene* scene = const_cast<Core::GameObject&>(object).GetScene();
                if (scene != nullptr)
                {
                    for (std::size_t index = 0; index < scene->GameObjectCount(); ++index)
                    {
                        Core::GameObject* candidate_object = scene->GameObjectAt(index);
                        UIInputFieldComponent* candidate = candidate_object != nullptr
                            ? candidate_object->GetComponent<UIInputFieldComponent>() : nullptr;
                        if (candidate == nullptr || !candidate->text_target.IsAssigned()) continue;
                        if (candidate->text_target.owner == object.ID() &&
                            candidate->text_target.component == text.StableID())
                        {
                            input = candidate;
                            break;
                        }
                    }
                }
            }
            Core::GameObject* input_owner = input != nullptr ? input->Owner() : nullptr;
            UISelectableComponent* selectable = input_owner != nullptr
                ? input_owner->GetComponent<UISelectableComponent>() : nullptr;
            const bool focused_input = input != nullptr && selectable != nullptr &&
                selectable->focused;
            if (!text.ActiveInHierarchy() || text.opacity <= 0.0f ||
                (text.ResolvedText().empty() && !focused_input)) return;

            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            font_atlas.BuildGlyphs(text, r.z, r.w, asset_database);

            // InputField selection is a background, so emit it before glyphs.
            if (focused_input && input->HasSelection() && !input->password)
            {
                const int selection_start = input->SelectionStart();
                const int selection_end = input->SelectionEnd();
                const DirectX::XMFLOAT4 selection_color =
                    MultiplyAlpha(input->selection_color, opacity);
                for (const UITextComponent::GlyphQuad& glyph : text.Glyphs())
                {
                    if (glyph.character_index < selection_start ||
                        glyph.character_index >= selection_end) continue;
                    DirectX::XMFLOAT4 select_rect{
                        r.x + glyph.position.x,
                        r.y + glyph.position.y,
                        (std::max)(glyph.advance, glyph.size.x),
                        glyph.size.y };
                    append_quad(select_rect, rect.ResolvedMatrix(),
                        { 0.0f, 0.0f, 1.0f, 1.0f }, selection_color, scale);
                }
                configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                    false, 0.0f, {}, {}, {});
                Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
            }

            if (!text.ResolvedText().empty())
            {
                const DirectX::XMFLOAT4 color = MultiplyAlpha(text.color,
                    text.opacity * opacity);
                append_text_glyphs(object, text, r, rect.ResolvedMatrix(), color, scale);
                configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0, true,
                    text.outline_width, text.outline_color,
                    text.shadow_offset, text.shadow_color);
                Flush(context, font_atlas.Texture(), states.blend_alpha, states, scissor);
            }

            if (focused_input)
            {
                const float blink = (std::max)(0.05f, input->caret_blink_seconds);
                const bool visible = std::fmod((std::max)(0.0f, effect_time), blink) < blink * 0.5f;
                if (visible)
                {
                    float caret_x = r.x;
                    float caret_y = r.y + (std::max)(0.0f, (r.w - text.font_size) * 0.5f);
                    float caret_h = (std::max)(1.0f, text.font_size);
                    bool position_found = false;
                    for (const UITextComponent::GlyphQuad& glyph : text.Glyphs())
                    {
                        if (glyph.character_index >= input->caret_index)
                        {
                            caret_x = r.x + glyph.position.x;
                            caret_y = r.y + glyph.position.y;
                            caret_h = (std::max)(1.0f, glyph.size.y);
                            position_found = true;
                            break;
                        }
                        caret_x = r.x + glyph.position.x + glyph.advance;
                        caret_y = r.y + glyph.position.y;
                        caret_h = (std::max)(1.0f, glyph.size.y);
                    }
                    (void)position_found;
                    const float logical_width = (std::max)(0.5f,
                        input->caret_width / (std::max)(0.0001f, scale));
                    append_quad({ caret_x, caret_y, logical_width, caret_h },
                        rect.ResolvedMatrix(), { 0.0f, 0.0f, 1.0f, 1.0f },
                        MultiplyAlpha(input->caret_color, opacity), scale);
                    configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                        false, 0.0f, {}, {}, {});
                    Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
                }
            }
        };

        const auto render_focus_outline = [&](const Core::GameObject& object,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            const UISelectableComponent* selectable =
                object.GetComponent<UISelectableComponent>();
            if (selectable == nullptr || !selectable->focused ||
                !selectable->ActiveInHierarchy()) return;
            const bool enabled = selectable->override_focus_style
                ? selectable->focus_outline_enabled : states.focus_outline_enabled;
            if (!enabled) return;
            const DirectX::XMFLOAT4 color = MultiplyAlpha(
                selectable->override_focus_style ? selectable->focus_outline_color
                    : states.focus_outline_color, opacity);
            const float pixel_width = selectable->override_focus_style
                ? selectable->focus_outline_width : states.focus_outline_width;
            const float width = (std::max)(0.5f,
                pixel_width / (std::max)(0.0001f, scale));
            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            const float half = width * 0.5f;
            const float pixel_radius = selectable->override_focus_style
                ? selectable->focus_corner_radius : states.focus_corner_radius;
            const float radius = (std::max)(0.0f,
                pixel_radius / (std::max)(0.0001f, scale));
            const DirectX::XMFLOAT4 outline_rect{
                r.x - half, r.y - half, r.z + width, r.w + width };
            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            UIShapeComponent outline_shape;
            outline_shape.shape = UIShapeComponent::Rectangle;
            outline_shape.corner_radius = radius + half;
            bool closed = true;
            build_shape_path(outline_shape, outline_rect, scale, closed);
            append_stroked_path(outline_shape, matrix, color, width, scale, true);
            configure_visual({}, UIShapeComponent::Solid, 0.0f, { 0.5f, 0.5f },
                {}, UIShapeComponent::StrokeSolid, false, 0.0f, {}, {}, {});
            Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
        };

        const auto render_scrollbars = [&](const Core::GameObject& object,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            const UIScrollViewComponent* scroll =
                object.GetComponent<UIScrollViewComponent>();
            if (scroll == nullptr || !scroll->show_scrollbars ||
                !scroll->ActiveInHierarchy()) return;

            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            const float width = (std::max)(2.0f,
                scroll->scrollbar_width / (std::max)(0.0001f, scale));
            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            const auto draw_rounded = [&](const DirectX::XMFLOAT4& bar,
                const DirectX::XMFLOAT4& color, float corner_radius)
            {
                UIShapeComponent shape;
                shape.shape = UIShapeComponent::Rectangle;
                shape.corner_radius = (std::min)((std::max)(0.0f, corner_radius),
                    (std::min)(std::fabs(bar.z), std::fabs(bar.w)) * 0.5f);
                bool closed = true;
                build_shape_path(shape, bar, scale, closed);
                if (shape_path.size() < 3) return;

                DirectX::XMFLOAT2 center{ 0.0f, 0.0f };
                for (const DirectX::XMFLOAT2& point : shape_path)
                {
                    center.x += point.x;
                    center.y += point.y;
                }
                const float inv_count = 1.0f /
                    static_cast<float>((std::max)(std::size_t{ 1 }, shape_path.size()));
                center.x *= inv_count;
                center.y *= inv_count;
                const DirectX::XMFLOAT4 fill = MultiplyAlpha(color, opacity);
                for (std::size_t index = 0; index < shape_path.size(); ++index)
                {
                    append_triangle_local(center, shape_path[index],
                        shape_path[(index + 1) % shape_path.size()],
                        bar, matrix, fill, scale);
                }
            };

            const float radius = (std::max)(0.0f,
                scroll->scrollbar_corner_radius / (std::max)(0.0001f, scale));
            if (scroll->vertical_overflow)
            {
                const DirectX::XMFLOAT4 track{ r.x + r.z - width, r.y, width, r.w };
                draw_rounded(track, scroll->scrollbar_track_color,
                    (std::min)(radius, width * 0.5f));
                const float thumb_h = (std::max)(width,
                    r.w * scroll->vertical_visible_ratio);
                const float travel = (std::max)(0.0f, r.w - thumb_h);
                const float thumb_y = r.y + travel * (1.0f - scroll->vertical_normalized);
                draw_rounded({ r.x + r.z - width, thumb_y, width, thumb_h },
                    scroll->scrollbar_thumb_color, radius);
            }
            if (scroll->horizontal_overflow)
            {
                const DirectX::XMFLOAT4 track{ r.x, r.y, r.z, width };
                draw_rounded(track, scroll->scrollbar_track_color,
                    (std::min)(radius, width * 0.5f));
                const float thumb_w = (std::max)(width,
                    r.z * scroll->horizontal_visible_ratio);
                const float travel = (std::max)(0.0f, r.z - thumb_w);
                const float thumb_x = r.x + travel * scroll->horizontal_normalized;
                draw_rounded({ thumb_x, r.y, thumb_w, width },
                    scroll->scrollbar_thumb_color, radius);
            }
            if (scroll->vertical_overflow || scroll->horizontal_overflow)
            {
                configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                    false, 0.0f, {}, {}, {});
                Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
            }
        };

        const auto configure_effect_target = [&](UIRenderTarget& target)
        {
            ID3D11RenderTargetView* offscreen = target.rtv.Get();
            context->OMSetRenderTargets(1, &offscreen, nullptr);
            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(target.width);
            viewport.Height = static_cast<float>(target.height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);

            Constants offscreen_constants = constants;
            offscreen_constants.screen_size = {
                static_cast<float>(target.width),
                static_cast<float>(target.height), 0.0f, 0.0f };
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                &offscreen_constants, 0, 0);
            draw_target_height = static_cast<float>(target.height);
        };

        const auto apply_effect_passes = [&](const UIEffectStackComponent& effects,
            UIRenderTarget*& current, UIRenderTarget* first, UIRenderTarget* second,
            ID3D11ShaderResourceView* runtime_mask_texture = nullptr,
            bool runtime_mask_luma = false, bool runtime_mask_invert = false)
        {
            UIRenderTarget* third = effects.effect_region.enabled
                ? render_target_pool_.Acquire(current != nullptr ? current->width : 1,
                    current != nullptr ? current->height : 1)
                : second;
            const std::vector<UIEffect>& effective_effects =
                effects.EffectiveEffects(asset_database);
            bool needs_temporal_history = false;
            for (const UIEffect& effect : effective_effects)
            {
                if (!effect.enabled) continue;
                const UIEffectKind kind = static_cast<UIEffectKind>(effect.kind);
                if (kind == UIEffectKind::MotionBlur || kind == UIEffectKind::Echo ||
                    kind == UIEffectKind::FeedbackZoom)
                {
                    needs_temporal_history = true;
                    break;
                }
            }
            const std::uint64_t temporal_owner_key = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(&effects));
            TemporalHistoryEntry* history = needs_temporal_history && current != nullptr
                ? TemporalHistoryFor(temporal_owner_key, current->width, current->height)
                : nullptr;

            Rendering::Effects::EffectChain::Context chain_context{};
            chain_context.device_context = context;
            chain_context.asset_database = asset_database;
            chain_context.shader_catalog = shader_catalog;
            chain_context.time = effect_time;
            chain_context.depth_disabled = states.depth_disabled;
            chain_context.rasterizer = states.rasterizer;
            chain_context.blend_none = states.blend_none;
            chain_context.blend_alpha = states.blend_alpha;
            chain_context.sampler = states.sampler;
            chain_context.resolve_texture = [&](const std::string& guid)
            {
                // Texture cache / white fallback は従来どおり UIRenderer が一元所有する。
                return TextureFor(guid, asset_database);
            };
            chain_context.runtime_mask_texture = runtime_mask_texture;
            chain_context.runtime_mask_luma = runtime_mask_luma;
            chain_context.runtime_mask_invert = runtime_mask_invert;
            chain_context.runtime_history_texture = history != nullptr && history->valid
                ? history->target.srv.Get() : nullptr;
            chain_context.effect_region = third != nullptr && third != second
                ? &effects.effect_region : nullptr;
            chain_context.configure_target = [&](UIRenderTarget& target)
            {
                configure_effect_target(target);
            };
            chain_context.draw_plain_fullscreen = [&](float width, float height,
                ID3D11ShaderResourceView* source, ID3D11BlendState* blend)
            {
                DirectX::XMFLOAT4X4 identity{};
                DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                    false, 0.0f, {}, {}, {});
                append_quad({ 0.0f, 0.0f, width, height }, identity,
                    { 0.0f, 0.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f);
                Flush(context, source, blend, states, nullptr);
            };
            chain_context.draw_effect_fullscreen = [&](float width, float height,
                ID3D11ShaderResourceView* source, ID3D11BlendState* blend,
                ID3D11PixelShader* shader, ID3D11Buffer* effect_constants)
            {
                DirectX::XMFLOAT4X4 identity{};
                DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                append_quad({ 0.0f, 0.0f, width, height }, identity,
                    { 0.0f, 0.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f);
                Flush(context, source, blend, states, nullptr,
                    shader, effect_constants);
            };
            chain_context.draw_region_fullscreen = [&](float width, float height,
                ID3D11ShaderResourceView* effected, ID3D11ShaderResourceView* original,
                ID3D11ShaderResourceView* region_mask, ID3D11BlendState* blend,
                ID3D11PixelShader* shader, ID3D11Buffer* effect_constants)
            {
                ID3D11ShaderResourceView* mask = region_mask;
                ID3D11ShaderResourceView* source = original;
                context->PSSetShaderResources(1, 1, &mask);
                context->PSSetShaderResources(2, 1, &source);
                DirectX::XMFLOAT4X4 identity{};
                DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                append_quad({ 0.0f, 0.0f, width, height }, identity,
                    { 0.0f, 0.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f);
                Flush(context, effected, blend, states, nullptr,
                    shader, effect_constants);
                ID3D11ShaderResourceView* null_views[2]{};
                context->PSSetShaderResources(1, 2, null_views);
            };
            current = effect_chain_.Apply(chain_context, effective_effects,
                current, first, second, third);
            if (history != nullptr && current != nullptr && current->texture &&
                history->target.texture)
            {
                // CopyResource 前に EffectChain が t0/t1 を解除している。前回の最終結果を
                // 保存するため Echo は再帰的な時間残像になり、MotionBlur は前後フレームを混ぜられる。
                context->CopyResource(history->target.texture.Get(), current->texture.Get());
                history->valid = true;
                history->last_used_serial = render_serial_;
            }
        };

        // UI の論理座標から、現在の描画先（Scene View の offset / zoom を含む）の
        // 実ピクセル座標へ変換する。Flush の scissor 変換と同じ値を使う。
        const auto to_output_point = [&](const DirectX::XMFLOAT2& canvas_point,
            float canvas_scale)
        {
            const DirectX::XMFLOAT2 screen_point = ToScreenPoint(canvas_point,
                canvas_scale, screen_height);
            const float scale_x = states.viewport_scale_x > 0.0001f
                ? states.viewport_scale_x : 1.0f;
            const float scale_y = states.viewport_scale_y > 0.0001f
                ? states.viewport_scale_y : 1.0f;
            return DirectX::XMFLOAT2{
                states.scissor_offset_x + screen_point.x * scale_x,
                states.scissor_offset_y + screen_point.y * scale_y };
        };

        const auto capture_scale_for = [&](const RectTransformComponent& rect,
            const DirectX::XMFLOAT4& source_rect, float canvas_scale,
            float& out_scale)
        {
            if (world_space_canvas_ || source_rect.z <= 0.0001f ||
                source_rect.w <= 0.0001f)
            {
                return false;
            }

            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            const DirectX::XMFLOAT2 p0 = to_output_point(
                TransformPoint(matrix, source_rect.x, source_rect.y), canvas_scale);
            const DirectX::XMFLOAT2 p1 = to_output_point(
                TransformPoint(matrix, source_rect.x + source_rect.z, source_rect.y),
                canvas_scale);
            const DirectX::XMFLOAT2 p2 = to_output_point(
                TransformPoint(matrix, source_rect.x + source_rect.z,
                    source_rect.y + source_rect.w), canvas_scale);
            const DirectX::XMFLOAT2 p3 = to_output_point(
                TransformPoint(matrix, source_rect.x, source_rect.y + source_rect.w),
                canvas_scale);
            constexpr float alignment_epsilon = 0.01f;
            if (std::fabs(p0.y - p1.y) > alignment_epsilon ||
                std::fabs(p1.x - p2.x) > alignment_epsilon ||
                std::fabs(p2.y - p3.y) > alignment_epsilon ||
                std::fabs(p3.x - p0.x) > alignment_epsilon ||
                p1.x <= p0.x || p0.y <= p3.y)
            {
                return false;
            }

            const float scale_x = (p1.x - p0.x) / source_rect.z;
            const float scale_y = (p0.y - p3.y) / source_rect.w;
            if (scale_x <= 0.0001f || scale_y <= 0.0001f ||
                std::fabs(scale_x - scale_y) >
                    (std::max)(scale_x, scale_y) * 0.0001f)
            {
                return false;
            }
            out_scale = scale_x;
            return true;
        };

        const auto make_backdrop_capture_plan = [&](const RectTransformComponent& rect,
            const DirectX::XMFLOAT4& composite_rect, float canvas_scale,
            const D3D11_RECT* scissor, BackdropCapturePlan& plan)
        {
            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            const DirectX::XMFLOAT2 p0 = to_output_point(
                TransformPoint(matrix, composite_rect.x, composite_rect.y), canvas_scale);
            const DirectX::XMFLOAT2 p1 = to_output_point(
                TransformPoint(matrix, composite_rect.x + composite_rect.z,
                    composite_rect.y), canvas_scale);
            const DirectX::XMFLOAT2 p2 = to_output_point(
                TransformPoint(matrix, composite_rect.x + composite_rect.z,
                    composite_rect.y + composite_rect.w), canvas_scale);
            const DirectX::XMFLOAT2 p3 = to_output_point(
                TransformPoint(matrix, composite_rect.x,
                    composite_rect.y + composite_rect.w), canvas_scale);
            const float min_x = (std::min)({ p0.x, p1.x, p2.x, p3.x });
            const float max_x = (std::max)({ p0.x, p1.x, p2.x, p3.x });
            const float min_y = (std::min)({ p0.y, p1.y, p2.y, p3.y });
            const float max_y = (std::max)({ p0.y, p1.y, p2.y, p3.y });
            D3D11_RECT full_rect{};
            full_rect.left = static_cast<LONG>(std::floor(min_x));
            full_rect.top = static_cast<LONG>(std::floor(min_y));
            full_rect.right = static_cast<LONG>(std::ceil(max_x));
            full_rect.bottom = static_cast<LONG>(std::ceil(max_y));
            if (full_rect.right <= full_rect.left || full_rect.bottom <= full_rect.top)
                return false;

            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> source_view;
            context->OMGetRenderTargets(1, source_view.GetAddressOf(), nullptr);
            if (!source_view) return false;
            Microsoft::WRL::ComPtr<ID3D11Resource> source_resource;
            source_view->GetResource(source_resource.GetAddressOf());
            if (!source_resource ||
                FAILED(source_resource.As(&plan.source_texture)) ||
                !plan.source_texture)
            {
                return false;
            }

            D3D11_TEXTURE2D_DESC source_desc{};
            plan.source_texture->GetDesc(&source_desc);
            if (source_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
                source_desc.SampleDesc.Count != 1)
            {
