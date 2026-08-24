#include "framework.h"
#include "mainInternal.h"

#include "../../../RePlayEngine/Rendering/DX12/D3D12DeviceContext.h"
#include "../../../RePlayEngine/Rendering/DX12/D3D12DescriptorHeapAllocator.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

namespace ReplayEngine::Runtime::Detail
{
    namespace
    {
        constexpr wchar_t kValidationWindowClass[] =
            L"ReplayEngineDX12ValidationWindowClass";
        constexpr const char* kStaticValidationKey = "validation:phase2-static";
        constexpr const char* kDdsValidationKey = "validation:phase2-dds";
        constexpr const char* kShaderValidationKey = "validation:phase2-custom-shader";

        bool WriteValidationDds(const std::filesystem::path& path)
        {
            // 4x4 の BC1/DXT1・1 mip。意図的に小さくし、TextureCompressor の出力と同じ
            // DDS + Block Compression Upload 経路を検証する。
            std::vector<std::uint8_t> bytes(128u + 8u, 0u);
            const auto write_u32 = [&bytes](std::size_t offset, std::uint32_t value)
            {
                if (offset + sizeof(value) <= bytes.size())
                    std::memcpy(bytes.data() + offset, &value, sizeof(value));
            };
            write_u32(0, 0x20534444u); // DDS Magic
            write_u32(4, 124u);
            write_u32(8, 0x00081007u); // CAPS|HEIGHT|WIDTH|PIXELFORMAT|LINEARSIZE
            write_u32(12, 4u);
            write_u32(16, 4u);
            write_u32(20, 8u);
            write_u32(76, 32u);
            write_u32(80, 0x4u); // DDPF_FOURCC フラグ
            write_u32(84, 0x31545844u); // DXT1 識別子
            write_u32(108, 0x1000u); // DDSCAPS_TEXTURE フラグ
            // BC1 の Endpoint は赤と青。すべての Selector は Endpoint 0 を選ぶ。
            bytes[128] = 0x00u;
            bytes[129] = 0xF8u;
            bytes[130] = 0x1Fu;
            bytes[131] = 0x00u;

            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file) return false;
            file.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(file);
        }

        LRESULT CALLBACK ValidationWindowProcedure(HWND window, UINT message,
            WPARAM wparam, LPARAM lparam)
        {
            return DefWindowProcW(window, message, wparam, lparam);
        }

        HWND CreateValidationWindow(HINSTANCE instance) noexcept
        {
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = ValidationWindowProcedure;
            window_class.hInstance = instance;
            window_class.lpszClassName = kValidationWindowClass;
            if (RegisterClassExW(&window_class) == 0 &&
                GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return nullptr;

            return CreateWindowExW(0, kValidationWindowClass, L"DX12 validation",
                WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, instance,
                nullptr);
        }

        bool Check(bool condition, const char* message, int& checks)
        {
            ++checks;
            if (condition) return true;
            std::fprintf(stderr, "DX12 validation failed: %s\n", message);
            return false;
        }

        bool RunDeviceFrameValidation(
            Rendering::DX12::D3D12DeviceContext& context, const float color[4],
            const char* label, int& checks)
        {
            const std::uint32_t submitted_frame = context.FrameIndex();
            ID3D12Resource* submitted_target = context.CurrentRenderTarget();
            const std::uint64_t previous_signal = context.LastSignaledFenceValue();

            if (!Check(context.BeginFrame(color), "BeginFrame", checks)) return false;
            if (!Check(context.ResourceStateTracker().StateOf(submitted_target) ==
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                "back buffer transition to RENDER_TARGET", checks))
                return false;

            Rendering::DX12::D3D12FrameConstants frame_constants;
            frame_constants.time_parameters.x = static_cast<float>(submitted_frame);
            if (!Check(context.SubmitFrameConstants(frame_constants),
                "frame constants upload", checks))
                return false;

            Rendering::RenderItemList render_items;
            Rendering::RenderItem first_item;
            first_item.owner = Core::ObjectID{ 101 };
            first_item.mesh_asset = "builtin:validation";
            first_item.cast_shadow = true;
            render_items.Add(first_item);
            Rendering::RenderItem second_item;
            second_item.owner = Core::ObjectID{ 102 };
            second_item.mesh_asset = "builtin:validation";
            second_item.skinned = true;
            second_item.outline = true;
            render_items.Add(second_item);

            if (!Check(context.SubmitRenderItems(render_items) &&
                context.RenderItemBatch().Size() == render_items.Size() &&
                context.RenderItemBatch().GpuBuffer() != nullptr &&
                context.RenderItemBatch().ShaderResourceAllocation().IsValid(),
                "render item frame-upload/SRV", checks))
                return false;
            if (!Check(context.CurrentFrameUploadUsed() > 0 &&
                context.CurrentFrameUploadUsed() <= context.CurrentFrameUploadCapacity(),
                "frame upload allocator accounting", checks))
                return false;

            Rendering::DX12::D3D12StaticSceneSubmission static_scene;
            if (!context.HasStaticMesh(kStaticValidationKey))
            {
                Rendering::DX12::D3D12StaticMeshSource mesh_source;
                mesh_source.key = kStaticValidationKey;
                mesh_source.vertices =
                {
                    { { -0.45f, -0.35f, 0.2f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f } },
                    { {  0.00f,  0.45f, 0.2f }, { 0.0f, 0.0f, -1.0f }, { 0.5f, 0.0f } },
                    { {  0.45f, -0.35f, 0.2f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f } },
                };
                mesh_source.indices = { 0, 1, 2 };
                static_scene.mesh_sources.push_back(std::move(mesh_source));
            }
            if (!context.HasStaticTexture(kDdsValidationKey))
            {
                Rendering::DX12::D3D12StaticTextureSource texture_source;
                texture_source.key = kDdsValidationKey;
                texture_source.source_path = std::filesystem::current_path() /
                    "DX12ValidationTexture.dds";
                static_scene.texture_sources.push_back(std::move(texture_source));
            }
            if (!context.HasStaticShader(kShaderValidationKey))
            {
                Rendering::DX12::D3D12StaticShaderSource shader_source;
                shader_source.key = kShaderValidationKey;
                shader_source.source_path = std::filesystem::current_path() /
                    "DX12ValidationCustomSurface.hlsl";
                shader_source.generated_declaration =
                    "#define REPLAY_MATERIAL_SCHEMA_INJECTED 1\n"
                    "cbuffer REPLAY_MATERIAL_CB : register(b9) { float4 PhaseColor; };\n"
                    "Texture2D PhaseMap : register(t40);\n";
                static_scene.shader_sources.push_back(std::move(shader_source));
            }
            Rendering::DX12::D3D12StaticDrawItem static_draw;
            static_draw.mesh_key = kStaticValidationKey;
            static_draw.base_color_texture_key = kDdsValidationKey;
            static_draw.shader_key = kShaderValidationKey;
            static_draw.material_constants.resize(sizeof(DirectX::XMFLOAT4));
            const DirectX::XMFLOAT4 phase_color{ 0.85f, 0.65f, 0.25f, 1.0f };
            std::memcpy(static_draw.material_constants.data(), &phase_color, sizeof(phase_color));
            Rendering::DX12::D3D12StaticMaterialTexture mapped_texture;
            mapped_texture.slot = 40;
            mapped_texture.texture_key = kDdsValidationKey;
            static_draw.material_textures.push_back(std::move(mapped_texture));
            static_draw.base_color_texture_key = kDdsValidationKey;
            static_draw.base_color = phase_color;
            static_scene.draws.push_back(std::move(static_draw));
            if (!Check(context.DrawStaticScene(static_scene),
                "Phase2 production static mesh/material draw", checks))
                return false;
            if (!Check(context.HasStaticMesh(kStaticValidationKey) &&
                context.HasStaticTexture(kDdsValidationKey) &&
                context.HasStaticTexture("__dx12_white") &&
                context.HasStaticTexture("__dx12_black") &&
                context.HasStaticTexture("__dx12_gray") &&
                context.HasStaticTexture("__dx12_bump") &&
                context.HasCompiledStaticShader(kShaderValidationKey) &&
                context.StaticMeshCacheSize() >= 1 && context.TextureCacheSize() >= 5,
                "Phase2 static mesh/DDS/custom shader/default texture caches", checks))
                return false;

            if (!Check(context.DrawValidationTriangle(), "validation triangle", checks))
                return false;
            if (!Check(context.EndFrame(), "EndFrame/Present", checks)) return false;
            if (!Check(context.ResourceStateTracker().StateOf(submitted_target) ==
                D3D12_RESOURCE_STATE_PRESENT,
                "back buffer transition to PRESENT", checks))
                return false;
            if (!Check(context.LastSignaledFenceValue() > previous_signal &&
                context.FrameFenceValue(submitted_frame) ==
                    context.LastSignaledFenceValue(),
                "globally monotonic frame fence signal", checks))
                return false;

            std::fprintf(stderr, "DX12 validation frame passed: %s fence=%llu\n",
                label, static_cast<unsigned long long>(context.LastSignaledFenceValue()));
            return true;
        }
    }

    int RunHeadlessDX12Validation(const char* command_line)
    {
        if (command_line == nullptr ||
            std::strstr(command_line, "--validate-dx12-device") == nullptr)
            return -1;

        const HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND window = CreateValidationWindow(instance);
        if (window == nullptr)
        {
            std::fprintf(stderr, "DX12 validation failed: CreateWindowExW error=%lu\n",
                static_cast<unsigned long>(GetLastError()));
            return 80;
        }

        Rendering::DX12::D3D12DeviceContext context;
        int checks = 0;
        bool ok = true;
        bool enable_debug = false;
#if defined(_DEBUG) || defined(DEBUG)
        enable_debug = true;
#endif
        ok = Check(context.Initialize(window, 64, 64, enable_debug, false, true),
            "Initialize", checks);

        if (ok)
        {
            ok = Check(context.IsInitialized() && context.Device() != nullptr &&
                context.CommandQueue() != nullptr && context.CommandList() != nullptr &&
                context.ResourceDescriptorAllocator().Heap() != nullptr &&
                context.SamplerDescriptorAllocator().Heap() != nullptr,
                "device/queue/list/descriptor heaps", checks);
        }
        if (ok)
        {
            ok = Check(context.SceneViewTarget().IsValid() &&
                context.GameViewTarget().IsValid() &&
                context.SceneViewTarget().width == 64 &&
                context.SceneViewTarget().height == 64,
                "Scene/Game offscreen render-target foundation", checks);
        }
        if (ok && enable_debug)
        {
            std::fprintf(stderr,
                "DX12 debug requested: layer=%d gpu_validation=%d dred=%d\n",
                context.DebugLayerEnabled() ? 1 : 0,
                context.GpuValidationEnabled() ? 1 : 0,
                context.DredEnabled() ? 1 : 0);
        }

        const std::uint32_t persistent_resource_descriptors =
            context.ResourceDescriptorAllocator().Used();
        if (ok)
        {
            Rendering::DX12::D3D12DescriptorAllocation descriptors{};
            ok = Check(context.ResourceDescriptorAllocator().Allocate(4, descriptors) &&
                context.ResourceDescriptorAllocator().Used() ==
                    persistent_resource_descriptors + 4,
                "resource descriptor allocate", checks);
            if (ok)
            {
                ok = Check(context.ResourceDescriptorAllocator().Free(descriptors) &&
                    context.ResourceDescriptorAllocator().Used() ==
                        persistent_resource_descriptors,
                    "resource descriptor immediate free", checks);
            }
        }

        if (ok)
        {
            Rendering::DX12::D3D12DescriptorAllocation deferred{};
            ok = Check(context.ResourceDescriptorAllocator().Allocate(1, deferred),
                "descriptor retirement allocate", checks);
            if (ok)
            {
                ok = Check(context.ResourceDescriptorAllocator().Retire(deferred, 100) &&
                    context.ResourceDescriptorAllocator().RetiredCount() == 1,
                    "descriptor retirement queue", checks);
            }
            if (ok)
            {
                context.ResourceDescriptorAllocator().ReleaseCompleted(99);
                ok = Check(context.ResourceDescriptorAllocator().RetiredCount() == 1,
                    "descriptor retirement waits for fence", checks);
            }
            if (ok)
            {
                context.ResourceDescriptorAllocator().ReleaseCompleted(100);
                ok = Check(context.ResourceDescriptorAllocator().RetiredCount() == 0 &&
                    context.ResourceDescriptorAllocator().Used() ==
                        persistent_resource_descriptors,
                    "descriptor retirement release", checks);
            }
        }

        const std::uint32_t persistent_sampler_descriptors =
            context.SamplerDescriptorAllocator().Used();
        if (ok)
        {
            Rendering::DX12::D3D12DescriptorAllocation sampler{};
            ok = Check(context.SamplerDescriptorAllocator().Allocate(1, sampler),
                "sampler descriptor allocate", checks);
            if (ok)
            {
                D3D12_SAMPLER_DESC description{};
                description.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                description.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                description.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                description.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                description.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                description.MaxAnisotropy = 1;
                description.MinLOD = 0.0f;
                description.MaxLOD = D3D12_FLOAT32_MAX;
                context.Device()->CreateSampler(&description, sampler.cpu);
                ok = Check(context.SamplerDescriptorAllocator().Free(sampler) &&
                    context.SamplerDescriptorAllocator().Used() ==
                        persistent_sampler_descriptors,
                    "sampler descriptor create/free", checks);
            }
        }

        const std::filesystem::path validation_dds =
            std::filesystem::current_path() / "DX12ValidationTexture.dds";
        if (ok)
            ok = Check(WriteValidationDds(validation_dds),
                "write Phase2 DDS validation texture", checks);

        const std::filesystem::path validation_shader =
            std::filesystem::current_path() / "DX12ValidationCustomSurface.hlsl";
        if (ok)
        {
            std::ofstream shader_file(validation_shader, std::ios::binary | std::ios::trunc);
            shader_file <<
                "#ifndef REPLAY_MATERIAL_SCHEMA_INJECTED\n"
                "cbuffer REPLAY_MATERIAL_CB : register(b9) { float4 PhaseColor; };\n"
                "Texture2D PhaseMap : register(t40);\n"
                "#endif\n"
                "#include \"static_mesh.hlsli\"\n"
                "#include \"frame_common.hlsli\"\n"
                "SamplerState PhaseSampler : register(s1);\n"
                "float4 main(VS_OUT pin) : SV_TARGET\n"
                "{\n"
                "    return PhaseMap.Sample(PhaseSampler, pin.texcoord) * PhaseColor * pin.color;\n"
                "}\n";
            ok = Check(static_cast<bool>(shader_file),
                "write Phase2 custom ShaderAsset validation source", checks);
        }

        constexpr float colors[][4] =
        {
            { 0.08f, 0.12f, 0.20f, 1.0f },
            { 0.20f, 0.10f, 0.06f, 1.0f },
            { 0.06f, 0.18f, 0.12f, 1.0f },
            { 0.16f, 0.08f, 0.18f, 1.0f },
        };

        // 各 Frame の後で WaitForGpu は意図的に呼ばない。GPU の遅延を許したまま
        // 2 つの Frame Resource を再利用し、旧来の重複 Fence 値の再発を検証する。
        for (int frame = 0; ok && frame < 6; ++frame)
        {
            char label[64]{};
            std::snprintf(label, sizeof(label), "pipelined frame %d", frame);
            ok = RunDeviceFrameValidation(context, colors[frame % 4], label, checks);
        }

        if (ok)
        {
            ok = Check(context.LastSignaledFenceValue() >= 6,
                "pipelined frames produced unique fence timeline", checks);
        }
        if (ok)
        {
            ok = Check(context.Resize(96, 64), "Resize", checks);
        }
        if (ok)
        {
            ok = Check(context.SceneViewTarget().IsValid() &&
                context.GameViewTarget().IsValid() &&
                context.SceneViewTarget().width == 96 &&
                context.SceneViewTarget().height == 64 &&
                context.GameViewTarget().width == 96 &&
                context.GameViewTarget().height == 64,
                "offscreen targets recreated by Resize", checks);
        }
        for (int frame = 0; ok && frame < 4; ++frame)
        {
            char label[64]{};
            std::snprintf(label, sizeof(label), "resized pipelined frame %d", frame);
            ok = RunDeviceFrameValidation(context, colors[(frame + 1) % 4], label, checks);
        }

        if (ok && context.IsInitialized())
            ok = Check(context.WaitForGpu(), "final GPU drain", checks);
        else if (context.IsInitialized())
            (void)context.WaitForGpu();
        if (ok)
        {
            ok = Check(context.ClearStaticAssetCaches(),
                "Phase2 static asset cache clear", checks);
        }
        if (ok)
        {
            ok = Check(!context.HasStaticMesh(kStaticValidationKey) &&
                !context.HasStaticTexture(kDdsValidationKey) &&
                context.HasStaticTexture("__dx12_white") &&
                context.HasStaticTexture("__dx12_black") &&
                context.HasStaticTexture("__dx12_gray") &&
                context.HasStaticTexture("__dx12_bump") &&
                !context.HasCompiledStaticShader(kShaderValidationKey) &&
                context.StaticMeshCacheSize() == 0 && context.TextureCacheSize() == 4,
                "cache clear preserves built-in fallback textures and drops custom PSOs", checks);
        }
        if (ok)
        {
            ok = Check(!context.HasFatalError() &&
                context.LastDeviceRemovedReason() == S_OK,
                "device remained available", checks);
        }
        std::error_code remove_error;
        std::filesystem::remove(validation_dds, remove_error);
        remove_error.clear();
        std::filesystem::remove(validation_shader, remove_error);
        context.Shutdown();
        DestroyWindow(window);
        UnregisterClassW(kValidationWindowClass, instance);

        if (!ok)
        {
            std::fprintf(stderr, "DX12 validation failed: %d checks\n", checks);
            return 81;
        }
        std::fprintf(stderr, "DX12 validation passed: %d checks\n", checks);
        return 0;
    }
}
