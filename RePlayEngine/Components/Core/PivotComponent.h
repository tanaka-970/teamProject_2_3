#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    // 回転・拡縮の基準点だけを保存する非破壊 Pivot Override。
    // Transform の原点そのものは変更しないので、Pivot を編集しただけでは
    // GameObject の見た目・親子 Transform・保存済み配置は一切変わらない。
    class PivotComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(PivotComponent)

    public:
        enum class Mode : int
        {
            SelfOrigin = 0,
            BoundsCenter,
            BoundsFace,
            CustomLocal,
            WorldPoint,
            TargetObject
        };

        // 保存する実体は int。
        //
        // PropertyRegistry は enum class をそのまま扱えず、
        // 「int のメンバ + .AsEnum({ラベル})」が既存の流儀
        // （MeshRendererComponent::shading_model と同じ）。
        // 読み書きは下の ModeValue() / SetMode() を使い、
        // 比較や代入では Mode を使って可読性を保つ。
        int mode = static_cast<int>(Mode::SelfOrigin);

        Mode ModeValue() const noexcept { return static_cast<Mode>(mode); }
        void SetMode(Mode value) noexcept { mode = static_cast<int>(value); }

        // CustomLocal ではローカル座標、WorldPoint ではワールド座標として使う。
        // BoundsFace では中心からどの面を選ぶかの方向も兼ねる。
        DirectX::XMFLOAT3 local_point{ 0.0f, 0.0f, 0.0f };

        Reflection::ObjectReference target;
    };
}
