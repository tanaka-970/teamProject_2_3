// UI Render の Effect Stack・Mask・offscreen 合成。
// UIRenderer::Render() 本文の連続断片。外部から直接 include しない。

                return false;
            }

            D3D11_RECT copy_rect = full_rect;
            const D3D11_RECT source_bounds{ 0, 0,
                static_cast<LONG>(source_desc.Width),
                static_cast<LONG>(source_desc.Height) };
            copy_rect = IntersectScissor(copy_rect, source_bounds);
            if (scissor != nullptr)
            {
                const float scale_x = states.viewport_scale_x > 0.0001f
                    ? states.viewport_scale_x : 1.0f;
                const float scale_y = states.viewport_scale_y > 0.0001f
                    ? states.viewport_scale_y : 1.0f;
                D3D11_RECT output_scissor{};
                output_scissor.left = static_cast<LONG>(std::floor(
                    states.scissor_offset_x + scissor->left * scale_x));
                output_scissor.top = static_cast<LONG>(std::floor(
                    states.scissor_offset_y + scissor->top * scale_y));
                output_scissor.right = static_cast<LONG>(std::ceil(
                    states.scissor_offset_x + scissor->right * scale_x));
                output_scissor.bottom = static_cast<LONG>(std::ceil(
                    states.scissor_offset_y + scissor->bottom * scale_y));
                if (states.scissor_bounds_enabled)
                    output_scissor = IntersectScissor(output_scissor,
                        states.scissor_bounds);
                copy_rect = IntersectScissor(copy_rect, output_scissor);
            }
            if (copy_rect.right <= copy_rect.left || copy_rect.bottom <= copy_rect.top)
                return false;

            plan.output_rect = full_rect;
            plan.width = static_cast<std::uint32_t>(full_rect.right - full_rect.left);
            plan.height = static_cast<std::uint32_t>(full_rect.bottom - full_rect.top);
            plan.destination_x = static_cast<std::uint32_t>(
                copy_rect.left - full_rect.left);
            plan.destination_y = static_cast<std::uint32_t>(
                copy_rect.top - full_rect.top);
            plan.source_box.left = static_cast<UINT>(copy_rect.left);
            plan.source_box.top = static_cast<UINT>(copy_rect.top);
            plan.source_box.right = static_cast<UINT>(copy_rect.right);
            plan.source_box.bottom = static_cast<UINT>(copy_rect.bottom);
            plan.source_box.front = 0;
            plan.source_box.back = 1;
            return plan.width > 0 && plan.height > 0;
        };

        const auto render_effect_with_backdrop = [&](const UIEffectStackComponent& effects,
            const RectTransformComponent& rect, const DirectX::XMFLOAT4& source_rect,
            float canvas_scale, const D3D11_RECT* scissor, const auto& draw_source)
        {
            if (!effects.HasActiveEffects(asset_database) || states.blend_none == nullptr)
                return false;

            float capture_scale = 1.0f;
            if (!capture_scale_for(rect, source_rect, canvas_scale, capture_scale))
                return false;

            const DirectX::XMFLOAT4 expansion = effects.ExpandBounds(
                source_rect.z * capture_scale, source_rect.w * capture_scale, asset_database);
            const float inverse_scale = 1.0f / (std::max)(0.0001f, capture_scale);
            const float expanded_width = (std::max)(1.0f,
                source_rect.z + (expansion.x + expansion.z) * inverse_scale);
            const float expanded_height = (std::max)(1.0f,
                source_rect.w + (expansion.y + expansion.w) * inverse_scale);
            const DirectX::XMFLOAT4 composite_rect{
                source_rect.x - expansion.x * inverse_scale,
                source_rect.y - expansion.y * inverse_scale,
                expanded_width,
                expanded_height };
            BackdropCapturePlan plan{};
            if (!make_backdrop_capture_plan(rect, composite_rect, canvas_scale,
                scissor, plan))
            {
                return false;
            }

            UIRenderTarget* target = render_target_pool_.Acquire(plan.width, plan.height);
            UIRenderTarget* scratch = render_target_pool_.Acquire(plan.width, plan.height);
            if (target == nullptr || scratch == nullptr || !target->texture ||
                !target->rtv || !target->srv || !scratch->texture ||
                !scratch->rtv || !scratch->srv ||
                target->texture.Get() == plan.source_texture.Get() ||
                scratch->texture.Get() == plan.source_texture.Get())
            {
                return false;
            }

            ID3D11ShaderResourceView* null_srvs[2]{};
            context->PSSetShaderResources(0, _countof(null_srvs), null_srvs);
            const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            context->ClearRenderTargetView(target->rtv.Get(), clear);
            context->CopySubresourceRegion(target->texture.Get(), 0,
                plan.destination_x, plan.destination_y, 0, plan.source_texture.Get(), 0,
                &plan.source_box);

            ID3D11RenderTargetView* previous_rtv = nullptr;
            ID3D11DepthStencilView* previous_dsv = nullptr;
            context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
            UINT viewport_count = 1;
            D3D11_VIEWPORT previous_viewport{};
            context->RSGetViewports(&viewport_count, &previous_viewport);

            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            const DirectX::XMFLOAT2 source_p0 = to_output_point(
                TransformPoint(matrix, source_rect.x, source_rect.y), canvas_scale);
            const DirectX::XMFLOAT2 source_p2 = to_output_point(
                TransformPoint(matrix, source_rect.x + source_rect.z,
                    source_rect.y + source_rect.w), canvas_scale);
            const float source_left = (std::min)(source_p0.x, source_p2.x);
            const float source_bottom = (std::max)(source_p0.y, source_p2.y);
            DirectX::XMFLOAT4 draw_rect{
                (source_left - static_cast<float>(plan.output_rect.left)) / capture_scale,
                (static_cast<float>(plan.height) -
                    (source_bottom - static_cast<float>(plan.output_rect.top))) /
                    capture_scale,
                source_rect.z,
                source_rect.w };

            configure_effect_target(*target);
            draw_source(draw_rect, capture_scale);

            ID3D11ShaderResourceView* null_srv = nullptr;
            context->PSSetShaderResources(0, 1, &null_srv);
            UIRenderTarget* current = target;
            apply_effect_passes(effects, current, target, scratch);

            context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
            if (viewport_count > 0) context->RSSetViewports(1, &previous_viewport);
            if (previous_rtv != nullptr) previous_rtv->Release();
            if (previous_dsv != nullptr) previous_dsv->Release();

            constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                &constants, 0, 0);
            draw_target_height = screen_height;
            append_quad(composite_rect, rect.ResolvedMatrix(),
                { 0.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
                canvas_scale);
            configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                false, 0.0f, {}, {}, {});
            Flush(context, current->srv.Get(), states.blend_none, states, scissor);
            return true;
        };

        const auto render_image_effect_with_backdrop = [&](
            const UIEffectStackComponent& effects, UIImageComponent& image,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor, const UIPuppetDeformComponent* puppet)
        {
            if (!image.ActiveInHierarchy() || image.opacity <= 0.0f ||
                image.fill_amount <= 0.0f)
            {
                return false;
            }
            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            return render_effect_with_backdrop(effects, rect, source_rect, scale,
                scissor, [&](const DirectX::XMFLOAT4& draw_rect, float capture_scale)
                {
                    DirectX::XMFLOAT4 source = draw_rect;
                    const ResolvedImageSource resolved = resolve_image_source(image);
                    DirectX::XMFLOAT4 uv = resolved.uv;
                    const float fill = (std::min)((std::max)(image.fill_amount, 0.0f),
                        1.0f);
                    if (image.fill_method == UIImageComponent::Horizontal)
                    {
                        source.z *= fill;
                        uv.z *= fill;
                    }
                    else if (image.fill_method == UIImageComponent::Vertical)
                    {
                        source.w *= fill;
                        uv.w *= fill;
                    }
                    DirectX::XMFLOAT4X4 identity{};
                    DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                    append_image_geometry(source, identity, uv,
                        MultiplyAlpha(image.color, image.opacity * opacity), capture_scale,
                        puppet, resolved.rotated);
                    configure_visual(image.fill_color_2, image.fill_mode, image.fill_angle,
                        image.fill_center, image.stroke_color_2, image.stroke_mode,
                        false, 0.0f, {}, {}, {});
                    Flush(context, TextureFor(resolved.texture_guid, asset_database),
                        BlendForImage(image, states), states, nullptr);
                });
        };

        const auto render_shape_effect_with_backdrop = [&] (
            const UIEffectStackComponent& effects, UIShapeComponent& shape,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!shape.ActiveInHierarchy() || opacity <= 0.0f) return false;
            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            return render_effect_with_backdrop(effects, rect, source_rect, scale,
                scissor, [&](const DirectX::XMFLOAT4& draw_rect, float capture_scale)
                {
                    DirectX::XMFLOAT4X4 identity{};
                    DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                    render_shape_geometry(shape, draw_rect, identity, capture_scale,
                        opacity, nullptr);
                });
        };

        const auto render_text_effect_with_backdrop = [&](const Core::GameObject& object,
            const UIEffectStackComponent& effects, UITextComponent& text,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!text.ActiveInHierarchy() || text.opacity <= 0.0f || text.ResolvedText().empty())
                return false;
            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            return render_effect_with_backdrop(effects, rect, source_rect, scale,
                scissor, [&](const DirectX::XMFLOAT4& draw_rect, float capture_scale)
                {
                    font_atlas.BuildGlyphs(text, source_rect.z, source_rect.w,
                        asset_database);
                    DirectX::XMFLOAT4X4 identity{};
                    DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                    append_text_glyphs(object, text, draw_rect, identity,
                        MultiplyAlpha(text.color, text.opacity * opacity), capture_scale);
                    configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0, true,
                        text.outline_width, text.outline_color,
                        text.shadow_offset, text.shadow_color);
                    Flush(context, font_atlas.Texture(), states.blend_alpha, states, nullptr);
                });
        };

        const auto render_effect_preview = [&](const UIEffectStackComponent& effects,
            UIImageComponent& image, const RectTransformComponent& rect, float scale,
            float opacity, const D3D11_RECT* scissor,
            const UIPuppetDeformComponent* puppet)
        {
            if (!effects.HasActiveEffects(asset_database) || !image.ActiveInHierarchy() ||
                image.opacity <= 0.0f || image.fill_amount <= 0.0f)
            {
                return false;
            }

            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            const DirectX::XMFLOAT4 expansion = effects.ExpandBounds(
                source_rect.z * scale, source_rect.w * scale, asset_database);
            // Effect の変位量は target_size.zw を使う実ピクセル単位なので、
            // RT の確保量へ Canvas 拡大率を掛けてはいけない。
            // 一方 composite_rect は論理単位で積まれ、描画時に scale が掛かる。
            // したがって確保量は「論理単位へ戻して」から矩形へ足す。
            // ここを実ピクセルのまま足すと、RT の実幅が
            // source*scale + expansion なのに矩形の実幅が (source + expansion)*scale となり、
            // scale != 1 のとき結果が縮んで位置もずれる。
            const float inverse_scale = 1.0f / (std::max)(0.0001f, scale);
            const float expand_left = expansion.x * inverse_scale;
            const float expand_top = expansion.y * inverse_scale;
            const float expand_right = expansion.z * inverse_scale;
            const float expand_bottom = expansion.w * inverse_scale;
            const float expanded_width = (std::max)(1.0f,
                source_rect.z + expand_left + expand_right);
            const float expanded_height = (std::max)(1.0f,
                source_rect.w + expand_top + expand_bottom);
            const std::uint32_t rt_width = static_cast<std::uint32_t>(
                std::ceil((std::max)(1.0f, expanded_width * scale)));
            const std::uint32_t rt_height = static_cast<std::uint32_t>(
                std::ceil((std::max)(1.0f, expanded_height * scale)));
            UIRenderTarget* target = render_target_pool_.Acquire(rt_width, rt_height);
            UIRenderTarget* scratch = render_target_pool_.Acquire(rt_width, rt_height);
            if (target == nullptr || scratch == nullptr ||
                !target->rtv || !target->srv || !scratch->rtv || !scratch->srv)
            {
                return false;
            }

            ID3D11RenderTargetView* previous_rtv = nullptr;
            ID3D11DepthStencilView* previous_dsv = nullptr;
            context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
            UINT viewport_count = 1;
            D3D11_VIEWPORT previous_viewport{};
            context->RSGetViewports(&viewport_count, &previous_viewport);

            const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            configure_effect_target(*target);
            context->ClearRenderTargetView(target->rtv.Get(), clear);

            DirectX::XMFLOAT4 draw_rect{
                expansion.x,
                expansion.y,
                source_rect.z,
                source_rect.w };
            const ResolvedImageSource resolved = resolve_image_source(image);
            DirectX::XMFLOAT4 uv = resolved.uv;
            const float fill = (std::min)((std::max)(image.fill_amount, 0.0f), 1.0f);
            if (image.fill_method == UIImageComponent::Horizontal)
            {
                draw_rect.z *= fill;
                uv.z *= fill;
            }
            else if (image.fill_method == UIImageComponent::Vertical)
            {
                draw_rect.w *= fill;
                uv.w *= fill;
            }

            DirectX::XMFLOAT4X4 identity{};
            DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
            append_image_geometry(draw_rect, identity, uv,
                MultiplyAlpha(image.color, image.opacity * opacity), scale,
                puppet, resolved.rotated);
            configure_visual(image.fill_color_2, image.fill_mode, image.fill_angle,
                image.fill_center, image.stroke_color_2, image.stroke_mode,
                false, 0.0f, {}, {}, {});
            Flush(context, TextureFor(resolved.texture_guid, asset_database),
                states.blend_alpha, states, nullptr);

            ID3D11ShaderResourceView* null_srv = nullptr;
            context->PSSetShaderResources(0, 1, &null_srv);
            UIRenderTarget* current = target;
            apply_effect_passes(effects, current, target, scratch);

            context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
            if (viewport_count > 0) context->RSSetViewports(1, &previous_viewport);
            if (previous_rtv != nullptr) previous_rtv->Release();
            if (previous_dsv != nullptr) previous_dsv->Release();

            constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                &constants, 0, 0);
            draw_target_height = screen_height;

            DirectX::XMFLOAT4 composite_rect{
                source_rect.x - expand_left,
                source_rect.y - expand_top,
                expanded_width,
                expanded_height };
            append_quad(composite_rect, rect.ResolvedMatrix(),
                { 0.0f, 0.0f, 1.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f, 1.0f }, scale);
            configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                false, 0.0f, {}, {}, {});
            Flush(context, current->srv.Get(), BlendForImage(image, states),
                states, scissor);
            return true;
        };

        const auto render_shape_effect_preview = [&] (
            const UIEffectStackComponent& effects, UIShapeComponent& shape,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!effects.HasActiveEffects(asset_database) ||
                !shape.ActiveInHierarchy() || opacity <= 0.0f)
                return false;

            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            const DirectX::XMFLOAT4 expansion = effects.ExpandBounds(
                source_rect.z * scale, source_rect.w * scale, asset_database);
            const float inverse_scale = 1.0f / (std::max)(0.0001f, scale);
            const float expand_left = expansion.x * inverse_scale;
            const float expand_top = expansion.y * inverse_scale;
            const float expand_right = expansion.z * inverse_scale;
            const float expand_bottom = expansion.w * inverse_scale;
            const float expanded_width = (std::max)(1.0f,
                source_rect.z + expand_left + expand_right);
            const float expanded_height = (std::max)(1.0f,
                source_rect.w + expand_top + expand_bottom);
            const std::uint32_t rt_width = static_cast<std::uint32_t>(
                std::ceil((std::max)(1.0f, expanded_width * scale)));
            const std::uint32_t rt_height = static_cast<std::uint32_t>(
                std::ceil((std::max)(1.0f, expanded_height * scale)));
            UIRenderTarget* target = render_target_pool_.Acquire(rt_width, rt_height);
            UIRenderTarget* scratch = render_target_pool_.Acquire(rt_width, rt_height);
            if (target == nullptr || scratch == nullptr || !target->rtv || !target->srv ||
                !scratch->rtv || !scratch->srv)
                return false;

            ID3D11RenderTargetView* previous_rtv = nullptr;
            ID3D11DepthStencilView* previous_dsv = nullptr;
            context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
            UINT viewport_count = 1;
            D3D11_VIEWPORT previous_viewport{};
            context->RSGetViewports(&viewport_count, &previous_viewport);
            const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            configure_effect_target(*target);
            context->ClearRenderTargetView(target->rtv.Get(), clear);

            DirectX::XMFLOAT4 draw_rect{
                expansion.x, expansion.y, source_rect.z, source_rect.w };
            DirectX::XMFLOAT4X4 identity{};
            DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
            render_shape_geometry(shape, draw_rect, identity, scale, opacity, nullptr);

            ID3D11ShaderResourceView* null_srv = nullptr;
            context->PSSetShaderResources(0, 1, &null_srv);
            UIRenderTarget* current = target;
            apply_effect_passes(effects, current, target, scratch);

            context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
            if (viewport_count > 0) context->RSSetViewports(1, &previous_viewport);
            if (previous_rtv != nullptr) previous_rtv->Release();
            if (previous_dsv != nullptr) previous_dsv->Release();
            constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);
            draw_target_height = screen_height;

            DirectX::XMFLOAT4 composite_rect{
                source_rect.x - expand_left, source_rect.y - expand_top,
                expanded_width, expanded_height };
            append_quad(composite_rect, rect.ResolvedMatrix(),
                { 0.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, scale);
            configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                false, 0.0f, {}, {}, {});
            Flush(context, current->srv.Get(), states.blend_alpha, states, scissor);
            return true;
        };

        const auto render_text_effect_preview = [&](const Core::GameObject& object,
            const UIEffectStackComponent& effects, UITextComponent& text,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!effects.HasActiveEffects(asset_database) || !text.ActiveInHierarchy() ||
                text.opacity <= 0.0f || text.ResolvedText().empty())
            {
                return false;
            }

            const DirectX::XMFLOAT4 source_rect = rect.ResolvedRect();
            const DirectX::XMFLOAT4 expansion = effects.ExpandBounds(
                source_rect.z * scale, source_rect.w * scale, asset_database);
            // Text も Image と同じ扱い。確保量は実ピクセルなので、
            // 論理単位の composite_rect へ足す前に拡大率で割り戻す。
            const float inverse_scale = 1.0f / (std::max)(0.0001f, scale);
            const float expand_left = expansion.x * inverse_scale;
            const float expand_top = expansion.y * inverse_scale;
            const float expand_right = expansion.z * inverse_scale;
            const float expand_bottom = expansion.w * inverse_scale;
            const float expanded_width = (std::max)(1.0f,
                source_rect.z + expand_left + expand_right);
            const float expanded_height = (std::max)(1.0f,
                source_rect.w + expand_top + expand_bottom);
            const std::uint32_t rt_width = static_cast<std::uint32_t>(
                std::ceil((std::max)(1.0f, expanded_width * scale)));
            const std::uint32_t rt_height = static_cast<std::uint32_t>(
                std::ceil((std::max)(1.0f, expanded_height * scale)));
            UIRenderTarget* target = render_target_pool_.Acquire(rt_width, rt_height);
            UIRenderTarget* scratch = render_target_pool_.Acquire(rt_width, rt_height);
            if (target == nullptr || scratch == nullptr ||
                !target->rtv || !target->srv || !scratch->rtv || !scratch->srv)
            {
                return false;
            }

            ID3D11RenderTargetView* previous_rtv = nullptr;
            ID3D11DepthStencilView* previous_dsv = nullptr;
            context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
            UINT viewport_count = 1;
            D3D11_VIEWPORT previous_viewport{};
            context->RSGetViewports(&viewport_count, &previous_viewport);

            const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            configure_effect_target(*target);
            context->ClearRenderTargetView(target->rtv.Get(), clear);

            font_atlas.BuildGlyphs(text, source_rect.z, source_rect.w, asset_database);
            const DirectX::XMFLOAT4 color =
                MultiplyAlpha(text.color, text.opacity * opacity);
            DirectX::XMFLOAT4X4 identity{};
            DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
            const auto draw_text_source = [&]()
            {
                append_text_glyphs(object, text, expansion, identity, color, scale);
                configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0, true,
                    text.outline_width, text.outline_color,
                    text.shadow_offset, text.shadow_color);
                Flush(context, font_atlas.Texture(), states.blend_alpha, states, nullptr);
            };

            draw_text_source();

            ID3D11ShaderResourceView* null_srv = nullptr;
            context->PSSetShaderResources(0, 1, &null_srv);
            UIRenderTarget* current = target;
            apply_effect_passes(effects, current, target, scratch);

            context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
            if (viewport_count > 0) context->RSSetViewports(1, &previous_viewport);
            if (previous_rtv != nullptr) previous_rtv->Release();
            if (previous_dsv != nullptr) previous_dsv->Release();

            constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                &constants, 0, 0);
            draw_target_height = screen_height;

