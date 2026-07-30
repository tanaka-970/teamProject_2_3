#pragma once

#include <DirectXMath.h>

// TAA用モーションベクターのフレーム状態。
//
// メッシュ側は「前フレームの自分の姿勢」しか持っていないため、
// 前フレームのビュー射影とジッター量はフレーム共通の値としてここから受け取る。
// framework が render() の先頭で Frame() を更新し、
// skinned_mesh / static_mesh が G-Buffer 描画時に参照する。
namespace motion_vectors
{
    // Shader\motion_vector_common.hlsli の
    // PREVIOUS_OBJECT_CONSTANT_BUFFER(VS b6) と並びを一致させる。
    struct ObjectConstants
    {
        DirectX::XMFLOAT4X4 previous_world{};
        DirectX::XMFLOAT4X4 previous_view_projection{};
        DirectX::XMFLOAT4   params{};   // x=有効, y/z=現フレームのジッター(NDC), w=予約
        DirectX::XMFLOAT4   params2{};  // x/y=前フレームのジッター(NDC), z/w=予約
    };

    struct FrameContext
    {
        DirectX::XMFLOAT4X4 previous_view_projection{};
        DirectX::XMFLOAT2   current_jitter{ 0.0f, 0.0f };   // NDC
        DirectX::XMFLOAT2   previous_jitter{ 0.0f, 0.0f };  // NDC
        // 履歴が有効で、かつモーションベクターを必要とするフレームだけ true。
        bool enabled = false;
        // 同一フレーム内で履歴を二重更新しないための識別子。
        unsigned long long frame_id = 0;
    };

    inline FrameContext& Frame()
    {
        static FrameContext context{};
        return context;
    }
}
