#pragma once

#include "IComponent.h"
#include <DirectXMath.h>

namespace ReplayEngine::Core
{
// 衝突形状だけを保持する設定コンポーネント。
// PlayerやStageから分離し、将来のRePlayエディタで任意オブジェクトへ追加・保存できるようにする。
    class SphereColliderComponent final : public IComponent
    {
    public:
        float radius = 0.38f;
        DirectX::XMFLOAT3 center_offset{ 0.0f, 0.38f, 0.0f };
        float skin_width = 0.015f;
        float walkable_normal_y = 0.25f;
    };
}
