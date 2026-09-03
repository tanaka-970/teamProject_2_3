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

        DirectX::XMVECTOR clip_corners[8]{};
        for (int index = 0; index < 8; ++index)
        {
            const DirectX::XMVECTOR local = DirectX::XMVectorSet(
                (index & 1) == 0 ? bounds.minimum.x : bounds.maximum.x,
                (index & 2) == 0 ? bounds.minimum.y : bounds.maximum.y,
                (index & 4) == 0 ? bounds.minimum.z : bounds.maximum.z, 1.0f);
            clip_corners[index] = DirectX::XMVector4Transform(
                DirectX::XMVector4Transform(local, world_matrix), view_projection_matrix);
        }

        const auto accumulate_clip = [&](DirectX::FXMVECTOR clip) noexcept
        {
            const float w = DirectX::XMVectorGetW(clip);
            if (!(w > 0.0f) || !std::isfinite(w)) return;
            const float normalized_x = DirectX::XMVectorGetX(clip) / w;
            const float normalized_y = DirectX::XMVectorGetY(clip) / w;
            if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y)) return;
            const float screen_x = viewport.TopLeftX +
                (normalized_x * 0.5f + 0.5f) * viewport.Width;
            const float screen_y = viewport.TopLeftY +
                (0.5f - normalized_y * 0.5f) * viewport.Height;
            if (!std::isfinite(screen_x) || !std::isfinite(screen_y)) return;
            if (!has_projected_corner)
            {
                minimum_x = maximum_x = screen_x;
                minimum_y = maximum_y = screen_y;
                has_projected_corner = true;
                return;
            }
            minimum_x = (std::min)(minimum_x, screen_x);
            minimum_y = (std::min)(minimum_y, screen_y);
            maximum_x = (std::max)(maximum_x, screen_x);
            maximum_y = (std::max)(maximum_y, screen_y);
        };

        for (int index = 0; index < 8; ++index) accumulate_clip(clip_corners[index]);

        // 角を捨てるだけだと、箱がニアプレーンをまたいだとき矩形が画面全体へ広がる。
        // カメラに寄るほど範囲が効かなくなるので、またいだ辺は交点を作って拾う。
        constexpr int edges[12][2] = {
            {0,1},{2,3},{4,5},{6,7},
            {0,2},{1,3},{4,6},{5,7},
            {0,4},{1,5},{2,6},{3,7} };
        constexpr float near_epsilon = 1.0e-4f;
        for (const auto& edge : edges)
        {
            const float w0 = DirectX::XMVectorGetW(clip_corners[edge[0]]);
            const float w1 = DirectX::XMVectorGetW(clip_corners[edge[1]]);
            if (!std::isfinite(w0) || !std::isfinite(w1)) continue;
            const bool front0 = w0 > near_epsilon;
            const bool front1 = w1 > near_epsilon;
            if (front0 == front1) continue;
            const float denominator = w0 - w1;
            if (!std::isfinite(denominator) || std::abs(denominator) < 1.0e-12f) continue;
            const float t = (w0 - near_epsilon) / denominator;
            if (!std::isfinite(t) || t < 0.0f || t > 1.0f) continue;
            accumulate_clip(DirectX::XMVectorLerp(
                clip_corners[edge[0]], clip_corners[edge[1]], t));
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
