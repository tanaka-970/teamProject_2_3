#pragma once

#include "../Common/PriorityComponentSelection.h"
#include "../../Core/ObjectID/ObjectID.h"
#include "../../Object/Component/Component.h"
#include "../../Scene/Runtime/Scene.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    // この GameObject がカメラの追従対象になれることを表す。
    //
    // 重要: このクラス自身はカメラを一切動かさない。
    //   カメラを動かすのはカメラ制御側（現状は SceneGame）。
    //   制御側は「CameraTargetComponent を持つ GameObject」を ObjectID で探し、
    //   その Transform と、ここに書かれたオフセット設定を読むだけ
    //   これにより、カメラ側が Player 具象型を知る必要がなくなる。
    //   追従対象を人型からメカやドローンへ変えても、
    //   この Component を付け替えるだけで済む。
    class CameraTargetComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(CameraTargetComponent)

    public:
        CameraTargetComponent() = default;

      
        DirectX::XMFLOAT3 target_offset{ 0.0f, 0.0f, 0.0f };

        // 注視点のオフセット。旧来のカメラ追従は position.y + 1.0f を見ていた。
        DirectX::XMFLOAT3 look_at_offset{ 0.0f, 1.0f, 0.0f };

        // 複数の対象がある場合に、値が大きいものを優先する。
        int priority = 0;
    };

    using CameraTargetSelection = PriorityComponentSelection<CameraTargetComponent>;

    inline CameraTargetSelection ResolveCameraTargetSelection(
        const Scene::Scene& scene, Core::ObjectID controlled)
    {
        return ResolvePriorityComponentSelection<CameraTargetComponent>(scene, controlled);
    }
}
