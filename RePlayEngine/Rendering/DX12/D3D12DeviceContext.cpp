#include "D3D12DeviceContext.h"
#include "D3D12ResourceFactory.h"

#include <algorithm>
#include <filesystem>

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

        constexpr std::uint16_t kValidationIndices[] = { 0, 1, 2 };

        constexpr char kValidationVertexShader[] = R"(
struct RenderItemData
{
    row_major float4x4 world;
    float4 tint;
    uint owner_low;
    uint owner_high;
    uint flags;
    uint reserved;
};
cbuffer FrameConstants : register(b0)
{
    row_major float4x4 view_projection;
    float4 camera_position;
    float4 time_parameters;
};
StructuredBuffer<RenderItemData> render_items : register(t0);
struct VertexInput { float3 position : POSITION; float4 color : COLOR; };
struct VertexOutput { float4 position : SV_POSITION; float4 color : COLOR; };
VertexOutput main(VertexInput input, uint instance_id : SV_InstanceID)
{
    VertexOutput output;
    const RenderItemData item = render_items[instance_id];
    output.position = mul(mul(float4(input.position, 1.0f), item.world),
        view_projection);
    output.color = input.color * item.tint;
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
        for (auto& constant_buffer : frame_constant_buffers_)
            constant_buffer.Reset();
        ReleaseRenderTargets();
        for (auto& batch : render_item_batches_)
            batch.Reset(&resource_descriptor_allocator_);
        if (fence_event_ != nullptr)
        {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }
        command_list_.Reset();
        for (auto& allocator : command_allocators_) allocator.Reset();
        fence_.Reset();
        swap_chain_.Reset();
        resource_descriptor_allocator_.Reset();
        upload_context_.Shutdown();
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
        if (!upload_context_.Initialize(device_.Get(), command_queue_.Get()) ||
            !resource_descriptor_allocator_.Initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096, true))
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
        if (!rtv_allocator_.Initialize(device_.Get(),
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount, false) ||
            !rtv_allocator_.Allocate(FrameCount, rtv_allocation_) ||
            !dsv_allocator_.Initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false) ||
            !dsv_allocator_.Allocate(1, dsv_allocation_))
            return false;
        for (std::uint32_t index = 0; index < FrameCount; ++index)
        {
            if (FAILED(swap_chain_->GetBuffer(index, IID_PPV_ARGS(&render_targets_[index]))))
                return false;
            device_->CreateRenderTargetView(render_targets_[index].Get(), nullptr,
                rtv_allocator_.CpuHandle(rtv_allocation_.index + index));
        }

        D3D12_HEAP_PROPERTIES depth_heap{};
        depth_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC depth_description{};
        depth_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depth_description.Width = width_;
        depth_description.Height = height_;
        depth_description.DepthOrArraySize = 1;
        depth_description.MipLevels = 1;
        depth_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depth_description.SampleDesc.Count = 1;
        depth_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depth_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear_value{};
        clear_value.Format = depth_description.Format;
        clear_value.DepthStencil.Depth = 1.0f;
        clear_value.DepthStencil.Stencil = 0;
        if (FAILED(device_->CreateCommittedResource(&depth_heap, D3D12_HEAP_FLAG_NONE,
            &depth_description, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear_value,
            IID_PPV_ARGS(&depth_stencil_buffer_))))
            return false;
        device_->CreateDepthStencilView(depth_stencil_buffer_.Get(), nullptr,
            dsv_allocator_.CpuHandle(dsv_allocation_.index));
        return true;
    }

    bool D3D12DeviceContext::CreateValidationTriangleResources() noexcept
    {
        D3D12ShaderCompiler shader_compiler;
        if (!shader_compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath()))
            return false;
        const std::filesystem::path source_name =
            std::filesystem::current_path() / "DX12ValidationTriangle.hlsl";
        const D3D12ShaderCompileResult vertex_shader = shader_compiler.CompileSource(
            kValidationVertexShader, source_name, L"main", L"vs_6_0");
        const D3D12ShaderCompileResult pixel_shader = shader_compiler.CompileSource(
            kValidationPixelShader, source_name, L"main", L"ps_6_0");
        shader_compiler.Shutdown();
        if (!vertex_shader.succeeded || vertex_shader.bytecode.empty() ||
            !pixel_shader.succeeded || pixel_shader.bytecode.empty())
            return false;

        D3D12_ROOT_SIGNATURE_DESC root_signature_desc{};
        root_signature_desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        D3D12_DESCRIPTOR_RANGE render_item_range{};
        render_item_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        render_item_range.NumDescriptors = 1;
        render_item_range.BaseShaderRegister = 0;
        render_item_range.OffsetInDescriptorsFromTableStart =
            D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER root_parameter_list[2]{};
        root_parameter_list[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_parameter_list[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        root_parameter_list[0].Descriptor.ShaderRegister = 0;
        root_parameter_list[1].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameter_list[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        root_parameter_list[1].DescriptorTable.NumDescriptorRanges = 1;
        root_parameter_list[1].DescriptorTable.pDescriptorRanges = &render_item_range;
        root_signature_desc.NumParameters = 2;
        root_signature_desc.pParameters = root_parameter_list;
        Microsoft::WRL::ComPtr<ID3DBlob> serialized_root_signature;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
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
        depth_stencil_desc.DepthEnable = TRUE;
        depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        depth_stencil_desc.StencilEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.pRootSignature = validation_root_signature_.Get();
        pipeline_desc.VS = { vertex_shader.bytecode.data(), vertex_shader.bytecode.size() };
        pipeline_desc.PS = { pixel_shader.bytecode.data(), pixel_shader.bytecode.size() };
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

        return validation_mesh_.Upload(device_.Get(), upload_context_,
            kValidationVertices, sizeof(kValidationVertices), sizeof(ValidationVertex),
            kValidationIndices, sizeof(kValidationIndices), DXGI_FORMAT_R16_UINT);
    }

    void D3D12DeviceContext::ReleaseRenderTargets() noexcept
    {
        for (auto& target : render_targets_) target.Reset();
        depth_stencil_buffer_.Reset();
        rtv_allocator_.Reset();
        rtv_allocation_ = {};
        dsv_allocator_.Reset();
        dsv_allocation_ = {};
    }

    void D3D12DeviceContext::ReleaseValidationTriangleResources() noexcept
    {
        validation_mesh_.Reset();
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
        const D3D12_CPU_DESCRIPTOR_HANDLE depth_view = CurrentDepthStencilView();
        command_list_->OMSetRenderTargets(1, &view, FALSE, &depth_view);
        command_list_->ClearRenderTargetView(view, clear_color, 0, nullptr);
        command_list_->ClearDepthStencilView(depth_view,
            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
        frame_open_ = true;
        return true;
    }

    bool D3D12DeviceContext::SubmitRenderItems(
        const ::ReplayEngine::Rendering::RenderItemList& items) noexcept
    {
        if (!frame_open_) return false;
        return render_item_batches_[frame_index_].Upload(device_.Get(),
            upload_context_, resource_descriptor_allocator_, items);
    }

    bool D3D12DeviceContext::SubmitFrameConstants(
        const D3D12FrameConstants& constants) noexcept
    {
        if (!frame_open_) return false;
        frame_constant_buffers_[frame_index_].Reset();
        return D3D12ResourceFactory::CreateConstantBuffer(device_.Get(), upload_context_,
            &constants, sizeof(constants), frame_constant_buffers_[frame_index_]);
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
            validation_root_signature_ == nullptr || !validation_mesh_.IsValid() ||
            frame_constant_buffers_[frame_index_] == nullptr ||
            render_item_batches_[frame_index_].Empty() ||
            resource_descriptor_allocator_.Heap() == nullptr)
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
        ID3D12DescriptorHeap* descriptor_heaps[] =
        {
            resource_descriptor_allocator_.Heap()
        };
        command_list_->SetDescriptorHeaps(1, descriptor_heaps);
        command_list_->SetGraphicsRootConstantBufferView(0,
            frame_constant_buffers_[frame_index_]->GetGPUVirtualAddress());
        command_list_->SetGraphicsRootDescriptorTable(1,
            render_item_batches_[frame_index_].ShaderResourceAllocation().gpu);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->IASetVertexBuffers(0, 1, &validation_mesh_.VertexView());
        command_list_->IASetIndexBuffer(&validation_mesh_.IndexView());
        command_list_->DrawIndexedInstanced(validation_mesh_.IndexCount(),
            static_cast<UINT>(render_item_batches_[frame_index_].Size()), 0, 0, 0);
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
        return rtv_allocator_.CpuHandle(rtv_allocation_.index + frame_index_);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12DeviceContext::CurrentDepthStencilView() const noexcept
    {
        return dsv_allocator_.CpuHandle(dsv_allocation_.index);
    }
}
