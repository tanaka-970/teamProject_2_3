#pragma once

#include "../Assets/VertexColorAsset.h"
#include <DirectXCollision.h>
#include <memory>

namespace ReplayEngine::VertexPaint
{
    enum class PaintMode { Add, Subtract, Replace, Smooth };

    struct Brush final
    {
        float radius = 0.25f;
        float strength = 1.0f;
        float falloff = 1.0f;
        float value = 0.0f;
        int channel = 0;
        PaintMode mode = PaintMode::Subtract;
    };

    struct RayHit final
    {
        bool hit = false;
        float distance = 0.0f;
        std::size_t face = static_cast<std::size_t>(-1);
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 barycentric{};
    };

    struct SurfaceRegion final
    {
        std::vector<std::uint32_t> faces, vertices;
    };

    class MeshSurface final
    {
    public:
        static RayHit RaycastMesh(const std::vector<DirectX::XMFLOAT3>& positions,
            const std::vector<std::uint32_t>& indices, const DirectX::XMFLOAT4X4& world,
            const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction, float max_distance);
        bool Initialize(const std::vector<DirectX::XMFLOAT3>& positions,
            const std::vector<std::uint32_t>& indices);
        bool SetPositions(const std::vector<DirectX::XMFLOAT3>& positions,
            const DirectX::XMFLOAT4X4& world);
        RayHit Raycast(const DirectX::XMFLOAT3& origin,
            const DirectX::XMFLOAT3& direction, float max_distance) const;
        SurfaceRegion QuerySurface(const DirectX::XMFLOAT3& center,
            float radius, std::size_t seed_face) const;
        bool Paint(std::vector<Assets::VertexColorRgba8>& colors,
            const SurfaceRegion& region, const DirectX::XMFLOAT3& center,
            const Brush& brush, float seconds, std::vector<float>& channel_values) const;

    private:
        std::vector<DirectX::XMFLOAT3> positions_;
        std::vector<std::uint32_t> indices_, groups_;
        std::vector<std::vector<std::uint32_t>> welded_, faces_, neighbors_;
        DirectX::BoundingBox bounds_{};
        bool positioned_ = false;
    };

    struct PaintAsset final
    {
        Assets::VertexColorAsset colors;
        std::uint64_t revision = 1;
        bool dirty = false;
    };

    struct ColorEdit final
    {
        std::shared_ptr<PaintAsset> target;
        Assets::VertexColorAsset before, after;
        void Apply(bool redo) const;
    };
}
