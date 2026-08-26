#include "framework.h"

#include "../../RePlayEngine/Motion/EasingCurveAsset.h"
#include "../../RePlayEngine/Motion/MotionEasing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace
{
    ImVec2 CurveToScreen(const DirectX::XMFLOAT2& point, const ImVec2& origin, float size)
    {
        return ImVec2(origin.x + point.x * size,
            origin.y + (1.0f - point.y) * size);
    }

    DirectX::XMFLOAT2 ScreenToCurve(const ImVec2& point, const ImVec2& origin, float size)
    {
        const float x = std::clamp((point.x - origin.x) / size, 0.0f, 1.0f);
        const float y = 1.0f - (point.y - origin.y) / size;
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
        const ImVec2& mouse, const ImVec2& origin, float size, float radius)
    {
        int hit = -1;
        float best = radius * radius;
        for (int index = 0; index < static_cast<int>(asset.control_points.size()); ++index)
        {
            const ImVec2 screen = CurveToScreen(asset.control_points[static_cast<std::size_t>(index)],
                origin, size);
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
        const ImVec2& mouse, const ImVec2& origin, float size, float radius)
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
            const float distance = DistanceSquared(mouse, CurveToScreen(point, origin, size));
            if (distance <= best)
            {
                best = distance;
                hit = index;
            }
        }
        return hit;
    }

    void DrawDashedLinear(ImDrawList* draw_list, const ImVec2& origin, float size)
    {
        constexpr int dash_count = 24;
        for (int index = 0; index < dash_count; index += 2)
        {
            const float a = static_cast<float>(index) / static_cast<float>(dash_count);
            const float b = static_cast<float>(index + 1) / static_cast<float>(dash_count);
            draw_list->AddLine(CurveToScreen({ a, a }, origin, size),
                CurveToScreen({ b, b }, origin, size), IM_COL32(150, 150, 150, 120), 1.0f);
        }
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
        ImGui::TextDisabled("Easing Curve を Project Browser から開いてください。");
        ImGui::End();
        return;
    }

    if (ImGui::Button("保存")) save_current_easing_curve();
    ImGui::SameLine();
    if (ImGui::Button("制御点を再近似"))
    {
        easing_editor_asset.FitControlPointsToSamples();
        easing_editor_dirty = true;
        easing_editor_status = "samples から制御点を再近似しました";
    }
    ImGui::SameLine();
    if (ImGui::Button("制御点をクリア"))
    {
        easing_editor_asset.control_points.clear();
        easing_editor_dirty = true;
        easing_editor_status = "制御点をクリアしました。samples を直接編集できます";
    }

    if (ImGui::InputText("名前", easing_editor_name_buffer,
        IM_ARRAYSIZE(easing_editor_name_buffer)))
    {
        easing_editor_asset.name = easing_editor_name_buffer;
        easing_editor_dirty = true;
    }

    int requested_count = easing_editor_asset.sample_count;
    if (ImGui::SliderInt("サンプル数", &requested_count, 16, 256))
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
        easing_editor_status = "サンプル数を変更しました";
    }

    static const char* preset_names[] = {
        "Linear", "Step", "EaseInQuad", "EaseOutQuad", "EaseInOutQuad",
        "EaseInCubic", "EaseOutCubic", "EaseInOutCubic", "EaseInBack",
        "EaseOutBack", "EaseInOutBack", "EaseInElastic", "EaseOutElastic",
        "EaseInOutElastic" };
    static const ReplayEngine::Motion::MotionEasing preset_values[] = {
        ReplayEngine::Motion::MotionEasing::Linear,
        ReplayEngine::Motion::MotionEasing::Step,
        ReplayEngine::Motion::MotionEasing::EaseInQuad,
        ReplayEngine::Motion::MotionEasing::EaseOutQuad,
        ReplayEngine::Motion::MotionEasing::EaseInOutQuad,
        ReplayEngine::Motion::MotionEasing::EaseInCubic,
        ReplayEngine::Motion::MotionEasing::EaseOutCubic,
        ReplayEngine::Motion::MotionEasing::EaseInOutCubic,
        ReplayEngine::Motion::MotionEasing::EaseInBack,
        ReplayEngine::Motion::MotionEasing::EaseOutBack,
        ReplayEngine::Motion::MotionEasing::EaseInOutBack,
        ReplayEngine::Motion::MotionEasing::EaseInElastic,
        ReplayEngine::Motion::MotionEasing::EaseOutElastic,
        ReplayEngine::Motion::MotionEasing::EaseInOutElastic };
    ImGui::SetNextItemWidth(190.0f);
    ImGui::Combo("固定プリセット", &easing_editor_preset_index,
        preset_names, IM_ARRAYSIZE(preset_names));
    ImGui::SameLine();
    if (ImGui::Button("サンプルへ焼く"))
    {
        easing_editor_asset.samples.resize(
            static_cast<std::size_t>(easing_editor_asset.sample_count));
        const float denominator = static_cast<float>((std::max)(1,
            easing_editor_asset.sample_count - 1));
        const auto easing = preset_values[easing_editor_preset_index];
        for (int index = 0; index < easing_editor_asset.sample_count; ++index)
        {
            const float t = static_cast<float>(index) / denominator;
            easing_editor_asset.samples[static_cast<std::size_t>(index)] =
                ReplayEngine::Motion::ApplyEasing(easing, t);
        }
        easing_editor_asset.Normalize();
        easing_editor_asset.FitControlPointsToSamples();
        easing_editor_dirty = true;
        easing_editor_status = std::string("プリセットを焼きました: ") +
            preset_names[easing_editor_preset_index];
    }

    ImGui::TextDisabled("左ドラッグ: フリーハンド / 丸をドラッグ: 編集 / 右クリック: 制御点追加・削除");
    const float available = ImGui::GetContentRegionAvail().x;
    const float canvas_size = (std::max)(180.0f, (std::min)(460.0f, available));
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
    DrawDashedLinear(draw_list, canvas_min, canvas_size);

    draw_list->PushClipRect(canvas_min, canvas_max, true);
    if (easing_editor_asset.samples.size() >= 2)
    {
        const float denominator = static_cast<float>(easing_editor_asset.samples.size() - 1);
        for (std::size_t index = 1; index < easing_editor_asset.samples.size(); ++index)
        {
            const DirectX::XMFLOAT2 a{
                static_cast<float>(index - 1) / denominator,
                easing_editor_asset.samples[index - 1] };
            const DirectX::XMFLOAT2 b{
                static_cast<float>(index) / denominator,
                easing_editor_asset.samples[index] };
            draw_list->AddLine(CurveToScreen(a, canvas_min, canvas_size),
                CurveToScreen(b, canvas_min, canvas_size), IM_COL32(80, 220, 145, 255), 2.0f);
        }
    }
    if (easing_editor_drawing && easing_editor_freehand_points.size() >= 2)
    {
        for (std::size_t index = 1; index < easing_editor_freehand_points.size(); ++index)
        {
            draw_list->AddLine(CurveToScreen(easing_editor_freehand_points[index - 1],
                canvas_min, canvas_size), CurveToScreen(easing_editor_freehand_points[index],
                canvas_min, canvas_size), IM_COL32(245, 205, 80, 255), 2.0f);
        }
    }

    if (!easing_editor_asset.control_points.empty())
    {
        for (int index = 0; index < static_cast<int>(easing_editor_asset.control_points.size()); ++index)
        {
            const ImVec2 point = CurveToScreen(
                easing_editor_asset.control_points[static_cast<std::size_t>(index)],
                canvas_min, canvas_size);
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
            const DirectX::XMFLOAT2 point{
                static_cast<float>(index) / denominator,
                easing_editor_asset.samples[static_cast<std::size_t>(index)] };
            draw_list->AddCircleFilled(CurveToScreen(point, canvas_min, canvas_size), 2.5f,
                IM_COL32(210, 220, 230, 210));
        }
    }
    draw_list->PopClipRect();

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        easing_editor_active_control_point = HitControlPoint(easing_editor_asset,
            mouse, canvas_min, canvas_size, 8.0f);
        easing_editor_active_sample = -1;
        if (easing_editor_active_control_point < 0 && easing_editor_asset.control_points.empty())
            easing_editor_active_sample = HitSample(easing_editor_asset,
                mouse, canvas_min, canvas_size, 6.0f);
        if (easing_editor_active_control_point < 0 && easing_editor_active_sample < 0)
        {
            easing_editor_drawing = true;
            easing_editor_freehand_points.clear();
            easing_editor_freehand_points.push_back(
                ScreenToCurve(mouse, canvas_min, canvas_size));
        }
    }

    if (easing_editor_active_control_point >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const int index = easing_editor_active_control_point;
        DirectX::XMFLOAT2 point = ScreenToCurve(mouse, canvas_min, canvas_size);
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
        easing_editor_dirty = true;
    }
    else if (easing_editor_active_sample >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const int index = easing_editor_active_sample;
        if (index > 0 && index + 1 < static_cast<int>(easing_editor_asset.samples.size()))
        {
            easing_editor_asset.samples[static_cast<std::size_t>(index)] =
                ScreenToCurve(mouse, canvas_min, canvas_size).y;
            easing_editor_asset.Normalize();
            easing_editor_dirty = true;
        }
    }
    else if (easing_editor_drawing && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        DirectX::XMFLOAT2 point = ScreenToCurve(mouse, canvas_min, canvas_size);
        if (!easing_editor_freehand_points.empty() &&
            point.x < easing_editor_freehand_points.back().x)
        {
            point.x = easing_editor_freehand_points.back().x;
        }
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
            easing_editor_dirty = true;
            easing_editor_status = "フリーハンドを samples へ焼きました";
        }
        else if (easing_editor_active_control_point >= 0)
        {
            easing_editor_status = "制御点から samples を再構築しました";
        }
        else if (easing_editor_active_sample >= 0)
        {
            easing_editor_status = "sample を直接編集しました";
        }
        easing_editor_drawing = false;
        easing_editor_freehand_points.clear();
        easing_editor_active_control_point = -1;
        easing_editor_active_sample = -1;
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        easing_editor_context_point = ScreenToCurve(mouse, canvas_min, canvas_size);
        easing_editor_context_control_point = HitControlPoint(easing_editor_asset,
            mouse, canvas_min, canvas_size, 9.0f);
        ImGui::OpenPopup("##EasingControlPointMenu");
    }
    if (ImGui::BeginPopup("##EasingControlPointMenu"))
    {
        const bool can_add = easing_editor_context_control_point < 0;
        if (ImGui::MenuItem("制御点を追加", nullptr, false, can_add))
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
            easing_editor_dirty = true;
            easing_editor_status = "制御点を追加しました";
        }
        const int selected = easing_editor_context_control_point;
        const bool can_delete = selected > 0 &&
            selected + 1 < static_cast<int>(easing_editor_asset.control_points.size());
        if (ImGui::MenuItem("制御点を削除", nullptr, false, can_delete))
        {
            easing_editor_asset.control_points.erase(
                easing_editor_asset.control_points.begin() + selected);
            easing_editor_asset.RebuildSamplesFromControlPoints();
            easing_editor_dirty = true;
            easing_editor_status = "制御点を削除しました";
        }
        ImGui::EndPopup();
    }

    if (!easing_editor_asset.samples.empty())
    {
        float min_y = easing_editor_asset.samples.front();
        float max_y = min_y;
        for (float value : easing_editor_asset.samples)
        {
            min_y = (std::min)(min_y, value);
            max_y = (std::max)(max_y, value);
        }
        ImGui::Text("samples: %d / control points: %d / Y: %.3f .. %.3f",
            easing_editor_asset.sample_count,
            static_cast<int>(easing_editor_asset.control_points.size()), min_y, max_y);
    }
    if (!easing_editor_status.empty())
        ImGui::TextWrapped("%s", easing_editor_status.c_str());

    ImGui::End();
}
