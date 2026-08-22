#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    // Light data lives on ordinary GameObjects. Transform supplies position and
    // orientation; these serializable properties supply photometric settings.
    // 影を落とすかと濃さは光源側の設定にする。Mesh 側は Cast / Receive Shadow だけ。
    class DirectionalLightComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(DirectionalLightComponent)
    public:
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float intensity = 3.0f;

        // ---- 影 ---------------------------------------------------------
        bool cast_shadows = true;
        // 影の濃さ。1 で真っ黒、0 で影なしと同じ見た目になる。
        float shadow_strength = 1.0f;
        // シャドウアクネ対策。大きくしすぎると影が浮く (ピーターパニング)。
        float shadow_depth_bias = 0.0016f;
        // 法線方向へずらす量 (影マップのテクセル単位)。
        float shadow_normal_bias = 1.4f;
        // 影を出す最遠距離。伸ばすほど影マップのテクセルが粗くなる。
        float shadow_distance = 120.0f;
    };

    class PointLightComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(PointLightComponent)
    public:
        DirectX::XMFLOAT4 color{ 1.0f, 0.9f, 0.75f, 1.0f };
        float intensity = 2.0f;
        float range = 10.0f;

        // ---- 影 ---- 1 灯で 6 面使うため影付きにできる数に上限がある。
        bool cast_shadows = false;
        float shadow_strength = 1.0f;
        float shadow_depth_bias = 0.0025f;
        // 近すぎる面を切る距離。小さすぎると深度の精度が落ちてアクネが出る。
        float shadow_near_plane = 0.15f;
    };

    class SpotLightComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(SpotLightComponent)
    public:
        DirectX::XMFLOAT4 color{ 1.0f, 0.95f, 0.85f, 1.0f };
        float intensity = 2.0f;
        float range = 12.0f;
        float inner_angle_degrees = 25.0f;
        float outer_angle_degrees = 40.0f;

        // ---- 影 ---- 円錐 1 つぶんなので影マップは 1 枚で足りる。
        bool cast_shadows = false;
        float shadow_strength = 1.0f;
        float shadow_depth_bias = 0.0018f;
        float shadow_near_plane = 0.15f;
    };
}
