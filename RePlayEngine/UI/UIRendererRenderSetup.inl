// UI Render の Canvas 収集・共通定数・基本 Shape/Text 描画前半。
// UIRenderer::Render() 本文の連続断片。外部から直接 include しない。

        if (context == nullptr || device_ == nullptr || !vertex_shader_ || !pixel_shader_)
            return;

        Constants constants{};
        constants.screen_size = { screen_width, screen_height, 0.0f, 0.0f };
        constants.world_view_projection = states.world_view_projection;
        context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);
        render_target_pool_.BeginFrame();
        ++render_serial_;
        if ((render_serial_ & 63ull) == 0ull) PruneTemporalHistory();

        std::vector<Core::GameObject*> canvases;
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            Core::GameObject* object = scene.GameObjectAt(index);
            if (object == nullptr || object->PendingDestroy() || !object->ActiveInHierarchy()) continue;
            if (object->GetComponent<CanvasComponent>() != nullptr) canvases.push_back(object);
        }
        std::stable_sort(canvases.begin(), canvases.end(),
            [](const Core::GameObject* lhs, const Core::GameObject* rhs)
            {
                const CanvasComponent* a = lhs != nullptr
                    ? lhs->GetComponent<CanvasComponent>() : nullptr;
                const CanvasComponent* b = rhs != nullptr
                    ? rhs->GetComponent<CanvasComponent>() : nullptr;
                return (a != nullptr ? a->sort_order : 0) <
                    (b != nullptr ? b->sort_order : 0);
            });

        float draw_target_height = screen_height;
        visual_constants_ = VisualConstants{};
        const auto clamp_outline_width = [](float outline_width) noexcept
        {
            const float non_negative = (std::max)(outline_width, 0.0f);
            return (std::min)(non_negative,
                static_cast<float>(FontAtlas::AtlasPaddingPixels()));
        };
        const auto configure_visual = [this, &clamp_outline_width](
            const DirectX::XMFLOAT4& fill_color_2,
            int fill_mode, float fill_angle, const DirectX::XMFLOAT2& fill_center,
            const DirectX::XMFLOAT4& stroke_color_2, int stroke_mode,
            bool text_mode, float outline_width,
            const DirectX::XMFLOAT4& outline_color,
            const DirectX::XMFLOAT2& shadow_offset,
            const DirectX::XMFLOAT4& shadow_color)
        {
            visual_constants_.fill_color_2 = fill_color_2;
            visual_constants_.fill_params = {
                static_cast<float>(fill_mode),
                DirectX::XMConvertToRadians(fill_angle),
                fill_center.x, fill_center.y };
            visual_constants_.stroke_color_2 = stroke_color_2;
            visual_constants_.stroke_params = {
                static_cast<float>(stroke_mode), clamp_outline_width(outline_width),
                text_mode ? 1.0f : 0.0f, 0.0f };
            visual_constants_.outline_color = outline_color;
            visual_constants_.shadow_offset = { shadow_offset.x, shadow_offset.y,
                0.0f, 0.0f };
            visual_constants_.shadow_color = shadow_color;
            // 3・4 色目は Shape が明示した描画だけで有効にする。
            // ここで戻さないと、直前の Shape の多色設定が Image / Text へ漏れる。
            visual_constants_.fill_color_3 = { 1.0f, 1.0f, 1.0f, 1.0f };
            visual_constants_.fill_color_4 = { 1.0f, 1.0f, 1.0f, 1.0f };
            visual_constants_.fill_stops = { 1.0f, -1.0f, -1.0f, 0.0f };
        };

        const auto emit_quad = [this, &draw_target_height](const DirectX::XMFLOAT4& rect,
            const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& uv,
            const DirectX::XMFLOAT4& color, float scale,
            const DirectX::XMFLOAT4& uv_bounds)
        {
            const DirectX::XMFLOAT2 p0 = ToScreenPoint(
                TransformPoint(matrix, rect.x, rect.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p1 = ToScreenPoint(
                TransformPoint(matrix, rect.x + rect.z, rect.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p2 = ToScreenPoint(
                TransformPoint(matrix, rect.x + rect.z, rect.y + rect.w), scale, draw_target_height);
            const DirectX::XMFLOAT2 p3 = ToScreenPoint(
                TransformPoint(matrix, rect.x, rect.y + rect.w), scale, draw_target_height);

            const DirectX::XMFLOAT2 uv0{ uv.x, uv.y + uv.w };
            const DirectX::XMFLOAT2 uv1{ uv.x + uv.z, uv.y + uv.w };
            const DirectX::XMFLOAT2 uv2{ uv.x + uv.z, uv.y };
            const DirectX::XMFLOAT2 uv3{ uv.x, uv.y };
            vertices_.push_back({ p0, uv0, { 0.0f, 1.0f }, color, uv_bounds });
            vertices_.push_back({ p3, uv3, { 0.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p2, uv2, { 1.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p0, uv0, { 0.0f, 1.0f }, color, uv_bounds });
            vertices_.push_back({ p2, uv2, { 1.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p1, uv1, { 1.0f, 1.0f }, color, uv_bounds });
        };

        const auto append_quad = [&emit_quad](const DirectX::XMFLOAT4& rect,
            const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& uv,
            const DirectX::XMFLOAT4& color, float scale)
        {
            const DirectX::XMFLOAT4 uv_bounds{
                uv.x, uv.y, uv.x + uv.z, uv.y + uv.w };
            emit_quad(rect, matrix, uv, color, scale, uv_bounds);
        };

        const auto append_quad_with_bounds = [&emit_quad](
            const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
            const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color,
            float scale, const DirectX::XMFLOAT4& uv_bounds)
        {
            emit_quad(rect, matrix, uv, color, scale, uv_bounds);
        };


        const auto resolve_image_source = [&](const UIImageComponent& image)
        {
            ResolvedImageSource source;
            ResolveImageSource(image, asset_database, source);
            return source;
        };

        const auto append_image_geometry = [this, &draw_target_height](
            const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
            const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color,
            float scale, const UIPuppetDeformComponent* puppet, bool rotated)
        {
            const DirectX::XMFLOAT4 uv_bounds{
                uv.x, uv.y, uv.x + uv.z, uv.y + uv.w };
            const int columns = puppet != nullptr && puppet->enabled_deform
                ? (std::max)(1, (std::min)(32, puppet->grid_columns)) : 1;
            const int rows = puppet != nullptr && puppet->enabled_deform
                ? (std::max)(1, (std::min)(32, puppet->grid_rows)) : 1;

            const auto make_vertex = [&](float nx, float ny)
            {
                DirectX::XMFLOAT2 normalized{ nx, ny };
                if (puppet != nullptr && puppet->enabled_deform)
                    normalized = puppet->DeformNormalizedPoint(normalized);
                const float local_x = rect.x + normalized.x * rect.z;
                const float local_y = rect.y + normalized.y * rect.w;
                const DirectX::XMFLOAT2 position = ToScreenPoint(
                    TransformPoint(matrix, local_x, local_y), scale, draw_target_height);

                DirectX::XMFLOAT2 texcoord{};
                if (!rotated)
                {
                    texcoord = {
                        uv.x + nx * uv.z,
                        uv.y + (1.0f - ny) * uv.w };
                }
                else
                {
                    // Atlas packer が 90 度時計回りで格納した Region を元向きへ戻す。
                    texcoord = {
                        uv.x + ny * uv.z,
                        uv.y + nx * uv.w };
                }
                return Vertex{ position, texcoord, { nx, 1.0f - ny }, color, uv_bounds };
            };

            for (int row = 0; row < rows; ++row)
            {
                const float y0 = static_cast<float>(row) / static_cast<float>(rows);
                const float y1 = static_cast<float>(row + 1) / static_cast<float>(rows);
                for (int column = 0; column < columns; ++column)
                {
                    const float x0 = static_cast<float>(column) / static_cast<float>(columns);
                    const float x1 = static_cast<float>(column + 1) / static_cast<float>(columns);
                    const Vertex p0 = make_vertex(x0, y0);
                    const Vertex p1 = make_vertex(x1, y0);
                    const Vertex p2 = make_vertex(x1, y1);
                    const Vertex p3 = make_vertex(x0, y1);
                    vertices_.push_back(p0);
                    vertices_.push_back(p3);
                    vertices_.push_back(p2);
                    vertices_.push_back(p0);
                    vertices_.push_back(p2);
                    vertices_.push_back(p1);
                }
            }
        };

        const auto emit_quad_local =
            [this, &draw_target_height](const DirectX::XMFLOAT4& rect,
                const DirectX::XMFLOAT4X4& matrix, const DirectX::XMFLOAT4& uv,
                const DirectX::XMFLOAT4& color, float scale,
                const DirectX::XMFLOAT2& local_scale, float rotation_degrees,
                const DirectX::XMFLOAT2& anchor, float shear_x,
                const DirectX::XMFLOAT4& uv_bounds)
        {
            const float pivot_x = rect.x + rect.z * anchor.x;
            const float pivot_y = rect.y + rect.w * anchor.y;
            const float radians = DirectX::XMConvertToRadians(rotation_degrees);
            const float c = std::cos(radians);
            const float s = std::sin(radians);

            const auto transform_local = [&](float x, float y)
            {
                float dx = (x - pivot_x) * local_scale.x;
                float dy = (y - pivot_y) * local_scale.y;
                dx += dy * shear_x;
                const float rx = dx * c - dy * s + pivot_x;
                const float ry = dx * s + dy * c + pivot_y;
                return ToScreenPoint(TransformPoint(matrix, rx, ry), scale,
                    draw_target_height);
            };

            const DirectX::XMFLOAT2 p0 = transform_local(rect.x, rect.y);
            const DirectX::XMFLOAT2 p1 = transform_local(rect.x + rect.z, rect.y);
            const DirectX::XMFLOAT2 p2 = transform_local(rect.x + rect.z, rect.y + rect.w);
            const DirectX::XMFLOAT2 p3 = transform_local(rect.x, rect.y + rect.w);

            const DirectX::XMFLOAT2 uv0{ uv.x, uv.y + uv.w };
            const DirectX::XMFLOAT2 uv1{ uv.x + uv.z, uv.y + uv.w };
            const DirectX::XMFLOAT2 uv2{ uv.x + uv.z, uv.y };
            const DirectX::XMFLOAT2 uv3{ uv.x, uv.y };
            vertices_.push_back({ p0, uv0, { 0.0f, 1.0f }, color, uv_bounds });
            vertices_.push_back({ p3, uv3, { 0.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p2, uv2, { 1.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p0, uv0, { 0.0f, 1.0f }, color, uv_bounds });
            vertices_.push_back({ p2, uv2, { 1.0f, 0.0f }, color, uv_bounds });
            vertices_.push_back({ p1, uv1, { 1.0f, 1.0f }, color, uv_bounds });
        };

        const auto append_quad_local = [&emit_quad_local](
            const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
            const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color, float scale,
            const DirectX::XMFLOAT2& local_scale, float rotation_degrees,
            const DirectX::XMFLOAT2& anchor)
        {
            const DirectX::XMFLOAT4 uv_bounds{
                uv.x, uv.y, uv.x + uv.z, uv.y + uv.w };
            emit_quad_local(rect, matrix, uv, color, scale, local_scale,
                rotation_degrees, anchor, 0.0f, uv_bounds);
        };

        const auto append_quad_local_with_bounds = [&emit_quad_local](
            const DirectX::XMFLOAT4& rect, const DirectX::XMFLOAT4X4& matrix,
            const DirectX::XMFLOAT4& uv, const DirectX::XMFLOAT4& color, float scale,
            const DirectX::XMFLOAT2& local_scale, float rotation_degrees,
            const DirectX::XMFLOAT2& anchor, float shear_x,
            const DirectX::XMFLOAT4& uv_bounds)
        {
            emit_quad_local(rect, matrix, uv, color, scale, local_scale,
                rotation_degrees, anchor, shear_x, uv_bounds);
        };

        const auto append_triangle_local =
            [this, &draw_target_height](const DirectX::XMFLOAT2& a,
                const DirectX::XMFLOAT2& b, const DirectX::XMFLOAT2& c,
                const DirectX::XMFLOAT4& bounds, const DirectX::XMFLOAT4X4& matrix,
                const DirectX::XMFLOAT4& color, float scale)
        {
            const DirectX::XMFLOAT2 uv{ 0.0f, 0.0f };
            const float width = (std::max)(0.0001f, std::fabs(bounds.z));
            const float height = (std::max)(0.0001f, std::fabs(bounds.w));
            const auto gradient_uv = [&bounds, width, height](
                const DirectX::XMFLOAT2& point)
            {
                return DirectX::XMFLOAT2{
                    (point.x - bounds.x) / width,
                    (point.y - bounds.y) / height };
            };
            const DirectX::XMFLOAT2 uv0 = gradient_uv(a);
            const DirectX::XMFLOAT2 uv1 = gradient_uv(b);
            const DirectX::XMFLOAT2 uv2 = gradient_uv(c);
            const DirectX::XMFLOAT2 p0 = ToScreenPoint(
                TransformPoint(matrix, a.x, a.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p1 = ToScreenPoint(
                TransformPoint(matrix, b.x, b.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p2 = ToScreenPoint(
                TransformPoint(matrix, c.x, c.y), scale, draw_target_height);
            vertices_.push_back({ p0, uv, uv0, color });
            vertices_.push_back({ p1, uv, uv1, color });
            vertices_.push_back({ p2, uv, uv2, color });
        };

        const auto append_line_segment_local =
            [this, &draw_target_height](const DirectX::XMFLOAT2& a,
                const DirectX::XMFLOAT2& b, const DirectX::XMFLOAT4X4& matrix,
                const DirectX::XMFLOAT4& color, float width, float scale,
                float gradient_u0, float gradient_u1)
        {
            const float pixel_width = width * scale;
            if (pixel_width <= 0.0f || color.w <= 0.0f) return;

            const DirectX::XMFLOAT2 p0 = ToScreenPoint(
                TransformPoint(matrix, a.x, a.y), scale, draw_target_height);
            const DirectX::XMFLOAT2 p1 = ToScreenPoint(
                TransformPoint(matrix, b.x, b.y), scale, draw_target_height);
            const float dx = p1.x - p0.x;
            const float dy = p1.y - p0.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0.001f) return;

            const float nx = -dy / length * pixel_width * 0.5f;
            const float ny = dx / length * pixel_width * 0.5f;
            const DirectX::XMFLOAT2 uv{ 0.0f, 0.0f };
            const DirectX::XMFLOAT2 q0{ p0.x - nx, p0.y - ny };
            const DirectX::XMFLOAT2 q1{ p0.x + nx, p0.y + ny };
            const DirectX::XMFLOAT2 q2{ p1.x + nx, p1.y + ny };
            const DirectX::XMFLOAT2 q3{ p1.x - nx, p1.y - ny };

            vertices_.push_back({ q0, uv, { gradient_u0, 0.0f }, color });
            vertices_.push_back({ q1, uv, { gradient_u0, 1.0f }, color });
            vertices_.push_back({ q2, uv, { gradient_u1, 1.0f }, color });
            vertices_.push_back({ q0, uv, { gradient_u0, 0.0f }, color });
            vertices_.push_back({ q2, uv, { gradient_u1, 1.0f }, color });
            vertices_.push_back({ q3, uv, { gradient_u1, 0.0f }, color });
        };

        const auto local_distance = [](const DirectX::XMFLOAT2& a,
            const DirectX::XMFLOAT2& b) noexcept
        {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        };

        const auto lerp_point = [](const DirectX::XMFLOAT2& a,
            const DirectX::XMFLOAT2& b, float t) noexcept
        {
            return DirectX::XMFLOAT2{ Lerp(a.x, b.x, t), Lerp(a.y, b.y, t) };
        };

        std::vector<DirectX::XMFLOAT2> shape_path;
        std::vector<float> shape_lengths;
        const auto build_shape_path = [&](const UIShapeComponent& shape,
            const DirectX::XMFLOAT4& rect, float scale, bool& closed)
        {
            shape_path.clear();
            closed = true;
            constexpr float pi = 3.14159265358979323846f;

            const auto append_arc = [&](float cx, float cy, float rx, float ry,
                float start, float end, int steps)
            {
                for (int step = 0; step <= steps; ++step)
                {
                    const float t = static_cast<float>(step) /
                        static_cast<float>((std::max)(1, steps));
                    const float angle = Lerp(start, end, t);
                    shape_path.push_back({
                        cx + std::cos(angle) * rx,
                        cy + std::sin(angle) * ry
                    });
                }
            };

            switch (shape.shape)
            {
            case UIShapeComponent::Circle:
            {
                const float curvature = Clamp01(shape.arc_curvature);
                if (curvature <= 0.0001f)
                {
                    // 半径無限大の極限は直線。曲率 0 を特別扱いして
                    // 1 / curvature の計算を行わない。
                    closed = false;
                    shape_path.push_back({ rect.x, rect.y + rect.w * 0.5f });
                    shape_path.push_back({ rect.x + rect.z, rect.y + rect.w * 0.5f });
                    break;
                }
                else
                {
                    // 正規化した横幅 1 の弦に対し、半径を 1 / curvature に比例
                    // させる。上下の弧をつなぐと curvature=1 で円になり、
                    // curvature が 0 へ近づくほど両方が同じ直線へ収束する。
                    const float radius = 0.5f / curvature;
                    const float half_chord_height = (std::sqrt)(
                        (std::max)(0.0f, radius * radius - 0.25f));
                    const auto to_rect = [&rect](float x, float y)
                    {
                        return DirectX::XMFLOAT2{
                            rect.x + rect.z * x,
                            rect.y + rect.w * y };
                    };

                    const float top_center_y = 0.5f + half_chord_height;
                    const float bottom_center_y = 0.5f - half_chord_height;
                    const float top_delta = (std::sqrt)(
                        (std::max)(0.0f, radius * radius - 0.25f));
                    float top_start = std::atan2(-top_delta, -0.5f);
                    if (top_start < 0.0f) top_start += 2.0f * pi;
                    float top_end = std::atan2(-top_delta, 0.5f);
                    if (top_end < 0.0f) top_end += 2.0f * pi;
                    if (top_end <= top_start) top_end += 2.0f * pi;
                    const float bottom_start = std::atan2(top_delta, 0.5f);
                    const float bottom_end = std::atan2(top_delta, -0.5f);
                    const float arc_angle = top_end - top_start;

                    // 弦のサグが 0.5px 以下になる分割数を求める。固定 32 分割では
                    // 大きい UI の端点付近で弧の近似が粗くなり、ストロークの継ぎ目に
                    // 隙間が見えるため、画面上の半径に応じて増減させる。
                    const float pixel_radius = (std::max)(
                        std::fabs(rect.z), std::fabs(rect.w)) *
                        (std::max)(std::fabs(scale), 0.0001f) * radius;
                    const float max_angle = pixel_radius > 0.5f
                        ? 2.0f * (std::acos)((std::max)(-1.0f, (std::min)(1.0f,
                            1.0f - 0.5f / pixel_radius)))
                        : arc_angle;
                    constexpr int maximum_arc_subdivisions = 256;
                    const int subdivisions = (std::min)(maximum_arc_subdivisions,
                        (std::max)(1, static_cast<int>(std::ceil(
                            arc_angle / (std::max)(0.0001f, max_angle)))));

                    for (int step = 0; step <= subdivisions; ++step)
                    {
                        const float t = static_cast<float>(step) /
                            static_cast<float>(subdivisions);
                        const float angle = Lerp(top_start, top_end, t);
                        shape_path.push_back(to_rect(
                            0.5f + std::cos(angle) * radius,
                            top_center_y + std::sin(angle) * radius));
                    }
                    for (int step = 1; step <= subdivisions; ++step)
                    {
                        const float t = static_cast<float>(step) /
                            static_cast<float>(subdivisions);
                        const float angle = Lerp(bottom_start, bottom_end, t);
                        shape_path.push_back(to_rect(
                            0.5f + std::cos(angle) * radius,
                            bottom_center_y + std::sin(angle) * radius));
                    }
                }
                break;
            }
            case UIShapeComponent::Line:
                closed = false;
                shape_path.push_back({ rect.x, rect.y + rect.w * 0.5f });
                shape_path.push_back({ rect.x + rect.z, rect.y + rect.w * 0.5f });
                break;
            case UIShapeComponent::Polygon:
            {
                const int sides = (std::min)((std::max)(shape.sides, 3), 64);
                const float cx = rect.x + rect.z * 0.5f;
                const float cy = rect.y + rect.w * 0.5f;
                const float rx = rect.z * 0.5f;
                const float ry = rect.w * 0.5f;
                for (int side = 0; side < sides; ++side)
                {
                    const float angle = -pi * 0.5f +
                        (pi * 2.0f * static_cast<float>(side)) /
                        static_cast<float>(sides);
                    shape_path.push_back({
                        cx + std::cos(angle) * rx,
                        cy + std::sin(angle) * ry
                    });
                }
                break;
            }
            case UIShapeComponent::BezierPath:
            {
                closed = false;
                const DirectX::XMFLOAT2 p0{ rect.x, rect.y + rect.w * 0.5f };
                const DirectX::XMFLOAT2 p1{ rect.x + rect.z * 0.35f, rect.y };
                const DirectX::XMFLOAT2 p2{ rect.x + rect.z * 0.65f, rect.y + rect.w };
                const DirectX::XMFLOAT2 p3{ rect.x + rect.z, rect.y + rect.w * 0.5f };
                for (int step = 0; step <= 48; ++step)
                {
                    const float t = static_cast<float>(step) / 48.0f;
                    const float u = 1.0f - t;
                    const float uu = u * u;
                    const float tt = t * t;
                    const float uuu = uu * u;
                    const float ttt = tt * t;
                    shape_path.push_back({
                        p0.x * uuu + 3.0f * p1.x * uu * t +
                            3.0f * p2.x * u * tt + p3.x * ttt,
                        p0.y * uuu + 3.0f * p1.y * uu * t +
                            3.0f * p2.y * u * tt + p3.y * ttt
                    });
                }
                break;
            }
            case UIShapeComponent::CustomBezierPath:
            {
                const std::size_t count = shape.path_points.size();
                if (count < 2)
                {
                    closed = false;
                    break;
                }
                closed = shape.path_closed && count >= 3;
                const std::size_t segment_count = closed ? count : count - 1;
                const auto to_rect = [&rect](const DirectX::XMFLOAT2& p)
                {
                    return DirectX::XMFLOAT2{
                        rect.x + p.x * rect.z,
                        rect.y + p.y * rect.w };
                };
                const auto handle_at = [](const std::vector<DirectX::XMFLOAT2>& values,
                    std::size_t index)
                {
                    return index < values.size() ? values[index] : DirectX::XMFLOAT2{ 0.0f, 0.0f };
                };
                for (std::size_t segment = 0; segment < segment_count; ++segment)
                {
                    const std::size_t next = (segment + 1) % count;
                    const DirectX::XMFLOAT2 a = shape.path_points[segment];
                    const DirectX::XMFLOAT2 b = shape.path_points[next];
                    const DirectX::XMFLOAT2 out_handle = handle_at(shape.path_out_handles, segment);
                    const DirectX::XMFLOAT2 in_handle = handle_at(shape.path_in_handles, next);
                    const DirectX::XMFLOAT2 p0 = to_rect(a);
                    const DirectX::XMFLOAT2 p1 = to_rect({ a.x + out_handle.x, a.y + out_handle.y });
                    const DirectX::XMFLOAT2 p2 = to_rect({ b.x + in_handle.x, b.y + in_handle.y });
                    const DirectX::XMFLOAT2 p3 = to_rect(b);
                    const float chord_pixels = local_distance(p0, p3) *
                        (std::max)(0.0001f, std::fabs(scale));
                    const int subdivisions = (std::max)(8, (std::min)(96,
                        static_cast<int>(std::ceil(chord_pixels / 10.0f))));
                    for (int step = segment == 0 ? 0 : 1; step <= subdivisions; ++step)
                    {
                        const float t = static_cast<float>(step) /
                            static_cast<float>(subdivisions);
                        const float u = 1.0f - t;
                        const float uu = u * u;
                        const float tt = t * t;
                        const float uuu = uu * u;
                        const float ttt = tt * t;
                        shape_path.push_back({
                            p0.x * uuu + 3.0f * p1.x * uu * t +
                                3.0f * p2.x * u * tt + p3.x * ttt,
                            p0.y * uuu + 3.0f * p1.y * uu * t +
                                3.0f * p2.y * u * tt + p3.y * ttt });
                    }
                }
                // Closed Path の最後と先頭が同一点なら stroke 側で二重線にならないよう除く。
                if (closed && shape_path.size() > 1 &&
                    local_distance(shape_path.front(), shape_path.back()) < 0.0001f)
                    shape_path.pop_back();
                break;
            }
            case UIShapeComponent::Superellipse:
            {
                constexpr int subdivisions = 128;
                const float exponent = (std::max)(0.25f,
                    (std::min)(16.0f, shape.superellipse_exponent));
                const float power = 2.0f / exponent;
                const float cx = rect.x + rect.z * 0.5f;
                const float cy = rect.y + rect.w * 0.5f;
                const float rx = rect.z * 0.5f;
                const float ry = rect.w * 0.5f;
                for (int segment = 0; segment < subdivisions; ++segment)
                {
                    const float angle = 2.0f * pi * static_cast<float>(segment) /
                        static_cast<float>(subdivisions);
                    const float cosine = std::cos(angle);
                    const float sine = std::sin(angle);
                    const float x = std::copysign(
                        (std::pow)(std::fabs(cosine), power), cosine);
                    const float y = std::copysign(
                        (std::pow)(std::fabs(sine), power), sine);
                    shape_path.push_back({ cx + x * rx, cy + y * ry });
                }
                break;
            }
            case UIShapeComponent::PolarFormula:
            {
                constexpr int subdivisions = 160;
                const float cx = rect.x + rect.z * 0.5f;
                const float cy = rect.y + rect.w * 0.5f;
                const float rx = rect.z * 0.5f;
                const float ry = rect.w * 0.5f;
                const float base_radius = (std::max)(0.05f,
                    (std::min)(1.5f, shape.polar_base_radius));
                const float amplitude = (std::max)(-1.0f,
                    (std::min)(1.0f, shape.polar_amplitude));
                const float lobes = (std::max)(1.0f,
                    (std::min)(32.0f, shape.polar_lobes));
                const float rotation = DirectX::XMConvertToRadians(
                    shape.polar_rotation);
                for (int segment = 0; segment < subdivisions; ++segment)
                {
                    const float theta = 2.0f * pi * static_cast<float>(segment) /
                        static_cast<float>(subdivisions);
