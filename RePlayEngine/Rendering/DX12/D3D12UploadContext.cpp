#include "D3D12UploadContext.h"

#include <cstring>

namespace ReplayEngine::Rendering::DX12
{
    D3D12UploadContext::~D3D12UploadContext()
    {
        Shutdown();
    }

    bool D3D12UploadContext::Initialize(ID3D12Device* device,
        ID3D12CommandQueue* queue) noexcept
    {
        Shutdown();
        if (device == nullptr || queue == nullptr) return false;
        device_ = device;
        queue_ = queue;
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&command_allocator_))))
            return false;
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocator_.Get(), nullptr, IID_PPV_ARGS(&command_list_))))
            return false;
        if (FAILED(command_list_->Close())) return false;
        if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&fence_))))
            return false;
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return fence_event_ != nullptr;
    }

    void D3D12UploadContext::Shutdown() noexcept
    {
        if (IsInitialized()) WaitForGpu();
        if (fence_event_ != nullptr)
        {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }
        fence_.Reset();
        command_list_.Reset();
        command_allocator_.Reset();
        queue_.Reset();
        device_.Reset();
        fence_value_ = 0;
    }

    bool D3D12UploadContext::UploadBuffer(ID3D12Resource* destination,
        const void* data, std::uint64_t size,
        D3D12_RESOURCE_STATES final_state) noexcept
    {
        if (!IsInitialized() || destination == nullptr || data == nullptr || size == 0)
            return false;

        D3D12_RESOURCE_DESC upload_description{};
        upload_description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upload_description.Width = size;
        upload_description.Height = 1;
        upload_description.DepthOrArraySize = 1;
        upload_description.MipLevels = 1;
        upload_description.SampleDesc.Count = 1;
        upload_description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        Microsoft::WRL::ComPtr<ID3D12Resource> upload_resource;
        if (FAILED(device_->CreateCommittedResource(&upload_heap,
            D3D12_HEAP_FLAG_NONE, &upload_description,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload_resource))))
            return false;

        void* mapped_data = nullptr;
        if (FAILED(upload_resource->Map(0, nullptr, &mapped_data))) return false;
        std::memcpy(mapped_data, data, static_cast<std::size_t>(size));
        upload_resource->Unmap(0, nullptr);

        if (FAILED(command_allocator_->Reset()) ||
            FAILED(command_list_->Reset(command_allocator_.Get(), nullptr)))
            return false;
        command_list_->CopyBufferRegion(destination, 0, upload_resource.Get(), 0, size);
        if (final_state != D3D12_RESOURCE_STATE_COPY_DEST)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = destination;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = final_state;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            command_list_->ResourceBarrier(1, &barrier);
        }
        if (FAILED(command_list_->Close())) return false;
        ID3D12CommandList* lists[] = { command_list_.Get() };
        queue_->ExecuteCommandLists(1, lists);
        return WaitForGpu();
    }

    bool D3D12UploadContext::WaitForGpu() noexcept
    {
        if (!IsInitialized() || fence_event_ == nullptr) return false;
        const std::uint64_t value = ++fence_value_;
        if (FAILED(queue_->Signal(fence_.Get(), value))) return false;
        if (fence_->GetCompletedValue() < value)
        {
            if (FAILED(fence_->SetEventOnCompletion(value, fence_event_))) return false;
            if (WaitForSingleObject(fence_event_, INFINITE) != WAIT_OBJECT_0) return false;
        }
        return true;
    }
}
