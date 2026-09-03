#include "D3D12DeviceContext.h"
#include "D3D12ObjectName.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        constexpr DXGI_FORMAT kScene3DGBufferFormats[kScene3DGBufferCount] =
        {
            DXGI_FORMAT_R16G16B16A16_FLOAT, // BaseColorとシェーディングモデル
            DXGI_FORMAT_R16G16B16A16_FLOAT, // Emissive
            DXGI_FORMAT_R16G16B16A16_FLOAT, // Normalと深度互換値
            DXGI_FORMAT_R8G8B8A8_UNORM,     // Metallic/Roughness/AO/モデル
            DXGI_FORMAT_R16G16_FLOAT,       // モーションベクター
            DXGI_FORMAT_R16G16B16A16_UNORM, // Toonの色3つと指数2つをbit packして運ぶ
        };

        // ライティングパスの root parameter 番号。GBuffer 0..4 は既存の t0..t4 を動かさず、
        // 追加分は影配列の t6/t7 を避けて t8 以降へ置く。
        constexpr UINT kScene3DLightingGBufferRootSlot[kScene3DGBufferCount] =
        {
            1, 2, 3, 4, 5, 9,
        };
        constexpr UINT kScene3DLightingSrvRangeCount = 15;
        constexpr DXGI_FORMAT kScene3DDepthResourceFormat = DXGI_FORMAT_R32_TYPELESS;
        constexpr DXGI_FORMAT kScene3DDepthDsvFormat = DXGI_FORMAT_D32_FLOAT;
        constexpr DXGI_FORMAT kScene3DDepthSrvFormat = DXGI_FORMAT_R32_FLOAT;
        constexpr DXGI_FORMAT kScene3DSceneTargetFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr DXGI_FORMAT kScene3DBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

        void SceneDebugMessage(const char* message) noexcept
        {
            if (message == nullptr) return;
            OutputDebugStringA(message);
            // GUI subsystem では OutputDebugString が読めないので stderr にも出す。
            std::fprintf(stderr, "%s", message);
        }

        bool LightingTraceEnabled() noexcept
        {
            static const bool enabled = []
            {
                // MSVC は getenv を C4996 で拒否するため _dupenv_s を使う。
                char* value = nullptr;
                std::size_t length = 0;
                if (_dupenv_s(&value, &length, "REPLAY_LIGHTING_TRACE") != 0 ||
                    value == nullptr)
                    return false;
                const bool on = value[0] != '\0' && std::strcmp(value, "0") != 0;
                std::free(value);
                return on;
            }();
            return enabled;
        }

        struct Scene3DObjectConstants final
        {
            DirectX::XMFLOAT4X4 world{};
            DirectX::XMFLOAT4X4 previous_world{};
            DirectX::XMFLOAT4 morph{};
        };

        struct Scene3DSceneConstants final
        {
            DirectX::XMFLOAT4X4 view_projection{};
            DirectX::XMFLOAT4X4 previous_view_projection{};
        };

        struct Scene3DLayerConstants final
        {
            DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
            DirectX::XMFLOAT4 params{};
        };

        struct Scene3DMaterialConstants final
        {
            DirectX::XMFLOAT4 base_color{ 1, 1, 1, 1 };
            DirectX::XMFLOAT4 emissive_strength{};
            DirectX::XMFLOAT4 normal_adjust_center{};
            DirectX::XMFLOAT4 normal_adjust_params{};
            DirectX::XMFLOAT4 surface_params{ 0, 0.55f, 1, 0.5f };
            DirectX::XMFLOAT4 render_params{};
            // BuiltIn シェーダ用の汎用枠。x=効果ID、y/z/w はその効果の引数。
            // BuiltIn は自前 PSO を持てないため、固有表現はここへ載せる。
            DirectX::XMFLOAT4 builtin_params{};
            // Toon の追加枠。rgb=ShadowTint、w=RimPower。既定は効果オフ。
            DirectX::XMFLOAT4 builtin_params1{};
            // Toon の追加枠。rgb=RimColor、w=SpecularPower。
            DirectX::XMFLOAT4 builtin_params2{ 0, 0, 0, 1 };
            // Toon の追加枠。rgb=SpecularTint、w=Model Effect の Deferred 整合フラグ。
            DirectX::XMFLOAT4 builtin_params3{};
        };

        struct Scene3DPointLightGpu final
        {
            DirectX::XMFLOAT4 position_range{};
            DirectX::XMFLOAT4 color_intensity{};
            // x=開始Slice、y=強度、z=有効、w=予約
            DirectX::XMFLOAT4 shadow{};
        };

        struct Scene3DSpotLightGpu final
        {
            DirectX::XMFLOAT4 position_range{};
            DirectX::XMFLOAT4 direction_inner{};
            DirectX::XMFLOAT4 color_outer{};
            DirectX::XMFLOAT4 params{};
        };

        struct Scene3DLocalShadowSliceGpu final
        {
            DirectX::XMFLOAT4X4 view_projection{};
            DirectX::XMFLOAT4 params{};
        };

        struct Scene3DLightingConstants final
        {
            DirectX::XMFLOAT4X4 inverse_view_projection{};
            DirectX::XMFLOAT4X4 view{};
            DirectX::XMFLOAT4 camera_position{};
            DirectX::XMFLOAT4 directional_direction_intensity{};
            // rgb=色、w=Directional Lightの有効状態
            DirectX::XMFLOAT4 directional_color_flags{};
            Scene3DPointLightGpu point_lights[8]{};
            Scene3DSpotLightGpu spot_lights[4]{};
            DirectX::XMUINT4 counts{};
            DirectX::XMFLOAT4X4 csm_view_projection[4]{};
            DirectX::XMFLOAT4 csm_split_distances{};
            DirectX::XMFLOAT4 csm_params{};
            DirectX::XMFLOAT4 csm_params2{};
            DirectX::XMFLOAT4 csm_params3{};
            DirectX::XMFLOAT4 csm_texel_world{};
            Scene3DLocalShadowSliceGpu local_shadow_slices[16]{};
            DirectX::XMUINT4 shadow_flags{}; // x=CSM有効、y=Local有効、z/w=予約
            DirectX::XMUINT4 debug_flags{}; // x=DeferredDebugMode、y/z/w=予約
            DirectX::XMFLOAT4X4 previous_view_projection{};
            DirectX::XMFLOAT4X4 sky_rotation{};
            DirectX::XMFLOAT4 sky_jitter{};
            DirectX::XMFLOAT4 ibl_params{};
            DirectX::XMFLOAT4 sky_blend{};
            DirectX::XMFLOAT4 sky_motion{};
            DirectX::XMFLOAT4 cloud_layer1_params{};
            DirectX::XMFLOAT4 cloud_layer2_params{};
            DirectX::XMFLOAT4 cloud_layer1_color{};
            DirectX::XMFLOAT4 cloud_layer2_color{};
            DirectX::XMFLOAT4 star_params{};
            DirectX::XMFLOAT4 star_color{};
            DirectX::XMFLOAT4 moon_params{};
            DirectX::XMFLOAT4 moon_direction{};
            DirectX::XMFLOAT4 moon_color{};
            DirectX::XMFLOAT4X4 previous_sky_rotation{};
            // x=トゥーンへの環境光の強さ、y/z/w=予約
            DirectX::XMFLOAT4 toon_environment{};
        };

        struct Scene3DShadowObjectConstants final
        {
            DirectX::XMFLOAT4X4 world{};
            DirectX::XMFLOAT4 morph{};
            DirectX::XMFLOAT4 alpha{}; // x=モード、y=カットオフ、z/w=予約
        };

        struct Scene3DShadowPassConstants final
        {
            DirectX::XMFLOAT4X4 view_projection{};
        };

        struct Scene3DShadowCoverageConstants final
        {
            DirectX::XMFLOAT4X4 view_projection{};
            DirectX::XMFLOAT4 viewport{};
            DirectX::XMFLOAT4 rect{};
            DirectX::XMFLOAT4 control{};
            DirectX::XMFLOAT4 params0[4]{};
            DirectX::XMFLOAT4 params1[4]{};
            DirectX::XMFLOAT4 params2[4]{};
            DirectX::XMFLOAT4 params3[4]{};
            DirectX::XMFLOAT4 meta[4]{};
            DirectX::XMFLOAT4 region_params[4]{};
            DirectX::XMFLOAT4 region_settings[4]{};
        };

        struct Scene3DPostProcessConstants final
        {
            float exposure = 0.619f;
            float bloom_intensity = 0.25f;
            float bloom_threshold = 1.0f;
            float vignette_strength = 0.138f;
            float fxaa_enable = 1.0f;
            float taa_blend = 0.88f;
            float ssao_strength = 1.0f;
            float ssr_strength = 1.0f;
            float history_valid = 0.0f;
            DirectX::XMFLOAT3 alignment_padding{};
            DirectX::XMFLOAT2 screen_size{};
            DirectX::XMFLOAT2 post_flags{ 1.0f, 1.0f };
            DirectX::XMFLOAT4 color_filter{ 1, 1, 1, 1 };
            DirectX::XMFLOAT4 feature_flags{};
            DirectX::XMFLOAT4 debug_options{};
            // SSR のレイマーチはビュー空間で行う。行列はここでだけ post 側へ渡す。
            DirectX::XMFLOAT4X4 view{};
            DirectX::XMFLOAT4X4 projection{};
            DirectX::XMFLOAT4X4 inverse_projection{};
            // x=Near、y=Far、z/w=予約
            DirectX::XMFLOAT4 camera_planes{ 0.1f, 10000.0f, 0, 0 };
            DirectX::XMFLOAT4 ssao_params0{ 0.75f, 1.6f, 1.0f, 0.35f };
            DirectX::XMFLOAT4 ssao_params1{ 4.0f, 8.0f, 60.0f, 140.0f };
            DirectX::XMFLOAT4 ssao_params2{ 1.0f, 1.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 ssr_params0{ 40.0f, 0.55f, 3.0f, 32.0f };
            DirectX::XMFLOAT4 ssr_params1{ 4.0f, 0.6f, 0.08f, 0.001f };
            DirectX::XMFLOAT4 ssr_params2{ 0.0f, 1.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 taa_params0{ 1.0f, 0.0f, 48.0f, 0.0f };
        };

        static_assert(sizeof(Scene3DObjectConstants) % 16 == 0);
        static_assert(sizeof(Scene3DSceneConstants) % 16 == 0);
        static_assert(sizeof(Scene3DLayerConstants) % 16 == 0);
        static_assert(sizeof(Scene3DMaterialConstants) % 16 == 0);
        static_assert(sizeof(Scene3DMaterialConstants) == sizeof(DirectX::XMFLOAT4) * 10);
        static_assert(sizeof(Scene3DLightingConstants) % 16 == 0);
        static_assert(sizeof(Scene3DShadowObjectConstants) % 16 == 0);
        static_assert(sizeof(Scene3DShadowPassConstants) % 16 == 0);
        static_assert(sizeof(Scene3DShadowCoverageConstants) % 16 == 0);
        static_assert(sizeof(Scene3DPostProcessConstants) % 16 == 0);


        D3D12_BLEND_DESC MakeBlend(bool transparent) noexcept
        {
            D3D12_BLEND_DESC blend{};
            for (D3D12_RENDER_TARGET_BLEND_DESC& target : blend.RenderTarget)
            {
                target.BlendEnable = FALSE;
                target.LogicOpEnable = FALSE;
                target.SrcBlend = D3D12_BLEND_ONE;
                target.DestBlend = D3D12_BLEND_ZERO;
                target.BlendOp = D3D12_BLEND_OP_ADD;
                target.SrcBlendAlpha = D3D12_BLEND_ONE;
                target.DestBlendAlpha = D3D12_BLEND_ZERO;
                target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                target.LogicOp = D3D12_LOGIC_OP_NOOP;
                target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }
            if (transparent)
            {
                auto& target = blend.RenderTarget[0];
                target.BlendEnable = TRUE;
                target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                target.BlendOp = D3D12_BLEND_OP_ADD;
                target.SrcBlendAlpha = D3D12_BLEND_ONE;
                target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            }
            return blend;
        }

        D3D12_BLEND_DESC MakeLayerBlend(ShaderLayerBlend mode) noexcept
        {
            D3D12_BLEND_DESC blend = MakeBlend(false);
            auto& target = blend.RenderTarget[0];
            target.BlendEnable = TRUE;
            target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            target.SrcBlendAlpha = D3D12_BLEND_ONE;
            target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            switch (mode)
            {
            case ShaderLayerBlend::Additive:
                target.DestBlend = D3D12_BLEND_ONE;
                target.DestBlendAlpha = D3D12_BLEND_ONE;
                break;
            case ShaderLayerBlend::Multiply:
                target.SrcBlend = D3D12_BLEND_DEST_COLOR;
                target.DestBlend = D3D12_BLEND_ZERO;
                target.SrcBlendAlpha = D3D12_BLEND_ZERO;
                target.DestBlendAlpha = D3D12_BLEND_ONE;
                break;
            case ShaderLayerBlend::Alpha:
            default:
                break;
            }
            return blend;
        }

        D3D12_RASTERIZER_DESC MakeRaster(bool double_sided) noexcept
        {
            D3D12_RASTERIZER_DESC raster{};
            raster.FillMode = D3D12_FILL_MODE_SOLID;
            raster.CullMode = D3D12_CULL_MODE_NONE;
            raster.FrontCounterClockwise = TRUE;
            raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            raster.DepthClipEnable = TRUE;
            return raster;
        }

        D3D12_DEPTH_STENCIL_DESC MakeDepth(bool write) noexcept
        {
            D3D12_DEPTH_STENCIL_DESC depth{};
            depth.DepthEnable = TRUE;
            depth.DepthWriteMask = write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            depth.StencilEnable = FALSE;
            depth.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
            depth.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
            depth.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            depth.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            depth.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
            depth.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            depth.BackFace = depth.FrontFace;
            return depth;
        }

        bool SerializeRoot(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc,
            Microsoft::WRL::ComPtr<ID3D12RootSignature>& root, const wchar_t* name) noexcept
        {
            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            Microsoft::WRL::ComPtr<ID3DBlob> errors;
            if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                &blob, &errors)))
                return false;
            if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(),
                blob->GetBufferSize(), IID_PPV_ARGS(&root)))) return false;
            SetD3D12ObjectName(root.Get(), name != nullptr ? name : L"RootSignature", L"Main");
            return true;
        }
    }

    bool D3D12DeviceContext::CreateScene3DRendererResources() noexcept
    {
        const auto fail = [this](const char* stage) noexcept
        {
            SetInitializationFailure(stage, E_FAIL);
            return false;
        };
        if (device_ == nullptr) return fail("Scene3D.Device");
        D3D12ShaderCompiler compiler;
        if (!compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath()))
            return fail("Scene3D.DXC.Initialize");

        const std::filesystem::path shader_directory = std::filesystem::current_path() / "Shader";
        const auto static_vs = compiler.CompileFile(shader_directory / "dx12_static_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        const auto skinned_vs = compiler.CompileFile(shader_directory / "dx12_skinned_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        const auto static_layer_vs = compiler.CompileFile(
            shader_directory / "static_mesh_outline_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        const auto skinned_layer_vs = compiler.CompileFile(
            shader_directory / "skinned_mesh_outline_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        const auto layer_ps = compiler.CompileFile(shader_directory / "outline_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        const auto gbuffer_ps = compiler.CompileFile(shader_directory / "dx12_gbuffer_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        const auto depth_alpha_ps = compiler.CompileFile(
            shader_directory / "dx12_depth_alpha_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        const auto forward_ps = compiler.CompileFile(shader_directory / "dx12_forward_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        const auto model_effect_extract_ps = compiler.CompileFile(
            shader_directory / "dx12_model_effect_extract_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        const auto fullscreen_vs = compiler.CompileFile(shader_directory / "dx12_fullscreen_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        const auto lighting_ps = compiler.CompileFile(shader_directory / "dx12_lighting_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        const auto skybox_vs = compiler.CompileFile(shader_directory / "dx12_skybox_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        const auto skybox_ps = compiler.CompileFile(shader_directory / "dx12_skybox_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        const auto temporal_input_ps = compiler.CompileFile(
            shader_directory / "dx12_postprocess_ps.hlsl",
            L"temporal_input_main", L"ps_6_0", debug_layer_enabled_);
        const auto taa_resolve_ps = compiler.CompileFile(
            shader_directory / "dx12_postprocess_ps.hlsl",
            L"taa_resolve_main", L"ps_6_0", debug_layer_enabled_);
        const auto postprocess_ps = compiler.CompileFile(
            shader_directory / "dx12_postprocess_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        const auto ssao_ps = compiler.CompileFile(
            shader_directory / "dx12_ssao_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        const auto shadow_static_vs = compiler.CompileFile(
            shader_directory / "dx12_shadow_static_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        const auto shadow_skinned_vs = compiler.CompileFile(
            shader_directory / "dx12_shadow_skinned_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        const auto shadow_alpha_ps = compiler.CompileFile(
            shader_directory / "dx12_shadow_alpha_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        compiler.Shutdown();
        if (!static_vs.succeeded || !skinned_vs.succeeded || !static_layer_vs.succeeded ||
            !skinned_layer_vs.succeeded || !layer_ps.succeeded || !gbuffer_ps.succeeded ||
            !depth_alpha_ps.succeeded || !forward_ps.succeeded ||
            !model_effect_extract_ps.succeeded || !fullscreen_vs.succeeded ||
            !lighting_ps.succeeded || !skybox_vs.succeeded || !skybox_ps.succeeded ||
            !temporal_input_ps.succeeded ||
            !taa_resolve_ps.succeeded || !postprocess_ps.succeeded ||
            !shadow_static_vs.succeeded || !shadow_skinned_vs.succeeded ||
            !shadow_alpha_ps.succeeded)
        {
            if (!static_vs.diagnostics.empty()) SceneDebugMessage(static_vs.diagnostics.c_str());
            if (!skinned_vs.diagnostics.empty()) SceneDebugMessage(skinned_vs.diagnostics.c_str());
            if (!static_layer_vs.diagnostics.empty())
                SceneDebugMessage(static_layer_vs.diagnostics.c_str());
            if (!skinned_layer_vs.diagnostics.empty())
                SceneDebugMessage(skinned_layer_vs.diagnostics.c_str());
            if (!layer_ps.diagnostics.empty()) SceneDebugMessage(layer_ps.diagnostics.c_str());
            if (!gbuffer_ps.diagnostics.empty()) SceneDebugMessage(gbuffer_ps.diagnostics.c_str());
            if (!depth_alpha_ps.diagnostics.empty()) SceneDebugMessage(depth_alpha_ps.diagnostics.c_str());
            if (!forward_ps.diagnostics.empty()) SceneDebugMessage(forward_ps.diagnostics.c_str());
            if (!model_effect_extract_ps.diagnostics.empty())
                SceneDebugMessage(model_effect_extract_ps.diagnostics.c_str());
            if (!fullscreen_vs.diagnostics.empty()) SceneDebugMessage(fullscreen_vs.diagnostics.c_str());
            if (!lighting_ps.diagnostics.empty()) SceneDebugMessage(lighting_ps.diagnostics.c_str());
            if (!skybox_vs.diagnostics.empty()) SceneDebugMessage(skybox_vs.diagnostics.c_str());
            if (!skybox_ps.diagnostics.empty()) SceneDebugMessage(skybox_ps.diagnostics.c_str());
            if (!temporal_input_ps.diagnostics.empty())
                SceneDebugMessage(temporal_input_ps.diagnostics.c_str());
            if (!taa_resolve_ps.diagnostics.empty()) SceneDebugMessage(taa_resolve_ps.diagnostics.c_str());
            if (!postprocess_ps.diagnostics.empty()) SceneDebugMessage(postprocess_ps.diagnostics.c_str());
            if (!shadow_static_vs.diagnostics.empty()) SceneDebugMessage(shadow_static_vs.diagnostics.c_str());
            if (!shadow_skinned_vs.diagnostics.empty()) SceneDebugMessage(shadow_skinned_vs.diagnostics.c_str());
            if (!shadow_alpha_ps.diagnostics.empty()) SceneDebugMessage(shadow_alpha_ps.diagnostics.c_str());
            return fail("Scene3D.ShaderCompile");
        }

        scene3d_static_vs_ = static_vs.bytecode;
        scene3d_skinned_vs_ = skinned_vs.bytecode;
        scene3d_static_layer_vs_ = static_layer_vs.bytecode;
        scene3d_skinned_layer_vs_ = skinned_layer_vs.bytecode;
        scene3d_layer_ps_ = layer_ps.bytecode;
        scene3d_gbuffer_ps_ = gbuffer_ps.bytecode;
        scene3d_depth_alpha_ps_ = depth_alpha_ps.bytecode;
        scene3d_forward_ps_ = forward_ps.bytecode;
        scene3d_model_effect_extract_ps_ = model_effect_extract_ps.bytecode;
        scene3d_fullscreen_vs_ = fullscreen_vs.bytecode;
        scene3d_lighting_ps_ = lighting_ps.bytecode;
        scene3d_skybox_vs_ = skybox_vs.bytecode;
        scene3d_skybox_ps_ = skybox_ps.bytecode;
        scene3d_temporal_input_ps_ = temporal_input_ps.bytecode;
        scene3d_taa_resolve_ps_ = taa_resolve_ps.bytecode;
        scene3d_postprocess_ps_ = postprocess_ps.bytecode;
        scene3d_ssao_ps_ = ssao_ps.bytecode;
        scene3d_shadow_static_vs_ = shadow_static_vs.bytecode;
        scene3d_shadow_skinned_vs_ = shadow_skinned_vs.bytecode;
        scene3d_shadow_alpha_ps_ = shadow_alpha_ps.bytecode;

        if (!resource_descriptor_allocator_.Allocate(1, scene3d_null_directional_shadow_srv_) ||
            !resource_descriptor_allocator_.Allocate(1, scene3d_null_local_shadow_srv_) ||
            !resource_descriptor_allocator_.Allocate(1, scene3d_null_ibl_diffuse_srv_) ||
            !resource_descriptor_allocator_.Allocate(1, scene3d_null_ibl_specular_srv_))
            return fail("Scene3D.NullEnvironmentDescriptor");
        D3D12_SHADER_RESOURCE_VIEW_DESC null_shadow_srv{};
        null_shadow_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_shadow_srv.Format = kScene3DDepthSrvFormat;
        null_shadow_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        null_shadow_srv.Texture2DArray.MostDetailedMip = 0;
        null_shadow_srv.Texture2DArray.MipLevels = 1;
        null_shadow_srv.Texture2DArray.FirstArraySlice = 0;
        null_shadow_srv.Texture2DArray.ArraySize = D3D12DirectionalShadowSubmission::CascadeCount;
        device_->CreateShaderResourceView(nullptr, &null_shadow_srv,
            scene3d_null_directional_shadow_srv_.cpu);
        null_shadow_srv.Texture2DArray.ArraySize = D3D12LocalShadowSubmission::SliceCount;
        device_->CreateShaderResourceView(nullptr, &null_shadow_srv,
            scene3d_null_local_shadow_srv_.cpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC null_ibl_srv{};
        null_ibl_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_ibl_srv.Format = kScene3DSceneTargetFormat;
        null_ibl_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        null_ibl_srv.TextureCube.MostDetailedMip = 0;
        null_ibl_srv.TextureCube.MipLevels = 1;
        null_ibl_srv.TextureCube.ResourceMinLODClamp = 0.0f;
        device_->CreateShaderResourceView(nullptr, &null_ibl_srv,
            scene3d_null_ibl_diffuse_srv_.cpu);
        device_->CreateShaderResourceView(nullptr, &null_ibl_srv,
            scene3d_null_ibl_specular_srv_.cpu);

        // t0..t5はMaterial Map、t6はCSM、t7はLocal Shadow Atlas、t10はToon RampMap、t11はScene Color。
        // Slot番号ではなく draw.material_texture_semantic_mask で意味を判定する。
        // t8/t9 は Bone Palette の root SRV が使うので RampMap は t10 へ置く。
        constexpr UINT kScene3DGeometrySrvRangeCount = 14;
        const UINT geometry_registers[kScene3DGeometrySrvRangeCount] =
            { 0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 33, 34, 36, 37 };
        const UINT geometry_table_slots[kScene3DGeometrySrvRangeCount] =
            { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20 };
        D3D12_DESCRIPTOR_RANGE geometry_ranges[kScene3DGeometrySrvRangeCount]{};
        for (UINT i = 0; i < static_cast<UINT>(std::size(geometry_ranges)); ++i)
        {
            geometry_ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            geometry_ranges[i].NumDescriptors = 1;
            geometry_ranges[i].BaseShaderRegister = geometry_registers[i];
        }
        D3D12_ROOT_PARAMETER geometry_parameters[21]{};
        for (UINT i = 0; i < 4; ++i)
        {
            geometry_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            geometry_parameters[i].Descriptor.ShaderRegister = i;
            geometry_parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        geometry_parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        geometry_parameters[4].Descriptor.ShaderRegister = 8;
        geometry_parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        geometry_parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        geometry_parameters[5].Descriptor.ShaderRegister = 9;
        geometry_parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        for (UINT i = 0; i < static_cast<UINT>(std::size(geometry_ranges)); ++i)
        {
            const UINT root_slot = geometry_table_slots[i];
            geometry_parameters[root_slot].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            geometry_parameters[root_slot].DescriptorTable.NumDescriptorRanges = 1;
            geometry_parameters[root_slot].DescriptorTable.pDescriptorRanges = &geometry_ranges[i];
            geometry_parameters[root_slot].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        geometry_parameters[16].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        geometry_parameters[16].Descriptor.ShaderRegister = 7;
        geometry_parameters[16].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC samplers[4]{};
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].ShaderRegister = 0;
        samplers[0].MaxAnisotropy = 1;
        samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        // LocalShadowAtlas の DX11 sampler は範囲外=1.0。D3D12でも同じ境界規約にする。
        samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        samplers[1].MaxAnisotropy = 1;
        samplers[1].ShaderRegister = 1;
        samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
        // PCSS blocker search用。比較Samplerとは分離し、深度値そのものを読む。
        samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[2].MaxAnisotropy = 1;
        samplers[2].ShaderRegister = 2;
        samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samplers[2].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[3] = samplers[0];
        samplers[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[3].ShaderRegister = 3;

        D3D12_ROOT_SIGNATURE_DESC geometry_root{};
        geometry_root.NumParameters = static_cast<UINT>(std::size(geometry_parameters));
        geometry_root.pParameters = geometry_parameters;
        geometry_root.NumStaticSamplers = static_cast<UINT>(std::size(samplers));
        geometry_root.pStaticSamplers = samplers;
        geometry_root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        if (!SerializeRoot(device_.Get(), geometry_root, scene3d_geometry_root_signature_, L"Scene3D.Geometry.RootSignature"))
            return fail("Scene3D.GeometryRootSignature");

        const UINT lighting_registers[kScene3DLightingSrvRangeCount] =
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 33, 34, 35, 36, 37, 38 };
        D3D12_DESCRIPTOR_RANGE lighting_ranges[kScene3DLightingSrvRangeCount]{};
        D3D12_ROOT_PARAMETER lighting_parameters[kScene3DLightingSrvRangeCount + 1]{};
        lighting_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        lighting_parameters[0].Descriptor.ShaderRegister = 0;
        lighting_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        for (UINT i = 0; i < kScene3DLightingSrvRangeCount; ++i)
        {
            lighting_ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            lighting_ranges[i].NumDescriptors = 1;
            lighting_ranges[i].BaseShaderRegister = lighting_registers[i];
            lighting_parameters[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            lighting_parameters[1 + i].DescriptorTable.NumDescriptorRanges = 1;
            lighting_parameters[1 + i].DescriptorTable.pDescriptorRanges = &lighting_ranges[i];
            lighting_parameters[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_STATIC_SAMPLER_DESC lighting_samplers[4] =
            { samplers[0], samplers[1], samplers[2], samplers[3] };
        lighting_samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        lighting_samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        lighting_samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        lighting_samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        D3D12_ROOT_SIGNATURE_DESC lighting_root{};
        lighting_root.NumParameters = static_cast<UINT>(std::size(lighting_parameters));
        lighting_root.pParameters = lighting_parameters;
        lighting_root.NumStaticSamplers = static_cast<UINT>(std::size(lighting_samplers));
        lighting_root.pStaticSamplers = lighting_samplers;
        lighting_root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        if (!SerializeRoot(device_.Get(), lighting_root, scene3d_lighting_root_signature_, L"Scene3D.Lighting.RootSignature"))
            return fail("Scene3D.LightingRootSignature");

        D3D12_DESCRIPTOR_RANGE shadow_texture_range{};
        shadow_texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        shadow_texture_range.NumDescriptors = 1;
        shadow_texture_range.BaseShaderRegister = 0;
        D3D12_DESCRIPTOR_RANGE shadow_coverage_texture_range{};
        shadow_coverage_texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        shadow_coverage_texture_range.NumDescriptors = 1;
        shadow_coverage_texture_range.BaseShaderRegister = 46;
        D3D12_ROOT_PARAMETER shadow_parameters[6]{};
        shadow_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        shadow_parameters[0].Descriptor.ShaderRegister = 0;
        shadow_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        shadow_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        shadow_parameters[1].Descriptor.ShaderRegister = 1;
        shadow_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        shadow_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        shadow_parameters[2].Descriptor.ShaderRegister = 8;
        shadow_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        shadow_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        shadow_parameters[3].DescriptorTable.NumDescriptorRanges = 1;
        shadow_parameters[3].DescriptorTable.pDescriptorRanges = &shadow_texture_range;
        shadow_parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        shadow_parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        shadow_parameters[4].Descriptor.ShaderRegister = 8;
        shadow_parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        shadow_parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        shadow_parameters[5].DescriptorTable.NumDescriptorRanges = 1;
        shadow_parameters[5].DescriptorTable.pDescriptorRanges = &shadow_coverage_texture_range;
        shadow_parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC shadow_root{};
        shadow_root.NumParameters = static_cast<UINT>(std::size(shadow_parameters));
        shadow_root.pParameters = shadow_parameters;
        shadow_root.NumStaticSamplers = 1;
        shadow_root.pStaticSamplers = &samplers[0];
        shadow_root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        if (!SerializeRoot(device_.Get(), shadow_root, scene3d_shadow_root_signature_, L"Scene3D.Shadow.RootSignature"))
            return fail("Scene3D.ShadowRootSignature");

        D3D12_INPUT_ELEMENT_DESC static_input[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_INPUT_ELEMENT_DESC skinned_input[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "MORPHPOSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 80, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "MORPHNORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 92, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        const auto create_geometry_pso = [this](bool skinned, bool transparent,
            bool double_sided, const D3D12_INPUT_ELEMENT_DESC* input, UINT input_count,
            ID3D12PipelineState** output) -> bool
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = scene3d_geometry_root_signature_.Get();
            const auto& vs = skinned ? scene3d_skinned_vs_ : scene3d_static_vs_;
            const auto& ps = transparent ? scene3d_forward_ps_ : scene3d_gbuffer_ps_;
            desc.VS = { vs.data(), vs.size() };
            desc.PS = { ps.data(), ps.size() };
            desc.BlendState = MakeBlend(transparent);
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = MakeRaster(double_sided);
            // Opaque/Mask は直前の Depth Prepass と同一頂点結果を再利用する。
            // GBuffer で深度を再書込みせず EQUAL で照合し、Transparent は通常の read-only LESS_EQUAL。
            desc.DepthStencilState = MakeDepth(false);
            if (!transparent)
                desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
            desc.InputLayout = { input, input_count };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            if (transparent)
            {
                desc.NumRenderTargets = 1;
                desc.RTVFormats[0] = kScene3DSceneTargetFormat;
            }
            else
            {
                desc.NumRenderTargets = kScene3DGBufferCount;
                for (UINT i = 0; i < kScene3DGBufferCount; ++i)
                    desc.RTVFormats[i] = kScene3DGBufferFormats[i];
            }
            desc.DSVFormat = kScene3DDepthDsvFormat;
            desc.SampleDesc.Count = 1;
            return SUCCEEDED(device_->CreateGraphicsPipelineState(&desc,
                IID_PPV_ARGS(output)));
        };

        HRESULT pipeline_hr = S_OK;
        const auto create_layer_pso = [this, &pipeline_hr](bool skinned,
            D3D12ShaderLayerPassKind kind, ShaderLayerBlend blend, bool depth_enabled,
            const D3D12_INPUT_ELEMENT_DESC* input, UINT input_count,
            ID3D12PipelineState** output) -> bool
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = scene3d_geometry_root_signature_.Get();
            const auto& vs = skinned ? scene3d_skinned_layer_vs_ : scene3d_static_layer_vs_;
            desc.VS = { vs.data(), vs.size() };
            desc.PS = { scene3d_layer_ps_.data(), scene3d_layer_ps_.size() };
            desc.BlendState = MakeLayerBlend(blend);
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = MakeRaster(true);
            desc.RasterizerState.FillMode = kind == D3D12ShaderLayerPassKind::Wireframe
                ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
            desc.RasterizerState.CullMode = kind == D3D12ShaderLayerPassKind::Outline
                ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_NONE;
            desc.RasterizerState.AntialiasedLineEnable = kind == D3D12ShaderLayerPassKind::Wireframe;
            desc.DepthStencilState = MakeDepth(false);
            if (!depth_enabled) desc.DepthStencilState.DepthEnable = FALSE;
            desc.InputLayout = { input, input_count };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = kScene3DSceneTargetFormat;
            desc.DSVFormat = kScene3DDepthDsvFormat;
            desc.SampleDesc.Count = 1;
            pipeline_hr = device_->CreateGraphicsPipelineState(&desc,
                IID_PPV_ARGS(output));
            return SUCCEEDED(pipeline_hr);
        };
        const auto create_model_effect_pso = [this, &pipeline_hr](bool skinned,
            bool overlay, bool double_sided, const D3D12_INPUT_ELEMENT_DESC* input,
            UINT input_count, ID3D12PipelineState** output) -> bool
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = scene3d_geometry_root_signature_.Get();
            const auto& vs = skinned ? scene3d_skinned_vs_ : scene3d_static_vs_;
            desc.VS = { vs.data(), vs.size() };
            desc.PS = { scene3d_forward_ps_.data(), scene3d_forward_ps_.size() };
            desc.BlendState = MakeBlend(true);
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = MakeRaster(double_sided);
            desc.DepthStencilState = MakeDepth(!overlay);
            if (overlay) desc.DepthStencilState.DepthEnable = FALSE;
            desc.InputLayout = { input, input_count };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = kScene3DSceneTargetFormat;
            desc.DSVFormat = kScene3DDepthDsvFormat;
            desc.SampleDesc.Count = 1;
            pipeline_hr = device_->CreateGraphicsPipelineState(&desc,
                IID_PPV_ARGS(output));
            return SUCCEEDED(pipeline_hr);
        };

        const auto create_model_effect_extract_pso = [this, &pipeline_hr](bool skinned,
            bool double_sided, const D3D12_INPUT_ELEMENT_DESC* input,
            UINT input_count, ID3D12PipelineState** output) -> bool
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = scene3d_geometry_root_signature_.Get();
            const auto& vs = skinned ? scene3d_skinned_vs_ : scene3d_static_vs_;
            desc.VS = { vs.data(), vs.size() };
            desc.PS = { scene3d_model_effect_extract_ps_.data(),
                scene3d_model_effect_extract_ps_.size() };
            desc.BlendState = MakeBlend(false);
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = MakeRaster(double_sided);
            desc.DepthStencilState = MakeDepth(false);
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
            desc.InputLayout = { input, input_count };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = kScene3DSceneTargetFormat;
            desc.DSVFormat = kScene3DDepthDsvFormat;
            desc.SampleDesc.Count = 1;
            pipeline_hr = device_->CreateGraphicsPipelineState(&desc,
                IID_PPV_ARGS(output));
            return SUCCEEDED(pipeline_hr);
        };

        const auto create_depth_pso = [this, &pipeline_hr](bool skinned, bool alpha_clip,
            bool double_sided, const D3D12_INPUT_ELEMENT_DESC* input, UINT input_count,
            ID3D12PipelineState** output) -> bool
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = scene3d_geometry_root_signature_.Get();
            const auto& vs = skinned ? scene3d_skinned_vs_ : scene3d_static_vs_;
            desc.VS = { vs.data(), vs.size() };
            if (alpha_clip)
            {
                desc.PS = { scene3d_depth_alpha_ps_.data(), scene3d_depth_alpha_ps_.size() };
            }
            desc.BlendState = MakeBlend(false);
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = MakeRaster(double_sided);
            desc.DepthStencilState = MakeDepth(true);
            desc.InputLayout = { input, input_count };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 0;
            desc.DSVFormat = kScene3DDepthDsvFormat;
            desc.SampleDesc.Count = 1;
            pipeline_hr = device_->CreateGraphicsPipelineState(&desc,
                IID_PPV_ARGS(output));
            return SUCCEEDED(pipeline_hr);
        };

        const auto create_shadow_pso = [this, &pipeline_hr](bool skinned, bool alpha_clip,
            bool double_sided, const D3D12_INPUT_ELEMENT_DESC* input, UINT input_count,
            ID3D12PipelineState** output) -> bool
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = scene3d_shadow_root_signature_.Get();
            const auto& vs = skinned ? scene3d_shadow_skinned_vs_ : scene3d_shadow_static_vs_;
            desc.VS = { vs.data(), vs.size() };
            if (alpha_clip)
            {
                desc.PS = { scene3d_shadow_alpha_ps_.data(), scene3d_shadow_alpha_ps_.size() };
            }
            desc.BlendState = MakeBlend(false);
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState = MakeRaster(double_sided);
            desc.DepthStencilState = MakeDepth(true);
            desc.InputLayout = { input, input_count };
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 0;
            desc.DSVFormat = kScene3DDepthDsvFormat;
            desc.SampleDesc.Count = 1;
            pipeline_hr = device_->CreateGraphicsPipelineState(&desc,
                IID_PPV_ARGS(output));
            return SUCCEEDED(pipeline_hr);
        };

        for (UINT sided = 0; sided < 2; ++sided)
        {
            for (UINT alpha = 0; alpha < 3; ++alpha)
            {
                const UINT index = sided * 3 + alpha;
                if (!create_geometry_pso(false, false, sided != 0, static_input,
                    static_cast<UINT>(std::size(static_input)),
                    scene3d_static_gbuffer_pipelines_[index].ReleaseAndGetAddressOf()))
                    return fail("Scene3D.PSO.StaticGBuffer");
                if (!create_geometry_pso(true, false, sided != 0, skinned_input,
                    static_cast<UINT>(std::size(skinned_input)),
                    scene3d_skinned_gbuffer_pipelines_[index].ReleaseAndGetAddressOf()))
                    return fail("Scene3D.PSO.SkinnedGBuffer");
            }
                if (!create_geometry_pso(false, true, sided != 0, static_input,
                    static_cast<UINT>(std::size(static_input)),
                    scene3d_static_forward_blend_pipelines_[sided].ReleaseAndGetAddressOf()))
                return fail("Scene3D.PSO.StaticForward");
            if (!create_geometry_pso(true, true, sided != 0, skinned_input,
                static_cast<UINT>(std::size(skinned_input)),
                scene3d_skinned_forward_blend_pipelines_[sided].ReleaseAndGetAddressOf()))
                return fail("Scene3D.PSO.SkinnedForward");
            if (!create_model_effect_extract_pso(false, sided != 0, static_input,
                static_cast<UINT>(std::size(static_input)),
                scene3d_static_model_effect_extract_pipelines_[sided].ReleaseAndGetAddressOf()))
                return fail("Scene3D.PSO.StaticModelEffectExtract");
            if (!create_model_effect_extract_pso(true, sided != 0, skinned_input,
                static_cast<UINT>(std::size(skinned_input)),
                scene3d_skinned_model_effect_extract_pipelines_[sided].ReleaseAndGetAddressOf()))
                return fail("Scene3D.PSO.SkinnedModelEffectExtract");
            for (UINT depth_mode = 0; depth_mode < 2; ++depth_mode)
            {
                const UINT model_index = sided * 2u + depth_mode;
                if (!create_model_effect_pso(false, depth_mode != 0, sided != 0,
                    static_input, static_cast<UINT>(std::size(static_input)),
                    scene3d_static_model_effect_pipelines_[model_index].ReleaseAndGetAddressOf()))
                    return fail("Scene3D.PSO.StaticModelEffect");
                if (!create_model_effect_pso(true, depth_mode != 0, sided != 0,
                    skinned_input, static_cast<UINT>(std::size(skinned_input)),
                    scene3d_skinned_model_effect_pipelines_[model_index].ReleaseAndGetAddressOf()))
                    return fail("Scene3D.PSO.SkinnedModelEffect");
            }
            for (UINT alpha_clip = 0; alpha_clip < 2; ++alpha_clip)
            {
                const UINT depth_index = sided * 2 + alpha_clip;
                if (!create_depth_pso(false, alpha_clip != 0, sided != 0, static_input,
                    static_cast<UINT>(std::size(static_input)),
                    scene3d_static_depth_pipelines_[depth_index].ReleaseAndGetAddressOf()))
                {
                    SetInitializationFailure(alpha_clip != 0
                        ? "Scene3D.PSO.StaticDepthMask" : "Scene3D.PSO.StaticDepthOpaque", pipeline_hr);
                    return false;
                }
                if (!create_depth_pso(true, alpha_clip != 0, sided != 0, skinned_input,
                    static_cast<UINT>(std::size(skinned_input)),
                    scene3d_skinned_depth_pipelines_[depth_index].ReleaseAndGetAddressOf()))
                {
                    SetInitializationFailure(alpha_clip != 0
                        ? "Scene3D.PSO.SkinnedDepthMask" : "Scene3D.PSO.SkinnedDepthOpaque", pipeline_hr);
                    return false;
                }
            }
            for (UINT alpha_clip = 0; alpha_clip < 2; ++alpha_clip)
            {
                const UINT shadow_index = sided * 2 + alpha_clip;
                if (!create_shadow_pso(false, alpha_clip != 0, sided != 0, static_input,
                    static_cast<UINT>(std::size(static_input)),
                    scene3d_static_shadow_pipelines_[shadow_index].ReleaseAndGetAddressOf()))
                {
                    SetInitializationFailure(alpha_clip != 0
                        ? "Scene3D.PSO.StaticShadowMask" : "Scene3D.PSO.StaticShadowOpaque", pipeline_hr);
                    return false;
                }
                if (!create_shadow_pso(true, alpha_clip != 0, sided != 0, skinned_input,
                    static_cast<UINT>(std::size(skinned_input)),
                    scene3d_skinned_shadow_pipelines_[shadow_index].ReleaseAndGetAddressOf()))
                {
                    SetInitializationFailure(alpha_clip != 0
                        ? "Scene3D.PSO.SkinnedShadowMask" : "Scene3D.PSO.SkinnedShadowOpaque", pipeline_hr);
                    return false;
                }
            }
        }

        for (UINT depth_mode = 0; depth_mode < 2; ++depth_mode)
        {
            for (UINT kind = 0; kind < 2; ++kind)
            {
                for (UINT blend = 0; blend < 3; ++blend)
                {
                    const UINT index = depth_mode * 6u + kind * 3u + blend;
                    if (!create_layer_pso(false,
                        static_cast<D3D12ShaderLayerPassKind>(kind),
                        static_cast<ShaderLayerBlend>(blend), depth_mode == 0,
                        static_input, static_cast<UINT>(std::size(static_input)),
                        scene3d_static_layer_pipelines_[index].ReleaseAndGetAddressOf()))
                        return fail("Scene3D.PSO.StaticLayer");
                    if (!create_layer_pso(true,
                        static_cast<D3D12ShaderLayerPassKind>(kind),
                        static_cast<ShaderLayerBlend>(blend), depth_mode == 0,
                        skinned_input, static_cast<UINT>(std::size(skinned_input)),
                        scene3d_skinned_layer_pipelines_[index].ReleaseAndGetAddressOf()))
                        return fail("Scene3D.PSO.SkinnedLayer");
                }
            }
        }

        for (UINT index = 0; index < std::size(scene3d_static_gbuffer_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_static_gbuffer_pipelines_[index].Get(),
                L"Scene3D.PSO.StaticGBuffer", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_skinned_gbuffer_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_skinned_gbuffer_pipelines_[index].Get(),
                L"Scene3D.PSO.SkinnedGBuffer", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_static_layer_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_static_layer_pipelines_[index].Get(),
                L"Scene3D.PSO.StaticLayer", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_skinned_layer_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_skinned_layer_pipelines_[index].Get(),
                L"Scene3D.PSO.SkinnedLayer", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_static_forward_blend_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_static_forward_blend_pipelines_[index].Get(),
                L"Scene3D.PSO.StaticForward", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_skinned_forward_blend_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_skinned_forward_blend_pipelines_[index].Get(),
                L"Scene3D.PSO.SkinnedForward", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_static_model_effect_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_static_model_effect_pipelines_[index].Get(),
                L"SceneEffect.PSO.StaticModel", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_skinned_model_effect_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_skinned_model_effect_pipelines_[index].Get(),
                L"SceneEffect.PSO.SkinnedModel", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_static_depth_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_static_depth_pipelines_[index].Get(),
                L"Scene3D.PSO.StaticDepth", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_skinned_depth_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_skinned_depth_pipelines_[index].Get(),
                L"Scene3D.PSO.SkinnedDepth", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_static_shadow_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_static_shadow_pipelines_[index].Get(),
                L"Scene3D.PSO.StaticShadow", L"Variant", index);
        for (UINT index = 0; index < std::size(scene3d_skinned_shadow_pipelines_); ++index)
            SetD3D12ObjectName(scene3d_skinned_shadow_pipelines_[index].Get(),
                L"Scene3D.PSO.SkinnedShadow", L"Variant", index);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC lighting{};
        lighting.pRootSignature = scene3d_lighting_root_signature_.Get();
        lighting.VS = { scene3d_fullscreen_vs_.data(), scene3d_fullscreen_vs_.size() };
        lighting.PS = { scene3d_lighting_ps_.data(), scene3d_lighting_ps_.size() };
        lighting.BlendState = MakeBlend(false);
        lighting.SampleMask = UINT_MAX;
        lighting.RasterizerState = MakeRaster(true);
        lighting.DepthStencilState.DepthEnable = FALSE;
        lighting.DepthStencilState.StencilEnable = FALSE;
        lighting.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        lighting.NumRenderTargets = 1;
        lighting.RTVFormats[0] = kScene3DSceneTargetFormat;
        lighting.SampleDesc.Count = 1;
        if (FAILED(device_->CreateGraphicsPipelineState(&lighting,
            IID_PPV_ARGS(&scene3d_lighting_pipeline_))))
            return fail("Scene3D.PSO.Lighting");
        SetD3D12ObjectName(scene3d_lighting_pipeline_.Get(), L"Scene3D.PSO", L"Lighting");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC skybox = lighting;
        skybox.VS = { scene3d_skybox_vs_.data(), scene3d_skybox_vs_.size() };
        skybox.PS = { scene3d_skybox_ps_.data(), scene3d_skybox_ps_.size() };
        skybox.DepthStencilState = MakeDepth(false);
        skybox.NumRenderTargets = 2;
        skybox.RTVFormats[0] = kScene3DSceneTargetFormat;
        skybox.RTVFormats[1] = kScene3DGBufferFormats[4];
        skybox.DSVFormat = kScene3DDepthDsvFormat;
        if (FAILED(device_->CreateGraphicsPipelineState(&skybox,
            IID_PPV_ARGS(&scene3d_skybox_pipeline_))))
            return fail("Scene3D.PSO.Skybox");
        SetD3D12ObjectName(scene3d_skybox_pipeline_.Get(), L"Scene3D.PSO", L"Skybox");

        D3D12_DESCRIPTOR_RANGE postprocess_ranges[11]{};
        for (UINT index = 0; index < 11; ++index)
        {
            postprocess_ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            postprocess_ranges[index].NumDescriptors = 1;
            postprocess_ranges[index].BaseShaderRegister = index;
        }
        D3D12_ROOT_PARAMETER postprocess_parameters[12]{};
        postprocess_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        postprocess_parameters[0].Descriptor.ShaderRegister = 0;
        postprocess_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        for (UINT index = 0; index < 11; ++index)
        {
            postprocess_parameters[index + 1].ParameterType =
                D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            postprocess_parameters[index + 1].DescriptorTable.NumDescriptorRanges = 1;
            postprocess_parameters[index + 1].DescriptorTable.pDescriptorRanges =
                &postprocess_ranges[index];
            postprocess_parameters[index + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_STATIC_SAMPLER_DESC postprocess_samplers[2]{};
        postprocess_samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        postprocess_samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        for (D3D12_STATIC_SAMPLER_DESC& sampler : postprocess_samplers)
        {
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.MaxAnisotropy = 1;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        postprocess_samplers[0].ShaderRegister = 0;
        postprocess_samplers[1].ShaderRegister = 1;
        D3D12_ROOT_SIGNATURE_DESC postprocess_root{};
        postprocess_root.NumParameters = static_cast<UINT>(std::size(postprocess_parameters));
        postprocess_root.pParameters = postprocess_parameters;
        postprocess_root.NumStaticSamplers = static_cast<UINT>(std::size(postprocess_samplers));
        postprocess_root.pStaticSamplers = postprocess_samplers;
        postprocess_root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        if (!SerializeRoot(device_.Get(), postprocess_root,
            scene3d_postprocess_root_signature_, L"Scene3D.PostProcess.RootSignature"))
            return fail("Scene3D.PostProcessRootSignature");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC temporal_input{};
        temporal_input.pRootSignature = scene3d_postprocess_root_signature_.Get();
        temporal_input.VS = { scene3d_fullscreen_vs_.data(), scene3d_fullscreen_vs_.size() };
        temporal_input.PS = { scene3d_temporal_input_ps_.data(), scene3d_temporal_input_ps_.size() };
        temporal_input.BlendState = MakeBlend(false);
        temporal_input.SampleMask = UINT_MAX;
        temporal_input.RasterizerState = MakeRaster(true);
        temporal_input.DepthStencilState.DepthEnable = FALSE;
        temporal_input.DepthStencilState.StencilEnable = FALSE;
        temporal_input.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        temporal_input.NumRenderTargets = 1;
        temporal_input.RTVFormats[0] = kScene3DSceneTargetFormat;
        temporal_input.SampleDesc.Count = 1;
        if (FAILED(device_->CreateGraphicsPipelineState(&temporal_input,
            IID_PPV_ARGS(&scene3d_temporal_input_pipeline_))))
            return fail("Scene3D.PSO.TemporalInput");
        SetD3D12ObjectName(scene3d_temporal_input_pipeline_.Get(), L"Scene3D.PSO", L"TemporalInput");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC taa_resolve = temporal_input;
        taa_resolve.PS = { scene3d_taa_resolve_ps_.data(), scene3d_taa_resolve_ps_.size() };
        if (FAILED(device_->CreateGraphicsPipelineState(&taa_resolve,
            IID_PPV_ARGS(&scene3d_taa_resolve_pipeline_))))
            return fail("Scene3D.PSO.TaaResolve");
        SetD3D12ObjectName(scene3d_taa_resolve_pipeline_.Get(), L"Scene3D.PSO", L"TaaResolve");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC postprocess = taa_resolve;
        postprocess.PS = { scene3d_postprocess_ps_.data(), scene3d_postprocess_ps_.size() };
        postprocess.RTVFormats[0] = kScene3DBackBufferFormat;
        if (FAILED(device_->CreateGraphicsPipelineState(&postprocess,
            IID_PPV_ARGS(&scene3d_postprocess_pipeline_))))
            return fail("Scene3D.PSO.PostProcess");
        SetD3D12ObjectName(scene3d_postprocess_pipeline_.Get(), L"Scene3D.PSO", L"PostProcess");

        // SSAO を半解像度の 1 チャンネルへ焼く。ポスト処理はこれを読むだけになる。
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ssao = taa_resolve;
        ssao.PS = { scene3d_ssao_ps_.data(), scene3d_ssao_ps_.size() };
        ssao.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
        if (FAILED(device_->CreateGraphicsPipelineState(&ssao,
            IID_PPV_ARGS(&scene3d_ssao_pipeline_))))
            return fail("Scene3D.PSO.Ssao");
        SetD3D12ObjectName(scene3d_ssao_pipeline_.Get(), L"Scene3D.PSO", L"Ssao");
        return true;
    }

#ifdef USE_IMGUI
    bool D3D12DeviceContext::CreateImGuiRendererResources() noexcept
    {
        if (device_ == nullptr) return false;
        D3D12ShaderCompiler compiler;
        if (!compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath())) return false;
        const std::filesystem::path shader_directory =
            std::filesystem::current_path() / "Shader";
        const auto vertex = compiler.CompileFile(shader_directory / "dx12_imgui_vs.hlsl",
            L"main", L"vs_6_0", debug_layer_enabled_);
        const auto pixel = compiler.CompileFile(shader_directory / "dx12_imgui_ps.hlsl",
            L"main", L"ps_6_0", debug_layer_enabled_);
        compiler.Shutdown();
        if (!vertex.succeeded || !pixel.succeeded)
        {
            if (!vertex.diagnostics.empty()) SceneDebugMessage(vertex.diagnostics.c_str());
            if (!pixel.diagnostics.empty()) SceneDebugMessage(pixel.diagnostics.c_str());
            return false;
        }
        imgui_vertex_shader_ = vertex.bytecode;
        imgui_pixel_shader_ = pixel.bytecode;

        D3D12_DESCRIPTOR_RANGE texture_range{};
        texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        texture_range.NumDescriptors = 1;
        texture_range.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].Descriptor.ShaderRegister = 0;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[1].DescriptorTable.pDescriptorRanges = &texture_range;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
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
        if (!SerializeRoot(device_.Get(), root_desc, imgui_root_signature_, L"ImGui.RootSignature")) return false;

        const D3D12_INPUT_ELEMENT_DESC input_layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
                static_cast<UINT>(offsetof(ImDrawVert, pos)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
                static_cast<UINT>(offsetof(ImDrawVert, uv)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
                static_cast<UINT>(offsetof(ImDrawVert, col)),
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = imgui_root_signature_.Get();
        pipeline.VS = { imgui_vertex_shader_.data(), imgui_vertex_shader_.size() };
        pipeline.PS = { imgui_pixel_shader_.data(), imgui_pixel_shader_.size() };
        pipeline.InputLayout = { input_layout, static_cast<UINT>(std::size(input_layout)) };
        pipeline.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pipeline.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        pipeline.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pipeline.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pipeline.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pipeline.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pipeline.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline.RasterizerState.FrontCounterClockwise = FALSE;
        pipeline.RasterizerState.DepthClipEnable = TRUE;
        pipeline.DepthStencilState.DepthEnable = FALSE;
        pipeline.DepthStencilState.StencilEnable = FALSE;
        pipeline.SampleMask = UINT_MAX;
        pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets = 1;
        pipeline.RTVFormats[0] = kScene3DBackBufferFormat;
        pipeline.SampleDesc.Count = 1;
        if (FAILED(device_->CreateGraphicsPipelineState(&pipeline,
            IID_PPV_ARGS(&imgui_pipeline_))))
            return false;
        SetD3D12ObjectName(imgui_pipeline_.Get(), L"ImGui.PSO", L"Main");
        return true;
    }

    bool D3D12DeviceContext::InitializeImGui() noexcept
    {
        if (imgui_ready_) return true;
        if (!CreateImGuiRendererResources()) return false;
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int texture_width = 0;
        int texture_height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &texture_width, &texture_height);
        if (pixels == nullptr || texture_width <= 0 || texture_height <= 0) return false;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texture{};
        texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width = static_cast<UINT64>(texture_width);
        texture.Height = static_cast<UINT>(texture_height);
        texture.DepthOrArraySize = 1;
        texture.MipLevels = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.SampleDesc.Count = 1;
        texture.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&imgui_font_texture_))))
            return false;
        SetD3D12ObjectName(imgui_font_texture_.Get(), L"ImGui.Font", L"Atlas");
        if (!resource_descriptor_allocator_.Allocate(1, imgui_font_srv_) ||
            !upload_context_.UploadTexture2D(imgui_font_texture_.Get(), pixels,
                static_cast<std::uint32_t>(texture_width),
                static_cast<std::uint32_t>(texture_height),
                static_cast<std::uint32_t>(texture_width) * 4u,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
            return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(imgui_font_texture_.Get(), &srv, imgui_font_srv_.cpu);

        const auto white = texture_cache_.find("__dx12_white");
        if (white == texture_cache_.end() || !white->second.resource ||
            !resource_descriptor_allocator_.Allocate(1, imgui_fallback_srv_))
            return false;
        device_->CreateShaderResourceView(white->second.resource.Get(), &srv,
            imgui_fallback_srv_.cpu);
        imgui_font_texture_id_ = static_cast<std::uint64_t>(imgui_font_srv_.gpu.ptr);
        io.Fonts->TexID = reinterpret_cast<ImTextureID>(
            static_cast<std::uintptr_t>(imgui_font_texture_id_));
        io.BackendRendererName = "ReplayEngine_DX12";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        imgui_ready_ = true;
        return true;
    }

    bool D3D12DeviceContext::DrawImGui(ImDrawData* draw_data) noexcept
    {
        BeginGpuPass(D3D12GpuPass::ImGui);
        const auto finish = [this](bool result) noexcept
        {
            EndGpuPass(D3D12GpuPass::ImGui);
            return result;
        };
        if (!imgui_ready_ || draw_data == nullptr || !frame_open_) return finish(true);
        if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f ||
            draw_data->TotalVtxCount <= 0 || draw_data->TotalIdxCount <= 0)
            return finish(true);

        D3D12LinearUploadAllocator& allocator = frame_resources_[frame_index_].upload_allocator;
        D3D12UploadAllocation vertex_upload{};
        D3D12UploadAllocation index_upload{};
        D3D12UploadAllocation constant_upload{};
        const auto allocate = [&allocator](std::uint64_t size, std::uint64_t alignment,
            D3D12UploadAllocation& output) noexcept
        {
            return allocator.Allocate(size, alignment, output);
        };
        const std::uint64_t vertex_size = static_cast<std::uint64_t>(draw_data->TotalVtxCount) *
            sizeof(ImDrawVert);
        const std::uint64_t index_size = static_cast<std::uint64_t>(draw_data->TotalIdxCount) *
            sizeof(ImDrawIdx);
        if (!allocate(vertex_size, 16, vertex_upload) ||
            !allocate(index_size, 4, index_upload))
            return finish(false);

        auto* vertex_destination = static_cast<std::uint8_t*>(vertex_upload.cpu);
        auto* index_destination = static_cast<std::uint8_t*>(index_upload.cpu);
        for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index)
        {
            const ImDrawList* list = draw_data->CmdLists[list_index];
            const std::size_t list_vertex_size = static_cast<std::size_t>(list->VtxBuffer.Size) *
                sizeof(ImDrawVert);
            const std::size_t list_index_size = static_cast<std::size_t>(list->IdxBuffer.Size) *
                sizeof(ImDrawIdx);
            std::memcpy(vertex_destination, list->VtxBuffer.Data, list_vertex_size);
            std::memcpy(index_destination, list->IdxBuffer.Data, list_index_size);
            vertex_destination += list_vertex_size;
            index_destination += list_index_size;
        }

        struct ImGuiConstants final
        {
            float projection[4][4]{};
        } constants{};
        const float left = draw_data->DisplayPos.x;
        const float right = left + draw_data->DisplaySize.x;
        const float top = draw_data->DisplayPos.y;
        const float bottom = top + draw_data->DisplaySize.y;
        constants.projection[0][0] = 2.0f / (right - left);
        constants.projection[1][1] = 2.0f / (top - bottom);
        constants.projection[2][2] = 0.5f;
        constants.projection[3][0] = (right + left) / (left - right);
        constants.projection[3][1] = (top + bottom) / (bottom - top);
        constants.projection[3][2] = 0.5f;
        constants.projection[3][3] = 1.0f;
        if (!allocate(sizeof(constants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
            constant_upload))
            return finish(false);
        std::memcpy(constant_upload.cpu, &constants, sizeof(constants));

        ID3D12DescriptorHeap* heaps[] = { resource_descriptor_allocator_.Heap() };
        command_list_->SetDescriptorHeaps(1, heaps);
        command_list_->SetGraphicsRootSignature(imgui_root_signature_.Get());
        command_list_->SetPipelineState(imgui_pipeline_.Get());
        command_list_->SetGraphicsRootConstantBufferView(0, constant_upload.gpu);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = CurrentRenderTargetView();
        command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const D3D12_VIEWPORT viewport{ 0.0f, 0.0f,
            static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
        const D3D12_RECT full_scissor{ 0, 0,
            static_cast<LONG>(width_), static_cast<LONG>(height_) };
        command_list_->RSSetViewports(1, &viewport);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VERTEX_BUFFER_VIEW vertex_view{};
        vertex_view.BufferLocation = vertex_upload.gpu;
        vertex_view.SizeInBytes = static_cast<UINT>(vertex_size);
        vertex_view.StrideInBytes = sizeof(ImDrawVert);
        command_list_->IASetVertexBuffers(0, 1, &vertex_view);
        D3D12_INDEX_BUFFER_VIEW index_view{};
        index_view.BufferLocation = index_upload.gpu;
        index_view.SizeInBytes = static_cast<UINT>(index_size);
        index_view.Format = sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        command_list_->IASetIndexBuffer(&index_view);

        int global_vertex_offset = 0;
        int global_index_offset = 0;
        const ImVec2 clip_offset = draw_data->DisplayPos;
        const ImVec2 clip_scale = draw_data->FramebufferScale;
        for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index)
        {
            const ImDrawList* list = draw_data->CmdLists[list_index];
            for (const ImDrawCmd& command : list->CmdBuffer)
            {
                if (command.UserCallback != nullptr)
                {
                    if (command.UserCallback == ImDrawCallback_ResetRenderState)
                    {
                        command_list_->SetGraphicsRootSignature(imgui_root_signature_.Get());
                        command_list_->SetPipelineState(imgui_pipeline_.Get());
                        command_list_->SetGraphicsRootConstantBufferView(0, constant_upload.gpu);
                    }
                    else
                    {
                        command.UserCallback(list, &command);
                    }
                    continue;
                }
                const ImVec2 clip_min{
                    (command.ClipRect.x - clip_offset.x) * clip_scale.x,
                    (command.ClipRect.y - clip_offset.y) * clip_scale.y };
                const ImVec2 clip_max{
                    (command.ClipRect.z - clip_offset.x) * clip_scale.x,
                    (command.ClipRect.w - clip_offset.y) * clip_scale.y };
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;
                D3D12_RECT scissor{
                    static_cast<LONG>((std::max)(0.0f, clip_min.x)),
                    static_cast<LONG>((std::max)(0.0f, clip_min.y)),
                    static_cast<LONG>((std::min)(static_cast<float>(width_), clip_max.x)),
                    static_cast<LONG>((std::min)(static_cast<float>(height_), clip_max.y)) };
                if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) continue;
                command_list_->RSSetScissorRects(1, &scissor);
                const std::uint64_t texture_id = command.TextureId == nullptr
                    ? 0 : static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                        command.TextureId));
                D3D12_GPU_DESCRIPTOR_HANDLE texture = imgui_fallback_srv_.gpu;
                if (texture_id == imgui_font_texture_id_)
                {
                    texture = imgui_font_srv_.gpu;
                }
                else if (texture_id == ui_preview_texture_id_)
                {
                    texture = ui_preview_target_.srv.gpu;
                }
                else if (texture_id != 0)
                {
                    const auto* request = reinterpret_cast<const ImGuiTextureRequest*>(
                        static_cast<std::uintptr_t>(texture_id));
                    if (imgui_texture_request_addresses_.find(request) !=
                        imgui_texture_request_addresses_.end())
                    {
                        const auto found = texture_cache_.find(request->key);
                        if (found != texture_cache_.end()) texture = found->second.srv.gpu;
                    }
                }
                command_list_->SetGraphicsRootDescriptorTable(1, texture);
                command_list_->DrawIndexedInstanced(command.ElemCount, 1,
                    command.IdxOffset + static_cast<UINT>(global_index_offset),
                    command.VtxOffset + global_vertex_offset, 0);
            }
            global_vertex_offset += list->VtxBuffer.Size;
            global_index_offset += list->IdxBuffer.Size;
        }
        // ImGui の clip rect を後続の描画パスへ持ち越さない。
        command_list_->RSSetScissorRects(1, &full_scissor);
        return finish(true);
    }

    void* D3D12DeviceContext::ImGuiTextureForPath(
        const std::filesystem::path& source_path) noexcept
    {
        if (!imgui_ready_ || source_path.empty()) return nullptr;
        std::filesystem::path resolved = source_path.lexically_normal();
        if (resolved.is_relative())
            resolved = std::filesystem::current_path() / resolved;
        resolved = resolved.lexically_normal();
        const std::string key = resolved.generic_string();
        if (key.empty()) return nullptr;
        const auto found = imgui_texture_requests_.find(key);
        if (found != imgui_texture_requests_.end()) return found->second.get();
        try
        {
            auto request = std::make_unique<ImGuiTextureRequest>();
            request->key = key;
            request->source_path = resolved;
            ImGuiTextureRequest* result = request.get();
            imgui_texture_requests_.emplace(key, std::move(request));
            imgui_texture_request_addresses_.insert(result);
            return result;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void* D3D12DeviceContext::ImGuiTextureForUIPreview() const noexcept
    {
        if (!imgui_ready_ || !ui_preview_target_.srv.IsValid()) return nullptr;
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(
            ui_preview_target_.srv.gpu.ptr));
    }

    void D3D12DeviceContext::ReleaseImGuiRendererResources() noexcept
    {
        imgui_ready_ = false;
        imgui_font_texture_id_ = 0;
        ui_preview_texture_id_ = 0;
        imgui_font_texture_.Reset();
        imgui_pipeline_.Reset();
        imgui_root_signature_.Reset();
        imgui_vertex_shader_.clear();
        imgui_pixel_shader_.clear();
        if (imgui_font_srv_.IsValid()) resource_descriptor_allocator_.Free(imgui_font_srv_);
        if (imgui_fallback_srv_.IsValid()) resource_descriptor_allocator_.Free(imgui_fallback_srv_);
        imgui_font_srv_ = {};
        imgui_fallback_srv_ = {};
        imgui_texture_request_addresses_.clear();
        imgui_texture_requests_.clear();
    }
#endif

    void D3D12DeviceContext::ReleaseScene3DRenderTargets() noexcept
    {
        for (Scene3DTarget& target : scene3d_gbuffer_)
        {
            if (target.resource) resource_state_tracker_.Forget(target.resource.Get());
            target.resource.Reset();
            if (target.rtv.IsValid()) rtv_allocator_.Free(target.rtv);
            if (target.srv.IsValid()) resource_descriptor_allocator_.Free(target.srv);
            target = {};
        }
        if (scene3d_depth_.resource) resource_state_tracker_.Forget(scene3d_depth_.resource.Get());
        scene3d_depth_.resource.Reset();
        if (scene3d_depth_.dsv.IsValid()) dsv_allocator_.Free(scene3d_depth_.dsv);
        if (scene3d_depth_.srv.IsValid()) resource_descriptor_allocator_.Free(scene3d_depth_.srv);
        scene3d_depth_ = {};
        if (scene3d_temporal_input_.resource)
            resource_state_tracker_.Forget(scene3d_temporal_input_.resource.Get());
        scene3d_temporal_input_.resource.Reset();
        if (scene3d_temporal_input_.rtv.IsValid()) rtv_allocator_.Free(scene3d_temporal_input_.rtv);
        if (scene3d_temporal_input_.srv.IsValid())
            resource_descriptor_allocator_.Free(scene3d_temporal_input_.srv);
        scene3d_temporal_input_ = {};
        if (scene3d_ssao_.resource)
            resource_state_tracker_.Forget(scene3d_ssao_.resource.Get());
        scene3d_ssao_.resource.Reset();
        if (scene3d_ssao_.rtv.IsValid()) rtv_allocator_.Free(scene3d_ssao_.rtv);
        if (scene3d_ssao_.srv.IsValid())
            resource_descriptor_allocator_.Free(scene3d_ssao_.srv);
        scene3d_ssao_ = {};
        if (scene3d_taa_resolved_.resource)
            resource_state_tracker_.Forget(scene3d_taa_resolved_.resource.Get());
        scene3d_taa_resolved_.resource.Reset();
        if (scene3d_taa_resolved_.rtv.IsValid()) rtv_allocator_.Free(scene3d_taa_resolved_.rtv);
        if (scene3d_taa_resolved_.srv.IsValid())
            resource_descriptor_allocator_.Free(scene3d_taa_resolved_.srv);
        scene3d_taa_resolved_ = {};
        if (scene3d_history_.resource)
            resource_state_tracker_.Forget(scene3d_history_.resource.Get());
        scene3d_history_.resource.Reset();
        if (scene3d_history_.srv.IsValid())
            resource_descriptor_allocator_.Free(scene3d_history_.srv);
        scene3d_history_ = {};
        if (scene3d_ssr_history_.resource)
            resource_state_tracker_.Forget(scene3d_ssr_history_.resource.Get());
        scene3d_ssr_history_.resource.Reset();
        if (scene3d_ssr_history_.srv.IsValid())
            resource_descriptor_allocator_.Free(scene3d_ssr_history_.srv);
        scene3d_ssr_history_ = {};
        if (scene3d_depth_history_.resource)
            resource_state_tracker_.Forget(scene3d_depth_history_.resource.Get());
        scene3d_depth_history_.resource.Reset();
        if (scene3d_depth_history_.srv.IsValid())
            resource_descriptor_allocator_.Free(scene3d_depth_history_.srv);
        scene3d_depth_history_ = {};
        scene3d_history_valid_ = false;
        scene3d_width_ = 0;
        scene3d_height_ = 0;
    }

    void D3D12DeviceContext::ReleaseScene3DShadowTargets() noexcept
    {
        const auto release_target = [this](Scene3DShadowTarget& target) noexcept
        {
            if (target.resource) resource_state_tracker_.Forget(target.resource.Get());
            target.resource.Reset();
            if (target.dsv.IsValid()) dsv_allocator_.Free(target.dsv);
            if (target.srv.IsValid()) resource_descriptor_allocator_.Free(target.srv);
            target = {};
        };
        release_target(scene3d_directional_shadow_);
        release_target(scene3d_local_shadow_);
    }

    bool D3D12DeviceContext::EnsureScene3DShadowTargets(
        const D3D12StaticSceneSubmission& submission) noexcept
    {
        if (device_ == nullptr) return false;
        const bool need_directional = submission.directional_shadow.enabled &&
            submission.directional_shadow.resolution != 0;
        const bool need_local = submission.local_shadows.enabled &&
            submission.local_shadows.used_slice_mask != 0 &&
            submission.local_shadows.resolution != 0;

        const bool directional_matches = !need_directional ||
            (scene3d_directional_shadow_.resource &&
                scene3d_directional_shadow_.resolution == submission.directional_shadow.resolution &&
                scene3d_directional_shadow_.array_size == D3D12DirectionalShadowSubmission::CascadeCount);
        const bool local_matches = !need_local ||
            (scene3d_local_shadow_.resource &&
                scene3d_local_shadow_.resolution == submission.local_shadows.resolution &&
                scene3d_local_shadow_.array_size == D3D12LocalShadowSubmission::SliceCount);
        const bool stale_directional = !need_directional && scene3d_directional_shadow_.resource != nullptr;
        const bool stale_local = !need_local && scene3d_local_shadow_.resource != nullptr;
        if (directional_matches && local_matches && !stale_directional && !stale_local)
            return true;

        // Shadowの解像度・有効状態の変更は稀なので、ここで待機して
        // 前フレームが参照中のDSV/SRVを再利用せず、Descriptor/Resourceの寿命を守る。
        if ((scene3d_directional_shadow_.resource || scene3d_local_shadow_.resource) && !WaitForGpu())
            return false;
        ReleaseScene3DShadowTargets();

        const auto create_target = [this](Scene3DShadowTarget& target,
            std::uint32_t resolution, std::uint32_t array_size,
            const wchar_t* name) noexcept -> bool
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = resolution;
            desc.Height = resolution;
            desc.DepthOrArraySize = static_cast<UINT16>(array_size);
            desc.MipLevels = 1;
            desc.Format = kScene3DDepthResourceFormat;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            D3D12_CLEAR_VALUE clear{};
            clear.Format = kScene3DDepthDsvFormat;
            clear.DepthStencil.Depth = 1.0f;
            clear.DepthStencil.Stencil = 0;
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                IID_PPV_ARGS(&target.resource))))
                return false;
            SetD3D12ObjectName(target.resource.Get(), name != nullptr ? name : L"Scene3D.Shadow", L"DepthArray");
            if (!dsv_allocator_.Allocate(array_size, target.dsv) ||
                !resource_descriptor_allocator_.Allocate(1, target.srv))
                return false;

            for (std::uint32_t slice = 0; slice < array_size; ++slice)
            {
                D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
                dsv.Format = kScene3DDepthDsvFormat;
                dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                dsv.Texture2DArray.MipSlice = 0;
                dsv.Texture2DArray.FirstArraySlice = slice;
                dsv.Texture2DArray.ArraySize = 1;
                device_->CreateDepthStencilView(target.resource.Get(), &dsv,
                    dsv_allocator_.CpuHandle(target.dsv.index + slice));
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Format = kScene3DDepthSrvFormat;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            srv.Texture2DArray.MostDetailedMip = 0;
            srv.Texture2DArray.MipLevels = 1;
            srv.Texture2DArray.FirstArraySlice = 0;
            srv.Texture2DArray.ArraySize = array_size;
            device_->CreateShaderResourceView(target.resource.Get(), &srv, target.srv.cpu);
            target.resolution = resolution;
            target.array_size = array_size;
            resource_state_tracker_.Track(target.resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
            return true;
        };

        if (need_directional && !create_target(scene3d_directional_shadow_,
            submission.directional_shadow.resolution,
            D3D12DirectionalShadowSubmission::CascadeCount, L"Scene3D.ShadowDirectional"))
        {
            ReleaseScene3DShadowTargets();
            return false;
        }
        if (need_local && !create_target(scene3d_local_shadow_,
            submission.local_shadows.resolution, D3D12LocalShadowSubmission::SliceCount,
            L"Scene3D.ShadowLocal"))
        {
            ReleaseScene3DShadowTargets();
            return false;
        }
        return true;
    }

    void D3D12DeviceContext::ReleaseScene3DRendererResources() noexcept
    {
        ReleaseScene3DRenderTargets();
        ReleaseScene3DShadowTargets();
        skinned_mesh_cache_.clear();
        skinned_mesh_bounds_cache_.clear();
        scene3d_motion_history_.clear();
        scene3d_frame_serial_ = 0;
        scene3d_history_write_serial_ = 0;
        for (auto& pso : scene3d_static_gbuffer_pipelines_) pso.Reset();
        for (auto& pso : scene3d_skinned_gbuffer_pipelines_) pso.Reset();
        for (auto& pso : scene3d_static_layer_pipelines_) pso.Reset();
        for (auto& pso : scene3d_skinned_layer_pipelines_) pso.Reset();
        for (auto& pso : scene3d_static_depth_pipelines_) pso.Reset();
        for (auto& pso : scene3d_skinned_depth_pipelines_) pso.Reset();
        for (auto& pso : scene3d_static_forward_blend_pipelines_) pso.Reset();
        for (auto& pso : scene3d_skinned_forward_blend_pipelines_) pso.Reset();
        for (auto& pso : scene3d_static_model_effect_pipelines_) pso.Reset();
        for (auto& pso : scene3d_skinned_model_effect_pipelines_) pso.Reset();
        for (auto& pso : scene3d_static_model_effect_extract_pipelines_) pso.Reset();
        for (auto& pso : scene3d_skinned_model_effect_extract_pipelines_) pso.Reset();
        for (auto& pso : scene3d_static_shadow_pipelines_) pso.Reset();
        for (auto& pso : scene3d_skinned_shadow_pipelines_) pso.Reset();
        scene3d_lighting_pipeline_.Reset();
        scene3d_skybox_pipeline_.Reset();
        scene3d_temporal_input_pipeline_.Reset();
        scene3d_taa_resolve_pipeline_.Reset();
        scene3d_postprocess_pipeline_.Reset();
        scene3d_geometry_root_signature_.Reset();
        scene3d_lighting_root_signature_.Reset();
        scene3d_shadow_root_signature_.Reset();
        scene3d_postprocess_root_signature_.Reset();
        scene3d_static_vs_.clear();
        scene3d_skinned_vs_.clear();
        scene3d_static_layer_vs_.clear();
        scene3d_skinned_layer_vs_.clear();
        scene3d_layer_ps_.clear();
        scene3d_gbuffer_ps_.clear();
        scene3d_depth_alpha_ps_.clear();
        scene3d_forward_ps_.clear();
        scene3d_model_effect_extract_ps_.clear();
        scene3d_fullscreen_vs_.clear();
        scene3d_lighting_ps_.clear();
        scene3d_skybox_vs_.clear();
        scene3d_skybox_ps_.clear();
        scene3d_temporal_input_ps_.clear();
        scene3d_taa_resolve_ps_.clear();
        scene3d_postprocess_ps_.clear();
        scene3d_ssao_ps_.clear();
        scene3d_shadow_static_vs_.clear();
        scene3d_shadow_skinned_vs_.clear();
        scene3d_shadow_alpha_ps_.clear();
        if (scene3d_null_directional_shadow_srv_.IsValid())
            resource_descriptor_allocator_.Free(scene3d_null_directional_shadow_srv_);
        if (scene3d_null_local_shadow_srv_.IsValid())
            resource_descriptor_allocator_.Free(scene3d_null_local_shadow_srv_);
        if (scene3d_null_ibl_diffuse_srv_.IsValid())
            resource_descriptor_allocator_.Free(scene3d_null_ibl_diffuse_srv_);
        if (scene3d_null_ibl_specular_srv_.IsValid())
            resource_descriptor_allocator_.Free(scene3d_null_ibl_specular_srv_);
        scene3d_null_directional_shadow_srv_ = {};
        scene3d_null_local_shadow_srv_ = {};
        scene3d_null_ibl_diffuse_srv_ = {};
        scene3d_null_ibl_specular_srv_ = {};
    }

    bool D3D12DeviceContext::EnsureScene3DRenderTargets() noexcept
    {
        if (device_ == nullptr || width_ == 0 || height_ == 0) return false;
        if (scene3d_width_ == width_ && scene3d_height_ == height_ &&
            scene3d_gbuffer_[0].resource && scene3d_depth_.resource &&
            scene3d_temporal_input_.resource && scene3d_taa_resolved_.resource &&
            scene3d_history_.resource &&
            scene3d_ssr_history_.resource && scene3d_depth_history_.resource)
            return true;
        ReleaseScene3DRenderTargets();

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texture{};
        texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width = width_;
        texture.Height = height_;
        texture.DepthOrArraySize = 1;
        texture.MipLevels = 1;
        texture.SampleDesc.Count = 1;
        texture.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        const float clear_color[4] = { 0, 0, 0, 0 };
        for (std::uint32_t i = 0; i < kScene3DGBufferCount; ++i)
        {
            Scene3DTarget& target = scene3d_gbuffer_[i];
            target.format = kScene3DGBufferFormats[i];
            texture.Format = target.format;
            D3D12_CLEAR_VALUE clear{};
            clear.Format = target.format;
            std::memcpy(clear.Color, clear_color, sizeof(clear_color));
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &texture, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear,
                IID_PPV_ARGS(&target.resource))))
            {
                ReleaseScene3DRenderTargets();
                return false;
            }
            SetD3D12ObjectName(target.resource.Get(), L"Scene3D.GBuffer", L"MRT", i);
            if (!rtv_allocator_.Allocate(1, target.rtv) ||
                !resource_descriptor_allocator_.Allocate(1, target.srv))
            {
                ReleaseScene3DRenderTargets();
                return false;
            }
            D3D12_RENDER_TARGET_VIEW_DESC rtv{};
            rtv.Format = target.format;
            rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device_->CreateRenderTargetView(target.resource.Get(), &rtv, target.rtv.cpu);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Format = target.format;
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MipLevels = 1;
            device_->CreateShaderResourceView(target.resource.Get(), &srv, target.srv.cpu);
            resource_state_tracker_.Track(target.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

        Scene3DTarget& temporal_input = scene3d_temporal_input_;
        temporal_input.format = kScene3DSceneTargetFormat;
        texture.Format = temporal_input.format;
        D3D12_CLEAR_VALUE temporal_input_clear{};
        temporal_input_clear.Format = temporal_input.format;
        std::memcpy(temporal_input_clear.Color, clear_color, sizeof(clear_color));
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &texture, D3D12_RESOURCE_STATE_RENDER_TARGET, &temporal_input_clear,
            IID_PPV_ARGS(&temporal_input.resource))) ||
            !rtv_allocator_.Allocate(1, temporal_input.rtv) ||
            !resource_descriptor_allocator_.Allocate(1, temporal_input.srv))
        {
            ReleaseScene3DRenderTargets();
            return false;
        }
        SetD3D12ObjectName(temporal_input.resource.Get(), L"Scene3D.TemporalInput", L"HDR");
        D3D12_RENDER_TARGET_VIEW_DESC temporal_input_rtv{};
        temporal_input_rtv.Format = temporal_input.format;
        temporal_input_rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device_->CreateRenderTargetView(temporal_input.resource.Get(), &temporal_input_rtv,
            temporal_input.rtv.cpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC temporal_input_srv{};
        temporal_input_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        temporal_input_srv.Format = temporal_input.format;
        temporal_input_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        temporal_input_srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(temporal_input.resource.Get(), &temporal_input_srv,
            temporal_input.srv.cpu);
        resource_state_tracker_.Track(temporal_input.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        Scene3DTarget& taa_resolved = scene3d_taa_resolved_;
        taa_resolved.format = kScene3DSceneTargetFormat;
        texture.Format = taa_resolved.format;
        D3D12_CLEAR_VALUE taa_resolved_clear{};
        taa_resolved_clear.Format = taa_resolved.format;
        std::memcpy(taa_resolved_clear.Color, clear_color, sizeof(clear_color));
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &texture, D3D12_RESOURCE_STATE_RENDER_TARGET, &taa_resolved_clear,
            IID_PPV_ARGS(&taa_resolved.resource))) ||
            !rtv_allocator_.Allocate(1, taa_resolved.rtv) ||
            !resource_descriptor_allocator_.Allocate(1, taa_resolved.srv))
        {
            ReleaseScene3DRenderTargets();
            return false;
        }
        SetD3D12ObjectName(taa_resolved.resource.Get(), L"Scene3D.TaaResolved", L"HDR");
        D3D12_RENDER_TARGET_VIEW_DESC taa_resolved_rtv{};
        taa_resolved_rtv.Format = taa_resolved.format;
        taa_resolved_rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device_->CreateRenderTargetView(taa_resolved.resource.Get(), &taa_resolved_rtv,
            taa_resolved.rtv.cpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC taa_resolved_srv{};
        taa_resolved_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        taa_resolved_srv.Format = taa_resolved.format;
        taa_resolved_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        taa_resolved_srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(taa_resolved.resource.Get(), &taa_resolved_srv,
            taa_resolved.srv.cpu);
        resource_state_tracker_.Track(taa_resolved.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        // TAA用の前フレームHDR履歴。
        D3D12_RESOURCE_DESC history_desc = texture;
        history_desc.Format = kScene3DSceneTargetFormat;
        D3D12_CLEAR_VALUE history_clear{};
        history_clear.Format = kScene3DSceneTargetFormat;
        std::memcpy(history_clear.Color, clear_color, sizeof(clear_color));
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &history_desc, D3D12_RESOURCE_STATE_COPY_DEST, &history_clear,
            IID_PPV_ARGS(&scene3d_history_.resource))) ||
            !resource_descriptor_allocator_.Allocate(1, scene3d_history_.srv))
        {
            ReleaseScene3DRenderTargets();
            return false;
        }
        // SSAO は半解像度で焼く。AO は低周波なので、フル解像度で計算する必要がない。
        {
            Scene3DTarget& ssao = scene3d_ssao_;
            ssao.format = DXGI_FORMAT_R8_UNORM;
            D3D12_RESOURCE_DESC ssao_desc = texture;
            ssao_desc.Width = (std::max)(1u, width_ / 2u);
            ssao_desc.Height = (std::max)(1u, height_ / 2u);
            ssao_desc.Format = ssao.format;
            D3D12_CLEAR_VALUE ssao_clear{};
            ssao_clear.Format = ssao.format;
            ssao_clear.Color[0] = 1.0f;
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &ssao_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &ssao_clear,
                IID_PPV_ARGS(&ssao.resource))) ||
                !rtv_allocator_.Allocate(1, ssao.rtv) ||
                !resource_descriptor_allocator_.Allocate(1, ssao.srv))
            {
                ReleaseScene3DRenderTargets();
                return false;
            }
            SetD3D12ObjectName(ssao.resource.Get(), L"Scene3D.Ssao", L"HalfRes");
            D3D12_RENDER_TARGET_VIEW_DESC ssao_rtv{};
            ssao_rtv.Format = ssao.format;
            ssao_rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device_->CreateRenderTargetView(ssao.resource.Get(), &ssao_rtv, ssao.rtv.cpu);
            D3D12_SHADER_RESOURCE_VIEW_DESC ssao_srv{};
            ssao_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            ssao_srv.Format = ssao.format;
            ssao_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            ssao_srv.Texture2D.MipLevels = 1;
            device_->CreateShaderResourceView(ssao.resource.Get(), &ssao_srv, ssao.srv.cpu);
            resource_state_tracker_.Track(ssao.resource.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

        SetD3D12ObjectName(scene3d_history_.resource.Get(), L"Scene3D.History", L"HDR");
        D3D12_SHADER_RESOURCE_VIEW_DESC history_srv{};
        history_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        history_srv.Format = kScene3DSceneTargetFormat;
        history_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        history_srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(scene3d_history_.resource.Get(), &history_srv,
            scene3d_history_.srv.cpu);
        resource_state_tracker_.Track(scene3d_history_.resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST);

        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &history_desc, D3D12_RESOURCE_STATE_COPY_DEST, &history_clear,
            IID_PPV_ARGS(&scene3d_ssr_history_.resource))) ||
            !resource_descriptor_allocator_.Allocate(1, scene3d_ssr_history_.srv))
        {
            ReleaseScene3DRenderTargets();
            return false;
        }
        SetD3D12ObjectName(scene3d_ssr_history_.resource.Get(), L"Scene3D.History", L"SSR");
        device_->CreateShaderResourceView(scene3d_ssr_history_.resource.Get(), &history_srv,
            scene3d_ssr_history_.srv.cpu);
        resource_state_tracker_.Track(scene3d_ssr_history_.resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_RESOURCE_DESC depth_desc = texture;
        depth_desc.Format = kScene3DDepthResourceFormat;
        depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE depth_clear{};
        depth_clear.Format = kScene3DDepthDsvFormat;
        depth_clear.DepthStencil.Depth = 1.0f;
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &depth_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear,
            IID_PPV_ARGS(&scene3d_depth_.resource))))
        {
            ReleaseScene3DRenderTargets();
            return false;
        }
        SetD3D12ObjectName(scene3d_depth_.resource.Get(), L"Scene3D.Depth", L"Main");
        if (!dsv_allocator_.Allocate(1, scene3d_depth_.dsv) ||
            !resource_descriptor_allocator_.Allocate(1, scene3d_depth_.srv))
        {
            ReleaseScene3DRenderTargets();
            return false;
        }
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = kScene3DDepthDsvFormat;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->CreateDepthStencilView(scene3d_depth_.resource.Get(), &dsv, scene3d_depth_.dsv.cpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC depth_srv{};
        depth_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depth_srv.Format = kScene3DDepthSrvFormat;
        depth_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depth_srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(scene3d_depth_.resource.Get(), &depth_srv,
            scene3d_depth_.srv.cpu);
        resource_state_tracker_.Track(scene3d_depth_.resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &depth_desc, D3D12_RESOURCE_STATE_COPY_DEST, &depth_clear,
            IID_PPV_ARGS(&scene3d_depth_history_.resource))) ||
            !resource_descriptor_allocator_.Allocate(1, scene3d_depth_history_.srv))
        {
            ReleaseScene3DRenderTargets();
            return false;
        }
        SetD3D12ObjectName(scene3d_depth_history_.resource.Get(), L"Scene3D.History", L"Depth");
        device_->CreateShaderResourceView(scene3d_depth_history_.resource.Get(), &depth_srv,
            scene3d_depth_history_.srv.cpu);
        resource_state_tracker_.Track(scene3d_depth_history_.resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST);
        scene3d_width_ = width_;
        scene3d_height_ = height_;
        return true;
    }

    bool D3D12DeviceContext::EnsureSkinnedMesh(const D3D12SkinnedMeshSource& source) noexcept
    {
        if (source.key.empty() || source.vertices.empty() || source.indices.empty()) return false;
        if (!CacheSkinnedMeshLocalBounds(source)) return false;
        if (skinned_mesh_cache_.find(source.key) != skinned_mesh_cache_.end()) return true;
        if (source.vertices.size() > ((std::numeric_limits<std::uint32_t>::max)() /
            sizeof(D3D12SkinnedVertex)) ||
            source.indices.size() > ((std::numeric_limits<std::uint32_t>::max)() /
                sizeof(std::uint32_t)))
            return false;
        auto mesh = std::make_unique<D3D12MeshBuffer>();
        const std::uint32_t vertex_bytes = static_cast<std::uint32_t>(
            source.vertices.size() * sizeof(D3D12SkinnedVertex));
        const std::uint32_t index_bytes = static_cast<std::uint32_t>(
            source.indices.size() * sizeof(std::uint32_t));
        if (!mesh->Upload(device_.Get(), upload_context_, source.vertices.data(), vertex_bytes,
            sizeof(D3D12SkinnedVertex), source.indices.data(), index_bytes, DXGI_FORMAT_R32_UINT))
            return false;
        mesh->SetDebugName(source.key);
        skinned_mesh_cache_.emplace(source.key, std::move(mesh));
        return true;
    }

    bool D3D12DeviceContext::PreloadScene3DResources(
        const D3D12StaticSceneSubmission& submission,
        bool allow_static_mesh_cache_replacement) noexcept
    {
        if (device_ == nullptr) return false;
        if (!CacheMeshLocalBounds(submission, allow_static_mesh_cache_replacement)) return false;
        for (const D3D12StaticShaderSource& source : submission.shader_sources)
            EnsureStaticShader(source);
        if (!upload_context_.BeginBatch()) return false;
        bool upload_ok = true;
        for (const D3D12StaticMeshSource& source : submission.mesh_sources)
        {
            if (source.replace_existing && allow_static_mesh_cache_replacement)
            {
                const auto existing = static_mesh_cache_.find(source.key);
                if (existing != static_mesh_cache_.end()) static_mesh_cache_.erase(existing);
            }
            if (static_mesh_cache_.find(source.key) == static_mesh_cache_.end() &&
                !EnsureStaticMesh(source))
                upload_ok = false;
        }
        for (const D3D12SkinnedMeshSource& source : submission.skinned_mesh_sources)
        {
            if (!EnsureSkinnedMesh(source)) upload_ok = false;
        }
        for (const D3D12StaticTextureSource& source : submission.texture_sources)
        {
            if (!source.key.empty() && texture_cache_.find(source.key) == texture_cache_.end() &&
                static_texture_failures_.find(source.key) == static_texture_failures_.end() &&
                !EnsureStaticTexture(source) &&
                static_texture_failures_.find(source.key) == static_texture_failures_.end())
                upload_ok = false;
        }
        if (upload_ok && submission.sky.enabled && !EnsureSkyEnvironment(submission.sky))
            upload_ok = false;
        const bool batch_ok = upload_context_.EndBatch();
        return upload_ok && batch_ok;
    }

    D3D12Scene3DStateSnapshot D3D12DeviceContext::CaptureScene3DState() const noexcept
    {
        D3D12Scene3DStateSnapshot snapshot;
        snapshot.static_mesh_cache_size = static_mesh_cache_.size();
        snapshot.skinned_mesh_cache_size = skinned_mesh_cache_.size();
        snapshot.texture_cache_size = texture_cache_.size();
        snapshot.static_mesh_bounds_cache_size = static_mesh_bounds_cache_.size();
        snapshot.skinned_mesh_bounds_cache_size = skinned_mesh_bounds_cache_.size();
        snapshot.motion_history_size = scene3d_motion_history_.size();
        snapshot.motion_frame_serial = scene3d_frame_serial_;
        snapshot.scene_history_write_serial = scene3d_history_write_serial_;
        snapshot.scene_effect_history_write_serial = scene_effect_history_write_serial_;
        snapshot.scene_effect_history_size = scene_effect_history_targets_.size();
        snapshot.scene_history_valid = scene3d_history_valid_;
        for (std::uint32_t index = 0; index < kScene3DGBufferCount; ++index)
        {
            ID3D12Resource* resource = scene3d_gbuffer_[index].resource.Get();
            snapshot.gbuffer_resources[index] = reinterpret_cast<std::uintptr_t>(resource);
            snapshot.gbuffer_states[index] = resource_state_tracker_.StateOf(resource);
        }
        ID3D12Resource* depth_resource = scene3d_depth_.resource.Get();
        ID3D12Resource* history_resource = scene3d_history_.resource.Get();
        ID3D12Resource* directional_shadow_resource = scene3d_directional_shadow_.resource.Get();
        ID3D12Resource* local_shadow_resource = scene3d_local_shadow_.resource.Get();
        snapshot.depth_resource = reinterpret_cast<std::uintptr_t>(depth_resource);
        snapshot.history_resource = reinterpret_cast<std::uintptr_t>(history_resource);
        snapshot.directional_shadow_resource = reinterpret_cast<std::uintptr_t>(
            directional_shadow_resource);
        snapshot.local_shadow_resource = reinterpret_cast<std::uintptr_t>(local_shadow_resource);
        snapshot.depth_state = resource_state_tracker_.StateOf(depth_resource);
        snapshot.history_state = resource_state_tracker_.StateOf(history_resource);
        snapshot.directional_shadow_state = resource_state_tracker_.StateOf(
            directional_shadow_resource);
        snapshot.local_shadow_state = resource_state_tracker_.StateOf(local_shadow_resource);
        snapshot.directional_shadow_resolution = scene3d_directional_shadow_.resolution;
        snapshot.local_shadow_resolution = scene3d_local_shadow_.resolution;
        snapshot.frame_constants = current_frame_constants_;
        return snapshot;
    }

    bool D3D12DeviceContext::DrawScene3D(
        const D3D12StaticSceneSubmission& submission,
        D3D12Scene3DDrawOptions options) noexcept
    {
        last_model_effect_stack_count_ = 0;
        last_scene_draw_call_count_ = 0;
        last_scene_triangle_count_ = 0;
        last_scene_vertex_count_ = 0;
        last_screen_effect_stack_count_ = 0;
        last_shadow_coverage_draw_count_ = 0;
        // どの前提で落ちたかを stderr から特定できるようにする。
        const auto scene3d_fail = [](const char* step) noexcept
        {
            char message[128]{};
            std::snprintf(message, sizeof(message),
                "[DX12] DrawScene3D aborted at %s\n", step);
            SceneDebugMessage(message);
            return false;
        };
        if (!frame_open_) return scene3d_fail("frame-not-open");
        if (scene3d_geometry_root_signature_ == nullptr ||
            scene3d_lighting_root_signature_ == nullptr ||
            scene3d_shadow_root_signature_ == nullptr || scene3d_lighting_pipeline_ == nullptr ||
            scene3d_skybox_pipeline_ == nullptr ||
            scene3d_postprocess_root_signature_ == nullptr ||
            scene3d_temporal_input_pipeline_ == nullptr || scene3d_taa_resolve_pipeline_ == nullptr ||
            scene3d_postprocess_pipeline_ == nullptr)
            return scene3d_fail("root-signature-or-pipeline");
        if (!scene3d_null_directional_shadow_srv_.IsValid() ||
            !scene3d_null_local_shadow_srv_.IsValid() ||
            !scene3d_null_ibl_diffuse_srv_.IsValid() ||
            !scene3d_null_ibl_specular_srv_.IsValid())
            return scene3d_fail("null-environment-srv");
        if (!CacheMeshLocalBounds(submission, options.allow_static_mesh_cache_replacement))
            return scene3d_fail("mesh-local-bounds");
        if (!EnsureScene3DRenderTargets()) return scene3d_fail("EnsureScene3DRenderTargets");
        if (options.manage_shadow_targets && !EnsureScene3DShadowTargets(submission))
            return scene3d_fail("EnsureScene3DShadowTargets");

        for (const D3D12StaticShaderSource& source : submission.shader_sources)
            EnsureStaticShader(source);
        if (!upload_context_.BeginBatch()) return scene3d_fail("upload BeginBatch");
        bool upload_ok = true;
        for (const D3D12StaticMeshSource& source : submission.mesh_sources)
        {
            if (source.replace_existing && options.allow_static_mesh_cache_replacement)
            {
                const auto existing = static_mesh_cache_.find(source.key);
                if (existing != static_mesh_cache_.end())
                    static_mesh_cache_.erase(existing);
            }
            if (static_mesh_cache_.find(source.key) == static_mesh_cache_.end() &&
                !EnsureStaticMesh(source))
                upload_ok = false;
        }
        for (const D3D12SkinnedMeshSource& source : submission.skinned_mesh_sources)
        {
            if (!EnsureSkinnedMesh(source))
                upload_ok = false;
        }
        for (const D3D12StaticTextureSource& source : submission.texture_sources)
        {
            if (!source.key.empty() && texture_cache_.find(source.key) == texture_cache_.end() &&
                static_texture_failures_.find(source.key) == static_texture_failures_.end())
            {
                if (!EnsureStaticTexture(source) &&
                    static_texture_failures_.find(source.key) == static_texture_failures_.end())
                    upload_ok = false;
            }
        }
        for (const D3D12StaticTextureSource& source : scene_effect_submission_.texture_sources)
        {
            if (!source.key.empty() && texture_cache_.find(source.key) == texture_cache_.end() &&
                static_texture_failures_.find(source.key) == static_texture_failures_.end())
            {
                if (!EnsureStaticTexture(source) &&
                    static_texture_failures_.find(source.key) == static_texture_failures_.end())
                    upload_ok = false;
            }
        }
#ifdef USE_IMGUI
        for (const auto& request_entry : imgui_texture_requests_)
        {
            const ImGuiTextureRequest& request = *request_entry.second;
            if (texture_cache_.find(request.key) != texture_cache_.end() ||
                static_texture_failures_.find(request.key) != static_texture_failures_.end())
                continue;
            D3D12StaticTextureSource source;
            source.key = request.key;
            source.source_path = request.source_path;
            if (!EnsureStaticTexture(source) &&
                static_texture_failures_.find(source.key) == static_texture_failures_.end())
                upload_ok = false;
        }
#endif
        const bool batch_ok = upload_context_.EndBatch();
        if (!upload_ok) return scene3d_fail("mesh/texture/shader upload");
        if (!batch_ok) return scene3d_fail("upload EndBatch");
        const auto white = texture_cache_.find("__dx12_white");
        if (white == texture_cache_.end()) return scene3d_fail("white texture missing");

        D3D12LinearUploadAllocator& allocator = frame_resources_[frame_index_].upload_allocator;
        const auto allocate_bytes = [&allocator](const void* data, std::size_t size,
            std::uint64_t alignment, D3D12_GPU_VIRTUAL_ADDRESS& gpu) noexcept -> bool
        {
            const std::size_t actual = (std::max)(size, static_cast<std::size_t>(16));
            D3D12UploadAllocation allocation{};
            if (!allocator.Allocate(actual, alignment, allocation)) return false;
            std::memset(allocation.cpu, 0, actual);
            if (data != nullptr && size != 0) std::memcpy(allocation.cpu, data, size);
            gpu = allocation.gpu;
            return true;
        };
        const auto allocate_cb = [&allocate_bytes](const void* data, std::size_t size,
            D3D12_GPU_VIRTUAL_ADDRESS& gpu) noexcept -> bool
        {
            return allocate_bytes(data, size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, gpu);
        };

        const DirectX::XMFLOAT4X4 identity{
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        const std::vector<DirectX::XMFLOAT4X4> identity_palette{ identity };
        D3D12_GPU_VIRTUAL_ADDRESS identity_bone_gpu = 0;
        if (!allocate_bytes(&identity, sizeof(identity), 16, identity_bone_gpu)) return scene3d_fail("identity bone upload");

        Scene3DSceneConstants scene{};
        scene.view_projection = current_frame_constants_.view_projection;
        scene.previous_view_projection = current_frame_constants_.prev_view_projection;
        D3D12_GPU_VIRTUAL_ADDRESS scene_gpu = 0;
        if (!allocate_cb(&scene, sizeof(scene), scene_gpu)) return scene3d_fail("scene constants upload");

        const bool directional_shadow_available = submission.directional_shadow.enabled &&
            scene3d_directional_shadow_.resource != nullptr &&
            scene3d_directional_shadow_.srv.IsValid();
        const bool local_shadow_available = submission.local_shadows.enabled &&
            submission.local_shadows.used_slice_mask != 0 &&
            scene3d_local_shadow_.resource != nullptr && scene3d_local_shadow_.srv.IsValid();

        const StaticTextureResource* sky_source_texture = nullptr;
        const StaticTextureResource* ibl_diffuse_texture = nullptr;
        const StaticTextureResource* ibl_specular_texture = nullptr;
        const StaticTextureResource* sky_source_texture2 = nullptr;
        const StaticTextureResource* ibl_diffuse_texture2 = nullptr;
        const StaticTextureResource* ibl_specular_texture2 = nullptr;
        if (submission.sky.enabled && !submission.sky.texture_key.empty())
        {
            const auto sky_source = texture_cache_.find(submission.sky.texture_key);
            if (sky_source != texture_cache_.end() && sky_source->second.is_cube &&
                sky_source->second.srv.IsValid())
            {
                sky_source_texture = &sky_source->second;
                const auto diffuse = texture_cache_.find(
                    submission.sky.texture_key + ":ibl_diffuse");
                const auto specular = texture_cache_.find(
                    submission.sky.texture_key + ":ibl_specular");
                ibl_diffuse_texture = diffuse != texture_cache_.end() &&
                    diffuse->second.is_cube ? &diffuse->second : sky_source_texture;
                ibl_specular_texture = specular != texture_cache_.end() &&
                    specular->second.is_cube ? &specular->second : sky_source_texture;
            }
            if (!submission.sky.secondary_texture_key.empty())
            {
                const auto sky_source2 = texture_cache_.find(
                    submission.sky.secondary_texture_key);
                if (sky_source2 != texture_cache_.end() && sky_source2->second.is_cube &&
                    sky_source2->second.srv.IsValid())
                {
                    sky_source_texture2 = &sky_source2->second;
                    const auto diffuse2 = texture_cache_.find(
                        submission.sky.secondary_texture_key + ":ibl_diffuse");
                    const auto specular2 = texture_cache_.find(
                        submission.sky.secondary_texture_key + ":ibl_specular");
                    ibl_diffuse_texture2 = diffuse2 != texture_cache_.end() &&
                        diffuse2->second.is_cube ? &diffuse2->second : sky_source_texture2;
                    ibl_specular_texture2 = specular2 != texture_cache_.end() &&
                        specular2->second.is_cube ? &specular2->second : sky_source_texture2;
                }
            }
        }
        const bool sky_available = sky_source_texture != nullptr;
        const bool sky_secondary_available = sky_source_texture2 != nullptr;
        const D3D12_GPU_DESCRIPTOR_HANDLE ibl_diffuse_srv = sky_available &&
            ibl_diffuse_texture != nullptr ? ibl_diffuse_texture->srv.gpu :
            scene3d_null_ibl_diffuse_srv_.gpu;
        const D3D12_GPU_DESCRIPTOR_HANDLE ibl_specular_srv = sky_available &&
            ibl_specular_texture != nullptr ? ibl_specular_texture->srv.gpu :
            scene3d_null_ibl_specular_srv_.gpu;
        const D3D12_GPU_DESCRIPTOR_HANDLE sky_source_srv = sky_available
            ? sky_source_texture->srv.gpu : scene3d_null_ibl_diffuse_srv_.gpu;
        const D3D12_GPU_DESCRIPTOR_HANDLE ibl_diffuse_srv2 = sky_secondary_available &&
            ibl_diffuse_texture2 != nullptr ? ibl_diffuse_texture2->srv.gpu : ibl_diffuse_srv;
        const D3D12_GPU_DESCRIPTOR_HANDLE ibl_specular_srv2 = sky_secondary_available &&
            ibl_specular_texture2 != nullptr ? ibl_specular_texture2->srv.gpu : ibl_specular_srv;
        const D3D12_GPU_DESCRIPTOR_HANDLE sky_source_srv2 = sky_secondary_available
            ? sky_source_texture2->srv.gpu : sky_source_srv;

        Scene3DLightingConstants light{};
        light.inverse_view_projection = current_frame_constants_.inv_view_projection;
        light.view = current_frame_constants_.view;
        light.camera_position = current_frame_constants_.camera_position;
        light.previous_view_projection = current_frame_constants_.prev_view_projection;
        light.sky_rotation = submission.sky.rotation;
        light.sky_jitter = current_frame_constants_.jitter;
        light.ibl_params = { sky_available ? 1.0f : 0.0f,
            sky_available ? 1.0f : 0.0f,
            sky_available ? 1.0f : 0.0f, sky_available ?
            (std::max)(0.0f, (std::min)(submission.sky.intensity, 16.0f)) : 0.0f };
        light.previous_sky_rotation = submission.sky.previous_rotation;
        light.toon_environment = { sky_available ?
            (std::max)(0.0f, submission.sky.toon_environment) : 0.0f, 0.0f, 0.0f, 0.0f };
        light.sky_blend = { sky_available ? (std::max)(0.0f,
            (std::min)(submission.sky.blend, 1.0f)) : 0.0f,
            sky_secondary_available ? 1.0f : 0.0f,
            sky_available ? (std::max)(0.0f, (std::min)(submission.sky.time, 1.0f)) : 0.0f,
            0.0f };
        light.sky_motion = { sky_available && std::isfinite(submission.sky.cloud_time)
            ? submission.sky.cloud_time : 0.0f,
            sky_available && std::isfinite(submission.sky.previous_cloud_time)
                ? submission.sky.previous_cloud_time : 0.0f, 0.0f, 0.0f };
        const bool clouds_enabled = sky_available && submission.sky.clouds_enabled;
        light.cloud_layer1_params = {
            clouds_enabled && std::isfinite(submission.sky.cloud_layer1_speed.x)
                ? submission.sky.cloud_layer1_speed.x : 0.0f,
            clouds_enabled && std::isfinite(submission.sky.cloud_layer1_speed.y)
                ? submission.sky.cloud_layer1_speed.y : 0.0f,
            clouds_enabled && std::isfinite(submission.sky.cloud_layer1_scale)
                ? (std::max)(0.1f, submission.sky.cloud_layer1_scale) : 1.0f,
            clouds_enabled && std::isfinite(submission.sky.cloud_layer1_density)
                ? (std::max)(0.0f, (std::min)(1.0f, submission.sky.cloud_layer1_density)) : 0.0f };
        light.cloud_layer2_params = {
            clouds_enabled && std::isfinite(submission.sky.cloud_layer2_speed.x)
                ? submission.sky.cloud_layer2_speed.x : 0.0f,
            clouds_enabled && std::isfinite(submission.sky.cloud_layer2_speed.y)
                ? submission.sky.cloud_layer2_speed.y : 0.0f,
            clouds_enabled && std::isfinite(submission.sky.cloud_layer2_scale)
                ? (std::max)(0.1f, submission.sky.cloud_layer2_scale) : 1.0f,
            clouds_enabled && std::isfinite(submission.sky.cloud_layer2_density)
                ? (std::max)(0.0f, (std::min)(1.0f, submission.sky.cloud_layer2_density)) : 0.0f };
        light.cloud_layer1_color = submission.sky.cloud_layer1_color;
        light.cloud_layer2_color = submission.sky.cloud_layer2_color;
        light.star_params = { sky_available && submission.sky.stars_enabled &&
            std::isfinite(submission.sky.star_density)
                ? (std::max)(0.0f, (std::min)(1.0f, submission.sky.star_density)) : 0.0f,
            sky_available && submission.sky.stars_enabled &&
            std::isfinite(submission.sky.star_intensity)
                ? (std::max)(0.0f, submission.sky.star_intensity) : 0.0f,
            sky_available && submission.sky.stars_enabled ? 1.0f : 0.0f, 0.0f };
        light.star_color = submission.sky.star_color;
        light.moon_params = { sky_available && submission.sky.moon_enabled &&
            std::isfinite(submission.sky.moon_size)
                ? (std::max)(0.001f, (std::min)(0.5f, submission.sky.moon_size)) : 0.04f,
            sky_available && submission.sky.moon_enabled &&
            std::isfinite(submission.sky.moon_intensity)
                ? (std::max)(0.0f, submission.sky.moon_intensity) : 0.0f,
            sky_available && submission.sky.moon_enabled ? 1.0f : 0.0f, 0.0f };
        light.moon_direction = { submission.sky.moon_direction.x,
            submission.sky.moon_direction.y, submission.sky.moon_direction.z, 0.0f };
        light.moon_color = submission.sky.moon_color;
        light.directional_direction_intensity = {
            submission.directional_light.direction.x,
            submission.directional_light.direction.y,
            submission.directional_light.direction.z,
            submission.directional_light.intensity };
        light.directional_color_flags = {
            submission.directional_light.color.x,
            submission.directional_light.color.y,
            submission.directional_light.color.z,
            submission.directional_light.enabled ? 1.0f : 0.0f };

        const std::uint32_t point_count = (std::min)(
            static_cast<std::uint32_t>(submission.point_lights.size()), 8u);
        const std::uint32_t spot_count = (std::min)(
            static_cast<std::uint32_t>(submission.spot_lights.size()), 4u);
        light.counts = { point_count, spot_count, 0u, 0u };
        for (std::uint32_t i = 0; i < point_count; ++i)
        {
            const auto& source = submission.point_lights[i];
            const bool slice_range_valid = source.shadow_slice >= 0 &&
                source.shadow_slice + 5 < static_cast<std::int32_t>(D3D12LocalShadowSubmission::SliceCount);
            std::uint32_t required_mask = 0;
            if (slice_range_valid)
            {
                for (std::int32_t face = 0; face < 6; ++face)
                    required_mask |= (1u << static_cast<std::uint32_t>(source.shadow_slice + face));
            }
            const bool shadow_valid = local_shadow_available && source.cast_shadows &&
                slice_range_valid &&
                (submission.local_shadows.used_slice_mask & required_mask) == required_mask;
            light.point_lights[i].position_range = {
                source.position.x, source.position.y, source.position.z, source.range };
            light.point_lights[i].color_intensity = {
                source.color.x, source.color.y, source.color.z, source.intensity };
            light.point_lights[i].shadow = {
                shadow_valid ? static_cast<float>(source.shadow_slice) : -1.0f,
                source.shadow_strength, shadow_valid ? 1.0f : 0.0f, 0.0f };
        }
        for (std::uint32_t i = 0; i < spot_count; ++i)
        {
            const auto& source = submission.spot_lights[i];
            const bool slice_valid = source.shadow_slice >= 0 &&
                source.shadow_slice < static_cast<std::int32_t>(D3D12LocalShadowSubmission::SliceCount) &&
                (submission.local_shadows.used_slice_mask &
                    (1u << static_cast<std::uint32_t>(source.shadow_slice))) != 0;
            const bool shadow_valid = local_shadow_available && source.cast_shadows && slice_valid;
            light.spot_lights[i].position_range = {
                source.position.x, source.position.y, source.position.z, source.range };
            light.spot_lights[i].direction_inner = {
                source.direction.x, source.direction.y, source.direction.z, source.inner_cos };
            light.spot_lights[i].color_outer = {
                source.color.x, source.color.y, source.color.z, source.outer_cos };
            light.spot_lights[i].params = {
                source.intensity,
                shadow_valid ? static_cast<float>(source.shadow_slice) : -1.0f,
                source.shadow_strength, 0.0f };
        }
        for (std::uint32_t cascade = 0;
            cascade < D3D12DirectionalShadowSubmission::CascadeCount; ++cascade)
            light.csm_view_projection[cascade] = submission.directional_shadow.view_projection[cascade];
        light.csm_split_distances = submission.directional_shadow.split_distances;
        light.csm_params = submission.directional_shadow.params;
        light.csm_params2 = submission.directional_shadow.params2;
        light.csm_params3 = submission.directional_shadow.params3;
        light.csm_texel_world = submission.directional_shadow.texel_world;
        for (std::uint32_t slice = 0; slice < D3D12LocalShadowSubmission::SliceCount; ++slice)
        {
            light.local_shadow_slices[slice].view_projection =
                submission.local_shadows.slices[slice].view_projection;
            light.local_shadow_slices[slice].params = submission.local_shadows.slices[slice].params;
        }
        light.shadow_flags = {
            directional_shadow_available ? 1u : 0u,
            local_shadow_available ? 1u : 0u, 0u, 0u };
        light.debug_flags = { submission.post_process.deferred_debug_mode, 0u, 0u, 0u };
        D3D12_GPU_VIRTUAL_ADDRESS light_gpu = 0;
        if (!allocate_cb(&light, sizeof(light), light_gpu)) return false;

        struct PreparedSkinnedDraw final
        {
            D3D12_GPU_VIRTUAL_ADDRESS current_bones = 0;
            D3D12_GPU_VIRTUAL_ADDRESS previous_bones = 0;
            DirectX::XMFLOAT4X4 previous_world{};
            const std::vector<DirectX::XMFLOAT4X4>* current_palette = nullptr;
            std::string history_key;
        };
        std::vector<PreparedSkinnedDraw> prepared_skinned(submission.skinned_draws.size());
        for (std::size_t i = 0; i < submission.skinned_draws.size(); ++i)
        {
            const D3D12SkinnedDrawItem& draw = submission.skinned_draws[i];
            PreparedSkinnedDraw& prepared = prepared_skinned[i];
            prepared.current_palette = draw.bone_palette.empty() ? &identity_palette : &draw.bone_palette;
            const std::string& history_key = !draw.motion_key.empty()
                ? draw.motion_key : draw.surface.motion_key;
            prepared.history_key = history_key;
            const std::vector<DirectX::XMFLOAT4X4>* previous_palette = prepared.current_palette;
            prepared.previous_world = draw.surface.world;
            if (options.read_motion_history && !history_key.empty())
            {
                Scene3DMotionHistory& history = scene3d_motion_history_[history_key];
                if (history.valid)
                {
                    // スケルトンまたはAssetの変更で前回Paletteが短くなった場合は、
                    // 現在の頂点が参照する範囲を満たす現在Paletteへ戻す。
                    if (history.bones.size() >= prepared.current_palette->size())
                        previous_palette = &history.bones;
                    prepared.previous_world = history.world;
                }
            }
            if (!allocate_bytes(prepared.current_palette->data(),
                    prepared.current_palette->size() * sizeof(DirectX::XMFLOAT4X4), 16,
                    prepared.current_bones) ||
                !allocate_bytes(previous_palette->data(),
                    previous_palette->size() * sizeof(DirectX::XMFLOAT4X4), 16,
                    prepared.previous_bones))
                return false;
        }

        ID3D12DescriptorHeap* heaps[] = { resource_descriptor_allocator_.Heap() };
        command_list_->SetDescriptorHeaps(1, heaps);
        const D3D12_GPU_DESCRIPTOR_HANDLE directional_shadow_srv = directional_shadow_available
            ? scene3d_directional_shadow_.srv.gpu : scene3d_null_directional_shadow_srv_.gpu;
        const D3D12_GPU_DESCRIPTOR_HANDLE local_shadow_srv = local_shadow_available
            ? scene3d_local_shadow_.srv.gpu : scene3d_null_local_shadow_srv_.gpu;

        const auto texture_for = [&white, this](const D3D12StaticDrawItem& draw)
            -> const StaticTextureResource*
        {
            if (!draw.base_color_texture_key.empty())
            {
                const auto it = texture_cache_.find(draw.base_color_texture_key);
                if (it != texture_cache_.end()) return &it->second;
            }
            return &white->second;
        };
        const auto material_texture_for = [this](const D3D12StaticDrawItem& draw,
            std::uint32_t slot, const char* fallback_key) -> const StaticTextureResource*
        {
            for (const D3D12StaticMaterialTexture& mapped : draw.material_textures)
            {
                if (mapped.slot != slot) continue;
                const auto found = texture_cache_.find(mapped.texture_key);
                if (found != texture_cache_.end()) return &found->second;
                break;
            }
            const auto fallback = texture_cache_.find(fallback_key);
            return fallback != texture_cache_.end() ? &fallback->second : nullptr;
        };
        const auto draw_mesh = [this](D3D12MeshBuffer& mesh,
            const D3D12StaticDrawItem& draw)
        {
            const D3D12_VERTEX_BUFFER_VIEW vb = mesh.VertexView();
            const D3D12_INDEX_BUFFER_VIEW ib = mesh.IndexView();
            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            command_list_->IASetVertexBuffers(0, 1, &vb);
            command_list_->IASetIndexBuffer(&ib);
            const std::uint32_t available = mesh.IndexCount();
            const std::uint32_t start = (std::min)(draw.start_index, available);
            const std::uint32_t remaining = available - start;
            const std::uint32_t count = draw.index_count == 0
                ? remaining : (std::min)(draw.index_count, remaining);
            if (count != 0)
            {
                command_list_->DrawIndexedInstanced(count, 1, start, 0, 0);
                ++last_scene_draw_call_count_;
                last_scene_triangle_count_ += count / 3u;
                last_scene_vertex_count_ += vb.StrideInBytes != 0
                    ? vb.SizeInBytes / vb.StrideInBytes : 0u;
            }
        };
        const auto bind_surface = [this, &allocate_cb, scene_gpu, light_gpu, identity_bone_gpu,
            directional_shadow_srv, local_shadow_srv, ibl_diffuse_srv, ibl_specular_srv,
            ibl_diffuse_srv2, ibl_specular_srv2,
            &texture_for, &material_texture_for](
            const D3D12StaticDrawItem& draw, const DirectX::XMFLOAT4X4& previous_world,
            D3D12_GPU_VIRTUAL_ADDRESS current_bones,
            D3D12_GPU_VIRTUAL_ADDRESS previous_bones, float morph_weight,
            bool match_deferred_material) noexcept -> bool
        {
            Scene3DObjectConstants object{};
            object.world = draw.world;
            object.previous_world = previous_world;
            object.morph = { morph_weight, 0, 0, 0 };
            D3D12_GPU_VIRTUAL_ADDRESS object_gpu = 0;
            if (!allocate_cb(&object, sizeof(object), object_gpu)) return false;
            Scene3DMaterialConstants material{};
            material.base_color = draw.base_color;
            material.builtin_params = draw.builtin_params;
            material.builtin_params1 = draw.builtin_params1;
            material.builtin_params2 = draw.builtin_params2;
            material.builtin_params3 = draw.builtin_params3;
            material.builtin_params3.w = match_deferred_material ? 1.0f : 0.0f;
            material.normal_adjust_center = draw.normal_adjust_center;
            material.normal_adjust_params = draw.normal_adjust_params;
            material.emissive_strength = {
                draw.emissive.x, draw.emissive.y, draw.emissive.z, draw.emissive_strength };
            material.surface_params = {
                draw.metallic, draw.roughness, draw.ambient_occlusion, draw.alpha_cutoff };
            material.render_params = {
                static_cast<float>(static_cast<std::uint32_t>(draw.alpha_mode)),
                static_cast<float>(draw.lighting_model), draw.receive_shadow ? 1.0f : 0.0f,
                static_cast<float>(draw.material_texture_semantic_mask) };
            D3D12_GPU_VIRTUAL_ADDRESS material_gpu = 0;
            if (!allocate_cb(&material, sizeof(material), material_gpu)) return false;
            command_list_->SetGraphicsRootConstantBufferView(0, object_gpu);
            command_list_->SetGraphicsRootConstantBufferView(1, scene_gpu);
            command_list_->SetGraphicsRootConstantBufferView(2, material_gpu);
            command_list_->SetGraphicsRootConstantBufferView(3, light_gpu);
            command_list_->SetGraphicsRootShaderResourceView(4,
                current_bones ? current_bones : identity_bone_gpu);
            command_list_->SetGraphicsRootShaderResourceView(5,
                previous_bones ? previous_bones : identity_bone_gpu);
            const StaticTextureResource* normal = material_texture_for(draw, 41u, "__dx12_bump");
            const StaticTextureResource* metallic = material_texture_for(draw, 42u, "__dx12_white");
            const StaticTextureResource* roughness = material_texture_for(draw, 43u, "__dx12_white");
            const StaticTextureResource* emissive = material_texture_for(draw, 44u, "__dx12_black");
            const StaticTextureResource* occlusion = material_texture_for(draw, 45u, "__dx12_white");
            const StaticTextureResource* ramp = material_texture_for(draw, 46u, "__dx12_white");
            if (normal == nullptr || metallic == nullptr || roughness == nullptr ||
                emissive == nullptr || occlusion == nullptr || ramp == nullptr)
                return false;
            const StaticTextureResource* base = texture_for(draw);
            command_list_->SetGraphicsRootDescriptorTable(6, base->srgb_srv.IsValid()
                ? base->srgb_srv.gpu : base->srv.gpu);                                     // t0
            command_list_->SetGraphicsRootDescriptorTable(7, normal->srv.gpu);             // t1
            command_list_->SetGraphicsRootDescriptorTable(8, metallic->srv.gpu);           // t2
            command_list_->SetGraphicsRootDescriptorTable(9, roughness->srv.gpu);          // t3
            command_list_->SetGraphicsRootDescriptorTable(10, emissive->srgb_srv.IsValid()
                ? emissive->srgb_srv.gpu : emissive->srv.gpu);                              // t4
            command_list_->SetGraphicsRootDescriptorTable(11, occlusion->srv.gpu);          // t5
            command_list_->SetGraphicsRootDescriptorTable(12, directional_shadow_srv);      // t6
            command_list_->SetGraphicsRootDescriptorTable(13, local_shadow_srv);            // t7
            command_list_->SetGraphicsRootDescriptorTable(14, ramp->srv.gpu);               // t10
            command_list_->SetGraphicsRootDescriptorTable(17, ibl_diffuse_srv);             // t33
            command_list_->SetGraphicsRootDescriptorTable(18, ibl_specular_srv);            // t34
            command_list_->SetGraphicsRootDescriptorTable(19, ibl_diffuse_srv2);            // t36
            command_list_->SetGraphicsRootDescriptorTable(20, ibl_specular_srv2);           // t37
            return true;
        };
        const auto bind_layer = [this, &allocate_cb, scene_gpu, identity_bone_gpu](
            const D3D12StaticDrawItem& draw, const D3D12ShaderLayerPass& pass,
            const DirectX::XMFLOAT4X4& previous_world,
            D3D12_GPU_VIRTUAL_ADDRESS current_bones, float morph_weight) noexcept -> bool
        {
            Scene3DObjectConstants object{};
            object.world = draw.world;
            object.previous_world = previous_world;
            object.morph = { morph_weight, 0, 0, 0 };
            D3D12_GPU_VIRTUAL_ADDRESS object_gpu = 0;
            if (!allocate_cb(&object, sizeof(object), object_gpu)) return false;
            Scene3DLayerConstants layer{};
            layer.color = pass.color;
            layer.params = { pass.width, pass.opacity,
                static_cast<float>(static_cast<std::uint32_t>(pass.blend)), 0.0f };
            D3D12_GPU_VIRTUAL_ADDRESS layer_gpu = 0;
            if (!allocate_cb(&layer, sizeof(layer), layer_gpu)) return false;
            command_list_->SetGraphicsRootConstantBufferView(0, object_gpu);
            command_list_->SetGraphicsRootConstantBufferView(1, scene_gpu);
            command_list_->SetGraphicsRootShaderResourceView(4,
                current_bones ? current_bones : identity_bone_gpu);
            command_list_->SetGraphicsRootConstantBufferView(16, layer_gpu);
            return true;
        };
        const auto draw_layer_passes = [this, &bind_layer, &draw_mesh](
            D3D12MeshBuffer& mesh, const D3D12StaticDrawItem& draw, bool skinned,
            const DirectX::XMFLOAT4X4& previous_world,
            D3D12_GPU_VIRTUAL_ADDRESS current_bones, float morph_weight,
            bool depth_enabled) noexcept -> bool
        {
            for (const D3D12ShaderLayerPass& pass : draw.layer_passes)
            {
                const UINT kind = static_cast<UINT>(pass.kind);
                const UINT blend = static_cast<UINT>(pass.blend);
                if (kind >= 2u || blend >= 3u) continue;
                const UINT pipeline_index = (depth_enabled ? 0u : 6u) + kind * 3u + blend;
                ID3D12PipelineState* pipeline = skinned
                    ? scene3d_skinned_layer_pipelines_[pipeline_index].Get()
                    : scene3d_static_layer_pipelines_[pipeline_index].Get();
                command_list_->SetPipelineState(pipeline);
                if (!bind_layer(draw, pass, previous_world, current_bones, morph_weight))
                    return false;
                draw_mesh(mesh, draw);
            }
            return true;
        };
        const auto material_slot_selected = [](std::uint32_t target_slot_mask,
            std::uint32_t material_slot) noexcept
        {
            return target_slot_mask == 0xFFFFFFFFu ||
                (material_slot < 32u &&
                    (target_slot_mask & (1u << material_slot)) != 0u);
        };
        const auto model_effect_for_draw = [this, &material_slot_selected](
            const D3D12StaticDrawItem& draw)
            -> const D3D12ModelEffectStackSubmission*
        {
            for (const D3D12ModelEffectStackSubmission& stack : scene_effect_submission_.model_effects)
            {
                if (stack.owner_id == draw.owner_id &&
                    material_slot_selected(stack.target_slot_mask, draw.material_slot))
                    return &stack;
            }
            return nullptr;
        };
        const auto model_effect_isolated_from_scene = [this, &material_slot_selected](
            const D3D12StaticDrawItem& draw) noexcept
        {
            for (const D3D12ModelEffectStackSubmission& stack : scene_effect_submission_.model_effects)
            {
                if (stack.owner_id == draw.owner_id &&
                    material_slot_selected(stack.target_slot_mask, draw.material_slot) &&
                    (stack.isolate_from_scene || stack.depth_mode != 0))
                    return true;
            }
            return false;
        };
        if (LightingTraceEnabled())
        {
            try
            {
            std::ostringstream trace;
            trace << std::fixed << std::setprecision(6);
            trace << "[LightingTrace] submission directional="
                << (submission.directional_light.enabled ? 1 : 0)
                << " points=" << submission.point_lights.size()
                << " spots=" << submission.spot_lights.size()
                << " static_draws=" << submission.draws.size()
                << " skinned_draws=" << submission.skinned_draws.size()
                << " model_effects=" << scene_effect_submission_.model_effects.size() << '\n';
            for (std::size_t index = 0; index < submission.point_lights.size(); ++index)
            {
                const D3D12PointLightSubmission& source = submission.point_lights[index];
                trace << "[LightingTrace] point[" << index << "] position=("
                    << source.position.x << ',' << source.position.y << ',' << source.position.z
                    << ") color=(" << source.color.x << ',' << source.color.y << ','
                    << source.color.z << ") intensity=" << source.intensity
                    << " range=" << source.range << " cast_shadows="
                    << (source.cast_shadows ? 1 : 0) << " shadow_slice="
                    << source.shadow_slice << '\n';
            }
            for (std::uint32_t index = 0; index < point_count; ++index)
            {
                const Scene3DPointLightGpu& gpu = light.point_lights[index];
                trace << "[LightingTrace] gpu_point[" << index << "] position_range=("
                    << gpu.position_range.x << ',' << gpu.position_range.y << ','
                    << gpu.position_range.z << ',' << gpu.position_range.w
                    << ") color_intensity=(" << gpu.color_intensity.x << ','
                    << gpu.color_intensity.y << ',' << gpu.color_intensity.z << ','
                    << gpu.color_intensity.w << ") shadow=(" << gpu.shadow.x << ','
                    << gpu.shadow.y << ',' << gpu.shadow.z << ',' << gpu.shadow.w << ")\n";
            }
            const auto append_draw = [&trace, &model_effect_for_draw](
                const D3D12StaticDrawItem& draw, const char* kind, std::size_t index)
            {
                trace << "[LightingTrace] draw kind=" << kind << " index=" << index
                    << " owner=" << draw.owner_id << " mesh=\"" << draw.mesh_key
                    << "\" lighting_model=" << draw.lighting_model
                    << " receive_shadow=" << (draw.receive_shadow ? 1 : 0)
                    << " normal_map="
                    << ((draw.material_texture_semantic_mask & (1u << 1)) != 0u ? 1 : 0)
                    << " alpha_mode=" << static_cast<std::uint32_t>(draw.alpha_mode)
                    << " base=(" << draw.base_color.x << ',' << draw.base_color.y << ','
                    << draw.base_color.z << ',' << draw.base_color.w << ") metallic="
                    << draw.metallic << " roughness=" << draw.roughness << " ao="
                    << draw.ambient_occlusion << " model_effect="
                    << (model_effect_for_draw(draw) != nullptr ? 1 : 0) << '\n';
            };
            for (std::size_t index = 0; index < submission.draws.size(); ++index)
                append_draw(submission.draws[index], "static", index);
            for (std::size_t index = 0; index < submission.skinned_draws.size(); ++index)
                append_draw(submission.skinned_draws[index].surface, "skinned", index);
            for (std::size_t index = 0; index < scene_effect_submission_.model_effects.size();
                ++index)
            {
                const D3D12ModelEffectStackSubmission& stack =
                    scene_effect_submission_.model_effects[index];
                std::size_t matched_static = 0;
                std::size_t matched_skinned = 0;
                for (const D3D12StaticDrawItem& draw : submission.draws)
                    if (draw.owner_id == stack.owner_id &&
                        material_slot_selected(stack.target_slot_mask, draw.material_slot))
                        ++matched_static;
                for (const D3D12SkinnedDrawItem& draw : submission.skinned_draws)
                    if (draw.surface.owner_id == stack.owner_id &&
                        material_slot_selected(stack.target_slot_mask,
                            draw.surface.material_slot))
                        ++matched_skinned;
                trace << "[LightingTrace] model_effect[" << index << "] owner="
                    << stack.owner_id << " effects=" << stack.effects.size()
                    << " matched_static=" << matched_static
                    << " matched_skinned=" << matched_skinned
                    << " isolated_draw_scheduled=" << (stack.effects.empty() ? 0 : 1)
                    << " bind_surface_match_deferred_scheduled="
                    << (stack.effects.empty() ? 0 : 1)
                    << '\n';
            }
            const std::string signature = trace.str();
            if (signature != scene3d_lighting_trace_signature_)
            {
                scene3d_lighting_trace_signature_ = signature;
                SceneDebugMessage(signature.c_str());
            }
            }
            catch (...)
            {
                SceneDebugMessage("[LightingTrace] unavailable\n");
            }
        }
        const auto is_shadow_coverage_kind = [](std::uint32_t kind) noexcept
        {
            return kind == 5u || kind == 6u || kind == 7u || kind == 71u;
        };
        const auto shadow_coverage_stack_for_draw = [this, &material_slot_selected,
            &is_shadow_coverage_kind](const D3D12StaticDrawItem& draw) noexcept
            -> const D3D12ModelEffectStackSubmission*
        {
            for (const D3D12ModelEffectStackSubmission& stack : scene_effect_submission_.model_effects)
            {
                if (stack.owner_id != draw.owner_id ||
                    !material_slot_selected(stack.target_slot_mask, draw.material_slot))
                    continue;
                for (const D3D12UIEffectCommand& effect : stack.effects)
                    if (is_shadow_coverage_kind(effect.kind)) return &stack;
            }
            return nullptr;
        };
        const auto has_shadow_coverage = [&shadow_coverage_stack_for_draw](
            const D3D12StaticDrawItem& draw) noexcept
        {
            return shadow_coverage_stack_for_draw(draw) != nullptr;
        };
        const auto bind_shadow_surface = [this, &allocate_cb, identity_bone_gpu, &texture_for,
            &shadow_coverage_stack_for_draw, &is_shadow_coverage_kind, &white](
            const D3D12StaticDrawItem& draw, D3D12_GPU_VIRTUAL_ADDRESS current_bones,
            float morph_weight) noexcept -> bool
        {
            Scene3DShadowObjectConstants object{};
            object.world = draw.world;
            object.morph = { morph_weight, 0, 0, 0 };
            const bool alpha_clip = draw.alpha_mode != D3D12StaticAlphaMode::Opaque;
            const float cutoff = draw.alpha_mode == D3D12StaticAlphaMode::Blend
                ? 0.01f : draw.alpha_cutoff;
            object.alpha = { alpha_clip ? 1.0f : 0.0f, cutoff, draw.base_color.w, 0.0f };
            D3D12_GPU_VIRTUAL_ADDRESS object_gpu = 0;
            if (!allocate_cb(&object, sizeof(object), object_gpu)) return false;
            command_list_->SetGraphicsRootConstantBufferView(0, object_gpu);
            command_list_->SetGraphicsRootShaderResourceView(2,
                current_bones ? current_bones : identity_bone_gpu);
            command_list_->SetGraphicsRootDescriptorTable(3, texture_for(draw)->srv.gpu);

            Scene3DShadowCoverageConstants coverage{};
            coverage.view_projection = current_frame_constants_.view_projection;
            coverage.viewport = { 0.0f, 0.0f, static_cast<float>(width_),
                static_cast<float>(height_) };
            coverage.rect = coverage.viewport;
            const StaticTextureResource* coverage_mask = &white->second;
            int effect_count = 0;
            int mask_index = -1;
            int region_count = 0;
            if (const D3D12ModelEffectStackSubmission* stack =
                shadow_coverage_stack_for_draw(draw))
            {
                if (stack->scissor_enabled)
                {
                    coverage.rect = { static_cast<float>(stack->scissor.left),
                        static_cast<float>(stack->scissor.top),
                        static_cast<float>((std::max)(stack->scissor.right - stack->scissor.left, 1L)),
                        static_cast<float>((std::max)(stack->scissor.bottom - stack->scissor.top, 1L)) };
                }
                for (const D3D12UIEffectCommand& effect : stack->effects)
                {
                    if (!is_shadow_coverage_kind(effect.kind) || effect_count >= 4) continue;
                    const int index = effect_count;
                    coverage.params0[index] = { effect.radius, effect.intensity,
                        effect.threshold, effect.amount };
                    coverage.params1[index] = { effect.angle, effect.progress,
                        effect.softness, effect.speed };
                    coverage.params2[index] = { effect.direction.x, effect.direction.y,
                        effect.seed, effect.time };
                    bool mask_bound = false;
                    if (!effect.auxiliary_texture_key.empty() && mask_index < 0)
                    {
                        const auto found = texture_cache_.find(effect.auxiliary_texture_key);
                        if (found != texture_cache_.end())
                        {
                            coverage_mask = &found->second;
                            mask_index = index;
                            mask_bound = true;
                        }
                    }
                    coverage.params3[index] = { static_cast<float>(effect.waveform),
                        mask_bound ? 1.0f : 0.0f, 0.0f, 0.0f };
                    if (effect.kind == 5u)
                    {
                        const float center_x = effect.direction.x > 0.0f && effect.direction.x < 1.0f
                            ? effect.direction.x : 0.5f;
                        const float center_y = effect.direction.y > 0.0f && effect.direction.y < 1.0f
                            ? effect.direction.y : 0.5f;
                        const float half_width = effect.seed > 0.0f && effect.seed < 1.0f
                            ? effect.seed : 0.5f;
                        const float half_height = effect.speed > 0.0f && effect.speed < 1.0f
                            ? effect.speed : 0.5f;
                        coverage.params2[index] = { center_x, center_y, half_width, half_height };
                        coverage.params1[index].w = mask_bound ? 1.0f : 0.0f;
                    }
                    coverage.meta[index] = { static_cast<float>(effect.kind),
                        effect.region_enabled ? 1.0f : 0.0f, 0.0f, 0.0f };
                    if (effect.region_enabled && region_count == 0)
                    {
                        const int requested = (std::max)(1, (std::min)(4,
                            static_cast<int>(effect.effect_region_count.x + 0.5f)));
                        coverage.region_params[0] = effect.effect_region_params;
                        coverage.region_settings[0] = effect.effect_region_settings;
                        region_count = 1;
                        for (int extra = 1; extra < requested; ++extra)
                        {
                            coverage.region_params[extra] = effect.effect_region_extra_params[extra - 1];
                            coverage.region_settings[extra] = effect.effect_region_extra_settings[extra - 1];
                            region_count = extra + 1;
                        }
                    }
                    ++effect_count;
                }
            }
            coverage.control = { static_cast<float>(effect_count), static_cast<float>(mask_index),
                static_cast<float>(region_count), 0.0f };
            if (effect_count > 0) ++last_shadow_coverage_draw_count_;
            D3D12_GPU_VIRTUAL_ADDRESS coverage_gpu = 0;
            if (!allocate_cb(&coverage, sizeof(coverage), coverage_gpu)) return false;
            command_list_->SetGraphicsRootConstantBufferView(4, coverage_gpu);
            command_list_->SetGraphicsRootDescriptorTable(5, coverage_mask->srv.gpu);
            return true;
        };

        const auto render_shadow_casters = [this, &submission, &prepared_skinned,
            &bind_shadow_surface, &draw_mesh, &has_shadow_coverage, identity_bone_gpu](
            D3D12_GPU_VIRTUAL_ADDRESS pass_gpu) noexcept -> bool
        {
            command_list_->SetGraphicsRootSignature(scene3d_shadow_root_signature_.Get());
            command_list_->SetGraphicsRootConstantBufferView(1, pass_gpu);
            for (const D3D12StaticDrawItem& draw : submission.draws)
            {
                if (!draw.cast_shadow) continue;
                const auto it = static_mesh_cache_.find(draw.mesh_key);
                if (it == static_mesh_cache_.end() || !it->second || !it->second->IsValid()) continue;
                const UINT alpha_clip = (draw.alpha_mode != D3D12StaticAlphaMode::Opaque ||
                    has_shadow_coverage(draw)) ? 1u : 0u;
                const UINT index = (draw.double_sided ? 2u : 0u) + alpha_clip;
                command_list_->SetPipelineState(scene3d_static_shadow_pipelines_[index].Get());
                if (!bind_shadow_surface(draw, identity_bone_gpu, 0.0f)) return false;
                draw_mesh(*it->second, draw);
            }
            for (std::size_t i = 0; i < submission.skinned_draws.size(); ++i)
            {
                const D3D12SkinnedDrawItem& draw = submission.skinned_draws[i];
                if (!draw.surface.cast_shadow) continue;
                const auto it = skinned_mesh_cache_.find(draw.surface.mesh_key);
                if (it == skinned_mesh_cache_.end() || !it->second || !it->second->IsValid()) continue;
                const UINT alpha_clip = (draw.surface.alpha_mode != D3D12StaticAlphaMode::Opaque ||
                    has_shadow_coverage(draw.surface)) ? 1u : 0u;
                const UINT index = (draw.surface.double_sided ? 2u : 0u) + alpha_clip;
                command_list_->SetPipelineState(scene3d_skinned_shadow_pipelines_[index].Get());
                if (!bind_shadow_surface(draw.surface, prepared_skinned[i].current_bones,
                    draw.morph_weight))
                    return false;
                draw_mesh(*it->second, draw.surface);
            }
            return true;
        };

        // Scene 3DはDirectional/Point/SpotのShadow Mapから描画を開始する。
        if (directional_shadow_available)
        {
            BeginGpuPass(D3D12GpuPass::ShadowDirectional);
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                scene3d_directional_shadow_.resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE))
                return false;
            const float resolution = static_cast<float>(scene3d_directional_shadow_.resolution);
            const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, resolution, resolution, 0.0f, 1.0f };
            const D3D12_RECT scissor{ 0, 0,
                static_cast<LONG>(scene3d_directional_shadow_.resolution),
                static_cast<LONG>(scene3d_directional_shadow_.resolution) };
            command_list_->RSSetViewports(1, &viewport);
            command_list_->RSSetScissorRects(1, &scissor);
            for (std::uint32_t cascade = 0;
                cascade < D3D12DirectionalShadowSubmission::CascadeCount; ++cascade)
            {
                const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
                    dsv_allocator_.CpuHandle(scene3d_directional_shadow_.dsv.index + cascade);
                command_list_->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
                command_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH,
                    1.0f, 0, 0, nullptr);
                Scene3DShadowPassConstants pass{};
                pass.view_projection = submission.directional_shadow.view_projection[cascade];
                D3D12_GPU_VIRTUAL_ADDRESS pass_gpu = 0;
                if (!allocate_cb(&pass, sizeof(pass), pass_gpu) ||
                    !render_shadow_casters(pass_gpu))
                    return false;
            }
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                scene3d_directional_shadow_.resource.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
            EndGpuPass(D3D12GpuPass::ShadowDirectional);
        }

        if (local_shadow_available)
        {
            BeginGpuPass(D3D12GpuPass::ShadowLocal);
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                scene3d_local_shadow_.resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE))
                return false;
            const float resolution = static_cast<float>(scene3d_local_shadow_.resolution);
            const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, resolution, resolution, 0.0f, 1.0f };
            const D3D12_RECT scissor{ 0, 0,
                static_cast<LONG>(scene3d_local_shadow_.resolution),
                static_cast<LONG>(scene3d_local_shadow_.resolution) };
            command_list_->RSSetViewports(1, &viewport);
            command_list_->RSSetScissorRects(1, &scissor);
            for (std::uint32_t slice = 0; slice < D3D12LocalShadowSubmission::SliceCount; ++slice)
            {
                if ((submission.local_shadows.used_slice_mask & (1u << slice)) == 0) continue;
                const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
                    dsv_allocator_.CpuHandle(scene3d_local_shadow_.dsv.index + slice);
                command_list_->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
                command_list_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH,
                    1.0f, 0, 0, nullptr);
                Scene3DShadowPassConstants pass{};
                pass.view_projection = submission.local_shadows.slices[slice].view_projection;
                D3D12_GPU_VIRTUAL_ADDRESS pass_gpu = 0;
                if (!allocate_cb(&pass, sizeof(pass), pass_gpu) ||
                    !render_shadow_casters(pass_gpu))
                    return false;
            }
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                scene3d_local_shadow_.resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
            EndGpuPass(D3D12GpuPass::ShadowLocal);
        }

        const D3D12_VIEWPORT viewport{ 0.0f, 0.0f,
            static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
        const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, &scissor);

        BeginGpuPass(D3D12GpuPass::GBuffer);
        // Depth Prepass。Opaque/MaskはGBufferと同じGeometry/Alpha Cutoffを使う。
        if (!resource_state_tracker_.Transition(command_list_.Get(), scene3d_depth_.resource.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE))
            return false;
        command_list_->OMSetRenderTargets(0, nullptr, FALSE, &scene3d_depth_.dsv.cpu);
        command_list_->ClearDepthStencilView(scene3d_depth_.dsv.cpu, D3D12_CLEAR_FLAG_DEPTH,
            1.0f, 0, 0, nullptr);
        command_list_->SetGraphicsRootSignature(scene3d_geometry_root_signature_.Get());
        for (const D3D12StaticDrawItem& draw : submission.draws)
        {
            if (model_effect_isolated_from_scene(draw)) continue;
            if (draw.alpha_mode == D3D12StaticAlphaMode::Blend) continue;
            const auto it = static_mesh_cache_.find(draw.mesh_key);
            if (it == static_mesh_cache_.end() || !it->second || !it->second->IsValid()) continue;
            DirectX::XMFLOAT4X4 previous_world = draw.world;
            if (options.read_motion_history && !draw.motion_key.empty())
            {
                const auto history = scene3d_motion_history_.find(draw.motion_key);
                if (history != scene3d_motion_history_.end() && history->second.valid)
                    previous_world = history->second.world;
            }
            const UINT alpha_clip = draw.alpha_mode == D3D12StaticAlphaMode::Mask ? 1u : 0u;
            const UINT index = (draw.double_sided ? 2u : 0u) + alpha_clip;
            command_list_->SetPipelineState(scene3d_static_depth_pipelines_[index].Get());
            if (!bind_surface(draw, previous_world, identity_bone_gpu, identity_bone_gpu, 0.0f,
                false))
                return false;
            draw_mesh(*it->second, draw);
        }
        for (std::size_t i = 0; i < submission.skinned_draws.size(); ++i)
        {
            const D3D12SkinnedDrawItem& draw = submission.skinned_draws[i];
            if (model_effect_isolated_from_scene(draw.surface)) continue;
            if (draw.surface.alpha_mode == D3D12StaticAlphaMode::Blend) continue;
            const auto it = skinned_mesh_cache_.find(draw.surface.mesh_key);
            if (it == skinned_mesh_cache_.end() || !it->second || !it->second->IsValid()) continue;
            const UINT alpha_clip = draw.surface.alpha_mode == D3D12StaticAlphaMode::Mask ? 1u : 0u;
            const UINT index = (draw.surface.double_sided ? 2u : 0u) + alpha_clip;
            command_list_->SetPipelineState(scene3d_skinned_depth_pipelines_[index].Get());
            if (!bind_surface(draw.surface, prepared_skinned[i].previous_world,
                prepared_skinned[i].current_bones, prepared_skinned[i].previous_bones,
                draw.morph_weight, false))
                return false;
            draw_mesh(*it->second, draw.surface);
        }

        // 既存のGBuffer契約へDeferred Geometryを書き込む。
        for (Scene3DTarget& target : scene3d_gbuffer_)
        {
            if (!resource_state_tracker_.Transition(command_list_.Get(), target.resource.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET))
                return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[kScene3DGBufferCount]{};
        for (std::uint32_t i = 0; i < kScene3DGBufferCount; ++i)
            rtvs[i] = scene3d_gbuffer_[i].rtv.cpu;
        command_list_->OMSetRenderTargets(kScene3DGBufferCount, rtvs, FALSE,
            &scene3d_depth_.dsv.cpu);
        const float clear[4] = { 0,0,0,0 };
        for (const Scene3DTarget& target : scene3d_gbuffer_)
            command_list_->ClearRenderTargetView(target.rtv.cpu, clear, 0, nullptr);
        command_list_->SetGraphicsRootSignature(scene3d_geometry_root_signature_.Get());

        for (const D3D12StaticDrawItem& draw : submission.draws)
        {
            if (model_effect_isolated_from_scene(draw)) continue;
            if (draw.alpha_mode == D3D12StaticAlphaMode::Blend) continue;
            const auto it = static_mesh_cache_.find(draw.mesh_key);
            if (it == static_mesh_cache_.end() || !it->second || !it->second->IsValid()) continue;
            DirectX::XMFLOAT4X4 previous_world = draw.world;
            if (options.read_motion_history && !draw.motion_key.empty())
            {
                const auto history = scene3d_motion_history_.find(draw.motion_key);
                if (history != scene3d_motion_history_.end() && history->second.valid)
                    previous_world = history->second.world;
            }
            const UINT index = (draw.double_sided ? 3u : 0u) +
                static_cast<UINT>(draw.alpha_mode);
            command_list_->SetPipelineState(scene3d_static_gbuffer_pipelines_[index].Get());
            if (!bind_surface(draw, previous_world, identity_bone_gpu, identity_bone_gpu, 0.0f,
                false))
                return false;
            draw_mesh(*it->second, draw);
        }
        for (std::size_t i = 0; i < submission.skinned_draws.size(); ++i)
        {
            const D3D12SkinnedDrawItem& draw = submission.skinned_draws[i];
            if (model_effect_isolated_from_scene(draw.surface)) continue;
            if (draw.surface.alpha_mode == D3D12StaticAlphaMode::Blend) continue;
            const auto it = skinned_mesh_cache_.find(draw.surface.mesh_key);
            if (it == skinned_mesh_cache_.end() || !it->second || !it->second->IsValid()) continue;
            const UINT index = (draw.surface.double_sided ? 3u : 0u) +
                static_cast<UINT>(draw.surface.alpha_mode);
            command_list_->SetPipelineState(scene3d_skinned_gbuffer_pipelines_[index].Get());
            if (!bind_surface(draw.surface, prepared_skinned[i].previous_world,
                prepared_skinned[i].current_bones, prepared_skinned[i].previous_bones,
                draw.morph_weight, false))
                return false;
            draw_mesh(*it->second, draw.surface);
        }

        for (Scene3DTarget& target : scene3d_gbuffer_)
        {
            if (!resource_state_tracker_.Transition(command_list_.Get(), target.resource.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
        }
        if (!resource_state_tracker_.Transition(command_list_.Get(), scene3d_depth_.resource.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
            return false;

        EndGpuPass(D3D12GpuPass::GBuffer);
        BeginGpuPass(D3D12GpuPass::Lighting);
        // Deferred LightingはSwapChainではなくHDR Scene Targetへ出力する。
        // ここで初めて照明結果と最終表示変換を分離できる。
        if (!resource_state_tracker_.Transition(command_list_.Get(), scene_view_target_.color.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET))
            return false;
        const DirectX::XMFLOAT4& background = submission.background_color;
        const float background_clear[4] = {
            std::pow((std::max)(background.x, 0.0f), 2.2f),
            std::pow((std::max)(background.y, 0.0f), 2.2f),
            std::pow((std::max)(background.z, 0.0f), 2.2f),
            background.w };
        command_list_->ClearRenderTargetView(scene_view_target_.rtv.cpu,
            background_clear, 0, nullptr);
        D3D12_CPU_DESCRIPTOR_HANDLE lighting_rtv = scene_view_target_.rtv.cpu;
        command_list_->OMSetRenderTargets(1, &lighting_rtv, FALSE, nullptr);
        command_list_->SetGraphicsRootSignature(scene3d_lighting_root_signature_.Get());
        command_list_->SetPipelineState(scene3d_lighting_pipeline_.Get());
        command_list_->SetGraphicsRootConstantBufferView(0, light_gpu);
        for (std::uint32_t i = 0; i < kScene3DGBufferCount; ++i)
            command_list_->SetGraphicsRootDescriptorTable(
                kScene3DLightingGBufferRootSlot[i], scene3d_gbuffer_[i].srv.gpu);
        command_list_->SetGraphicsRootDescriptorTable(6, scene3d_depth_.srv.gpu);
        command_list_->SetGraphicsRootDescriptorTable(7, directional_shadow_srv);
        command_list_->SetGraphicsRootDescriptorTable(8, local_shadow_srv);
        command_list_->SetGraphicsRootDescriptorTable(10, ibl_diffuse_srv);
        command_list_->SetGraphicsRootDescriptorTable(11, ibl_specular_srv);
        command_list_->SetGraphicsRootDescriptorTable(12, sky_source_srv);
        command_list_->SetGraphicsRootDescriptorTable(13, ibl_diffuse_srv2);
        command_list_->SetGraphicsRootDescriptorTable(14, ibl_specular_srv2);
        command_list_->SetGraphicsRootDescriptorTable(15, sky_source_srv2);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->DrawInstanced(3, 1, 0, 0);

        const bool needs_deferred_debug_target =
            submission.post_process.render_output == 3u ||
            submission.post_process.render_output == 10u;
        if (needs_deferred_debug_target)
        {
            if (!resource_state_tracker_.Transition(command_list_.Get(), scene_view_target_.color.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                !resource_state_tracker_.Transition(command_list_.Get(), scene3d_deferred_target_.color.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST))
                return false;
            command_list_->CopyResource(scene3d_deferred_target_.color.Get(), scene_view_target_.color.Get());
            if (!resource_state_tracker_.Transition(command_list_.Get(), scene_view_target_.color.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET) ||
                !resource_state_tracker_.Transition(command_list_.Get(), scene3d_deferred_target_.color.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
        }

        const auto draw_sky_to_target = [this, light_gpu, sky_source_srv,
            ibl_diffuse_srv2, ibl_specular_srv2, sky_source_srv2,
            sky_available](D3D12OffscreenTarget& target) noexcept
        {
            if (!sky_available) return true;
            const bool target_is_scene = &target == &scene_view_target_;
            if (!target_is_scene && !resource_state_tracker_.Transition(command_list_.Get(),
                target.color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
                return false;
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                scene3d_gbuffer_[4].resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET) ||
                !resource_state_tracker_.Transition(command_list_.Get(), scene3d_depth_.resource.Get(),
                    D3D12_RESOURCE_STATE_DEPTH_READ))
                return false;
            if (!target_is_scene)
            {
                const float transparent[4]{ 0, 0, 0, 0 };
                command_list_->ClearRenderTargetView(target.rtv.cpu, transparent, 0, nullptr);
            }
            D3D12_CPU_DESCRIPTOR_HANDLE sky_targets[2] =
                { target.rtv.cpu, scene3d_gbuffer_[4].rtv.cpu };
            command_list_->OMSetRenderTargets(2, sky_targets, FALSE, &scene3d_depth_.dsv.cpu);
            command_list_->SetGraphicsRootSignature(scene3d_lighting_root_signature_.Get());
            command_list_->SetPipelineState(scene3d_skybox_pipeline_.Get());
            command_list_->SetGraphicsRootConstantBufferView(0, light_gpu);
            command_list_->SetGraphicsRootDescriptorTable(12, sky_source_srv);
            command_list_->SetGraphicsRootDescriptorTable(13, ibl_diffuse_srv2);
            command_list_->SetGraphicsRootDescriptorTable(14, ibl_specular_srv2);
            command_list_->SetGraphicsRootDescriptorTable(15, sky_source_srv2);
            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            command_list_->DrawInstanced(3, 1, 0, 0);
            if (!target_is_scene && !resource_state_tracker_.Transition(command_list_.Get(),
                target.color.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                scene3d_gbuffer_[4].resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
                !resource_state_tracker_.Transition(command_list_.Get(), scene3d_depth_.resource.Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
            return true;
        };
        if (sky_available && !draw_sky_to_target(scene_view_target_)) return false;

        EndGpuPass(D3D12GpuPass::Lighting);
        BeginGpuPass(D3D12GpuPass::Forward);
        // Transparent MaterialはForwardに残し、同じLight/Shadowデータを共有する。
        if (!resource_state_tracker_.Transition(command_list_.Get(), scene3d_depth_.resource.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE))
            return false;
        D3D12_CPU_DESCRIPTOR_HANDLE scene_target_rtv = scene_view_target_.rtv.cpu;
        command_list_->OMSetRenderTargets(1, &scene_target_rtv, FALSE, &scene3d_depth_.dsv.cpu);
        command_list_->SetGraphicsRootSignature(scene3d_geometry_root_signature_.Get());
        for (const D3D12StaticDrawItem& draw : submission.draws)
        {
            if (model_effect_isolated_from_scene(draw)) continue;
            if (draw.alpha_mode != D3D12StaticAlphaMode::Blend) continue;
            const auto it = static_mesh_cache_.find(draw.mesh_key);
            if (it == static_mesh_cache_.end() || !it->second || !it->second->IsValid()) continue;
            DirectX::XMFLOAT4X4 previous_world = draw.world;
            if (options.read_motion_history && !draw.motion_key.empty())
            {
                const auto history = scene3d_motion_history_.find(draw.motion_key);
                if (history != scene3d_motion_history_.end() && history->second.valid)
                    previous_world = history->second.world;
            }
            const UINT sided = draw.double_sided ? 1u : 0u;
            command_list_->SetPipelineState(scene3d_static_forward_blend_pipelines_[sided].Get());
            if (!bind_surface(draw, previous_world, identity_bone_gpu, identity_bone_gpu, 0.0f,
                false))
                return false;
            draw_mesh(*it->second, draw);
        }
        for (std::size_t i = 0; i < submission.skinned_draws.size(); ++i)
        {
            const D3D12SkinnedDrawItem& draw = submission.skinned_draws[i];
            if (model_effect_isolated_from_scene(draw.surface)) continue;
            if (draw.surface.alpha_mode != D3D12StaticAlphaMode::Blend) continue;
            const auto it = skinned_mesh_cache_.find(draw.surface.mesh_key);
            if (it == skinned_mesh_cache_.end() || !it->second || !it->second->IsValid()) continue;
            const UINT sided = draw.surface.double_sided ? 1u : 0u;
            command_list_->SetPipelineState(scene3d_skinned_forward_blend_pipelines_[sided].Get());
            if (!bind_surface(draw.surface, prepared_skinned[i].previous_world,
                prepared_skinned[i].current_bones, prepared_skinned[i].previous_bones,
                draw.morph_weight, false))
                return false;
            draw_mesh(*it->second, draw.surface);
        }
        for (const D3D12StaticDrawItem& draw : submission.draws)
        {
            if (model_effect_isolated_from_scene(draw) || draw.layer_passes.empty()) continue;
            const auto it = static_mesh_cache_.find(draw.mesh_key);
            if (it == static_mesh_cache_.end() || !it->second || !it->second->IsValid()) continue;
            DirectX::XMFLOAT4X4 previous_world = draw.world;
            if (options.read_motion_history && !draw.motion_key.empty())
            {
                const auto history = scene3d_motion_history_.find(draw.motion_key);
                if (history != scene3d_motion_history_.end() && history->second.valid)
                    previous_world = history->second.world;
            }
            if (!draw_layer_passes(*it->second, draw, false, previous_world,
                identity_bone_gpu, 0.0f, true))
                return false;
        }
        for (std::size_t i = 0; i < submission.skinned_draws.size(); ++i)
        {
            const D3D12SkinnedDrawItem& draw = submission.skinned_draws[i];
            if (model_effect_isolated_from_scene(draw.surface) ||
                draw.surface.layer_passes.empty())
                continue;
            const auto it = skinned_mesh_cache_.find(draw.surface.mesh_key);
            if (it == skinned_mesh_cache_.end() || !it->second || !it->second->IsValid()) continue;
            if (!draw_layer_passes(*it->second, draw.surface, true,
                prepared_skinned[i].previous_world, prepared_skinned[i].current_bones,
                draw.morph_weight, true))
                return false;
        }

        EndGpuPass(D3D12GpuPass::Forward);

        const auto draw_scene_effect_isolated = [this, &submission, &prepared_skinned,
            &bind_surface, &draw_layer_passes, &draw_mesh, &options, &material_slot_selected,
            identity_bone_gpu](
            D3D12OffscreenTarget& target, std::uint64_t owner_id,
            std::uint32_t rendering_layer_mask, std::uint32_t target_slot_mask,
            bool match_owner, bool preserve_depth) noexcept
        {
            if (!resource_state_tracker_.Transition(command_list_.Get(), target.color.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET))
                return false;
            const float transparent[4]{ 0, 0, 0, 0 };
            command_list_->ClearRenderTargetView(target.rtv.cpu, transparent, 0, nullptr);
            if (preserve_depth && !resource_state_tracker_.Transition(command_list_.Get(),
                scene3d_depth_.resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE))
                return false;
            const D3D12_VIEWPORT effect_viewport{ 0.0f, 0.0f,
                static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
            const D3D12_RECT effect_scissor{ 0, 0, static_cast<LONG>(width_),
                static_cast<LONG>(height_) };
            command_list_->RSSetViewports(1, &effect_viewport);
            command_list_->RSSetScissorRects(1, &effect_scissor);
            D3D12_CPU_DESCRIPTOR_HANDLE target_rtv = target.rtv.cpu;
            command_list_->OMSetRenderTargets(1, &target_rtv, FALSE,
                preserve_depth ? &scene3d_depth_.dsv.cpu : nullptr);
            command_list_->SetGraphicsRootSignature(scene3d_geometry_root_signature_.Get());
            for (const D3D12StaticDrawItem& draw : submission.draws)
            {
                const bool selected = match_owner ? draw.owner_id == owner_id &&
                    material_slot_selected(target_slot_mask, draw.material_slot)
                    : (draw.rendering_layer < 32u &&
                        (rendering_layer_mask & (1u << draw.rendering_layer)) != 0u);
                if (!selected) continue;
                const auto mesh = static_mesh_cache_.find(draw.mesh_key);
                if (mesh == static_mesh_cache_.end() || !mesh->second || !mesh->second->IsValid())
                    continue;
                DirectX::XMFLOAT4X4 previous_world = draw.world;
                if (options.read_motion_history && !draw.motion_key.empty())
                {
                    const auto history = scene3d_motion_history_.find(draw.motion_key);
                    if (history != scene3d_motion_history_.end() && history->second.valid)
                        previous_world = history->second.world;
                }
                const UINT depth_mode = preserve_depth ? 0u : 1u;
                const UINT pipeline_index = (draw.double_sided ? 2u : 0u) + depth_mode;
                command_list_->SetPipelineState(
                    scene3d_static_model_effect_pipelines_[pipeline_index].Get());
                if (!bind_surface(draw, previous_world, identity_bone_gpu, identity_bone_gpu,
                    0.0f, match_owner))
                    return false;
                draw_mesh(*mesh->second, draw);
            }
            for (std::size_t index = 0; index < submission.skinned_draws.size(); ++index)
            {
                const D3D12SkinnedDrawItem& draw = submission.skinned_draws[index];
                const bool selected = match_owner ? draw.surface.owner_id == owner_id &&
                    material_slot_selected(target_slot_mask, draw.surface.material_slot)
                    : (draw.surface.rendering_layer < 32u &&
                        (rendering_layer_mask & (1u << draw.surface.rendering_layer)) != 0u);
                if (!selected) continue;
                const auto mesh = skinned_mesh_cache_.find(draw.surface.mesh_key);
                if (mesh == skinned_mesh_cache_.end() || !mesh->second || !mesh->second->IsValid())
                    continue;
                const UINT depth_mode = preserve_depth ? 0u : 1u;
                const UINT pipeline_index = (draw.surface.double_sided ? 2u : 0u) + depth_mode;
                command_list_->SetPipelineState(
                    scene3d_skinned_model_effect_pipelines_[pipeline_index].Get());
                if (!bind_surface(draw.surface, prepared_skinned[index].previous_world,
                    prepared_skinned[index].current_bones, prepared_skinned[index].previous_bones,
                    draw.morph_weight, match_owner))
                    return false;
                draw_mesh(*mesh->second, draw.surface);
            }
            for (const D3D12StaticDrawItem& draw : submission.draws)
            {
                const bool selected = match_owner ? draw.owner_id == owner_id &&
                    material_slot_selected(target_slot_mask, draw.material_slot)
                    : (draw.rendering_layer < 32u &&
                        (rendering_layer_mask & (1u << draw.rendering_layer)) != 0u);
                if (!selected || draw.layer_passes.empty()) continue;
                const auto mesh = static_mesh_cache_.find(draw.mesh_key);
                if (mesh == static_mesh_cache_.end() || !mesh->second || !mesh->second->IsValid())
                    continue;
                DirectX::XMFLOAT4X4 previous_world = draw.world;
                if (options.read_motion_history && !draw.motion_key.empty())
                {
                    const auto history = scene3d_motion_history_.find(draw.motion_key);
                    if (history != scene3d_motion_history_.end() && history->second.valid)
                        previous_world = history->second.world;
                }
                if (!draw_layer_passes(*mesh->second, draw, false, previous_world,
                    identity_bone_gpu, 0.0f, preserve_depth))
                    return false;
            }
            for (std::size_t index = 0; index < submission.skinned_draws.size(); ++index)
            {
                const D3D12SkinnedDrawItem& draw = submission.skinned_draws[index];
                const bool selected = match_owner ? draw.surface.owner_id == owner_id &&
                    material_slot_selected(target_slot_mask, draw.surface.material_slot)
                    : (draw.surface.rendering_layer < 32u &&
                        (rendering_layer_mask & (1u << draw.surface.rendering_layer)) != 0u);
                if (!selected || draw.surface.layer_passes.empty()) continue;
                const auto mesh = skinned_mesh_cache_.find(draw.surface.mesh_key);
                if (mesh == skinned_mesh_cache_.end() || !mesh->second || !mesh->second->IsValid())
                    continue;
                if (!draw_layer_passes(*mesh->second, draw.surface, true,
                    prepared_skinned[index].previous_world,
                    prepared_skinned[index].current_bones, draw.morph_weight, preserve_depth))
                    return false;
            }
            const bool completed = resource_state_tracker_.Transition(command_list_.Get(),
                target.color.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            if (LightingTraceEnabled())
            {
                char message[256]{};
                std::snprintf(message, sizeof(message),
                    "[LightingTrace] isolated_draw owner=%llu match_owner=%d "
                    "preserve_depth=%d completed=%d bind_surface_match_deferred=%d\n",
                    static_cast<unsigned long long>(owner_id), match_owner ? 1 : 0,
                    preserve_depth ? 1 : 0, completed ? 1 : 0, match_owner ? 1 : 0);
                SceneDebugMessage(message);
            }
            return completed;
        };

        const auto draw_scene_effect_extracted = [this, &submission, &prepared_skinned,
            &bind_surface, &draw_layer_passes, &draw_mesh, &options, &material_slot_selected,
            identity_bone_gpu](
            D3D12OffscreenTarget& target, std::uint64_t owner_id,
            std::uint32_t rendering_layer_mask, std::uint32_t target_slot_mask,
            bool match_owner) noexcept
        {
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                scene_view_target_.color.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
                !resource_state_tracker_.Transition(command_list_.Get(), target.color.Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET) ||
                !resource_state_tracker_.Transition(command_list_.Get(),
                    scene3d_depth_.resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE))
                return false;
            const float transparent[4]{ 0, 0, 0, 0 };
            command_list_->ClearRenderTargetView(target.rtv.cpu, transparent, 0, nullptr);
            const D3D12_VIEWPORT effect_viewport{ 0.0f, 0.0f,
                static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
            const D3D12_RECT effect_scissor{ 0, 0, static_cast<LONG>(width_),
                static_cast<LONG>(height_) };
            command_list_->RSSetViewports(1, &effect_viewport);
            command_list_->RSSetScissorRects(1, &effect_scissor);
            command_list_->OMSetRenderTargets(1, &target.rtv.cpu, FALSE,
                &scene3d_depth_.dsv.cpu);
            command_list_->SetGraphicsRootSignature(scene3d_geometry_root_signature_.Get());
            const auto selected = [owner_id, rendering_layer_mask, target_slot_mask,
                match_owner, &material_slot_selected](
                const D3D12StaticDrawItem& draw) noexcept
            {
                return match_owner ? draw.owner_id == owner_id &&
                    material_slot_selected(target_slot_mask, draw.material_slot) :
                    (draw.rendering_layer < 32u &&
                        (rendering_layer_mask & (1u << draw.rendering_layer)) != 0u);
            };
            for (const D3D12StaticDrawItem& draw : submission.draws)
            {
                if (!selected(draw) || draw.alpha_mode == D3D12StaticAlphaMode::Blend) continue;
                const auto mesh = static_mesh_cache_.find(draw.mesh_key);
                if (mesh == static_mesh_cache_.end() || !mesh->second || !mesh->second->IsValid())
                    continue;
                DirectX::XMFLOAT4X4 previous_world = draw.world;
                if (options.read_motion_history && !draw.motion_key.empty())
                {
                    const auto history = scene3d_motion_history_.find(draw.motion_key);
                    if (history != scene3d_motion_history_.end() && history->second.valid)
                        previous_world = history->second.world;
                }
                const UINT sided = draw.double_sided ? 1u : 0u;
                command_list_->SetPipelineState(
                    scene3d_static_model_effect_extract_pipelines_[sided].Get());
                if (!bind_surface(draw, previous_world, identity_bone_gpu, identity_bone_gpu,
                    0.0f, false))
                    return false;
                command_list_->SetGraphicsRootDescriptorTable(15, scene_view_target_.srv.gpu);
                draw_mesh(*mesh->second, draw);
            }
            for (std::size_t index = 0; index < submission.skinned_draws.size(); ++index)
            {
                const D3D12SkinnedDrawItem& draw = submission.skinned_draws[index];
                if (!selected(draw.surface) ||
                    draw.surface.alpha_mode == D3D12StaticAlphaMode::Blend)
                    continue;
                const auto mesh = skinned_mesh_cache_.find(draw.surface.mesh_key);
                if (mesh == skinned_mesh_cache_.end() || !mesh->second || !mesh->second->IsValid())
                    continue;
                const UINT sided = draw.surface.double_sided ? 1u : 0u;
                command_list_->SetPipelineState(
                    scene3d_skinned_model_effect_extract_pipelines_[sided].Get());
                if (!bind_surface(draw.surface, prepared_skinned[index].previous_world,
                    prepared_skinned[index].current_bones, prepared_skinned[index].previous_bones,
                    draw.morph_weight, false))
                    return false;
                command_list_->SetGraphicsRootDescriptorTable(15, scene_view_target_.srv.gpu);
                draw_mesh(*mesh->second, draw.surface);
            }
            for (const D3D12StaticDrawItem& draw : submission.draws)
            {
                if (!selected(draw) || draw.alpha_mode != D3D12StaticAlphaMode::Blend) continue;
                const auto mesh = static_mesh_cache_.find(draw.mesh_key);
                if (mesh == static_mesh_cache_.end() || !mesh->second || !mesh->second->IsValid())
                    continue;
                DirectX::XMFLOAT4X4 previous_world = draw.world;
                if (options.read_motion_history && !draw.motion_key.empty())
                {
                    const auto history = scene3d_motion_history_.find(draw.motion_key);
                    if (history != scene3d_motion_history_.end() && history->second.valid)
                        previous_world = history->second.world;
                }
                const UINT sided = draw.double_sided ? 1u : 0u;
                command_list_->SetPipelineState(
                    scene3d_static_forward_blend_pipelines_[sided].Get());
                if (!bind_surface(draw, previous_world, identity_bone_gpu, identity_bone_gpu,
                    0.0f, false))
                    return false;
                draw_mesh(*mesh->second, draw);
            }
            for (std::size_t index = 0; index < submission.skinned_draws.size(); ++index)
            {
                const D3D12SkinnedDrawItem& draw = submission.skinned_draws[index];
                if (!selected(draw.surface) ||
                    draw.surface.alpha_mode != D3D12StaticAlphaMode::Blend)
                    continue;
                const auto mesh = skinned_mesh_cache_.find(draw.surface.mesh_key);
                if (mesh == skinned_mesh_cache_.end() || !mesh->second || !mesh->second->IsValid())
                    continue;
                const UINT sided = draw.surface.double_sided ? 1u : 0u;
                command_list_->SetPipelineState(
                    scene3d_skinned_forward_blend_pipelines_[sided].Get());
                if (!bind_surface(draw.surface, prepared_skinned[index].previous_world,
                    prepared_skinned[index].current_bones, prepared_skinned[index].previous_bones,
                    draw.morph_weight, false))
                    return false;
                draw_mesh(*mesh->second, draw.surface);
            }
            for (const D3D12StaticDrawItem& draw : submission.draws)
            {
                if (!selected(draw) || draw.layer_passes.empty()) continue;
                const auto mesh = static_mesh_cache_.find(draw.mesh_key);
                if (mesh == static_mesh_cache_.end() || !mesh->second || !mesh->second->IsValid())
                    continue;
                DirectX::XMFLOAT4X4 previous_world = draw.world;
                if (options.read_motion_history && !draw.motion_key.empty())
                {
                    const auto history = scene3d_motion_history_.find(draw.motion_key);
                    if (history != scene3d_motion_history_.end() && history->second.valid)
                        previous_world = history->second.world;
                }
                if (!draw_layer_passes(*mesh->second, draw, false, previous_world,
                    identity_bone_gpu, 0.0f, true))
                    return false;
            }
            for (std::size_t index = 0; index < submission.skinned_draws.size(); ++index)
            {
                const D3D12SkinnedDrawItem& draw = submission.skinned_draws[index];
                if (!selected(draw.surface) || draw.surface.layer_passes.empty()) continue;
                const auto mesh = skinned_mesh_cache_.find(draw.surface.mesh_key);
                if (mesh == skinned_mesh_cache_.end() || !mesh->second || !mesh->second->IsValid())
                    continue;
                if (!draw_layer_passes(*mesh->second, draw.surface, true,
                    prepared_skinned[index].previous_world,
                    prepared_skinned[index].current_bones, draw.morph_weight, true))
                    return false;
            }
            return resource_state_tracker_.Transition(command_list_.Get(), target.color.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        };

        for (const D3D12ModelEffectStackSubmission& stack : scene_effect_submission_.model_effects)
        {
            if (stack.effects.empty()) continue;
            ++last_model_effect_stack_count_;
            BeginGpuPass(D3D12GpuPass::ModelEffect);
            const bool isolate_from_scene = stack.isolate_from_scene || stack.depth_mode != 0;
            if (isolate_from_scene)
            {
                const bool preserve_depth = stack.depth_mode == 0;
                if (!draw_scene_effect_isolated(scene_effect_targets_[2], stack.owner_id, 0u,
                    stack.target_slot_mask, true, preserve_depth))
                    return false;
            }
            else if (!draw_scene_effect_extracted(scene_effect_targets_[2], stack.owner_id,
                0u, stack.target_slot_mask, true))
                return false;
            D3D12UIFrame effect_frame{};
            effect_frame.target_width = width_;
            effect_frame.target_height = height_;
            effect_frame.effects = stack.effects;
            effect_frame.requires_offscreen = true;
            effect_frame.capture_backdrop = true;
            effect_frame.preserve_output = true;
            if (!DrawRuntimeUIToTarget(effect_frame, &scene_effect_targets_[2],
                scene_effect_targets_))
                return false;
            const D3D12_RECT* effect_scissor = stack.scissor_enabled ? &stack.scissor : nullptr;
            if (!CompositeSceneEffectTarget(scene_effect_targets_[2], scene_view_target_,
                effect_scissor))
                return false;
            EndGpuPass(D3D12GpuPass::ModelEffect);
        }

        const auto apply_screen_effects = [this, &draw_scene_effect_extracted,
            &draw_sky_to_target](
            std::int32_t stage) noexcept
        {
            for (const D3D12ScreenEffectStackSubmission& stack : scene_effect_submission_.screen_effects)
            {
                if (stack.apply_stage != stage || stack.effects.empty()) continue;
                ++last_screen_effect_stack_count_;
                BeginGpuPass(D3D12GpuPass::ScreenEffect);
                if (stack.target_mode == 3)
                {
                    if (!scene_sky_effect_target_.IsValid() ||
                        scene_sky_effect_target_.width != width_ ||
                        scene_sky_effect_target_.height != height_)
                    {
                        if (!CreateOffscreenTarget(scene_sky_effect_target_, width_, height_,
                            DXGI_FORMAT_R16G16B16A16_FLOAT, L"SceneEffect.SkyRT"))
                            return false;
                    }
                    if (!draw_sky_to_target(scene_sky_effect_target_)) return false;
                    D3D12UIFrame sky_frame{};
                    sky_frame.target_width = width_;
                    sky_frame.target_height = height_;
                    sky_frame.effects = stack.effects;
                    sky_frame.requires_offscreen = true;
                    sky_frame.capture_backdrop = true;
                    sky_frame.preserve_output = true;
                    sky_frame.scene_effect_history = true;
                    if (!DrawRuntimeUIToTarget(sky_frame, &scene_sky_effect_target_,
                        scene_effect_targets_))
                        return false;
                    if (!CompositeSceneEffectTarget(scene_sky_effect_target_,
                        scene_view_target_, nullptr))
                        return false;
                }
                else if (stack.target_mode == 2)
                {
                    if (!draw_scene_effect_extracted(scene_effect_targets_[2], 0,
                        stack.target_rendering_layer_mask, 0xFFFFFFFFu, false))
                        return false;
                    D3D12UIFrame layer_frame{};
                    layer_frame.target_width = width_;
                    layer_frame.target_height = height_;
                    layer_frame.effects = stack.effects;
                    layer_frame.requires_offscreen = true;
                    layer_frame.capture_backdrop = true;
                    layer_frame.preserve_output = true;
                    if (!DrawRuntimeUIToTarget(layer_frame, &scene_effect_targets_[2],
                        scene_effect_targets_))
                        return false;
                    if (stage == 1)
                    {
                        if (!CompositeSceneEffectTarget(scene_effect_targets_[2],
                            scene_view_target_, nullptr))
                            return false;
                    }
                    else
                    {
                        if (!BuildRenderingLayerLdrSource(scene_effect_targets_[2],
                            ui_effect_targets_[2]))
                            return false;
                        D3D12UIFrame ldr_layer_frame{};
                        ldr_layer_frame.target_width = width_;
                        ldr_layer_frame.target_height = height_;
                        ldr_layer_frame.effects = stack.effects;
                        ldr_layer_frame.requires_offscreen = true;
                        ldr_layer_frame.capture_backdrop = true;
                        ldr_layer_frame.preserve_output = true;
                        if (!DrawRuntimeUIToTarget(ldr_layer_frame, &ui_effect_targets_[2],
                            ui_effect_targets_))
                            return false;
                        if (!CompositeUIEffectTargetToCurrent(ui_effect_targets_[2]))
                            return false;
                    }
                }
                else
                {
                    D3D12UIFrame effect_frame{};
                    effect_frame.target_width = width_;
                    effect_frame.target_height = height_;
                    effect_frame.effects = stack.effects;
                    effect_frame.requires_offscreen = true;
                    effect_frame.capture_backdrop = true;
                    effect_frame.preserve_output = true;
                    effect_frame.background_only = stack.target_mode == 1;
                    effect_frame.scene_effect_history = true;
                    if (stage == 1)
                    {
                        if (!DrawRuntimeUIToTarget(effect_frame, &scene_view_target_,
                            scene_effect_targets_))
                            return false;
                    }
                    else
                    {
                        if (!DrawRuntimeUIToTarget(effect_frame, nullptr, ui_effect_targets_))
                            return false;
                    }
                }
                EndGpuPass(D3D12GpuPass::ScreenEffect);
            }
            return true;
        };

        if (!apply_screen_effects(1)) return false;
        BeginGpuPass(D3D12GpuPass::PostProcess);
        if (!resource_state_tracker_.Transition(command_list_.Get(), scene_view_target_.color.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
            !resource_state_tracker_.Transition(command_list_.Get(), scene3d_depth_.resource.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
            return false;
        const bool scene_history_available = options.read_scene_history &&
            scene3d_history_valid_;
        if (scene_history_available &&
            (!resource_state_tracker_.Transition(command_list_.Get(), scene3d_history_.resource.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
             !resource_state_tracker_.Transition(command_list_.Get(), scene3d_ssr_history_.resource.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
             !resource_state_tracker_.Transition(command_list_.Get(), scene3d_depth_history_.resource.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)))
            return false;
        Scene3DPostProcessConstants post{};
        post.exposure = submission.post_process.exposure;
        post.bloom_intensity = submission.post_process.bloom_enabled
            ? submission.post_process.bloom_intensity : 0.0f;
        post.bloom_threshold = (std::max)(0.0f, submission.post_process.bloom_threshold);
        post.vignette_strength = submission.post_process.vignette_enabled
            ? submission.post_process.vignette_strength : 0.0f;
        post.fxaa_enable = submission.post_process.fxaa_enabled
            ? submission.post_process.fxaa_enable : 0.0f;
        post.taa_blend = (std::max)(0.0f, (std::min)(1.0f,
            submission.post_process.taa_blend));
        post.ssao_strength = (std::max)(0.0f, (std::min)(4.0f,
            submission.post_process.ssao_strength));
        post.ssr_strength = (std::max)(0.0f, (std::min)(4.0f,
            submission.post_process.ssr_strength));
        post.history_valid = scene_history_available ? 1.0f : 0.0f;
        post.post_flags = { submission.post_process.luminance_enabled ? 1.0f : 0.0f,
            submission.post_process.final_pass_enabled ? 1.0f : 0.0f };
        post.screen_size = { static_cast<float>(width_), static_cast<float>(height_) };
        post.color_filter = submission.post_process.color_filter;
        post.ssao_params0 = submission.post_process.ssao_params0;
        post.ssao_params1 = submission.post_process.ssao_params1;
        post.ssao_params2 = submission.post_process.ssao_params2;
        post.ssr_params0 = submission.post_process.ssr_params0;
        post.ssr_params1 = submission.post_process.ssr_params1;
        post.ssr_params2 = submission.post_process.ssr_params2;
        post.taa_params0 = submission.post_process.taa_params0;
        post.feature_flags = {
            submission.post_process.taa_enabled ? 1.0f : 0.0f,
            submission.post_process.ssao_enabled ? 1.0f : 0.0f,
            submission.post_process.ssr_enabled ? 1.0f : 0.0f,
            0.0f };
        post.debug_options = {
            static_cast<float>(submission.post_process.render_output),
            static_cast<float>(submission.post_process.deferred_debug_mode), 0.0f, 0.0f };
        post.view = current_frame_constants_.view;
        post.projection = current_frame_constants_.projection;
        post.inverse_projection = current_frame_constants_.inv_projection;
        post.camera_planes = current_frame_constants_.camera_planes;
        D3D12_GPU_VIRTUAL_ADDRESS post_gpu = 0;
        if (!allocate_cb(&post, sizeof(post), post_gpu)) return false;

        // SSAO は半解像度で 1 回だけ焼く。ポスト処理は結果を読むだけにする。
        if (post.feature_flags.y > 0.5f)
        {
            if (!resource_state_tracker_.Transition(command_list_.Get(),
                scene3d_ssao_.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
                return false;
            const std::uint32_t ssao_width = (std::max)(1u, width_ / 2u);
            const std::uint32_t ssao_height = (std::max)(1u, height_ / 2u);
            const D3D12_VIEWPORT ssao_viewport{ 0.0f, 0.0f,
                static_cast<float>(ssao_width), static_cast<float>(ssao_height), 0.0f, 1.0f };
            const D3D12_RECT ssao_scissor{ 0, 0,
                static_cast<LONG>(ssao_width), static_cast<LONG>(ssao_height) };
            command_list_->RSSetViewports(1, &ssao_viewport);
            command_list_->RSSetScissorRects(1, &ssao_scissor);
            command_list_->OMSetRenderTargets(1, &scene3d_ssao_.rtv.cpu, FALSE, nullptr);
            command_list_->SetGraphicsRootSignature(scene3d_postprocess_root_signature_.Get());
            command_list_->SetGraphicsRootConstantBufferView(0, post_gpu);
            command_list_->SetGraphicsRootDescriptorTable(1, scene_view_target_.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(2, scene_view_target_.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(3, scene3d_depth_.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(4, scene3d_gbuffer_[4].srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(5, scene3d_gbuffer_[2].srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(6, scene3d_gbuffer_[3].srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(7, scene3d_gbuffer_[0].srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(8, scene3d_deferred_target_.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(9, scene3d_depth_.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(10, scene_view_target_.srv.gpu);
            command_list_->SetPipelineState(scene3d_ssao_pipeline_.Get());
            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            command_list_->DrawInstanced(3, 1, 0, 0);
        }
        if (!resource_state_tracker_.Transition(command_list_.Get(),
            scene3d_ssao_.resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
            return false;
        // SSAO パスで半分にしたビューポートを必ず戻す。戻さないと後段が縮んで描かれる。
        {
            const D3D12_VIEWPORT full_viewport{ 0.0f, 0.0f,
                static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
            const D3D12_RECT full_scissor{ 0, 0,
                static_cast<LONG>(width_), static_cast<LONG>(height_) };
            command_list_->RSSetViewports(1, &full_viewport);
            command_list_->RSSetScissorRects(1, &full_scissor);
        }

        const auto bind_postprocess_inputs = [this, scene_history_available](
            D3D12_GPU_DESCRIPTOR_HANDLE scene_color) noexcept
        {
            command_list_->SetGraphicsRootDescriptorTable(1, scene_color);
            command_list_->SetGraphicsRootDescriptorTable(2,
                scene_history_available ? scene3d_history_.srv.gpu : scene_view_target_.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(3, scene3d_depth_.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(4, scene3d_gbuffer_[4].srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(5, scene3d_gbuffer_[2].srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(6, scene3d_gbuffer_[3].srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(7, scene3d_gbuffer_[0].srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(8, scene3d_deferred_target_.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(9, scene_history_available
                ? scene3d_depth_history_.srv.gpu : scene3d_depth_.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(10, scene_history_available
                ? scene3d_ssr_history_.srv.gpu : scene_view_target_.srv.gpu);
            command_list_->SetGraphicsRootDescriptorTable(11, scene3d_ssao_.srv.gpu);
        };

        if (!resource_state_tracker_.Transition(command_list_.Get(), scene3d_temporal_input_.resource.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET))
            return false;
        command_list_->OMSetRenderTargets(1, &scene3d_temporal_input_.rtv.cpu, FALSE, nullptr);
        command_list_->SetGraphicsRootSignature(scene3d_postprocess_root_signature_.Get());
        command_list_->SetPipelineState(scene3d_temporal_input_pipeline_.Get());
        command_list_->SetGraphicsRootConstantBufferView(0, post_gpu);
        bind_postprocess_inputs(scene_view_target_.srv.gpu);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->DrawInstanced(3, 1, 0, 0);
        if (!resource_state_tracker_.Transition(command_list_.Get(), scene3d_temporal_input_.resource.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
            !resource_state_tracker_.Transition(command_list_.Get(), scene3d_taa_resolved_.resource.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET))
            return false;

        // TAAのHDR境界はFXAA/SSAO/SSR後・Bloom/ToneMap前に固定し、履歴もこの境界のResolve結果だけを戻す。
        command_list_->OMSetRenderTargets(1, &scene3d_taa_resolved_.rtv.cpu, FALSE, nullptr);
        command_list_->SetPipelineState(scene3d_taa_resolve_pipeline_.Get());
        command_list_->SetGraphicsRootConstantBufferView(0, post_gpu);
        bind_postprocess_inputs(scene3d_temporal_input_.srv.gpu);
        command_list_->DrawInstanced(3, 1, 0, 0);
        if (!resource_state_tracker_.Transition(command_list_.Get(), scene3d_taa_resolved_.resource.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
            return false;

        if (!TransitionCurrentRenderTarget(D3D12_RESOURCE_STATE_RENDER_TARGET)) return false;
        D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_rtv = CurrentRenderTargetView();
        const bool use_present_viewport = options.present_viewport_enabled &&
            options.present_viewport.Width > 0.0f && options.present_viewport.Height > 0.0f &&
            options.present_scissor.right > options.present_scissor.left &&
            options.present_scissor.bottom > options.present_scissor.top;
        const D3D12_VIEWPORT& present_viewport = use_present_viewport
            ? options.present_viewport : viewport;
        const D3D12_RECT& present_scissor = use_present_viewport
            ? options.present_scissor : scissor;
        command_list_->OMSetRenderTargets(1, &back_buffer_rtv, FALSE, nullptr);
        command_list_->SetPipelineState(scene3d_postprocess_pipeline_.Get());
        command_list_->SetGraphicsRootConstantBufferView(0, post_gpu);
        bind_postprocess_inputs(submission.post_process.render_output == 0u
            ? scene3d_taa_resolved_.srv.gpu : scene_view_target_.srv.gpu);
        command_list_->RSSetViewports(1, &present_viewport);
        command_list_->RSSetScissorRects(1, &present_scissor);
        command_list_->DrawInstanced(3, 1, 0, 0);

        if (options.write_scene_history)
        {
            if (!resource_state_tracker_.Transition(command_list_.Get(), scene3d_taa_resolved_.resource.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                !resource_state_tracker_.Transition(command_list_.Get(), scene3d_history_.resource.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST) ||
                !resource_state_tracker_.Transition(command_list_.Get(), scene_view_target_.color.Get(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                !resource_state_tracker_.Transition(command_list_.Get(), scene3d_ssr_history_.resource.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST) ||
                !resource_state_tracker_.Transition(command_list_.Get(), scene3d_depth_.resource.Get(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE) ||
                !resource_state_tracker_.Transition(command_list_.Get(), scene3d_depth_history_.resource.Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST))
                return false;
            command_list_->CopyResource(scene3d_history_.resource.Get(),
                scene3d_taa_resolved_.resource.Get());
            command_list_->CopyResource(scene3d_ssr_history_.resource.Get(),
                scene_view_target_.color.Get());
            command_list_->CopyResource(scene3d_depth_history_.resource.Get(),
                scene3d_depth_.resource.Get());
            if (!resource_state_tracker_.Transition(command_list_.Get(), scene3d_depth_.resource.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
                return false;
            scene3d_history_valid_ = true;
            ++scene3d_history_write_serial_;
        }
        EndGpuPass(D3D12GpuPass::PostProcess);
        if (options.apply_final_screen_effects && !apply_screen_effects(0)) return false;
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, &scissor);

        if (options.write_motion_history)
        {
            ++scene3d_frame_serial_;
            for (const D3D12StaticDrawItem& draw : submission.draws)
            {
                if (draw.motion_key.empty()) continue;
                Scene3DMotionHistory& history = scene3d_motion_history_[draw.motion_key];
                history.world = draw.world;
                history.bones.clear();
                history.frame_serial = scene3d_frame_serial_;
                history.valid = true;
            }
            for (std::size_t i = 0; i < submission.skinned_draws.size(); ++i)
            {
                PreparedSkinnedDraw& prepared = prepared_skinned[i];
                if (prepared.history_key.empty() || prepared.current_palette == nullptr) continue;
                Scene3DMotionHistory& history = scene3d_motion_history_[prepared.history_key];
                history.world = submission.skinned_draws[i].surface.world;
                history.bones = *prepared.current_palette;
                history.frame_serial = scene3d_frame_serial_;
                history.valid = true;
            }
        }
        return true;
    }

}
