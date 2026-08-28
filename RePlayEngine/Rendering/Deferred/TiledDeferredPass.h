#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering
{
    class TiledDeferredPass final
    {
    public:
        struct Light
        {
            DirectX::XMFLOAT4 position_radius{ 0, 0, 0, 0 };
            DirectX::XMFLOAT4 color_intensity{ 1, 1, 1, 1 };
            DirectX::XMFLOAT4 direction_cone{ 0, -1, 0, 1 };
            DirectX::XMFLOAT4 params{ 0, 0, -1, 1 };
        };
        enum class LightType : int { Point = 0, Spot = 1 };
        static constexpr std::uint32_t kTileSize = 16;
        void ClearLights() noexcept { lights_.clear(); }
        void AddPointLight(const DirectX::XMFLOAT3& position, float radius,
            const DirectX::XMFLOAT3& color, float intensity,
            int shadow_slice = -1, float shadow_strength = 1.0f)
        {
            Light light{};
            light.position_radius = { position.x, position.y, position.z, radius };
            light.color_intensity = { color.x, color.y, color.z, intensity };
            light.params = { 0.0f, static_cast<float>(LightType::Point),
                static_cast<float>(shadow_slice), shadow_strength };
            lights_.push_back(light);
        }
        void AddSpotLight(const DirectX::XMFLOAT3& position, float radius,
            const DirectX::XMFLOAT3& direction, float inner_cosine, float outer_cosine,
            const DirectX::XMFLOAT3& color, float intensity,
            int shadow_slice = -1, float shadow_strength = 1.0f)
        {
            Light light{};
            light.position_radius = { position.x, position.y, position.z, radius };
            light.color_intensity = { color.x, color.y, color.z, intensity };
            light.direction_cone = { direction.x, direction.y, direction.z, inner_cosine };
            light.params = { outer_cosine, static_cast<float>(LightType::Spot),
                static_cast<float>(shadow_slice), shadow_strength };
            lights_.push_back(light);
        }
        std::size_t LightCount() const noexcept { return lights_.size(); }
        bool Initialized() const noexcept { return true; }
        std::uint32_t TileCountX() const noexcept { return tile_count_x_; }
        std::uint32_t TileCountY() const noexcept { return tile_count_y_; }
        void SetViewport(std::uint32_t width, std::uint32_t height) noexcept
        {
            tile_count_x_ = (width + kTileSize - 1u) / kTileSize;
            tile_count_y_ = (height + kTileSize - 1u) / kTileSize;
        }
        bool enabled = false;
        bool debug_heatmap = false;
        float heatmap_scale = 32.0f;
    private:
        std::vector<Light> lights_;
        std::uint32_t tile_count_x_ = 0;
        std::uint32_t tile_count_y_ = 0;
    };
}
