#include "D3D12DeviceContext.h"
#include "D3D12ResourceFactory.h"
#include "D3D12ObjectName.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        constexpr DXGI_FORMAT kUiRenderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        constexpr DXGI_FORMAT kSceneEffectRenderTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

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
        const auto layer_extract = compiler.CompileFile(
            shader_directory / "dx12_screen_layer_extract_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        compiler.Shutdown();
        if (!vertex.succeeded || !pixel.succeeded || !layer_extract.succeeded)
        {
            OutputDebugStringA("[DX12] UI shader compilation failed.\n");
            std::fprintf(stderr, "[DX12] UI shader compilation failed: VS=0x%08lx PS=0x%08lx Layer=0x%08lx\n",
                static_cast<unsigned long>(vertex.status),
                static_cast<unsigned long>(pixel.status),
                static_cast<unsigned long>(layer_extract.status));
            if (!vertex.diagnostics.empty()) OutputDebugStringA(vertex.diagnostics.c_str());
            if (!pixel.diagnostics.empty()) OutputDebugStringA(pixel.diagnostics.c_str());
            if (!layer_extract.diagnostics.empty()) OutputDebugStringA(layer_extract.diagnostics.c_str());
            if (!vertex.diagnostics.empty()) std::fprintf(stderr, "%s\n", vertex.diagnostics.c_str());
            if (!pixel.diagnostics.empty()) std::fprintf(stderr, "%s\n", pixel.diagnostics.c_str());
            if (!layer_extract.diagnostics.empty()) std::fprintf(stderr, "%s\n", layer_extract.diagnostics.c_str());
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
        SetD3D12ObjectName(ui_root_signature_.Get(), L"UI.RootSignature", L"Canvas");

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
            SetD3D12ObjectName(ui_pipelines_[index].Get(), L"UI.PSO", L"Blend", index);
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC hdr_composite{};
        hdr_composite.pRootSignature = ui_root_signature_.Get();
        hdr_composite.VS = { ui_vertex_shader_.data(), ui_vertex_shader_.size() };
        hdr_composite.PS = { ui_pixel_shader_.data(), ui_pixel_shader_.size() };
        hdr_composite.InputLayout = { input_layout, static_cast<UINT>(std::size(input_layout)) };
        hdr_composite.BlendState = MakeUiBlend(D3D12UIBlendMode::Alpha);
        hdr_composite.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        hdr_composite.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        hdr_composite.RasterizerState.DepthClipEnable = TRUE;
        hdr_composite.DepthStencilState.DepthEnable = FALSE;
        hdr_composite.DepthStencilState.StencilEnable = FALSE;
        hdr_composite.SampleMask = UINT_MAX;
        hdr_composite.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        hdr_composite.NumRenderTargets = 1;
        hdr_composite.RTVFormats[0] = kSceneEffectRenderTargetFormat;
        hdr_composite.SampleDesc.Count = 1;
        const HRESULT hdr_composite_result = device_->CreateGraphicsPipelineState(
            &hdr_composite, IID_PPV_ARGS(&ui_hdr_composite_pipeline_));
        if (FAILED(hdr_composite_result))
        {
            std::fprintf(stderr,
                "[DX12] Scene Effect composite PSO creation failed: 0x%08lx\n",
                static_cast<unsigned long>(hdr_composite_result));
            ReleaseUIRendererResources();
            return false;
        }
        SetD3D12ObjectName(ui_hdr_composite_pipeline_.Get(),
            L"SceneEffect.PSO", L"Composite");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC background_composite = hdr_composite;
        background_composite.DepthStencilState.DepthEnable = TRUE;
        background_composite.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        background_composite.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        background_composite.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        background_composite.RTVFormats[0] = kUiRenderTargetFormat;
        const HRESULT background_result = device_->CreateGraphicsPipelineState(
            &background_composite, IID_PPV_ARGS(&ui_background_composite_pipeline_));
        if (FAILED(background_result))
        {
            std::fprintf(stderr,
                "[DX12] Background Screen Effect composite PSO creation failed: 0x%08lx\n",
                static_cast<unsigned long>(background_result));
            ReleaseUIRendererResources();
            return false;
        }
        SetD3D12ObjectName(ui_background_composite_pipeline_.Get(),
            L"ScreenEffect.PSO", L"BackgroundComposite");
        background_composite.RTVFormats[0] = kSceneEffectRenderTargetFormat;
        const HRESULT hdr_background_result = device_->CreateGraphicsPipelineState(
            &background_composite, IID_PPV_ARGS(&ui_hdr_background_composite_pipeline_));
        if (FAILED(hdr_background_result))
        {
            std::fprintf(stderr,
                "[DX12] HDR Background Screen Effect composite PSO creation failed: 0x%08lx\n",
                static_cast<unsigned long>(hdr_background_result));
            ReleaseUIRendererResources();
            return false;
        }
        SetD3D12ObjectName(ui_hdr_background_composite_pipeline_.Get(),
            L"ScreenEffect.PSO", L"HDRBackgroundComposite");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC layer_extract_pipeline = hdr_composite;
        layer_extract_pipeline.PS = { layer_extract.bytecode.data(), layer_extract.bytecode.size() };
        layer_extract_pipeline.BlendState = {};
        layer_extract_pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        layer_extract_pipeline.DepthStencilState.DepthEnable = FALSE;
        layer_extract_pipeline.DSVFormat = DXGI_FORMAT_UNKNOWN;
        layer_extract_pipeline.RTVFormats[0] = kUiRenderTargetFormat;
        const HRESULT layer_extract_result = device_->CreateGraphicsPipelineState(
            &layer_extract_pipeline, IID_PPV_ARGS(&ui_screen_layer_extract_pipeline_));
        if (FAILED(layer_extract_result))
        {
            std::fprintf(stderr,
                "[DX12] Screen Effect layer extract PSO creation failed: 0x%08lx\n",
                static_cast<unsigned long>(layer_extract_result));
            ReleaseUIRendererResources();
            return false;
        }
        SetD3D12ObjectName(ui_screen_layer_extract_pipeline_.Get(),
            L"ScreenEffect.PSO", L"LayerExtract");
        return true;
    }

    bool D3D12DeviceContext::CreateUIEffectResources() noexcept
    {
        if (device_ == nullptr || ui_vertex_shader_.empty()) return false;

        // UIEffectKindの保存順と同じ並びにする。全種類を個別PSOへ割り当て、
        // 未対応Effectを黙って素通しする状態を作らない。
        static constexpr const wchar_t* shader_files[]
        {
            L"ui_effect_blur.hlsl",
            L"ui_effect_glow.hlsl",
            L"ui_effect_color_adjust.hlsl",
            L"ui_effect_noise.hlsl",
            L"ui_effect_shake.hlsl",
            L"ui_effect_mask.hlsl",
            L"ui_effect_wipe.hlsl",
            L"ui_effect_dissolve.hlsl",
            L"ui_effect_distortion.hlsl",
            L"ui_effect_chromatic_aberration.hlsl",
            L"ui_effect_kuwahara.hlsl",
            L"ui_effect_halftone.hlsl",
            L"ui_effect_directional_blur.hlsl",
            L"ui_effect_radial_blur.hlsl",
            L"ui_effect_rotational_blur.hlsl",
            L"ui_effect_vignette.hlsl",
            L"ui_effect_light_streaks.hlsl",
            L"ui_effect_lens_distortion.hlsl",
            L"ui_effect_posterize.hlsl",
            L"ui_effect_threshold.hlsl",
            L"ui_effect_color_ramp.hlsl",
            L"ui_effect_levels.hlsl",
            L"ui_effect_temperature.hlsl",
            L"ui_effect_edge_detect.hlsl",
            L"ui_effect_outline.hlsl",
            L"ui_effect_long_shadow.hlsl",
            L"ui_effect_cross_hatch.hlsl",
            L"ui_effect_brush_stroke.hlsl",
            L"ui_effect_mosaic.hlsl",
            L"ui_effect_crystallize.hlsl",
            L"ui_effect_stained_glass.hlsl",
            L"ui_effect_twirl.hlsl",
            L"ui_effect_spherize.hlsl",
            L"ui_effect_ripple.hlsl",
            L"ui_effect_polar_coordinates.hlsl",
            L"ui_effect_scanlines.hlsl",
            L"ui_effect_crt.hlsl",
            L"ui_effect_glitch.hlsl",
            L"ui_effect_dither.hlsl",
            L"ui_effect_vhs.hlsl",
            L"ui_effect_letterbox.hlsl",
            L"ui_effect_waveform.hlsl",
            L"ui_effect_displacement_map.hlsl",
            L"ui_effect_turbulent_displace.hlsl",
            L"ui_effect_fractal_noise.hlsl",
            L"ui_effect_motion_blur.hlsl",
            L"ui_effect_echo.hlsl",
            L"ui_effect_drop_shadow.hlsl",
            L"ui_effect_inner_shadow.hlsl",
            L"ui_effect_lut.hlsl",
            L"ui_effect_tone_curve.hlsl",
            L"ui_effect_matte_composite.hlsl",
            L"ui_effect_matte_morphology.hlsl",
            L"ui_effect_bevel_emboss.hlsl",
            L"ui_effect_kaleidoscope.hlsl",
            L"ui_effect_page_curl.hlsl",
            L"ui_effect_ascii_led_matrix.hlsl",
            L"ui_effect_feedback_zoom.hlsl",
            L"ui_effect_liquid_glass.hlsl",
            L"ui_effect_light_sweep.hlsl",
            L"ui_effect_shockwave.hlsl",
            L"ui_effect_pixel_sort.hlsl",
            L"ui_effect_hologram.hlsl",
            L"ui_effect_iridescent_foil.hlsl",
            L"ui_effect_radar_sweep.hlsl",
            L"ui_effect_energy_pulse.hlsl",
            L"ui_effect_circuit_flow.hlsl",
            L"ui_effect_heat_haze.hlsl",
            L"ui_effect_water_caustics.hlsl",
            L"ui_effect_voronoi_shatter.hlsl",
            L"ui_effect_ink_bleed.hlsl",
            L"ui_effect_burn_reveal.hlsl",
            L"ui_effect_portal_vortex.hlsl",
            L"ui_effect_frost_crack.hlsl"
        };
        static_assert(std::size(shader_files) == UIEffectKindCount,
            "UIEffectKind and DX12 UI Effect shader table must remain one-to-one.");

        D3D12ShaderCompiler compiler;
        if (!compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath())) return false;
        const std::filesystem::path shader_directory =
            std::filesystem::current_path() / "Shader";
        const auto effect_vertex = compiler.CompileFile(
            shader_directory / L"dx12_ui_effect_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        if (!effect_vertex.succeeded)
        {
            if (!effect_vertex.diagnostics.empty())
            {
                OutputDebugStringA(effect_vertex.diagnostics.c_str());
                std::fprintf(stderr, "%s\n", effect_vertex.diagnostics.c_str());
            }
            compiler.Shutdown();
            ReleaseUIEffectResources();
            return false;
        }
        ui_effect_vertex_shader_ = effect_vertex.bytecode;
        std::array<std::vector<std::uint8_t>, UIEffectKindCount> pixel_bytecodes;
        for (std::size_t index = 0; index < std::size(shader_files); ++index)
        {
            const auto pixel = compiler.CompileFile(shader_directory / shader_files[index],
                L"main", L"ps_6_0", debug_layer_enabled_);
            if (!pixel.succeeded)
            {
                std::fwprintf(stderr,
                    L"[DX12] UI Effect shader compilation failed: %ls (0x%08lx)\n",
                    shader_files[index], static_cast<unsigned long>(pixel.status));
                if (!pixel.diagnostics.empty())
                {
                    OutputDebugStringA(pixel.diagnostics.c_str());
                    std::fprintf(stderr, "%s\n", pixel.diagnostics.c_str());
                }
                compiler.Shutdown();
                ReleaseUIEffectResources();
                return false;
            }
            pixel_bytecodes[index] = pixel.bytecode;
        }
        const auto region_pixel = compiler.CompileFile(
            shader_directory / L"ui_effect_region_blend.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        compiler.Shutdown();
        if (!region_pixel.succeeded || region_pixel.bytecode.empty())
        {
            if (!region_pixel.diagnostics.empty())
            {
                OutputDebugStringA(region_pixel.diagnostics.c_str());
                std::fprintf(stderr, "%s\n", region_pixel.diagnostics.c_str());
            }
            ReleaseUIEffectResources();
            return false;
        }

        // 通常Effectはt0/t1、適用範囲の合成はさらに元画像t2を読む。
        D3D12_DESCRIPTOR_RANGE texture_ranges[3]{};
        for (std::size_t index = 0; index < std::size(texture_ranges); ++index)
        {
            texture_ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            texture_ranges[index].NumDescriptors = 1;
            texture_ranges[index].BaseShaderRegister = static_cast<UINT>(index);
        }
        D3D12_ROOT_PARAMETER parameters[5]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].Descriptor.ShaderRegister = 0;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[1].Descriptor.ShaderRegister = 1;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        for (std::size_t index = 0; index < std::size(texture_ranges); ++index)
        {
            parameters[index + 2].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[index + 2].DescriptorTable.NumDescriptorRanges = 1;
            parameters[index + 2].DescriptorTable.pDescriptorRanges =
                &texture_ranges[index];
            parameters[index + 2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
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
        if (!SerializeUiRootSignature(device_.Get(), root_desc,
            ui_effect_root_signature_))
            return false;
        SetD3D12ObjectName(ui_effect_root_signature_.Get(), L"UI.Effect.RootSignature", L"Effect");

        const D3D12_INPUT_ELEMENT_DESC input_layout[]
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

        for (std::size_t index = 0; index < ui_effect_pipelines_.size(); ++index)
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
            pipeline.pRootSignature = ui_effect_root_signature_.Get();
            pipeline.VS = { effect_vertex.bytecode.data(), effect_vertex.bytecode.size() };
            pipeline.PS = { pixel_bytecodes[index].data(), pixel_bytecodes[index].size() };
            pipeline.InputLayout = {
                input_layout, static_cast<UINT>(std::size(input_layout)) };
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
            const HRESULT result = device_->CreateGraphicsPipelineState(
                &pipeline, IID_PPV_ARGS(&ui_effect_pipelines_[index]));
            if (FAILED(result))
            {
                std::fprintf(stderr,
                    "[DX12] UI Effect PSO[%zu] creation failed: 0x%08lx\n",
                    index, static_cast<unsigned long>(result));
                ReleaseUIEffectResources();
                return false;
            }
            SetD3D12ObjectName(ui_effect_pipelines_[index].Get(), L"UI.Effect.PSO", L"Kind", index);
            pipeline.RTVFormats[0] = kSceneEffectRenderTargetFormat;
            const HRESULT hdr_result = device_->CreateGraphicsPipelineState(
                &pipeline, IID_PPV_ARGS(&ui_effect_hdr_pipelines_[index]));
            if (FAILED(hdr_result))
            {
                std::fprintf(stderr,
                    "[DX12] Scene Effect PSO[%zu] creation failed: 0x%08lx\n",
                    index, static_cast<unsigned long>(hdr_result));
                ReleaseUIEffectResources();
                return false;
            }
            SetD3D12ObjectName(ui_effect_hdr_pipelines_[index].Get(),
                L"SceneEffect.PSO", L"Kind", index);
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC region_pipeline{};
        region_pipeline.pRootSignature = ui_effect_root_signature_.Get();
        region_pipeline.VS = { effect_vertex.bytecode.data(), effect_vertex.bytecode.size() };
        region_pipeline.PS = {
            region_pixel.bytecode.data(), region_pixel.bytecode.size() };
        region_pipeline.InputLayout = {
            input_layout, static_cast<UINT>(std::size(input_layout)) };
        region_pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        region_pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        region_pipeline.RasterizerState.DepthClipEnable = TRUE;
        region_pipeline.DepthStencilState.DepthEnable = FALSE;
        region_pipeline.DepthStencilState.StencilEnable = FALSE;
        region_pipeline.BlendState.RenderTarget[0].BlendEnable = FALSE;
        region_pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        region_pipeline.SampleMask = UINT_MAX;
        region_pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        region_pipeline.NumRenderTargets = 1;
        region_pipeline.RTVFormats[0] = kUiRenderTargetFormat;
        region_pipeline.SampleDesc.Count = 1;
        const HRESULT region_result = device_->CreateGraphicsPipelineState(
            &region_pipeline, IID_PPV_ARGS(&ui_effect_region_pipeline_));
        if (FAILED(region_result))
        {
            std::fprintf(stderr,
                "[DX12] UI Effect region PSO creation failed: 0x%08lx\n",
                static_cast<unsigned long>(region_result));
            ReleaseUIEffectResources();
            return false;
        }
        SetD3D12ObjectName(ui_effect_region_pipeline_.Get(), L"UI.Effect.PSO", L"RegionBlend");
        region_pipeline.RTVFormats[0] = kSceneEffectRenderTargetFormat;
        const HRESULT hdr_region_result = device_->CreateGraphicsPipelineState(
            &region_pipeline, IID_PPV_ARGS(&ui_effect_region_hdr_pipeline_));
        if (FAILED(hdr_region_result))
        {
            std::fprintf(stderr,
                "[DX12] Scene Effect region PSO creation failed: 0x%08lx\n",
                static_cast<unsigned long>(hdr_region_result));
            ReleaseUIEffectResources();
            return false;
        }
        SetD3D12ObjectName(ui_effect_region_hdr_pipeline_.Get(),
            L"SceneEffect.PSO", L"RegionBlend");
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
        ui_hdr_composite_pipeline_.Reset();
        ui_background_composite_pipeline_.Reset();
        ui_hdr_background_composite_pipeline_.Reset();
        ui_screen_layer_extract_pipeline_.Reset();
        ui_root_signature_.Reset();
        ui_vertex_shader_.clear();
        ui_pixel_shader_.clear();
    }

    bool D3D12DeviceContext::CreateCustomUIEffectPipelines(
        const D3D12UICustomEffectShaderSource& source,
        Microsoft::WRL::ComPtr<ID3D12PipelineState>& ldr,
        Microsoft::WRL::ComPtr<ID3D12PipelineState>& hdr,
        std::string& diagnostics) noexcept
    {
        if (device_ == nullptr || ui_effect_root_signature_ == nullptr ||
            ui_effect_vertex_shader_.empty() || source.source_path.empty())
        {
            diagnostics = "UI Effect renderer is not ready";
            return false;
        }
        std::ifstream file(source.source_path, std::ios::binary);
        if (!file)
        {
            diagnostics = "Shader source file could not be opened: " +
                source.source_path.generic_u8string();
            return false;
        }
        std::string shader_source((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        if (shader_source.size() >= 3 &&
            static_cast<unsigned char>(shader_source[0]) == 0xEF &&
            static_cast<unsigned char>(shader_source[1]) == 0xBB &&
            static_cast<unsigned char>(shader_source[2]) == 0xBF)
            shader_source.erase(0, 3);
        if (shader_source.empty())
        {
            diagnostics = "Shader source file is empty: " + source.source_path.generic_u8string();
            return false;
        }

        D3D12ShaderCompiler compiler;
        if (!compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath()))
        {
            diagnostics = "DXC compiler initialization failed";
            return false;
        }
        const std::string combined = "#line 1 \"REPLAY_GENERATED\"\n" +
            source.generated_declaration + "#line 1 \"" +
            source.source_path.generic_u8string() + "\"\n" + shader_source;
        const auto pixel = compiler.CompileSource(combined, source.source_path,
            L"main", L"ps_6_0", debug_layer_enabled_);
        compiler.Shutdown();
        diagnostics = pixel.diagnostics;
        if (!pixel.succeeded || pixel.bytecode.empty())
        {
            if (diagnostics.empty()) diagnostics = "DXC compilation failed";
            return false;
        }

        const D3D12_INPUT_ELEMENT_DESC input_layout[]
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
        pipeline.VS = { ui_effect_vertex_shader_.data(), ui_effect_vertex_shader_.size() };
        pipeline.PS = { pixel.bytecode.data(), pixel.bytecode.size() };
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
        HRESULT result = device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&ldr));
        if (FAILED(result))
        {
            diagnostics = "Custom UI Effect LDR PSO creation failed: 0x" +
                std::to_string(static_cast<unsigned long>(result));
            return false;
        }
        pipeline.RTVFormats[0] = kSceneEffectRenderTargetFormat;
        result = device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&hdr));
        if (FAILED(result))
        {
            ldr.Reset();
            diagnostics = "Custom UI Effect HDR PSO creation failed: 0x" +
                std::to_string(static_cast<unsigned long>(result));
            return false;
        }
        return true;
    }

    bool D3D12DeviceContext::PrepareCustomUIEffectShader(
        const D3D12UICustomEffectShaderSource& source) noexcept
    {
        if (source.guid.empty()) return true;
        if (custom_ui_effect_pipelines_.find(source.guid) != custom_ui_effect_pipelines_.end())
            return true;
        if (custom_ui_effect_shader_diagnostics_.find(source.guid) !=
            custom_ui_effect_shader_diagnostics_.end()) return false;
        CustomUIEffectPipelines pipelines;
        std::string diagnostics;
        if (!CreateCustomUIEffectPipelines(source, pipelines.ldr, pipelines.hdr, diagnostics))
        {
            try { custom_ui_effect_shader_diagnostics_.emplace(source.guid, std::move(diagnostics)); }
            catch (...) {}
            return false;
        }
        try { custom_ui_effect_pipelines_.emplace(source.guid, std::move(pipelines)); }
        catch (...) { return false; }
        return true;
    }

    const std::string* D3D12DeviceContext::CustomUIEffectShaderDiagnostic(
        const std::string& guid) const noexcept
    {
        const auto found = custom_ui_effect_shader_diagnostics_.find(guid);
        return found == custom_ui_effect_shader_diagnostics_.end() ? nullptr : &found->second;
    }

    void D3D12DeviceContext::ReleaseUIEffectResources() noexcept
    {
        for (auto& pipeline : ui_effect_pipelines_) pipeline.Reset();
        for (auto& pipeline : ui_effect_hdr_pipelines_) pipeline.Reset();
        ui_effect_region_pipeline_.Reset();
        ui_effect_region_hdr_pipeline_.Reset();
        custom_ui_effect_pipelines_.clear();
        custom_ui_effect_shader_diagnostics_.clear();
        ui_effect_root_signature_.Reset();
        ui_effect_vertex_shader_.clear();
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
        const DXGI_FORMAT effect_format = effect_targets[0].color != nullptr
            ? effect_targets[0].color->GetDesc().Format : kUiRenderTargetFormat;
        const bool hdr_effect = effect_format == kSceneEffectRenderTargetFormat;
        if (frame.batches.empty() && frame.effects.empty() &&
            frame.effect_groups.empty())
        {
            if (output_target != nullptr)
            {
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    output_target->color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
                    return false;
                const float clear[4]{ frame.clear_color.x, frame.clear_color.y,
                    frame.clear_color.z, frame.clear_color.w };
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
            // バッチ固有の clip を次の UI サブパスへ持ち越さない。
            command_list_->RSSetScissorRects(1, &full_scissor);
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
            D3D12_CPU_DESCRIPTOR_HANDLE rtv,
            const D3D12_RECT* composite_scissor = nullptr) noexcept
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
            ID3D12PipelineState* composite_pipeline = nullptr;
            if (frame.background_only)
                composite_pipeline = hdr_effect
                    ? ui_hdr_background_composite_pipeline_.Get()
                    : ui_background_composite_pipeline_.Get();
            else
                composite_pipeline = hdr_effect
                    ? ui_hdr_composite_pipeline_.Get()
                    : ui_pipelines_[static_cast<std::size_t>(D3D12UIBlendMode::Alpha)].Get();
            if (composite_pipeline == nullptr) return false;
            command_list_->SetPipelineState(composite_pipeline);
            if (frame.background_only)
            {
                if (!scene3d_depth_.resource || !resource_state_tracker_.Transition(
                    command_list_.Get(), scene3d_depth_.resource.Get(), D3D12_RESOURCE_STATE_DEPTH_READ))
                    return false;
                command_list_->OMSetRenderTargets(1, &rtv, FALSE, &scene3d_depth_.dsv.cpu);
            }
            else
            {
                command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            }
            command_list_->RSSetViewports(1, &viewport);
            command_list_->RSSetScissorRects(1,
                composite_scissor != nullptr ? composite_scissor : &full_scissor);
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
            // グループ固有の composite clip を後続の UI 描画へ持ち越さない。
            command_list_->RSSetScissorRects(1, &full_scissor);
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
            if (!frame.preserve_output)
            {
                const float clear[4]{ frame.clear_color.x, frame.clear_color.y,
                    frame.clear_color.z, frame.clear_color.w };
                command_list_->ClearRenderTargetView(output_target->rtv.cpu, clear, 0, nullptr);
            }
        }

        if ((!frame.requires_offscreen && !frame.capture_backdrop && frame.effect_groups.empty()) ||
            !effect_targets[0].IsValid() ||
            (hdr_effect ? ui_effect_hdr_pipelines_[0] == nullptr
                : ui_effect_pipelines_[0] == nullptr))
        {
            return draw_batches(output_rtv, -1, 0, frame.batches.size()) && finish_output();
        }

        const bool uses_scene_effect_history = hdr_effect || frame.scene_effect_history;
        auto& history_targets = uses_scene_effect_history
            ? scene_effect_history_targets_
            : (output_target != nullptr
                ? ui_preview_effect_history_targets_ : ui_effect_history_targets_);
        const auto apply_effects = [&](D3D12OffscreenTarget*& source_target,
            const std::vector<D3D12UIEffectCommand>& effects,
            bool capture_backdrop, D3D12OffscreenTarget* backdrop_target) noexcept
        {
            UIEffectHistoryEntry* history = nullptr;
            std::uint64_t history_map_key = 0;
            for (const D3D12UIEffectCommand& effect : effects)
            {
                if (effect.temporal && effect.history_key != 0)
                {
                    history_map_key = effect.history_key ^
                        (static_cast<std::uint64_t>(frame.target_width) *
                            0x9E3779B185EBCA87ull) ^
                        (static_cast<std::uint64_t>(frame.target_height) *
                            0xC2B2AE3D27D4EB4Full);
                    break;
                }
            }
            if (history_map_key != 0)
            {
                try
                {
                    auto [found, inserted] = history_targets.try_emplace(history_map_key);
                    history = &found->second;
                    if (!history->target.IsValid() ||
                        history->target.width != frame.target_width ||
                        history->target.height != frame.target_height)
                    {
                        if (!CreateOffscreenTarget(history->target,
                            frame.target_width, frame.target_height,
                            effect_format))
                        {
                            history_targets.erase(found);
                            return false;
                        }
                        history->valid = false;
                    }
                }
                catch (...)
                {
                    return false;
                }
            }
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
                ID3D12PipelineState* effect_pipeline = nullptr;
                if (!effect.custom_shader.empty())
                {
                    const auto custom = custom_ui_effect_pipelines_.find(effect.custom_shader);
                    if (custom == custom_ui_effect_pipelines_.end()) continue;
                    effect_pipeline = hdr_effect ? custom->second.hdr.Get() : custom->second.ldr.Get();
                }
                else
                {
                    if (effect.kind >= ui_effect_pipelines_.size()) return false;
                    effect_pipeline = hdr_effect ? ui_effect_hdr_pipelines_[effect.kind].Get()
                        : ui_effect_pipelines_[effect.kind].Get();
                }
                if (effect_pipeline == nullptr) return false;
                command_list_->SetPipelineState(effect_pipeline);

                // 移行前EffectChainと同じレジスタ順・同じ値で定数を渡す。
                D3D12UIEffectConstants constants{};
                constants.effect_color = effect.color;
                constants.effect_params0 = { effect.radius, effect.intensity,
                    effect.threshold, effect.amount };
                constants.effect_params1 = { effect.angle, effect.progress,
                    effect.softness, effect.speed };
                constants.effect_params2 = { effect.direction.x, effect.direction.y,
                    effect.seed, effect.time };
                constants.target_size = { static_cast<float>(frame.target_width),
                    static_cast<float>(frame.target_height),
                    1.0f / static_cast<float>(frame.target_width),
                    1.0f / static_cast<float>(frame.target_height) };
                constants.effect_color_2 = effect.color_2;
                constants.effect_color_3 = effect.color_3;
                constants.effect_color_4 = effect.color_4;
                constants.effect_color_stops = effect.color_stops;
                constants.effect_params3.x = static_cast<float>(effect.waveform);
                constants.effect_params3.z = effect.brush_atlas ? 1.0f : 0.0f;
                constants.brush_pattern_settings = effect.brush_pattern_settings;
                constants.brush_pattern_weights = effect.brush_pattern_weights;
                constants.effect_region_params = effect.effect_region_params;
                constants.effect_region_settings = effect.effect_region_settings;
                constants.effect_region_extra_params = effect.effect_region_extra_params;
                constants.effect_region_extra_settings = effect.effect_region_extra_settings;
                constants.effect_region_count = effect.effect_region_count;
                constants.effect_region_path_counts = effect.effect_region_path_counts;
                constants.effect_region_path_points = effect.effect_region_path_points;
                // Mask Effectは移行前と同じcenter/sizeの既定補正を使う。
                if (effect.kind == 5)
                {
                    const float center_x = effect.direction.x > 0.0f && effect.direction.x < 1.0f
                        ? effect.direction.x : 0.5f;
                    const float center_y = effect.direction.y > 0.0f && effect.direction.y < 1.0f
                        ? effect.direction.y : 0.5f;
                    const float half_width = effect.seed > 0.0f && effect.seed < 1.0f
                        ? effect.seed : 0.5f;
                    const float half_height = effect.speed > 0.0f && effect.speed < 1.0f
                        ? effect.speed : 0.5f;
                    constants.effect_params2 = {
                        center_x, center_y, half_width, half_height };
                    constants.effect_params1.w =
                        effect.auxiliary_texture_key.empty() ? 0.0f : 1.0f;
                }
                D3D12_GPU_DESCRIPTOR_HANDLE auxiliary = backdrop_target != nullptr
                    ? backdrop_target->srv.gpu : source_target->srv.gpu;
                bool auxiliary_valid = false;
                if (effect.temporal && history != nullptr && history->valid)
                {
                    auxiliary = history->target.srv.gpu;
                    auxiliary_valid = true;
                }
                else if (!effect.auxiliary_texture_key.empty())
                {
                    const auto found = texture_cache_.find(effect.auxiliary_texture_key);
                    if (found != texture_cache_.end())
                    {
                        auxiliary = found->second.srv.gpu;
                        auxiliary_valid = true;
                    }
                }
                constants.effect_params3.y = auxiliary_valid ? 1.0f : 0.0f;
                if (effect.kind == 5)
                    constants.effect_params1.w = auxiliary_valid ? 1.0f : 0.0f;
                const auto vertices = make_fullscreen_vertices();
                D3D12UploadAllocation vertex_upload{};
                D3D12UploadAllocation constants_upload{};
                D3D12UploadAllocation custom_constants_upload{};
                const DirectX::XMFLOAT4 empty_custom_constants{};
                const void* custom_constants = effect.custom_constants.empty()
                    ? static_cast<const void*>(&empty_custom_constants)
                    : static_cast<const void*>(effect.custom_constants.data());
                const std::uint64_t custom_constants_size = effect.custom_constants.empty()
                    ? sizeof(empty_custom_constants) : effect.custom_constants.size();
                if (!allocator.Allocate(sizeof(vertices), 16, vertex_upload) ||
                    !allocator.Allocate(sizeof(constants),
                        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants_upload) ||
                    !allocator.Allocate(custom_constants_size,
                        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, custom_constants_upload))
                    return false;
                std::memcpy(vertex_upload.cpu, vertices.data(), sizeof(vertices));
                std::memcpy(constants_upload.cpu, &constants, sizeof(constants));
                std::memcpy(custom_constants_upload.cpu, custom_constants,
                    static_cast<std::size_t>(custom_constants_size));
                command_list_->SetGraphicsRootConstantBufferView(0, constants_upload.gpu);
                command_list_->SetGraphicsRootConstantBufferView(1, custom_constants_upload.gpu);
                command_list_->SetGraphicsRootDescriptorTable(2, source_target->srv.gpu);
                command_list_->SetGraphicsRootDescriptorTable(3, auxiliary);
                // 通常Effectでは未使用。Region passとRoot Signatureを共通化するため
                // 常に有効な元画像をt2へ結ぶ。
                command_list_->SetGraphicsRootDescriptorTable(4, source_target->srv.gpu);
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
                if (effect.region_enabled)
                {
                    ID3D12PipelineState* region_pipeline = hdr_effect
                        ? ui_effect_region_hdr_pipeline_.Get()
                        : ui_effect_region_pipeline_.Get();
                    if (region_pipeline == nullptr) return false;
                    D3D12OffscreenTarget* region_destination = nullptr;
                    for (std::size_t target_index = 0; target_index < 3; ++target_index)
                    {
                        D3D12OffscreenTarget* candidate = &effect_targets[target_index];
                        if (candidate != source_target && candidate != destination)
                        {
                            region_destination = candidate;
                            break;
                        }
                    }
                    if (region_destination == nullptr || !region_destination->IsValid() ||
                        !resource_state_tracker_.Transition(command_list_.Get(),
                            region_destination->color.Get(),
                            D3D12_RESOURCE_STATE_RENDER_TARGET))
                        return false;
                    command_list_->OMSetRenderTargets(1,
                        &region_destination->rtv.cpu, FALSE, nullptr);
                    command_list_->SetPipelineState(region_pipeline);
                    command_list_->SetGraphicsRootConstantBufferView(0,
                        constants_upload.gpu);
                    command_list_->SetGraphicsRootDescriptorTable(2,
                        destination->srv.gpu);
                    D3D12_GPU_DESCRIPTOR_HANDLE region_mask = source_target->srv.gpu;
                    if (!effect.region_mask_texture_key.empty())
                    {
                        const auto found = texture_cache_.find(
                            effect.region_mask_texture_key);
                        if (found != texture_cache_.end())
                            region_mask = found->second.srv.gpu;
                    }
                    command_list_->SetGraphicsRootDescriptorTable(3, region_mask);
                    command_list_->SetGraphicsRootDescriptorTable(4,
                        source_target->srv.gpu);
                    command_list_->IASetPrimitiveTopology(
                        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    command_list_->IASetVertexBuffers(0, 1, &view);
                    command_list_->DrawInstanced(
                        static_cast<UINT>(vertices.size()), 1, 0, 0);
                    if (!resource_state_tracker_.Transition(command_list_.Get(),
                        region_destination->color.Get(),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                        return false;
                    source_target = region_destination;
                }
                else
                {
                    source_target = destination;
                }
            }
            if (history != nullptr)
            {
                // 移行前と同じくStack全体の最終結果を次回描画の履歴にする。
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    source_target->color.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                    !resource_state_tracker_.Transition(command_list_.Get(),
                        history->target.color.Get(), D3D12_RESOURCE_STATE_COPY_DEST))
                    return false;
                command_list_->CopyResource(history->target.color.Get(),
                    source_target->color.Get());
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    source_target->color.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
                    !resource_state_tracker_.Transition(command_list_.Get(),
                        history->target.color.Get(),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                    return false;
                history->valid = true;
                if (uses_scene_effect_history) ++scene_effect_history_write_serial_;
            }
            return true;
        };

        if (!frame.effect_groups.empty())
        {
            const auto capture_current_backdrop = [&](D3D12OffscreenTarget*& target) noexcept
            {
                if (!effect_targets[3].IsValid()) return false;
                ID3D12Resource* backdrop_source = output_target != nullptr
                    ? output_target->color.Get() : render_targets_[frame_index_].Get();
                if (!resource_state_tracker_.Transition(command_list_.Get(), backdrop_source,
                    D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                    !resource_state_tracker_.Transition(command_list_.Get(),
                        effect_targets[3].color.Get(), D3D12_RESOURCE_STATE_COPY_DEST))
                    return false;
                command_list_->CopyResource(effect_targets[3].color.Get(), backdrop_source);
                if (!resource_state_tracker_.Transition(command_list_.Get(), backdrop_source,
                    D3D12_RESOURCE_STATE_RENDER_TARGET) ||
                    !resource_state_tracker_.Transition(command_list_.Get(),
                        effect_targets[3].color.Get(),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                    return false;
                target = &effect_targets[3];
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
                    // 不正または非連続なGroup情報でもフレーム処理を停止させない。
                    ++batch_index;
                    continue;
                }

                D3D12OffscreenTarget* group_backdrop = nullptr;
                if (group.capture_backdrop && !capture_current_backdrop(group_backdrop))
                    return false;
                D3D12OffscreenTarget* source_target = &effect_targets[0];
                if (group_backdrop != nullptr)
                {
                    // 移行前は「背景を捕捉したRTへ対象UIを重ねてからEffect」を掛ける。
                    // t1へ背景を結ぶだけではBlurなどt0専用Effectに背景が届かない。
                    if (!resource_state_tracker_.Transition(command_list_.Get(),
                        group_backdrop->color.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                        !resource_state_tracker_.Transition(command_list_.Get(),
                            source_target->color.Get(), D3D12_RESOURCE_STATE_COPY_DEST))
                        return false;
                    command_list_->CopyResource(source_target->color.Get(),
                        group_backdrop->color.Get());
                    if (!resource_state_tracker_.Transition(command_list_.Get(),
                        group_backdrop->color.Get(),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
                        !resource_state_tracker_.Transition(command_list_.Get(),
                            source_target->color.Get(),
                            D3D12_RESOURCE_STATE_RENDER_TARGET))
                        return false;
                }
                else
                {
                    if (!resource_state_tracker_.Transition(command_list_.Get(),
                        source_target->color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
                        return false;
                    const float clear[4]{ 0, 0, 0, 0 };
                    command_list_->ClearRenderTargetView(source_target->rtv.cpu,
                        clear, 0, nullptr);
                }
                if (!draw_batches(source_target->rtv.cpu,
                    static_cast<std::int32_t>(group_index), group_first, group_end))
                    return false;
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    source_target->color.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                    return false;
                if (!apply_effects(source_target, group.effects,
                    group.capture_backdrop, group_backdrop)) return false;
                const D3D12_RECT* group_scissor = group.composite_scissor_enabled
                    ? &group.composite_scissor : nullptr;
                if (!draw_composite(source_target->srv.gpu, output_rtv,
                    group_scissor))
                    return false;
                batch_index = group_end;
            }
            return finish_output();
        }

        D3D12OffscreenTarget* backdrop_target = nullptr;
        if (frame.capture_backdrop && effect_targets[3].IsValid())
        {
            ID3D12Resource* backdrop_source = output_target != nullptr
                ? output_target->color.Get() : render_targets_[frame_index_].Get();
            if (!resource_state_tracker_.Transition(command_list_.Get(), backdrop_source,
                D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                !resource_state_tracker_.Transition(command_list_.Get(),
                    effect_targets[3].color.Get(), D3D12_RESOURCE_STATE_COPY_DEST))
                return false;
            command_list_->CopyResource(effect_targets[3].color.Get(), backdrop_source);
            if (!resource_state_tracker_.Transition(command_list_.Get(), backdrop_source,
                D3D12_RESOURCE_STATE_RENDER_TARGET) ||
                !resource_state_tracker_.Transition(command_list_.Get(),
                    effect_targets[3].color.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
            backdrop_target = &effect_targets[3];
        }

        D3D12OffscreenTarget* source_target = &effect_targets[0];
        if (backdrop_target != nullptr)
        {
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                backdrop_target->color.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                !resource_state_tracker_.Transition(command_list_.Get(),
                    source_target->color.Get(), D3D12_RESOURCE_STATE_COPY_DEST))
                return false;
            command_list_->CopyResource(source_target->color.Get(),
                backdrop_target->color.Get());
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                backdrop_target->color.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
                !resource_state_tracker_.Transition(command_list_.Get(),
                    source_target->color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
                return false;
        }
        else
        {
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                source_target->color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
                return false;
            const float clear[4]{ 0, 0, 0, 0 };
            command_list_->ClearRenderTargetView(source_target->rtv.cpu,
                clear, 0, nullptr);
        }
        if (!draw_batches(source_target->rtv.cpu, -2, 0, frame.batches.size())) return false;
        if (!resource_state_tracker_.Transition(command_list_.Get(), source_target->color.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) return false;

        if (!apply_effects(source_target, frame.effects, frame.capture_backdrop,
            backdrop_target)) return false;

        const bool output_is_effect_target = output_target != nullptr &&
            (output_target == &effect_targets[0] || output_target == &effect_targets[1] ||
                output_target == &effect_targets[2] || output_target == &effect_targets[3]);
        if (output_is_effect_target)
        {
            if (source_target != output_target)
            {
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    source_target->color.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                    !resource_state_tracker_.Transition(command_list_.Get(),
                        output_target->color.Get(), D3D12_RESOURCE_STATE_COPY_DEST))
                    return false;
                command_list_->CopyResource(output_target->color.Get(),
                    source_target->color.Get());
                if (!resource_state_tracker_.Transition(command_list_.Get(),
                    source_target->color.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
                    !resource_state_tracker_.Transition(command_list_.Get(),
                        output_target->color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
                    return false;
            }
            return finish_output();
        }
        return draw_composite(source_target->srv.gpu, output_rtv) && finish_output();
    }

    bool D3D12DeviceContext::BuildRenderingLayerLdrSource(
        const D3D12OffscreenTarget& layer_source, D3D12OffscreenTarget& ldr_output) noexcept
    {
        if (!frame_open_ || !layer_source.IsValid() || !ldr_output.IsValid() ||
            !ui_effect_targets_[3].IsValid() || ui_root_signature_ == nullptr ||
            ui_screen_layer_extract_pipeline_ == nullptr)
            return false;
        if (layer_source.width != width_ || layer_source.height != height_ ||
            ldr_output.width != width_ || ldr_output.height != height_)
            return false;
        ID3D12Resource* current = render_targets_[frame_index_].Get();
        if (!resource_state_tracker_.Transition(command_list_.Get(), current,
            D3D12_RESOURCE_STATE_COPY_SOURCE) ||
            !resource_state_tracker_.Transition(command_list_.Get(), ui_effect_targets_[3].color.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST))
            return false;
        command_list_->CopyResource(ui_effect_targets_[3].color.Get(), current);
        if (!resource_state_tracker_.Transition(command_list_.Get(), current,
            D3D12_RESOURCE_STATE_RENDER_TARGET) ||
            !resource_state_tracker_.Transition(command_list_.Get(), ui_effect_targets_[3].color.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
            !resource_state_tracker_.Transition(command_list_.Get(), layer_source.color.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
            !resource_state_tracker_.Transition(command_list_.Get(), ldr_output.color.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET))
            return false;
        const auto white = texture_cache_.find("__dx12_white");
        if (white == texture_cache_.end()) return false;
        const float width = static_cast<float>(width_);
        const float height = static_cast<float>(height_);
        const std::array<D3D12UIVertex, 6> vertices =
        {
            D3D12UIVertex{{0, 0}, {0, 0}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{0, height}, {0, 1}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{width, height}, {1, 1}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{0, 0}, {0, 0}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{width, height}, {1, 1}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{width, 0}, {1, 0}, {1, 1, 1, 1}, {0, 0, 1, 1}},
        };
        D3D12UIVisualConstants constants{};
        constants.screen_size = { width, height, 0, 0 };
        D3D12UploadAllocation vertex_upload{};
        D3D12UploadAllocation constants_upload{};
        D3D12LinearUploadAllocator& allocator = frame_resources_[frame_index_].upload_allocator;
        if (!allocator.Allocate(sizeof(vertices), 16, vertex_upload) ||
            !allocator.Allocate(sizeof(constants),
                D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants_upload))
            return false;
        std::memcpy(vertex_upload.cpu, vertices.data(), sizeof(vertices));
        std::memcpy(constants_upload.cpu, &constants, sizeof(constants));
        ID3D12DescriptorHeap* heaps[] =
        {
            resource_descriptor_allocator_.Heap(), sampler_descriptor_allocator_.Heap()
        };
        command_list_->SetDescriptorHeaps(static_cast<UINT>(std::size(heaps)), heaps);
        const D3D12_VIEWPORT viewport{ 0, 0, width, height, 0, 1 };
        const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
        const float clear[4]{ 0, 0, 0, 0 };
        command_list_->ClearRenderTargetView(ldr_output.rtv.cpu, clear, 0, nullptr);
        command_list_->SetGraphicsRootSignature(ui_root_signature_.Get());
        command_list_->SetPipelineState(ui_screen_layer_extract_pipeline_.Get());
        command_list_->OMSetRenderTargets(1, &ldr_output.rtv.cpu, FALSE, nullptr);
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, &scissor);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->SetGraphicsRootConstantBufferView(0, constants_upload.gpu);
        command_list_->SetGraphicsRootDescriptorTable(1, ui_effect_targets_[3].srv.gpu);
        command_list_->SetGraphicsRootDescriptorTable(2, layer_source.srv.gpu);
        command_list_->SetGraphicsRootDescriptorTable(3, white->second.srv.gpu);
        command_list_->SetGraphicsRootDescriptorTable(4, white->second.srv.gpu);
        command_list_->SetGraphicsRootDescriptorTable(5, white->second.srv.gpu);
        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = vertex_upload.gpu;
        view.SizeInBytes = sizeof(vertices);
        view.StrideInBytes = sizeof(D3D12UIVertex);
        command_list_->IASetVertexBuffers(0, 1, &view);
        command_list_->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
        return resource_state_tracker_.Transition(command_list_.Get(), ldr_output.color.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    bool D3D12DeviceContext::CompositeUIEffectTargetToCurrent(
        const D3D12OffscreenTarget& source) noexcept
    {
        if (!frame_open_ || !source.IsValid() || ui_root_signature_ == nullptr)
            return false;
        const auto white = texture_cache_.find("__dx12_white");
        if (white == texture_cache_.end()) return false;
        if (!resource_state_tracker_.Transition(command_list_.Get(), source.color.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
            !TransitionCurrentRenderTarget(D3D12_RESOURCE_STATE_RENDER_TARGET))
            return false;
        const float width = static_cast<float>(width_);
        const float height = static_cast<float>(height_);
        const std::array<D3D12UIVertex, 6> vertices =
        {
            D3D12UIVertex{{0, 0}, {0, 0}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{0, height}, {0, 1}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{width, height}, {1, 1}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{0, 0}, {0, 0}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{width, height}, {1, 1}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{width, 0}, {1, 0}, {1, 1, 1, 1}, {0, 0, 1, 1}},
        };
        D3D12UIVisualConstants constants{};
        constants.screen_size = { width, height, 0, 0 };
        D3D12UploadAllocation vertex_upload{};
        D3D12UploadAllocation constants_upload{};
        D3D12LinearUploadAllocator& allocator = frame_resources_[frame_index_].upload_allocator;
        if (!allocator.Allocate(sizeof(vertices), 16, vertex_upload) ||
            !allocator.Allocate(sizeof(constants),
                D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants_upload))
            return false;
        std::memcpy(vertex_upload.cpu, vertices.data(), sizeof(vertices));
        std::memcpy(constants_upload.cpu, &constants, sizeof(constants));
        ID3D12DescriptorHeap* heaps[] =
        {
            resource_descriptor_allocator_.Heap(), sampler_descriptor_allocator_.Heap()
        };
        command_list_->SetDescriptorHeaps(static_cast<UINT>(std::size(heaps)), heaps);
        const D3D12_VIEWPORT viewport{ 0, 0, width, height, 0, 1 };
        const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = CurrentRenderTargetView();
        command_list_->SetGraphicsRootSignature(ui_root_signature_.Get());
        command_list_->SetPipelineState(
            ui_pipelines_[static_cast<std::size_t>(D3D12UIBlendMode::Alpha)].Get());
        command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, &scissor);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->SetGraphicsRootConstantBufferView(0, constants_upload.gpu);
        command_list_->SetGraphicsRootDescriptorTable(1, source.srv.gpu);
        for (UINT index = 0; index < 4; ++index)
            command_list_->SetGraphicsRootDescriptorTable(2 + index, white->second.srv.gpu);
        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = vertex_upload.gpu;
        view.SizeInBytes = sizeof(vertices);
        view.StrideInBytes = sizeof(D3D12UIVertex);
        command_list_->IASetVertexBuffers(0, 1, &view);
        command_list_->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
        return true;
    }

    bool D3D12DeviceContext::CompositeSceneEffectTarget(
        const D3D12OffscreenTarget& source, D3D12OffscreenTarget& destination,
        const D3D12_RECT* scissor) noexcept
    {
        if (!frame_open_ || !source.IsValid() || !destination.IsValid() ||
            ui_root_signature_ == nullptr || ui_hdr_composite_pipeline_ == nullptr)
            return false;
        if (source.width != destination.width || source.height != destination.height)
            return false;
        const auto white = texture_cache_.find("__dx12_white");
        if (white == texture_cache_.end()) return false;
        if (!resource_state_tracker_.Transition(command_list_.Get(), source.color.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
            !resource_state_tracker_.Transition(command_list_.Get(), destination.color.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET))
            return false;
        ID3D12DescriptorHeap* heaps[] =
        {
            resource_descriptor_allocator_.Heap(), sampler_descriptor_allocator_.Heap()
        };
        command_list_->SetDescriptorHeaps(static_cast<UINT>(std::size(heaps)), heaps);
        const float width = static_cast<float>(destination.width);
        const float height = static_cast<float>(destination.height);
        const std::array<D3D12UIVertex, 6> vertices =
        {
            D3D12UIVertex{{0, 0}, {0, 0}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{0, height}, {0, 1}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{width, height}, {1, 1}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{0, 0}, {0, 0}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{width, height}, {1, 1}, {1, 1, 1, 1}, {0, 0, 1, 1}},
            D3D12UIVertex{{width, 0}, {1, 0}, {1, 1, 1, 1}, {0, 0, 1, 1}},
        };
        D3D12UIVisualConstants constants{};
        constants.screen_size = { width, height, 0, 0 };
        D3D12UploadAllocation vertex_upload{};
        D3D12UploadAllocation constants_upload{};
        D3D12LinearUploadAllocator& allocator = frame_resources_[frame_index_].upload_allocator;
        if (!allocator.Allocate(sizeof(vertices), 16, vertex_upload) ||
            !allocator.Allocate(sizeof(constants),
                D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants_upload))
            return false;
        std::memcpy(vertex_upload.cpu, vertices.data(), sizeof(vertices));
        std::memcpy(constants_upload.cpu, &constants, sizeof(constants));
        const D3D12_VIEWPORT viewport{ 0, 0, width, height, 0, 1 };
        const D3D12_RECT full{ 0, 0, static_cast<LONG>(destination.width),
            static_cast<LONG>(destination.height) };
        command_list_->SetGraphicsRootSignature(ui_root_signature_.Get());
        command_list_->SetPipelineState(ui_hdr_composite_pipeline_.Get());
        command_list_->OMSetRenderTargets(1, &destination.rtv.cpu, FALSE, nullptr);
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, scissor != nullptr ? scissor : &full);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->SetGraphicsRootConstantBufferView(0, constants_upload.gpu);
        command_list_->SetGraphicsRootDescriptorTable(1, source.srv.gpu);
        for (UINT index = 0; index < 4; ++index)
            command_list_->SetGraphicsRootDescriptorTable(2 + index, white->second.srv.gpu);
        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = vertex_upload.gpu;
        view.SizeInBytes = sizeof(vertices);
        view.StrideInBytes = sizeof(D3D12UIVertex);
        command_list_->IASetVertexBuffers(0, 1, &view);
        command_list_->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
        command_list_->RSSetScissorRects(1, &full);
        return true;
    }

    bool D3D12DeviceContext::DrawRuntimeUI(const D3D12UIFrame& frame) noexcept
    {
        BeginGpuPass(D3D12GpuPass::RuntimeUI);
        if (!frame.effects.empty() || !frame.effect_groups.empty())
            BeginGpuPass(D3D12GpuPass::UIEffect);
        const bool result = DrawRuntimeUIToTarget(frame, nullptr, ui_effect_targets_);
        if (!frame.effects.empty() || !frame.effect_groups.empty())
            EndGpuPass(D3D12GpuPass::UIEffect);
        EndGpuPass(D3D12GpuPass::RuntimeUI);
        return result;
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
        for (auto& entry : ui_preview_effect_history_targets_)
            ReleaseOffscreenTarget(entry.second.target);
        ui_preview_effect_history_targets_.clear();
        if (!CreateOffscreenTarget(ui_preview_target_, width, height,
            DXGI_FORMAT_R8G8B8A8_UNORM) ||
            !CreateOffscreenTarget(ui_preview_effect_targets_[0], width, height,
                DXGI_FORMAT_R8G8B8A8_UNORM) ||
            !CreateOffscreenTarget(ui_preview_effect_targets_[1], width, height,
                DXGI_FORMAT_R8G8B8A8_UNORM) ||
            !CreateOffscreenTarget(ui_preview_effect_targets_[2], width, height,
                DXGI_FORMAT_R8G8B8A8_UNORM) ||
            !CreateOffscreenTarget(ui_preview_effect_targets_[3], width, height,
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
        BeginGpuPass(D3D12GpuPass::UIPreview);
        if (!ui_preview_target_.IsValid()) return false;
        const bool result = DrawRuntimeUIToTarget(frame, &ui_preview_target_,
            ui_preview_effect_targets_);
        EndGpuPass(D3D12GpuPass::UIPreview);
        return result;
    }
}
