#pragma once

#include <DirectXMath.h>

namespace ReplayEngine::Rendering
{
    // SSAO/SSR/TAAが共有するフレーム単位のカメラ情報。
    // Shader\frame_common.hlsli の FRAME_CONSTANT_BUFFER(b4) と必ず同じ並びにする。
    // projection にはTAA用のジッターを適用済みの行列を入れ、深度復元も同じ行列の
    // 逆行列で行う(ジッター有無で座標がずれないようにするため)。
    struct FrameConstants
    {
        DirectX::XMFLOAT4X4 view{};
        DirectX::XMFLOAT4X4 projection{};
        DirectX::XMFLOAT4X4 view_projection{};
        DirectX::XMFLOAT4X4 inv_view{};
        DirectX::XMFLOAT4X4 inv_projection{};
        DirectX::XMFLOAT4X4 inv_view_projection{};
        DirectX::XMFLOAT4X4 prev_view_projection{};
        DirectX::XMFLOAT4   camera_position{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4   screen_size{ 1.0f, 1.0f, 1.0f, 1.0f };   // x=w, y=h, z=1/w, w=1/h
        DirectX::XMFLOAT4   camera_planes{ 0.1f, 10000.0f, 1.0f, 1.0f }; // x=near, y=far, z=tan(fovY/2), w=aspect
        DirectX::XMFLOAT4   jitter{};             // xy=今フレームのNDCジッター, zw=前フレーム
        DirectX::XMFLOAT4   frame_params{};       // x=frame_index, y=elapsed_time, z/w=予約
    };
    static_assert(sizeof(FrameConstants) % 16 == 0, "定数バッファは16バイト境界に揃える");
}
