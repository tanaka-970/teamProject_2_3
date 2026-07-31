#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

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
    //     Owner の Transform のワールド位置 + center_offset
    //   で求める。Collider が Transform を二重所有しないための決まり。
    //
    // 責任:
    //   形状パラメータを持つことだけ。
    //   実際の判定は IPhysicsQueryService が、移動の解決は CharacterMotorComponent が行う。
    class SphereColliderComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(SphereColliderComponent)

    public:
        SphereColliderComponent() = default;

        // Owner のワールド位置へ center_offset を足した、判定に使う球の中心。
        // Owner が居ない場合は原点を返す（Inspector 描画中に消えても落ちない）。
        DirectX::XMFLOAT3 WorldCenter() const noexcept;

        // 旧実装と同じ既定値。挙動を変えないためそのまま引き継ぐ。
        float radius = 0.38f;
        DirectX::XMFLOAT3 center_offset{ 0.0f, 0.38f, 0.0f };

        // 壁へ押し戻す際に残す余白。0 だと面に貼り付いて再衝突し続ける。
        float skin_width = 0.015f;

        // これ以上の上向き成分を持つ面を「歩ける床」とみなす。
        float walkable_normal_y = 0.25f;
    };
}
