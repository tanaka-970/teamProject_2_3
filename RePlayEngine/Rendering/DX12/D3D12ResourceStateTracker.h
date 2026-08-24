#pragma once

#include <d3d12.h>

#include <unordered_map>

namespace ReplayEngine::Rendering::DX12
{
    // Phase 1 ではリソース全体の状態だけを追跡する。Subresource 単位の分割状態は
    // 未実装として扱い、Texture の mip/slice の Upload/描画が必要になった段階で
    // このクラスを拡張する。
    class D3D12ResourceStateTracker final
    {
    public:
        bool Track(ID3D12Resource* resource, D3D12_RESOURCE_STATES state) noexcept;
        void Forget(ID3D12Resource* resource) noexcept;
        void Reset() noexcept { states_.clear(); }

        bool IsTracked(ID3D12Resource* resource) const noexcept;
        D3D12_RESOURCE_STATES StateOf(ID3D12Resource* resource,
            D3D12_RESOURCE_STATES fallback = D3D12_RESOURCE_STATE_COMMON) const noexcept;

        bool Transition(ID3D12GraphicsCommandList* command_list,
            ID3D12Resource* resource, D3D12_RESOURCE_STATES after) noexcept;
        bool UavBarrier(ID3D12GraphicsCommandList* command_list,
            ID3D12Resource* resource) noexcept;

    private:
        std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> states_;
    };
}
