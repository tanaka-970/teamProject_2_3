#include "LandscapeRendererComponent.h"

#include <cmath>

namespace ReplayEngine::Components
{
    bool LandscapeRendererComponent::ShouldSubmitChunk(
        const DirectX::XMFLOAT3& camera_position,
        const DirectX::XMFLOAT3& bounds_min, const DirectX::XMFLOAT3& bounds_max,
        bool resident) const noexcept
    {
        if (!(load_range > 0.0f) || !std::isfinite(load_range)) return true;
        const float range = load_range * (resident ? unload_hysteresis_scale : 1.0f);
        const auto axis_distance = [](float point, float minimum, float maximum) noexcept
        {
            if (point < minimum) return minimum - point;
            if (point > maximum) return point - maximum;
            return 0.0f;
        };
        const float x = axis_distance(camera_position.x, bounds_min.x, bounds_max.x);
        const float y = axis_distance(camera_position.y, bounds_min.y, bounds_max.y);
        const float z = axis_distance(camera_position.z, bounds_min.z, bounds_max.z);
        return x * x + y * y + z * z <= range * range;
    }
}
