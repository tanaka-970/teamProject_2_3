#include "LandscapeColliderComponent.h"
#include "LandscapeComponent.h"
#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <utility>

using namespace DirectX;

namespace ReplayEngine::Components
{
    void LandscapeColliderComponent::RebuildLegacyTriangles() const
    {
        if (!legacy_triangles_dirty_) return;
        triangles_.clear();
        std::size_t triangle_count = 0;
        bool face_order_available = true;
        for (const CookedChunk& chunk : cooked_chunks_)
        {
            if (chunk.cooked == nullptr) continue;
            triangle_count += chunk.cooked->TriangleCount();
            face_order_available = face_order_available &&
                chunk.face_indices.size() == chunk.cooked->TriangleCount();
        }
        if (face_order_available) triangles_.resize(triangle_count);
        else triangles_.reserve(triangle_count);
        for (const CookedChunk& chunk : cooked_chunks_)
        {
            if (chunk.cooked == nullptr || !chunk.cooked->Valid()) continue;
            const Physics::Triangle* source = chunk.cooked->Triangles();
            if (!face_order_available)
            {
                triangles_.insert(triangles_.end(), source,
                    source + chunk.cooked->TriangleCount());
                continue;
            }
            for (std::size_t i = 0; i < chunk.cooked->TriangleCount(); ++i)
                if (chunk.face_indices[i] < triangles_.size())
                    triangles_[chunk.face_indices[i]] = source[i];
        }
        legacy_triangles_dirty_ = false;
    }

    const std::vector<Physics::Triangle>& LandscapeColliderComponent::Triangles() const
    {
        RebuildLegacyTriangles();
        return triangles_;
    }

    const std::shared_ptr<const Physics::CookedMeshCollisionData>&
        LandscapeColliderComponent::Cooked() const
    {
        if (!legacy_cooked_dirty_) return cooked_;
        RebuildLegacyTriangles();
        if (triangles_.empty())
        {
            cooked_.reset();
        }
        else
        {
            Physics::CookKey key;
            key.asset_guid = "runtime-landscape-legacy";
            key.content_revision = std::to_string(geometry_revision_);
            key.settings.cell_size = cooked_cell_size_;
            key.settings.double_sided = cooked_double_sided_;
            key.settings.sub_mesh_index = -1;
            cooked_ = Physics::CookedMeshCollisionData::Build(key, triangles_);
        }
        legacy_cooked_dirty_ = false;
        return cooked_;
    }

    bool LandscapeColliderComponent::RefreshGeometryIfChanged(
        Physics::CookedMeshCollisionCache* shared_cook_cache)
    {
        last_recooked_chunk_count_ = 0;
        last_recooked_triangle_count_ = 0;
        const Core::GameObject* owner = Owner();
        if (owner == nullptr)
        {
            status_ = "Owner GameObject がありません。";
            cooked_chunks_.clear();
            triangles_.clear();
            cooked_.reset();
            cooked_chunk_count_ = 0;
            return false;
        }
        const auto* landscape = owner->GetComponent<LandscapeComponent>();
        if (landscape == nullptr || !landscape->Data().Valid())
        {
            status_ = "Landscape Component が無いか、geometry が無効です。";
            cooked_chunks_.clear();
            triangles_.clear();
            cooked_.reset();
            cooked_chunk_count_ = 0;
            return false;
        }

        bool changed = false;
        const auto& data = landscape->Data();
        const float requested_cell_size = (std::max)(0.05f, collision_cell_size);
        const bool geometry_changed = geometry_revision_ != data.Revision();
        const bool cook_settings_changed = cooked_cell_size_ <= 0.0f ||
            cooked_double_sided_ != double_sided ||
            std::fabs(cooked_cell_size_ - requested_cell_size) > 1.0e-6f;
        const auto& source_chunks = data.Chunks();
        bool chunk_layout_changed = cooked_chunks_.size() != source_chunks.size();
        if (!chunk_layout_changed)
        {
            for (std::size_t i = 0; i < source_chunks.size(); ++i)
            {
                if (!(cooked_chunks_[i].coord == source_chunks[i].coord))
                {
                    chunk_layout_changed = true;
                    break;
                }
            }
        }

        // ドラッグ終了後に revision が変わったチャンクだけを一度 cook する。
        if ((geometry_changed || cook_settings_changed || chunk_layout_changed) &&
            !interactive_edit_active_)
        {
            if (chunk_layout_changed)
                cooked_chunks_.assign(source_chunks.size(), CookedChunk{});

            const auto& vertices = data.Vertices();
            for (std::size_t chunk_index = 0; chunk_index < source_chunks.size(); ++chunk_index)
            {
                const auto& source_chunk = source_chunks[chunk_index];
                CookedChunk& target_chunk = cooked_chunks_[chunk_index];
                if (!(target_chunk.coord == source_chunk.coord))
                {
                    target_chunk = {};
                    target_chunk.coord = source_chunk.coord;
                }
                const bool chunk_changed = cook_settings_changed ||
                    target_chunk.source_revision != source_chunk.revision ||
                    target_chunk.cooked == nullptr;
                if (!chunk_changed) continue;

                Physics::CookKey key;
                // LandscapeData::revision は Play clone でも元 geometry の値を維持する。
                // そのため Editor と Runtime World が同じ key になり、重い grid Cook を
                // Play のたびに作り直さず shared cache から取得できる。
                key.asset_guid = "runtime-landscape:" + std::to_string(data.Revision()) +
                    ":" + std::to_string(source_chunk.coord.x) +
                    ":" + std::to_string(source_chunk.coord.z);
                key.content_revision = std::to_string(data.TopologyRevision());
                key.settings.cell_size = requested_cell_size;
                key.settings.double_sided = double_sided;
                key.settings.sub_mesh_index = -1;

                bool cooked_now = false;
                const auto build_triangles = [&vertices, &source_chunk, &cooked_now](
                    const Physics::CookKey&, std::vector<Physics::Triangle>& out)
                {
                    cooked_now = true;
                    out.clear();
                    out.reserve(source_chunk.indices.size() / 3u);
                    for (std::size_t i = 0; i + 2 < source_chunk.indices.size(); i += 3)
                    {
                        Physics::Triangle triangle{};
                        triangle.vertices[0] = vertices[source_chunk.indices[i]].position;
                        triangle.vertices[1] = vertices[source_chunk.indices[i + 1]].position;
                        triangle.vertices[2] = vertices[source_chunk.indices[i + 2]].position;
                        triangle.material_index = 0;
                        out.push_back(triangle);
                    }
                    return !out.empty();
                };

                if (shared_cook_cache != nullptr)
                {
                    // cache hit なら build_triangles 自体が呼ばれない。三角形配列の生成も
                    // spatial grid Cook もゼロになる。
                    target_chunk.cooked = shared_cook_cache->Acquire(key, build_triangles);
                }
                else
                {
                    std::vector<Physics::Triangle> chunk_triangles;
                    build_triangles(key, chunk_triangles);
                    target_chunk.cooked = Physics::CookedMeshCollisionData::Build(
                        std::move(key), std::move(chunk_triangles));
                }
                target_chunk.source_revision = source_chunk.revision;
                target_chunk.face_indices = data.ChunkFaceIndices(chunk_index);
                if (cooked_now)
                {
                    ++last_recooked_chunk_count_;
                    last_recooked_triangle_count_ += source_chunk.indices.size() / 3u;
                }
            }

            geometry_revision_ = data.Revision();
            cooked_double_sided_ = double_sided;
            cooked_cell_size_ = requested_cell_size;
            cooked_chunk_count_ = 0;
            for (const CookedChunk& chunk : cooked_chunks_)
                if (chunk.cooked != nullptr && chunk.cooked->Valid()) ++cooked_chunk_count_;
            if (last_recooked_chunk_count_ != 0 || chunk_layout_changed)
            {
                legacy_triangles_dirty_ = true;
                legacy_cooked_dirty_ = true;
                cooked_.reset();
                changed = true;
            }
        }

        XMFLOAT4X4 current_world = owner->GetTransform().WorldMatrixFloat4x4();
        current_world._41 += center_offset.x;
        current_world._42 += center_offset.y;
        current_world._43 += center_offset.z;
        if (!transform_valid_ || std::memcmp(&current_world, &cached_world_, sizeof(current_world)) != 0 || changed)
        {
            cached_world_ = current_world;
            world_ = current_world;
            transform_valid_ = true;

            const XMMATRIX world = XMLoadFloat4x4(&world_);
            XMVECTOR determinant{};
            XMStoreFloat4x4(&inverse_world_, XMMatrixInverse(&determinant, world));

            const XMFLOAT3 scale = owner->GetTransform().WorldScale();
            const float ax = std::fabs(scale.x), ay = std::fabs(scale.y), az = std::fabs(scale.z);
            const float minimum = (std::max)(1.0e-5f, (std::min)({ ax, ay, az }));
            negative_scale_ = scale.x * scale.y * scale.z < 0.0f;
            local_radius_scale_ = 1.0f / minimum;

            const XMFLOAT3 local_min = data.BoundsMin();
            const XMFLOAT3 local_max = data.BoundsMax();
            world_bounds_min_ = { FLT_MAX, FLT_MAX, FLT_MAX };
            world_bounds_max_ = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            for (int corner = 0; corner < 8; ++corner)
            {
                const XMFLOAT3 point{
                    (corner & 1) ? local_max.x : local_min.x,
                    (corner & 2) ? local_max.y : local_min.y,
                    (corner & 4) ? local_max.z : local_min.z };
                XMFLOAT3 transformed{};
                XMStoreFloat3(&transformed,
                    XMVector3TransformCoord(XMLoadFloat3(&point), world));
                world_bounds_min_.x = (std::min)(world_bounds_min_.x, transformed.x);
                world_bounds_min_.y = (std::min)(world_bounds_min_.y, transformed.y);
                world_bounds_min_.z = (std::min)(world_bounds_min_.z, transformed.z);
                world_bounds_max_.x = (std::max)(world_bounds_max_.x, transformed.x);
                world_bounds_max_.y = (std::max)(world_bounds_max_.y, transformed.y);
                world_bounds_max_.z = (std::max)(world_bounds_max_.z, transformed.z);
            }
            changed = true;
        }

        status_.clear();
        return changed;
    }

    bool LandscapeColliderComponent::ComputeWorldBounds(XMFLOAT3& minimum,
        XMFLOAT3& maximum) const
    {
        if (cooked_chunk_count_ == 0 || !transform_valid_) return false;
        minimum = world_bounds_min_;
        maximum = world_bounds_max_;
        return true;
    }
}
