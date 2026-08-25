#include "D3D12DeviceContext.h"
#include "D3D12ResourceFactory.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <filesystem>

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        constexpr DXGI_FORMAT kUiRenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

        bool SerializeUiRootSignature(ID3D12Device* device,
            const D3D12_ROOT_SIGNATURE_DESC& desc,
            Microsoft::WRL::ComPtr<ID3D12RootSignature>& root) noexcept
        {
            if (device == nullptr) return false;
            Microsoft::WRL::ComPtr<ID3DBlob> serialized;
            Microsoft::WRL::ComPtr<ID3DBlob> errors;
            const HRESULT serialize_result = D3D12SerializeRootSignature(&desc,
                D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
            if (FAILED(serialize_result))
            {
                std::fprintf(stderr, "[DX12] UI root signature serialization failed: 0x%08lx\n",
                    static_cast<unsigned long>(serialize_result));
                return false;
            }
            const HRESULT root_result = device->CreateRootSignature(0,
                serialized->GetBufferPointer(), serialized->GetBufferSize(),
                IID_PPV_ARGS(&root));
            if (FAILED(root_result))
            {
                std::fprintf(stderr, "[DX12] UI root signature creation failed: 0x%08lx\n",
                    static_cast<unsigned long>(root_result));
                return false;
            }
            return true;
        }

        D3D12_BLEND_DESC MakeUiBlend(D3D12UIBlendMode mode) noexcept
        {
            D3D12_BLEND_DESC blend{};
            auto& target = blend.RenderTarget[0];
            target.BlendEnable = TRUE;
            target.BlendOp = D3D12_BLEND_OP_ADD;
            target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            target.SrcBlendAlpha = D3D12_BLEND_ONE;
            target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            switch (mode)
            {
            case D3D12UIBlendMode::Additive:
                target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                target.DestBlend = D3D12_BLEND_ONE;
                break;
            case D3D12UIBlendMode::Multiply:
                target.SrcBlend = D3D12_BLEND_ZERO;
                target.DestBlend = D3D12_BLEND_SRC_COLOR;
                break;
            case D3D12UIBlendMode::Screen:
                target.SrcBlend = D3D12_BLEND_ONE;
                target.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
                break;
            case D3D12UIBlendMode::Premultiplied:
                target.SrcBlend = D3D12_BLEND_ONE;
                target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                break;
            default:
                target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                break;
            }
            return blend;
        }
    }

    bool D3D12DeviceContext::CreateUIRendererResources() noexcept
    {
        if (device_ == nullptr) return false;
        D3D12ShaderCompiler compiler;
        if (!compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath())) return false;
        const std::filesystem::path shader_directory =
            std::filesystem::current_path() / "Shader";
        const auto vertex = compiler.CompileFile(shader_directory / "dx12_ui_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        const auto pixel = compiler.CompileFile(shader_directory / "dx12_ui_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        compiler.Shutdown();
        if (!vertex.succeeded || !pixel.succeeded)
        {
            OutputDebugStringA("[DX12] UI shader compilation failed.\n");
            std::fprintf(stderr, "[DX12] UI shader compilation failed: VS=0x%08lx PS=0x%08lx\n",
                static_cast<unsigned long>(vertex.status),
                static_cast<unsigned long>(pixel.status));
            if (!vertex.diagnostics.empty()) OutputDebugStringA(vertex.diagnostics.c_str());
            if (!pixel.diagnostics.empty()) OutputDebugStringA(pixel.diagnostics.c_str());
            if (!vertex.diagnostics.empty()) std::fprintf(stderr, "%s\n", vertex.diagnostics.c_str());
            if (!pixel.diagnostics.empty()) std::fprintf(stderr, "%s\n", pixel.diagnostics.c_str());
            return false;
        }
        ui_vertex_shader_ = vertex.bytecode;
        ui_pixel_shader_ = pixel.bytecode;

        D3D12_DESCRIPTOR_RANGE texture_ranges[5]{};
        for (std::size_t index = 0; index < std::size(texture_ranges); ++index)
        {
            texture_ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            texture_ranges[index].NumDescriptors = 1;
            texture_ranges[index].BaseShaderRegister = static_cast<UINT>(index);
        }
        D3D12_ROOT_PARAMETER parameters[6]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].Descriptor.ShaderRegister = 0;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        for (std::size_t index = 0; index < std::size(texture_ranges); ++index)
        {
            parameters[index + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[index + 1].DescriptorTable.NumDescriptorRanges = 1;
            parameters[index + 1].DescriptorTable.pDescriptorRanges = &texture_ranges[index];
            parameters[index + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = static_cast<UINT>(std::size(parameters));
        root_desc.pParameters = parameters;
        root_desc.NumStaticSamplers = 1;
        root_desc.pStaticSamplers = &sampler;
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        if (!SerializeUiRootSignature(device_.Get(), root_desc, ui_root_signature_)) return false;

        const D3D12_INPUT_ELEMENT_DESC input_layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
                static_cast<UINT>(offsetof(D3D12UIVertex, position)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
                static_cast<UINT>(offsetof(D3D12UIVertex, uv)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                static_cast<UINT>(offsetof(D3D12UIVertex, color)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                static_cast<UINT>(offsetof(D3D12UIVertex, uv_bounds)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        for (std::size_t index = 0; index < std::size(ui_pipelines_); ++index)
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
            pipeline.pRootSignature = ui_root_signature_.Get();
            pipeline.VS = { ui_vertex_shader_.data(), ui_vertex_shader_.size() };
            pipeline.PS = { ui_pixel_shader_.data(), ui_pixel_shader_.size() };
            pipeline.InputLayout = { input_layout, static_cast<UINT>(std::size(input_layout)) };
            pipeline.BlendState = MakeUiBlend(static_cast<D3D12UIBlendMode>(index));
            pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pipeline.RasterizerState.DepthClipEnable = TRUE;
            pipeline.DepthStencilState.DepthEnable = FALSE;
            pipeline.DepthStencilState.StencilEnable = FALSE;
            pipeline.SampleMask = UINT_MAX;
            pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pipeline.NumRenderTargets = 1;
            pipeline.RTVFormats[0] = kUiRenderTargetFormat;
            pipeline.SampleDesc.Count = 1;
            const HRESULT pipeline_result = device_->CreateGraphicsPipelineState(&pipeline,
                IID_PPV_ARGS(&ui_pipelines_[index]));
            if (FAILED(pipeline_result))
            {
                std::fprintf(stderr, "[DX12] UI PSO[%zu] creation failed: 0x%08lx\n",
                    index, static_cast<unsigned long>(pipeline_result));
                ReleaseUIRendererResources();
                return false;
            }
        }
        return true;
    }

    bool D3D12DeviceContext::CreateUIEffectResources() noexcept
    {
        if (device_ == nullptr || ui_vertex_shader_.empty()) return false;
        D3D12ShaderCompiler compiler;
        if (!compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath())) return false;
        const std::filesystem::path shader_directory =
            std::filesystem::current_path() / "Shader";
        const auto pixel = compiler.CompileFile(shader_directory / "dx12_ui_effect_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        compiler.Shutdown();
        if (!pixel.succeeded)
        {
            OutputDebugStringA("[DX12] UI effect shader compilation failed.\n");
            std::fprintf(stderr, "[DX12] UI effect shader compilation failed: 0x%08lx\n",
                static_cast<unsigned long>(pixel.status));
            if (!pixel.diagnostics.empty()) OutputDebugStringA(pixel.diagnostics.c_str());
            if (!pixel.diagnostics.empty()) std::fprintf(stderr, "%s\n", pixel.diagnostics.c_str());
            return false;
        }
        ui_effect_pixel_shader_ = pixel.bytecode;

        D3D12_DESCRIPTOR_RANGE texture_ranges[2]{};
        for (std::size_t index = 0; index < std::size(texture_ranges); ++index)
        {
            texture_ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            texture_ranges[index].NumDescriptors = 1;
            texture_ranges[index].BaseShaderRegister = static_cast<UINT>(index);
        }
        D3D12_ROOT_PARAMETER parameters[3]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].Descriptor.ShaderRegister = 0;
        // The shared UI vertex shader also consumes screen_size from b0.
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        for (std::size_t index = 0; index < std::size(texture_ranges); ++index)
        {
            parameters[index + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[index + 1].DescriptorTable.NumDescriptorRanges = 1;
            parameters[index + 1].DescriptorTable.pDescriptorRanges = &texture_ranges[index];
            parameters[index + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = static_cast<UINT>(std::size(parameters));
        root_desc.pParameters = parameters;
        root_desc.NumStaticSamplers = 1;
        root_desc.pStaticSamplers = &sampler;
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        if (!SerializeUiRootSignature(device_.Get(), root_desc, ui_effect_root_signature_))
            return false;

        const D3D12_INPUT_ELEMENT_DESC input_layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
                static_cast<UINT>(offsetof(D3D12UIVertex, position)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
                static_cast<UINT>(offsetof(D3D12UIVertex, uv)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                static_cast<UINT>(offsetof(D3D12UIVertex, color)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                static_cast<UINT>(offsetof(D3D12UIVertex, uv_bounds)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = ui_effect_root_signature_.Get();
        pipeline.VS = { ui_vertex_shader_.data(), ui_vertex_shader_.size() };
        pipeline.PS = { ui_effect_pixel_shader_.data(), ui_effect_pixel_shader_.size() };
        pipeline.InputLayout = { input_layout, static_cast<UINT>(std::size(input_layout)) };
        pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline.RasterizerState.DepthClipEnable = TRUE;
        pipeline.DepthStencilState.DepthEnable = FALSE;
        pipeline.DepthStencilState.StencilEnable = FALSE;
        pipeline.BlendState.RenderTarget[0].BlendEnable = FALSE;
        pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline.SampleMask = UINT_MAX;
        pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets = 1;
        pipeline.RTVFormats[0] = kUiRenderTargetFormat;
        pipeline.SampleDesc.Count = 1;
        const HRESULT pipeline_result = device_->CreateGraphicsPipelineState(&pipeline,
            IID_PPV_ARGS(&ui_effect_pipeline_));
        if (FAILED(pipeline_result))
        {
            std::fprintf(stderr, "[DX12] UI effect PSO creation failed: 0x%08lx\n",
                static_cast<unsigned long>(pipeline_result));
            ReleaseUIEffectResources();
            return false;
        }
        return true;
    }

    void D3D12DeviceContext::ReleaseUIRendererResources() noexcept
    {
        ReleaseUIEffectResources();
        for (auto& entry : ui_font_texture_cache_)
        {
            if (entry.second.srv.IsValid()) resource_descriptor_allocator_.Free(entry.second.srv);
        }
        ui_font_texture_cache_.clear();
        ui_font_texture_revisions_.clear();
        for (auto& pipeline : ui_pipelines_) pipeline.Reset();
        ui_root_signature_.Reset();
        ui_vertex_shader_.clear();
        ui_pixel_shader_.clear();
    }

    void D3D12DeviceContext::ReleaseUIEffectResources() noexcept
    {
        ui_effect_pipeline_.Reset();
        ui_effect_root_signature_.Reset();
        ui_effect_pixel_shader_.clear();
    }

    bool D3D12DeviceContext::EnsureUIFontTexture(
        const D3D12UIFontAtlasSource& source) noexcept
    {
        if (source.key.empty() || source.rgba.empty() || source.width == 0 || source.height == 0)
            return false;
        const auto existing = ui_font_texture_cache_.find(source.key);
        const auto revision = ui_font_texture_revisions_.find(source.key);
        if (existing != ui_font_texture_cache_.end() && revision != ui_font_texture_revisions_.end() &&
            revision->second == source.revision)
            return true;

        // Atlas再生成時に旧SRVをGPU完了前へ戻さない。Atlas更新は通常低頻度なので、
        // Fenceを明示的に待ってから同じ論理キーを差し替える。
        if (existing != ui_font_texture_cache_.end())
        {
            if (!WaitForGpu()) return false;
            if (existing->second.srv.IsValid())
                resource_descriptor_allocator_.Free(existing->second.srv);
            ui_font_texture_cache_.erase(existing);
            ui_font_texture_revisions_.erase(source.key);
        }

        StaticTextureResource texture;
        const std::uint64_t expected = static_cast<std::uint64_t>(source.width) *
            static_cast<std::uint64_t>(source.height) * 4ull;
        if (expected != source.rgba.size()) return false;
        if (!D3D12ResourceFactory::CreateTexture2DRgba8(device_.Get(), upload_context_,
            source.rgba.data(), source.width, source.height, source.width * 4u,
            texture.resource))
            return false;
        if (!resource_descriptor_allocator_.Allocate(1, texture.srv)) return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(texture.resource.Get(), &srv, texture.srv.cpu);
        texture.width = source.width;
        texture.height = source.height;
        texture.mip_levels = 1;
        texture.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        try
        {
            ui_font_texture_cache_.emplace(source.key, std::move(texture));
            ui_font_texture_revisions_[source.key] = source.revision;
        }
        catch (...)
        {
            if (texture.srv.IsValid()) resource_descriptor_allocator_.Free(texture.srv);
            return false;
        }
        return true;
    }

    bool D3D12DeviceContext::DrawRuntimeUIToTarget(
        const D3D12UIFrame& frame,
        D3D12OffscreenTarget* output_target,
        D3D12OffscreenTarget* effect_targets) noexcept
    {
        if (!frame_open_ || ui_root_signature_ == nullptr) return true;
        if (frame.target_width == 0 || frame.target_height == 0) return true;
        if (effect_targets == nullptr) effect_targets = ui_effect_targets_;
        if (output_target != nullptr && (!output_target->IsValid() ||
            output_target->width != frame.target_width ||
            output_target->height != frame.target_height)) return false;
        if (frame.batches.empty())
        {
            if (output_target != nullptr)
            {
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    output_target->color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
                    return false;
                const float clear[4]{ 0, 0, 0, 0 };
                command_list_->ClearRenderTargetView(output_target->rtv.cpu, clear, 0, nullptr);
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    output_target->color.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                    return false;
            }
            return true;
        }

        if (!upload_context_.BeginBatch()) return false;
        bool upload_ok = true;
        for (const D3D12StaticTextureSource& source : frame.texture_sources)
        {
            if (source.key.empty() || texture_cache_.find(source.key) != texture_cache_.end()) continue;
            if (!EnsureStaticTexture(source) &&
                static_texture_failures_.find(source.key) == static_texture_failures_.end())
                upload_ok = false;
        }
        for (const D3D12UIFontAtlasSource& source : frame.font_atlases)
        {
            if (!EnsureUIFontTexture(source)) upload_ok = false;
        }
        if (!upload_context_.EndBatch()) upload_ok = false;
        if (!upload_ok) return false;

        const auto white = texture_cache_.find("__dx12_white");
        if (white == texture_cache_.end()) return false;
        ID3D12DescriptorHeap* heaps[] =
        {
            resource_descriptor_allocator_.Heap(), sampler_descriptor_allocator_.Heap()
        };
        command_list_->SetDescriptorHeaps(static_cast<UINT>(std::size(heaps)), heaps);
        D3D12LinearUploadAllocator& allocator = frame_resources_[frame_index_].upload_allocator;
        const D3D12_VIEWPORT viewport{
            0.0f, 0.0f, static_cast<float>(frame.target_width),
            static_cast<float>(frame.target_height), 0.0f, 1.0f };
        const D3D12_RECT full_scissor{ 0, 0,
            static_cast<LONG>(frame.target_width), static_cast<LONG>(frame.target_height) };

        const auto texture_for_key = [&](const std::string& key)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE texture = white->second.srv.gpu;
            const auto image = texture_cache_.find(key);
            if (image != texture_cache_.end()) texture = image->second.srv.gpu;
            else
            {
                const auto font = ui_font_texture_cache_.find(key);
                if (font != ui_font_texture_cache_.end()) texture = font->second.srv.gpu;
            }
            return texture;
        };

        const auto draw_batches = [&](D3D12_CPU_DESCRIPTOR_HANDLE rtv,
            std::int32_t requested_group, std::size_t first_batch,
            std::size_t last_batch) noexcept
        {
            command_list_->SetGraphicsRootSignature(ui_root_signature_.Get());
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            command_list_->RSSetViewports(1, &viewport);
            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            last_batch = (std::min)(last_batch, frame.batches.size());
            for (std::size_t batch_index = first_batch; batch_index < last_batch; ++batch_index)
            {
                const D3D12UIBatch& batch = frame.batches[batch_index];
                if (requested_group == -1 && batch.effect_group >= 0) continue;
                if (requested_group >= 0 && batch.effect_group != requested_group) continue;
                if (batch.vertices.empty()) continue;
                D3D12UploadAllocation vertex_upload{};
                D3D12UploadAllocation constants_upload{};
                const std::uint64_t vertex_bytes =
                    static_cast<std::uint64_t>(batch.vertices.size()) * sizeof(D3D12UIVertex);
                if (!allocator.Allocate(vertex_bytes, 16, vertex_upload) ||
                    !allocator.Allocate(sizeof(D3D12UIVisualConstants),
                        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants_upload))
                    return false;
                std::memcpy(vertex_upload.cpu, batch.vertices.data(),
                    static_cast<std::size_t>(vertex_bytes));
                std::memcpy(constants_upload.cpu, &batch.constants,
                    sizeof(batch.constants));
                ID3D12PipelineState* pipeline =
                    ui_pipelines_[static_cast<std::size_t>(batch.blend)].Get();
                if (pipeline == nullptr) return false;
                command_list_->SetPipelineState(pipeline);
                command_list_->SetGraphicsRootConstantBufferView(0, constants_upload.gpu);
                command_list_->SetGraphicsRootDescriptorTable(1,
                    texture_for_key(batch.texture_key));
                for (std::size_t mask_index = 0; mask_index < batch.masks.size(); ++mask_index)
                {
                    const D3D12_GPU_DESCRIPTOR_HANDLE mask_texture =
                        batch.mask_enabled && mask_index < batch.mask_count
                        ? texture_for_key(batch.masks[mask_index].texture_key)
                        : white->second.srv.gpu;
                    command_list_->SetGraphicsRootDescriptorTable(
                        static_cast<UINT>(2 + mask_index), mask_texture);
                }
                command_list_->RSSetScissorRects(1,
                    batch.scissor_enabled ? &batch.scissor : &full_scissor);
                D3D12_VERTEX_BUFFER_VIEW view{};
                view.BufferLocation = vertex_upload.gpu;
                view.SizeInBytes = static_cast<UINT>(vertex_bytes);
                view.StrideInBytes = sizeof(D3D12UIVertex);
                command_list_->IASetVertexBuffers(0, 1, &view);
                command_list_->DrawInstanced(static_cast<UINT>(batch.vertices.size()), 1, 0, 0);
            }
            return true;
        };

        const auto make_fullscreen_vertices = [&]()
        {
            const float width = static_cast<float>(frame.target_width);
            const float height = static_cast<float>(frame.target_height);
            const D3D12UIVertex white_vertex{
                { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 1, 1, 1, 1 }, { 0, 0, 1, 1 } };
            std::array<D3D12UIVertex, 6> vertices =
            {
                white_vertex,
                D3D12UIVertex{{ 0.0f, height }, { 0.0f, 1.0f }, { 1, 1, 1, 1 }, { 0, 0, 1, 1 }},
                D3D12UIVertex{{ width, height }, { 1.0f, 1.0f }, { 1, 1, 1, 1 }, { 0, 0, 1, 1}},
                white_vertex,
                D3D12UIVertex{{ width, height }, { 1.0f, 1.0f }, { 1, 1, 1, 1 }, { 0, 0, 1, 1}},
                D3D12UIVertex{{ width, 0.0f }, { 1.0f, 0.0f }, { 1, 1, 1, 1 }, { 0, 0, 1, 1}}
            };
            return vertices;
        };

        const auto draw_composite = [&](D3D12_GPU_DESCRIPTOR_HANDLE source,
            D3D12_CPU_DESCRIPTOR_HANDLE rtv) noexcept
        {
            const auto vertices = make_fullscreen_vertices();
            D3D12UploadAllocation vertex_upload{};
            D3D12UploadAllocation constants_upload{};
            if (!allocator.Allocate(sizeof(vertices), 16, vertex_upload) ||
                !allocator.Allocate(sizeof(D3D12UIVisualConstants),
                    D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants_upload))
                return false;
            D3D12UIVisualConstants constants{};
            constants.screen_size = { static_cast<float>(frame.target_width),
                static_cast<float>(frame.target_height), 0, 0 };
            std::memcpy(vertex_upload.cpu, vertices.data(), sizeof(vertices));
            std::memcpy(constants_upload.cpu, &constants, sizeof(constants));
            command_list_->SetGraphicsRootSignature(ui_root_signature_.Get());
            command_list_->SetPipelineState(ui_pipelines_[
                static_cast<std::size_t>(D3D12UIBlendMode::Alpha)].Get());
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            command_list_->RSSetViewports(1, &viewport);
            command_list_->RSSetScissorRects(1, &full_scissor);
            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            command_list_->SetGraphicsRootConstantBufferView(0, constants_upload.gpu);
            command_list_->SetGraphicsRootDescriptorTable(1, source);
            command_list_->SetGraphicsRootDescriptorTable(2, white->second.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(3, white->second.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(4, white->second.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(5, white->second.srv.gpu);
            D3D12_VERTEX_BUFFER_VIEW view{};
            view.BufferLocation = vertex_upload.gpu;
            view.SizeInBytes = sizeof(vertices);
            view.StrideInBytes = sizeof(D3D12UIVertex);
            command_list_->IASetVertexBuffers(0, 1, &view);
            command_list_->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
            return true;
        };

        const D3D12_CPU_DESCRIPTOR_HANDLE output_rtv = output_target != nullptr
            ? output_target->rtv.cpu : CurrentRenderTargetView();
        const auto finish_output = [&]() noexcept
        {
            return output_target == nullptr || resource_state_tracker_.Transition(
                command_list_.Get(), output_target->color.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        };
        if (output_target != nullptr)
        {
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                output_target->color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
                return false;
            const float clear[4]{ 0, 0, 0, 0 };
            command_list_->ClearRenderTargetView(output_target->rtv.cpu, clear, 0, nullptr);
        }

        if ((!frame.requires_offscreen && !frame.capture_backdrop && frame.effect_groups.empty()) ||
            !effect_targets[0].IsValid() ||
            ui_effect_pipeline_ == nullptr)
        {
            return draw_batches(output_rtv, -1, 0, frame.batches.size()) && finish_output();
        }

        const auto apply_effects = [&](D3D12OffscreenTarget*& source_target,
            const std::vector<D3D12UIEffectCommand>& effects,
            bool capture_backdrop, D3D12OffscreenTarget* backdrop_target) noexcept
        {
            for (const D3D12UIEffectCommand& effect : effects)
            {
                D3D12OffscreenTarget* destination = source_target == &effect_targets[0]
                    ? &effect_targets[1] : &effect_targets[0];
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    destination->color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET)) return false;
                command_list_->OMSetRenderTargets(1, &destination->rtv.cpu, FALSE, nullptr);
                command_list_->RSSetViewports(1, &viewport);
                command_list_->RSSetScissorRects(1, &full_scissor);
                command_list_->SetGraphicsRootSignature(ui_effect_root_signature_.Get());
                command_list_->SetPipelineState(ui_effect_pipeline_.Get());

                D3D12UIVisualConstants constants{};
                constants.screen_size = { static_cast<float>(frame.target_width),
                    static_cast<float>(frame.target_height), 0, 0 };
                constants.mode.x = static_cast<float>(effect.kind);
                constants.fill_color_2 = effect.color;
                constants.outline_color = effect.color_2;
                constants.shadow_offset = { effect.direction.x, effect.direction.y, 0, 0 };
                constants.shadow_color = effect.color_2;
                constants.fill_parameters = { effect.radius, effect.intensity,
                    effect.amount, capture_backdrop ? 1.0f : 0.0f };
                const auto vertices = make_fullscreen_vertices();
                D3D12UploadAllocation vertex_upload{};
                D3D12UploadAllocation constants_upload{};
                if (!allocator.Allocate(sizeof(vertices), 16, vertex_upload) ||
                    !allocator.Allocate(sizeof(constants),
                        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants_upload))
                    return false;
                std::memcpy(vertex_upload.cpu, vertices.data(), sizeof(vertices));
                std::memcpy(constants_upload.cpu, &constants, sizeof(constants));
                command_list_->SetGraphicsRootConstantBufferView(0, constants_upload.gpu);
                command_list_->SetGraphicsRootDescriptorTable(1, source_target->srv.gpu);
                command_list_->SetGraphicsRootDescriptorTable(2,
                    backdrop_target != nullptr ? backdrop_target->srv.gpu : source_target->srv.gpu);
                D3D12_VERTEX_BUFFER_VIEW view{};
                view.BufferLocation = vertex_upload.gpu;
                view.SizeInBytes = sizeof(vertices);
                view.StrideInBytes = sizeof(D3D12UIVertex);
                command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                command_list_->IASetVertexBuffers(0, 1, &view);
                command_list_->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    destination->color.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                    return false;
                source_target = destination;
            }
            return true;
        };

        if (!frame.effect_groups.empty())
        {
            const auto capture_current_backdrop = [&](D3D12OffscreenTarget*& target) noexcept
            {
                if (!effect_targets[2].IsValid()) return false;
                ID3D12Resource* backdrop_source = output_target != nullptr
                    ? output_target->color.Get() : render_targets_[frame_index_].Get();
                if (!resource_state_tracker_.Transition(command_list_.Get(), backdrop_source,
                    D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                    !resource_state_tracker_.Transition(command_list_.Get(),
                        effect_targets[2].color.Get(), D3D12_RESOURCE_STATE_COPY_DEST))
                    return false;
                command_list_->CopyResource(effect_targets[2].color.Get(), backdrop_source);
                if (!resource_state_tracker_.Transition(command_list_.Get(), backdrop_source,
                    D3D12_RESOURCE_STATE_RENDER_TARGET) ||
                    !resource_state_tracker_.Transition(command_list_.Get(),
                        effect_targets[2].color.Get(),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                    return false;
                target = &effect_targets[2];
                return true;
            };

            std::size_t batch_index = 0;
            while (batch_index < frame.batches.size())
            {
                const D3D12UIBatch& batch = frame.batches[batch_index];
                if (batch.effect_group < 0 ||
                    static_cast<std::size_t>(batch.effect_group) >= frame.effect_groups.size())
                {
                    std::size_t direct_end = batch_index + 1;
                    while (direct_end < frame.batches.size() &&
                        frame.batches[direct_end].effect_group < 0)
                        ++direct_end;
                    if (!draw_batches(output_rtv, -1,
                        batch_index, direct_end)) return false;
                    batch_index = direct_end;
                    continue;
                }

                const std::size_t group_index = static_cast<std::size_t>(batch.effect_group);
                const D3D12UIEffectGroup& group = frame.effect_groups[group_index];
                const std::size_t group_first = (std::min)(
                    static_cast<std::size_t>(group.first_batch), frame.batches.size());
                const std::size_t group_end = (std::min)(
                    group_first + static_cast<std::size_t>(group.batch_count),
                    frame.batches.size());
                if (group_first != batch_index || group_end <= group_first)
                {
                    // Invalid/non-contiguous group metadata must not stall the frame.
                    ++batch_index;
                    continue;
                }

                D3D12OffscreenTarget* group_backdrop = nullptr;
                if (group.capture_backdrop && !capture_current_backdrop(group_backdrop))
                    return false;
                D3D12OffscreenTarget* source_target = &effect_targets[0];
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    source_target->color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
                    return false;
                const float clear[4]{ 0, 0, 0, 0 };
                command_list_->ClearRenderTargetView(source_target->rtv.cpu, clear, 0, nullptr);
                if (!draw_batches(source_target->rtv.cpu,
                    static_cast<std::int32_t>(group_index), group_first, group_end))
                    return false;
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    source_target->color.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                    return false;
                if (!apply_effects(source_target, group.effects,
                    group.capture_backdrop, group_backdrop)) return false;
                if (!draw_composite(source_target->srv.gpu, output_rtv))
                    return false;
                batch_index = group_end;
            }
            return finish_output();
        }

        D3D12OffscreenTarget* backdrop_target = nullptr;
        if (frame.capture_backdrop && effect_targets[2].IsValid())
        {
            ID3D12Resource* backdrop_source = output_target != nullptr
                ? output_target->color.Get() : render_targets_[frame_index_].Get();
            if (!resource_state_tracker_.Transition(command_list_.Get(), backdrop_source,
                D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                !resource_state_tracker_.Transition(command_list_.Get(),
                    effect_targets[2].color.Get(), D3D12_RESOURCE_STATE_COPY_DEST))
                return false;
            command_list_->CopyResource(effect_targets[2].color.Get(), backdrop_source);
            if (!resource_state_tracker_.Transition(command_list_.Get(), backdrop_source,
                D3D12_RESOURCE_STATE_RENDER_TARGET) ||
                !resource_state_tracker_.Transition(command_list_.Get(),
                    effect_targets[2].color.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
            backdrop_target = &effect_targets[2];
        }

        D3D12OffscreenTarget* source_target = &effect_targets[0];
        if (!resource_state_tracker_.Transition(command_list_.Get(), source_target->color.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET)) return false;
        const float clear[4]{ 0, 0, 0, 0 };
        command_list_->ClearRenderTargetView(source_target->rtv.cpu, clear, 0, nullptr);
        if (!draw_batches(source_target->rtv.cpu, -2, 0, frame.batches.size())) return false;
        if (!resource_state_tracker_.Transition(command_list_.Get(), source_target->color.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) return false;

        if (!apply_effects(source_target, frame.effects, frame.capture_backdrop,
            backdrop_target)) return false;

        return draw_composite(source_target->srv.gpu, output_rtv) && finish_output();
    }

    bool D3D12DeviceContext::DrawRuntimeUI(const D3D12UIFrame& frame) noexcept
    {
        return DrawRuntimeUIToTarget(frame, nullptr, ui_effect_targets_);
    }

    bool D3D12DeviceContext::EnsureUIPreviewTarget(
        std::uint32_t width, std::uint32_t height) noexcept
    {
        if (!IsInitialized() || frame_open_ || width == 0 || height == 0 ||
            width > 16384u || height > 16384u) return false;
        if (ui_preview_target_.IsValid() && ui_preview_target_.width == width &&
            ui_preview_target_.height == height && ui_preview_effect_targets_[0].IsValid() &&
            ui_preview_effect_targets_[0].width == width &&
            ui_preview_effect_targets_[0].height == height)
            return true;
        if (!WaitForGpu()) return false;
        ReleaseOffscreenTarget(ui_preview_target_);
        for (D3D12OffscreenTarget& target : ui_preview_effect_targets_)
            ReleaseOffscreenTarget(target);
        if (!CreateOffscreenTarget(ui_preview_target_, width, height,
            DXGI_FORMAT_R8G8B8A8_UNORM) ||
            !CreateOffscreenTarget(ui_preview_effect_targets_[0], width, height,
                DXGI_FORMAT_R8G8B8A8_UNORM) ||
            !CreateOffscreenTarget(ui_preview_effect_targets_[1], width, height,
                DXGI_FORMAT_R8G8B8A8_UNORM) ||
            !CreateOffscreenTarget(ui_preview_effect_targets_[2], width, height,
                DXGI_FORMAT_R8G8B8A8_UNORM))
        {
            ReleaseOffscreenTarget(ui_preview_target_);
            for (D3D12OffscreenTarget& target : ui_preview_effect_targets_)
                ReleaseOffscreenTarget(target);
            return false;
        }
#ifdef USE_IMGUI
        ui_preview_texture_id_ = static_cast<std::uint64_t>(
            ui_preview_target_.srv.gpu.ptr);
#endif
        return true;
    }

    bool D3D12DeviceContext::DrawRuntimeUIPreview(const D3D12UIFrame& frame) noexcept
    {
        if (!ui_preview_target_.IsValid()) return false;
        return DrawRuntimeUIToTarget(frame, &ui_preview_target_,
            ui_preview_effect_targets_);
    }
}
