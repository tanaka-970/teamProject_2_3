#include "framework.h"
#include "mainInternal.h"

#include "../../../RePlayEngine/Rendering/DX12/D3D12DeviceContext.h"
#include "../../../RePlayEngine/Rendering/DX12/D3D12DescriptorHeapAllocator.h"
#include "../../../RePlayEngine/UI/Effects/UIEffect.h"

#include <cmath>
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
        constexpr const char* kStaticValidationKey = "validation:static";
        constexpr const char* kSkinnedValidationKey = "validation:skinned";
        constexpr const char* kDdsValidationKey = "validation:dds";
        constexpr const char* kShaderValidationKey = "validation:custom-shader";

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
                    "Shader" / "dx12_validation_custom_surface_ps.hlsl";
                shader_source.generated_declaration.clear();
                static_scene.shader_sources.push_back(std::move(shader_source));
            }
            if (!context.HasSkinnedMesh(kSkinnedValidationKey))
            {
                Rendering::DX12::D3D12SkinnedMeshSource skinned_source;
                skinned_source.key = kSkinnedValidationKey;
                Rendering::DX12::D3D12SkinnedVertex a;
                a.position = { -0.35f, -0.25f, 0.25f };
                a.normal = { 0.0f, 0.0f, -1.0f };
                a.texcoord = { 0.0f, 1.0f };
                Rendering::DX12::D3D12SkinnedVertex b;
                b.position = { 0.0f, 0.35f, 0.25f };
                b.normal = { 0.0f, 0.0f, -1.0f };
                b.texcoord = { 0.5f, 0.0f };
                Rendering::DX12::D3D12SkinnedVertex c;
                c.position = { 0.35f, -0.25f, 0.25f };
                c.normal = { 0.0f, 0.0f, -1.0f };
                c.texcoord = { 1.0f, 1.0f };
                skinned_source.vertices = { a, b, c };
                skinned_source.indices = { 0, 1, 2 };
                static_scene.skinned_mesh_sources.push_back(std::move(skinned_source));
            }

            Rendering::DX12::D3D12StaticDrawItem static_draw;
            static_draw.mesh_key = kStaticValidationKey;
            static_draw.base_color_texture_key = kDdsValidationKey;
            static_draw.shader_key = kShaderValidationKey;
            static_draw.material_constants.resize(sizeof(DirectX::XMFLOAT4));
            const DirectX::XMFLOAT4 validation_color{ 0.85f, 0.65f, 0.25f, 1.0f };
            std::memcpy(static_draw.material_constants.data(), &validation_color, sizeof(validation_color));
            Rendering::DX12::D3D12StaticMaterialTexture mapped_texture;
            mapped_texture.slot = 40;
            mapped_texture.texture_key = kDdsValidationKey;
            static_draw.material_textures.push_back(std::move(mapped_texture));
            static_draw.base_color_texture_key = kDdsValidationKey;
            static_draw.base_color = validation_color;
            static_scene.draws.push_back(std::move(static_draw));

            Rendering::DX12::D3D12SkinnedDrawItem skinned_draw;
            skinned_draw.surface.mesh_key = kSkinnedValidationKey;
            skinned_draw.surface.base_color_texture_key = kDdsValidationKey;
            skinned_draw.surface.base_color = { 0.25f, 0.75f, 0.95f, 1.0f };
            skinned_draw.motion_key = "validation:scene3d-motion";
            skinned_draw.bone_palette.push_back(DirectX::XMFLOAT4X4{
                1,0,0,0, 0,1,0,0, 0,0,1,0,
                static_cast<float>(submitted_frame & 1u) * 0.01f,0,0,1 });
            static_scene.skinned_draws.push_back(std::move(skinned_draw));
            static_scene.directional_light.enabled = true;
            static_scene.directional_light.direction = { 0.3f, -1.0f, 0.25f };
            static_scene.directional_light.color = { 1.0f, 0.95f, 0.9f };
            static_scene.directional_light.intensity = 1.5f;
            static_scene.directional_light.cast_shadows = true;
            static_scene.directional_light.shadow_strength = 1.0f;
            static_scene.directional_shadow.enabled = true;
            static_scene.directional_shadow.resolution = 64;
            static_scene.directional_shadow.split_distances = { 10.0f, 25.0f, 50.0f, 100.0f };
            static_scene.directional_shadow.params = { 0.002f, 1.0f, 1.5f, 1.0f };
            static_scene.directional_shadow.params2 = { 64.0f, 2.0f, 0.002f, 0.0f };
            static_scene.directional_shadow.params3 = { 2.0f, 0.02f, 1.0f, 1.0f };
            static_scene.directional_shadow.texel_world = { 0.01f, 0.02f, 0.04f, 0.08f };
            for (std::uint32_t cascade = 0;
                cascade < Rendering::DX12::D3D12DirectionalShadowSubmission::CascadeCount; ++cascade)
            {
                static_scene.directional_shadow.view_projection[cascade] = DirectX::XMFLOAT4X4{
                    1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            }

            Rendering::DX12::D3D12PointLightSubmission point;
            point.position = { 0.0f, 1.0f, -1.0f };
            point.range = 5.0f;
            point.color = { 0.4f, 0.65f, 1.0f };
            point.intensity = 2.0f;
            point.cast_shadows = true;
            point.shadow_strength = 1.0f;
            point.shadow_slice = 0;
            static_scene.point_lights.push_back(point);
            Rendering::DX12::D3D12SpotLightSubmission spot;
            spot.position = { 0.0f, 2.0f, -1.0f };
            spot.direction = { 0.0f, -1.0f, 0.4f };
            spot.range = 6.0f;
            spot.intensity = 1.5f;
            spot.cast_shadows = true;
            spot.shadow_strength = 1.0f;
            spot.shadow_slice = 6;
            static_scene.spot_lights.push_back(spot);

            static_scene.local_shadows.enabled = true;
            static_scene.local_shadows.resolution = 64;
            static_scene.local_shadows.used_slice_mask = 0x7Fu; // point 0..5 + spot 6
            for (std::uint32_t slice = 0;
                slice < Rendering::DX12::D3D12LocalShadowSubmission::SliceCount; ++slice)
            {
                static_scene.local_shadows.slices[slice].view_projection = DirectX::XMFLOAT4X4{
                    1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
                static_scene.local_shadows.slices[slice].params = { 0.05f, 10.0f, 0.002f, 0.0f };
            }

            if (!Check(context.DrawStaticScene(static_scene),
                "DX12 static mesh/material draw", checks))
                return false;

            // 旧Static Bridge検証を変えずに、Alpha Testの整合性と
            // Forward Transparent経路も実行する。
            Rendering::DX12::D3D12StaticDrawItem mask_draw = static_scene.draws.front();
            mask_draw.alpha_mode = Rendering::DX12::D3D12StaticAlphaMode::Mask;
            mask_draw.alpha_cutoff = 0.5f;
            mask_draw.base_color.w = 0.9f;
            static_scene.draws.push_back(std::move(mask_draw));

            Rendering::DX12::D3D12StaticDrawItem transparent_draw = static_scene.draws.front();
            transparent_draw.alpha_mode = Rendering::DX12::D3D12StaticAlphaMode::Blend;
            transparent_draw.base_color.w = 0.35f;
            transparent_draw.cast_shadow = false;
            static_scene.draws.push_back(std::move(transparent_draw));

            Rendering::DX12::D3D12SkinnedDrawItem second_skinned = static_scene.skinned_draws.front();
            second_skinned.motion_key = "validation:skinned-secondary";
            second_skinned.surface.base_color = { 0.95f, 0.35f, 0.25f, 1.0f };
            second_skinned.surface.world._41 = 0.1f;
            static_scene.skinned_draws.push_back(std::move(second_skinned));

            if (!Check(context.DrawScene3D(static_scene),
                "DX12 skinned/animation/depth/GBuffer/deferred/forward/light/shadow draw", checks))
                return false;
            // Runtime UI の実提出も同じ Command List / Frame Upload / Descriptor Heap で
            // 検証する。固定色を製品描画へ出す診断UIではなく、通常のUI DrawCommand ABIを使う。
            Rendering::DX12::D3D12UIFrame ui_frame;
            ui_frame.target_width = context.Width();
            ui_frame.target_height = context.Height();
            // 直接Quadだけでなく、UIEffectStackと同じオフスクリーン往復と
            // Backdrop取得を通して検証する。
            ui_frame.requires_offscreen = true;
            ui_frame.capture_backdrop = true;
            Rendering::DX12::D3D12UIEffectCommand blur_effect;
            blur_effect.kind = static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::Blur);
            blur_effect.radius = 2.0f;
            blur_effect.intensity = 1.0f;
            blur_effect.region_enabled = true;
            blur_effect.effect_region_params = { 0.5f, 0.5f, 0.35f, 0.35f };
            blur_effect.effect_region_settings = { 0.0f, 0.05f, 1.0f, 0.0f };
            blur_effect.effect_region_count.x = 1.0f;
            ui_frame.effects.push_back(blur_effect);
            Rendering::DX12::D3D12UIEffectCommand echo_effect;
            echo_effect.kind = static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::Echo);
            echo_effect.temporal = true;
            echo_effect.history_key = 1;
            echo_effect.intensity = 0.4f;
            echo_effect.amount = 1.0f;
            ui_frame.effects.push_back(echo_effect);
            Rendering::DX12::D3D12UIBatch ui_batch;
            ui_batch.texture_key = "__dx12_white";
            ui_batch.constants.screen_size = {
                static_cast<float>(context.Width()), static_cast<float>(context.Height()), 0, 0 };
            ui_batch.scissor = { 0, 0, static_cast<LONG>(context.Width()),
                static_cast<LONG>(context.Height()) };
            ui_batch.scissor_enabled = true;
            ui_batch.vertices =
            {
                { { 4, 4 }, { 0, 1 }, { 1, 1, 1, 0.15f }, { 0, 0, 1, 1 } },
                { { 4, 20 }, { 0, 0 }, { 1, 1, 1, 0.15f }, { 0, 0, 1, 1 } },
                { { 20, 20 }, { 1, 0 }, { 1, 1, 1, 0.15f }, { 0, 0, 1, 1 } },
                { { 4, 4 }, { 0, 1 }, { 1, 1, 1, 0.15f }, { 0, 0, 1, 1 } },
                { { 20, 20 }, { 1, 0 }, { 1, 1, 1, 0.15f }, { 0, 0, 1, 1 } },
                { { 20, 4 }, { 1, 1 }, { 1, 1, 1, 0.15f }, { 0, 0, 1, 1 } },
            };
            const auto triangle_area_twice = [](const Rendering::DX12::D3D12UIVertex& a,
                const Rendering::DX12::D3D12UIVertex& b,
                const Rendering::DX12::D3D12UIVertex& c) noexcept
            {
                return (b.position.x - a.position.x) *
                    (c.position.y - a.position.y) -
                    (b.position.y - a.position.y) *
                    (c.position.x - a.position.x);
            };
            if (!Check(std::fabs(triangle_area_twice(ui_batch.vertices[0],
                    ui_batch.vertices[1], ui_batch.vertices[2])) > 1.0f &&
                std::fabs(triangle_area_twice(ui_batch.vertices[3],
                    ui_batch.vertices[4], ui_batch.vertices[5])) > 1.0f,
                "DX12 UI quad contains two non-degenerate triangles", checks))
                return false;

            // Nested Shape Clipと画面座標Matteを実際のUI PSへ通す。
            ui_batch.clip_enabled = true;
            ui_batch.clip_count = 2;
            ui_batch.clips[0].parameters = { 2, 0, 0, 0 };
            ui_batch.clips[0].bounds = { 12, 12, 10, 10 };
            ui_batch.clips[0].shape = { 5, 0.5f, 0, 0 };
            ui_batch.clips[1].parameters = { 5, 0, 0, 0.2f };
            ui_batch.clips[1].bounds = { 12, 12, 9, 9 };
            ui_batch.clips[1].shape = { 5, 0.5f, 15, 0 };
            ui_batch.clip = ui_batch.clips[0];
            ui_batch.constants.clip_parameters = ui_batch.clips[0].parameters;
            ui_batch.constants.clip_bounds = ui_batch.clips[0].bounds;
            ui_batch.constants.clip_shape = ui_batch.clips[0].shape;
            ui_batch.constants.clip_state.x = 2.0f;
            ui_batch.constants.clip_parameters_extra[0] =
                ui_batch.clips[1].parameters;
            ui_batch.constants.clip_bounds_extra[0] = ui_batch.clips[1].bounds;
            ui_batch.constants.clip_shapes_extra[0] = ui_batch.clips[1].shape;
            ui_batch.mask_enabled = true;
            ui_batch.mask_count = 1;
            ui_batch.masks[0].texture_key = "__dx12_white";
            ui_batch.masks[0].screen_origin = { 4, 4 };
            ui_batch.masks[0].screen_inverse = { 1.0f / 16.0f, 0,
                0, 1.0f / 16.0f };
            ui_batch.constants.mask_parameters.x = 1.0f;
            ui_batch.constants.mask_origins[0] = { 4, 4, 0, 0 };
            ui_batch.constants.mask_inverses[0] =
                ui_batch.masks[0].screen_inverse;
            ui_batch.effect_group = 0;
            Rendering::DX12::D3D12UIEffectGroup ui_group;
            ui_group.first_batch = 0;
            ui_group.batch_count = 1;
            ui_group.effects = ui_frame.effects;
            ui_group.capture_backdrop = true;
            ui_group.composite_scissor = { 2, 2, 22, 22 };
            ui_group.composite_scissor_enabled = true;
            ui_group.target_scope = 1;
            ui_frame.effect_groups.push_back(std::move(ui_group));
            ui_frame.effects.clear();
            ui_frame.batches.push_back(std::move(ui_batch));
            if (!Check(context.DrawRuntimeUI(ui_frame),
                "DX12 runtime UI command recording", checks))
                return false;
            if (!Check(context.HasSkinnedMesh(kSkinnedValidationKey),
                "DX12 skinned mesh cache", checks))
                return false;
            if (!Check(context.HasStaticMesh(kStaticValidationKey) &&
                context.HasStaticTexture(kDdsValidationKey) &&
                context.HasStaticTexture("__dx12_white") &&
                context.HasStaticTexture("__dx12_black") &&
                context.HasStaticTexture("__dx12_gray") &&
                context.HasStaticTexture("__dx12_bump") &&
                context.HasCompiledStaticShader(kShaderValidationKey) &&
                context.StaticMeshCacheSize() >= 1 && context.TextureCacheSize() >= 5,
                "DX12 static mesh/DDS/custom shader/default texture caches", checks))
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
        if (command_line == nullptr)
            return -1;

        constexpr const char* kDx12ValidationCommands[] =
        {
            "--validate-dx12-device",
            "--validate-dx12-gpu",
            "--validate-dx12-skinned",
            "--validate-dx12-animation",
            "--validate-dx12-gbuffer",
            "--validate-dx12-deferred",
            "--validate-dx12-directional-light",
            "--validate-dx12-point-light",
            "--validate-dx12-spot-light",
            "--validate-dx12-csm",
            "--validate-dx12-point-shadow",
            "--validate-dx12-spot-shadow",
        };
        bool requested = false;
        for (const char* validation_command : kDx12ValidationCommands)
        {
            if (std::strstr(command_line, validation_command) != nullptr)
            {
                requested = true;
                break;
            }
        }
        if (!requested)
            return -1;

        // 標準検証はDebug Layerで安定して全件確認し、GPU検証は明示指定時だけ有効にする。
        const bool gpu_validation_requested =
            std::strstr(command_line, "--validate-dx12-gpu") != nullptr;

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
        // 通常の検証は軽いDebug Layerだけにし、GPU検証は明示指定時だけ有効化する。
        ok = Check(context.Initialize(window, 64, 64, enable_debug, false, true,
            gpu_validation_requested && enable_debug),
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
                "write DX12 DDS validation texture", checks);


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
                "DX12 static asset cache clear", checks);
        }
        if (ok)
        {
            ok = Check(!context.HasStaticMesh(kStaticValidationKey) &&
                !context.HasSkinnedMesh(kSkinnedValidationKey) &&
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
