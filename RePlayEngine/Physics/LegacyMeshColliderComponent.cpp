// 【Legacy】旧 ReplayEngine::Core::MeshColliderComponent の実装。
// 新基盤は Components/Physics/MeshColliderComponent（ローカル Cook 共有方式）。
// これは旧 Stage がまだ使っているため残している。Stage 移行完了時に削除する。
// ファイル名を分けているのは、MSVC が obj をフラットに出す構成で
// 同名 .cpp が衝突するのを避けるため。
#include "../Core/Components/MeshColliderComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ReplayEngine::Core
{
    void MeshColliderComponent::Clear()
    {
        triangles_.clear();
        cells_.clear();
        visit_stamps_.clear();
        cell_count_x_ = cell_count_z_ = 0;
        current_stamp_ = 0;
    }

    void MeshColliderComponent::Build(std::vector<Physics::Triangle> triangles, float cell_size)
    {
        Clear();
        triangles_ = std::move(triangles);
        if (triangles_.empty()) return;
        cell_size_ = (std::max)(cell_size, 0.25f);

        float min_x = (std::numeric_limits<float>::max)();
        float min_z = (std::numeric_limits<float>::max)();
        float max_x = std::numeric_limits<float>::lowest();
        float max_z = std::numeric_limits<float>::lowest();
        for (const auto& triangle : triangles_)
        {
            for (const auto& vertex : triangle.vertices)
            {
                min_x = (std::min)(min_x, vertex.x); min_z = (std::min)(min_z, vertex.z);
                max_x = (std::max)(max_x, vertex.x); max_z = (std::max)(max_z, vertex.z);
            }
        }

        min_x_ = min_x; min_z_ = min_z;
        const float width = (std::max)(max_x - min_x, cell_size_);
        const float depth = (std::max)(max_z - min_z, cell_size_);
        constexpr int maximum_cells_per_axis = 256;
        while (width / cell_size_ > maximum_cells_per_axis ||
               depth / cell_size_ > maximum_cells_per_axis)
            cell_size_ *= 2.0f;
        cell_count_x_ = (std::max)(1, static_cast<int>(std::ceil(width / cell_size_)));
        cell_count_z_ = (std::max)(1, static_cast<int>(std::ceil(depth / cell_size_)));
        cells_.resize(static_cast<std::size_t>(cell_count_x_) * cell_count_z_);

        for (std::uint32_t index = 0; index < triangles_.size(); ++index)
        {
            const auto& v = triangles_[index].vertices;
            int x0, x1, z0, z1;
            CellRange((std::min)({ v[0].x, v[1].x, v[2].x }),
                (std::max)({ v[0].x, v[1].x, v[2].x }),
                (std::min)({ v[0].z, v[1].z, v[2].z }),
                (std::max)({ v[0].z, v[1].z, v[2].z }), x0, x1, z0, z1);
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                    cells_[static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * cell_count_x_].push_back(index);
        }
        visit_stamps_.assign(triangles_.size(), 0);
    }

    void MeshColliderComponent::CellRange(float min_x, float max_x,
        float min_z, float max_z, int& x0, int& x1, int& z0, int& z1) const noexcept
    {
        const float inverse = 1.0f / cell_size_;
        x0 = (std::clamp)(static_cast<int>(std::floor((min_x - min_x_) * inverse)), 0, cell_count_x_ - 1);
        x1 = (std::clamp)(static_cast<int>(std::floor((max_x - min_x_) * inverse)), 0, cell_count_x_ - 1);
        z0 = (std::clamp)(static_cast<int>(std::floor((min_z - min_z_) * inverse)), 0, cell_count_z_ - 1);
        z1 = (std::clamp)(static_cast<int>(std::floor((max_z - min_z_) * inverse)), 0, cell_count_z_ - 1);
    }

    void MeshColliderComponent::CollectTriangles(const DirectX::XMFLOAT3& aabb_min,
        const DirectX::XMFLOAT3& aabb_max, std::vector<std::uint32_t>& indices) const
    {
        indices.clear();
        if (cells_.empty() || !Enabled()) return;
        if (aabb_max.x < min_x_ || aabb_max.z < min_z_ ||
            aabb_min.x > min_x_ + cell_size_ * cell_count_x_ ||
            aabb_min.z > min_z_ + cell_size_ * cell_count_z_) return;

        if (++current_stamp_ == 0)
        {
            std::fill(visit_stamps_.begin(), visit_stamps_.end(), 0u);
            current_stamp_ = 1;
        }
        int x0, x1, z0, z1;
        CellRange(aabb_min.x, aabb_max.x, aabb_min.z, aabb_max.z, x0, x1, z0, z1);
        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const auto& cell = cells_[static_cast<std::size_t>(x) + static_cast<std::size_t>(z) * cell_count_x_];
                for (std::uint32_t index : cell)
                {
                    if (visit_stamps_[index] == current_stamp_) continue;
                    visit_stamps_[index] = current_stamp_;
                    indices.push_back(index);
                }
            }
        }
    }
}
