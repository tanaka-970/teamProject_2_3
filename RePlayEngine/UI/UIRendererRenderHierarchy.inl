// UI Render の子階層再帰、Canvas 列挙、最終状態復元。
// UIRenderer::Render() 本文の連続断片。外部から直接 include しない。

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
            Flush(context, current->srv.Get(), states.blend_alpha, states, scissor);
            return true;
        };

        std::function<void(Core::GameObject&, float, float, const D3D11_RECT*, int, bool, bool)>
            render_object;
        render_object = [&](Core::GameObject& object, float scale, float opacity,
            const D3D11_RECT* inherited_scissor, int depth, bool backdrop_allowed,
            bool skip_current_group_effect)
        {
            if (depth > maximum_ui_depth || object.PendingDestroy() || !object.ActiveInHierarchy())
                return;

            RectTransformComponent* rect = object.GetComponent<RectTransformComponent>();
            UIEffectStackComponent* effects = object.GetComponent<UIEffectStackComponent>();
            D3D11_RECT local_scissor{};
            const D3D11_RECT* active_scissor = inherited_scissor;
            const UIMaskComponent* special_mask = nullptr;
            bool render_self = true;

            if (rect != nullptr)
            {
                if (const UIMaskComponent* mask = object.GetComponent<UIMaskComponent>())
                {
                    if (mask->ActiveInHierarchy() && mask->enabled_mask)
                    {
                        if (mask->mask_mode == UIMaskComponent::Rectangle)
                        {
                            local_scissor = MakeScissor(*rect, scale,
                                screen_width, screen_height);
                            if (inherited_scissor != nullptr)
                                local_scissor = IntersectScissor(*inherited_scissor,
                                    local_scissor);
                            active_scissor = &local_scissor;
                        }
                        else
                        {
                            // 画像／形状 Mask は既存 Effect Stack の Mask pass へ送る。
                            // ここでは新しい子描画経路を作らず、下の offscreen 合成だけを行う。
                            special_mask = mask;
                        }
                        render_self = mask->show_mask_graphic;
                    }
                }
            }

            if (rect != nullptr && effects != nullptr && effects->enabled &&
                effects->target_scope == UIEffectStackComponent::Subtree &&
                !skip_current_group_effect && effects->HasActiveEffects(asset_database))
            {
                // After Effects の Precompose と同じ扱い。既存 UIRenderTargetPool と
                // render_object を再利用し、別の合成基盤は作らない。
                const std::uint32_t target_width = static_cast<std::uint32_t>(
                    (std::max)(1.0f, std::ceil(screen_width * scale)));
                const std::uint32_t target_height = static_cast<std::uint32_t>(
                    (std::max)(1.0f, std::ceil(screen_height * scale)));
                UIRenderTarget* target = render_target_pool_.Acquire(target_width, target_height);
                UIRenderTarget* scratch = render_target_pool_.Acquire(target_width, target_height);
                if (target != nullptr && scratch != nullptr && target->rtv && target->srv &&
                    scratch->rtv && scratch->srv)
                {
                    ID3D11RenderTargetView* previous_rtv = nullptr;
                    ID3D11DepthStencilView* previous_dsv = nullptr;
                    context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
                    UINT viewport_count = 1;
                    D3D11_VIEWPORT previous_viewport{};
                    context->RSGetViewports(&viewport_count, &previous_viewport);
                    const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                    configure_effect_target(*target);
                    context->ClearRenderTargetView(target->rtv.Get(), clear);
                    render_object(object, scale, opacity, inherited_scissor, depth,
                        false, true);
                    UIRenderTarget* current = target;
                    apply_effect_passes(*effects, current, target, scratch);

                    context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
                    if (viewport_count > 0) context->RSSetViewports(1, &previous_viewport);
                    if (previous_rtv != nullptr) previous_rtv->Release();
                    if (previous_dsv != nullptr) previous_dsv->Release();
                    constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
                    context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                        &constants, 0, 0);
                    draw_target_height = screen_height;
                    DirectX::XMFLOAT4X4 identity{};
                    DirectX::XMStoreFloat4x4(&identity, DirectX::XMMatrixIdentity());
                    append_quad({ 0.0f, 0.0f, screen_width, screen_height }, identity,
                        { 0.0f, 0.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }, scale);
                    configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                        false, 0.0f, {}, {}, {});
                    Flush(context, current->srv.Get(), states.blend_alpha, states,
                        inherited_scissor);
                    return;
                }
            }

            if (rect != nullptr && render_self)
            {
                // Subtree capture 中の root では、この Stack を Self に二重適用しない。
                UIEffectStackComponent* self_effects =
                    (skip_current_group_effect && effects != nullptr &&
                        effects->target_scope == UIEffectStackComponent::Subtree)
                    ? nullptr : effects;
                if (UIShapeComponent* shape = object.GetComponent<UIShapeComponent>())
                {
                    const bool backdrop_rendered = backdrop_allowed && self_effects != nullptr &&
                        self_effects->capture_backdrop &&
                        render_shape_effect_with_backdrop(*self_effects, *shape, *rect, scale,
                            opacity, active_scissor);
                    if (!backdrop_rendered && (self_effects == nullptr ||
                        !render_shape_effect_preview(*self_effects, *shape, *rect, scale,
                            opacity, active_scissor)))
                    {
                        render_shape(*shape, *rect, scale, opacity, active_scissor);
                    }
                }
                if (UIImageComponent* image = object.GetComponent<UIImageComponent>())
                {
                    const UIPuppetDeformComponent* puppet =
                        object.GetComponent<UIPuppetDeformComponent>();
                    const bool backdrop_rendered = backdrop_allowed && self_effects != nullptr &&
                        self_effects->capture_backdrop &&
                        render_image_effect_with_backdrop(*self_effects, *image, *rect, scale,
                            opacity, active_scissor, puppet);
                    if (!backdrop_rendered && (self_effects == nullptr ||
                        !render_effect_preview(*self_effects, *image, *rect, scale,
                            opacity, active_scissor, puppet)))
                    {
                        render_image(*image, *rect, scale, opacity, active_scissor, puppet);
                    }
                }
                if (UITextComponent* text = object.GetComponent<UITextComponent>())
                {
                    const bool backdrop_rendered = backdrop_allowed && self_effects != nullptr &&
                        self_effects->capture_backdrop &&
                        render_text_effect_with_backdrop(object, *self_effects, *text, *rect,
                            scale, opacity, active_scissor);
                    if (!backdrop_rendered && (self_effects == nullptr ||
                        !render_text_effect_preview(object, *self_effects, *text, *rect, scale,
                            opacity, active_scissor)))
                    {
                        render_text(object, *text, *rect, scale, opacity, active_scissor);
                    }
                }
                render_focus_outline(object, *rect, scale, opacity, active_scissor);
                render_scrollbars(object, *rect, scale, opacity, active_scissor);
            }

            if (special_mask != nullptr)
            {
                const std::uint32_t target_width = static_cast<std::uint32_t>(
                    (std::max)(1.0f, std::ceil(screen_width * scale)));
                const std::uint32_t target_height = static_cast<std::uint32_t>(
                    (std::max)(1.0f, std::ceil(screen_height * scale)));
                UIRenderTarget* target = render_target_pool_.Acquire(
                    target_width, target_height);
                UIRenderTarget* scratch = render_target_pool_.Acquire(
                    target_width, target_height);

                if (target != nullptr && scratch != nullptr && target->rtv &&
                    target->srv && scratch->rtv && scratch->srv)
                {
                    ID3D11RenderTargetView* previous_rtv = nullptr;
                    ID3D11DepthStencilView* previous_dsv = nullptr;
                    context->OMGetRenderTargets(1, &previous_rtv, &previous_dsv);
                    UINT viewport_count = 1;
                    D3D11_VIEWPORT previous_viewport{};
                    context->RSGetViewports(&viewport_count, &previous_viewport);

                    const FLOAT clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                    configure_effect_target(*target);
                    context->ClearRenderTargetView(target->rtv.Get(), clear);
                     std::vector<Core::GameObject*> ordered_children = object.Children();
                     std::stable_sort(ordered_children.begin(), ordered_children.end(),
                         [](const Core::GameObject* lhs, const Core::GameObject* rhs)
                         {
                             const RectTransformComponent* a = lhs != nullptr
                                 ? lhs->GetComponent<RectTransformComponent>() : nullptr;
                             const RectTransformComponent* b = rhs != nullptr
                                 ? rhs->GetComponent<RectTransformComponent>() : nullptr;
                             return (a != nullptr ? a->sort_order : 0) <
                                 (b != nullptr ? b->sort_order : 0);
                         });
                     for (Core::GameObject* child : ordered_children)
                     {
                         if (child != nullptr)
                             render_object(*child, scale, opacity,
                                inherited_scissor, depth + 1, false, false);
                    }

                    // Object Alpha / Luma は参照 GameObject/Subtree を同じ UIRenderer で
                    // 一時 RT に描く。複数Matteは既存EffectChainで Add/Subtract/Intersect
                    // してから、最後に既存Mask Effectの t1 へ渡す。
                    UIRenderTarget* matte_target = nullptr;
                    const bool object_matte =
                        special_mask->mask_mode == UIMaskComponent::ObjectAlpha ||
                        special_mask->mask_mode == UIMaskComponent::ObjectLuma;
                    if (object_matte)
                    {
                        Scene::Scene* scene = object.GetScene();
                        std::vector<std::pair<Reflection::ObjectReference, int>> matte_references;
                        if (special_mask->mask_object.IsAssigned())
                            matte_references.push_back({ special_mask->mask_object,
                                UIMaskComponent::MatteAdd });
                        for (std::size_t matte_index = 0;
                            matte_index < special_mask->matte_objects.size(); ++matte_index)
                        {
                            const Reflection::ObjectReference& reference =
                                special_mask->matte_objects[matte_index];
                            if (!reference.IsAssigned()) continue;
                            int operation = matte_index < special_mask->matte_operations.size()
                                ? special_mask->matte_operations[matte_index]
                                : UIMaskComponent::MatteAdd;
                            operation = (std::max)(
                                static_cast<int>(UIMaskComponent::MatteAdd),
                                (std::min)(
                                    static_cast<int>(UIMaskComponent::MatteIntersect),
                                    operation));
                            matte_references.push_back({ reference, operation });
                        }

                        UIRenderTarget* matte_a = nullptr;
                        UIRenderTarget* matte_b = nullptr;
                        UIRenderTarget* matte_source = nullptr;
                        const auto render_matte_object = [&](const Reflection::ObjectReference& reference,
                            UIRenderTarget*& destination) -> bool
                        {
                            Core::GameObject* matte_object = scene != nullptr
                                ? scene->FindGameObjectByID(reference.object) : nullptr;
                            if (matte_object == nullptr || matte_object == &object) return false;
                            if (destination == nullptr)
                                destination = render_target_pool_.Acquire(target_width, target_height);
                            if (destination == nullptr || !destination->rtv || !destination->srv)
                                return false;

                            configure_effect_target(*destination);
                            context->ClearRenderTargetView(destination->rtv.Get(), clear);
                            render_object(*matte_object, scale, opacity, inherited_scissor,
                                depth + 1, false, false);
                            ID3D11ShaderResourceView* null_srvs[2]{};
                            context->PSSetShaderResources(0, 2, null_srvs);
                            return true;
                        };

                        bool have_matte = false;
                        for (std::size_t matte_index = 0;
                            matte_index < matte_references.size(); ++matte_index)
                        {
                            const auto& matte_entry = matte_references[matte_index];
                            if (!have_matte)
                            {
                                if (render_matte_object(matte_entry.first, matte_a))
                                {
                                    matte_target = matte_a;
                                    have_matte = true;
                                }
                                continue;
                            }

                            if (!render_matte_object(matte_entry.first, matte_source))
                                continue;
                            if (matte_b == nullptr)
                                matte_b = render_target_pool_.Acquire(target_width, target_height);
                            if (matte_b == nullptr || !matte_b->rtv || !matte_b->srv)
                                continue;

                            UIEffectStackComponent combine_effects;
                            UI::UIEffect combine_effect;
                            combine_effect.kind =
                                static_cast<int>(UI::UIEffectKind::MatteComposite);
                            combine_effect.mask = "__runtime_ui_matte";
                            combine_effect.amount =
                                static_cast<float>(matte_entry.second);
                            combine_effects.effects.push_back(combine_effect);
                            UIRenderTarget* combined = matte_target;
                            apply_effect_passes(combine_effects, combined,
                                matte_a, matte_b, matte_source->srv.Get(),
                                special_mask->mask_mode == UIMaskComponent::ObjectLuma,
                                false);
                            matte_target = combined;
                        }
                    }

                    UIEffectStackComponent mask_effects;
                    UI::UIEffect mask_effect;
                    mask_effect.kind = static_cast<int>(UI::UIEffectKind::Mask);
                    mask_effect.softness = Clamp01(special_mask->softness);
                    const DirectX::XMFLOAT4 mask_rect = rect != nullptr
                        ? rect->ResolvedRect() : DirectX::XMFLOAT4{};
                    mask_effect.direction = {
                        (mask_rect.x + mask_rect.z * 0.5f) /
                            (std::max)(1.0f, screen_width),
                        (mask_rect.y + mask_rect.w * 0.5f) /
                            (std::max)(1.0f, screen_height) };
                    mask_effect.seed = mask_rect.z /
                        (std::max)(1.0f, screen_width) * 0.5f;
                    mask_effect.speed = mask_rect.w /
                        (std::max)(1.0f, screen_height) * 0.5f;
                    if (special_mask->mask_mode == UIMaskComponent::Shape)
                    {
                        mask_effect.amount = 1.0f;
                    }
                    else if (object_matte && matte_target != nullptr && matte_target->srv)
                    {
                        mask_effect.mask = "__runtime_ui_matte";
                    }
                    else
                    {
                        mask_effect.mask = special_mask->mask_image.guid;
                    }
                    mask_effects.effects.push_back(mask_effect);
                    UIRenderTarget* current = target;
                    apply_effect_passes(mask_effects, current, target, scratch,
                        matte_target != nullptr ? matte_target->srv.Get() : nullptr,
                        special_mask->mask_mode == UIMaskComponent::ObjectLuma,
                        special_mask->invert);

                    context->OMSetRenderTargets(1, &previous_rtv, previous_dsv);
                    if (viewport_count > 0)
                        context->RSSetViewports(1, &previous_viewport);
                    if (previous_rtv != nullptr) previous_rtv->Release();
                    if (previous_dsv != nullptr) previous_dsv->Release();

                    constants.screen_size = {
                        screen_width, screen_height, 0.0f, 0.0f };
                    context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                        &constants, 0, 0);
                    draw_target_height = screen_height;
                    DirectX::XMFLOAT4X4 identity{};
                    DirectX::XMStoreFloat4x4(&identity,
                        DirectX::XMMatrixIdentity());
                    append_quad({ 0.0f, 0.0f, screen_width, screen_height },
                        identity, { 0.0f, 0.0f, 1.0f, 1.0f },
                        { 1.0f, 1.0f, 1.0f, 1.0f }, scale);
                    configure_visual({}, 0, 0.0f, { 0.5f, 0.5f }, {}, 0,
                        false, 0.0f, {}, {}, {});
                    Flush(context, current->srv.Get(), states.blend_alpha,
                        states, inherited_scissor);
                    return;
                }
            }

             std::vector<Core::GameObject*> ordered_children = object.Children();
             std::stable_sort(ordered_children.begin(), ordered_children.end(),
                 [](const Core::GameObject* lhs, const Core::GameObject* rhs)
                 {
                     const RectTransformComponent* a = lhs != nullptr
                         ? lhs->GetComponent<RectTransformComponent>() : nullptr;
                     const RectTransformComponent* b = rhs != nullptr
                         ? rhs->GetComponent<RectTransformComponent>() : nullptr;
                     return (a != nullptr ? a->sort_order : 0) <
                         (b != nullptr ? b->sort_order : 0);
                 });
             for (Core::GameObject* child : ordered_children)
             {
                 if (child != nullptr)
                     render_object(*child, scale, opacity, active_scissor, depth + 1,
                        backdrop_allowed, false);
            }
        };

        for (Core::GameObject* canvas_object : canvases)
        {
            if (canvas_object == nullptr) continue;
            CanvasComponent* canvas = canvas_object->GetComponent<CanvasComponent>();
            if (canvas == nullptr || !canvas->ActiveInHierarchy()) continue;

            const float scale = UILayout::CanvasScale(*canvas, screen_width, screen_height);
            const float safe_scale = scale > 0.0001f ? scale : 1.0f;
            // UILayout の解決矩形は共通のまま、World Space だけを Canvas の
            // ワールド変換とカメラ行列で投影する。平面の高さを 1 とし、
            // reference_resolution の比率で幅を決める。
            world_space_canvas_ = canvas->render_mode == CanvasComponent::WorldSpace;
            if (world_space_canvas_)
            {
                const float reference_width = canvas->reference_resolution.x > 0.0f
                    ? canvas->reference_resolution.x : 1920.0f;
                const float reference_height = canvas->reference_resolution.y > 0.0f
                    ? canvas->reference_resolution.y : 1080.0f;
                constants.world_canvas_params = {
                    1.0f, reference_width / reference_height, 1.0f, 0.0f };
                DirectX::XMStoreFloat4x4(&constants.world_canvas_matrix,
                    canvas_object->GetTransform().WorldMatrix());
            }
            else
            {
                constants.world_canvas_params = { 0.0f, 0.0f, 0.0f, 0.0f };
                DirectX::XMStoreFloat4x4(&constants.world_canvas_matrix,
                    DirectX::XMMatrixIdentity());
            }
            context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr,
                &constants, 0, 0);
            render_object(*canvas_object, safe_scale,
                (std::min)((std::max)(canvas->opacity, 0.0f), 1.0f), nullptr, 0, true, false);
        }
        world_space_canvas_ = false;

        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
        context->RSSetScissorRects(0, nullptr);
