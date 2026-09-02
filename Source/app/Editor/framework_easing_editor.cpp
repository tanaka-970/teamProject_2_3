#include "framework.h"

#include "../../RePlayEngine/Motion/EasingCurveAsset.h"
#include "../../RePlayEngine/Motion/MotionEasing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{
    struct CurveRange final
    {
        float min_y = 0.0f;
        float max_y = 1.0f;
    };

    struct EasingPresetInfo final
    {
        const char* name;
        ReplayEngine::Motion::MotionEasing easing;
        const char* formula;
    };

    const EasingPresetInfo preset_infos[] =
    {
        { "Linear", ReplayEngine::Motion::MotionEasing::Linear, u8"y = t" },
        { "Step", ReplayEngine::Motion::MotionEasing::Step, u8"y = 0（区間中は値を保持）" },
        { "EaseInQuad", ReplayEngine::Motion::MotionEasing::EaseInQuad, u8"y = t^2" },
        { "EaseOutQuad", ReplayEngine::Motion::MotionEasing::EaseOutQuad, u8"y = 1 - (1 - t)^2" },
        { "EaseInOutQuad", ReplayEngine::Motion::MotionEasing::EaseInOutQuad,
            u8"t < 0.5 ? y = 2*t^2 : y = 1 - (-2*t + 2)^2 / 2" },
        { "EaseInCubic", ReplayEngine::Motion::MotionEasing::EaseInCubic, u8"y = t^3" },
        { "EaseOutCubic", ReplayEngine::Motion::MotionEasing::EaseOutCubic,
            u8"y = 1 - pow(1 - t, 3)" },
        { "EaseInOutCubic", ReplayEngine::Motion::MotionEasing::EaseInOutCubic,
            u8"t < 0.5 ? y = 4*t^3 : y = 1 - (-2*t + 2)^3 / 2" },
        { "EaseInBack", ReplayEngine::Motion::MotionEasing::EaseInBack,
            u8"c1 = 1.70158, c3 = c1 + 1, y = c3*t^3 - c1*t^2" },
        { "EaseOutBack", ReplayEngine::Motion::MotionEasing::EaseOutBack,
            u8"c1 = 1.70158, c3 = c1 + 1, u = t - 1, y = 1 + c3*u^3 + c1*u^2" },
        { "EaseInOutBack", ReplayEngine::Motion::MotionEasing::EaseInOutBack,
            u8"c1 = 1.70158, c2 = c1*1.525; t < 0.5 ? y = (2t)^2*((c2+1)*2t-c2)/2 : y = (2t-2)^2*((c2+1)*(2t-2)+c2)/2 + 1" },
        { "EaseInElastic", ReplayEngine::Motion::MotionEasing::EaseInElastic,
            u8"y = -2^(10*t-10) * sin((10*t-10.75) * 2π/3)（端点は t）" },
        { "EaseOutElastic", ReplayEngine::Motion::MotionEasing::EaseOutElastic,
            u8"y = 2^(-10*t) * sin((10*t-0.75) * 2π/3) + 1（端点は t）" },
        { "EaseInOutElastic", ReplayEngine::Motion::MotionEasing::EaseInOutElastic,
            u8"t=0/1 ? y=t : t<0.5 ? y=-(2^(20t-10)*sin((20t-11.125)*2π/4.5))/2 : y=(2^(-20t+10)*sin((20t-11.125)*2π/4.5))/2+1" }
    };

    struct EasingPreviewState final
    {
        bool playing = false;
        float duration = 1.0f;
        float t = 0.0f;
        std::vector<float> trail;
        std::string guid;
    };

    EasingPreviewState preview_state;

    CurveRange ComputeCurveRange(const ReplayEngine::Motion::EasingCurveAsset& asset,
        const std::vector<DirectX::XMFLOAT2>* freehand = nullptr)
    {
        float min_y = 0.0f;
        float max_y = 1.0f;
        for (const float value : asset.samples)
        {
            if (!std::isfinite(value)) continue;
            min_y = (std::min)(min_y, value);
            max_y = (std::max)(max_y, value);
        }
        for (const DirectX::XMFLOAT2& point : asset.control_points)
        {
            if (!std::isfinite(point.y)) continue;
            min_y = (std::min)(min_y, point.y);
            max_y = (std::max)(max_y, point.y);
        }
        if (freehand != nullptr)
        {
            for (const DirectX::XMFLOAT2& point : *freehand)
            {
                if (!std::isfinite(point.y)) continue;
                min_y = (std::min)(min_y, point.y);
                max_y = (std::max)(max_y, point.y);
            }
        }
        float span = max_y - min_y;
        if (!std::isfinite(span) || span < 0.001f)
        {
            min_y = 0.0f;
            max_y = 1.0f;
            span = 1.0f;
        }
        const float padding = (std::max)(0.08f, span * 0.10f);
        return { min_y - padding, max_y + padding };
    }

    ImVec2 CurveToScreen(const DirectX::XMFLOAT2& point, const ImVec2& origin,
        float size, const CurveRange& range)
    {
        const float span = (std::max)(0.0001f, range.max_y - range.min_y);
        const float normalized_y = (point.y - range.min_y) / span;
        return ImVec2(origin.x + point.x * size,
            origin.y + (1.0f - normalized_y) * size);
    }

    DirectX::XMFLOAT2 ScreenToCurve(const ImVec2& point, const ImVec2& origin,
        float size, const CurveRange& range)
    {
        const float x = std::clamp((point.x - origin.x) / size, 0.0f, 1.0f);
        const float normalized_y = 1.0f - (point.y - origin.y) / size;
        const float y = range.min_y + normalized_y * (range.max_y - range.min_y);
        return { x, y };
    }

    float DistanceSquared(const ImVec2& a, const ImVec2& b)
    {
        const float x = a.x - b.x;
        const float y = a.y - b.y;
        return x * x + y * y;
    }

    void BurnFreehandSamples(ReplayEngine::Motion::EasingCurveAsset& asset,
        const std::vector<DirectX::XMFLOAT2>& raw_points)
    {
        if (raw_points.size() < 2) return;
        std::vector<DirectX::XMFLOAT2> points;
        points.reserve(raw_points.size() + 2);
        float previous_x = 0.0f;
        for (DirectX::XMFLOAT2 point : raw_points)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) continue;
            point.x = std::clamp(point.x, 0.0f, 1.0f);
            if (!points.empty() && point.x < previous_x) point.x = previous_x;
            previous_x = point.x;
            if (!points.empty() && std::fabs(points.back().x - point.x) <= 0.000001f)
                points.back().y = point.y;
            else
                points.push_back(point);
        }
        if (points.empty()) return;
        if (points.front().x > 0.0f) points.insert(points.begin(), { 0.0f, 0.0f });
        else points.front().y = 0.0f;
        if (points.back().x < 1.0f) points.push_back({ 1.0f, 1.0f });
        else points.back().y = 1.0f;
        if (points.size() < 2) return;

        asset.sample_count = std::clamp(asset.sample_count, 16, 256);
        asset.samples.resize(static_cast<std::size_t>(asset.sample_count));
        const float denominator = static_cast<float>((std::max)(1, asset.sample_count - 1));
        std::size_t segment = 0;
        for (int index = 0; index < asset.sample_count; ++index)
        {
            const float x = static_cast<float>(index) / denominator;
            while (segment + 2 < points.size() && x > points[segment + 1].x) ++segment;
            const std::size_t right = (std::min)(segment + 1, points.size() - 1);
            const float width = points[right].x - points[segment].x;
            const float u = width > 0.000001f ?
                std::clamp((x - points[segment].x) / width, 0.0f, 1.0f) : 1.0f;
            asset.samples[static_cast<std::size_t>(index)] = points[segment].y +
                (points[right].y - points[segment].y) * u;
        }
        asset.Normalize();
        asset.FitControlPointsToSamples();
    }

    int HitControlPoint(const ReplayEngine::Motion::EasingCurveAsset& asset,
        const ImVec2& mouse, const ImVec2& origin, float size, float radius,
        const CurveRange& range)
    {
        int hit = -1;
        float best = radius * radius;
        for (int index = 0; index < static_cast<int>(asset.control_points.size()); ++index)
        {
            const ImVec2 screen = CurveToScreen(asset.control_points[static_cast<std::size_t>(index)],
                origin, size, range);
            const float distance = DistanceSquared(mouse, screen);
            if (distance <= best)
            {
                best = distance;
                hit = index;
            }
        }
        return hit;
    }

    int HitSample(const ReplayEngine::Motion::EasingCurveAsset& asset,
        const ImVec2& mouse, const ImVec2& origin, float size, float radius,
        const CurveRange& range)
    {
        if (asset.samples.empty()) return -1;
        int hit = -1;
        float best = radius * radius;
        const float denominator = static_cast<float>((std::max)(1,
            static_cast<int>(asset.samples.size()) - 1));
        for (int index = 0; index < static_cast<int>(asset.samples.size()); ++index)
        {
            const DirectX::XMFLOAT2 point{
                static_cast<float>(index) / denominator,
                asset.samples[static_cast<std::size_t>(index)] };
            const float distance = DistanceSquared(mouse, CurveToScreen(point, origin, size, range));
            if (distance <= best)
            {
                best = distance;
                hit = index;
            }
        }
        return hit;
    }

    void DrawDashedLinear(ImDrawList* draw_list, const ImVec2& origin, float size,
        const CurveRange& range)
    {
        constexpr int dash_count = 24;
        for (int index = 0; index < dash_count; index += 2)
        {
            const float a = static_cast<float>(index) / static_cast<float>(dash_count);
            const float b = static_cast<float>(index + 1) / static_cast<float>(dash_count);
            draw_list->AddLine(CurveToScreen({ a, a }, origin, size, range),
                CurveToScreen({ b, b }, origin, size, range), IM_COL32(150, 150, 150, 120), 1.0f);
        }
    }

    void BuildSpeedSamples(const ReplayEngine::Motion::EasingCurveAsset& asset,
        std::vector<float>& speed, float& max_abs_speed, float& average_abs_speed)
    {
        speed.assign(asset.samples.size(), 0.0f);
        max_abs_speed = 0.0f;
        average_abs_speed = 0.0f;
        if (asset.samples.size() < 2) return;
        const float dt = 1.0f / static_cast<float>(asset.samples.size() - 1);
        for (std::size_t index = 0; index < asset.samples.size(); ++index)
        {
            float value = 0.0f;
            if (index == 0)
                value = (asset.samples[1] - asset.samples[0]) / dt;
            else if (index + 1 == asset.samples.size())
                value = (asset.samples[index] - asset.samples[index - 1]) / dt;
            else
                value = (asset.samples[index + 1] - asset.samples[index - 1]) / (2.0f * dt);
            if (!std::isfinite(value)) value = 0.0f;
            speed[index] = value;
            const float absolute = std::fabs(value);
            max_abs_speed = (std::max)(max_abs_speed, absolute);
            average_abs_speed += absolute;
        }
        average_abs_speed /= static_cast<float>(speed.size());
    }

    void DrawSpeedGraph(const ReplayEngine::Motion::EasingCurveAsset& asset, float width)
    {
        std::vector<float> speed;
        float max_abs_speed = 0.0f;
        float average_abs_speed = 0.0f;
        BuildSpeedSamples(asset, speed, max_abs_speed, average_abs_speed);
        ImGui::Text(u8"最大速度: %.3f / 平均速度: %.3f", max_abs_speed, average_abs_speed);
        const float graph_height = 150.0f;
        ImGui::InvisibleButton("##EasingSpeedGraph", ImVec2(width, graph_height));
        const ImVec2 graph_min = ImGui::GetItemRectMin();
        const ImVec2 graph_max = ImGui::GetItemRectMax();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(graph_min, graph_max, IM_COL32(25, 27, 31, 255));
        draw_list->AddRect(graph_min, graph_max, IM_COL32(115, 120, 130, 255));
        for (int grid = 1; grid < 4; ++grid)
        {
            const float x = graph_min.x + width * static_cast<float>(grid) / 4.0f;
            draw_list->AddLine(ImVec2(x, graph_min.y), ImVec2(x, graph_max.y),
                IM_COL32(80, 84, 92, 100));
        }
        const float zero_y = graph_min.y + graph_height * 0.5f;
        draw_list->AddLine(ImVec2(graph_min.x, zero_y), ImVec2(graph_max.x, zero_y),
            IM_COL32(150, 150, 155, 170), 1.0f);
        if (speed.size() < 2) return;
        const float scale = (std::max)(0.0001f, max_abs_speed);
        const float denominator = static_cast<float>(speed.size() - 1);
        const auto to_screen = [&](std::size_t index, float value)
        {
            const float x = graph_min.x + width * static_cast<float>(index) / denominator;
            const float y = zero_y - (value / scale) * graph_height * 0.45f;
            return ImVec2(x, y);
        };
        draw_list->PushClipRect(graph_min, graph_max, true);
        for (std::size_t index = 1; index < speed.size(); ++index)
        {
            const float middle = (speed[index - 1] + speed[index]) * 0.5f;
            const ImU32 color = middle < 0.0f
                ? IM_COL32(245, 105, 105, 255)
                : IM_COL32(90, 205, 245, 255);
            draw_list->AddLine(to_screen(index - 1, speed[index - 1]),
                to_screen(index, speed[index]), color, 2.0f);
        }
        draw_list->PopClipRect();
        ImGui::TextDisabled(u8"赤い区間は速度が負で、値が戻る動きです。");
    }

    void DrawBehaviorPreview(const ReplayEngine::Motion::EasingCurveAsset& asset, float width,
        const std::string& guid)
    {
        if (preview_state.guid != guid)
        {
            preview_state.guid = guid;
            preview_state.playing = false;
            preview_state.t = 0.0f;
            preview_state.trail.clear();
        }

        if (ImGui::Button(preview_state.playing ? u8"停止###EasingBehaviorPreview" :
            u8"再生###EasingBehaviorPreview"))
        {
            preview_state.playing = !preview_state.playing;
            if (preview_state.playing) preview_state.trail.clear();
        }
        ReplayEngine::Editor::EditorHelp::Item("button.easing.preview_play");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat(u8"再生時間", &preview_state.duration, 0.2f, 5.0f, u8"%.2f 秒");

        if (preview_state.playing)
        {
            const float duration = (std::max)(0.2f, preview_state.duration);
            preview_state.t += ImGui::GetIO().DeltaTime / duration;
            if (preview_state.t >= 1.0f)
            {
                preview_state.t = std::fmod(preview_state.t, 1.0f);
                preview_state.trail.clear();
            }
            const float value = asset.Evaluate(preview_state.t);
            if (std::isfinite(value))
            {
                preview_state.trail.push_back(value);
                constexpr std::size_t max_trail = 14;
                if (preview_state.trail.size() > max_trail)
                    preview_state.trail.erase(preview_state.trail.begin());
            }
            ImGui::Text(u8"時間 t: %.3f", preview_state.t);
        }
        else
        {
            ImGui::SetNextItemWidth((std::max)(180.0f, width - 120.0f));
            if (ImGui::SliderFloat(u8"時間 t##EasingPreviewTime", &preview_state.t,
                0.0f, 1.0f, "%.3f"))
            {
                preview_state.trail.clear();
            }
        }

        float min_value = 0.0f;
        float max_value = 1.0f;
        for (const float value : asset.samples)
        {
            if (!std::isfinite(value)) continue;
            min_value = (std::min)(min_value, value);
            max_value = (std::max)(max_value, value);
        }
        float span = max_value - min_value;
        if (!std::isfinite(span) || span < 0.001f) span = 1.0f;
        const float padding = (std::max)(0.05f, span * 0.10f);
        min_value -= padding;
        max_value += padding;
        span = max_value - min_value;

        const float band_height = 72.0f;
        ImGui::InvisibleButton("##EasingBehaviorBand", ImVec2(width, band_height));
        const ImVec2 band_min = ImGui::GetItemRectMin();
        const ImVec2 band_max = ImGui::GetItemRectMax();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(band_min, band_max, IM_COL32(28, 30, 34, 255));
        draw_list->AddRect(band_min, band_max, IM_COL32(100, 105, 115, 255));
        const float left = band_min.x + 48.0f;
        const float right = band_max.x - 12.0f;
        const float lane_width = (std::max)(1.0f, right - left);
        const auto to_x = [&](float value)
        {
            return left + (value - min_value) / span * lane_width;
        };
        const float curve_y = band_min.y + 23.0f;
        const float linear_y = band_min.y + 50.0f;
        draw_list->AddText(ImVec2(band_min.x + 6.0f, curve_y - 7.0f),
            IM_COL32(220, 225, 230, 255), u8"カーブ");
        draw_list->AddText(ImVec2(band_min.x + 6.0f, linear_y - 7.0f),
            IM_COL32(190, 195, 205, 255), "Linear");
        draw_list->AddLine(ImVec2(left, curve_y), ImVec2(right, curve_y),
            IM_COL32(95, 100, 108, 255), 1.0f);
        draw_list->AddLine(ImVec2(left, linear_y), ImVec2(right, linear_y),
            IM_COL32(95, 100, 108, 255), 1.0f);
        draw_list->AddLine(ImVec2(to_x(0.0f), band_min.y + 5.0f),
            ImVec2(to_x(0.0f), band_max.y - 5.0f), IM_COL32(125, 130, 138, 130), 1.0f);
        draw_list->AddLine(ImVec2(to_x(1.0f), band_min.y + 5.0f),
            ImVec2(to_x(1.0f), band_max.y - 5.0f), IM_COL32(125, 130, 138, 130), 1.0f);
        for (std::size_t index = 0; index < preview_state.trail.size(); ++index)
        {
            const unsigned int alpha = static_cast<unsigned int>(35 +
                100 * (index + 1) / (std::max)(std::size_t(1), preview_state.trail.size()));
            draw_list->AddCircleFilled(ImVec2(to_x(preview_state.trail[index]), curve_y), 4.0f,
                IM_COL32(85, 220, 145, alpha));
        }
        float curve_value = asset.Evaluate(preview_state.t);
        if (!std::isfinite(curve_value)) curve_value = preview_state.t;
        draw_list->AddCircleFilled(ImVec2(to_x(curve_value), curve_y), 7.0f,
            IM_COL32(90, 235, 155, 255));
        draw_list->AddCircleFilled(ImVec2(to_x(preview_state.t), linear_y), 6.0f,
            IM_COL32(230, 230, 235, 255));
    }
}

void framework::draw_easing_editor()
{
    if (!show_easing_editor_panel) return;
    const std::string title = easing_editor_dirty
        ? u8"イージングカーブ *###EasingCurveEditor"
        : u8"イージングカーブ###EasingCurveEditor";
    if (!ImGui::Begin(title.c_str(), &show_easing_editor_panel))
    {
        ImGui::End();
        return;
    }
    if (!easing_editor_loaded)
    {
        ImGui::TextWrapped(u8"Project Browser の 作成 → イージングカーブ で新規作成するか、既存の .replayeasing をダブルクリックしてください。");
        if (ImGui::Button(u8"新規作成##EasingCreateFromEditor"))
            project_create_easing_curve("NewEasingCurve");
        ReplayEngine::Editor::EditorHelp::Item("button.easing.create");
        ImGui::End();
        return;
    }

    if (ImGui::Button(u8"保存")) save_current_easing_curve();
    ReplayEngine::Editor::EditorHelp::Item("button.easing.save");
    ImGui::SameLine();
    if (ImGui::Button(u8"制御点を再近似"))
    {
        easing_editor_asset.FitControlPointsToSamples();
        easing_editor_dirty = true;
        easing_editor_status = u8"サンプルから制御点を再近似しました";
    }
    ReplayEngine::Editor::EditorHelp::Item("button.easing.fit_control_points");
    ImGui::SameLine();
    if (ImGui::Button(u8"制御点をクリア"))
    {
        easing_editor_asset.control_points.clear();
        easing_editor_dirty = true;
        easing_editor_status = u8"制御点をクリアしました。サンプルを直接編集できます";
    }
    ReplayEngine::Editor::EditorHelp::Item("button.easing.clear_control_points");

    if (ImGui::InputText(u8"名前", easing_editor_name_buffer,
        IM_ARRAYSIZE(easing_editor_name_buffer)))
    {
        easing_editor_asset.name = easing_editor_name_buffer;
        easing_editor_dirty = true;
    }

    int requested_count = easing_editor_asset.sample_count;
    if (ImGui::SliderInt(u8"サンプル数", &requested_count, 16, 256))
    {
        ReplayEngine::Motion::EasingCurveAsset source = easing_editor_asset;
        easing_editor_asset.sample_count = requested_count;
        easing_editor_asset.samples.resize(static_cast<std::size_t>(requested_count));
        const float denominator = static_cast<float>((std::max)(1, requested_count - 1));
        for (int index = 0; index < requested_count; ++index)
        {
            const float t = static_cast<float>(index) / denominator;
            easing_editor_asset.samples[static_cast<std::size_t>(index)] = source.Evaluate(t);
        }
        easing_editor_asset.Normalize();
        easing_editor_dirty = true;
        easing_editor_status = u8"サンプル数を変更しました";
    }

    const char* preset_names[IM_ARRAYSIZE(preset_infos)]{};
    for (int index = 0; index < IM_ARRAYSIZE(preset_infos); ++index)
        preset_names[index] = preset_infos[index].name;
    ImGui::SetNextItemWidth(190.0f);
    ImGui::Combo(u8"固定プリセット", &easing_editor_preset_index,
        preset_names, IM_ARRAYSIZE(preset_names));
    ImGui::SameLine();
    if (ImGui::Button(u8"サンプルへ焼く"))
    {
        easing_editor_asset.samples.resize(
            static_cast<std::size_t>(easing_editor_asset.sample_count));
        const float denominator = static_cast<float>((std::max)(1,
            easing_editor_asset.sample_count - 1));
        const auto easing = preset_infos[easing_editor_preset_index].easing;
        for (int index = 0; index < easing_editor_asset.sample_count; ++index)
        {
            const float t = static_cast<float>(index) / denominator;
            easing_editor_asset.samples[static_cast<std::size_t>(index)] =
                ReplayEngine::Motion::ApplyEasing(easing, t);
        }
        easing_editor_asset.Normalize();
        easing_editor_asset.FitControlPointsToSamples();
        easing_editor_formula_preset_index = easing_editor_preset_index;
        easing_editor_dirty = true;
        easing_editor_status = std::string(u8"プリセットを焼きました: ") +
            preset_infos[easing_editor_preset_index].name;
    }
    ReplayEngine::Editor::EditorHelp::Item("button.easing.bake_samples");

    ImGui::TextDisabled(u8"左ドラッグ: フリーハンド / 丸をドラッグ: 編集 / 右クリック: 制御点追加・削除");
    const float available = ImGui::GetContentRegionAvail().x;
    const float canvas_size = (std::max)(180.0f, (std::min)(460.0f, available));
    const CurveRange curve_range = ComputeCurveRange(easing_editor_asset,
        easing_editor_drawing ? &easing_editor_freehand_points : nullptr);
    ImGui::InvisibleButton("##EasingCanvas", ImVec2(canvas_size, canvas_size));
    const ImVec2 canvas_min = ImGui::GetItemRectMin();
    const ImVec2 canvas_max = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(canvas_min, canvas_max, IM_COL32(25, 27, 31, 255));
    draw_list->AddRect(canvas_min, canvas_max, IM_COL32(115, 120, 130, 255));
    for (int grid = 1; grid < 4; ++grid)
    {
        const float fraction = static_cast<float>(grid) / 4.0f;
        const float x = canvas_min.x + canvas_size * fraction;
        const float y = canvas_min.y + canvas_size * fraction;
        draw_list->AddLine(ImVec2(x, canvas_min.y), ImVec2(x, canvas_max.y),
            IM_COL32(80, 84, 92, 120));
        draw_list->AddLine(ImVec2(canvas_min.x, y), ImVec2(canvas_max.x, y),
            IM_COL32(80, 84, 92, 120));
    }
    DrawDashedLinear(draw_list, canvas_min, canvas_size, curve_range);

    draw_list->PushClipRect(canvas_min, canvas_max, true);
    if (easing_editor_asset.samples.size() >= 2)
    {
        const float denominator = static_cast<float>(easing_editor_asset.samples.size() - 1);
        for (std::size_t index = 1; index < easing_editor_asset.samples.size(); ++index)
        {
            const float ay = easing_editor_asset.samples[index - 1];
            const float by = easing_editor_asset.samples[index];
            if (!std::isfinite(ay) || !std::isfinite(by)) continue;
            const DirectX::XMFLOAT2 a{ static_cast<float>(index - 1) / denominator, ay };
            const DirectX::XMFLOAT2 b{ static_cast<float>(index) / denominator, by };
            draw_list->AddLine(CurveToScreen(a, canvas_min, canvas_size, curve_range),
                CurveToScreen(b, canvas_min, canvas_size, curve_range),
                IM_COL32(80, 220, 145, 255), 2.0f);
        }
    }
    if (easing_editor_drawing && easing_editor_freehand_points.size() >= 2)
    {
        for (std::size_t index = 1; index < easing_editor_freehand_points.size(); ++index)
        {
            draw_list->AddLine(CurveToScreen(easing_editor_freehand_points[index - 1],
                canvas_min, canvas_size, curve_range),
                CurveToScreen(easing_editor_freehand_points[index], canvas_min, canvas_size,
                    curve_range), IM_COL32(245, 205, 80, 255), 2.0f);
        }
    }

    if (!easing_editor_asset.control_points.empty())
    {
        for (int index = 0; index < static_cast<int>(easing_editor_asset.control_points.size()); ++index)
        {
            const ImVec2 point = CurveToScreen(
                easing_editor_asset.control_points[static_cast<std::size_t>(index)],
                canvas_min, canvas_size, curve_range);
            draw_list->AddCircleFilled(point, 5.0f,
                index == easing_editor_active_control_point
                    ? IM_COL32(255, 215, 90, 255) : IM_COL32(230, 235, 240, 255));
            draw_list->AddCircle(point, 6.0f, IM_COL32(20, 20, 20, 255));
        }
    }
    else if (!easing_editor_asset.samples.empty())
    {
        const float denominator = static_cast<float>((std::max)(1,
            static_cast<int>(easing_editor_asset.samples.size()) - 1));
        for (int index = 0; index < static_cast<int>(easing_editor_asset.samples.size()); ++index)
        {
            const float sample_y = easing_editor_asset.samples[static_cast<std::size_t>(index)];
            if (!std::isfinite(sample_y)) continue;
            const DirectX::XMFLOAT2 point{ static_cast<float>(index) / denominator, sample_y };
            draw_list->AddCircleFilled(CurveToScreen(point, canvas_min, canvas_size, curve_range),
                2.5f, IM_COL32(210, 220, 230, 210));
        }
    }
    draw_list->PopClipRect();

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (hovered)
    {
        const DirectX::XMFLOAT2 cursor = ScreenToCurve(mouse, canvas_min, canvas_size, curve_range);
        const float curve_y = easing_editor_asset.Evaluate(cursor.x);
        draw_list->PushClipRect(canvas_min, canvas_max, true);
        draw_list->AddLine(ImVec2(mouse.x, canvas_min.y), ImVec2(mouse.x, canvas_max.y),
            IM_COL32(230, 230, 235, 110), 1.0f);
        draw_list->AddLine(ImVec2(canvas_min.x, mouse.y), ImVec2(canvas_max.x, mouse.y),
            IM_COL32(230, 230, 235, 110), 1.0f);
        char cursor_text[128]{};
        std::snprintf(cursor_text, sizeof(cursor_text), u8"t %.3f / y %.3f / カーブ %.3f",
            cursor.x, cursor.y, std::isfinite(curve_y) ? curve_y : 0.0f);
        const ImVec2 text_size = ImGui::CalcTextSize(cursor_text);
        const float text_x = (std::min)(canvas_max.x - text_size.x - 5.0f, mouse.x + 8.0f);
        const float text_y = (std::max)(canvas_min.y + 4.0f, mouse.y - text_size.y - 7.0f);
        draw_list->AddText(ImVec2(text_x, text_y), IM_COL32(245, 245, 245, 240), cursor_text);
        draw_list->PopClipRect();
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        easing_editor_active_control_point = HitControlPoint(easing_editor_asset,
            mouse, canvas_min, canvas_size, 8.0f, curve_range);
        easing_editor_active_sample = -1;
        if (easing_editor_active_control_point < 0 && easing_editor_asset.control_points.empty())
            easing_editor_active_sample = HitSample(easing_editor_asset,
                mouse, canvas_min, canvas_size, 6.0f, curve_range);
        if (easing_editor_active_control_point < 0 && easing_editor_active_sample < 0)
        {
            easing_editor_drawing = true;
            easing_editor_formula_preset_index = -1;
            easing_editor_freehand_points.clear();
            easing_editor_freehand_points.push_back(
                ScreenToCurve(mouse, canvas_min, canvas_size, curve_range));
        }
    }

    if (easing_editor_active_control_point >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const int index = easing_editor_active_control_point;
        DirectX::XMFLOAT2 point = ScreenToCurve(mouse, canvas_min, canvas_size, curve_range);
        const int last = static_cast<int>(easing_editor_asset.control_points.size()) - 1;
        if (index == 0) point = { 0.0f, 0.0f };
        else if (index == last) point = { 1.0f, 1.0f };
        else
        {
            point.x = std::clamp(point.x,
                easing_editor_asset.control_points[static_cast<std::size_t>(index - 1)].x,
                easing_editor_asset.control_points[static_cast<std::size_t>(index + 1)].x);
        }
        easing_editor_asset.control_points[static_cast<std::size_t>(index)] = point;
        easing_editor_asset.RebuildSamplesFromControlPoints();
        easing_editor_formula_preset_index = -1;
        easing_editor_dirty = true;
    }
    else if (easing_editor_active_sample >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const int index = easing_editor_active_sample;
        if (index > 0 && index + 1 < static_cast<int>(easing_editor_asset.samples.size()))
        {
            easing_editor_asset.samples[static_cast<std::size_t>(index)] =
                ScreenToCurve(mouse, canvas_min, canvas_size, curve_range).y;
            easing_editor_asset.Normalize();
            easing_editor_formula_preset_index = -1;
            easing_editor_dirty = true;
        }
    }
    else if (easing_editor_drawing && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        DirectX::XMFLOAT2 point = ScreenToCurve(mouse, canvas_min, canvas_size, curve_range);
        if (!easing_editor_freehand_points.empty() && point.x < easing_editor_freehand_points.back().x)
            point.x = easing_editor_freehand_points.back().x;
        if (easing_editor_freehand_points.empty() ||
            std::fabs(point.x - easing_editor_freehand_points.back().x) > 0.001f ||
            std::fabs(point.y - easing_editor_freehand_points.back().y) > 0.001f)
        {
            easing_editor_freehand_points.push_back(point);
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (easing_editor_drawing)
        {
            BurnFreehandSamples(easing_editor_asset, easing_editor_freehand_points);
            easing_editor_formula_preset_index = -1;
            easing_editor_dirty = true;
            easing_editor_status = u8"フリーハンドをサンプルへ焼きました";
        }
        else if (easing_editor_active_control_point >= 0)
        {
            easing_editor_status = u8"制御点からサンプルを再構築しました";
        }
        else if (easing_editor_active_sample >= 0)
        {
            easing_editor_status = u8"サンプルを直接編集しました";
        }
        easing_editor_drawing = false;
        easing_editor_freehand_points.clear();
        easing_editor_active_control_point = -1;
        easing_editor_active_sample = -1;
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        easing_editor_context_point = ScreenToCurve(mouse, canvas_min, canvas_size, curve_range);
        easing_editor_context_control_point = HitControlPoint(easing_editor_asset,
            mouse, canvas_min, canvas_size, 9.0f, curve_range);
        ImGui::OpenPopup("##EasingControlPointMenu");
    }
    if (ImGui::BeginPopup("##EasingControlPointMenu"))
    {
        const bool can_add = easing_editor_context_control_point < 0;
        if (ImGui::MenuItem(u8"制御点を追加", nullptr, false, can_add))
        {
            if (easing_editor_asset.control_points.empty())
                easing_editor_asset.FitControlPointsToSamples();
            DirectX::XMFLOAT2 new_point = easing_editor_context_point;
            new_point.x = std::clamp(new_point.x, 0.001f, 0.999f);
            const auto found = std::lower_bound(easing_editor_asset.control_points.begin(),
                easing_editor_asset.control_points.end(), new_point.x,
                [](const DirectX::XMFLOAT2& point, float x) { return point.x < x; });
            easing_editor_asset.control_points.insert(found, new_point);
            easing_editor_asset.RebuildSamplesFromControlPoints();
            easing_editor_formula_preset_index = -1;
            easing_editor_dirty = true;
            easing_editor_status = u8"制御点を追加しました";
        }
        const int selected = easing_editor_context_control_point;
        const bool can_delete = selected > 0 &&
            selected + 1 < static_cast<int>(easing_editor_asset.control_points.size());
        if (ImGui::MenuItem(u8"制御点を削除", nullptr, false, can_delete))
        {
            easing_editor_asset.control_points.erase(
                easing_editor_asset.control_points.begin() + selected);
            easing_editor_asset.RebuildSamplesFromControlPoints();
            easing_editor_formula_preset_index = -1;
            easing_editor_dirty = true;
            easing_editor_status = u8"制御点を削除しました";
        }
        ImGui::EndPopup();
    }

    if (!easing_editor_asset.samples.empty())
    {
        float min_y = (std::numeric_limits<float>::max)();
        float max_y = (std::numeric_limits<float>::lowest)();
        for (const float value : easing_editor_asset.samples)
        {
            if (!std::isfinite(value)) continue;
            min_y = (std::min)(min_y, value);
            max_y = (std::max)(max_y, value);
        }
        if (!std::isfinite(min_y) || !std::isfinite(max_y))
        {
            min_y = 0.0f;
            max_y = 1.0f;
        }
        ImGui::Text(u8"サンプル: %d / 制御点: %d / Y: %.3f .. %.3f",
            easing_editor_asset.sample_count,
            static_cast<int>(easing_editor_asset.control_points.size()), min_y, max_y);
        ImGui::Text(u8"オーバーシュート: 最大 %.3f / 最小 %.3f", max_y, min_y);
    }

    if (easing_editor_formula_preset_index >= 0 &&
        easing_editor_formula_preset_index < IM_ARRAYSIZE(preset_infos))
    {
        const EasingPresetInfo& info = preset_infos[easing_editor_formula_preset_index];
        ImGui::Text(u8"種類: %s", info.name);
        ImGui::TextWrapped(u8"式: %s", info.formula);
    }
    else
    {
        ImGui::Text(u8"種類: カスタム（サンプル %d 点）", easing_editor_asset.sample_count);
    }
    if (!easing_editor_asset.control_points.empty())
    {
        ImGui::TextDisabled(u8"制御点座標");
        for (std::size_t index = 0; index < easing_editor_asset.control_points.size(); ++index)
        {
            const DirectX::XMFLOAT2& point = easing_editor_asset.control_points[index];
            ImGui::Text("P%d (%.2f, %.2f)", static_cast<int>(index), point.x, point.y);
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(u8"速度グラフ");
    DrawSpeedGraph(easing_editor_asset, canvas_size);

    ImGui::Separator();
    ImGui::TextUnformatted(u8"挙動プレビュー");
    DrawBehaviorPreview(easing_editor_asset, canvas_size, easing_editor_guid);

    if (!easing_editor_status.empty())
        ImGui::TextWrapped("%s", easing_editor_status.c_str());

    ImGui::End();
}
