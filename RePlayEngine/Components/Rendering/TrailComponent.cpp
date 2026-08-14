#include "TrailComponent.h"

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Components
{
    void TrailComponent::UpdateRuntime(float elapsed_time,
        const DirectX::XMFLOAT3& sampled_position)
    {
        const float delta_time = (std::max)(0.0f, elapsed_time);
        for (auto point = runtime_points_.begin(); point != runtime_points_.end();)
        {
            point->life_remaining -= delta_time;
            if (point->life_remaining <= 0.0f)
                point = runtime_points_.erase(point);
            else
                ++point;
        }

        const float safe_lifetime = (std::max)(0.0f, lifetime);
        if (!emitting || safe_lifetime <= 0.0f) return;

        bool append = runtime_points_.empty();
        if (!append)
        {
            const DirectX::XMFLOAT3& previous = runtime_points_.back().position;
            const float x = sampled_position.x - previous.x;
            const float y = sampled_position.y - previous.y;
            const float z = sampled_position.z - previous.z;
            append = std::sqrt(x * x + y * y + z * z) >=
                (std::max)(0.0f, min_distance);
        }
        if (append)
        {
            try
            {
                runtime_points_.push_back({ sampled_position, safe_lifetime });
            }
            catch (...)
            {
                // 異常な点数で確保に失敗しても Scene 全体の更新は続ける。
                return;
            }
        }

        if (max_points > 0)
        {
            while (runtime_points_.size() > static_cast<std::size_t>(max_points))
                runtime_points_.pop_front();
        }
    }

    void TrailComponent::RuntimePath(std::vector<DirectX::XMFLOAT3>& points,
        std::vector<float>& alpha) const
    {
        points.clear();
        alpha.clear();
        try
        {
            points.reserve(runtime_points_.size());
            alpha.reserve(runtime_points_.size());
            const float safe_lifetime = (std::max)(0.0001f, lifetime);
            for (const RuntimePoint& point : runtime_points_)
            {
                points.push_back(point.position);
                alpha.push_back((std::max)(0.0f,
                    (std::min)(1.0f, point.life_remaining / safe_lifetime)));
            }
        }
        catch (...)
        {
            points.clear();
            alpha.clear();
        }
    }

    Rendering::LineStrokeStyle TrailComponent::StrokeStyle() const
    {
        Rendering::LineStrokeStyle style;
        style.width_start = width_start;
        style.width_end = width_end;
        style.billboard = billboard;
        style.uv_mode = uv_mode;
        style.uv_tiling = uv_tiling;
        style.uv_scroll = uv_scroll;
        style.texture_guid = texture.guid;
        style.fill_color = fill_color;
        style.fill_color_2 = fill_color_2;
        style.fill_mode = fill_mode;
        style.trim_start = trim_start;
        style.trim_end = trim_end;
        style.trim_offset = trim_offset;
        style.closed = false;
        return style;
    }
}
