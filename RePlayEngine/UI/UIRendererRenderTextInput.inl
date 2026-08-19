// UI Render の Text/InputField/選択表示と直接描画経路。
// UIRenderer::Render() 本文の連続断片。外部から直接 include しない。

                    const float radial = base_radius +
                        amplitude * std::cos(lobes * theta);
                    const float angle = theta + rotation;
                    shape_path.push_back({
                        cx + std::cos(angle) * rx * radial,
                        cy + std::sin(angle) * ry * radial });
                }
                break;
            }
            default:
            {
                const float radius = (std::min)(
                    (std::max)(0.0f, shape.corner_radius),
                    (std::min)(std::fabs(rect.z), std::fabs(rect.w)) * 0.5f);
                if (radius <= 0.001f)
                {
                    shape_path.push_back({ rect.x, rect.y });
                    shape_path.push_back({ rect.x + rect.z, rect.y });
                    shape_path.push_back({ rect.x + rect.z, rect.y + rect.w });
                    shape_path.push_back({ rect.x, rect.y + rect.w });
                }
                else
                {
                    append_arc(rect.x + rect.z - radius, rect.y + radius,
                        radius, radius, -pi * 0.5f, 0.0f, 8);
                    append_arc(rect.x + rect.z - radius, rect.y + rect.w - radius,
                        radius, radius, 0.0f, pi * 0.5f, 8);
                    append_arc(rect.x + radius, rect.y + rect.w - radius,
                        radius, radius, pi * 0.5f, pi, 8);
                    append_arc(rect.x + radius, rect.y + radius,
                        radius, radius, pi, pi * 1.5f, 8);
                }
                break;
            }
            }
        };

        const auto append_stroked_path = [&](const UIShapeComponent& shape,
            const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& color,
            float stroke_width, float scale, bool closed)
        {
            const std::size_t point_count = shape_path.size();
            if (point_count < 2 || stroke_width <= 0.0f || color.w <= 0.0f) return;
            const std::size_t segment_count = closed ? point_count : point_count - 1;
            if (segment_count == 0) return;

            shape_lengths.clear();
            shape_lengths.reserve(segment_count + 1);
            shape_lengths.push_back(0.0f);
            for (std::size_t segment = 0; segment < segment_count; ++segment)
            {
                const DirectX::XMFLOAT2& a = shape_path[segment];
                const DirectX::XMFLOAT2& b = shape_path[(segment + 1) % point_count];
                shape_lengths.push_back(shape_lengths.back() + local_distance(a, b));
            }
            const float total_length = shape_lengths.back();
            if (total_length <= 0.001f) return;

            const float base_start = Clamp01(shape.trim_start);
            const float base_end = Clamp01(shape.trim_end);
            const float span = (std::max)(0.0f, base_end - base_start);
            if (span <= 0.0f) return;

            float interval_starts[2]{ 0.0f, 0.0f };
            float interval_ends[2]{ 0.0f, 0.0f };
            int interval_count = 0;
            if (span >= 0.9999f)
            {
                interval_starts[interval_count] = 0.0f;
                interval_ends[interval_count] = total_length;
                ++interval_count;
            }
            else
            {
                float start = std::fmod(base_start + shape.trim_offset, 1.0f);
                if (start < 0.0f) start += 1.0f;
                const float end = start + span;
                interval_starts[interval_count] = start * total_length;
                interval_ends[interval_count] = (std::min)(end, 1.0f) * total_length;
                ++interval_count;
                if (end > 1.0f)
                {
                    interval_starts[interval_count] = 0.0f;
                    interval_ends[interval_count] = (end - 1.0f) * total_length;
                    ++interval_count;
                }
            }

            const float dash_length = (std::max)(0.0f, shape.dash_length);
            const float dash_gap = (std::max)(0.0f, shape.dash_gap);
            const float dash_pattern = dash_length + dash_gap;
            const bool dashed = dash_length > 0.0f && dash_gap > 0.0f;

            const auto emit_segment = [&](const DirectX::XMFLOAT2& a,
                const DirectX::XMFLOAT2& b, float abs_a, float abs_b)
            {
                if (abs_b <= abs_a) return;
                if (!dashed)
                {
                    append_line_segment_local(a, b, matrix, color, stroke_width, scale,
                        abs_a / total_length, abs_b / total_length);
                    return;
                }

                float cursor = abs_a;
                int guard = 0;
                while (cursor < abs_b && guard++ < 256)
                {
                    float phase = std::fmod(cursor + shape.dash_offset, dash_pattern);
                    if (phase < 0.0f) phase += dash_pattern;
                    if (phase < dash_length)
                    {
                        const float step = (std::min)(abs_b - cursor,
                            dash_length - phase);
                        const float next = cursor + step;
                        const float ta = (cursor - abs_a) / (abs_b - abs_a);
                        const float tb = (next - abs_a) / (abs_b - abs_a);
                        append_line_segment_local(lerp_point(a, b, ta),
                            lerp_point(a, b, tb), matrix, color, stroke_width, scale,
                            cursor / total_length, next / total_length);
                        cursor = next;
                    }
                    else
                    {
                        cursor += (std::min)(abs_b - cursor, dash_pattern - phase);
                    }
                }
            };

            for (std::size_t segment = 0; segment < segment_count; ++segment)
            {
                const float segment_start = shape_lengths[segment];
                const float segment_end = shape_lengths[segment + 1];
                if (segment_end <= segment_start) continue;

                const DirectX::XMFLOAT2& a = shape_path[segment];
                const DirectX::XMFLOAT2& b = shape_path[(segment + 1) % point_count];
                for (int interval = 0; interval < interval_count; ++interval)
                {
                    const float clipped_start =
                        (std::max)(segment_start, interval_starts[interval]);
                    const float clipped_end =
                        (std::min)(segment_end, interval_ends[interval]);
                    if (clipped_end <= clipped_start) continue;

                    const float ta = (clipped_start - segment_start) /
                        (segment_end - segment_start);
                    const float tb = (clipped_end - segment_start) /
                        (segment_end - segment_start);
                    emit_segment(lerp_point(a, b, ta), lerp_point(a, b, tb),
                        clipped_start, clipped_end);
                }
            }
        };

        const auto render_image = [&](UIImageComponent& image,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!image.ActiveInHierarchy() || image.opacity <= 0.0f || image.fill_amount <= 0.0f)
                return;

            DirectX::XMFLOAT4 draw_rect = rect.ResolvedRect();
            DirectX::XMFLOAT4 uv{ image.uv_offset.x, image.uv_offset.y,
                image.uv_scale.x, image.uv_scale.y };
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

            append_quad(draw_rect, rect.ResolvedMatrix(), uv,
                MultiplyAlpha(image.color, image.opacity * opacity), scale);
            configure_visual(image.fill_color_2, image.fill_mode, image.fill_angle,
                image.fill_center, image.stroke_color_2, image.stroke_mode,
                false, 0.0f, {}, {}, {});
            Flush(context, TextureFor(image.sprite.guid, asset_database),
                BlendForImage(image, states), states, scissor);
        };

        const auto render_shape = [&](UIShapeComponent& shape,
            const RectTransformComponent& rect, float scale, float opacity,
            const D3D11_RECT* scissor)
        {
            if (!shape.ActiveInHierarchy() || opacity <= 0.0f) return;

            const DirectX::XMFLOAT4 r = rect.ResolvedRect();
            bool closed = true;
            build_shape_path(shape, r, scale, closed);
            if (shape_path.empty()) return;

            const DirectX::XMFLOAT4X4 matrix = rect.ResolvedMatrix();
            const bool has_fill = closed && shape.shape != UIShapeComponent::Line &&
                shape.fill_color.w * opacity > 0.0f && shape_path.size() >= 3;
            const bool split_draws = shape.fill_mode != UIShapeComponent::Solid ||
                shape.stroke_mode != UIShapeComponent::StrokeSolid;
            if (has_fill)
            {
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

                const DirectX::XMFLOAT4 fill =
                    MultiplyAlpha(shape.fill_color, opacity);
                configure_visual(shape.fill_color_2, shape.fill_mode,
                    shape.fill_angle, shape.fill_center,
                    shape.stroke_color_2, shape.stroke_mode,
                    false, 0.0f, {}, {}, {});
                visual_constants_.fill_color_3 = shape.fill_color_3;
                visual_constants_.fill_color_4 = shape.fill_color_4;
                visual_constants_.fill_stops = {
                    shape.fill_stop_2, shape.fill_stop_3,
                    shape.fill_stop_4, 0.0f };
                for (std::size_t index = 0; index < shape_path.size(); ++index)
                {
                    append_triangle_local(center, shape_path[index],
                        shape_path[(index + 1) % shape_path.size()],
                        r, matrix, fill, scale);
                }
                if (split_draws)
                {
                    Flush(context, white_texture_.Get(), states.blend_alpha,
                        states, scissor);
                }
            }

            float stroke_width = shape.stroke_width;
            DirectX::XMFLOAT4 stroke = shape.stroke_color;
            if (shape.shape == UIShapeComponent::Line && stroke_width <= 0.0f)
            {
                stroke_width = 1.0f;
                stroke = shape.fill_color;
            }
            configure_visual(shape.fill_color_2, UIShapeComponent::Solid,
                0.0f, { 0.5f, 0.5f }, shape.stroke_color_2,
                shape.stroke_mode, false, 0.0f, {}, {}, {});
            append_stroked_path(shape, matrix, MultiplyAlpha(stroke, opacity),
                stroke_width, scale, closed);
            if (!split_draws || !has_fill)
            {
                Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
            }
            else if (!vertices_.empty())
            {
                Flush(context, white_texture_.Get(), states.blend_alpha, states, scissor);
            }
        };

        const auto animator_anchor = [](int anchor) noexcept
        {
            switch (anchor)
            {
            case UITextAnimatorComponent::BaselineLeft: return DirectX::XMFLOAT2{ 0.0f, 0.5f };
            case UITextAnimatorComponent::BaselineCenter: return DirectX::XMFLOAT2{ 0.5f, 0.5f };
            case UITextAnimatorComponent::TopLeft: return DirectX::XMFLOAT2{ 0.0f, 0.0f };
            case UITextAnimatorComponent::BottomCenter: return DirectX::XMFLOAT2{ 0.5f, 1.0f };
            default: return DirectX::XMFLOAT2{ 0.5f, 0.5f };
            }
        };

        std::vector<const UITextAnimatorComponent*> text_animators;
        const auto append_text_glyphs = [&](const Core::GameObject& object,
            UITextComponent& text, const DirectX::XMFLOAT4& origin,
            const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& base_color,
            float scale)
        {
            GatherTextAnimators(object, text_animators);
            const std::vector<UITextComponent::GlyphQuad>& glyphs = text.Glyphs();
            const float glyph_count = (std::max)(1.0f,
                static_cast<float>(text.DisplayCharacterCount()));
            const float outline_extent = clamp_outline_width(text.outline_width);
            const float shadow_extent_x = (std::max)(0.0f,
                std::fabs(text.shadow_offset.x));
            const float shadow_extent_y = (std::max)(0.0f,
                std::fabs(text.shadow_offset.y));
            const bool has_text_effect = outline_extent > 0.0f ||
                text.shadow_color.w > 0.0f;

            for (const UITextComponent::GlyphQuad& glyph : glyphs)
            {
                DirectX::XMFLOAT4 glyph_rect{
                    origin.x + glyph.position.x,
                    origin.y + glyph.position.y,
                    glyph.size.x,
                    glyph.size.y
                };
                DirectX::XMFLOAT4 color{
                    base_color.x * glyph.rich_color.x,
                    base_color.y * glyph.rich_color.y,
                    base_color.z * glyph.rich_color.z,
                    base_color.w * glyph.rich_color.w };
                DirectX::XMFLOAT2 local_scale{
                    glyph.rich_bold ? 1.035f : 1.0f,
                    glyph.rich_bold ? 1.035f : 1.0f };
                DirectX::XMFLOAT2 anchor{ 0.5f, 0.5f };
                float rotation = 0.0f;
                bool transformed = glyph.rich_bold || glyph.rich_italic;
                const float rich_italic_shear = glyph.rich_italic ? -0.18f : 0.0f;

                for (const UITextAnimatorComponent* animator : text_animators)
                {
                    const float position =
                        (static_cast<float>(glyph.character_index) + 0.5f) / glyph_count;
                    const float influence = RangeInfluence(*animator, position);
                    if (influence <= 0.0f) continue;

                    glyph_rect.x += animator->position_offset.x * influence;
                    glyph_rect.y += animator->position_offset.y * influence;
                    glyph_rect.x += animator->character_spacing *
                        static_cast<float>(glyph.character_index) * influence;
                    glyph_rect.x += animator->random_position.x *
                        RandomSigned(animator->random_seed, glyph.character_index, 11u) *
                        influence;
                    glyph_rect.y += animator->random_position.y *
                        RandomSigned(animator->random_seed, glyph.character_index, 23u) *
                        influence;

                    rotation += animator->rotation * influence;
                    rotation += animator->random_rotation *
                        RandomSigned(animator->random_seed, glyph.character_index, 37u) *
                        influence;
                    local_scale.x *= Lerp(1.0f, animator->scale.x, influence);
                    local_scale.y *= Lerp(1.0f, animator->scale.y, influence);
                    color = LerpColor(color,
                        { base_color.x * glyph.rich_color.x * animator->color.x,
                          base_color.y * glyph.rich_color.y * animator->color.y,
                          base_color.z * glyph.rich_color.z * animator->color.z,
                          base_color.w * glyph.rich_color.w * animator->color.w },
                        influence);
                    color.w *= Lerp(1.0f, animator->opacity, influence);
                    anchor = animator_anchor(animator->anchor);
                    transformed = true;
                }

                const DirectX::XMFLOAT4 glyph_uv_bounds{
                    glyph.uv.x, glyph.uv.y,
                    glyph.uv.x + glyph.uv.z, glyph.uv.y + glyph.uv.w };
                DirectX::XMFLOAT4 draw_rect = glyph_rect;
                DirectX::XMFLOAT4 draw_uv = glyph.uv;
                if (has_text_effect)
                {
                    // outline_width / shadow_offset は画面ピクセルの値なので、
                    // CPU 側ではエフェクトを収める分だけクアッドを広げる。
                    // 実際のしきい値と UV の変化量はシェーダー側で計算するため、
                    // Text Animator の回転・拡縮でもサンプル方向を固定しない。
                    const float safe_canvas_scale = (std::max)(
                        std::fabs(scale), 0.0001f);
                    const float safe_local_scale_x = (std::max)(
                        std::fabs(local_scale.x), 0.0001f);
                    const float safe_local_scale_y = (std::max)(
                        std::fabs(local_scale.y), 0.0001f);
                    const float expand_x = (outline_extent + shadow_extent_x) /
                        (safe_canvas_scale * safe_local_scale_x);
                    const float expand_y = (outline_extent + shadow_extent_y) /
                        (safe_canvas_scale * safe_local_scale_y);
                    draw_rect.x -= expand_x;
                    draw_rect.y -= expand_y;
                    draw_rect.z += expand_x * 2.0f;
                    draw_rect.w += expand_y * 2.0f;
                    const float safe_glyph_width = (std::max)(
                        std::fabs(glyph_rect.z), 0.0001f);
                    const float safe_glyph_height = (std::max)(
                        std::fabs(glyph_rect.w), 0.0001f);
                    const float uv_expand_x = expand_x / safe_glyph_width * glyph.uv.z;
                    const float uv_expand_y = expand_y / safe_glyph_height * glyph.uv.w;
                    draw_uv.x -= uv_expand_x;
                    draw_uv.y -= uv_expand_y;
                    draw_uv.z += uv_expand_x * 2.0f;
                    draw_uv.w += uv_expand_y * 2.0f;
                }

                if (transformed)
                {
                    append_quad_local_with_bounds(draw_rect, matrix, draw_uv, color,
                        scale, local_scale, rotation, anchor, rich_italic_shear,
                        glyph_uv_bounds);
                }
                else
                {
                    append_quad_with_bounds(draw_rect, matrix, draw_uv, color, scale,
                        glyph_uv_bounds);
                }
            }
        };

        const auto render_text = [&](const Core::GameObject& object,
            UITextComponent& text, const RectTransformComponent& rect, float scale,
