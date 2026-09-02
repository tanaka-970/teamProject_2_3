#pragma once

#include <d3d12.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>

namespace ReplayEngine::Rendering::DX12
{
    struct D3D12MeshLocalBounds final
    {
        DirectX::XMFLOAT3 minimum{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 maximum{ 0.0f, 0.0f, 0.0f };
        bool valid = false;
    };

    inline bool CalculateD3D12ScreenRect(
        const D3D12MeshLocalBounds& bounds,
        const DirectX::XMFLOAT4X4& world,
        const DirectX::XMFLOAT4X4& view_projection,
        const D3D12_VIEWPORT& viewport,
        D3D12_RECT& result) noexcept
    {
        result = {};
        if (!bounds.valid || viewport.Width <= 0.0f || viewport.Height <= 0.0f)
            return false;

        const DirectX::XMMATRIX world_matrix = DirectX::XMLoadFloat4x4(&world);
        const DirectX::XMMATRIX view_projection_matrix =
            DirectX::XMLoadFloat4x4(&view_projection);
        bool has_projected_corner = false;
        float minimum_x = 0.0f;
        float minimum_y = 0.0f;
        float maximum_x = 0.0f;
        float maximum_y = 0.0f;
        for (int z = 0; z < 2; ++z)
        {
            for (int y = 0; y < 2; ++y)
            {
                for (int x = 0; x < 2; ++x)
                {
                    const DirectX::XMVECTOR local = DirectX::XMVectorSet(
                        x == 0 ? bounds.minimum.x : bounds.maximum.x,
                        y == 0 ? bounds.minimum.y : bounds.maximum.y,
                        z == 0 ? bounds.minimum.z : bounds.maximum.z, 1.0f);
                    const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(
                        DirectX::XMVector4Transform(local, world_matrix),
                        view_projection_matrix);
                    const float w = DirectX::XMVectorGetW(clip);
                    if (!(w > 0.0f) || !std::isfinite(w)) continue;
                    const float normalized_x = DirectX::XMVectorGetX(clip) / w;
                    const float normalized_y = DirectX::XMVectorGetY(clip) / w;
                    if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y)) continue;
                    const float screen_x = viewport.TopLeftX +
                        (normalized_x * 0.5f + 0.5f) * viewport.Width;
                    const float screen_y = viewport.TopLeftY +
                        (0.5f - normalized_y * 0.5f) * viewport.Height;
                    if (!std::isfinite(screen_x) || !std::isfinite(screen_y)) continue;
                    if (!has_projected_corner)
                    {
                        minimum_x = maximum_x = screen_x;
                        minimum_y = maximum_y = screen_y;
                        has_projected_corner = true;
                    }
                    else
                    {
                        minimum_x = (std::min)(minimum_x, screen_x);
                        minimum_y = (std::min)(minimum_y, screen_y);
                        maximum_x = (std::max)(maximum_x, screen_x);
                        maximum_y = (std::max)(maximum_y, screen_y);
                    }
                }
            }
        }
        if (!has_projected_corner) return false;

        const float viewport_right = viewport.TopLeftX + viewport.Width;
        const float viewport_bottom = viewport.TopLeftY + viewport.Height;
        if (maximum_x <= viewport.TopLeftX || minimum_x >= viewport_right ||
            maximum_y <= viewport.TopLeftY || minimum_y >= viewport_bottom)
            return false;

        const float clipped_left = (std::max)(viewport.TopLeftX,
            (std::min)(minimum_x, viewport_right));
        const float clipped_top = (std::max)(viewport.TopLeftY,
            (std::min)(minimum_y, viewport_bottom));
        const float clipped_right = (std::max)(viewport.TopLeftX,
            (std::min)(maximum_x, viewport_right));
        const float clipped_bottom = (std::max)(viewport.TopLeftY,
            (std::min)(maximum_y, viewport_bottom));
        result.left = static_cast<LONG>(std::floor(clipped_left));
        result.top = static_cast<LONG>(std::floor(clipped_top));
        result.right = static_cast<LONG>(std::ceil(clipped_right));
        result.bottom = static_cast<LONG>(std::ceil(clipped_bottom));
        return result.right > result.left && result.bottom > result.top;
    }
}
