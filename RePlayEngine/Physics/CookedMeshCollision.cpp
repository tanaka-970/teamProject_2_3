#include "CookedMeshCollision.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace ReplayEngine::Physics
{
    namespace
    {
        constexpr float minimum_cell_size = 0.05f;
        constexpr int maximum_cells_per_axis = 512;
    }

    std::shared_ptr<const CookedMeshCollisionData> CookedMeshCollisionData::Build(
        std::string asset_guid, std::vector<Triangle> local_triangles,
        const Settings& settings)
    {
        // コンストラクタが private なので make_shared は使えない。
        // shared_ptr へ即座に載せるので所有権は明確。
        std::shared_ptr<CookedMeshCollisionData> data(new CookedMeshCollisionData());
        data->asset_guid_ = std::move(asset_guid);
        data->settings_ = settings;
        data->settings_.cell_size = std::max(minimum_cell_size, settings.cell_size);
        data->triangles_ = std::move(local_triangles);
        data->BuildGrid();
        return data;
    }

    void CookedMeshCollisionData::BuildGrid()
    {
        cells_.clear();
        visit_stamps_.clear();
        current_stamp_ = 0;
        cell_count_x_ = 0;
        cell_count_z_ = 0;
        bounds_min_ = { 0.0f, 0.0f, 0.0f };
        bounds_max_ = { 0.0f, 0.0f, 0.0f };

        if (triangles_.empty()) return;

        // ローカル空間の AABB を求める。
        bounds_min_ = { FLT_MAX, FLT_MAX, FLT_MAX };
        bounds_max_ = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (const Triangle& triangle : triangles_)
        {
            for (const XMFLOAT3& vertex : triangle.vertices)
            {
                bounds_min_.x = std::min(bounds_min_.x, vertex.x);
                bounds_min_.y = std::min(bounds_min_.y, vertex.y);
                bounds_min_.z = std::min(bounds_min_.z, vertex.z);
                bounds_max_.x = std::max(bounds_max_.x, vertex.x);
                bounds_max_.y = std::max(bounds_max_.y, vertex.y);
                bounds_max_.z = std::max(bounds_max_.z, vertex.z);
            }
        }

        const float cell = settings_.cell_size;
        const float width = std::max(0.0f, bounds_max_.x - bounds_min_.x);
        const float depth = std::max(0.0f, bounds_max_.z - bounds_min_.z);

        cell_count_x_ = std::max(1, static_cast<int>(width / cell) + 1);
        cell_count_z_ = std::max(1, static_cast<int>(depth / cell) + 1);

        // メッシュが極端に大きい・セルが極端に小さい場合にメモリが爆発しないよう抑える。
        cell_count_x_ = std::min(cell_count_x_, maximum_cells_per_axis);
        cell_count_z_ = std::min(cell_count_z_, maximum_cells_per_axis);

        cells_.assign(static_cast<std::size_t>(cell_count_x_) *
            static_cast<std::size_t>(cell_count_z_), {});
        visit_stamps_.assign(triangles_.size(), 0);

        // 三角形を、跨るすべてのセルへ登録する。
        for (std::uint32_t index = 0; index < triangles_.size(); ++index)
        {
            const Triangle& triangle = triangles_[index];
            const float min_x = std::min({ triangle.vertices[0].x, triangle.vertices[1].x, triangle.vertices[2].x });
            const float max_x = std::max({ triangle.vertices[0].x, triangle.vertices[1].x, triangle.vertices[2].x });
            const float min_z = std::min({ triangle.vertices[0].z, triangle.vertices[1].z, triangle.vertices[2].z });
            const float max_z = std::max({ triangle.vertices[0].z, triangle.vertices[1].z, triangle.vertices[2].z });

            int x0 = 0, x1 = 0, z0 = 0, z1 = 0;
            CellRange(min_x, max_x, min_z, max_z, x0, x1, z0, z1);
            for (int z = z0; z <= z1; ++z)
            {
                for (int x = x0; x <= x1; ++x)
                {
                    cells_[static_cast<std::size_t>(z) * cell_count_x_ + x].push_back(index);
                }
            }
        }
    }

    void CookedMeshCollisionData::CellRange(float min_x, float max_x, float min_z, float max_z,
        int& x0, int& x1, int& z0, int& z1) const noexcept
    {
        const float cell = settings_.cell_size;
        x0 = static_cast<int>((min_x - bounds_min_.x) / cell);
        x1 = static_cast<int>((max_x - bounds_min_.x) / cell);
        z0 = static_cast<int>((min_z - bounds_min_.z) / cell);
        z1 = static_cast<int>((max_z - bounds_min_.z) / cell);

        x0 = std::max(0, std::min(x0, cell_count_x_ - 1));
        x1 = std::max(0, std::min(x1, cell_count_x_ - 1));
        z0 = std::max(0, std::min(z0, cell_count_z_ - 1));
        z1 = std::max(0, std::min(z1, cell_count_z_ - 1));
    }

    void CookedMeshCollisionData::CollectTriangles(const XMFLOAT3& local_aabb_min,
        const XMFLOAT3& local_aabb_max, std::vector<std::uint32_t>& out) const
    {
        out.clear();
        if (triangles_.empty() || cells_.empty()) return;

        // AABB がローカル境界の外なら何も返さない。
        if (local_aabb_max.x < bounds_min_.x || local_aabb_min.x > bounds_max_.x) return;
        if (local_aabb_max.z < bounds_min_.z || local_aabb_min.z > bounds_max_.z) return;
        if (local_aabb_max.y < bounds_min_.y || local_aabb_min.y > bounds_max_.y) return;

        int x0 = 0, x1 = 0, z0 = 0, z1 = 0;
        CellRange(local_aabb_min.x, local_aabb_max.x,
            local_aabb_min.z, local_aabb_max.z, x0, x1, z0, z1);

        // 訪問スタンプを更新して重複を弾く。
        // ラップアラウンドしたときだけ全体をクリアする。
        ++current_stamp_;
        if (current_stamp_ == 0)
        {
            std::fill(visit_stamps_.begin(), visit_stamps_.end(), 0);
            current_stamp_ = 1;
        }

        for (int z = z0; z <= z1; ++z)
        {
            for (int x = x0; x <= x1; ++x)
            {
                for (const std::uint32_t index :
                    cells_[static_cast<std::size_t>(z) * cell_count_x_ + x])
                {
                    if (visit_stamps_[index] == current_stamp_) continue;
                    visit_stamps_[index] = current_stamp_;
                    out.push_back(index);
                }
            }
        }
    }

    // -----------------------------------------------------------------------

    std::shared_ptr<const CookedMeshCollisionData> CookedMeshCollisionCache::Acquire(
        const std::string& asset_guid,
        const CookedMeshCollisionData::Settings& settings,
        const Loader& loader)
    {
        if (asset_guid.empty()) return nullptr;

        // 一度失敗した Asset は再試行しない。毎フレーム同じ読み込みを走らせないため。
        const auto failure = failures_.find(asset_guid);
        if (failure != failures_.end() && failure->second) return nullptr;

        const auto found = entries_.find(asset_guid);
        if (found != entries_.end())
        {
            // Cook 設定が変わっていたら作り直す。
            if (found->second.settings == settings) return found->second.data;
            entries_.erase(found);
        }

        if (!loader)
        {
            failures_[asset_guid] = true;
            return nullptr;
        }

        std::vector<Triangle> local_triangles;
        if (!loader(asset_guid, local_triangles) || local_triangles.empty())
        {
            // 無効な Collider を作らせない。呼び出し側は nullptr を見て登録を諦める。
            failures_[asset_guid] = true;
            return nullptr;
        }

        Entry entry;
        entry.settings = settings;
        entry.data = CookedMeshCollisionData::Build(
            asset_guid, std::move(local_triangles), settings);

        const auto inserted = entries_.emplace(asset_guid, std::move(entry));
        return inserted.first->second.data;
    }

    void CookedMeshCollisionCache::Invalidate(const std::string& asset_guid)
    {
        entries_.erase(asset_guid);
        failures_.erase(asset_guid);
    }

    void CookedMeshCollisionCache::Clear() noexcept
    {
        entries_.clear();
        failures_.clear();
    }

    bool CookedMeshCollisionCache::Failed(const std::string& asset_guid) const
    {
        const auto found = failures_.find(asset_guid);
        return found != failures_.end() && found->second;
    }
}
