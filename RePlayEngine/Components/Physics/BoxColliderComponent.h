#pragma once

#include "ColliderComponent.h"

namespace ReplayEngine::Components
{
    // 直方体の衝突形状。
    //
    // Transform の反映:
    //   Owner の Position / Rotation / Scale をすべて反映する。
    //   回転した箱を軸並行として扱うと、見た目と当たりがずれるため、
    //   判定は「クエリを箱のローカル空間へ移して軸並行として解く」方式にしてある。
    //   Mesh Collider と同じ考え方なので、実装も同じ経路を通る。
    //
    // size は「辺の長さ」であって半分の長さではない。
    // Inspector で 1,1,1 と入れたら 1 メートル角になる方が直感的なため。
    class BoxColliderComponent final : public ColliderComponent
    {
        REPLAY_COMPONENT_BODY(BoxColliderComponent)

    public:
        BoxColliderComponent() = default;

        ColliderShape Shape() const noexcept override { return ColliderShape::Box; }

        bool ComputeWorldBounds(DirectX::XMFLOAT3& minimum,
            DirectX::XMFLOAT3& maximum) const override;

        // Character Motor の移動用 Collider として選べる。
        bool UsableAsCharacterShape() const noexcept override { return true; }

        std::string StatusMessage() const override;

        // 拡縮を反映した半辺長（ローカル軸ごと）。
        DirectX::XMFLOAT3 WorldHalfExtents() const noexcept;

        // 辺の長さ。
        DirectX::XMFLOAT3 size{ 1.0f, 1.0f, 1.0f };
    };
}
