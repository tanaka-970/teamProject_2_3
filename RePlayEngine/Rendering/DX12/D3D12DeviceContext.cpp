#include "D3D12DeviceContext.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

        struct ValidationVertex
        {
            float position[3];
            float color[4];
        };

        constexpr ValidationVertex kValidationVertices[] =
        {
            { { 0.0f, 0.65f, 0.0f }, { 1.0f, 0.2f, 0.2f, 1.0f } },
            { { 0.65f, -0.55f, 0.0f }, { 0.2f, 1.0f, 0.2f, 1.0f } },
            { { -0.65f, -0.55f, 0.0f }, { 0.2f, 0.4f, 1.0f, 1.0f } },
        };

        constexpr char kValidationVertexShader[] = R"(
struct VertexInput { float3 position : POSITION; float4 color : COLOR; };
struct VertexOutput { float4 position : SV_POSITION; float4 color : COLOR; };
VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    return output;
})";

        constexpr char kValidationPixelShader[] = R"(
struct PixelInput { float4 position : SV_POSITION; float4 color : COLOR; };
float4 main(PixelInput input) : SV_TARGET
{
    return input.color;
})";

        bool IsValidSize(std::uint32_t width, std::uint32_t height) noexcept
        {
            return width != 0 && height != 0;
        }
    }

    D3D12DeviceContext::~D3D12DeviceContext()
    {
        Shutdown();
    }

    bool D3D12DeviceContext::Initialize(HWND window, std::uint32_t width,
        std::uint32_t height, bool enable_debug_layer, bool force_warp) noexcept
    {
        if (window == nullptr || !IsValidSize(width, height)) return false;
        Shutdown();
        if (!CreateDevice(enable_debug_layer, force_warp))
        {
            Shutdown();
            return false;
        }
        if (!CreateSwapChain(window, width, height) || !CreateRenderTargets() ||
            !CreateValidationTriangleResources())
        {
            Shutdown();
            return false;
        }
        return true;
    }

    void D3D12DeviceContext::Shutdown() noexcept
    {
        if (device_ != nullptr) WaitForGpu();
        ReleaseValidationTriangleResources();
        ReleaseRenderTargets();
        if (fence_event_ != nullptr)
        {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }
        command_list_.Reset();
        for (auto& allocator : command_allocators_) allocator.Reset();
        fence_.Reset();
        rtv_heap_.Reset();
        swap_chain_.Reset();
        command_queue_.Reset();
        device_.Reset();
        adapter_.Reset();
        factory_.Reset();
        frame_index_ = 0;
        width_ = 0;
        height_ = 0;
        frame_open_ = false;
        std::fill(std::begin(fence_values_), std::end(fence_values_), 0);
    }

    bool D3D12DeviceContext::CreateDevice(bool enable_debug_layer,
        bool force_warp) noexcept
    {
        UINT factory_flags = 0;
        if (enable_debug_layer)
        {
            Microsoft::WRL::ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
                factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
        if (FAILED(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory_))))
            return false;

        if (force_warp)
        {
            if (FAILED(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_)))) return false;
        }
        else
        {
            for (UINT index = 0;; ++index)
            {
                Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
                if (factory_->EnumAdapters1(index, &candidate) == DXGI_ERROR_NOT_FOUND)
                    break;
                DXGI_ADAPTER_DESC1 description{};
                if (FAILED(candidate->GetDesc1(&description)) ||
                    (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                    continue;
                if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                    __uuidof(ID3D12Device), nullptr)))
                {
                    if (FAILED(candidate.As(&adapter_))) return false;
                    break;
                }
            }
            if (adapter_ == nullptr)
            {
                if (FAILED(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_)))) return false;
            }
        }

        if (FAILED(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device_))))
            return false;

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (FAILED(device_->CreateCommandQueue(&queue_desc,
            IID_PPV_ARGS(&command_queue_))))
            return false;

        for (auto& allocator : command_allocators_)
        {
            if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&allocator))))
                return false;
        }
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            command_allocators_[0].Get(), nullptr, IID_PPV_ARGS(&command_list_))))
            return false;
        if (FAILED(command_list_->Close())) return false;
        if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&fence_))))
            return false;
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return fence_event_ != nullptr;
    }

    bool D3D12DeviceContext::CreateSwapChain(HWND window, std::uint32_t width,
        std::uint32_t height) noexcept
    {
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width;
        description.Height = height;
        description.Format = kBackBufferFormat;
        description.BufferCount = FrameCount;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.SampleDesc.Count = 1;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain;
        if (FAILED(factory_->CreateSwapChainForHwnd(command_queue_.Get(), window,
            &description, nullptr, nullptr, &swap_chain)))
            return false;
        if (FAILED(factory_->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER)))
            return false;
        if (FAILED(swap_chain.As(&swap_chain_))) return false;
        frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
        width_ = width;
        height_ = height;
        return true;
    }

    bool D3D12DeviceContext::CreateRenderTargets() noexcept
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.NumDescriptors = FrameCount;
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(device_->CreateDescriptorHeap(&heap_desc,
            IID_PPV_ARGS(&rtv_heap_))))
            return false;
        rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        for (std::uint32_t index = 0; index < FrameCount; ++index)
        {
            if (FAILED(swap_chain_->GetBuffer(index, IID_PPV_ARGS(&render_targets_[index]))))
                return false;
            device_->CreateRenderTargetView(render_targets_[index].Get(), nullptr, handle);
            handle.ptr += rtv_descriptor_size_;
        }
        return true;
    }

    bool D3D12DeviceContext::CreateValidationTriangleResources() noexcept
    {
        Microsoft::WRL::ComPtr<ID3DBlob> vertex_shader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixel_shader;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        if (FAILED(D3DCompile(kValidationVertexShader,
            sizeof(kValidationVertexShader) - 1, "DX12ValidationTriangle.hlsl",
            nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS,
            0, &vertex_shader, &errors)))
            return false;
        errors.Reset();
        if (FAILED(D3DCompile(kValidationPixelShader,
            sizeof(kValidationPixelShader) - 1, "DX12ValidationTriangle.hlsl",
            nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS,
            0, &pixel_shader, &errors)))
            return false;

        D3D12_ROOT_SIGNATURE_DESC root_signature_desc{};
        root_signature_desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Microsoft::WRL::ComPtr<ID3DBlob> serialized_root_signature;
        if (FAILED(D3D12SerializeRootSignature(&root_signature_desc,
            D3D_ROOT_SIGNATURE_VERSION_1, &serialized_root_signature, &errors)))
            return false;
        if (FAILED(device_->CreateRootSignature(0,
            serialized_root_signature->GetBufferPointer(),
            serialized_root_signature->GetBufferSize(),
            IID_PPV_ARGS(&validation_root_signature_))))
            return false;

        D3D12_INPUT_ELEMENT_DESC input_elements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
                0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_BLEND_DESC blend_desc{};
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        D3D12_RASTERIZER_DESC rasterizer_desc{};
        rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizer_desc.CullMode = D3D12_CULL_MODE_NONE;
        rasterizer_desc.DepthClipEnable = TRUE;
        D3D12_DEPTH_STENCIL_DESC depth_stencil_desc{};
        depth_stencil_desc.DepthEnable = FALSE;
        depth_stencil_desc.StencilEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.pRootSignature = validation_root_signature_.Get();
        pipeline_desc.VS = { vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize() };
        pipeline_desc.PS = { pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize() };
        pipeline_desc.BlendState = blend_desc;
        pipeline_desc.SampleMask = UINT_MAX;
        pipeline_desc.RasterizerState = rasterizer_desc;
        pipeline_desc.DepthStencilState = depth_stencil_desc;
        pipeline_desc.InputLayout = { input_elements, 2 };
        pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1;
        pipeline_desc.RTVFormats[0] = kBackBufferFormat;
        pipeline_desc.SampleDesc.Count = 1;
        if (FAILED(device_->CreateGraphicsPipelineState(&pipeline_desc,
            IID_PPV_ARGS(&validation_pipeline_))))
            return false;

        const UINT64 vertex_buffer_size = sizeof(kValidationVertices);
        D3D12_HEAP_PROPERTIES heap_properties{};
        heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC resource_desc{};
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resource_desc.Width = vertex_buffer_size;
        resource_desc.Height = 1;
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 1;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device_->CreateCommittedResource(&heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&validation_vertex_buffer_))))
            return false;
        void* mapped_data = nullptr;
        if (FAILED(validation_vertex_buffer_->Map(0, nullptr, &mapped_data))) return false;
        std::memcpy(mapped_data, kValidationVertices, sizeof(kValidationVertices));
        validation_vertex_buffer_->Unmap(0, nullptr);
        return true;
    }

    void D3D12DeviceContext::ReleaseRenderTargets() noexcept
    {
        for (auto& target : render_targets_) target.Reset();
        rtv_heap_.Reset();
    }

    void D3D12DeviceContext::ReleaseValidationTriangleResources() noexcept
    {
        validation_vertex_buffer_.Reset();
        validation_pipeline_.Reset();
        validation_root_signature_.Reset();
    }

    bool D3D12DeviceContext::Resize(std::uint32_t width, std::uint32_t height) noexcept
    {
        if (!IsInitialized() || frame_open_ || !IsValidSize(width, height)) return false;
        WaitForGpu();
        ReleaseRenderTargets();
        if (FAILED(swap_chain_->ResizeBuffers(FrameCount, width, height,
            kBackBufferFormat, 0)))
            return false;
        frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
        width_ = width;
        height_ = height;
        return CreateRenderTargets();
    }

    bool D3D12DeviceContext::WaitForFrame(std::uint32_t frame_index) noexcept
    {
        const std::uint64_t value = fence_values_[frame_index];
        if (value == 0 || fence_->GetCompletedValue() >= value) return true;
        if (FAILED(fence_->SetEventOnCompletion(value, fence_event_))) return false;
        return WaitForSingleObject(fence_event_, INFINITE) == WAIT_OBJECT_0;
    }

    bool D3D12DeviceContext::TransitionCurrentRenderTarget(
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) noexcept
    {
        if (command_list_ == nullptr || render_targets_[frame_index_] == nullptr) return false;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = render_targets_[frame_index_].Get();
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list_->ResourceBarrier(1, &barrier);
        return true;
    }

    bool D3D12DeviceContext::BeginFrame(const float clear_color[4]) noexcept
    {
        if (!IsInitialized() || frame_open_ || clear_color == nullptr) return false;
        if (!WaitForFrame(frame_index_)) return false;
        if (FAILED(command_allocators_[frame_index_]->Reset())) return false;
        if (FAILED(command_list_->Reset(command_allocators_[frame_index_].Get(), nullptr)))
            return false;
        if (!TransitionCurrentRenderTarget(D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET))
            return false;
        const D3D12_CPU_DESCRIPTOR_HANDLE view = CurrentRenderTargetView();
        command_list_->OMSetRenderTargets(1, &view, FALSE, nullptr);
        command_list_->ClearRenderTargetView(view, clear_color, 0, nullptr);
        frame_open_ = true;
        return true;
    }

    bool D3D12DeviceContext::EndFrame() noexcept
    {
        if (!frame_open_) return false;
        if (!TransitionCurrentRenderTarget(D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT))
            return false;
        if (FAILED(command_list_->Close())) return false;
        ID3D12CommandList* lists[] = { command_list_.Get() };
        command_queue_->ExecuteCommandLists(1, lists);
        if (FAILED(swap_chain_->Present(1, 0))) return false;
        const std::uint64_t signal_value = ++fence_values_[frame_index_];
        if (FAILED(command_queue_->Signal(fence_.Get(), signal_value))) return false;
        frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
        frame_open_ = false;
        return true;
    }

    bool D3D12DeviceContext::DrawValidationTriangle() noexcept
    {
        if (!frame_open_ || validation_pipeline_ == nullptr ||
            validation_root_signature_ == nullptr || validation_vertex_buffer_ == nullptr)
            return false;
        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width_);
        viewport.Height = static_cast<float>(height_);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, &scissor);
        command_list_->SetGraphicsRootSignature(validation_root_signature_.Get());
        command_list_->SetPipelineState(validation_pipeline_.Get());
        D3D12_VERTEX_BUFFER_VIEW vertex_view{};
        vertex_view.BufferLocation = validation_vertex_buffer_->GetGPUVirtualAddress();
        vertex_view.SizeInBytes = sizeof(kValidationVertices);
        vertex_view.StrideInBytes = sizeof(ValidationVertex);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->IASetVertexBuffers(0, 1, &vertex_view);
        command_list_->DrawInstanced(3, 1, 0, 0);
        return true;
    }

    void D3D12DeviceContext::WaitForGpu() noexcept
    {
        if (command_queue_ == nullptr || fence_ == nullptr || fence_event_ == nullptr) return;
        const std::uint64_t value = ++fence_values_[frame_index_];
        if (FAILED(command_queue_->Signal(fence_.Get(), value))) return;
        if (fence_->GetCompletedValue() >= value) return;
        if (SUCCEEDED(fence_->SetEventOnCompletion(value, fence_event_)))
            WaitForSingleObject(fence_event_, INFINITE);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12DeviceContext::CurrentRenderTargetView() const noexcept
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if (rtv_heap_ == nullptr) return handle;
        handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(frame_index_) * rtv_descriptor_size_;
        return handle;
    }
}
