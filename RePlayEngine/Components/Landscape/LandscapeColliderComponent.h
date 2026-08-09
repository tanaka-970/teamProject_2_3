#pragma once

#include "../Physics/ColliderComponent.h"
#include "../../Physics/SphereCast.h"
#include "../../Physics/CookedMeshCollision.h"

#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace ReplayEngine::Components
{
    // LandscapeComponent の任意 topology をそのまま衝突形状として使う。
    // AssetGUID/Cook を要求しないので、Editor の Sculpt / Tunnel 後に即座に更新できる。
    class LandscapeColliderComponent final : public ColliderComponent
    {
        REPLAY_COMPONENT_BODY(LandscapeColliderComponent)
    public:
        ColliderShape Shape() const noexcept override { return ColliderShape::Landscape; }
        bool ComputeWorldBounds(DirectX::XMFLOAT3& minimum,
            DirectX::XMFLOAT3& maximum) const override;
        bool UsableAsCharacterShape() const noexcept override { return false; }
        std::string StatusMessage() const override { return status_; }

        bool RefreshGeometryIfChanged();

        // Sculpt の連続ドラッグ中は collision cook を止め、マウスを離した後に
        // 最新 revision を 1 回だけ cook する。Editor の一時状態であり保存しない。
        void BeginInteractiveEdit() noexcept { interactive_edit_active_ = true; }
        void EndInteractiveEdit() noexcept { interactive_edit_active_ = false; }
        bool InteractiveEditActive() const noexcept { return interactive_edit_active_; }

        bool ReadyForQuery() const noexcept
        { return cooked_ != nullptr && cooked_->Valid() && ActiveInHierarchy(); }

        const std::vector<Physics::Triangle>& Triangles() const noexcept { return triangles_; }
        const std::shared_ptr<const Physics::CookedMeshCollisionData>& Cooked() const noexcept
        { return cooked_; }
        const DirectX::XMFLOAT4X4& WorldMatrix() const noexcept { return world_; }
        const DirectX::XMFLOAT4X4& InverseWorldMatrix() const noexcept { return inverse_world_; }
        bool NegativeScale() const noexcept { return negative_scale_; }
        float LocalRadiusScale() const noexcept { return local_radius_scale_; }

        bool double_sided = true;
        // Landscape は編集で頻繁に再生成されるため Asset Cook cache には載せず、
        // Component 自身が revision 単位の spatial cook を持つ。
        // 値は local-space XZ grid の cell size。
        float collision_cell_size = 4.0f;
        bool debug_draw_wireframe = false;

    private:
        std::vector<Physics::Triangle> triangles_;
        std::shared_ptr<const Physics::CookedMeshCollisionData> cooked_;
        std::uint64_t geometry_revision_ = 0;
        bool cooked_double_sided_ = true;
        float cooked_cell_size_ = 0.0f;
        DirectX::XMFLOAT4X4 world_{};
        DirectX::XMFLOAT4X4 inverse_world_{};
        DirectX::XMFLOAT4X4 cached_world_{};
        DirectX::XMFLOAT3 world_bounds_min_{};
        DirectX::XMFLOAT3 world_bounds_max_{};
        bool transform_valid_ = false;
        bool negative_scale_ = false;
        float local_radius_scale_ = 1.0f;
        bool interactive_edit_active_ = false;
        std::string status_;
    };
}
