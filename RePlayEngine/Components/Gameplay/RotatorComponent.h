#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    // 所有 GameObject を一定速度で回し続ける動作確認用の Component。
    //
    // GameObject + Component 基盤が実際に機能していることを、
    // 既存の Player や Stage に一切触れずに確認するために用意した。
    //
    // 回転はローカルのオイラー角へ直接加算する。
    // RePlayEngine の Transform がオイラー角を正としているため、
    // 任意軸まわりの厳密な合成ではなく「各軸の角度を進める」動作になる。
    // Y 軸まわりの一定回転という一般的な用途では見た目どおりに動く。
    // 厳密な任意軸回転が必要になった時点でクォータニオン化を検討すること。
    class RotatorComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(RotatorComponent)

    public:
        RotatorComponent() = default;

        void OnUpdate(float delta_time) override;

        // 回す軸。正規化していなくてもよい（内部で正規化する）。
        DirectX::XMFLOAT3 axis{ 0.0f, 1.0f, 0.0f };

        // 1 秒あたりの回転量（度）。負値で逆回転。
        float degrees_per_second = 90.0f;

        // 累積角度をリセットしたい場合に Editor から押せるよう、
        // 実行時の経過を保持する。保存対象ではない。
        float elapsed_seconds = 0.0f;
    };
}
