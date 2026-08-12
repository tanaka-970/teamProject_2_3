#pragma once

#include "LineRendererComponent.h"

#include <deque>

namespace ReplayEngine::Components
{
    class TrailComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(TrailComponent)

    public:
        struct RuntimePoint final
        {
            DirectX::XMFLOAT3 position{};
            float life_remaining = 0.0f;
        };

        TrailComponent() = default;

        void UpdateRuntime(float elapsed_time,
            const DirectX::XMFLOAT3& sampled_position);
        void RuntimePath(std::vector<DirectX::XMFLOAT3>& points,
            std::vector<float>& alpha) const;
        Rendering::LineStrokeStyle StrokeStyle() const;

        bool emitting = true;
        float lifetime = 1.0f;
        float min_distance = 0.05f;
        int max_points = 0;
        bool world_space = true;

        float width_start = 0.1f;
        float width_end = 0.1f;
        bool billboard = true;
        int uv_mode = LineRendererComponent::Stretch;
        float uv_tiling = 1.0f;
        float uv_scroll = 0.0f;
        Reflection::AssetReference texture;
        DirectX::XMFLOAT4 fill_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 fill_color_2{ 1.0f, 1.0f, 1.0f, 1.0f };
        int fill_mode = LineRendererComponent::Solid;
        float trim_start = 0.0f;
        float trim_end = 1.0f;
        float trim_offset = 0.0f;

    private:
        // 古い点を先頭から頻繁に落とすため vector ではなく deque を使う。
        // 固定長上限は持たず、寿命と任意の max_points だけで減らす。
        std::deque<RuntimePoint> runtime_points_;
    };
}
