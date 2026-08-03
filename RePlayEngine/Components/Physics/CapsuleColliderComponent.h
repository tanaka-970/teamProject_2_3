#pragma once

#include "ColliderComponent.h"

namespace ReplayEngine::Components
{
    // カプセル（両端が半球の円柱）の衝突形状。
    //
    // height は「両端の半球を含めた全長」。
    // 半径 0.5 / 高さ 2.0 なら、円柱部分の長さは 1.0 になる。
    //
    // height が直径未満の場合:
    //   形状として成立しないため、判定は直径へ切り上げた値で行い、
    //   Inspector へ警告を出す。黙って別の形で当たるより、
    //   「今この値がどう解釈されているか」が見える方が事故が少ない。
    class CapsuleColliderComponent final : public ColliderComponent
    {
        REPLAY_COMPONENT_BODY(CapsuleColliderComponent)

    public:
        // 軸の向き。GameObject の回転がさらに掛かる。
        enum Axis
        {
            Axis_X = 0,
            Axis_Y = 1,
            Axis_Z = 2,
        };

        CapsuleColliderComponent() = default;

        ColliderShape Shape() const noexcept override { return ColliderShape::Capsule; }

        bool ComputeWorldBounds(DirectX::XMFLOAT3& minimum,
            DirectX::XMFLOAT3& maximum) const override;

        // Character Motor の移動用 Collider として選べる。
        bool UsableAsCharacterShape() const noexcept override { return true; }

        std::string StatusMessage() const override;

        // 拡縮を反映した半径。
        float EffectiveRadius() const noexcept;

        // 実際に判定へ使う全長。height が直径未満なら直径へ切り上げる。
        float EffectiveHeight() const noexcept;

        // 円柱部分の両端（＝両半球の中心）をワールド空間で返す。
        void WorldSegment(DirectX::XMFLOAT3& start, DirectX::XMFLOAT3& end) const noexcept;

        bool HeightTooSmall() const noexcept;

        float radius = 0.4f;
        float height = 1.8f;

        // Axis_X / Axis_Y / Axis_Z
        int axis = Axis_Y;
    };
}
