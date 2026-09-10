#include "framework.h"
#include "mainInternal.h"

#include "../../../RePlayEngine/Rendering/DX12/D3D12DeviceContext.h"
#include "../../../RePlayEngine/Rendering/DX12/D3D12DescriptorHeapAllocator.h"
#include "../../../RePlayEngine/Rendering/Capture/GoldenImage.h"
#include "../../../RePlayEngine/Rendering/Effects/EffectPresetAsset.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "../../../RePlayEngine/UI/Effects/UIEffect.h"
#include "../../../RePlayEngine/UI/FontAtlas.h"
#include "../../../RePlayEngine/UI/RichTextParser.h"
#include "../../../RePlayEngine/Components/UI/UITextComponent.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
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

        bool RunModelScreenBoundsValidation(
            Rendering::DX12::D3D12DeviceContext& context, int& checks)
        {
            constexpr std::uint64_t owner_id = 0xB1E00001ull;
            Rendering::DX12::D3D12StaticSceneSubmission submission;
            Rendering::DX12::D3D12StaticMeshSource source;
            source.key = "validation:model-screen-bounds";
            source.vertices =
            {
                { { -0.5f, -0.25f, 0.2f }, { 0, 0, -1 }, { 0, 1 } },
                { { -0.5f,  0.5f, 0.2f }, { 0, 0, -1 }, { 0, 0 } },
                { {  0.5f, -0.25f, 0.2f }, { 0, 0, -1 }, { 1, 1 } }
            };
            submission.mesh_sources.push_back(std::move(source));
            if (!Check(context.CacheMeshLocalBounds(submission),
                "model local bounds cache", checks))
                return false;

            Rendering::DX12::D3D12MeshLocalBounds bounds;
            if (!Check(context.GetStaticMeshLocalBounds(
                "validation:model-screen-bounds", bounds) && bounds.valid &&
                bounds.minimum.x == -0.5f && bounds.minimum.y == -0.25f &&
                bounds.maximum.x == 0.5f && bounds.maximum.y == 0.5f,
                "model local bounds values", checks))
                return false;

            const DirectX::XMFLOAT4X4 identity{
                1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            DirectX::XMFLOAT4X4 world = identity;
            world._41 = 0.25f;
            const D3D12_VIEWPORT viewport{ 0, 0, 64, 64, 0, 1 };
            D3D12_RECT rect{};
            const bool projected = Rendering::DX12::CalculateD3D12ScreenRect(
                bounds, world, identity, viewport, rect);
            if (projected)
            {
                std::fprintf(stderr,
                    "DX12 model screen rect owner=%llu left=%ld top=%ld right=%ld bottom=%ld\n",
                    static_cast<unsigned long long>(owner_id),
                    static_cast<long>(rect.left), static_cast<long>(rect.top),
                    static_cast<long>(rect.right), static_cast<long>(rect.bottom));
            }
            if (!Check(projected && rect.left == 24 && rect.top == 16 &&
                rect.right == 56 && rect.bottom == 40,
                "model screen rect projection", checks))
                return false;

            world._41 = 2.0f;
            if (!Check(!Rendering::DX12::CalculateD3D12ScreenRect(
                bounds, world, identity, viewport, rect),
                "model screen rect offscreen is empty", checks))
                return false;

            DirectX::XMFLOAT4X4 behind_view_projection = identity;
            behind_view_projection._34 = 1.0f;
            behind_view_projection._44 = 0.0f;
            world = identity;
            world._43 = -1.0f;
            return Check(!Rendering::DX12::CalculateD3D12ScreenRect(
                bounds, world, behind_view_projection, viewport, rect),
                "model screen rect behind camera is empty", checks);
        }

        // 失敗したときだけ Debug Layer の蓄積メッセージを stderr へ流す。
        void DumpDebugMessages(Rendering::DX12::D3D12DeviceContext& context)
        {
            std::vector<Rendering::DX12::D3D12DebugMessage> messages;
            context.ConsumeDebugMessages(messages);
            for (const auto& message : messages)
            {
                std::fprintf(stderr, "  [DX12 debug] %s (x%u)\n",
                    message.text.c_str(), message.repeat_count);
            }
        }

        bool HasCommandLineToken(const char* command_line, const char* expected)
        {
            if (command_line == nullptr || expected == nullptr || *expected == '\0')
                return false;
            std::istringstream arguments(command_line);
            std::string token;
            while (arguments >> token)
            {
                if (token == expected) return true;
            }
            return false;
        }

        bool ReadCommandLineValue(const char* command_line, const char* key,
            std::string& value)
        {
            value.clear();
            if (command_line == nullptr || key == nullptr) return false;
            std::istringstream arguments(command_line);
            std::string token;
            while (arguments >> token)
            {
                if (token != key) continue;
                if (!(arguments >> value) || value.rfind("--", 0) == 0)
                {
                    value.clear();
                    return true;
                }
                return true;
            }
            return false;
        }

        bool IsSafeValidationName(const std::string& name)
        {
            if (name.empty() || name.size() > 96u || name.find("..") != std::string::npos)
                return false;
            return name.find_first_of("\\/:*?\"<>|") == std::string::npos;
        }

        std::array<Rendering::DX12::D3D12UIVertex, 6> MakeValidationQuad(
            float left, float top, float right, float bottom,
            const DirectX::XMFLOAT4& color = { 1, 1, 1, 1 })
        {
            using Vertex = Rendering::DX12::D3D12UIVertex;
            return
            {
                Vertex{{ left, top }, { 0, 0 }, color, { 0, 0, 1, 1 }},
                Vertex{{ left, bottom }, { 0, 1 }, color, { 0, 0, 1, 1 }},
                Vertex{{ right, bottom }, { 1, 1 }, color, { 0, 0, 1, 1 }},
                Vertex{{ left, top }, { 0, 0 }, color, { 0, 0, 1, 1 }},
                Vertex{{ right, bottom }, { 1, 1 }, color, { 0, 0, 1, 1 }},
                Vertex{{ right, top }, { 1, 0 }, color, { 0, 0, 1, 1 }},
            };
        }

        Rendering::DX12::D3D12UIBatch MakeValidationUiBatch(
            std::uint32_t width, std::uint32_t height, float inset = 4.0f)
        {
            Rendering::DX12::D3D12UIBatch batch;
            batch.texture_key = "__dx12_white";
            batch.constants.screen_size = {
                static_cast<float>(width), static_cast<float>(height), 0, 0 };
            batch.scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
            batch.scissor_enabled = true;
            const float right = (std::max)(inset + 2.0f,
                static_cast<float>(width) - inset);
            const float bottom = (std::max)(inset + 2.0f,
                static_cast<float>(height) - inset);
            const auto vertices = MakeValidationQuad(inset, inset, right, bottom);
            batch.vertices.assign(vertices.begin(), vertices.end());
            return batch;
        }

        int RunGoldenComparison(const char* command_line)
        {
            std::string name;
            if (!ReadCommandLineValue(command_line, "--compare-golden", name))
                return -1;
            if (!IsSafeValidationName(name))
            {
                std::fprintf(stderr,
                    "compare-golden failed: missing or invalid image name\n");
                return 82;
            }

            const std::filesystem::path root =
                std::filesystem::path("Saved") / "Golden";
            const std::filesystem::path current_path = root / (name + ".png");
            const std::filesystem::path reference_path =
                root / "reference" / (name + ".png");
            const std::filesystem::path diff_path =
                root / "diff" / (name + ".png");

            Rendering::Capture::Image current;
            Rendering::Capture::Image reference;
            Rendering::Capture::Image diff;
            Rendering::Capture::CompareResult result;
            std::string error;
            if (!Rendering::Capture::GoldenImage::LoadPng(
                reference_path, reference, error))
            {
                std::fprintf(stderr, "compare-golden failed: %s\n", error.c_str());
                return 82;
            }
            error.clear();
            if (!Rendering::Capture::GoldenImage::LoadPng(
                current_path, current, error))
            {
                std::fprintf(stderr, "compare-golden failed: %s\n", error.c_str());
                return 82;
            }

            if (!Rendering::Capture::GoldenImage::Compare(
                reference, current, 0, result, &diff))
            {
                std::fprintf(stderr,
                    "compare-golden failed: image comparison did not complete\n");
                return 82;
            }
            if (!result.compared || result.size_mismatch)
            {
                std::fprintf(stderr,
                    "compare-golden failed: size mismatch reference=%ux%u current=%ux%u\n",
                    reference.width, reference.height, current.width, current.height);
                return 82;
            }

            const double differing_ratio = result.total_pixels != 0
                ? static_cast<double>(result.differing_pixels) /
                    static_cast<double>(result.total_pixels)
                : 1.0;
            std::fprintf(stderr,
                "compare-golden: name=%s max_channel_delta=%d differing_pixels=%zu/%zu ratio=%.6f%%\n",
                name.c_str(), result.max_channel_delta, result.differing_pixels,
                result.total_pixels, differing_ratio * 100.0);

            error.clear();
            if (!Rendering::Capture::GoldenImage::SavePng(diff_path, diff, error))
            {
                std::fprintf(stderr,
                    "compare-golden failed to write diff: %s\n", error.c_str());
                return 82;
            }

            constexpr int kMaximumChannelDelta = 2;
            constexpr double kMaximumDifferingPixelRatio = 0.001;
            if (result.max_channel_delta > kMaximumChannelDelta ||
                differing_ratio > kMaximumDifferingPixelRatio)
            {
                std::fprintf(stderr,
                    "compare-golden FAIL: limits max_delta<=%d differing_ratio<=0.100%% diff=%s\n",
                    kMaximumChannelDelta, diff_path.generic_u8string().c_str());
                return 82;
            }

            std::fprintf(stderr,
                "compare-golden PASS: diff=%s\n",
                diff_path.generic_u8string().c_str());
            return 0;
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
            skinned_draw.bone_palette =
                std::make_shared<std::vector<DirectX::XMFLOAT4X4>>();
            skinned_draw.bone_palette->push_back(DirectX::XMFLOAT4X4{
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
                static_scene.local_shadows.slices[slice].params = { 0.05f, 10.0f, 0.002f, 1.5f };
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
            {
                DumpDebugMessages(context);
                return false;
            }

            const Rendering::DX12::D3D12Scene3DStateSnapshot active_scene_state =
                context.CaptureScene3DState();
            Rendering::DX12::D3D12FrameConstants isolated_constants = frame_constants;
            isolated_constants.camera_position = { 7.0f, 3.0f, -5.0f, 1.0f };
            Rendering::DX12::D3D12StaticSceneSubmission isolated_scene = static_scene;
            isolated_scene.mesh_sources.clear();
            isolated_scene.skinned_mesh_sources.clear();
            isolated_scene.texture_sources.clear();
            isolated_scene.shader_sources.clear();
            isolated_scene.directional_light = {};
            isolated_scene.point_lights.clear();
            isolated_scene.spot_lights.clear();
            isolated_scene.directional_shadow = {};
            isolated_scene.local_shadows = {};
            isolated_scene.post_process.bloom_enabled = false;
            isolated_scene.post_process.vignette_enabled = false;
            isolated_scene.post_process.fxaa_enabled = false;
            isolated_scene.post_process.taa_enabled = false;
            isolated_scene.post_process.ssao_enabled = false;
            isolated_scene.post_process.ssr_enabled = false;
            Rendering::DX12::D3D12Scene3DDrawOptions isolated_options;
            isolated_options.manage_shadow_targets = false;
            isolated_options.allow_static_mesh_cache_replacement = false;
            isolated_options.read_motion_history = false;
            isolated_options.write_motion_history = false;
            isolated_options.read_scene_history = false;
            isolated_options.write_scene_history = false;
            if (!Check(context.SubmitFrameConstants(isolated_constants) &&
                context.DrawScene3D(isolated_scene, isolated_options) &&
                context.SubmitFrameConstants(frame_constants),
                "isolated Loading Scene 3D draw", checks))
            {
                DumpDebugMessages(context);
                return false;
            }
            const Rendering::DX12::D3D12Scene3DStateSnapshot isolated_scene_state =
                context.CaptureScene3DState();
            bool same_gbuffer_resources = true;
            bool same_gbuffer_states = true;
            for (std::uint32_t index = 0; index < Rendering::DX12::kScene3DGBufferCount; ++index)
            {
                same_gbuffer_resources = same_gbuffer_resources &&
                    active_scene_state.gbuffer_resources[index] ==
                    isolated_scene_state.gbuffer_resources[index];
                same_gbuffer_states = same_gbuffer_states &&
                    active_scene_state.gbuffer_states[index] ==
                    isolated_scene_state.gbuffer_states[index];
            }
            Check(active_scene_state.motion_history_size == isolated_scene_state.motion_history_size &&
                active_scene_state.motion_frame_serial == isolated_scene_state.motion_frame_serial,
                "isolated draw keeps active motion history/frame serial", checks);
            Check(active_scene_state.scene_history_valid == isolated_scene_state.scene_history_valid &&
                active_scene_state.scene_history_write_serial ==
                    isolated_scene_state.scene_history_write_serial &&
                active_scene_state.history_resource == isolated_scene_state.history_resource &&
                active_scene_state.history_state == isolated_scene_state.history_state,
                "isolated draw keeps active TAA/SSR scene history", checks);
            Check(active_scene_state.scene_effect_history_write_serial ==
                    isolated_scene_state.scene_effect_history_write_serial &&
                active_scene_state.scene_effect_history_size ==
                    isolated_scene_state.scene_effect_history_size,
                "isolated draw keeps active scene effect temporal history", checks);
            Check(active_scene_state.directional_shadow_resource ==
                    isolated_scene_state.directional_shadow_resource &&
                active_scene_state.local_shadow_resource == isolated_scene_state.local_shadow_resource &&
                active_scene_state.directional_shadow_resolution ==
                    isolated_scene_state.directional_shadow_resolution &&
                active_scene_state.local_shadow_resolution ==
                    isolated_scene_state.local_shadow_resolution &&
                active_scene_state.directional_shadow_state ==
                    isolated_scene_state.directional_shadow_state &&
                active_scene_state.local_shadow_state == isolated_scene_state.local_shadow_state,
                "isolated draw keeps active shadow targets", checks);
            Check(same_gbuffer_resources && same_gbuffer_states &&
                active_scene_state.depth_resource == isolated_scene_state.depth_resource &&
                active_scene_state.depth_state == isolated_scene_state.depth_state,
                "isolated draw keeps active GBuffer/depth resources and states", checks);
            Check(active_scene_state.static_mesh_cache_size ==
                    isolated_scene_state.static_mesh_cache_size &&
                active_scene_state.skinned_mesh_cache_size ==
                    isolated_scene_state.skinned_mesh_cache_size &&
                active_scene_state.texture_cache_size == isolated_scene_state.texture_cache_size &&
                active_scene_state.static_mesh_bounds_cache_size ==
                    isolated_scene_state.static_mesh_bounds_cache_size &&
                active_scene_state.skinned_mesh_bounds_cache_size ==
                    isolated_scene_state.skinned_mesh_bounds_cache_size,
                "isolated draw with pre-cached assets keeps mesh/texture cache state", checks);
            const auto same_bytes = [](const auto& left, const auto& right) noexcept
            {
                return std::memcmp(&left, &right, sizeof(left)) == 0;
            };
            const auto& active_frame = active_scene_state.frame_constants;
            const auto& restored_frame = isolated_scene_state.frame_constants;
            Check(same_bytes(active_frame.view_projection, restored_frame.view_projection) &&
                same_bytes(active_frame.camera_position, restored_frame.camera_position) &&
                same_bytes(active_frame.time_parameters, restored_frame.time_parameters) &&
                same_bytes(active_frame.view, restored_frame.view) &&
                same_bytes(active_frame.projection, restored_frame.projection) &&
                same_bytes(active_frame.inv_view, restored_frame.inv_view) &&
                same_bytes(active_frame.inv_projection, restored_frame.inv_projection) &&
                same_bytes(active_frame.inv_view_projection, restored_frame.inv_view_projection) &&
                same_bytes(active_frame.prev_view_projection, restored_frame.prev_view_projection) &&
                same_bytes(active_frame.screen_size, restored_frame.screen_size) &&
                same_bytes(active_frame.camera_planes, restored_frame.camera_planes) &&
                same_bytes(active_frame.jitter, restored_frame.jitter),
                "isolated draw restores active Frame Constants", checks);

            Rendering::DX12::D3D12StaticSceneSubmission preload_scene;
            Rendering::DX12::D3D12StaticMeshSource preload_mesh;
            preload_mesh.key = "validation:loading-preload-static";
            preload_mesh.vertices =
            {
                { { -0.2f, -0.2f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f } },
                { {  0.0f,  0.2f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.5f, 0.0f } },
                { {  0.2f, -0.2f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f } },
            };
            preload_mesh.indices = { 0, 1, 2 };
            preload_scene.mesh_sources.push_back(std::move(preload_mesh));
            const auto preload_before = context.CaptureScene3DState();
            const bool preload_ok = context.PreloadScene3DResources(preload_scene, false);
            const auto preload_after = context.CaptureScene3DState();
            bool preload_gbuffer_unchanged = true;
            for (std::uint32_t index = 0; index < Rendering::DX12::kScene3DGBufferCount; ++index)
            {
                preload_gbuffer_unchanged = preload_gbuffer_unchanged &&
                    preload_after.gbuffer_resources[index] == preload_before.gbuffer_resources[index] &&
                    preload_after.gbuffer_states[index] == preload_before.gbuffer_states[index];
            }
            Check(preload_ok && context.HasStaticMesh("validation:loading-preload-static") &&
                preload_after.static_mesh_cache_size == preload_before.static_mesh_cache_size + 1 &&
                preload_after.motion_frame_serial == preload_before.motion_frame_serial &&
                preload_after.scene_history_write_serial == preload_before.scene_history_write_serial &&
                preload_after.scene_effect_history_write_serial ==
                    preload_before.scene_effect_history_write_serial &&
                preload_after.directional_shadow_resource == preload_before.directional_shadow_resource &&
                preload_after.local_shadow_resource == preload_before.local_shadow_resource &&
                preload_after.depth_resource == preload_before.depth_resource &&
                preload_after.depth_state == preload_before.depth_state &&
                preload_gbuffer_unchanged,
                "Loading Scene GPU preload uploads mesh without touching histories/render targets", checks);
            const auto preload_second_before = context.CaptureScene3DState();
            const bool preload_second_ok = context.PreloadScene3DResources(preload_scene, false);
            const auto preload_second_after = context.CaptureScene3DState();
            Check(preload_second_ok &&
                preload_second_after.static_mesh_cache_size ==
                    preload_second_before.static_mesh_cache_size &&
                preload_second_after.static_mesh_bounds_cache_size ==
                    preload_second_before.static_mesh_bounds_cache_size,
                "Loading Scene GPU preload is idempotent for resident mesh", checks);
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

        bool RunExtendedUIValidation(
            Rendering::DX12::D3D12DeviceContext& context, int& checks)
        {
            if (!Check(context.EnsureUIPreviewTarget(64, 64),
                "DX12 UI Preview target create", checks))
                return false;

            const float clear[4]{ 0.02f, 0.03f, 0.05f, 1.0f };
            if (!Check(context.BeginFrame(clear), "DX12 UI validation BeginFrame", checks))
                return false;
            Rendering::DX12::D3D12FrameConstants frame_constants;
            if (!Check(context.SubmitFrameConstants(frame_constants),
                "DX12 UI validation frame constants", checks))
                return false;

            {
                static constexpr const char* visual_labels[] =
                {
                    "DX12 UI Sprite GPU draw",
                    "DX12 UI Image GPU draw",
                    "DX12 UI UV GPU draw",
                    "DX12 UI Atlas GPU draw",
                    "DX12 UI Shape GPU draw"
                };
                for (std::size_t visual = 0; visual < std::size(visual_labels); ++visual)
                {
                    Rendering::DX12::D3D12UIFrame frame;
                    frame.target_width = context.Width();
                    frame.target_height = context.Height();
                    auto batch = MakeValidationUiBatch(
                        frame.target_width, frame.target_height,
                        4.0f + static_cast<float>(visual));
                    if (visual == 0u)
                    {
                        batch.texture_key = "__dx12_white";
                    }
                    else if (visual == 1u)
                    {
                        // 基本色は頂点色が正本。定数側に単色の fill_color は無い。
                        for (auto& vertex : batch.vertices)
                            vertex.color = { 0.8f, 0.6f, 0.3f, 1.0f };
                    }
                    else if (visual == 2u)
                    {
                        for (auto& vertex : batch.vertices)
                            vertex.uv = { vertex.uv.x * 0.5f, vertex.uv.y * 0.5f };
                    }
                    else if (visual == 3u)
                    {
                        for (auto& vertex : batch.vertices)
                            vertex.uv_bounds = { 0.125f, 0.25f, 0.75f, 0.5f };
                    }
                    else
                    {
                        batch.constants.mode.x = 2.0f;
                        batch.constants.fill_parameters.w = 1.0f;
                        batch.constants.fill_color_2 = { 0.2f, 0.8f, 1.0f, 1.0f };
                    }
                    frame.batches.push_back(std::move(batch));
                    if (!Check(context.DrawRuntimeUI(frame), visual_labels[visual], checks))
                        return false;
                }
            }

            {
                Rendering::DX12::D3D12UIFrame frame;
                frame.target_width = context.Width();
                frame.target_height = context.Height();
                constexpr float cell = 12.0f;
                for (int y = 0; y < 3; ++y)
                {
                    for (int x = 0; x < 3; ++x)
                    {
                        Rendering::DX12::D3D12UIBatch batch;
                        batch.texture_key = "__dx12_white";
                        batch.constants.screen_size = {
                            static_cast<float>(frame.target_width),
                            static_cast<float>(frame.target_height), 0, 0 };
                        const float left = 4.0f + cell * static_cast<float>(x);
                        const float top = 4.0f + cell * static_cast<float>(y);
                        const auto vertices = MakeValidationQuad(
                            left, top, left + cell, top + cell,
                            { 0.85f, 0.85f, 0.9f, 0.75f });
                        batch.vertices.assign(vertices.begin(), vertices.end());
                        frame.batches.push_back(std::move(batch));
                    }
                }
                if (!Check(frame.batches.size() == 9u &&
                    context.DrawRuntimeUI(frame),
                    "DX12 UI Nine Slice GPU draw (9 regions)", checks))
                    return false;
            }

            {
                const UI::RichTextResult rich = UI::RichTextParser::Parse(
                    "<color=#FF8040>日</color>本\n<b>語</b>", true, 24.0f);
                const bool has_bold_character = std::any_of(
                    rich.characters.begin(), rich.characters.end(),
                    [](const UI::RichTextCharacter& character)
                    {
                        return character.style.bold;
                    });
                const bool has_newline_character = std::any_of(
                    rich.characters.begin(), rich.characters.end(),
                    [](const UI::RichTextCharacter& character)
                    {
                        return character.codepoint == '\n';
                    });
                if (!Check(rich.markup_valid && has_bold_character,
                    "DX12 UI RichText parser", checks))
                    return false;
                const bool has_japanese_character = std::any_of(
                    rich.characters.begin(), rich.characters.end(),
                    [](const UI::RichTextCharacter& character)
                    {
                        return character.codepoint >= 0x3000u;
                    });
                if (!Check(has_japanese_character && rich.display_character_count >= 4,
                    "DX12 UI Japanese UTF-8 parser", checks))
                    return false;
                if (!Check(has_newline_character,
                    "DX12 UI newline parser", checks))
                    return false;

                UI::FontAtlas cpu_atlas;
                if (!Check(cpu_atlas.InitializeCpuOnly(),
                    "DX12 UI fallback Font Atlas CPU initialization", checks))
                    return false;
                Components::UITextComponent text_component;
                text_component.text = "一行目\n二行目";
                text_component.font_size = 24.0f;
                text_component.word_wrap = true;
                text_component.rich_text = false;
                cpu_atlas.BuildGlyphs(text_component, 48.0f, 72.0f, nullptr);
                const bool multiple_lines = std::any_of(
                    text_component.Glyphs().begin(), text_component.Glyphs().end(),
                    [&text_component](const Components::UITextComponent::GlyphQuad& glyph)
                    {
                        return !text_component.Glyphs().empty() &&
                            glyph.position.y != text_component.Glyphs().front().position.y;
                    });
                if (!Check(text_component.DisplayCharacterCount() >= 6 &&
                    !text_component.Glyphs().empty(),
                    "DX12 UI Text glyph layout", checks))
                    return false;
                if (!Check(multiple_lines,
                    "DX12 UI multiline layout", checks))
                    return false;

                Rendering::DX12::D3D12UIFontAtlasSource atlas;
                if (!Check(cpu_atlas.CopyActiveAtlas(atlas.key, atlas.rgba,
                    atlas.width, atlas.height, atlas.revision),
                    "DX12 UI fallback Font Atlas snapshot", checks))
                    return false;
                if (!Check(!atlas.key.empty() && !atlas.rgba.empty() &&
                    atlas.width != 0u && atlas.height != 0u,
                    "DX12 UI fallback Font resolved", checks))
                    return false;

                Rendering::DX12::D3D12UIFrame frame;
                frame.target_width = context.Width();
                frame.target_height = context.Height();
                const float atlas_width = static_cast<float>(atlas.width);
                const float atlas_height = static_cast<float>(atlas.height);
                const std::string atlas_key = atlas.key;
                frame.font_atlases.push_back(std::move(atlas));
                auto text = MakeValidationUiBatch(frame.target_width, frame.target_height, 8.0f);
                text.texture_key = atlas_key;
                text.constants.mode.y = 1.0f;
                text.constants.atlas_size = {
                    atlas_width, atlas_height,
                    atlas_width > 0.0f ? 1.0f / atlas_width : 0.0f,
                    atlas_height > 0.0f ? 1.0f / atlas_height : 0.0f };
                frame.batches.push_back(std::move(text));
                if (!Check(context.DrawRuntimeUI(frame) &&
                    context.UIFontTextureCacheSize() >= 1u,
                    "DX12 UI Text fallback Font Atlas GPU path", checks))
                    return false;
            }

            {
                Rendering::DX12::D3D12UIFrame frame;
                frame.target_width = context.Width();
                frame.target_height = context.Height();
                auto masked = MakeValidationUiBatch(frame.target_width, frame.target_height, 6.0f);
                masked.clip_enabled = true;
                masked.clip_count = 2;
                masked.clips[0].parameters = { 1, 0, 1.5f, 0.15f };
                masked.clips[0].bounds = { 32, 32, 24, 24 };
                masked.clips[0].shape = { 4, 0.5f, 0, 0 };
                masked.clips[1].parameters = { 5, 1, 2.0f, 0.2f };
                masked.clips[1].bounds = { 32, 32, 20, 20 };
                masked.clips[1].shape = { 5, 0.4f, 20, 0 };
                masked.clip = masked.clips[0];
                masked.constants.clip_state.x = 2.0f;
                masked.constants.clip_parameters = masked.clips[0].parameters;
                masked.constants.clip_bounds = masked.clips[0].bounds;
                masked.constants.clip_shape = masked.clips[0].shape;
                masked.constants.clip_parameters_extra[0] = masked.clips[1].parameters;
                masked.constants.clip_bounds_extra[0] = masked.clips[1].bounds;
                masked.constants.clip_shapes_extra[0] = masked.clips[1].shape;
                masked.mask_enabled = true;
                masked.mask_count = 4;
                for (std::size_t mask = 0; mask < masked.mask_count; ++mask)
                {
                    masked.masks[mask].texture_key = "__dx12_white";
                    masked.masks[mask].luma = mask == 1u || mask == 3u;
                    masked.masks[mask].invert = mask == 2u;
                    masked.masks[mask].operation = static_cast<std::int32_t>(
                        mask == 0u ? 0u : mask == 1u ? 2u : 1u);
                    masked.masks[mask].screen_origin = { 4.0f, 4.0f };
                    masked.masks[mask].screen_inverse = { 1.0f / 56.0f, 0, 0, 1.0f / 56.0f };
                    masked.constants.mask_uvs[mask] = { 0, 0, 1, 1 };
                    float* mask_operations = &masked.constants.mask_operations.x;
                    mask_operations[mask] =
                        static_cast<float>(masked.masks[mask].operation);
                }
                masked.constants.mask_parameters.x =
                    static_cast<float>(masked.mask_count);
                masked.constants.mask_luma = { 0, 1, 0, 1 };
                masked.constants.mask_inverts = { 0, 0, 1, 0 };
                if (!Check(masked.clip_enabled && masked.clip_count >= 1u,
                    "DX12 UI Rectangle clip mask command", checks))
                    return false;
                if (!Check(masked.clip_count >= 2u &&
                    masked.clips[1].shape.x != masked.clips[0].shape.x,
                    "DX12 UI Shape clip mask command", checks))
                    return false;
                if (!Check(masked.mask_enabled && !masked.masks[0].texture_key.empty(),
                    "DX12 UI Image mask command", checks))
                    return false;
                if (!Check(!masked.masks[0].luma,
                    "DX12 UI Object Alpha mask command", checks))
                    return false;
                if (!Check(masked.masks[1].luma,
                    "DX12 UI Object Luma mask command", checks))
                    return false;
                if (!Check(masked.clip_count == 2u && masked.mask_count == 4u,
                    "DX12 UI Nested Mask command", checks))
                    return false;
                if (!Check(masked.masks[1].operation == 2 &&
                    masked.masks[2].operation == 1,
                    "DX12 UI multiple Matte operation command", checks))
                    return false;
                if (!Check(masked.masks[2].invert,
                    "DX12 UI Mask invert command", checks))
                    return false;
                if (!Check(masked.clips[0].parameters.z > 0.0f &&
                    masked.clips[1].parameters.z > 0.0f,
                    "DX12 UI Mask softness command", checks))
                    return false;
                frame.batches.push_back(std::move(masked));
                if (!Check(context.DrawRuntimeUI(frame),
                    "DX12 UI nested clip/mask GPU draw", checks))
                    return false;
            }

            {
                static constexpr const char* control_labels[] =
                {
                    "DX12 UI ScrollView visual GPU draw",
                    "DX12 UI InputField visual GPU draw",
                    "DX12 UI Button visual GPU draw",
                    "DX12 UI Toggle visual GPU draw",
                    "DX12 UI Slider visual GPU draw",
                    "DX12 UI Selectable visual GPU draw"
                };
                for (std::size_t index = 0; index < std::size(control_labels); ++index)
                {
                    Rendering::DX12::D3D12UIFrame frame;
                    frame.target_width = context.Width();
                    frame.target_height = context.Height();
                    const float inset = 3.0f + static_cast<float>(index) * 2.5f;
                    auto batch = MakeValidationUiBatch(
                        frame.target_width, frame.target_height, inset);
                    batch.constants.fill_color_2 = {
                        0.2f + 0.1f * static_cast<float>(index), 0.4f, 0.8f, 0.85f };
                    frame.batches.push_back(std::move(batch));
                    if (!Check(context.DrawRuntimeUI(frame), control_labels[index], checks))
                        return false;
                }
            }

            constexpr std::uint32_t effect_count = static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::Count);
            if (!Check(effect_count == 86u,
                "DX12 UIEffectKind Count sentinel is 86", checks))
                return false;
            for (std::uint32_t kind = 0; kind < effect_count; ++kind)
            {
                Rendering::DX12::D3D12UIFrame frame;
                frame.target_width = context.Width();
                frame.target_height = context.Height();
                frame.requires_offscreen = true;
                frame.capture_backdrop = true;
                frame.preserve_output = true;
                frame.batches.push_back(
                    MakeValidationUiBatch(frame.target_width, frame.target_height, 10.0f));
                Rendering::DX12::D3D12UIEffectCommand effect;
                effect.kind = kind;
                effect.radius = 2.0f;
                effect.intensity = 0.75f;
                effect.threshold = 0.1f;
                effect.amount = 0.6f;
                effect.progress = 0.45f;
                effect.softness = 0.1f;
                effect.time = 1.25f;
                effect.speed = 0.5f;
                effect.seed = 3.0f;
                effect.direction = { 0.75f, 0.25f };
                effect.auxiliary_texture_key = "__dx12_white";
                effect.region_enabled = true;
                effect.region_mask_texture_key = "__dx12_white";
                effect.effect_region_params = { 0.5f, 0.5f, 0.35f, 0.35f };
                effect.effect_region_settings = { 0.15f, 0.03f, 1.0f, 2.0f };
                effect.effect_region_count.x = 1.0f;
                effect.temporal = kind ==
                    static_cast<std::uint32_t>(ReplayEngine::UI::UIEffectKind::MotionBlur) ||
                    kind == static_cast<std::uint32_t>(ReplayEngine::UI::UIEffectKind::Echo) ||
                    kind == static_cast<std::uint32_t>(ReplayEngine::UI::UIEffectKind::FeedbackZoom);
                if (effect.temporal)
                    effect.history_key = 0xD1200000ull + kind;
                frame.effects.push_back(std::move(effect));
                char label[112]{};
                std::snprintf(label, sizeof(label),
                    "DX12 UI Effect kind %u actual PSO/GPU draw", kind);
                if (!Check(context.DrawRuntimeUI(frame), label, checks))
                    return false;
            }
            if (!Check(context.RuntimeUIEffectHistoryCount() >= 3u,
                "DX12 UI temporal histories created", checks))
                return false;

            {
                Rendering::DX12::D3D12UIFrame frame;
                frame.target_width = context.Width();
                frame.target_height = context.Height();
                frame.requires_offscreen = true;
                frame.capture_backdrop = true;
                frame.preserve_output = true;
                auto batch = MakeValidationUiBatch(
                    frame.target_width, frame.target_height, 5.0f);
                batch.effect_group = 0;
                frame.batches.push_back(std::move(batch));
                Rendering::DX12::D3D12UIEffectCommand first;
                first.kind = static_cast<std::uint32_t>(
                    ReplayEngine::UI::UIEffectKind::Blur);
                first.radius = 3.0f;
                first.region_enabled = true;
                first.effect_region_count.x = 2.0f;
                first.effect_region_params = { 0.35f, 0.5f, 0.25f, 0.3f };
                first.effect_region_settings = { 0, 0.04f, 1, 0 };
                first.effect_region_extra_params[0] = { 0.7f, 0.5f, 0.18f, 0.25f };
                first.effect_region_extra_settings[0] = { 0, 0.05f, 1, 4 };
                Rendering::DX12::D3D12UIEffectCommand second;
                second.kind = static_cast<std::uint32_t>(
                    ReplayEngine::UI::UIEffectKind::Glow);
                second.intensity = 0.5f;
                Rendering::DX12::D3D12UIEffectGroup group;
                group.first_batch = 0;
                group.batch_count = 1;
                group.effects = { first, second };
                group.capture_backdrop = true;
                group.composite_scissor = { 2, 2,
                    static_cast<LONG>(frame.target_width - 2u),
                    static_cast<LONG>(frame.target_height - 2u) };
                group.composite_scissor_enabled = true;
                frame.effect_groups.push_back(std::move(group));
                if (!Check(context.DrawRuntimeUI(frame),
                    "DX12 UI Backdrop/multi-effect/region/expand-bounds GPU path", checks))
                    return false;
            }

            {
                Rendering::DX12::D3D12UIFrame preview;
                preview.target_width = 64;
                preview.target_height = 64;
                preview.requires_offscreen = true;
                preview.batches.push_back(MakeValidationUiBatch(64, 64, 7.0f));
                if (!Check(context.DrawRuntimeUIPreview(preview),
                    "DX12 UI Preview GPU draw", checks))
                    return false;
            }

            {
                Rendering::DX12::D3D12UIFrame temporal;
                temporal.target_width = context.Width();
                temporal.target_height = context.Height();
                temporal.requires_offscreen = true;
                temporal.capture_backdrop = true;
                temporal.batches.push_back(
                    MakeValidationUiBatch(temporal.target_width, temporal.target_height, 9.0f));
                Rendering::DX12::D3D12UIEffectCommand echo;
                echo.kind = static_cast<std::uint32_t>(
                    ReplayEngine::UI::UIEffectKind::Echo);
                echo.temporal = true;
                echo.history_key = 0xD12E0001ull;
                echo.intensity = 0.4f;
                temporal.effects.push_back(echo);
                if (!Check(context.DrawRuntimeUI(temporal),
                    "DX12 UI temporal frame N", checks))
                    return false;
            }

            if (!Check(context.EndFrame(), "DX12 UI validation EndFrame", checks))
                return false;

            if (!Check(context.BeginFrame(clear),
                "DX12 UI temporal second BeginFrame", checks))
                return false;
            if (!Check(context.SubmitFrameConstants(frame_constants),
                "DX12 UI temporal second frame constants", checks))
                return false;
            Rendering::DX12::D3D12UIFrame temporal_second;
            temporal_second.target_width = context.Width();
            temporal_second.target_height = context.Height();
            temporal_second.requires_offscreen = true;
            temporal_second.capture_backdrop = true;
            temporal_second.batches.push_back(
                MakeValidationUiBatch(temporal_second.target_width,
                    temporal_second.target_height, 9.0f));
            Rendering::DX12::D3D12UIEffectCommand echo_second;
            echo_second.kind = static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::Echo);
            echo_second.temporal = true;
            echo_second.history_key = 0xD12E0001ull;
            echo_second.intensity = 0.45f;
            temporal_second.effects.push_back(echo_second);
            if (!Check(context.DrawRuntimeUI(temporal_second),
                "DX12 UI temporal frame N+1", checks))
                return false;
            if (!Check(context.EndFrame(),
                "DX12 UI temporal second EndFrame", checks))
                return false;
            if (!Check(context.RuntimeUIEffectHistoryCount() >= 3u,
                "DX12 UI temporal history survives consecutive frames", checks))
                return false;

#ifdef USE_IMGUI
            const bool owns_imgui_context = ImGui::GetCurrentContext() == nullptr;
            if (owns_imgui_context) ImGui::CreateContext();
            if (!Check(context.InitializeImGui(),
                "DX12 composition ImGui renderer initialization", checks))
            {
                if (owns_imgui_context) ImGui::DestroyContext();
                return false;
            }

            const float composition_clear[4]{ 0.015f, 0.02f, 0.03f, 1.0f };
            if (!Check(context.BeginFrame(composition_clear),
                "DX12 composition BeginFrame", checks))
                return false;
            if (!Check(context.SubmitFrameConstants(frame_constants),
                "DX12 composition frame constants", checks))
                return false;

            Rendering::DX12::D3D12StaticSceneSubmission composition_scene;
            Rendering::DX12::D3D12StaticDrawItem composition_draw;
            composition_draw.mesh_key = kStaticValidationKey;
            composition_draw.base_color_texture_key = kDdsValidationKey;
            composition_draw.base_color = { 0.6f, 0.7f, 0.9f, 1.0f };
            composition_scene.draws.push_back(std::move(composition_draw));
            if (!Check(context.DrawScene3D(composition_scene),
                "DX12 composition Scene3D draw", checks))
                return false;
            const std::uint32_t scene_sequence =
                context.GpuPassSequence(Rendering::DX12::D3D12GpuPass::GBuffer);
            const std::uint32_t post_sequence =
                context.GpuPassSequence(Rendering::DX12::D3D12GpuPass::PostProcess);
            if (!Check(scene_sequence != 0u && post_sequence > scene_sequence,
                "DX12 composition PostProcess follows Scene3D", checks))
                return false;

            Rendering::DX12::D3D12UIFrame composition_ui;
            composition_ui.target_width = context.Width();
            composition_ui.target_height = context.Height();
            composition_ui.batches.push_back(MakeValidationUiBatch(
                composition_ui.target_width, composition_ui.target_height, 14.0f));
            if (!Check(context.DrawRuntimeUI(composition_ui),
                "DX12 composition Runtime UI draw", checks))
                return false;
            const std::uint32_t runtime_ui_sequence =
                context.GpuPassSequence(Rendering::DX12::D3D12GpuPass::RuntimeUI);
            if (!Check(runtime_ui_sequence > post_sequence,
                "DX12 composition Runtime UI follows PostProcess", checks))
                return false;

            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<float>(context.Width()),
                static_cast<float>(context.Height()));
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(2.0f, 2.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(48.0f, 32.0f), ImGuiCond_Always);
            ImGui::Begin("DX12ValidationComposition", nullptr,
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration);
            ImGui::TextUnformatted("DX12");
            ImGui::End();
            ImGui::Render();
            if (!Check(context.DrawImGui(ImGui::GetDrawData()),
                "DX12 composition ImGui draw", checks))
                return false;
            const std::uint32_t imgui_sequence =
                context.GpuPassSequence(Rendering::DX12::D3D12GpuPass::ImGui);
            if (!Check(imgui_sequence > runtime_ui_sequence,
                "DX12 composition ImGui follows Runtime UI", checks))
                return false;
            if (!Check(context.EndFrame(),
                "DX12 composition EndFrame", checks))
                return false;
            if (owns_imgui_context) ImGui::DestroyContext();
#else
            if (!Check(false, "DX12 composition ImGui requires USE_IMGUI", checks))
                return false;
#endif
            return true;
        }

        Rendering::DX12::D3D12StaticSceneSubmission MakeEffectValidationScene()
        {
            Rendering::DX12::D3D12StaticSceneSubmission scene;
            Rendering::DX12::D3D12StaticDrawItem target;
            target.mesh_key = kStaticValidationKey;
            target.owner_id = 0xD1200101ull;
            target.rendering_layer = 3u;
            target.motion_key = "validation:effect-target";
            target.base_color_texture_key = kDdsValidationKey;
            target.base_color = { 0.85f, 0.35f, 0.18f, 1.0f };
            target.cast_shadow = true;
            scene.draws.push_back(target);

            Rendering::DX12::D3D12StaticDrawItem background = target;
            background.owner_id = 0xD1200102ull;
            background.rendering_layer = 1u;
            background.motion_key = "validation:effect-background";
            background.world._41 = 0.25f;
            background.base_color = { 0.2f, 0.55f, 0.85f, 1.0f };
            scene.draws.push_back(background);

            scene.directional_light.enabled = true;
            scene.directional_light.cast_shadows = true;
            scene.directional_light.direction = { 0.3f, -1.0f, 0.25f };
            scene.directional_light.intensity = 1.0f;
            scene.directional_shadow.enabled = true;
            scene.directional_shadow.resolution = 64;
            scene.directional_shadow.split_distances = { 8, 16, 32, 64 };
            scene.directional_shadow.params = { 0.002f, 1.0f, 1.0f, 1.0f };
            scene.directional_shadow.params2 = { 64.0f, 1.0f, 0.002f, 0.0f };
            scene.directional_shadow.params3 = { 1.0f, 0.02f, 1.0f, 1.0f };
            scene.directional_shadow.texel_world = { 0.01f, 0.02f, 0.04f, 0.08f };
            for (std::uint32_t cascade = 0;
                cascade < Rendering::DX12::D3D12DirectionalShadowSubmission::CascadeCount;
                ++cascade)
            {
                scene.directional_shadow.view_projection[cascade] =
                    DirectX::XMFLOAT4X4{
                        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
            }
            return scene;
        }

        Rendering::DX12::D3D12SceneEffectSubmission MakeEffectValidationSubmission()
        {
            Rendering::DX12::D3D12SceneEffectSubmission submission;

            Rendering::DX12::D3D12ModelEffectStackSubmission model;
            model.owner_id = 0xD1200101ull;
            model.depth_mode = 0;
            Rendering::DX12::D3D12UIEffectCommand blur;
            blur.kind = static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::Blur);
            blur.radius = 2.5f;
            blur.intensity = 0.6f;
            Rendering::DX12::D3D12UIEffectCommand dissolve;
            dissolve.kind = static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::Dissolve);
            dissolve.progress = 0.45f;
            dissolve.softness = 0.08f;
            dissolve.auxiliary_texture_key = "__dx12_white";
            dissolve.region_enabled = true;
            dissolve.region_mask_texture_key = "__dx12_white";
            dissolve.effect_region_count.x = 1.0f;
            dissolve.effect_region_params = { 0.5f, 0.5f, 0.4f, 0.4f };
            dissolve.effect_region_settings = { 0, 0.02f, 1, 0 };
            model.effects = { blur, dissolve };
            submission.model_effects.push_back(std::move(model));

            Rendering::DX12::D3D12ScreenEffectStackSubmission before;
            before.owner_id = 0xD1200201ull;
            before.apply_stage = 1;
            before.target_mode = 0;
            Rendering::DX12::D3D12UIEffectCommand color;
            color.kind = static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::ColorAdjust);
            color.intensity = 0.4f;
            before.effects.push_back(color);
            submission.screen_effects.push_back(std::move(before));

            Rendering::DX12::D3D12ScreenEffectStackSubmission background;
            background.owner_id = 0xD1200202ull;
            background.apply_stage = 0;
            background.target_mode = 1;
            Rendering::DX12::D3D12UIEffectCommand vignette;
            vignette.kind = static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::Vignette);
            vignette.intensity = 0.25f;
            background.effects.push_back(vignette);
            submission.screen_effects.push_back(std::move(background));

            Rendering::DX12::D3D12ScreenEffectStackSubmission layer;
            layer.owner_id = 0xD1200203ull;
            layer.apply_stage = 0;
            layer.target_mode = 2;
            layer.target_rendering_layer_mask = 1u << 3u;
            Rendering::DX12::D3D12UIEffectCommand glow;
            glow.kind = static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::Glow);
            glow.intensity = 0.35f;
            layer.effects.push_back(glow);
            submission.screen_effects.push_back(std::move(layer));

            Rendering::DX12::D3D12ScreenEffectStackSubmission temporal;
            temporal.owner_id = 0xD1200204ull;
            temporal.apply_stage = 0;
            temporal.target_mode = 0;
            Rendering::DX12::D3D12UIEffectCommand feedback;
            feedback.kind = static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::FeedbackZoom);
            feedback.temporal = true;
            feedback.history_key = 0xD12F0001ull;
            feedback.intensity = 0.2f;
            feedback.amount = 0.2f;
            temporal.effects.push_back(feedback);
            submission.screen_effects.push_back(std::move(temporal));
            return submission;
        }

        bool RunExtendedEffectValidation(
            Rendering::DX12::D3D12DeviceContext& context, int& checks)
        {
            const Rendering::DX12::D3D12SceneEffectSubmission validation_effects =
                MakeEffectValidationSubmission();
            if (!Check(!validation_effects.model_effects.empty() &&
                validation_effects.model_effects.front().effects.size() == 2u,
                "DX12 Model Effect multi-effect chain submission", checks))
                return false;
            if (!Check(validation_effects.model_effects.front().effects.front().radius > 0.0f,
                "DX12 Model Effect expand-bounds source", checks))
                return false;
            const auto& dissolve = validation_effects.model_effects.front().effects[1];
            if (!Check(dissolve.region_enabled && dissolve.effect_region_count.x >= 1.0f,
                "DX12 Model Effect region submission", checks))
                return false;
            if (!Check(!dissolve.region_mask_texture_key.empty(),
                "DX12 Model Effect mask submission", checks))
                return false;
            if (!Check(dissolve.kind == static_cast<std::uint32_t>(
                ReplayEngine::UI::UIEffectKind::Dissolve),
                "DX12 Model Effect shadow-coverage kind submission", checks))
                return false;
            if (!Check(validation_effects.screen_effects.size() >= 4u &&
                validation_effects.screen_effects[0].effects.size() == 1u,
                "DX12 Screen Effect single-effect submission", checks))
                return false;
            if (!Check(validation_effects.screen_effects[1].target_mode == 1,
                "DX12 Screen Effect backdrop/background target submission", checks))
                return false;
            if (!Check(validation_effects.screen_effects[2].target_mode == 2 &&
                validation_effects.screen_effects[2].target_rendering_layer_mask == (1u << 3u),
                "DX12 Screen Effect rendering-layer target submission", checks))
                return false;
            if (!Check(validation_effects.screen_effects[3].effects.front().temporal,
                "DX12 Screen Effect temporal submission", checks))
                return false;

            const float clear[4]{ 0.04f, 0.04f, 0.06f, 1.0f };
            Rendering::DX12::D3D12FrameConstants frame_constants;
            for (int frame_index = 0; frame_index < 2; ++frame_index)
            {
                if (!Check(context.BeginFrame(clear),
                    frame_index == 0
                        ? "DX12 Model/Screen Effect frame N BeginFrame"
                        : "DX12 Model/Screen Effect frame N+1 BeginFrame",
                    checks))
                    return false;
                if (!Check(context.SubmitFrameConstants(frame_constants),
                    "DX12 Model/Screen Effect frame constants", checks))
                    return false;
                context.SetSceneEffects(validation_effects);
                Rendering::DX12::D3D12StaticSceneSubmission scene =
                    MakeEffectValidationScene();
                if (!Check(context.DrawScene3D(scene),
                    "DX12 Model/Screen Effect GPU draw", checks))
                    return false;
                if (!Check(context.LastModelEffectStackCount() == 1u,
                    "DX12 Model Effect stack consumed by renderer", checks))
                    return false;
                if (!Check(context.LastScreenEffectStackCount() == 4u,
                    "DX12 Screen Effect stacks consumed across before/after stages", checks))
                    return false;
                if (!Check(context.LastShadowCoverageDrawCount() >= 1u,
                    "DX12 face-removing Model Effect changes shadow caster coverage", checks))
                    return false;
                if (!Check(context.GpuPassSequence(
                    Rendering::DX12::D3D12GpuPass::ModelEffect) != 0u,
                    "DX12 Model Effect GPU pass recorded", checks))
                    return false;
                if (!Check(context.GpuPassSequence(
                    Rendering::DX12::D3D12GpuPass::ScreenEffect) != 0u,
                    "DX12 Screen Effect GPU pass recorded", checks))
                    return false;
                if (!Check(context.GpuPassSequence(
                    Rendering::DX12::D3D12GpuPass::ShadowDirectional) != 0u,
                    "DX12 Effect shadow-coverage shadow pass recorded", checks))
                    return false;
                if (!Check(context.EndFrame(),
                    frame_index == 0
                        ? "DX12 Model/Screen Effect frame N EndFrame"
                        : "DX12 Model/Screen Effect frame N+1 EndFrame",
                    checks))
                    return false;
            }

            if (!Check(context.SceneEffectHistoryCount() >= 1u,
                "DX12 Screen Effect temporal history across two frames", checks))
                return false;
            context.SetSceneEffects(Rendering::DX12::D3D12SceneEffectSubmission{});
            return Check(Rendering::DX12::D3D12GpuPassCount >= 13u,
                "DX12 ModelEffect/ScreenEffect pass markers are registered", checks);
        }

        bool RunReloadValidation(
            Rendering::DX12::D3D12DeviceContext& context, int& checks)
        {
            const std::filesystem::path preset_path =
                std::filesystem::path("Saved") / "Validation" /
                "dx12_reload_validation.replayeffect";
            std::error_code filesystem_error;
            std::filesystem::create_directories(preset_path.parent_path(), filesystem_error);
            if (filesystem_error)
                return Check(false, "DX12 reload validation directory create", checks);

            Rendering::Effects::EffectPresetAsset preset;
            UI::UIEffect preset_effect;
            preset_effect.kind = static_cast<int>(UI::UIEffectKind::Glow);
            preset_effect.intensity = 0.4f;
            preset.effects.push_back(preset_effect);
            std::string error;
            if (!Check(preset.SaveToFile(preset_path, error),
                "DX12 reload Effect Preset create", checks))
                return false;

            Scene::Serialization::SceneData scene_data;
            std::ostringstream scene_stream;
            if (!Check(Scene::Serialization::SceneSerializer::WriteText(
                scene_data, scene_stream, error),
                "DX12 reload Scene serialization source", checks))
                return false;
            const std::string serialized_scene = scene_stream.str();

            std::uint32_t stable_descriptors = 0;
            std::size_t stable_font_cache = 0;
            std::uint64_t stable_upload_used = 0;
            for (int cycle = 0; cycle < 3; ++cycle)
            {
                Rendering::Effects::EffectPresetAsset loaded_preset;
                error.clear();
                if (!Check(loaded_preset.LoadFromFile(preset_path, error) &&
                    loaded_preset.effects.size() == 1u,
                    "DX12 reload Effect Preset read", checks))
                    return false;

                Scene::Serialization::SceneData reloaded_scene;
                std::istringstream scene_input(serialized_scene);
                error.clear();
                if (!Check(Scene::Serialization::SceneSerializer::ReadText(
                    reloaded_scene, scene_input, error),
                    "DX12 reload Scene read", checks))
                    return false;

                if (!Check(context.ClearStaticAssetCaches(),
                    "DX12 reload texture/mesh/shader cache clear", checks))
                    return false;

                const float clear[4]{ 0.03f, 0.05f, 0.07f, 1.0f };
                char frame_label[96]{};
                std::snprintf(frame_label, sizeof(frame_label),
                    "reload texture/scene cycle %d", cycle + 1);
                if (!RunDeviceFrameValidation(context, clear, frame_label, checks))
                    return false;

                const float ui_clear[4]{ 0.01f, 0.01f, 0.02f, 1.0f };
                if (!Check(context.BeginFrame(ui_clear),
                    "DX12 reload font BeginFrame", checks))
                    return false;
                Rendering::DX12::D3D12FrameConstants frame_constants;
                if (!Check(context.SubmitFrameConstants(frame_constants),
                    "DX12 reload font frame constants", checks))
                    return false;
                Rendering::DX12::D3D12UIFrame ui;
                ui.target_width = context.Width();
                ui.target_height = context.Height();
                Rendering::DX12::D3D12UIFontAtlasSource atlas;
                atlas.key = "validation:reload-font";
                atlas.width = 8;
                atlas.height = 8;
                atlas.revision = static_cast<std::uint64_t>(cycle + 1);
                atlas.rgba.resize(8u * 8u * 4u, 255u);
                ui.font_atlases.push_back(atlas);
                auto text = MakeValidationUiBatch(
                    ui.target_width, ui.target_height, 10.0f);
                text.texture_key = atlas.key;
                text.constants.mode.y = 1.0f;
                ui.batches.push_back(std::move(text));
                if (!Check(context.DrawRuntimeUI(ui),
                    "DX12 reload Font Atlas regeneration", checks))
                    return false;
                const std::uint64_t upload_used = context.CurrentFrameUploadUsed();
                if (!Check(context.EndFrame(),
                    "DX12 reload font EndFrame", checks))
                    return false;
                if (!Check(context.WaitForGpu(),
                    "DX12 reload cycle GPU drain", checks))
                    return false;

                const std::uint32_t used =
                    context.ResourceDescriptorAllocator().Used();
                const std::size_t font_cache = context.UIFontTextureCacheSize();
                if (cycle == 1)
                {
                    stable_descriptors = used;
                    stable_font_cache = font_cache;
                    stable_upload_used = upload_used;
                }
                if (cycle >= 2)
                {
                    if (!Check(used == stable_descriptors,
                        "DX12 reload descriptor usage delta is zero after warm cycle",
                        checks))
                        return false;
                    if (!Check(font_cache == stable_font_cache,
                        "DX12 reload Font Atlas cache count is stable", checks))
                        return false;
                    if (!Check(upload_used == stable_upload_used,
                        "DX12 reload frame upload usage delta is zero after warm cycle",
                        checks))
                        return false;
                }
            }
            std::filesystem::remove(preset_path, filesystem_error);
            return true;
        }
    }

    int RunHeadlessDX12Validation(const char* command_line)
    {
        if (command_line == nullptr)
            return -1;

        const int golden_compare_result = RunGoldenComparison(command_line);
        if (golden_compare_result >= 0)
            return golden_compare_result;

        constexpr const char* kDx12ValidationAliases[] =
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
        const bool canonical_requested =
            HasCommandLineToken(command_line, "--validate-dx12");
        const bool ui_requested =
            HasCommandLineToken(command_line, "--validate-dx12-ui");
        const bool effect_requested =
            HasCommandLineToken(command_line, "--validate-dx12-effect");
        const bool resize_requested =
            HasCommandLineToken(command_line, "--validate-dx12-resize");
        const bool reload_requested =
            HasCommandLineToken(command_line, "--validate-dx12-reload");
        bool legacy_alias_requested = false;
        const char* matched_alias = nullptr;
        for (const char* validation_command : kDx12ValidationAliases)
        {
            if (HasCommandLineToken(command_line, validation_command))
            {
                legacy_alias_requested = true;
                matched_alias = validation_command;
                break;
            }
        }
        const bool any_dx12_validation = canonical_requested || ui_requested ||
            effect_requested || resize_requested || reload_requested ||
            legacy_alias_requested;
        if (!any_dx12_validation)
            return -1;
        if (legacy_alias_requested && matched_alias != nullptr)
        {
            std::fprintf(stderr,
                "DX12 validation alias: %s -> --validate-dx12 (same base suite)\n",
                matched_alias);
        }

        // Releaseでも明示指定されたGPU-Based Validationを有効化できるようにする。
        const bool gpu_validation_requested =
            HasCommandLineToken(command_line, "--validate-dx12-gpu") ||
            HasCommandLineToken(command_line, "--dx12-gpu-validation=on");

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
        // GPU-Based Validationを要求した場合はReleaseでもDebug Layerを初期化する。
        const bool requested_debug_layer = enable_debug || gpu_validation_requested;
        ok = Check(context.Initialize(window, 64, 64, requested_debug_layer, false, true,
            gpu_validation_requested, true),
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
        if (ok && requested_debug_layer)
        {
            std::fprintf(stderr,
                "DX12 debug requested: layer=%d gpu_validation=%d dred=%d\n",
                context.DebugLayerEnabled() ? 1 : 0,
                context.GpuValidationEnabled() ? 1 : 0,
                context.DredEnabled() ? 1 : 0);
        }
        if (ok)
            ok = RunModelScreenBoundsValidation(context, checks);

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
        if (ok && ui_requested)
            ok = RunExtendedUIValidation(context, checks);
        if (ok && effect_requested)
            ok = RunExtendedEffectValidation(context, checks);
        const std::pair<std::uint32_t, std::uint32_t> resize_cases[] =
        {
            { 96u, 64u },
            { 48u, 96u },
            { 1u, 1u },
            { 64u, 64u },
        };
        const std::size_t resize_case_count = resize_requested
            ? std::size(resize_cases) : 1u;
        std::uint32_t stable_resize_descriptor_used = 0;
        for (std::size_t resize_index = 0; ok && resize_index < resize_case_count; ++resize_index)
        {
            const auto [resize_width, resize_height] = resize_cases[resize_index];
            char resize_label[96]{};
            std::snprintf(resize_label, sizeof(resize_label),
                "Resize %ux%u", resize_width, resize_height);
            const std::uint64_t resize_fence_before = context.LastSignaledFenceValue();
            ok = Check(context.Resize(resize_width, resize_height), resize_label, checks);
            if (ok && resize_requested)
            {
                ok = Check(context.CompletedFenceValue() >= resize_fence_before,
                    "DX12 Resize waits for the submitted fence before RT/descriptor reuse",
                    checks);
            }
            if (ok && resize_requested)
            {
                ok = Check(context.RuntimeUIEffectHistoryCount() == 0u &&
                    context.UIPreviewEffectHistoryCount() == 0u &&
                    context.SceneEffectHistoryCount() == 0u,
                    "DX12 Resize discards Runtime/UI Preview/Scene Effect temporal history",
                    checks);
            }
            if (ok)
            {
                ok = Check(context.SceneViewTarget().IsValid() &&
                    context.GameViewTarget().IsValid() &&
                    context.SceneViewTarget().width == resize_width &&
                    context.SceneViewTarget().height == resize_height &&
                    context.GameViewTarget().width == resize_width &&
                    context.GameViewTarget().height == resize_height,
                    "offscreen targets recreated by Resize", checks);
            }
            if (ok)
            {
                char label[96]{};
                std::snprintf(label, sizeof(label),
                    "resized frame %ux%u", resize_width, resize_height);
                ok = RunDeviceFrameValidation(context,
                    colors[(resize_index + 1u) % 4u], label, checks);
            }
            if (ok && resize_requested)
            {
                const std::uint32_t used =
                    context.ResourceDescriptorAllocator().Used();
                if (resize_index == 0u)
                    stable_resize_descriptor_used = used;
                else
                    ok = Check(used == stable_resize_descriptor_used,
                        "DX12 Resize descriptor usage remains constant", checks);
            }
        }

        if (ok && context.IsInitialized())
            ok = Check(context.WaitForGpu(), "final GPU drain", checks);
        else if (context.IsInitialized())
            (void)context.WaitForGpu();
        if (ok && reload_requested)
            ok = RunReloadValidation(context, checks);
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
