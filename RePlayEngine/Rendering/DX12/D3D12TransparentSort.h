#pragma once

#include "D3D12ScreenBounds.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering::DX12
{
    struct D3D12TransparentSortInput final
    {
        std::uint64_t owner_id = 0;
        D3D12MeshLocalBounds bounds;
        DirectX::XMFLOAT4X4 world{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        bool skinned = false;
        std::uint32_t index = 0;
    };

    struct D3D12TransparentSortEntry final
    {
        bool skinned = false;
        std::uint32_t index = 0;
        std::uint64_t owner_id = 0;
        float depth = 0.0f;
    };

    inline DirectX::XMFLOAT3 CalculateD3D12TransparentCenter(
        const D3D12MeshLocalBounds& bounds,
        const DirectX::XMFLOAT4X4& world) noexcept
    {
        if (!bounds.valid)
            return DirectX::XMFLOAT3{ world._41, world._42, world._43 };

        const DirectX::XMVECTOR local_center = DirectX::XMVectorSet(
            bounds.minimum.x * 0.5f + bounds.maximum.x * 0.5f,
            bounds.minimum.y * 0.5f + bounds.maximum.y * 0.5f,
            bounds.minimum.z * 0.5f + bounds.maximum.z * 0.5f, 1.0f);
        DirectX::XMFLOAT3 center{};
        DirectX::XMStoreFloat3(&center, DirectX::XMVector3Transform(
            local_center, DirectX::XMLoadFloat4x4(&world)));
        return center;
    }

    inline float CalculateD3D12TransparentDepth(
        const D3D12TransparentSortInput& input,
        const DirectX::XMFLOAT3& camera_position) noexcept
    {
        const DirectX::XMFLOAT3 center = CalculateD3D12TransparentCenter(
            input.bounds, input.world);
        const float x = center.x - camera_position.x;
        const float y = center.y - camera_position.y;
        const float z = center.z - camera_position.z;
        return std::sqrt(x * x + y * y + z * z);
    }

    inline void BuildD3D12TransparentDrawOrder(
        const std::vector<D3D12TransparentSortInput>& inputs,
        const DirectX::XMFLOAT3& camera_position,
        std::vector<D3D12TransparentSortEntry>& order)
    {
        order.clear();
        if (inputs.empty()) return;

        struct SortItem final
        {
            D3D12TransparentSortEntry entry;
            std::vector<D3D12TransparentSortInput>::size_type first_input = 0;
            float group_depth = 0.0f;
            bool group_finite = true;
        };

        // グループ計算の作業領域もスレッドごとに容量を再利用する。
        static thread_local std::vector<SortItem> items;
        items.clear();
        items.reserve(inputs.size());
        for (std::vector<D3D12TransparentSortInput>::size_type i = 0; i < inputs.size(); ++i)
        {
            const D3D12TransparentSortInput& input = inputs[i];
            items.push_back(SortItem{
                D3D12TransparentSortEntry{ input.skinned, input.index, input.owner_id,
                    CalculateD3D12TransparentDepth(input, camera_position) }, i });
        }
        std::stable_sort(items.begin(), items.end(),
            [](const SortItem& left, const SortItem& right) noexcept
            {
                return left.entry.owner_id < right.entry.owner_id;
            });

        for (std::vector<SortItem>::size_type begin = 0; begin < items.size();)
        {
            auto end = begin;
            float depth = 0.0f;
            bool finite = true;
            while (end < items.size() && items[end].entry.owner_id == items[begin].entry.owner_id)
            {
                const float item_depth = items[end].entry.depth;
                if (!std::isfinite(item_depth))
                    finite = false;
                else if (item_depth > depth)
                    depth = item_depth;
                ++end;
            }
            const auto first_input = items[begin].first_input;
            for (auto i = begin; i < end; ++i)
            {
                items[i].first_input = first_input;
                items[i].group_depth = depth;
                items[i].group_finite = finite;
            }
            begin = end;
        }

        std::stable_sort(items.begin(), items.end(),
            [](const SortItem& left, const SortItem& right) noexcept
            {
                // 非有限値を含む owner は連続性を保ったままグループごと末尾へ送る。
                if (left.group_finite != right.group_finite) return left.group_finite;
                if (left.group_finite && left.group_depth != right.group_depth)
                    return left.group_depth > right.group_depth;
                if (left.first_input != right.first_input)
                    return left.first_input < right.first_input;
                const bool left_finite = std::isfinite(left.entry.depth);
                const bool right_finite = std::isfinite(right.entry.depth);
                if (left_finite != right_finite) return left_finite;
                return left_finite && left.entry.depth > right.entry.depth;
            });

        order.reserve(items.size());
        for (const SortItem& item : items) order.push_back(item.entry);
    }
}