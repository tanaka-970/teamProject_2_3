#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering::DX12
{
    struct D3D12TextureSubresourceSource final
    {
        const void* data = nullptr;
        std::uint64_t row_pitch = 0;
        std::uint64_t slice_pitch = 0;
    };

    class D3D12UploadContext final
    {
    public:
        D3D12UploadContext() = default;
        ~D3D12UploadContext();

        D3D12UploadContext(const D3D12UploadContext&) = delete;
        D3D12UploadContext& operator=(const D3D12UploadContext&) = delete;

        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue) noexcept;
        void Shutdown() noexcept;

        // Static Asset の Upload は Batch 化し、複数の Buffer/Texture を持つ Mesh でも
        // Resource ごとに CPU/GPU 往復を発生させない。BeginBatch() を使わず
        // UploadBuffer() を呼ぶ既存経路は同期動作を維持する。
        bool BeginBatch() noexcept;
        bool EndBatch() noexcept;
        bool IsBatchOpen() const noexcept { return batch_open_; }

        bool UploadBuffer(ID3D12Resource* destination, const void* data,
            std::uint64_t size, D3D12_RESOURCE_STATES final_state) noexcept;
        bool UploadTexture2D(ID3D12Resource* destination, const void* rgba_data,
            std::uint32_t width, std::uint32_t height, std::uint32_t source_row_pitch,
            D3D12_RESOURCE_STATES final_state) noexcept;
        bool UploadTextureSubresources(ID3D12Resource* destination,
            const std::vector<D3D12TextureSubresourceSource>& sources,
            D3D12_RESOURCE_STATES final_state) noexcept;

        bool IsInitialized() const noexcept
        {
            return device_ != nullptr && queue_ != nullptr && command_list_ != nullptr;
        }
        std::uint64_t WaitCount() const noexcept { return wait_count_; }
        std::uint64_t WaitNanoseconds() const noexcept { return wait_nanoseconds_; }
        std::uint64_t UploadCount() const noexcept { return upload_count_; }

    private:
        bool BeginSingleUploadIfNeeded(bool& opened_here) noexcept;
        bool FinishSingleUploadIfNeeded(bool opened_here) noexcept;
        bool WaitForGpu() noexcept;

        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator_;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> pending_upload_resources_;
        HANDLE fence_event_ = nullptr;
        std::uint64_t fence_value_ = 0;
        std::uint64_t wait_count_ = 0;
        std::uint64_t wait_nanoseconds_ = 0;
        std::uint64_t upload_count_ = 0;
        bool batch_open_ = false;
        bool batch_has_commands_ = false;
    };
}
