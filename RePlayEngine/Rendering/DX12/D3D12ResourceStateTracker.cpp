#include "D3D12ResourceStateTracker.h"

#include <cstdio>
#include <windows.h>

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
        if (found == states_.end())
        {
            ReportValidation("DX12 state tracker: 未追跡ResourceへTransitionが要求されました");
            return false;
        }
        const D3D12_RESOURCE_STATES before = found->second;
        if (before == after) return true;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &barrier);
        ++barrier_count_;
        if (barrier_callback_) barrier_callback_(resource, before, after);
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
        ++barrier_count_;
        if (barrier_callback_) barrier_callback_(resource,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return true;
    }

    bool D3D12ResourceStateTracker::RequireState(ID3D12Resource* resource,
        D3D12_RESOURCE_STATES expected, const char* usage) noexcept
    {
        if (resource == nullptr) return false;
        const auto found = states_.find(resource);
        if (found != states_.end() && found->second == expected) return true;
        std::string message = "DX12 state tracker: ";
        message += usage != nullptr ? usage : "resource use";
        message += " の状態が記録と一致しません";
        ReportValidation(message);
        return false;
    }

    void D3D12ResourceStateTracker::ReportValidation(const std::string& message) noexcept
    {
        ++validation_error_count_;
        if (validation_callback_) validation_callback_(message);
        std::fprintf(stderr, "%s\n", message.c_str());
        OutputDebugStringA((message + "\n").c_str());
        if (break_on_error_ && ::IsDebuggerPresent()) __debugbreak();
    }
}
