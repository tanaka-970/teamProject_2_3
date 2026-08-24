#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>

namespace ReplayEngine::Rendering::DX12
{
    class D3D12UploadContext final
    {
    public:
        D3D12UploadContext() = default;
        ~D3D12UploadContext();

        D3D12UploadContext(const D3D12UploadContext&) = delete;
        D3D12UploadContext& operator=(const D3D12UploadContext&) = delete;

        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue) noexcept;
        void Shutdown() noexcept;
        bool UploadBuffer(ID3D12Resource* destination, const void* data,
            std::uint64_t size, D3D12_RESOURCE_STATES final_state) noexcept;

        bool IsInitialized() const noexcept
        {
            return device_ != nullptr && queue_ != nullptr && command_list_ != nullptr;
        }

    private:
        bool WaitForGpu() noexcept;

        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator_;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
        HANDLE fence_event_ = nullptr;
        std::uint64_t fence_value_ = 0;
    };
}
