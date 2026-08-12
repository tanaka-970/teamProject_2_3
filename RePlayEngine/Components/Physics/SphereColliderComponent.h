#pragma once

#include "ColliderComponent.h"

namespace ReplayEngine::Components
{
    // 球状の衝突形状。
    //
    // 旧 ReplayEngine::Core::SphereColliderComponent（Player / Stage へ値メンバとして
    // 埋め込まれていたもの）を、新しい Component 基盤へ移したもの。
    // 名前空間が Core -> Components なので完全修飾名が重複せず、
    // 移行期間中は旧クラスと併存できる。
    //
    // Transform について:
    //   自前の座標を一切持たない。中心は必ず
    //     Owner の Transform のワールド位置 + center_offset（基底が持つ）
    //   で求める。Collider が Transform を二重所有しないための決まり。
    //
    // 責任:
    //   形状パラメータを持つことだけ。
    //   実際の判定は SceneCollisionWorld が、移動の解決は CharacterMotorComponent が行う。
    class SphereColliderComponent final : public ColliderComponent
    {
        REPLAY_COMPONENT_BODY(SphereColliderComponent)

    public:
        ColliderShape Shape() const noexcept override { return ColliderShape::Sphere; }

        bool ComputeWorldBounds(DirectX::XMFLOAT3& minimum,
            DirectX::XMFLOAT3& maximum) const override;

        // Character Motor の移動用 Collider として選べる。
        bool UsableAsCharacterShape() const noexcept override { return true; }

        std::string StatusMessage() const override;

        // Owner の拡縮を反映した実効半径。
        // 非一様な拡縮では最大軸を採る。すり抜けるより、少し早めに当たる方が安全なため。
        float EffectiveRadius() const noexcept;

        // 中心オフセットは 0 が既定で、判定球を GameObject の原点に一致させる。
        // 以前は旧 Player に合わせて 0.38 が入っていたが、題材固有の値を
        // エンジンの既定値に持たせない方針のため外した。足元を原点にしたい
        // ゲーム側は Inspector の「中心オフセット」へ明示的に設定する。
        // 半径は旧実装と同じ既定値。形状の既定値なのでそのまま引き継ぐ。
        float radius = 0.38f;

        // 壁へ押し戻す際に残す余白。0 だと面に貼り付いて再衝突し続ける。
        float skin_width = 0.015f;

        // これ以上の上向き成分を持つ面を「歩ける床」とみなす。
        float walkable_normal_y = 0.25f;
    };
}
