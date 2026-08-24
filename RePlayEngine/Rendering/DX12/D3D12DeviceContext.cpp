#include "D3D12DeviceContext.h"
#include "D3D12ResourceFactory.h"

#include <d3d12sdklayers.h>
#include <wincodec.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

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

        constexpr char kStaticVertexShader[] = R"(
cbuffer OBJECT_CONSTANT_BUFFER : register(b0)
{
    row_major float4x4 world;
    float4 material_color;
};
cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 light_direction;
    float4 camera_position;
};
struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};
struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 world_position : POSITION;
    float4 world_normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
    float4 current_clip : TEXCOORD1;
    float4 previous_clip : TEXCOORD2;
};
VS_OUT main(VertexInput input)
{
    VS_OUT output;
    const float4 local_position = float4(input.position, 1.0f);
    output.world_position = mul(local_position, world);
    output.position = mul(output.world_position, view_projection);
    output.world_normal = float4(normalize(mul(float4(input.normal, 0.0f), world).xyz), 0.0f);
    output.color = material_color;
    output.texcoord = input.texcoord;
    output.current_clip = output.position;
    output.previous_clip = output.position;
    return output;
})";

        constexpr char kStaticPixelShader[] = R"(
cbuffer REPLAY_MATERIAL_CB : register(b9)
{
    float4 base_color;
    float4 emissive_strength;
    float4 surface_params;
    float4 render_params;
};
Texture2D BaseMap : register(t0);
SamplerState BaseSampler : register(s1);
struct PixelInput
{
    float4 position : SV_POSITION;
    float4 world_position : POSITION;
    float4 world_normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
    float4 current_clip : TEXCOORD1;
    float4 previous_clip : TEXCOORD2;
};
float4 main(PixelInput input) : SV_TARGET
{
    float4 texel = BaseMap.Sample(BaseSampler, input.texcoord) * base_color * input.color;
    const uint alpha_mode = (uint)(render_params.x + 0.5f);
    if (alpha_mode == 1u && texel.a < surface_params.w) discard;

    // Phase 2 では Deferred Lighting を Phase 3 へ残す。Static Bridge は
    // Texture/Material の編集を維持しつつ、Custom Unlit Surface Shader を
    // 同じ Root Signature と PSO Cache で実行できるようにする。
    float3 color = texel.rgb + emissive_strength.rgb * emissive_strength.w;
    return float4(color, texel.a);
})";

        struct DecodedRgbaImage final
        {
            std::vector<std::uint8_t> pixels;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
        };

        struct DecodedDdsImage final
        {
            std::vector<std::uint8_t> bytes;
            std::vector<D3D12TextureSubresourceSource> subresources;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint16_t mip_levels = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        };

        constexpr std::uint32_t MakeFourCc(char a, char b, char c, char d) noexcept
        {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
        }

#pragma pack(push, 1)
        struct DdsPixelFormat final
        {
            std::uint32_t size;
            std::uint32_t flags;
            std::uint32_t four_cc;
            std::uint32_t rgb_bit_count;
            std::uint32_t r_mask;
            std::uint32_t g_mask;
            std::uint32_t b_mask;
            std::uint32_t a_mask;
        };

        struct DdsHeader final
        {
            std::uint32_t size;
            std::uint32_t flags;
            std::uint32_t height;
            std::uint32_t width;
            std::uint32_t pitch_or_linear_size;
            std::uint32_t depth;
            std::uint32_t mip_map_count;
            std::uint32_t reserved1[11];
            DdsPixelFormat pixel_format;
            std::uint32_t caps;
            std::uint32_t caps2;
            std::uint32_t caps3;
            std::uint32_t caps4;
            std::uint32_t reserved2;
        };

        struct DdsHeaderDx10 final
        {
            DXGI_FORMAT format;
            std::uint32_t resource_dimension;
            std::uint32_t misc_flag;
            std::uint32_t array_size;
            std::uint32_t misc_flags2;
        };
#pragma pack(pop)

        static_assert(sizeof(DdsPixelFormat) == 32);
        static_assert(sizeof(DdsHeader) == 124);
        static_assert(sizeof(DdsHeaderDx10) == 20);

        bool IsBlockCompressed(DXGI_FORMAT format, std::uint32_t& block_bytes) noexcept
        {
            switch (format)
            {
            case DXGI_FORMAT_BC1_TYPELESS:
            case DXGI_FORMAT_BC1_UNORM:
            case DXGI_FORMAT_BC1_UNORM_SRGB:
            case DXGI_FORMAT_BC4_TYPELESS:
            case DXGI_FORMAT_BC4_UNORM:
            case DXGI_FORMAT_BC4_SNORM:
                block_bytes = 8;
                return true;
            case DXGI_FORMAT_BC2_TYPELESS:
            case DXGI_FORMAT_BC2_UNORM:
            case DXGI_FORMAT_BC2_UNORM_SRGB:
            case DXGI_FORMAT_BC3_TYPELESS:
            case DXGI_FORMAT_BC3_UNORM:
            case DXGI_FORMAT_BC3_UNORM_SRGB:
            case DXGI_FORMAT_BC5_TYPELESS:
            case DXGI_FORMAT_BC5_UNORM:
            case DXGI_FORMAT_BC5_SNORM:
            case DXGI_FORMAT_BC6H_TYPELESS:
            case DXGI_FORMAT_BC6H_UF16:
            case DXGI_FORMAT_BC6H_SF16:
            case DXGI_FORMAT_BC7_TYPELESS:
            case DXGI_FORMAT_BC7_UNORM:
            case DXGI_FORMAT_BC7_UNORM_SRGB:
                block_bytes = 16;
                return true;
            default:
                block_bytes = 0;
                return false;
            }
        }

        DXGI_FORMAT LegacyDdsFormat(const DdsPixelFormat& format) noexcept
        {
            constexpr std::uint32_t kFourCc = 0x4u;
            constexpr std::uint32_t kRgb = 0x40u;
            if ((format.flags & kFourCc) != 0)
            {
                switch (format.four_cc)
                {
                case MakeFourCc('D', 'X', 'T', '1'): return DXGI_FORMAT_BC1_UNORM;
                case MakeFourCc('D', 'X', 'T', '3'): return DXGI_FORMAT_BC2_UNORM;
                case MakeFourCc('D', 'X', 'T', '5'): return DXGI_FORMAT_BC3_UNORM;
                case MakeFourCc('A', 'T', 'I', '1'):
                case MakeFourCc('B', 'C', '4', 'U'): return DXGI_FORMAT_BC4_UNORM;
                case MakeFourCc('B', 'C', '4', 'S'): return DXGI_FORMAT_BC4_SNORM;
                case MakeFourCc('A', 'T', 'I', '2'):
                case MakeFourCc('B', 'C', '5', 'U'): return DXGI_FORMAT_BC5_UNORM;
                case MakeFourCc('B', 'C', '5', 'S'): return DXGI_FORMAT_BC5_SNORM;
                default: return DXGI_FORMAT_UNKNOWN;
                }
            }
            if ((format.flags & kRgb) != 0 && format.rgb_bit_count == 32)
            {
                if (format.r_mask == 0x000000ffu && format.g_mask == 0x0000ff00u &&
                    format.b_mask == 0x00ff0000u && format.a_mask == 0xff000000u)
                    return DXGI_FORMAT_R8G8B8A8_UNORM;
                if (format.r_mask == 0x00ff0000u && format.g_mask == 0x0000ff00u &&
                    format.b_mask == 0x000000ffu && format.a_mask == 0xff000000u)
                    return DXGI_FORMAT_B8G8R8A8_UNORM;
            }
            return DXGI_FORMAT_UNKNOWN;
        }

        bool IsSupportedStaticDdsFormat(DXGI_FORMAT format) noexcept
        {
            // Resource が Typeless の場合でも、SRV は Typeless Format を直接使えない。
            // Phase 2 の DDS 管理は単純さを優先し、View Format を推測せず Typed Format
            // だけを受け付ける。
            switch (format)
            {
            case DXGI_FORMAT_BC1_TYPELESS:
            case DXGI_FORMAT_BC2_TYPELESS:
            case DXGI_FORMAT_BC3_TYPELESS:
            case DXGI_FORMAT_BC4_TYPELESS:
            case DXGI_FORMAT_BC5_TYPELESS:
            case DXGI_FORMAT_BC6H_TYPELESS:
            case DXGI_FORMAT_BC7_TYPELESS:
                return false;
            default:
                break;
            }
            std::uint32_t block_bytes = 0;
            if (IsBlockCompressed(format, block_bytes)) return true;
            switch (format)
            {
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                return true;
            default:
                return false;
            }
        }

        bool DecodeDds2D(const std::filesystem::path& path,
            DecodedDdsImage& image) noexcept
        {
            image = {};
            if (path.empty()) return false;
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;

            constexpr std::uint32_t kDdsMagic = 0x20534444u;
            constexpr std::uint32_t kDx10FourCc = MakeFourCc('D', 'X', '1', '0');
            std::uint32_t magic = 0;
            DdsHeader header{};
            stream.read(reinterpret_cast<char*>(&magic), sizeof(magic));
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream || magic != kDdsMagic || header.size != sizeof(DdsHeader) ||
                header.pixel_format.size != sizeof(DdsPixelFormat) || header.width == 0 ||
                header.height == 0 || header.width > 16384 || header.height > 16384)
                return false;

            DXGI_FORMAT dxgi_format = DXGI_FORMAT_UNKNOWN;
            if (header.pixel_format.four_cc == kDx10FourCc)
            {
                DdsHeaderDx10 dx10{};
                stream.read(reinterpret_cast<char*>(&dx10), sizeof(dx10));
                // Phase 2 の Material 経路が扱うのは通常の Texture2D Asset だけ。
                // Array/Cubemap/3D Resource は後続の Environment/Shadow 実装で扱う。
                constexpr std::uint32_t kTexture2D = 3u; // D3D10_RESOURCE_DIMENSION_TEXTURE2D
                constexpr std::uint32_t kTextureCube = 0x4u;
                if (!stream || dx10.resource_dimension != kTexture2D || dx10.array_size != 1 ||
                    (dx10.misc_flag & kTextureCube) != 0)
                    return false;
                dxgi_format = dx10.format;
            }
            else
            {
                // この Static Material Loader では旧形式の Cubemap/Volume Texture を拒否する。
                constexpr std::uint32_t kCaps2Cubemap = 0x00000200u;
                constexpr std::uint32_t kCaps2Volume = 0x00200000u;
                if ((header.caps2 & (kCaps2Cubemap | kCaps2Volume)) != 0) return false;
                dxgi_format = LegacyDdsFormat(header.pixel_format);
            }
            if (!IsSupportedStaticDdsFormat(dxgi_format)) return false;

            const std::uint32_t mip_count = (std::max)(1u, header.mip_map_count);
            if (mip_count > 15u) return false;
            const std::streampos payload_start = stream.tellg();
            if (payload_start < 0) return false;
            stream.seekg(0, std::ios::end);
            const std::streampos payload_end = stream.tellg();
            if (payload_end < payload_start) return false;
            const std::uint64_t payload_size = static_cast<std::uint64_t>(
                payload_end - payload_start);
            if (payload_size == 0 || payload_size > (1ull << 34)) return false;
            stream.seekg(payload_start, std::ios::beg);

            try
            {
                image.bytes.resize(static_cast<std::size_t>(payload_size));
                stream.read(reinterpret_cast<char*>(image.bytes.data()),
                    static_cast<std::streamsize>(image.bytes.size()));
                if (!stream) { image = {}; return false; }
                image.subresources.reserve(mip_count);

                std::size_t offset = 0;
                std::uint32_t width = header.width;
                std::uint32_t height = header.height;
                std::uint32_t block_bytes = 0;
                const bool block_compressed = IsBlockCompressed(dxgi_format, block_bytes);
                for (std::uint32_t mip = 0; mip < mip_count; ++mip)
                {
                    std::uint64_t row_pitch = 0;
                    std::uint64_t row_count = 0;
                    if (block_compressed)
                    {
                        const std::uint64_t blocks_wide = (std::max)(1u, (width + 3u) / 4u);
                        const std::uint64_t blocks_high = (std::max)(1u, (height + 3u) / 4u);
                        row_pitch = blocks_wide * block_bytes;
                        row_count = blocks_high;
                    }
                    else
                    {
                        row_pitch = static_cast<std::uint64_t>(width) * 4ull;
                        row_count = height;
                    }
                    const std::uint64_t slice_pitch = row_pitch * row_count;
                    if (slice_pitch == 0 || slice_pitch > image.bytes.size() - offset)
                    {
                        image = {};
                        return false;
                    }
                    D3D12TextureSubresourceSource source;
                    source.data = image.bytes.data() + offset;
                    source.row_pitch = row_pitch;
                    source.slice_pitch = slice_pitch;
                    image.subresources.push_back(source);
                    offset += static_cast<std::size_t>(slice_pitch);
                    width = (std::max)(1u, width >> 1);
                    height = (std::max)(1u, height >> 1);
                }
            }
            catch (...)
            {
                image = {};
                return false;
            }
            image.width = header.width;
            image.height = header.height;
            image.mip_levels = static_cast<std::uint16_t>(mip_count);
            image.format = dxgi_format;
            return true;
        }

        bool DecodeWicRgba8(const std::filesystem::path& path,
            DecodedRgbaImage& image) noexcept
        {
            image = {};
            if (path.empty()) return false;

            bool uninitialize_com = false;
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            if (hr == CO_E_NOTINITIALIZED)
            {
                const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                uninitialize_com = SUCCEEDED(com);
                hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
            }
            if (FAILED(hr) || factory == nullptr)
            {
                if (uninitialize_com) CoUninitialize();
                return false;
            }

            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
                GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
            UINT width = 0, height = 0;
            if (SUCCEEDED(hr)) hr = frame->GetSize(&width, &height);
            if (FAILED(hr) || width == 0 || height == 0 ||
                width > 16384 || height > 16384)
            {
                if (uninitialize_com) CoUninitialize();
                return false;
            }

            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            hr = factory->CreateFormatConverter(&converter);
            if (SUCCEEDED(hr))
            {
                hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                    WICBitmapDitherTypeNone, nullptr, 0.0,
                    WICBitmapPaletteTypeCustom);
            }
            if (FAILED(hr))
            {
                if (uninitialize_com) CoUninitialize();
                return false;
            }

            try
            {
                const std::uint64_t byte_count = static_cast<std::uint64_t>(width) *
                    static_cast<std::uint64_t>(height) * 4ull;
                if (byte_count > (std::numeric_limits<UINT>::max)())
                {
                    if (uninitialize_com) CoUninitialize();
                    return false;
                }
                image.pixels.resize(static_cast<std::size_t>(byte_count));
                hr = converter->CopyPixels(nullptr, width * 4u,
                    static_cast<UINT>(byte_count), image.pixels.data());
                if (SUCCEEDED(hr))
                {
                    image.width = width;
                    image.height = height;
                }
            }
            catch (...)
            {
                hr = E_OUTOFMEMORY;
            }
            if (uninitialize_com) CoUninitialize();
            if (FAILED(hr)) image = {};
            return image.width != 0 && image.height != 0 && !image.pixels.empty();
        }

        bool IsValidSize(std::uint32_t width, std::uint32_t height) noexcept
        {
            return width != 0 && height != 0;
        }

        void DebugMessage(const char* message) noexcept
        {
            if (message != nullptr) OutputDebugStringA(message);
        }
    }

    D3D12DeviceContext::~D3D12DeviceContext()
    {
        Shutdown();
    }

    bool D3D12DeviceContext::Initialize(HWND window, std::uint32_t width,
        std::uint32_t height, bool enable_debug_layer, bool force_warp,
        bool create_validation_resources) noexcept
    {
        if (window == nullptr || !IsValidSize(width, height)) return false;
        Shutdown();
        validation_resources_enabled_ = create_validation_resources;
        if (!CreateDevice(enable_debug_layer, force_warp))
        {
            Shutdown();
            return false;
        }
        if (!CreateSwapChain(window, width, height) || !CreateRenderTargets())
        {
            Shutdown();
            return false;
        }
        if (!CreateStaticRendererResources())
        {
            Shutdown();
            return false;
        }
        if (validation_resources_enabled_ && !CreateValidationTriangleResources())
        {
            Shutdown();
            return false;
        }
        return true;
    }

    void D3D12DeviceContext::Shutdown() noexcept
    {
        if (device_ != nullptr) (void)WaitForGpu();

        for (auto& batch : render_item_batches_)
            batch.Reset(&resource_descriptor_allocator_);
        ReleaseValidationTriangleResources();
        ReleaseStaticRendererResources();
        ReleaseRenderTargets();

        upload_context_.Shutdown();
        for (auto& frame : frame_resources_) frame.Shutdown();

        if (fence_event_ != nullptr)
        {
            CloseHandle(fence_event_);
            fence_event_ = nullptr;
        }
        command_list_.Reset();
        fence_.Reset();
        swap_chain_.Reset();
        sampler_descriptor_allocator_.Reset();
        resource_descriptor_allocator_.Reset();
        command_queue_.Reset();
        resource_state_tracker_.Reset();

        if (debug_layer_enabled_ && device_ != nullptr)
        {
            Microsoft::WRL::ComPtr<ID3D12DebugDevice> debug_device;
            if (SUCCEEDED(device_.As(&debug_device)) && debug_device)
            {
                debug_device->ReportLiveDeviceObjects(
                    D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
            }
        }
        device_.Reset();
        adapter_.Reset();
        factory_.Reset();

        next_fence_value_ = 1;
        last_signaled_fence_value_ = 0;
        frame_index_ = 0;
        width_ = 0;
        height_ = 0;
        frame_open_ = false;
        debug_layer_enabled_ = false;
        gpu_validation_enabled_ = false;
        dred_enabled_ = false;
        allow_tearing_ = false;
        validation_resources_enabled_ = false;
        fatal_error_ = false;
        last_device_removed_reason_ = S_OK;
        current_frame_constants_ = {};
    }

    bool D3D12DeviceContext::ConfigureDebug(bool enable_debug_layer) noexcept
    {
        debug_layer_enabled_ = false;
        gpu_validation_enabled_ = false;
        dred_enabled_ = false;
        if (!enable_debug_layer) return true;

        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))) && debug)
        {
            debug->EnableDebugLayer();
            debug_layer_enabled_ = true;

            Microsoft::WRL::ComPtr<ID3D12Debug1> debug1;
            if (SUCCEEDED(debug.As(&debug1)) && debug1)
            {
                debug1->SetEnableGPUBasedValidation(TRUE);
                debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
                gpu_validation_enabled_ = true;
            }
        }

        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))) && dred)
        {
            dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred_enabled_ = true;
        }
        return true;
    }

    bool D3D12DeviceContext::CreateDevice(bool enable_debug_layer,
        bool force_warp) noexcept
    {
        if (!ConfigureDebug(enable_debug_layer)) return false;
        const UINT factory_flags = debug_layer_enabled_ ? DXGI_CREATE_FACTORY_DEBUG : 0u;
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
                Microsoft::WRL::ComPtr<IDXGIAdapter4> candidate;
                const HRESULT enumerate = factory_->EnumAdapterByGpuPreference(index,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
                if (enumerate == DXGI_ERROR_NOT_FOUND) break;
                if (FAILED(enumerate) || candidate == nullptr) continue;

                DXGI_ADAPTER_DESC3 description{};
                if (FAILED(candidate->GetDesc3(&description)) ||
                    (description.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0)
                    continue;
                if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                    __uuidof(ID3D12Device), nullptr)))
                {
                    adapter_ = candidate;
                    break;
                }
            }
            if (adapter_ == nullptr &&
                FAILED(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_))))
                return false;
        }

        if (FAILED(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device_))))
            return false;

        if (debug_layer_enabled_)
        {
            Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue;
            if (SUCCEEDED(device_.As(&info_queue)) && info_queue)
            {
                info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
                info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            }
        }

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (FAILED(device_->CreateCommandQueue(&queue_desc,
            IID_PPV_ARGS(&command_queue_))))
            return false;

        if (!upload_context_.Initialize(device_.Get(), command_queue_.Get()) ||
            !resource_descriptor_allocator_.Initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096, true) ||
            !sampler_descriptor_allocator_.Initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 128, true))
            return false;

        for (auto& frame : frame_resources_)
        {
            if (!frame.Initialize(device_.Get(), FrameUploadCapacity)) return false;
        }
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            frame_resources_[0].command_allocator.Get(), nullptr,
            IID_PPV_ARGS(&command_list_))))
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
        BOOL tearing_supported = FALSE;
        allow_tearing_ = SUCCEEDED(factory_->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing_supported,
            sizeof(tearing_supported))) && tearing_supported == TRUE;

        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width;
        description.Height = height;
        description.Format = kBackBufferFormat;
        description.BufferCount = FrameCount;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.SampleDesc.Count = 1;
        description.Flags = allow_tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

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
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount + 2u, false) ||
            !rtv_allocator_.Allocate(FrameCount, rtv_allocation_) ||
            !dsv_allocator_.Initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 3, false) ||
            !dsv_allocator_.Allocate(1, dsv_allocation_))
            return false;

        for (std::uint32_t index = 0; index < FrameCount; ++index)
        {
            if (FAILED(swap_chain_->GetBuffer(index,
                IID_PPV_ARGS(&render_targets_[index]))))
                return false;
            device_->CreateRenderTargetView(render_targets_[index].Get(), nullptr,
                rtv_allocator_.CpuHandle(rtv_allocation_.index + index));
            if (!resource_state_tracker_.Track(render_targets_[index].Get(),
                D3D12_RESOURCE_STATE_PRESENT))
                return false;
        }

        D3D12_HEAP_PROPERTIES depth_heap{};
        depth_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC depth_description{};
        depth_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depth_description.Width = width_;
        depth_description.Height = height_;
        depth_description.DepthOrArraySize = 1;
        depth_description.MipLevels = 1;
        depth_description.Format = kDepthFormat;
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
        if (!resource_state_tracker_.Track(depth_stencil_buffer_.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE))
            return false;

        if (!CreateOffscreenTarget(scene_view_target_, width_, height_) ||
            !CreateOffscreenTarget(game_view_target_, width_, height_))
            return false;
        return true;
    }

    bool D3D12DeviceContext::CreateOffscreenTarget(D3D12OffscreenTarget& target,
        std::uint32_t width, std::uint32_t height) noexcept
    {
        if (!IsValidSize(width, height)) return false;
        ReleaseOffscreenTarget(target);
        if (!rtv_allocator_.Allocate(1, target.rtv) ||
            !dsv_allocator_.Allocate(1, target.dsv) ||
            !resource_descriptor_allocator_.Allocate(1, target.srv))
        {
            ReleaseOffscreenTarget(target);
            return false;
        }

        D3D12_HEAP_PROPERTIES default_heap{};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC color_description{};
        color_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        color_description.Width = width;
        color_description.Height = height;
        color_description.DepthOrArraySize = 1;
        color_description.MipLevels = 1;
        color_description.Format = kBackBufferFormat;
        color_description.SampleDesc.Count = 1;
        color_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        color_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE color_clear{};
        color_clear.Format = kBackBufferFormat;
        color_clear.Color[3] = 1.0f;
        if (FAILED(device_->CreateCommittedResource(&default_heap,
            D3D12_HEAP_FLAG_NONE, &color_description,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &color_clear,
            IID_PPV_ARGS(&target.color))))
        {
            ReleaseOffscreenTarget(target);
            return false;
        }
        device_->CreateRenderTargetView(target.color.Get(), nullptr, target.rtv.cpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = kBackBufferFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(target.color.Get(), &srv, target.srv.cpu);
        if (!resource_state_tracker_.Track(target.color.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))
        {
            ReleaseOffscreenTarget(target);
            return false;
        }

        D3D12_RESOURCE_DESC depth_description{};
        depth_description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depth_description.Width = width;
        depth_description.Height = height;
        depth_description.DepthOrArraySize = 1;
        depth_description.MipLevels = 1;
        depth_description.Format = kDepthFormat;
        depth_description.SampleDesc.Count = 1;
        depth_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depth_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE depth_clear{};
        depth_clear.Format = kDepthFormat;
        depth_clear.DepthStencil.Depth = 1.0f;
        if (FAILED(device_->CreateCommittedResource(&default_heap,
            D3D12_HEAP_FLAG_NONE, &depth_description,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear,
            IID_PPV_ARGS(&target.depth))))
        {
            ReleaseOffscreenTarget(target);
            return false;
        }
        device_->CreateDepthStencilView(target.depth.Get(), nullptr, target.dsv.cpu);
        if (!resource_state_tracker_.Track(target.depth.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE))
        {
            ReleaseOffscreenTarget(target);
            return false;
        }
        target.width = width;
        target.height = height;
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
        pipeline_desc.DSVFormat = kDepthFormat;
        pipeline_desc.SampleDesc.Count = 1;
        if (FAILED(device_->CreateGraphicsPipelineState(&pipeline_desc,
            IID_PPV_ARGS(&validation_pipeline_))))
            return false;

        return validation_mesh_.Upload(device_.Get(), upload_context_,
            kValidationVertices, sizeof(kValidationVertices), sizeof(ValidationVertex),
            kValidationIndices, sizeof(kValidationIndices), DXGI_FORMAT_R16_UINT);
    }


    bool D3D12DeviceContext::CreateStaticRendererResources() noexcept
    {
        D3D12ShaderCompiler shader_compiler;
        if (!shader_compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath()))
            return false;
        const std::filesystem::path source_name =
            std::filesystem::current_path() / "DX12StaticRenderer.hlsl";
        const D3D12ShaderCompileResult vertex_shader = shader_compiler.CompileSource(
            kStaticVertexShader, source_name, L"main", L"vs_6_0", debug_layer_enabled_);
        const D3D12ShaderCompileResult pixel_shader = shader_compiler.CompileSource(
            kStaticPixelShader, source_name, L"main", L"ps_6_0", debug_layer_enabled_);
        shader_compiler.Shutdown();
        if (!vertex_shader.succeeded || vertex_shader.bytecode.empty() ||
            !pixel_shader.succeeded || pixel_shader.bytecode.empty())
        {
            DebugMessage("[DX12] Static renderer shader compilation failed.\n");
            return false;
        }
        static_vertex_shader_bytecode_ = vertex_shader.bytecode;

        constexpr std::uint32_t kMaterialTextureSlots = 8;
        constexpr std::uint32_t kSamplerSlots = 3;
        D3D12_DESCRIPTOR_RANGE ranges[1 + kMaterialTextureSlots + kSamplerSlots]{};
        D3D12_ROOT_PARAMETER parameters[4 + 1 + kMaterialTextureSlots + kSamplerSlots]{};

        // b0 は Object、b1 は Scene、b4 は Frame 互換、b9 は Material Schema。
        const UINT cb_registers[4] = { 0u, 1u, 4u, 9u };
        for (UINT i = 0; i < 4; ++i)
        {
            parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[i].ShaderVisibility = i == 0 ? D3D12_SHADER_VISIBILITY_VERTEX :
                i == 3 ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_ALL;
            parameters[i].Descriptor.ShaderRegister = cb_registers[i];
        }

        // t0 は Phase 2 Bridge と旧 Custom Shader が使う Legacy/Base Texture。
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[4].DescriptorTable.NumDescriptorRanges = 1;
        parameters[4].DescriptorTable.pDescriptorRanges = &ranges[0];

        // t40 以降は既存の ShaderConstantPacker が管理する。Cache 済み SRV を
        // 連続した一時 Table へコピーせずに済むよう、1 Descriptor Table ずつ保持する。
        for (std::uint32_t i = 0; i < kMaterialTextureSlots; ++i)
        {
            D3D12_DESCRIPTOR_RANGE& range = ranges[1 + i];
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = 40u + i;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            D3D12_ROOT_PARAMETER& parameter = parameters[5 + i];
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            parameter.DescriptorTable.NumDescriptorRanges = 1;
            parameter.DescriptorTable.pDescriptorRanges = &range;
        }

        // 旧 Shader Library は s0/s1/s2 を使う。Phase 2 では 3 Slot とも Anisotropic
        // Sampler を使い、Sampler State の特殊化は ABI を変えずに追加できるようにする。
        for (std::uint32_t i = 0; i < kSamplerSlots; ++i)
        {
            D3D12_DESCRIPTOR_RANGE& range = ranges[1 + kMaterialTextureSlots + i];
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = i;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            D3D12_ROOT_PARAMETER& parameter = parameters[5 + kMaterialTextureSlots + i];
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            parameter.DescriptorTable.NumDescriptorRanges = 1;
            parameter.DescriptorTable.pDescriptorRanges = &range;
        }

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = static_cast<UINT>(std::size(parameters));
        root_desc.pParameters = parameters;
        root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
        Microsoft::WRL::ComPtr<ID3DBlob> serialized;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized, &errors)) || serialized == nullptr)
            return false;
        if (FAILED(device_->CreateRootSignature(0, serialized->GetBufferPointer(),
            serialized->GetBufferSize(), IID_PPV_ARGS(&static_root_signature_))))
            return false;

        if (!CreateStaticPipelineSet(pixel_shader.bytecode, static_bridge_pipelines_))
            return false;

        for (D3D12DescriptorAllocation& allocation : static_samplers_)
            if (!sampler_descriptor_allocator_.Allocate(1, allocation)) return false;

        // 旧 Sampler State ABI に合わせ、s0=Point/Border、s1=Linear/Clamp、
        // s2=Anisotropic/Wrap とする。既存の ShaderAsset/Composer は Backend の内部が
        // 変わっても同じ Sampler Register を使い続けられる。
        D3D12_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        device_->CreateSampler(&sampler, static_samplers_[0].cpu);

        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        device_->CreateSampler(&sampler, static_samplers_[1].cpu);

        sampler.Filter = D3D12_FILTER_ANISOTROPIC;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxAnisotropy = 16;
        device_->CreateSampler(&sampler, static_samplers_[2].cpu);

        if (!upload_context_.BeginBatch()) return false;
        const bool defaults_created =
            CreateSolidStaticTexture("__dx12_white", 0xFFFFFFFFu) &&
            CreateSolidStaticTexture("__dx12_black", 0xFF000000u) &&
            CreateSolidStaticTexture("__dx12_gray", 0xFF808080u) &&
            CreateSolidStaticTexture("__dx12_bump", 0xFFFF8080u);
        const bool defaults_uploaded = upload_context_.EndBatch();
        return defaults_created && defaults_uploaded;
    }

    bool D3D12DeviceContext::CreateStaticPipelineSet(
        const std::vector<std::uint8_t>& pixel_shader,
        StaticPipelineSet& pipelines) noexcept
    {
        for (auto& pipeline : pipelines.pipelines) pipeline.Reset();
        if (static_root_signature_ == nullptr || static_vertex_shader_bytecode_.empty() ||
            pixel_shader.empty())
            return false;

        D3D12_INPUT_ELEMENT_DESC input_elements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        for (std::uint32_t sided = 0; sided < 2; ++sided)
        {
            for (std::uint32_t alpha = 0; alpha < 3; ++alpha)
            {
                D3D12_BLEND_DESC blend{};
                blend.AlphaToCoverageEnable = FALSE;
                blend.IndependentBlendEnable = FALSE;
                D3D12_RENDER_TARGET_BLEND_DESC target{};
                target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                if (alpha == static_cast<std::uint32_t>(D3D12StaticAlphaMode::Blend))
                {
                    target.BlendEnable = TRUE;
                    target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                    target.BlendOp = D3D12_BLEND_OP_ADD;
                    target.SrcBlendAlpha = D3D12_BLEND_ONE;
                    target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                }
                blend.RenderTarget[0] = target;

                D3D12_RASTERIZER_DESC raster{};
                raster.FillMode = D3D12_FILL_MODE_SOLID;
                raster.CullMode = sided != 0 ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
                raster.FrontCounterClockwise = TRUE;
                raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
                raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
                raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
                raster.DepthClipEnable = TRUE;
                raster.MultisampleEnable = FALSE;
                raster.AntialiasedLineEnable = FALSE;
                raster.ForcedSampleCount = 0;
                raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

                D3D12_DEPTH_STENCIL_DESC depth{};
                depth.DepthEnable = TRUE;
                depth.DepthWriteMask = alpha ==
                    static_cast<std::uint32_t>(D3D12StaticAlphaMode::Blend)
                    ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
                depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
                depth.StencilEnable = FALSE;

                D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
                desc.pRootSignature = static_root_signature_.Get();
                desc.VS = { static_vertex_shader_bytecode_.data(),
                    static_vertex_shader_bytecode_.size() };
                desc.PS = { pixel_shader.data(), pixel_shader.size() };
                desc.BlendState = blend;
                desc.SampleMask = UINT_MAX;
                desc.RasterizerState = raster;
                desc.DepthStencilState = depth;
                desc.InputLayout = { input_elements, static_cast<UINT>(std::size(input_elements)) };
                desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                desc.NumRenderTargets = 1;
                desc.RTVFormats[0] = kBackBufferFormat;
                desc.DSVFormat = kDepthFormat;
                desc.SampleDesc.Count = 1;
                const std::size_t pipeline_index = static_cast<std::size_t>(sided * 3u + alpha);
                if (FAILED(device_->CreateGraphicsPipelineState(&desc,
                    IID_PPV_ARGS(&pipelines.pipelines[pipeline_index]))))
                {
                    for (auto& pipeline : pipelines.pipelines) pipeline.Reset();
                    return false;
                }
            }
        }
        return true;
    }

    void D3D12DeviceContext::ReleaseStaticRendererResources() noexcept
    {
        for (auto& entry : texture_cache_)
        {
            if (entry.second.srv.IsValid())
                resource_descriptor_allocator_.Free(entry.second.srv);
        }
        texture_cache_.clear();
        static_texture_failures_.clear();
        static_mesh_cache_.clear();
        for (D3D12DescriptorAllocation& sampler : static_samplers_)
        {
            if (sampler.IsValid()) sampler_descriptor_allocator_.Free(sampler);
            sampler = {};
        }
        for (auto& pipeline : static_bridge_pipelines_.pipelines) pipeline.Reset();
        custom_static_pipelines_.clear();
        custom_static_shader_failures_.clear();
        static_vertex_shader_bytecode_.clear();
        static_root_signature_.Reset();
    }

    bool D3D12DeviceContext::CreateSolidStaticTexture(const char* key,
        std::uint32_t rgba) noexcept
    {
        if (key == nullptr || *key == '\0') return false;
        if (texture_cache_.find(key) != texture_cache_.end()) return true;
        StaticTextureResource texture;
        if (!D3D12ResourceFactory::CreateTexture2DRgba8(device_.Get(), upload_context_,
            &rgba, 1, 1, 4, texture.resource))
            return false;
        if (!resource_descriptor_allocator_.Allocate(1, texture.srv)) return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = texture.format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = texture.mip_levels;
        device_->CreateShaderResourceView(texture.resource.Get(), &srv, texture.srv.cpu);
        texture.width = 1;
        texture.height = 1;
        try
        {
            auto result = texture_cache_.try_emplace(key);
            if (!result.second)
            {
                resource_descriptor_allocator_.Free(texture.srv);
                return true;
            }
            result.first->second.resource = std::move(texture.resource);
            result.first->second.srv = texture.srv;
            result.first->second.width = texture.width;
            result.first->second.height = texture.height;
            result.first->second.mip_levels = texture.mip_levels;
            result.first->second.format = texture.format;
            texture.srv = {};
        }
        catch (...)
        {
            if (texture.srv.IsValid())
                resource_descriptor_allocator_.Free(texture.srv);
            return false;
        }
        return true;
    }

    bool D3D12DeviceContext::EnsureStaticMesh(
        const D3D12StaticMeshSource& source) noexcept
    {
        if (source.key.empty()) return false;
        if (static_mesh_cache_.find(source.key) != static_mesh_cache_.end()) return true;
        if (source.vertices.empty() || source.indices.empty()) return false;
        const std::uint64_t vertex_bytes = static_cast<std::uint64_t>(source.vertices.size()) *
            sizeof(D3D12StaticVertex);
        const std::uint64_t index_bytes = static_cast<std::uint64_t>(source.indices.size()) *
            sizeof(std::uint32_t);
        if (vertex_bytes > (std::numeric_limits<std::uint32_t>::max)() ||
            index_bytes > (std::numeric_limits<std::uint32_t>::max)())
            return false;
        auto mesh = std::make_unique<D3D12MeshBuffer>();
        if (!mesh->Upload(device_.Get(), upload_context_, source.vertices.data(),
            static_cast<std::uint32_t>(vertex_bytes), sizeof(D3D12StaticVertex),
            source.indices.data(), static_cast<std::uint32_t>(index_bytes),
            DXGI_FORMAT_R32_UINT))
            return false;
        try
        {
            static_mesh_cache_.emplace(source.key, std::move(mesh));
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    bool D3D12DeviceContext::EnsureStaticTexture(
        const D3D12StaticTextureSource& source) noexcept
    {
        if (source.key.empty()) return true;
        if (texture_cache_.find(source.key) != texture_cache_.end()) return true;
        if (static_texture_failures_.find(source.key) != static_texture_failures_.end())
            return false;

        const auto remember_decode_failure = [this, &source]() noexcept
        {
            try { static_texture_failures_.insert(source.key); }
            catch (...) {}
        };
        StaticTextureResource texture;
        std::string extension = source.source_path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });

        if (extension == ".dds")
        {
            DecodedDdsImage decoded;
            if (!DecodeDds2D(source.source_path, decoded))
            {
                DebugMessage("[DX12] DDS texture decode failed; using white fallback.\n");
                remember_decode_failure();
                return false;
            }
            if (!D3D12ResourceFactory::CreateTexture2D(device_.Get(), upload_context_,
                decoded.width, decoded.height, decoded.mip_levels, decoded.format,
                decoded.subresources, texture.resource))
                return false;
            texture.width = decoded.width;
            texture.height = decoded.height;
            texture.mip_levels = decoded.mip_levels;
            texture.format = decoded.format;
        }
        else
        {
            DecodedRgbaImage decoded;
            if (!DecodeWicRgba8(source.source_path, decoded))
            {
                DebugMessage("[DX12] WIC texture decode failed; using white fallback.\n");
                remember_decode_failure();
                return false;
            }
            const std::uint32_t row_pitch = decoded.width * 4u;
            if (!D3D12ResourceFactory::CreateTexture2DRgba8(device_.Get(), upload_context_,
                decoded.pixels.data(), decoded.width, decoded.height, row_pitch,
                texture.resource))
                return false;
            texture.width = decoded.width;
            texture.height = decoded.height;
            texture.mip_levels = 1;
            texture.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        if (!resource_descriptor_allocator_.Allocate(1, texture.srv)) return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = texture.format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = texture.mip_levels;
        device_->CreateShaderResourceView(texture.resource.Get(), &srv, texture.srv.cpu);
        try
        {
            auto result = texture_cache_.try_emplace(source.key);
            if (!result.second)
            {
                resource_descriptor_allocator_.Free(texture.srv);
                return true;
            }
            result.first->second.resource = std::move(texture.resource);
            result.first->second.srv = texture.srv;
            result.first->second.width = texture.width;
            result.first->second.height = texture.height;
            result.first->second.mip_levels = texture.mip_levels;
            result.first->second.format = texture.format;
            texture.srv = {};
        }
        catch (...)
        {
            if (texture.srv.IsValid())
                resource_descriptor_allocator_.Free(texture.srv);
            return false;
        }
        return true;
    }

    bool D3D12DeviceContext::EnsureStaticShader(
        const D3D12StaticShaderSource& source) noexcept
    {
        if (source.key.empty()) return true;
        if (custom_static_pipelines_.find(source.key) != custom_static_pipelines_.end())
            return true;
        if (custom_static_shader_failures_.find(source.key) !=
            custom_static_shader_failures_.end())
            return false;
        if (source.source_path.empty()) return false;

        std::ifstream file(source.source_path, std::ios::binary);
        if (!file)
        {
            try { custom_static_shader_failures_.insert(source.key); }
            catch (...) {}
            return false;
        }
        std::string body((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        if (body.size() >= 3 &&
            static_cast<unsigned char>(body[0]) == 0xEF &&
            static_cast<unsigned char>(body[1]) == 0xBB &&
            static_cast<unsigned char>(body[2]) == 0xBF)
            body.erase(0, 3);

        std::string combined;
        try
        {
            combined.reserve(source.generated_declaration.size() + body.size() + 256);
            combined += "#line 1 \"REPLAY_DX12_GENERATED\"\n";
            combined += source.generated_declaration;
            combined += "\n#line 1 \"";
            combined += source.source_path.generic_string();
            combined += "\"\n";
            combined += body;
        }
        catch (...)
        {
            return false;
        }

        D3D12ShaderCompiler compiler;
        if (!compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath()))
            return false;
        D3D12ShaderCompileOptions options;
        options.debug = debug_layer_enabled_;
        options.optimize = !debug_layer_enabled_;
        options.warnings_as_errors = false;
        options.include_directories.push_back(std::filesystem::current_path() / "Shader");
        options.include_directories.push_back(
            std::filesystem::current_path() / "Shader" / "Include");
        options.defines.push_back({ L"REPLAY_SKINNED", L"0" });
        const D3D12ShaderCompileResult compiled = compiler.CompileSource(combined,
            source.source_path, L"main", L"ps_6_0", options);
        compiler.Shutdown();
        if (!compiled.succeeded || compiled.bytecode.empty())
        {
            DebugMessage("[DX12] Custom surface shader DXC compile failed; using bridge PS.\n");
            if (!compiled.diagnostics.empty()) DebugMessage(compiled.diagnostics.c_str());
            try { custom_static_shader_failures_.insert(source.key); }
            catch (...) {}
            return false;
        }

        StaticPipelineSet pipelines;
        if (!CreateStaticPipelineSet(compiled.bytecode, pipelines))
        {
            DebugMessage("[DX12] Custom surface shader PSO creation failed; using bridge PS.\n");
            try { custom_static_shader_failures_.insert(source.key); }
            catch (...) {}
            return false;
        }
        try
        {
            custom_static_pipelines_.emplace(source.key, std::move(pipelines));
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    bool D3D12DeviceContext::ClearStaticAssetCaches() noexcept
    {
        if (!IsInitialized() || frame_open_) return false;
        std::size_t persistent_textures = 0;
        for (const auto& entry : texture_cache_)
            if (entry.first.rfind("__dx12_", 0) == 0) ++persistent_textures;
        if (static_mesh_cache_.empty() && texture_cache_.size() <= persistent_textures &&
            static_texture_failures_.empty() && custom_static_pipelines_.empty() &&
            custom_static_shader_failures_.empty())
            return true;
        if (!WaitForGpu()) return false;

        static_mesh_cache_.clear();
        static_texture_failures_.clear();
        custom_static_pipelines_.clear();
        custom_static_shader_failures_.clear();
        for (auto it = texture_cache_.begin(); it != texture_cache_.end();)
        {
            if (it->first.rfind("__dx12_", 0) == 0)
            {
                ++it;
                continue;
            }
            if (it->second.srv.IsValid())
                resource_descriptor_allocator_.Free(it->second.srv);
            it = texture_cache_.erase(it);
        }
        return true;
    }

    ID3D12PipelineState* D3D12DeviceContext::StaticPipeline(
        const std::string& shader_key, bool double_sided,
        D3D12StaticAlphaMode alpha_mode) const noexcept
    {
        const std::uint32_t alpha = (std::min)(2u,
            static_cast<std::uint32_t>(alpha_mode));
        const std::size_t index = static_cast<std::size_t>((double_sided ? 3u : 0u) + alpha);
        if (!shader_key.empty())
        {
            const auto custom = custom_static_pipelines_.find(shader_key);
            if (custom != custom_static_pipelines_.end())
                return custom->second.pipelines[index].Get();
        }
        return static_bridge_pipelines_.pipelines[index].Get();
    }

    bool D3D12DeviceContext::DrawStaticScene(
        const D3D12StaticSceneSubmission& submission) noexcept
    {
        if (!frame_open_ || static_root_signature_ == nullptr ||
            !static_samplers_[0].IsValid() || !static_samplers_[1].IsValid() ||
            !static_samplers_[2].IsValid())
            return false;

        // Custom ShaderAsset の PSO は遅延コンパイルし、Shader GUID 単位で Cache する。
        // Phase 2 Static ABI に合わずコンパイルできない Shader は失敗として記録し、
        // その Material は Bridge Pixel Shader へ戻してフレーム全体を壊さない。
        for (const D3D12StaticShaderSource& source : submission.shader_sources)
            EnsureStaticShader(source);

        // Cache Miss は 1 つの Command List / 1 回の同期点へまとめて Upload する。
        // Source File の Decode 失敗は意図的に White Texture へ戻すが、GPU Resource または
        // Allocation の失敗は黙って消さず、フレームを失敗させる。
        if (!upload_context_.BeginBatch()) return false;
        bool resource_upload_ok = true;
        for (const D3D12StaticMeshSource& source : submission.mesh_sources)
        {
            if (static_mesh_cache_.find(source.key) == static_mesh_cache_.end() &&
                !EnsureStaticMesh(source))
            {
                resource_upload_ok = false;
            }
        }
        for (const D3D12StaticTextureSource& source : submission.texture_sources)
        {
            if (!source.key.empty() &&
                texture_cache_.find(source.key) == texture_cache_.end() &&
                static_texture_failures_.find(source.key) == static_texture_failures_.end())
            {
                if (!EnsureStaticTexture(source) &&
                    static_texture_failures_.find(source.key) == static_texture_failures_.end())
                    resource_upload_ok = false;
            }
        }
        const bool upload_batch_ok = upload_context_.EndBatch();
        if (!resource_upload_ok || !upload_batch_ok) return false;

        ID3D12DescriptorHeap* heaps[] =
        {
            resource_descriptor_allocator_.Heap(),
            sampler_descriptor_allocator_.Heap()
        };
        command_list_->SetDescriptorHeaps(static_cast<UINT>(std::size(heaps)), heaps);
        command_list_->SetGraphicsRootSignature(static_root_signature_.Get());

        const auto white = texture_cache_.find("__dx12_white");
        if (white == texture_cache_.end()) return false;

        D3D12LinearUploadAllocator& allocator = frame_resources_[frame_index_].upload_allocator;
        const auto allocate_constants = [&allocator](const void* bytes, std::size_t byte_count,
            D3D12_GPU_VIRTUAL_ADDRESS& gpu) noexcept -> bool
        {
            const std::size_t actual_size = (std::max)(byte_count, static_cast<std::size_t>(16));
            D3D12UploadAllocation allocation{};
            if (!allocator.Allocate(actual_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
                allocation))
                return false;
            std::memset(allocation.cpu, 0, actual_size);
            if (bytes != nullptr && byte_count != 0)
                std::memcpy(allocation.cpu, bytes, byte_count);
            gpu = allocation.gpu;
            return true;
        };

        // b1 は従来の static_mesh.hlsli ABI を維持する。b4 は frame_common.hlsli を
        // 対応させ、Project/Composer の Surface Shader が DX11/DX12 で同じ Frame Symbol
        // を使えるようにする。
        StaticSceneConstants scene{};
        scene.view_projection = current_frame_constants_.view_projection;
        scene.camera_position = current_frame_constants_.camera_position;
        scene.light_direction = { 0.0f, -1.0f, 0.0f, 0.0f };
        D3D12_GPU_VIRTUAL_ADDRESS scene_gpu = 0;
        if (!allocate_constants(&scene, sizeof(scene), scene_gpu)) return false;

        StaticFrameCompatibilityConstants compatibility{};
        compatibility.frame_view = current_frame_constants_.view;
        compatibility.frame_projection = current_frame_constants_.projection;
        compatibility.frame_view_projection = current_frame_constants_.view_projection;
        compatibility.frame_inv_view = current_frame_constants_.inv_view;
        compatibility.frame_inv_projection = current_frame_constants_.inv_projection;
        compatibility.frame_inv_view_projection = current_frame_constants_.inv_view_projection;
        compatibility.frame_prev_view_projection = current_frame_constants_.prev_view_projection;
        compatibility.frame_camera_position = current_frame_constants_.camera_position;
        compatibility.frame_screen_size = current_frame_constants_.screen_size;
        compatibility.frame_camera_planes = current_frame_constants_.camera_planes;
        compatibility.frame_jitter = current_frame_constants_.jitter;
        compatibility.frame_params = current_frame_constants_.time_parameters;
        D3D12_GPU_VIRTUAL_ADDRESS frame_compatibility_gpu = 0;
        if (!allocate_constants(&compatibility, sizeof(compatibility),
            frame_compatibility_gpu))
            return false;

        for (const D3D12StaticDrawItem& draw : submission.draws)
        {
            const auto mesh_it = static_mesh_cache_.find(draw.mesh_key);
            if (mesh_it == static_mesh_cache_.end() || !mesh_it->second ||
                !mesh_it->second->IsValid())
                continue;

            const StaticTextureResource* base_texture = &white->second;
            if (!draw.base_color_texture_key.empty())
            {
                const auto texture_it = texture_cache_.find(draw.base_color_texture_key);
                if (texture_it != texture_cache_.end()) base_texture = &texture_it->second;
            }

            const bool custom_shader = !draw.shader_key.empty() &&
                custom_static_pipelines_.find(draw.shader_key) != custom_static_pipelines_.end();

            StaticObjectConstants object{};
            object.world = draw.world;
            object.material_color = custom_shader ? draw.vertex_tint :
                DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f };
            D3D12_GPU_VIRTUAL_ADDRESS object_gpu = 0;
            if (!allocate_constants(&object, sizeof(object), object_gpu)) return false;

            StaticBridgeMaterialConstants bridge{};
            bridge.base_color = draw.base_color;
            bridge.emissive_strength = { draw.emissive.x, draw.emissive.y,
                draw.emissive.z, draw.emissive_strength };
            bridge.surface_params = { draw.metallic, draw.roughness,
                draw.ambient_occlusion, draw.alpha_cutoff };
            bridge.render_params = {
                static_cast<float>(static_cast<std::uint32_t>(draw.alpha_mode)),
                draw.double_sided ? 1.0f : 0.0f, 0.0f, 0.0f };

            const void* material_bytes = custom_shader && !draw.material_constants.empty()
                ? draw.material_constants.data() : static_cast<const void*>(&bridge);
            const std::size_t material_size = custom_shader && !draw.material_constants.empty()
                ? draw.material_constants.size() : sizeof(bridge);
            D3D12_GPU_VIRTUAL_ADDRESS material_gpu = 0;
            if (!allocate_constants(material_bytes, material_size, material_gpu)) return false;

            const StaticTextureResource* material_textures[8]{};
            for (StaticTextureResource const*& texture : material_textures)
                texture = &white->second;
            for (const D3D12StaticMaterialTexture& mapped : draw.material_textures)
            {
                if (mapped.slot < 40u || mapped.slot >= 48u) continue;
                const auto texture_it = texture_cache_.find(mapped.texture_key);
                if (texture_it != texture_cache_.end())
                    material_textures[mapped.slot - 40u] = &texture_it->second;
            }

            ID3D12PipelineState* pipeline = StaticPipeline(
                custom_shader ? draw.shader_key : std::string{},
                draw.double_sided, draw.alpha_mode);
            if (pipeline == nullptr) return false;
            command_list_->SetPipelineState(pipeline);
            command_list_->SetGraphicsRootConstantBufferView(0, object_gpu);             // b0
            command_list_->SetGraphicsRootConstantBufferView(1, scene_gpu);              // b1
            command_list_->SetGraphicsRootConstantBufferView(2, frame_compatibility_gpu);// b4
            command_list_->SetGraphicsRootConstantBufferView(3, material_gpu);           // b9
            command_list_->SetGraphicsRootDescriptorTable(4, base_texture->srv.gpu);      // t0
            for (std::uint32_t i = 0; i < 8; ++i)
                command_list_->SetGraphicsRootDescriptorTable(5 + i,
                    material_textures[i]->srv.gpu);                                       // t40..t47
            command_list_->SetGraphicsRootDescriptorTable(13, static_samplers_[0].gpu);   // s0
            command_list_->SetGraphicsRootDescriptorTable(14, static_samplers_[1].gpu);   // s1
            command_list_->SetGraphicsRootDescriptorTable(15, static_samplers_[2].gpu);   // s2

            const D3D12_VERTEX_BUFFER_VIEW vertex_view = mesh_it->second->VertexView();
            const D3D12_INDEX_BUFFER_VIEW index_view = mesh_it->second->IndexView();
            command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            command_list_->IASetVertexBuffers(0, 1, &vertex_view);
            command_list_->IASetIndexBuffer(&index_view);
            const std::uint32_t available = mesh_it->second->IndexCount();
            const std::uint32_t start = (std::min)(draw.start_index, available);
            const std::uint32_t remaining = available - start;
            const std::uint32_t count = draw.index_count == 0
                ? remaining : (std::min)(draw.index_count, remaining);
            if (count != 0)
                command_list_->DrawIndexedInstanced(count, 1, start, 0, 0);
        }
        return true;
    }

    void D3D12DeviceContext::ReleaseOffscreenTarget(
        D3D12OffscreenTarget& target) noexcept
    {
        if (target.color) resource_state_tracker_.Forget(target.color.Get());
        if (target.depth) resource_state_tracker_.Forget(target.depth.Get());
        target.color.Reset();
        target.depth.Reset();
        if (target.rtv.IsValid()) rtv_allocator_.Free(target.rtv);
        if (target.dsv.IsValid()) dsv_allocator_.Free(target.dsv);
        if (target.srv.IsValid()) resource_descriptor_allocator_.Free(target.srv);
        target = {};
    }

    void D3D12DeviceContext::ReleaseRenderTargets() noexcept
    {
        ReleaseOffscreenTarget(scene_view_target_);
        ReleaseOffscreenTarget(game_view_target_);
        for (auto& target : render_targets_)
        {
            if (target) resource_state_tracker_.Forget(target.Get());
            target.Reset();
        }
        if (depth_stencil_buffer_)
            resource_state_tracker_.Forget(depth_stencil_buffer_.Get());
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

    bool D3D12DeviceContext::Resize(std::uint32_t width,
        std::uint32_t height) noexcept
    {
        if (!IsInitialized() || fatal_error_ || frame_open_ ||
            !IsValidSize(width, height))
            return false;
        if (!WaitForGpu()) return false;
        ReleaseRenderTargets();
        const UINT flags = allow_tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
        const HRESULT result = swap_chain_->ResizeBuffers(FrameCount, width, height,
            kBackBufferFormat, flags);
        if (FAILED(result))
        {
            ReportDeviceRemoved(result);
            return false;
        }
        frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
        width_ = width;
        height_ = height;
        if (!CreateRenderTargets())
        {
            ReportDeviceRemoved(E_FAIL);
            return false;
        }
        return true;
    }

    void D3D12DeviceContext::ReclaimDeferredDescriptors() noexcept
    {
        if (fence_ == nullptr) return;
        const std::uint64_t completed = fence_->GetCompletedValue();
        if (completed == (std::numeric_limits<std::uint64_t>::max)())
        {
            ReportDeviceRemoved(DXGI_ERROR_DEVICE_REMOVED);
            return;
        }
        resource_descriptor_allocator_.ReleaseCompleted(completed);
        sampler_descriptor_allocator_.ReleaseCompleted(completed);
    }

    bool D3D12DeviceContext::WaitForFrame(std::uint32_t frame_index) noexcept
    {
        if (frame_index >= FrameCount || fence_ == nullptr || fence_event_ == nullptr)
            return false;
        const std::uint64_t value = frame_resources_[frame_index].fence_value;
        std::uint64_t completed = fence_->GetCompletedValue();
        if (completed == (std::numeric_limits<std::uint64_t>::max)())
        {
            ReportDeviceRemoved(DXGI_ERROR_DEVICE_REMOVED);
            return false;
        }
        if (value == 0 || completed >= value)
        {
            ReclaimDeferredDescriptors();
            return true;
        }
        const HRESULT event_result = fence_->SetEventOnCompletion(value, fence_event_);
        if (FAILED(event_result))
        {
            ReportDeviceRemoved(event_result);
            return false;
        }
        if (WaitForSingleObject(fence_event_, INFINITE) != WAIT_OBJECT_0)
        {
            ReportDeviceRemoved(E_FAIL);
            return false;
        }
        completed = fence_->GetCompletedValue();
        if (completed == (std::numeric_limits<std::uint64_t>::max)())
        {
            ReportDeviceRemoved(DXGI_ERROR_DEVICE_REMOVED);
            return false;
        }
        ReclaimDeferredDescriptors();
        return completed >= value;
    }

    bool D3D12DeviceContext::TransitionCurrentRenderTarget(
        D3D12_RESOURCE_STATES after) noexcept
    {
        if (command_list_ == nullptr || render_targets_[frame_index_] == nullptr)
            return false;
        return resource_state_tracker_.Transition(command_list_.Get(),
            render_targets_[frame_index_].Get(), after);
    }

    bool D3D12DeviceContext::BeginFrame(const float clear_color[4]) noexcept
    {
        if (!IsInitialized() || fatal_error_ || frame_open_ || clear_color == nullptr)
            return false;
        if (!WaitForFrame(frame_index_)) return false;

        D3D12FrameResource& frame = frame_resources_[frame_index_];
        render_item_batches_[frame_index_].Reset(&resource_descriptor_allocator_);
        frame.ResetAfterGpu();
        HRESULT reset_result = frame.command_allocator->Reset();
        if (FAILED(reset_result))
        {
            ReportDeviceRemoved(reset_result);
            return false;
        }
        reset_result = command_list_->Reset(frame.command_allocator.Get(), nullptr);
        if (FAILED(reset_result))
        {
            ReportDeviceRemoved(reset_result);
            return false;
        }
        if (!TransitionCurrentRenderTarget(D3D12_RESOURCE_STATE_RENDER_TARGET))
        {
            command_list_->Close();
            ReportDeviceRemoved(E_FAIL);
            return false;
        }

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width_);
        viewport.Height = static_cast<float>(height_);
        viewport.MaxDepth = 1.0f;
        const D3D12_RECT scissor{ 0, 0,
            static_cast<LONG>(width_), static_cast<LONG>(height_) };
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, &scissor);

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
            frame_resources_[frame_index_].upload_allocator,
            resource_descriptor_allocator_, items);
    }

    bool D3D12DeviceContext::SubmitFrameConstants(
        const D3D12FrameConstants& constants) noexcept
    {
        if (!frame_open_) return false;
        D3D12UploadAllocation allocation{};
        if (!frame_resources_[frame_index_].upload_allocator.Allocate(
            sizeof(constants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
            allocation))
            return false;
        std::memcpy(allocation.cpu, &constants, sizeof(constants));
        frame_resources_[frame_index_].frame_constants_gpu = allocation.gpu;
        current_frame_constants_ = constants;
        return true;
    }

    std::uint64_t D3D12DeviceContext::SignalQueue() noexcept
    {
        if (command_queue_ == nullptr || fence_ == nullptr) return 0;
        if (next_fence_value_ == 0 ||
            next_fence_value_ == (std::numeric_limits<std::uint64_t>::max)())
        {
            ReportDeviceRemoved(E_FAIL);
            return 0;
        }
        const std::uint64_t value = next_fence_value_++;
        const HRESULT result = command_queue_->Signal(fence_.Get(), value);
        if (FAILED(result))
        {
            ReportDeviceRemoved(result);
            return 0;
        }
        last_signaled_fence_value_ = value;
        return value;
    }

    bool D3D12DeviceContext::EndFrame() noexcept
    {
        if (!frame_open_) return false;
        const std::uint32_t submitted_frame = frame_index_;
        if (!TransitionCurrentRenderTarget(D3D12_RESOURCE_STATE_PRESENT))
        {
            frame_open_ = false;
            command_list_->Close();
            ReportDeviceRemoved(E_FAIL);
            return false;
        }
        const HRESULT close_result = command_list_->Close();
        if (FAILED(close_result))
        {
            frame_open_ = false;
            ReportDeviceRemoved(close_result);
            return false;
        }
        ID3D12CommandList* lists[] = { command_list_.Get() };
        command_queue_->ExecuteCommandLists(1, lists);

        const HRESULT present = swap_chain_->Present(1, 0);
        if (FAILED(present))
        {
            frame_open_ = false;
            ReportDeviceRemoved(present);
            return false;
        }

        const std::uint64_t signal_value = SignalQueue();
        if (signal_value == 0)
        {
            frame_open_ = false;
            return false;
        }
        frame_resources_[submitted_frame].fence_value = signal_value;
        frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
        frame_open_ = false;
        return true;
    }

    bool D3D12DeviceContext::DrawValidationTriangle() noexcept
    {
        const D3D12FrameResource& frame = frame_resources_[frame_index_];
        if (!frame_open_ || validation_pipeline_ == nullptr ||
            validation_root_signature_ == nullptr || !validation_mesh_.IsValid() ||
            frame.frame_constants_gpu == 0 ||
            render_item_batches_[frame_index_].Empty() ||
            resource_descriptor_allocator_.Heap() == nullptr)
            return false;

        command_list_->SetGraphicsRootSignature(validation_root_signature_.Get());
        command_list_->SetPipelineState(validation_pipeline_.Get());
        ID3D12DescriptorHeap* descriptor_heaps[] =
        {
            resource_descriptor_allocator_.Heap()
        };
        command_list_->SetDescriptorHeaps(1, descriptor_heaps);
        command_list_->SetGraphicsRootConstantBufferView(0,
            frame.frame_constants_gpu);
        command_list_->SetGraphicsRootDescriptorTable(1,
            render_item_batches_[frame_index_].ShaderResourceAllocation().gpu);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->IASetVertexBuffers(0, 1, &validation_mesh_.VertexView());
        command_list_->IASetIndexBuffer(&validation_mesh_.IndexView());
        command_list_->DrawIndexedInstanced(validation_mesh_.IndexCount(),
            static_cast<UINT>(render_item_batches_[frame_index_].Size()), 0, 0, 0);
        return true;
    }

    bool D3D12DeviceContext::WaitForGpu() noexcept
    {
        if (command_queue_ == nullptr || fence_ == nullptr || fence_event_ == nullptr)
            return false;
        const std::uint64_t value = SignalQueue();
        if (value == 0) return false;
        std::uint64_t completed = fence_->GetCompletedValue();
        if (completed == (std::numeric_limits<std::uint64_t>::max)())
        {
            ReportDeviceRemoved(DXGI_ERROR_DEVICE_REMOVED);
            return false;
        }
        if (completed < value)
        {
            const HRESULT event_result = fence_->SetEventOnCompletion(value, fence_event_);
            if (FAILED(event_result))
            {
                ReportDeviceRemoved(event_result);
                return false;
            }
            if (WaitForSingleObject(fence_event_, INFINITE) != WAIT_OBJECT_0)
            {
                ReportDeviceRemoved(E_FAIL);
                return false;
            }
            completed = fence_->GetCompletedValue();
            if (completed == (std::numeric_limits<std::uint64_t>::max)())
            {
                ReportDeviceRemoved(DXGI_ERROR_DEVICE_REMOVED);
                return false;
            }
        }
        ReclaimDeferredDescriptors();
        return completed >= value;
    }

    void D3D12DeviceContext::ReportDeviceRemoved(HRESULT trigger) noexcept
    {
        fatal_error_ = true;
        if (device_ == nullptr)
        {
            last_device_removed_reason_ = trigger;
            return;
        }
        const HRESULT reason = device_->GetDeviceRemovedReason();
        last_device_removed_reason_ = FAILED(reason) ? reason : trigger;

        char message[256]{};
        std::snprintf(message, sizeof(message),
            "[DX12] device failure trigger=0x%08lx reason=0x%08lx\n",
            static_cast<unsigned long>(trigger),
            static_cast<unsigned long>(last_device_removed_reason_));
        DebugMessage(message);

        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        if (SUCCEEDED(device_.As(&dred)) && dred)
        {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
            if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs)) &&
                breadcrumbs.pHeadAutoBreadcrumbNode != nullptr)
                DebugMessage("[DX12][DRED] auto breadcrumbs are available.\n");

            D3D12_DRED_PAGE_FAULT_OUTPUT page_fault{};
            if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&page_fault)) &&
                page_fault.PageFaultVA != 0)
                DebugMessage("[DX12][DRED] page-fault data is available.\n");
        }
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
