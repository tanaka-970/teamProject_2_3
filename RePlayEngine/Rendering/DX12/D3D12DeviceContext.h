#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include "D3D12DescriptorHeapAllocator.h"
#include "D3D12FrameConstants.h"
#include "D3D12MeshBuffer.h"
#include "D3D12RenderItemBatch.h"
#include "D3D12ShaderCompiler.h"
#include "D3D12UploadContext.h"

#include <cstdint>

namespace ReplayEngine::Rendering::DX12
{
    class D3D12DeviceContext final
    {
    public:
        static constexpr std::uint32_t FrameCount = 2;

        D3D12DeviceContext() = default;
        ~D3D12DeviceContext();

        D3D12DeviceContext(const D3D12DeviceContext&) = delete;
        D3D12DeviceContext& operator=(const D3D12DeviceContext&) = delete;

        bool Initialize(HWND window, std::uint32_t width, std::uint32_t height,
            bool enable_debug_layer = false, bool force_warp = false) noexcept;
        void Shutdown() noexcept;
        bool Resize(std::uint32_t width, std::uint32_t height) noexcept;

        bool BeginFrame(const float clear_color[4]) noexcept;
        bool SubmitFrameConstants(const D3D12FrameConstants& constants) noexcept;
        bool SubmitRenderItems(
            const ::ReplayEngine::Rendering::RenderItemList& items) noexcept;
        bool DrawValidationTriangle() noexcept;
        bool EndFrame() noexcept;
        void WaitForGpu() noexcept;

        bool IsInitialized() const noexcept { return device_ != nullptr; }
        bool IsFrameOpen() const noexcept { return frame_open_; }
        std::uint32_t Width() const noexcept { return width_; }
        std::uint32_t Height() const noexcept { return height_; }
        std::uint32_t FrameIndex() const noexcept { return frame_index_; }

        ID3D12Device* Device() const noexcept { return device_.Get(); }
        ID3D12CommandQueue* CommandQueue() const noexcept { return command_queue_.Get(); }
        ID3D12GraphicsCommandList* CommandList() const noexcept { return command_list_.Get(); }
        D3D12UploadContext& UploadContext() noexcept { return upload_context_; }
        D3D12DescriptorHeapAllocator& ResourceDescriptorAllocator() noexcept
        {
            return resource_descriptor_allocator_;
        }
        const D3D12RenderItemBatch& RenderItemBatch() const noexcept
        {
            return render_item_batches_[frame_index_];
        }
        ID3D12Resource* CurrentRenderTarget() const noexcept
        {
            return render_targets_[frame_index_].Get();
        }
        D3D12_CPU_DESCRIPTOR_HANDLE CurrentRenderTargetView() const noexcept;
        D3D12_CPU_DESCRIPTOR_HANDLE CurrentDepthStencilView() const noexcept;

    private:
        bool CreateDevice(bool enable_debug_layer, bool force_warp) noexcept;
        bool CreateSwapChain(HWND window, std::uint32_t width,
            std::uint32_t height) noexcept;
        bool CreateRenderTargets() noexcept;
        bool CreateValidationTriangleResources() noexcept;
        void ReleaseRenderTargets() noexcept;
        void ReleaseValidationTriangleResources() noexcept;
        bool WaitForFrame(std::uint32_t frame_index) noexcept;
        bool TransitionCurrentRenderTarget(D3D12_RESOURCE_STATES before,
            D3D12_RESOURCE_STATES after) noexcept;

        Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
        Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> validation_root_signature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> validation_pipeline_;
        D3D12MeshBuffer validation_mesh_;
        Microsoft::WRL::ComPtr<ID3D12Resource> frame_constant_buffers_[FrameCount];
        Microsoft::WRL::ComPtr<ID3D12Resource> depth_stencil_buffer_;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocators_[FrameCount];
        Microsoft::WRL::ComPtr<ID3D12Resource> render_targets_[FrameCount];

        D3D12DescriptorHeapAllocator rtv_allocator_;
        D3D12DescriptorAllocation rtv_allocation_{};
        D3D12DescriptorHeapAllocator dsv_allocator_;
        D3D12DescriptorAllocation dsv_allocation_{};
        D3D12DescriptorHeapAllocator resource_descriptor_allocator_;
        D3D12UploadContext upload_context_;
        D3D12RenderItemBatch render_item_batches_[FrameCount];
        HANDLE fence_event_ = nullptr;
        std::uint64_t fence_values_[FrameCount]{};
        std::uint32_t frame_index_ = 0;
        std::uint32_t width_ = 0;
        std::uint32_t height_ = 0;
        bool frame_open_ = false;
    };
}
