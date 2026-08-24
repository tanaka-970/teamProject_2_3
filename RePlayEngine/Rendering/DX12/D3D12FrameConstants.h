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
    };
}
