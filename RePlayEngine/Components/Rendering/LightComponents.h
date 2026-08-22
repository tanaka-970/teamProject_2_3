#pragma once

#include "../../Object/Component/Component.h"

#include <DirectXMath.h>

namespace ReplayEngine::Components
{
    // Light data lives on ordinary GameObjects. Transform supplies position and
    // orientation; these serializable properties supply photometric settings.
    //
    // 【影の設定はライト側に置く】
    //   Unity の Light Inspector / Unreal の Light Details と同じ考え方で、
    //   「この光が影を落とすか」「どのくらい濃いか」は光源の設定にする。
    //   Mesh 側にあるのは Cast Shadow / Receive Shadow の 2 つだけで、
    //   これは「この形状を影計算に含めるか」という別の話。
    //   全体の描画設定にあるのは品質と枚数の上限だけで、
    //   個々のライトの意思をそこから上書きしない。
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
        // この距離より遠くには影を出さない。カスケードの総距離。
        float shadow_distance = 240.0f;
    };

    class PointLightComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(PointLightComponent)
    public:
        DirectX::XMFLOAT4 color{ 1.0f, 0.9f, 0.75f, 1.0f };
        float intensity = 2.0f;
        float range = 10.0f;

        // ---- 影 ---------------------------------------------------------
        // 全方向を覆うため 1 灯で 6 面ぶんの影マップを使う。
        // 影付きにできる Point の数には上限があり、超えた分は影なしになる。
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

        // ---- 影 ---------------------------------------------------------
        // 円錐 1 つぶんなので影マップは 1 枚で足りる。Point より軽い。
        bool cast_shadows = false;
        float shadow_strength = 1.0f;
        float shadow_depth_bias = 0.0018f;
        float shadow_near_plane = 0.15f;
    };
}
