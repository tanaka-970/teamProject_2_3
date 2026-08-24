#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include "D3D12DescriptorHeapAllocator.h"
#include "D3D12FrameConstants.h"
#include "D3D12FrameResource.h"
#include "D3D12MeshBuffer.h"
#include "D3D12RenderItemBatch.h"
#include "D3D12ResourceStateTracker.h"
#include "D3D12ShaderCompiler.h"
#include "D3D12UploadContext.h"

#include <DirectXMath.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ReplayEngine::Rendering::DX12
{
    struct D3D12StaticVertex final
    {
        DirectX::XMFLOAT3 position{};
        DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT2 texcoord{};
    };

    static_assert(sizeof(D3D12StaticVertex) == 32,
        "DX12 static vertex ABI must stay position/normal/texcoord (32 bytes).");

    struct D3D12StaticMeshSource final
    {
        // Submission は DrawStaticScene が消費するまで Cache Miss の Geometry を所有する。
        // 旧 CPU Mesh を必要時に変換する際の Dangling Pointer を防ぐ。
        std::string key;
        std::vector<D3D12StaticVertex> vertices;
        std::vector<std::uint32_t> indices;
    };

    struct D3D12StaticTextureSource final
    {
        std::string key;
        std::filesystem::path source_path;
    };

    // Custom Surface Shader の正本は既存の ShaderCatalog/PropertySchema とする。
    // Framework は Source Path と生成済み b9/t40+ 宣言だけを渡し、DX12 Backend が
    // DXC Compile と PSO Cache を担当する。
    struct D3D12StaticShaderSource final
    {
        std::string key;
        std::filesystem::path source_path;
        std::string generated_declaration;
    };

    struct D3D12StaticMaterialTexture final
    {
        std::uint32_t slot = 0;
        std::string texture_key;
    };

    enum class D3D12StaticAlphaMode : std::uint32_t
    {
        Opaque = 0,
        Mask = 1,
        Blend = 2,
    };

    struct D3D12StaticDrawItem final
    {
        std::string mesh_key;
        std::string base_color_texture_key;
        // 空文字なら Phase 2 Material Bridge の Pixel Shader を使う。空でない Key は、
        // Static Phase 2 Root Signature に適合する場合に DXC Compile 済みの
        // ShaderCatalog Surface Shader を使い、Custom PSO の失敗時は安全に戻す。
        std::string shader_key;
        std::vector<std::uint8_t> material_constants;
        std::vector<D3D12StaticMaterialTexture> material_textures;
        std::uint32_t start_index = 0;
        std::uint32_t index_count = 0; // 0 は Cache 済み Index Buffer 全体を描画する。
        DirectX::XMFLOAT4X4 world{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1 };
        DirectX::XMFLOAT4 base_color{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 vertex_tint{ 1, 1, 1, 1 };
        DirectX::XMFLOAT3 emissive{ 0, 0, 0 };
        float emissive_strength = 0.0f;
        float metallic = 0.0f;
        float roughness = 0.55f;
        float ambient_occlusion = 1.0f;
        float alpha_cutoff = 0.5f;
        D3D12StaticAlphaMode alpha_mode = D3D12StaticAlphaMode::Opaque;
        bool double_sided = false;
    };

    struct D3D12StaticSceneSubmission final
    {
        std::vector<D3D12StaticMeshSource> mesh_sources;
        std::vector<D3D12StaticTextureSource> texture_sources;
        std::vector<D3D12StaticShaderSource> shader_sources;
        std::vector<D3D12StaticDrawItem> draws;
    };

    struct D3D12OffscreenTarget final
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> color;
        Microsoft::WRL::ComPtr<ID3D12Resource> depth;
        D3D12DescriptorAllocation rtv{};
        D3D12DescriptorAllocation dsv{};
        D3D12DescriptorAllocation srv{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool IsValid() const noexcept
        {
            return color != nullptr && depth != nullptr && rtv.IsValid() &&
                dsv.IsValid() && srv.IsValid() && width != 0 && height != 0;
        }
    };

    class D3D12DeviceContext final
    {
    public:
        static constexpr std::uint32_t FrameCount = 2;
        static constexpr std::uint64_t FrameUploadCapacity = 8ull * 1024ull * 1024ull;

        D3D12DeviceContext() = default;
        ~D3D12DeviceContext();

        D3D12DeviceContext(const D3D12DeviceContext&) = delete;
        D3D12DeviceContext& operator=(const D3D12DeviceContext&) = delete;

        bool Initialize(HWND window, std::uint32_t width, std::uint32_t height,
            bool enable_debug_layer = false, bool force_warp = false,
            bool create_validation_resources = true) noexcept;
        void Shutdown() noexcept;
        bool Resize(std::uint32_t width, std::uint32_t height) noexcept;

        bool BeginFrame(const float clear_color[4]) noexcept;
        bool SubmitFrameConstants(const D3D12FrameConstants& constants) noexcept;
        bool SubmitRenderItems(
            const ::ReplayEngine::Rendering::RenderItemList& items) noexcept;
        bool DrawStaticScene(const D3D12StaticSceneSubmission& submission) noexcept;
        bool DrawValidationTriangle() noexcept;
        bool EndFrame() noexcept;
        bool WaitForGpu() noexcept;
        // Scene/Asset Reload の境界。Cache 済み Static Mesh/Texture を解放する前に
        // GPU Idle を待ち、古い Asset を保持し続けないようにする。
        bool ClearStaticAssetCaches() noexcept;

        bool IsInitialized() const noexcept { return device_ != nullptr; }
        bool IsFrameOpen() const noexcept { return frame_open_; }
        std::uint32_t Width() const noexcept { return width_; }
        std::uint32_t Height() const noexcept { return height_; }
        std::uint32_t FrameIndex() const noexcept { return frame_index_; }
        std::uint64_t LastSignaledFenceValue() const noexcept
        {
            return last_signaled_fence_value_;
        }
        std::uint64_t FrameFenceValue(std::uint32_t index) const noexcept
        {
            return index < FrameCount ? frame_resources_[index].fence_value : 0;
        }
        std::uint64_t CurrentFrameUploadUsed() const noexcept
        {
            return frame_resources_[frame_index_].upload_allocator.Used();
        }
        std::uint64_t CurrentFrameUploadCapacity() const noexcept
        {
            return frame_resources_[frame_index_].upload_allocator.Capacity();
        }
        bool DebugLayerEnabled() const noexcept { return debug_layer_enabled_; }
        bool GpuValidationEnabled() const noexcept { return gpu_validation_enabled_; }
        bool DredEnabled() const noexcept { return dred_enabled_; }
        bool HasFatalError() const noexcept { return fatal_error_; }
        HRESULT LastDeviceRemovedReason() const noexcept
        {
            return last_device_removed_reason_;
        }

        ID3D12Device* Device() const noexcept { return device_.Get(); }
        ID3D12CommandQueue* CommandQueue() const noexcept { return command_queue_.Get(); }
        ID3D12GraphicsCommandList* CommandList() const noexcept { return command_list_.Get(); }
        D3D12UploadContext& UploadContext() noexcept { return upload_context_; }
        D3D12DescriptorHeapAllocator& ResourceDescriptorAllocator() noexcept
        {
            return resource_descriptor_allocator_;
        }
        D3D12DescriptorHeapAllocator& SamplerDescriptorAllocator() noexcept
        {
            return sampler_descriptor_allocator_;
        }
        D3D12ResourceStateTracker& ResourceStateTracker() noexcept
        {
            return resource_state_tracker_;
        }
        const D3D12ResourceStateTracker& ResourceStateTracker() const noexcept
        {
            return resource_state_tracker_;
        }
        const D3D12RenderItemBatch& RenderItemBatch() const noexcept
        {
            return render_item_batches_[frame_index_];
        }
        std::size_t StaticMeshCacheSize() const noexcept
        {
            return static_mesh_cache_.size();
        }
        std::size_t TextureCacheSize() const noexcept
        {
            return texture_cache_.size();
        }
        bool HasStaticMesh(const std::string& key) const noexcept
        {
            return static_mesh_cache_.find(key) != static_mesh_cache_.end();
        }
        bool HasStaticTexture(const std::string& key) const noexcept
        {
            // File Decode の失敗も解決済みとして扱う。DrawStaticScene は White Texture へ戻し、
            // build_dx12_static_scene が同じ不正 Asset を毎フレーム再試行しないようにする。
            return key.empty() || texture_cache_.find(key) != texture_cache_.end() ||
                static_texture_failures_.find(key) != static_texture_failures_.end();
        }
        bool HasStaticShader(const std::string& key) const noexcept
        {
            return key.empty() || custom_static_pipelines_.find(key) !=
                custom_static_pipelines_.end() ||
                custom_static_shader_failures_.find(key) != custom_static_shader_failures_.end();
        }
        bool HasCompiledStaticShader(const std::string& key) const noexcept
        {
            return !key.empty() && custom_static_pipelines_.find(key) !=
                custom_static_pipelines_.end();
        }
        ID3D12Resource* CurrentRenderTarget() const noexcept
        {
            return render_targets_[frame_index_].Get();
        }
        D3D12_CPU_DESCRIPTOR_HANDLE CurrentRenderTargetView() const noexcept;
        D3D12_CPU_DESCRIPTOR_HANDLE CurrentDepthStencilView() const noexcept;

        const D3D12OffscreenTarget& SceneViewTarget() const noexcept
        {
            return scene_view_target_;
        }
        const D3D12OffscreenTarget& GameViewTarget() const noexcept
        {
            return game_view_target_;
        }

    private:
        bool ConfigureDebug(bool enable_debug_layer) noexcept;
        bool CreateDevice(bool enable_debug_layer, bool force_warp) noexcept;
        bool CreateSwapChain(HWND window, std::uint32_t width,
            std::uint32_t height) noexcept;
        bool CreateRenderTargets() noexcept;
        bool CreateOffscreenTarget(D3D12OffscreenTarget& target,
            std::uint32_t width, std::uint32_t height) noexcept;
        bool CreateValidationTriangleResources() noexcept;
        bool CreateStaticRendererResources() noexcept;
        void ReleaseStaticRendererResources() noexcept;
        bool EnsureStaticMesh(const D3D12StaticMeshSource& source) noexcept;
        bool EnsureStaticTexture(const D3D12StaticTextureSource& source) noexcept;
        bool EnsureStaticShader(const D3D12StaticShaderSource& source) noexcept;
        bool CreateSolidStaticTexture(const char* key, std::uint32_t rgba) noexcept;
        struct StaticPipelineSet;
        bool CreateStaticPipelineSet(const std::vector<std::uint8_t>& pixel_shader,
            StaticPipelineSet& pipelines) noexcept;
        ID3D12PipelineState* StaticPipeline(const std::string& shader_key,
            bool double_sided, D3D12StaticAlphaMode alpha_mode) const noexcept;
        void ReleaseRenderTargets() noexcept;
        void ReleaseOffscreenTarget(D3D12OffscreenTarget& target) noexcept;
        void ReleaseValidationTriangleResources() noexcept;
        bool WaitForFrame(std::uint32_t frame_index) noexcept;
        bool TransitionCurrentRenderTarget(D3D12_RESOURCE_STATES after) noexcept;
        std::uint64_t SignalQueue() noexcept;
        void ReclaimDeferredDescriptors() noexcept;
        void ReportDeviceRemoved(HRESULT trigger) noexcept;

        struct StaticTextureResource final
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12DescriptorAllocation srv{};
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint16_t mip_levels = 1;
            DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        };

        struct StaticObjectConstants final
        {
            DirectX::XMFLOAT4X4 world{};
            DirectX::XMFLOAT4 material_color{ 1, 1, 1, 1 };
        };
        struct StaticSceneConstants final
        {
            DirectX::XMFLOAT4X4 view_projection{};
            DirectX::XMFLOAT4 light_direction{ 0, -1, 0, 0 };
            DirectX::XMFLOAT4 camera_position{};
        };
        struct StaticFrameCompatibilityConstants final
        {
            DirectX::XMFLOAT4X4 frame_view{};
            DirectX::XMFLOAT4X4 frame_projection{};
            DirectX::XMFLOAT4X4 frame_view_projection{};
            DirectX::XMFLOAT4X4 frame_inv_view{};
            DirectX::XMFLOAT4X4 frame_inv_projection{};
            DirectX::XMFLOAT4X4 frame_inv_view_projection{};
            DirectX::XMFLOAT4X4 frame_prev_view_projection{};
            DirectX::XMFLOAT4 frame_camera_position{};
            DirectX::XMFLOAT4 frame_screen_size{};
            DirectX::XMFLOAT4 frame_camera_planes{};
            DirectX::XMFLOAT4 frame_jitter{};
            DirectX::XMFLOAT4 frame_params{};
        };
        struct StaticBridgeMaterialConstants final
        {
            DirectX::XMFLOAT4 base_color{};
            DirectX::XMFLOAT4 emissive_strength{};
            DirectX::XMFLOAT4 surface_params{};
            DirectX::XMFLOAT4 render_params{};
        };
        struct StaticPipelineSet final
        {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelines[6];
        };
        static_assert(sizeof(StaticObjectConstants) % 16 == 0);
        static_assert(sizeof(StaticSceneConstants) % 16 == 0);
        static_assert(sizeof(StaticFrameCompatibilityConstants) % 16 == 0);
        static_assert(sizeof(StaticBridgeMaterialConstants) % 16 == 0);

        Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
        Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> validation_root_signature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> validation_pipeline_;
        D3D12MeshBuffer validation_mesh_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> static_root_signature_;
        std::vector<std::uint8_t> static_vertex_shader_bytecode_;
        StaticPipelineSet static_bridge_pipelines_{};
        D3D12DescriptorAllocation static_samplers_[3]{};
        std::unordered_map<std::string, std::unique_ptr<D3D12MeshBuffer>> static_mesh_cache_;
        std::unordered_map<std::string, StaticTextureResource> texture_cache_;
        std::unordered_set<std::string> static_texture_failures_;
        std::unordered_map<std::string, StaticPipelineSet> custom_static_pipelines_;
        std::unordered_set<std::string> custom_static_shader_failures_;
        Microsoft::WRL::ComPtr<ID3D12Resource> depth_stencil_buffer_;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
        D3D12FrameResource frame_resources_[FrameCount];
        Microsoft::WRL::ComPtr<ID3D12Resource> render_targets_[FrameCount];

        D3D12DescriptorHeapAllocator rtv_allocator_;
        D3D12DescriptorAllocation rtv_allocation_{};
        D3D12DescriptorHeapAllocator dsv_allocator_;
        D3D12DescriptorAllocation dsv_allocation_{};
        D3D12DescriptorHeapAllocator resource_descriptor_allocator_;
        D3D12DescriptorHeapAllocator sampler_descriptor_allocator_;
        D3D12UploadContext upload_context_;
        D3D12ResourceStateTracker resource_state_tracker_;
        D3D12RenderItemBatch render_item_batches_[FrameCount];
        D3D12FrameConstants current_frame_constants_{};
        D3D12OffscreenTarget scene_view_target_{};
        D3D12OffscreenTarget game_view_target_{};
        HANDLE fence_event_ = nullptr;
        std::uint64_t next_fence_value_ = 1;
        std::uint64_t last_signaled_fence_value_ = 0;
        std::uint32_t frame_index_ = 0;
        std::uint32_t width_ = 0;
        std::uint32_t height_ = 0;
        bool frame_open_ = false;
        bool debug_layer_enabled_ = false;
        bool gpu_validation_enabled_ = false;
        bool dred_enabled_ = false;
        bool allow_tearing_ = false;
        bool validation_resources_enabled_ = false;
        bool fatal_error_ = false;
        HRESULT last_device_removed_reason_ = S_OK;
    };
}
