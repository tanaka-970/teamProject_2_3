#include "D3D12ResourceStateTracker.h"

namespace ReplayEngine::Rendering::DX12
{
    bool D3D12ResourceStateTracker::Track(ID3D12Resource* resource,
        D3D12_RESOURCE_STATES state) noexcept
    {
        if (resource == nullptr) return false;
        try
        {
            states_[resource] = state;
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    void D3D12ResourceStateTracker::Forget(ID3D12Resource* resource) noexcept
    {
        if (resource != nullptr) states_.erase(resource);
    }

    bool D3D12ResourceStateTracker::IsTracked(ID3D12Resource* resource) const noexcept
    {
        return resource != nullptr && states_.find(resource) != states_.end();
    }

    D3D12_RESOURCE_STATES D3D12ResourceStateTracker::StateOf(
        ID3D12Resource* resource, D3D12_RESOURCE_STATES fallback) const noexcept
    {
        if (resource == nullptr) return fallback;
        const auto found = states_.find(resource);
        return found != states_.end() ? found->second : fallback;
    }

    bool D3D12ResourceStateTracker::Transition(
        ID3D12GraphicsCommandList* command_list, ID3D12Resource* resource,
        D3D12_RESOURCE_STATES after) noexcept
    {
        if (command_list == nullptr || resource == nullptr) return false;
        auto found = states_.find(resource);
        if (found == states_.end()) return false;
        const D3D12_RESOURCE_STATES before = found->second;
        if (before == after) return true;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &barrier);
        found->second = after;
        return true;
    }

    bool D3D12ResourceStateTracker::UavBarrier(
        ID3D12GraphicsCommandList* command_list, ID3D12Resource* resource) noexcept
    {
        if (command_list == nullptr || resource == nullptr) return false;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource;
        command_list->ResourceBarrier(1, &barrier);
        return true;
    }
}
