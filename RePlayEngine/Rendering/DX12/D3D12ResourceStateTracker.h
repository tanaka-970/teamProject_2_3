#pragma once

#include <d3d12.h>

#include <unordered_map>
#include <functional>
#include <string>

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
        void Reset() noexcept { states_.clear(); barrier_count_ = 0; validation_error_count_ = 0; }

        bool IsTracked(ID3D12Resource* resource) const noexcept;
        D3D12_RESOURCE_STATES StateOf(ID3D12Resource* resource,
            D3D12_RESOURCE_STATES fallback = D3D12_RESOURCE_STATE_COMMON) const noexcept;

        bool Transition(ID3D12GraphicsCommandList* command_list,
            ID3D12Resource* resource, D3D12_RESOURCE_STATES after) noexcept;
        bool UavBarrier(ID3D12GraphicsCommandList* command_list,
            ID3D12Resource* resource) noexcept;
        bool RequireState(ID3D12Resource* resource, D3D12_RESOURCE_STATES expected,
            const char* usage) noexcept;
        void SetBarrierCallback(std::function<void(ID3D12Resource*,
            D3D12_RESOURCE_STATES, D3D12_RESOURCE_STATES)> callback)
        { barrier_callback_ = std::move(callback); }
        void SetValidationCallback(std::function<void(const std::string&)> callback)
        { validation_callback_ = std::move(callback); }
        void SetBreakOnError(bool enabled) noexcept { break_on_error_ = enabled; }
        std::uint64_t BarrierCount() const noexcept { return barrier_count_; }
        std::uint64_t ValidationErrorCount() const noexcept { return validation_error_count_; }

    private:
        void ReportValidation(const std::string& message) noexcept;
        std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> states_;
        std::function<void(ID3D12Resource*, D3D12_RESOURCE_STATES, D3D12_RESOURCE_STATES)> barrier_callback_;
        std::function<void(const std::string&)> validation_callback_;
        std::uint64_t barrier_count_ = 0;
        std::uint64_t validation_error_count_ = 0;
        bool break_on_error_ = false;
    };
}
