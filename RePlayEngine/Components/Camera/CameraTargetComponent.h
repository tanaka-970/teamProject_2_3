#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    // この GameObject がカメラの追従対象になれることを表す。
    //
    // 重要: このクラス自身はカメラを一切動かさない。
    //   カメラを動かすのはカメラ制御側（現状は SceneGame）。
    //   制御側は「CameraTargetComponent を持つ GameObject」を ObjectID で探し、
    //   その Transform と、ここに書かれたオフセット設定を読むだけ。
    //
    //   これにより、カメラ側が Player 具象型を知る必要がなくなる。
    //   追従対象を人型からメカやドローンへ変えても、
    //   この Component を付け替えるだけで済む。
    class CameraTargetComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(CameraTargetComponent)

    public:
        CameraTargetComponent() = default;

        // 追従の基準点を GameObject のワールド位置からずらす量。
        //
        // look_at_offset との違い:
        //   target_offset  … 「追従している点」そのものをずらす。
        //                     モデルの原点が足元にある場合に腰の高さへ寄せる、
        //                     機体の中心が前方にある場合に後ろへ寄せる、といった用途。
        //   look_at_offset … 追従点はそのままで「カメラが見る点」だけをずらす。
        DirectX::XMFLOAT3 target_offset{ 0.0f, 0.0f, 0.0f };

        // 注視点のオフセット。旧来のカメラ追従は position.y + 1.0f を見ていた。
        DirectX::XMFLOAT3 look_at_offset{ 0.0f, 1.0f, 0.0f };

        // カメラを置く距離と高さ。旧 SceneGame の follow_distance / follow_height。
        float follow_distance = 6.5f;
        float follow_height = 2.25f;

        // 追従の追いつき速さ。大きいほど機敏。
        float follow_lag = 12.0f;

        // 複数の対象がある場合に、値が大きいものを優先する。
        int priority = 0;
    };
}
