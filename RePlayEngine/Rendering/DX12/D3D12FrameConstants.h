#pragma once

#include <DirectXMath.h>

namespace ReplayEngine::Rendering::DX12
{
    struct D3D12FrameConstants final
    {
        DirectX::XMFLOAT4X4 view_projection{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4 camera_position{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4 time_parameters{ 0.0f, 0.0f, 0.0f, 0.0f };

        // Phase 2 の Production Static Rendering では、上の 3 フィールドを Validation
        // Shader 用に固定し、追加フィールドで frame_common.hlsli の定義を対応させる。
        DirectX::XMFLOAT4X4 view{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT4X4 projection = view;
        DirectX::XMFLOAT4X4 inv_view = view;
        DirectX::XMFLOAT4X4 inv_projection = view;
        DirectX::XMFLOAT4X4 inv_view_projection = view;
        DirectX::XMFLOAT4X4 prev_view_projection = view;
        DirectX::XMFLOAT4 screen_size{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 camera_planes{ 0.1f, 10000.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 jitter{};
    };
}
