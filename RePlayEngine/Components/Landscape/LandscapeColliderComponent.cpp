#include "LandscapeColliderComponent.h"
#include "LandscapeComponent.h"
#include "../../Object/GameObject/GameObject.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

using namespace DirectX;

namespace ReplayEngine::Components
{
    bool LandscapeColliderComponent::RefreshGeometryIfChanged()
    {
        const Core::GameObject* owner = Owner();
        if (owner == nullptr)
        {
            status_ = "Owner GameObject がありません。";
            triangles_.clear();
            cooked_.reset();
            return false;
        }
        const auto* landscape = owner->GetComponent<LandscapeComponent>();
        if (landscape == nullptr || !landscape->Data().Valid())
        {
            status_ = "Landscape Component が無いか、geometry が無効です。";
            triangles_.clear();
            cooked_.reset();
            return false;
        }

        bool changed = false;
        const auto& data = landscape->Data();
        const float requested_cell_size = (std::max)(0.05f, collision_cell_size);
        const bool geometry_changed = geometry_revision_ != data.Revision();
        const bool cook_settings_changed = cooked_ == nullptr ||
            cooked_double_sided_ != double_sided ||
            std::fabs(cooked_cell_size_ - requested_cell_size) > 1.0e-6f;
        // Sculpt のドラッグ中に毎フレーム 8k+ triangles を再Cookしない。
        // 既存 cooked geometry はそのまま問い合わせ可能で、終了後の最初の Refresh で
        // 最新 revision へ一度だけ追いつく。
        if ((geometry_changed || cook_settings_changed) && !interactive_edit_active_)
        {
            if (geometry_changed)
            {
                triangles_.clear();
                triangles_.reserve(data.FaceCount());
                const auto& vertices = data.Vertices();
                const auto& indices = data.Indices();
                for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
                {
                    Physics::Triangle triangle{};
                    triangle.vertices[0] = vertices[indices[i]].position;
                    triangle.vertices[1] = vertices[indices[i + 1]].position;
                    triangle.vertices[2] = vertices[indices[i + 2]].position;
                    triangle.material_index = 0;
                    triangles_.push_back(triangle);
                }
                geometry_revision_ = data.Revision();
            }

            // MeshCollider と同じ XZ spatial grid を Landscape にも利用する。
            // AssetGUID cache は使わない。Sculpt/Topology edit の revision が変わった
            // ときだけ、この Component が local triangles から cook し直す。
            Physics::CookKey key;
            key.asset_guid = "runtime-landscape";
            key.content_revision = std::to_string(data.Revision());
            key.settings.cell_size = requested_cell_size;
            key.settings.double_sided = double_sided;
            key.settings.sub_mesh_index = -1;
            cooked_ = Physics::CookedMeshCollisionData::Build(key, triangles_);
            cooked_double_sided_ = double_sided;
            cooked_cell_size_ = requested_cell_size;
            changed = true;
        }

        const XMFLOAT4X4 current_world = owner->GetTransform().WorldMatrixFloat4x4();
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
        if (triangles_.empty() || !transform_valid_) return false;
        minimum = world_bounds_min_;
        maximum = world_bounds_max_;
        return true;
    }
}
