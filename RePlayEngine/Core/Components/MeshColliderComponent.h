#pragma once

#include "IComponent.h"
#include "../../Physics/SphereCast.h"

#include <vector>

namespace ReplayEngine::Core
{
// チーム制作と同じXZ空間グリッド方式を使う静的三角形noコライダー。
// 描画資産と衝突判定を分離して管理する。
    class MeshColliderComponent final : public IComponent
    {
    public:
        void Build(std::vector<Physics::Triangle> triangles, float cell_size = 4.0f);
        void Clear();

        bool Valid() const noexcept { return !triangles_.empty(); }
        float CellSize() const noexcept { return cell_size_; }
        std::size_t TriangleCount() const noexcept { return triangles_.size(); }
        const Physics::Triangle& TriangleAt(std::size_t index) const { return triangles_.at(index); }

        void CollectTriangles(const DirectX::XMFLOAT3& aabb_min,
            const DirectX::XMFLOAT3& aabb_max,
            std::vector<std::uint32_t>& indices) const;

    private:
        void CellRange(float min_x, float max_x, float min_z, float max_z,
            int& x0, int& x1, int& z0, int& z1) const noexcept;

        std::vector<Physics::Triangle> triangles_;
        std::vector<std::vector<std::uint32_t>> cells_;
        float min_x_ = 0.0f;
        float min_z_ = 0.0f;
        float cell_size_ = 4.0f;
        int cell_count_x_ = 0;
        int cell_count_z_ = 0;
        mutable std::vector<std::uint32_t> visit_stamps_;
        mutable std::uint32_t current_stamp_ = 0;
    };
}
