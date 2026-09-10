#include "D3D12DeviceContext.h"
#include "D3D12ResourceFactory.h"
#include "D3D12ObjectName.h"

#include <d3d12sdklayers.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <limits>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace ReplayEngine::Rendering::DX12
{
    namespace
    {
        constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

        DXGI_FORMAT ToLinearTextureFormat(DXGI_FORMAT format) noexcept
        {
            switch (format)
            {
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_UNORM;
            case DXGI_FORMAT_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_UNORM;
            case DXGI_FORMAT_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_UNORM;
            case DXGI_FORMAT_BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_UNORM;
            default: return format;
            }
        }

        DXGI_FORMAT ToSrgbTextureFormat(DXGI_FORMAT format) noexcept
        {
            switch (ToLinearTextureFormat(format))
            {
            case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM_SRGB;
            case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_UNORM_SRGB;
            case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM_SRGB;
            case DXGI_FORMAT_BC7_UNORM: return DXGI_FORMAT_BC7_UNORM_SRGB;
            default: return DXGI_FORMAT_UNKNOWN;
            }
        }

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

        template <typename Vertex>
        D3D12MeshLocalBounds MakeMeshLocalBounds(const std::vector<Vertex>& vertices) noexcept
        {//
            D3D12MeshLocalBounds result;
            if (vertices.empty()) return result;
            result.minimum = vertices.front().position;
            result.maximum = vertices.front().position;
            for (const Vertex& vertex : vertices)
            {
                result.minimum.x = (std::min)(result.minimum.x, vertex.position.x);
                result.minimum.y = (std::min)(result.minimum.y, vertex.position.y);
                result.minimum.z = (std::min)(result.minimum.z, vertex.position.z);
                result.maximum.x = (std::max)(result.maximum.x, vertex.position.x);
                result.maximum.y = (std::max)(result.maximum.y, vertex.position.y);
                result.maximum.z = (std::max)(result.maximum.z, vertex.position.z);
            }
            result.valid = std::isfinite(result.minimum.x) &&
                std::isfinite(result.minimum.y) && std::isfinite(result.minimum.z) &&
                std::isfinite(result.maximum.x) &&
                std::isfinite(result.maximum.y) && std::isfinite(result.maximum.z);
            return result;
        }

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
            bool is_cube = false;
            std::uint32_t array_size = 1;
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
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                return true;
            default:
                return false;
            }
        }

        std::uint32_t DdsBytesPerPixel(DXGI_FORMAT format) noexcept
        {
            switch (format)
            {
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                return 4;
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                return 8;
            default:
                return 0;
            }
        }

        bool BuildDdsSubresources(DecodedDdsImage& image) noexcept
        {
            if (image.width == 0 || image.height == 0 || image.mip_levels == 0 ||
                image.array_size == 0)
                return false;
            std::uint32_t block_bytes = 0;
            const bool block_compressed = IsBlockCompressed(image.format, block_bytes);
            const std::uint32_t bytes_per_pixel = DdsBytesPerPixel(image.format);
            if (!block_compressed && bytes_per_pixel == 0) return false;
            try
            {
                image.subresources.clear();
                image.subresources.reserve(static_cast<std::size_t>(image.mip_levels) *
                    image.array_size);
                std::size_t offset = 0;
                for (std::uint32_t array_slice = 0; array_slice < image.array_size;
                    ++array_slice)
                {
                    std::uint32_t width = image.width;
                    std::uint32_t height = image.height;
                    for (std::uint32_t mip = 0; mip < image.mip_levels; ++mip)
                    {
                        std::uint64_t row_pitch = 0;
                        std::uint64_t row_count = 0;
                        if (block_compressed)
                        {
                            const std::uint64_t blocks_wide =
                                (std::max)(1u, (width + 3u) / 4u);
                            const std::uint64_t blocks_high =
                                (std::max)(1u, (height + 3u) / 4u);
                            row_pitch = blocks_wide * block_bytes;
                            row_count = blocks_high;
                        }
                        else
                        {
                            row_pitch = static_cast<std::uint64_t>(width) *
                                bytes_per_pixel;
                            row_count = height;
                        }
                        const std::uint64_t slice_pitch = row_pitch * row_count;
                        if (slice_pitch == 0 || offset > image.bytes.size() ||
                            slice_pitch > image.bytes.size() - offset)
                        {
                            image.subresources.clear();
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
            }
            catch (...)
            {
                image.subresources.clear();
                return false;
            }
            return true;
        }

        bool DecodeDdsFile(const std::filesystem::path& path,
            DecodedDdsImage& image, bool require_cube) noexcept
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
            bool is_cube = false;
            std::uint32_t array_size = 1;
            if (header.pixel_format.four_cc == kDx10FourCc)
            {
                DdsHeaderDx10 dx10{};
                stream.read(reinterpret_cast<char*>(&dx10), sizeof(dx10));
                constexpr std::uint32_t kTexture2D = 3u;
                constexpr std::uint32_t kTextureCube = 0x4u;
                if (!stream || dx10.resource_dimension != kTexture2D ||
                    dx10.array_size == 0)
                    return false;
                dxgi_format = dx10.format;
                is_cube = (dx10.misc_flag & kTextureCube) != 0;
                if (is_cube)
                {
                    if (dx10.array_size != 1) return false;
                    array_size = 6;
                }
                else
                {
                    if (dx10.array_size != 1) return false;
                    array_size = 1;
                }
            }
            else
            {
                constexpr std::uint32_t kCaps2Cubemap = 0x00000200u;
                constexpr std::uint32_t kCaps2Volume = 0x00200000u;
                constexpr std::uint32_t kCaps2CubeFaces = 0x0000fc00u;
                if ((header.caps2 & kCaps2Volume) != 0) return false;
                is_cube = (header.caps2 & kCaps2Cubemap) != 0;
                if (is_cube && (header.caps2 & kCaps2CubeFaces) != kCaps2CubeFaces)
                    return false;
                if (is_cube) array_size = 6;
                dxgi_format = LegacyDdsFormat(header.pixel_format);
            }
            if (array_size == 0 || (require_cube != is_cube))
                return false;
            if (require_cube && !is_cube) return false;
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
            image.is_cube = is_cube;
            image.array_size = array_size;
            if (!BuildDdsSubresources(image))
            {
                image = {};
                return false;
            }
            return true;
        }

        bool DecodeDds2D(const std::filesystem::path& path,
            DecodedDdsImage& image) noexcept
        {
            return DecodeDdsFile(path, image, false);
        }

        bool DecodeDdsCube(const std::filesystem::path& path,
            DecodedDdsImage& image) noexcept
        {
            return DecodeDdsFile(path, image, true);
        }

        struct DecodedHdrPanorama final
        {
            std::vector<float> rgb;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
        };

        struct SkyCpuFloat3 final
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        SkyCpuFloat3 SkyAdd(SkyCpuFloat3 a, SkyCpuFloat3 b) noexcept
        {
            return { a.x + b.x, a.y + b.y, a.z + b.z };
        }

        SkyCpuFloat3 SkyMultiply(SkyCpuFloat3 value, float scalar) noexcept
        {
            return { value.x * scalar, value.y * scalar, value.z * scalar };
        }

        float SkyDot(SkyCpuFloat3 a, SkyCpuFloat3 b) noexcept
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        SkyCpuFloat3 SkyCross(SkyCpuFloat3 a, SkyCpuFloat3 b) noexcept
        {
            return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x };
        }

        SkyCpuFloat3 SkyNormalize(SkyCpuFloat3 value) noexcept
        {
            const float length_squared = SkyDot(value, value);
            if (length_squared <= 1.0e-12f) return { 0.0f, 1.0f, 0.0f };
            const float inverse_length = 1.0f / std::sqrt(length_squared);
            return SkyMultiply(value, inverse_length);
        }

        float SkyClamp(float value, float minimum, float maximum) noexcept
        {
            return (std::max)(minimum, (std::min)(maximum, value));
        }

        std::uint16_t FloatToSkyHalf(float value) noexcept
        {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            const std::uint32_t sign = (bits >> 16) & 0x8000u;
            const std::uint32_t exponent = (bits >> 23) & 0xffu;
            std::uint32_t mantissa = bits & 0x007fffffu;
            if (exponent == 0xffu)
            {
                if (mantissa != 0) return static_cast<std::uint16_t>(sign | 0x7e00u);
                return static_cast<std::uint16_t>(sign | 0x7c00u);
            }
            const int half_exponent = static_cast<int>(exponent) - 127 + 15;
            if (half_exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7c00u);
            if (half_exponent <= 0)
            {
                if (half_exponent < -10) return static_cast<std::uint16_t>(sign);
                mantissa |= 0x00800000u;
                const int shift = 14 - half_exponent;
                std::uint32_t rounded = mantissa >> shift;
                const std::uint32_t remainder = mantissa & ((1u << shift) - 1u);
                const std::uint32_t halfway = 1u << (shift - 1);
                if (remainder > halfway ||
                    (remainder == halfway && (rounded & 1u) != 0))
                    ++rounded;
                return static_cast<std::uint16_t>(sign | rounded);
            }
            mantissa += 0x00001000u;
            if ((mantissa & 0x00800000u) != 0)
            {
                mantissa = 0;
                if (half_exponent + 1 >= 31)
                    return static_cast<std::uint16_t>(sign | 0x7c00u);
                return static_cast<std::uint16_t>(sign |
                    (static_cast<std::uint32_t>(half_exponent + 1) << 10));
            }
            return static_cast<std::uint16_t>(sign |
                (static_cast<std::uint32_t>(half_exponent) << 10) |
                (mantissa >> 13));
        }

        float SkyHalfToFloat(std::uint16_t value) noexcept
        {
            const std::uint32_t sign = (static_cast<std::uint32_t>(value & 0x8000u)) << 16;
            const std::uint32_t exponent = (value >> 10) & 0x1fu;
            const std::uint32_t mantissa = value & 0x03ffu;
            std::uint32_t bits = sign;
            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    float result = 0.0f;
                    std::memcpy(&result, &bits, sizeof(result));
                    return result;
                }
                const float result = std::ldexp(static_cast<float>(mantissa), -24);
                return (sign != 0) ? -result : result;
            }
            if (exponent == 31)
            {
                bits |= 0x7f800000u | (mantissa << 13);
                float result = 0.0f;
                std::memcpy(&result, &bits, sizeof(result));
                return result;
            }
            bits |= ((exponent + 112u) << 23) | (mantissa << 13);
            float result = 0.0f;
            std::memcpy(&result, &bits, sizeof(result));
            return result;
        }

        float SkySrgbToLinear(float value) noexcept
        {
            const float clamped = SkyClamp(value, 0.0f, 1.0f);
            return clamped <= 0.04045f ? clamped / 12.92f :
                std::pow((clamped + 0.055f) / 1.055f, 2.4f);
        }

        SkyCpuFloat3 SkyFaceDirection(std::uint32_t face, float u, float v) noexcept
        {
            switch (face)
            {
            case 0: return SkyNormalize({ 1.0f, -v, -u });
            case 1: return SkyNormalize({ -1.0f, -v, u });
            case 2: return SkyNormalize({ u, 1.0f, v });
            case 3: return SkyNormalize({ u, -1.0f, -v });
            case 4: return SkyNormalize({ u, -v, 1.0f });
            default: return SkyNormalize({ -u, -v, -1.0f });
            }
        }

        SkyCpuFloat3 DecodeHdrPixel(const std::uint8_t* pixel) noexcept
        {
            if (pixel == nullptr || pixel[3] == 0) return {};
            const float scale = std::ldexp(1.0f, static_cast<int>(pixel[3]) - 128 - 8);
            return { (static_cast<float>(pixel[0]) + 0.5f) * scale,
                (static_cast<float>(pixel[1]) + 0.5f) * scale,
                (static_cast<float>(pixel[2]) + 0.5f) * scale };
        }

        bool ReadHdrLine(const std::vector<std::uint8_t>& bytes, std::size_t& offset,
            std::string& line) noexcept
        {
            try
            {
                if (offset >= bytes.size()) return false;
                const std::size_t start = offset;
                while (offset < bytes.size() && bytes[offset] != '\n') ++offset;
                line.assign(reinterpret_cast<const char*>(bytes.data() + start), offset - start);
                if (offset < bytes.size()) ++offset;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            catch (...)
            {
                line.clear();
                return false;
            }
        }

        bool DecodeHdrPanorama(const std::filesystem::path& path,
            DecodedHdrPanorama& image) noexcept
        {
            image = {};
            if (path.empty()) return false;
            try
            {
                std::ifstream stream(path, std::ios::binary);
                if (!stream) return false;
                std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                    std::istreambuf_iterator<char>());
                std::size_t offset = 0;
                std::string line;
                if (!ReadHdrLine(bytes, offset, line) ||
                    (line.rfind("#?RADIANCE", 0) != 0 && line.rfind("#?RGBE", 0) != 0))
                    return false;

                std::int32_t x_sign = 1;
                std::int32_t y_sign = -1;
                std::uint32_t width = 0;
                std::uint32_t height = 0;
                bool resolution_found = false;
                while (ReadHdrLine(bytes, offset, line))
                {
                    std::istringstream resolution(line);
                    std::string axis;
                    std::int32_t size = 0;
                    while (resolution >> axis >> size)
                    {
                        if (axis.size() != 2 || size <= 0) continue;
                        if (axis[1] == 'X')
                        {
                            x_sign = axis[0] == '-' ? -1 : 1;
                            width = static_cast<std::uint32_t>(size);
                        }
                        else if (axis[1] == 'Y')
                        {
                            y_sign = axis[0] == '-' ? -1 : 1;
                            height = static_cast<std::uint32_t>(size);
                        }
                    }
                    if (width != 0 && height != 0)
                    {
                        resolution_found = true;
                        break;
                    }
                }
                const std::uint64_t pixel_count = static_cast<std::uint64_t>(width) * height;
                if (!resolution_found || width > 16384 || height > 16384 ||
                    pixel_count == 0 || pixel_count > (1ull << 27))
                    return false;
                image.rgb.assign(static_cast<std::size_t>(pixel_count) * 3u, 0.0f);

                const auto store_pixel = [&](std::uint32_t file_x, std::uint32_t file_y,
                    const std::uint8_t* rgbe) noexcept
                {
                    const std::uint32_t x = x_sign > 0 ? file_x : width - file_x - 1;
                    const std::uint32_t y = y_sign < 0 ? file_y : height - file_y - 1;
                    const SkyCpuFloat3 color = DecodeHdrPixel(rgbe);
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * width + x) * 3u;
                    image.rgb[index + 0] = color.x;
                    image.rgb[index + 1] = color.y;
                    image.rgb[index + 2] = color.z;
                };

                for (std::uint32_t file_y = 0; file_y < height; ++file_y)
                {
                    if (offset + 4 > bytes.size()) return false;
                    const std::uint8_t first[4] =
                        { bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3] };
                    offset += 4;
                    const bool rle = width >= 8 && width <= 32767 && first[0] == 2 &&
                        first[1] == 2 && (first[2] & 0x80u) == 0 &&
                        ((static_cast<std::uint32_t>(first[2]) << 8) | first[3]) == width;
                    if (!rle)
                    {
                        store_pixel(0, file_y, first);
                        for (std::uint32_t file_x = 1; file_x < width; ++file_x)
                        {
                            if (offset + 4 > bytes.size()) return false;
                            store_pixel(file_x, file_y, bytes.data() + offset);
                            offset += 4;
                        }
                        continue;
                    }

                    std::array<std::vector<std::uint8_t>, 4> channels;
                    for (auto& channel : channels) channel.resize(width);
                    for (std::uint32_t channel_index = 0; channel_index < 4; ++channel_index)
                    {
                        std::uint32_t file_x = 0;
                        while (file_x < width)
                        {
                            if (offset >= bytes.size()) return false;
                            const std::uint8_t count = bytes[offset++];
                            if (count > 128)
                            {
                                const std::uint32_t run = count - 128u;
                                if (run == 0 || file_x + run > width || offset >= bytes.size())
                                    return false;
                                std::fill_n(channels[channel_index].begin() + file_x, run,
                                    bytes[offset++]);
                                file_x += run;
                            }
                            else
                            {
                                const std::uint32_t literal = count;
                                if (literal == 0 || file_x + literal > width ||
                                    offset + literal > bytes.size())
                                    return false;
                                std::copy_n(bytes.begin() + offset, literal,
                                    channels[channel_index].begin() + file_x);
                                offset += literal;
                                file_x += literal;
                            }
                        }
                    }
                    std::uint8_t pixel[4]{};
                    for (std::uint32_t file_x = 0; file_x < width; ++file_x)
                    {
                        for (std::uint32_t channel_index = 0; channel_index < 4; ++channel_index)
                            pixel[channel_index] = channels[channel_index][file_x];
                        store_pixel(file_x, file_y, pixel);
                    }
                }
                image.width = width;
                image.height = height;
            }
            catch (...)
            {
                image = {};
            }
            return image.width != 0 && image.height != 0 && !image.rgb.empty();
        }

        SkyCpuFloat3 SampleHdrPanorama(const DecodedHdrPanorama& image,
            SkyCpuFloat3 direction) noexcept
        {
            constexpr float kPi = 3.14159265358979323846f;
            direction = SkyNormalize(direction);
            float u = std::atan2(direction.z, direction.x) / (2.0f * kPi) + 0.5f;
            u -= std::floor(u);
            const float v = 0.5f - std::asin(SkyClamp(direction.y, -1.0f, 1.0f)) / kPi;
            const float x = u * image.width - 0.5f;
            const float y = v * image.height - 0.5f;
            const int x0 = static_cast<int>(std::floor(x));
            const int y0 = static_cast<int>(std::floor(y));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;
            const float fx = x - std::floor(x);
            const float fy = y - std::floor(y);
            const auto read = [&image](int px, int py) noexcept
            {
                const int wrapped_x = ((px % static_cast<int>(image.width)) +
                    static_cast<int>(image.width)) % static_cast<int>(image.width);
                const int clamped_y = (std::max)(0, (std::min)(static_cast<int>(image.height) - 1,
                    py));
                const std::size_t index =
                    (static_cast<std::size_t>(clamped_y) * image.width + wrapped_x) * 3u;
                return SkyCpuFloat3{ image.rgb[index], image.rgb[index + 1],
                    image.rgb[index + 2] };
            };
            const SkyCpuFloat3 top = SkyAdd(SkyMultiply(read(x0, y0), 1.0f - fx),
                SkyMultiply(read(x1, y0), fx));
            const SkyCpuFloat3 bottom = SkyAdd(SkyMultiply(read(x0, y1), 1.0f - fx),
                SkyMultiply(read(x1, y1), fx));
            return SkyAdd(SkyMultiply(top, 1.0f - fy), SkyMultiply(bottom, fy));
        }

        bool AppendSkyHalfRgba(std::vector<std::uint8_t>& bytes,
            SkyCpuFloat3 color) noexcept
        {
            try
            {
                const std::uint16_t channels[4] =
                    { FloatToSkyHalf(color.x), FloatToSkyHalf(color.y),
                        FloatToSkyHalf(color.z), FloatToSkyHalf(1.0f) };
                const std::size_t offset = bytes.size();
                bytes.resize(offset + sizeof(channels));
                std::memcpy(bytes.data() + offset, channels, sizeof(channels));
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool BuildCubeFromPanorama(const DecodedHdrPanorama& panorama,
            DecodedDdsImage& cube) noexcept
        {
            cube = {};
            if (panorama.width == 0 || panorama.height == 0 || panorama.rgb.empty())
                return false;
            try
            {
                const std::uint32_t face_size = (std::max)(1u,
                    (std::min)(1024u, (std::min)(panorama.width / 4u,
                        panorama.height / 2u)));
                std::uint16_t mip_levels = 1;
                std::uint32_t mip_size = face_size;
                while (mip_size > 1 && mip_levels < 15)
                {
                    mip_size = (std::max)(1u, mip_size >> 1);
                    ++mip_levels;
                }
                for (std::uint32_t face = 0; face < 6; ++face)
                {
                    for (std::uint32_t mip = 0; mip < mip_levels; ++mip)
                    {
                        const std::uint32_t size = (std::max)(1u, face_size >> mip);
                        for (std::uint32_t y = 0; y < size; ++y)
                        {
                            const float v = ((static_cast<float>(y) + 0.5f) / size) *
                                2.0f - 1.0f;
                            for (std::uint32_t x = 0; x < size; ++x)
                            {
                                const float u = ((static_cast<float>(x) + 0.5f) / size) *
                                    2.0f - 1.0f;
                                if (!AppendSkyHalfRgba(cube.bytes,
                                    SampleHdrPanorama(panorama, SkyFaceDirection(face, u, v))))
                                {
                                    cube = {};
                                    return false;
                                }
                            }
                        }
                    }
                }
                cube.width = face_size;
                cube.height = face_size;
                cube.mip_levels = mip_levels;
                cube.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                cube.is_cube = true;
                cube.array_size = 6;
                if (!BuildDdsSubresources(cube)) cube = {};
            }
            catch (...)
            {
                cube = {};
            }
            return cube.width != 0 && cube.height != 0 && !cube.subresources.empty();
        }

        bool CopyCubeMip0ToFloat(const DecodedDdsImage& cube,
            std::vector<float>& rgba, std::uint32_t& copied_width) noexcept
        {
            rgba.clear();
            copied_width = 0;
            if (!cube.is_cube || cube.array_size != 6 || cube.width == 0 ||
                cube.width != cube.height || cube.mip_levels == 0 ||
                cube.subresources.size() < static_cast<std::size_t>(cube.mip_levels) * 6u)
                return false;
            const bool half = cube.format == DXGI_FORMAT_R16G16B16A16_FLOAT;
            const bool rgba8 = cube.format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                cube.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            const bool bgra8 = cube.format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                cube.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            if (!half && !rgba8 && !bgra8) return false;
            try
            {
                constexpr std::uint32_t kIblSourceMaxSize = 128;
                std::uint32_t source_mip = 0;
                std::uint32_t source_width = cube.width;
                while (source_width > kIblSourceMaxSize && source_mip + 1u < cube.mip_levels)
                {
                    ++source_mip;
                    source_width = (std::max)(1u, cube.width >> source_mip);
                }
                const std::size_t face_texel_count =
                    static_cast<std::size_t>(source_width) * source_width;
                rgba.resize(face_texel_count * 6u * 4u, 0.0f);
                const std::uint32_t bytes_per_pixel = half ? 8u : 4u;
                for (std::uint32_t face = 0; face < 6; ++face)
                {
                    const std::size_t subresource_index =
                        static_cast<std::size_t>(face) * cube.mip_levels + source_mip;
                    const D3D12TextureSubresourceSource& source =
                        cube.subresources[subresource_index];
                    if (source.data == nullptr || source.row_pitch <
                        static_cast<std::uint64_t>(source_width) * bytes_per_pixel)
                    {
                        rgba.clear();
                        return false;
                    }
                    for (std::uint32_t y = 0; y < source_width; ++y)
                    {
                        const auto* row = static_cast<const std::uint8_t*>(source.data) +
                            static_cast<std::size_t>(source.row_pitch) * y;
                        for (std::uint32_t x = 0; x < source_width; ++x)
                        {
                            const auto* pixel = row + static_cast<std::size_t>(x) * bytes_per_pixel;
                            SkyCpuFloat3 color{};
                            if (half)
                            {
                                std::uint16_t channels[4]{};
                                std::memcpy(channels, pixel, sizeof(channels));
                                color = { SkyHalfToFloat(channels[0]), SkyHalfToFloat(channels[1]),
                                    SkyHalfToFloat(channels[2]) };
                            }
                            else if (rgba8)
                            {
                                color = { pixel[0] / 255.0f, pixel[1] / 255.0f,
                                    pixel[2] / 255.0f };
                                if (cube.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
                                    color = { SkySrgbToLinear(color.x), SkySrgbToLinear(color.y),
                                        SkySrgbToLinear(color.z) };
                            }
                            else
                            {
                                color = { pixel[2] / 255.0f, pixel[1] / 255.0f,
                                    pixel[0] / 255.0f };
                                if (cube.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                                    color = { SkySrgbToLinear(color.x), SkySrgbToLinear(color.y),
                                        SkySrgbToLinear(color.z) };
                            }
                            const std::size_t index =
                                (static_cast<std::size_t>(face) * face_texel_count +
                                    static_cast<std::size_t>(y) * source_width + x) * 4u;
                            rgba[index + 0] = color.x;
                            rgba[index + 1] = color.y;
                            rgba[index + 2] = color.z;
                            rgba[index + 3] = 1.0f;
                        }
                    }
                }
                copied_width = source_width;
            }
            catch (...)
            {
                rgba.clear();
                copied_width = 0;
                return false;
            }
            return true;
        }

        SkyCpuFloat3 SampleCpuCube(const std::vector<float>& rgba,
            std::uint32_t width, SkyCpuFloat3 direction) noexcept
        {
            if (width == 0 || rgba.size() < static_cast<std::size_t>(width) * width * 24u)
                return {};
            direction = SkyNormalize(direction);
            const SkyCpuFloat3 absolute =
                { std::abs(direction.x), std::abs(direction.y), std::abs(direction.z) };
            std::uint32_t face = 0;
            float u = 0.0f;
            float v = 0.0f;
            float major = absolute.x;
            if (absolute.x >= absolute.y && absolute.x >= absolute.z)
            {
                if (direction.x >= 0.0f)
                {
                    face = 0;
                    u = -direction.z / major;
                }
                else
                {
                    face = 1;
                    u = direction.z / major;
                }
                v = -direction.y / major;
            }
            else if (absolute.y >= absolute.z)
            {
                major = absolute.y;
                if (direction.y >= 0.0f)
                {
                    face = 2;
                    u = direction.x / major;
                    v = direction.z / major;
                }
                else
                {
                    face = 3;
                    u = direction.x / major;
                    v = -direction.z / major;
                }
            }
            else
            {
                major = absolute.z;
                if (direction.z >= 0.0f)
                {
                    face = 4;
                    u = direction.x / major;
                }
                else
                {
                    face = 5;
                    u = -direction.x / major;
                }
                v = -direction.y / major;
            }
            const float x = SkyClamp((u * 0.5f + 0.5f) * width - 0.5f, 0.0f,
                static_cast<float>(width - 1));
            const float y = SkyClamp((v * 0.5f + 0.5f) * width - 0.5f, 0.0f,
                static_cast<float>(width - 1));
            const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(x));
            const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(y));
            const std::uint32_t x1 = (std::min)(width - 1, x0 + 1);
            const std::uint32_t y1 = (std::min)(width - 1, y0 + 1);
            const float fx = x - x0;
            const float fy = y - y0;
            const std::size_t face_offset = static_cast<std::size_t>(face) * width * width * 4u;
            const auto read = [&](std::uint32_t px, std::uint32_t py) noexcept
            {
                const std::size_t index = face_offset +
                    (static_cast<std::size_t>(py) * width + px) * 4u;
                return SkyCpuFloat3{ rgba[index], rgba[index + 1], rgba[index + 2] };
            };
            const SkyCpuFloat3 top = SkyAdd(SkyMultiply(read(x0, y0), 1.0f - fx),
                SkyMultiply(read(x1, y0), fx));
            const SkyCpuFloat3 bottom = SkyAdd(SkyMultiply(read(x0, y1), 1.0f - fx),
                SkyMultiply(read(x1, y1), fx));
            return SkyAdd(SkyMultiply(top, 1.0f - fy), SkyMultiply(bottom, fy));
        }

        struct SkyCacheHeader final
        {
            std::uint32_t magic = 0;
            std::uint32_t version = 0;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint32_t mip_levels = 0;
            std::uint32_t format = 0;
            std::uint32_t flags = 0;
            std::uint64_t payload_size = 0;
        };

        constexpr std::uint32_t kSkyCacheMagic = 0x31425953u;
        constexpr std::uint32_t kSkyCacheVersion = 1u;

        std::filesystem::path SkyCachePath(const std::filesystem::path& source,
            const char* suffix) noexcept
        {
            try
            {
                std::error_code error;
                const std::filesystem::path absolute =
                    std::filesystem::absolute(source, error);
                const std::filesystem::path fingerprint_path = error ? source : absolute;
                std::string fingerprint = fingerprint_path.generic_string();
                const std::uintmax_t file_size = std::filesystem::file_size(source, error);
                if (!error) fingerprint += ":" + std::to_string(file_size);
                error.clear();
                const auto write_time = std::filesystem::last_write_time(source, error);
                if (!error) fingerprint += ":" +
                    std::to_string(write_time.time_since_epoch().count());
                std::uint64_t hash = 1469598103934665603ull;
                for (const unsigned char value : fingerprint)
                {
                    hash ^= value;
                    hash *= 1099511628211ull;
                }
                std::ostringstream name;
                name << std::hex << std::setw(16) << std::setfill('0') << hash << suffix;
                const std::filesystem::path directory =
                    std::filesystem::current_path(error) / "Saved" / "SkyCache";
                return directory / name.str();
            }
            catch (...)
            {
                return {};
            }
        }

        void PruneSkyCache(const std::filesystem::path& directory,
            const std::unordered_set<std::string>& protected_stems) noexcept
        {
            constexpr std::size_t kMaxSkyCacheGroups = 128;
            struct CacheGroup final
            {
                std::string stem;
                std::filesystem::file_time_type newest{};
                bool has_time = false;
            };
            try
            {
                if (directory.empty()) return;
                std::error_code error;
                if (!std::filesystem::is_directory(directory, error) || error) return;
                std::vector<CacheGroup> groups;
                std::unordered_map<std::string, std::size_t> group_indices;
                std::filesystem::directory_iterator iterator(directory, error);
                if (error) return;
                const std::filesystem::directory_iterator end;
                for (; iterator != end && !error; iterator.increment(error))
                {
                    std::error_code file_error;
                    if (!iterator->is_regular_file(file_error) || file_error) continue;
                    const std::string extension = iterator->path().extension().string();
                    if (extension != ".cube" && extension != ".iem" && extension != ".pmrem")
                        continue;
                    const std::string stem = iterator->path().stem().string();
                    const auto found = group_indices.find(stem);
                    std::size_t group_index = 0;
                    if (found == group_indices.end())
                    {
                        group_index = groups.size();
                        group_indices.emplace(stem, group_index);
                        groups.push_back({ stem, {}, false });
                    }
                    else
                    {
                        group_index = found->second;
                    }
                    std::error_code time_error;
                    const auto write_time = std::filesystem::last_write_time(
                        iterator->path(), time_error);
                    if (!time_error && (!groups[group_index].has_time ||
                        write_time > groups[group_index].newest))
                    {
                        groups[group_index].newest = write_time;
                        groups[group_index].has_time = true;
                    }
                }
                if (groups.size() <= kMaxSkyCacheGroups) return;
                std::sort(groups.begin(), groups.end(),
                    [](const CacheGroup& left, const CacheGroup& right) noexcept
                    {
                        if (left.has_time != right.has_time) return !left.has_time;
                        if (left.has_time && left.newest != right.newest)
                            return left.newest < right.newest;
                        return left.stem < right.stem;
                    });
                std::size_t remove_groups = groups.size() - kMaxSkyCacheGroups;
                for (const CacheGroup& group : groups)
                {
                    if (remove_groups == 0) break;
                    if (protected_stems.find(group.stem) != protected_stems.end()) continue;
                    for (const char* suffix : { ".cube", ".iem", ".pmrem" })
                    {
                        std::error_code remove_error;
                        std::filesystem::remove(directory / (group.stem + suffix), remove_error);
                    }
                    --remove_groups;
                }
            }
            catch (...)
            {
            }
        }

        bool WriteSkyCubeCache(const std::filesystem::path& path,
            const DecodedDdsImage& cube) noexcept
        {
            if (path.empty() || !cube.is_cube || cube.array_size != 6 || cube.width == 0 ||
                cube.height != cube.width || cube.mip_levels == 0 || cube.bytes.empty())
                return false;
            try
            {
                std::error_code error;
                std::filesystem::create_directories(path.parent_path(), error);
                if (error) return false;
                std::ofstream stream(path, std::ios::binary | std::ios::trunc);
                if (!stream) return false;
                const SkyCacheHeader header{ kSkyCacheMagic, kSkyCacheVersion, cube.width,
                    cube.height, cube.mip_levels, static_cast<std::uint32_t>(cube.format), 1u,
                    static_cast<std::uint64_t>(cube.bytes.size()) };
                stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
                stream.write(reinterpret_cast<const char*>(cube.bytes.data()),
                    static_cast<std::streamsize>(cube.bytes.size()));
                return static_cast<bool>(stream);
            }
            catch (...)
            {
                return false;
            }
        }

        bool ReadSkyCubeCache(const std::filesystem::path& path,
            DecodedDdsImage& cube) noexcept
        {
            cube = {};
            if (path.empty()) return false;
            try
            {
                std::ifstream stream(path, std::ios::binary);
                if (!stream) return false;
                SkyCacheHeader header{};
                stream.read(reinterpret_cast<char*>(&header), sizeof(header));
                if (!stream || header.magic != kSkyCacheMagic ||
                    header.version != kSkyCacheVersion || (header.flags & 1u) == 0 ||
                    header.width == 0 || header.height != header.width ||
                    header.width > 16384 || header.mip_levels == 0 || header.mip_levels > 15 ||
                    header.payload_size == 0 || header.payload_size > (1ull << 34) ||
                    !IsSupportedStaticDdsFormat(static_cast<DXGI_FORMAT>(header.format)))
                    return false;
                cube.bytes.resize(static_cast<std::size_t>(header.payload_size));
                stream.read(reinterpret_cast<char*>(cube.bytes.data()),
                    static_cast<std::streamsize>(cube.bytes.size()));
                if (!stream) { cube = {}; return false; }
                cube.width = header.width;
                cube.height = header.height;
                cube.mip_levels = static_cast<std::uint16_t>(header.mip_levels);
                cube.format = static_cast<DXGI_FORMAT>(header.format);
                cube.is_cube = true;
                cube.array_size = 6;
                if (!BuildDdsSubresources(cube)) cube = {};
            }
            catch (...)
            {
                cube = {};
            }
            return cube.width != 0 && !cube.subresources.empty();
        }

        bool DecodeHdrPanoramaToCube(const std::filesystem::path& path,
            DecodedDdsImage& cube) noexcept
        {
            cube = {};
            if (ReadSkyCubeCache(SkyCachePath(path, ".cube"), cube)) return true;
            DecodedHdrPanorama panorama;
            if (!DecodeHdrPanorama(path, panorama) || !BuildCubeFromPanorama(panorama, cube))
                return false;
            WriteSkyCubeCache(SkyCachePath(path, ".cube"), cube);
            return true;
        }

        bool InitializeGeneratedCube(DecodedDdsImage& cube, std::uint32_t size,
            std::uint16_t mip_levels) noexcept
        {
            cube = {};
            if (size == 0 || mip_levels == 0 || mip_levels > 15) return false;
            cube.width = size;
            cube.height = size;
            cube.mip_levels = mip_levels;
            cube.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            cube.is_cube = true;
            cube.array_size = 6;
            return true;
        }

        bool BuildIblCubes(const std::vector<float>& source, std::uint32_t source_width,
            DecodedDdsImage& diffuse, DecodedDdsImage& specular) noexcept
        {
            diffuse = {};
            specular = {};
            if (source_width == 0 || source.size() <
                static_cast<std::size_t>(source_width) * source_width * 24u)
                return false;
            try
            {
                constexpr float kPi = 3.14159265358979323846f;
                constexpr std::uint32_t kDiffuseSize = 32;
                constexpr std::uint32_t kDiffuseSamples = 64;
                constexpr std::uint32_t kSpecularSamples = 128;
                const std::uint32_t specular_size = (std::min)(128u, source_width);
                std::uint16_t specular_mips = 1;
                std::uint32_t size = specular_size;
                while (size > 1 && specular_mips < 15)
                {
                    size = (std::max)(1u, size >> 1);
                    ++specular_mips;
                }
                if (!InitializeGeneratedCube(diffuse, kDiffuseSize, 1) ||
                    !InitializeGeneratedCube(specular, specular_size, specular_mips))
                    return false;

                for (std::uint32_t face = 0; face < 6; ++face)
                {
                    for (std::uint32_t y = 0; y < kDiffuseSize; ++y)
                    {
                        const float v = ((static_cast<float>(y) + 0.5f) / kDiffuseSize) *
                            2.0f - 1.0f;
                        for (std::uint32_t x = 0; x < kDiffuseSize; ++x)
                        {
                            const float u = ((static_cast<float>(x) + 0.5f) / kDiffuseSize) *
                                2.0f - 1.0f;
                            const SkyCpuFloat3 normal = SkyFaceDirection(face, u, v);
                            const SkyCpuFloat3 up = std::abs(normal.y) < 0.999f ?
                                SkyCpuFloat3{ 0.0f, 1.0f, 0.0f } :
                                SkyCpuFloat3{ 1.0f, 0.0f, 0.0f };
                            const SkyCpuFloat3 tangent = SkyNormalize(SkyCross(up, normal));
                            const SkyCpuFloat3 bitangent = SkyCross(normal, tangent);
                            SkyCpuFloat3 irradiance{};
                            for (std::uint32_t sample_index = 0;
                                sample_index < kDiffuseSamples; ++sample_index)
                            {
                                const float xi1 = (static_cast<float>(sample_index) + 0.5f) /
                                    kDiffuseSamples;
                                const float xi2 = std::fmod(
                                    static_cast<float>(sample_index) * 0.61803398875f + 0.5f,
                                    1.0f);
                                const float phi = 2.0f * kPi * xi2;
                                const float cos_theta = std::sqrt(1.0f - xi1);
                                const float sin_theta = std::sqrt(xi1);
                                const SkyCpuFloat3 local =
                                    { std::cos(phi) * sin_theta,
                                        std::sin(phi) * sin_theta, cos_theta };
                                const SkyCpuFloat3 sample_direction = SkyNormalize(SkyAdd(
                                    SkyAdd(SkyMultiply(tangent, local.x),
                                        SkyMultiply(bitangent, local.y)),
                                    SkyMultiply(normal, local.z)));
                                irradiance = SkyAdd(irradiance,
                                    SampleCpuCube(source, source_width, sample_direction));
                            }
                            irradiance = SkyMultiply(irradiance, kPi / kDiffuseSamples);
                            if (!AppendSkyHalfRgba(diffuse.bytes, irradiance)) return false;
                        }
                    }
                }

                for (std::uint32_t face = 0; face < 6; ++face)
                {
                    for (std::uint32_t mip = 0; mip < specular_mips; ++mip)
                    {
                        const std::uint32_t mip_size = (std::max)(1u, specular_size >> mip);
                        const float roughness = specular_mips > 1 ?
                            static_cast<float>(mip) / (specular_mips - 1u) : 0.0f;
                        for (std::uint32_t y = 0; y < mip_size; ++y)
                        {
                            const float v = ((static_cast<float>(y) + 0.5f) / mip_size) *
                                2.0f - 1.0f;
                            for (std::uint32_t x = 0; x < mip_size; ++x)
                            {
                                const float u = ((static_cast<float>(x) + 0.5f) / mip_size) *
                                    2.0f - 1.0f;
                                const SkyCpuFloat3 normal = SkyFaceDirection(face, u, v);
                                SkyCpuFloat3 filtered{};
                                float total_weight = 0.0f;
                                if (roughness < 0.001f)
                                {
                                    filtered = SampleCpuCube(source, source_width, normal);
                                    total_weight = 1.0f;
                                }
                                else
                                {
                                    const SkyCpuFloat3 view = normal;
                                    const SkyCpuFloat3 up = std::abs(normal.y) < 0.999f ?
                                        SkyCpuFloat3{ 0.0f, 1.0f, 0.0f } :
                                        SkyCpuFloat3{ 1.0f, 0.0f, 0.0f };
                                    const SkyCpuFloat3 tangent =
                                        SkyNormalize(SkyCross(up, normal));
                                    const SkyCpuFloat3 bitangent = SkyCross(normal, tangent);
                                    const float alpha = (std::max)(roughness * roughness,
                                        0.0025f);
                                    const float alpha_squared = alpha * alpha;
                                    for (std::uint32_t sample_index = 0;
                                        sample_index < kSpecularSamples; ++sample_index)
                                    {
                                        const float xi1 = (static_cast<float>(sample_index) +
                                            0.5f) / kSpecularSamples;
                                        const float xi2 = std::fmod(
                                            static_cast<float>(sample_index) * 0.7548776662f +
                                            0.5f, 1.0f);
                                        const float phi = 2.0f * kPi * xi2;
                                        const float cos_theta = std::sqrt((1.0f - xi1) /
                                            (1.0f + (alpha_squared - 1.0f) * xi1));
                                        const float sin_theta = std::sqrt(
                                            (std::max)(0.0f, 1.0f - cos_theta * cos_theta));
                                        const SkyCpuFloat3 local =
                                            { std::cos(phi) * sin_theta,
                                                std::sin(phi) * sin_theta, cos_theta };
                                        const SkyCpuFloat3 half_vector = SkyNormalize(SkyAdd(
                                            SkyAdd(SkyMultiply(tangent, local.x),
                                                SkyMultiply(bitangent, local.y)),
                                            SkyMultiply(normal, local.z)));
                                        const SkyCpuFloat3 light = SkyNormalize(SkyAdd(
                                            SkyMultiply(half_vector, 2.0f *
                                                SkyDot(view, half_vector)),
                                            SkyMultiply(view, -1.0f)));
                                        const float no_light = (std::max)(0.0f,
                                            SkyDot(normal, light));
                                        if (no_light <= 0.0f) continue;
                                        filtered = SkyAdd(filtered, SkyMultiply(
                                            SampleCpuCube(source, source_width, light), no_light));
                                        total_weight += no_light;
                                    }
                                }
                                if (total_weight > 0.0f)
                                    filtered = SkyMultiply(filtered, 1.0f / total_weight);
                                if (!AppendSkyHalfRgba(specular.bytes, filtered)) return false;
                            }
                        }
                    }
                }
                if (!BuildDdsSubresources(diffuse) || !BuildDdsSubresources(specular))
                {
                    diffuse = {};
                    specular = {};
                    return false;
                }
            }
            catch (...)
            {
                diffuse = {};
                specular = {};
                return false;
            }
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
        bool create_validation_resources, bool enable_gpu_validation,
        bool enable_dred) noexcept
    {
        last_initialization_stage_[0] = '\0';
        last_initialization_result_ = S_OK;
        if (window == nullptr || !IsValidSize(width, height))
        {
            SetInitializationFailure("invalid-window-or-size", E_INVALIDARG);
            return false;
        }
        Shutdown();
        validation_resources_enabled_ = create_validation_resources;
        if (!CreateDevice(enable_debug_layer || enable_gpu_validation, force_warp,
            enable_gpu_validation, enable_dred))
        {
            SetInitializationFailure("CreateDevice", E_FAIL);
            Shutdown();
            return false;
        }
        if (!CreateSwapChain(window, width, height))
        {
            SetInitializationFailure("CreateSwapChain", E_FAIL);
            Shutdown();
            return false;
        }
        if (!CreateRenderTargets())
        {
            SetInitializationFailure("CreateRenderTargets", E_FAIL);
            Shutdown();
            return false;
        }
        if (!CreateStaticRendererResources())
        {
            SetInitializationFailure("CreateStaticRendererResources", E_FAIL);
            Shutdown();
            return false;
        }
        if (!CreateScene3DRendererResources())
        {
            SetInitializationFailure("CreateScene3DRendererResources", E_FAIL);
            Shutdown();
            return false;
        }
        if (!CreateUIRendererResources() || !CreateUIEffectResources())
        {
            SetInitializationFailure("CreateUIRendererResources", E_FAIL);
            Shutdown();
            return false;
        }
        if (validation_resources_enabled_ && !CreateValidationTriangleResources())
        {
            SetInitializationFailure("CreateValidationTriangleResources", E_FAIL);
            Shutdown();
            return false;
        }
        std::snprintf(last_initialization_stage_, sizeof(last_initialization_stage_),
            "%s", "success");
        return true;
    }

    void D3D12DeviceContext::Shutdown() noexcept
    {
        if (device_ != nullptr) (void)WaitForGpu();

        for (auto& batch : render_item_batches_)
            batch.Reset(&resource_descriptor_allocator_);
#ifdef USE_IMGUI
        ReleaseImGuiRendererResources();
#endif
        ReleaseUIRendererResources();
        ReleaseValidationTriangleResources();
        ReleaseScene3DRendererResources();
        ReleaseStaticRendererResources();
        ReleaseBackBufferCapture();
        ReleaseRenderTargets();

        upload_context_.Shutdown();
        for (auto& frame : frame_resources_) frame.Shutdown();
        diagnostics_.DrainInfoQueue();
        diagnostics_.PrepareLiveObjectReport();

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

        last_shutdown_live_object_lines_ = 0;
        last_shutdown_live_object_detail_lines_ = 0;
        last_shutdown_live_object_report_ok_ = diagnostics_.ReportLiveObjects(
            last_shutdown_live_object_lines_, last_shutdown_live_object_detail_lines_);
        diagnostics_.Shutdown();
        device_.Reset();
        adapter_.Reset();
        factory_.Reset();

        next_fence_value_ = 1;
        last_signaled_fence_value_ = 0;
        frame_upload_peak_ = 0;
        fence_wait_count_ = 0;
        fence_wait_nanoseconds_ = 0;
        pso_cache_hits_ = 0;
        pso_cache_misses_ = 0;
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

    bool D3D12DeviceContext::ConfigureDebug(bool enable_debug_layer,
        bool enable_gpu_validation, bool enable_dred) noexcept
    {
        debug_layer_enabled_ = false;
        gpu_validation_enabled_ = false;
        dred_enabled_ = false;

        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if (enable_debug_layer &&
            SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))) && debug)
        {
            debug->EnableDebugLayer();
            debug_layer_enabled_ = true;

            Microsoft::WRL::ComPtr<ID3D12Debug1> debug1;
            if (enable_gpu_validation && SUCCEEDED(debug.As(&debug1)) && debug1)
            {
                debug1->SetEnableGPUBasedValidation(TRUE);
                debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
                gpu_validation_enabled_ = true;
            }
        }

        if (enable_dred)
        {
            Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))) && dred)
            {
                dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred_enabled_ = true;
            }
        }
        return true;
    }

    bool D3D12DeviceContext::CreateDevice(bool enable_debug_layer,
        bool force_warp, bool enable_gpu_validation, bool enable_dred) noexcept
    {
        if (!ConfigureDebug(enable_debug_layer, enable_gpu_validation, enable_dred))
            return false;
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
        SetD3D12ObjectName(device_.Get(), L"Device", L"Primary");

        if (debug_layer_enabled_)
        {
            Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue;
            if (SUCCEEDED(device_.As(&info_queue)) && info_queue)
            {
                // IDEのデバッガ接続時だけ即時停止し、CLI検証ではメッセージを収集して
                // 最後まで検証結果を返す。デバッガ未接続で停止すると、実機検証の
                // エラー内容と終了時のLive Objectを回収できない。
                const BOOL break_on_error = ::IsDebuggerPresent() ? TRUE : FALSE;
                info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION,
                    break_on_error);
                info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,
                    break_on_error);
            }
        }

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (FAILED(device_->CreateCommandQueue(&queue_desc,
            IID_PPV_ARGS(&command_queue_))))
            return false;
        SetD3D12ObjectName(command_queue_.Get(), L"CommandQueue", L"Direct");

        if (!upload_context_.Initialize(device_.Get(), command_queue_.Get()) ||
            !resource_descriptor_allocator_.Initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096, true) ||
            !sampler_descriptor_allocator_.Initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 128, true))
            return false;

        for (std::uint32_t slot = 0; slot < FrameCount; ++slot)
        {
            auto& frame = frame_resources_[slot];
            if (!frame.Initialize(device_.Get(), FrameUploadCapacity)) return false;
            SetD3D12ObjectName(frame.command_allocator.Get(), L"Frame.CommandAllocator", L"Direct", slot);
        }
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            frame_resources_[0].command_allocator.Get(), nullptr,
            IID_PPV_ARGS(&command_list_))))
            return false;
        SetD3D12ObjectName(command_list_.Get(), L"CommandList", L"Frame");
        if (FAILED(command_list_->Close())) return false;
        if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&fence_))))
            return false;
        SetD3D12ObjectName(fence_.Get(), L"Fence", L"Frame");
        if (!diagnostics_.Initialize(device_.Get(), command_queue_.Get())) return false;
        resource_state_tracker_.SetBarrierCallback([this](ID3D12Resource* resource,
            D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            diagnostics_.RecordBarrier(resource, before, after);
        });
        resource_state_tracker_.SetValidationCallback([this](const std::string& message)
        {
            diagnostics_.PushMessage(D3D12_MESSAGE_SEVERITY_ERROR, message);
        });
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
        const HRESULT create_result = factory_->CreateSwapChainForHwnd(
            command_queue_.Get(), window, &description, nullptr, nullptr, &swap_chain);
        if (FAILED(create_result))
        {
            SetInitializationFailure("CreateSwapChainForHwnd", create_result);
            return false;
        }
        const HRESULT association_result = factory_->MakeWindowAssociation(
            window, DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(association_result))
        {
            SetInitializationFailure("MakeWindowAssociation", association_result);
            return false;
        }
        const HRESULT query_result = swap_chain.As(&swap_chain_);
        if (FAILED(query_result))
        {
            SetInitializationFailure("QuerySwapChain4", query_result);
            return false;
        }
        frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
        width_ = width;
        height_ = height;
        return true;
    }

    bool D3D12DeviceContext::CreateRenderTargets() noexcept
    {
        // Scene/Game View、Deferred Lit、UI Effect、Scene Effect、UI Preview、GBuffer、Temporal 履歴が
        // それぞれ RTV と DSV を 1 枚ずつ取るので、内訳ぶんの余裕を持たせる。
        if (!rtv_allocator_.Initialize(device_.Get(),
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount + 64u, false) ||
            !rtv_allocator_.Allocate(FrameCount, rtv_allocation_) ||
            !dsv_allocator_.Initialize(device_.Get(),
                D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 128, false) ||
            !dsv_allocator_.Allocate(1, dsv_allocation_))
            return false;

        for (std::uint32_t index = 0; index < FrameCount; ++index)
        {
            if (FAILED(swap_chain_->GetBuffer(index,
                IID_PPV_ARGS(&render_targets_[index]))))
                return false;
            device_->CreateRenderTargetView(render_targets_[index].Get(), nullptr,
                rtv_allocator_.CpuHandle(rtv_allocation_.index + index));
            SetD3D12ObjectName(render_targets_[index].Get(), L"SwapChain.BackBuffer", L"Color", index);
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
        SetD3D12ObjectName(depth_stencil_buffer_.Get(), L"Scene.Depth", L"Main");
        device_->CreateDepthStencilView(depth_stencil_buffer_.Get(), nullptr,
            dsv_allocator_.CpuHandle(dsv_allocation_.index));
        if (!resource_state_tracker_.Track(depth_stencil_buffer_.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE))
            return false;

        if (!CreateOffscreenTarget(scene_view_target_, width_, height_,
                DXGI_FORMAT_R16G16B16A16_FLOAT, L"SceneView") ||
            !CreateOffscreenTarget(scene3d_deferred_target_, width_, height_,
                DXGI_FORMAT_R16G16B16A16_FLOAT, L"SceneDeferredLit") ||
            !CreateOffscreenTarget(game_view_target_, width_, height_,
                DXGI_FORMAT_R16G16B16A16_FLOAT, L"GameView") ||
            !CreateOffscreenTarget(ui_effect_targets_[0], width_, height_,
                DXGI_FORMAT_R8G8B8A8_UNORM, L"UI.EffectRT0") ||
            !CreateOffscreenTarget(ui_effect_targets_[1], width_, height_,
                DXGI_FORMAT_R8G8B8A8_UNORM, L"UI.EffectRT1") ||
            !CreateOffscreenTarget(ui_effect_targets_[2], width_, height_,
                DXGI_FORMAT_R8G8B8A8_UNORM, L"UI.EffectRT2") ||
            !CreateOffscreenTarget(ui_effect_targets_[3], width_, height_,
                DXGI_FORMAT_R8G8B8A8_UNORM, L"UI.BackdropRT") ||
            !CreateOffscreenTarget(scene_effect_targets_[0], width_, height_,
                DXGI_FORMAT_R16G16B16A16_FLOAT, L"SceneEffect.RT0") ||
            !CreateOffscreenTarget(scene_effect_targets_[1], width_, height_,
                DXGI_FORMAT_R16G16B16A16_FLOAT, L"SceneEffect.RT1") ||
            !CreateOffscreenTarget(scene_effect_targets_[2], width_, height_,
                DXGI_FORMAT_R16G16B16A16_FLOAT, L"SceneEffect.RT2") ||
            !CreateOffscreenTarget(scene_effect_targets_[3], width_, height_,
                DXGI_FORMAT_R16G16B16A16_FLOAT, L"SceneEffect.BackdropRT"))
            return false;
        return true;
    }

    bool D3D12DeviceContext::CreateOffscreenTarget(D3D12OffscreenTarget& target,
        std::uint32_t width, std::uint32_t height, DXGI_FORMAT format,
        const wchar_t* debug_name) noexcept
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
        // SceneView/GameViewはHDR、UI Effect RTはSwapChainと同じLDR RGBA8。
        // UIのping-pongは最終合成前のアルファを保持する必要がある。
        color_description.Format = format;
        color_description.SampleDesc.Count = 1;
        color_description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        color_description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE color_clear{};
        color_clear.Format = color_description.Format;
        color_clear.Color[3] = 1.0f;
        if (FAILED(device_->CreateCommittedResource(&default_heap,
            D3D12_HEAP_FLAG_NONE, &color_description,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &color_clear,
            IID_PPV_ARGS(&target.color))))
        {
            ReleaseOffscreenTarget(target);
            return false;
        }
        SetD3D12ObjectName(target.color.Get(), debug_name != nullptr ? debug_name : L"Offscreen", L"Color");
        device_->CreateRenderTargetView(target.color.Get(), nullptr, target.rtv.cpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = color_description.Format;
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
        target.format = format;
        return true;
    }

    bool D3D12DeviceContext::CreateValidationTriangleResources() noexcept
    {
        D3D12ShaderCompiler shader_compiler;
        if (!shader_compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath()))
            return false;
        const std::filesystem::path shader_directory =
            std::filesystem::current_path() / "Shader";
        const D3D12ShaderCompileResult vertex_shader = shader_compiler.CompileFile(
            shader_directory / "dx12_validation_triangle_vs.hlsl", L"main", L"vs_6_0");
        const D3D12ShaderCompileResult pixel_shader = shader_compiler.CompileFile(
            shader_directory / "dx12_validation_triangle_ps.hlsl", L"main", L"ps_6_0");
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
        SetD3D12ObjectName(validation_root_signature_.Get(), L"Validation.RootSignature", L"Triangle");

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
        // 検証描画は DSV を張らずバックバッファへ直接出すため深度を持たない。
        D3D12_DEPTH_STENCIL_DESC depth_stencil_desc{};
        depth_stencil_desc.DepthEnable = FALSE;
        depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
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
        pipeline_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        pipeline_desc.SampleDesc.Count = 1;
        if (FAILED(device_->CreateGraphicsPipelineState(&pipeline_desc,
            IID_PPV_ARGS(&validation_pipeline_))))
            return false;
        SetD3D12ObjectName(validation_pipeline_.Get(), L"Validation.PSO", L"Triangle");

        if (!validation_mesh_.Upload(device_.Get(), upload_context_,
            kValidationVertices, sizeof(kValidationVertices), sizeof(ValidationVertex),
            kValidationIndices, sizeof(kValidationIndices), DXGI_FORMAT_R16_UINT))
            return false;
        validation_mesh_.SetDebugName("validation-triangle");
        return true;
    }


    bool D3D12DeviceContext::CreateStaticRendererResources() noexcept
    {
        D3D12ShaderCompiler shader_compiler;
        if (!shader_compiler.Initialize(D3D12ShaderCompiler::FindDefaultLibraryPath()))
            return false;
        const std::filesystem::path shader_directory =
            std::filesystem::current_path() / "Shader";
        const D3D12ShaderCompileResult vertex_shader = shader_compiler.CompileFile(
            shader_directory / "dx12_static_bridge_vs.hlsl", L"main", L"vs_6_0", debug_layer_enabled_);
        const D3D12ShaderCompileResult pixel_shader = shader_compiler.CompileFile(
            shader_directory / "dx12_static_bridge_ps.hlsl", L"main", L"ps_6_0", debug_layer_enabled_);
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
        SetD3D12ObjectName(static_root_signature_.Get(), L"Static.RootSignature", L"MaterialBridge");

        if (!CreateStaticPipelineSet(pixel_shader.bytecode, static_bridge_pipelines_, "bridge"))
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
        StaticPipelineSet& pipelines, std::string_view debug_key) noexcept
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
                const std::string pso_key = (debug_key.empty() ? std::string("static") :
                    std::string(debug_key)) + ":" + std::to_string(pipeline_index);
                SetD3D12ObjectNameUtf8(pipelines.pipelines[pipeline_index].Get(),
                    L"Static.PSO", pso_key);
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
            if (entry.second.srgb_srv.IsValid())
                resource_descriptor_allocator_.Free(entry.second.srgb_srv);
        }
        texture_cache_.clear();
        sky_cpu_cubes_.clear();
        sky_source_paths_.clear();
        sky_ibl_failures_.clear();
        last_sky_ibl_status_ = D3D12SkyIblStatus::Ready;
        sky_cache_prune_initialized_ = false;
        static_texture_failures_.clear();
        static_mesh_cache_.clear();
        static_mesh_bounds_cache_.clear();
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
        SetD3D12ObjectNameUtf8(texture.resource.Get(), L"Texture.Solid", key);
        if (!resource_descriptor_allocator_.Allocate(1, texture.srv)) return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = ToLinearTextureFormat(texture.format);
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = texture.mip_levels;
        device_->CreateShaderResourceView(texture.resource.Get(), &srv, texture.srv.cpu);
        const DXGI_FORMAT srgb_format = ToSrgbTextureFormat(texture.format);
        if (srgb_format != DXGI_FORMAT_UNKNOWN)
        {
            if (!resource_descriptor_allocator_.Allocate(1, texture.srgb_srv))
            {
                resource_descriptor_allocator_.Free(texture.srv);
                return false;
            }
            srv.Format = srgb_format;
            device_->CreateShaderResourceView(texture.resource.Get(), &srv, texture.srgb_srv.cpu);
        }
        texture.width = 1;
        texture.height = 1;
        try
        {
            auto result = texture_cache_.try_emplace(key);
            if (!result.second)
            {
                resource_descriptor_allocator_.Free(texture.srv);
                if (texture.srgb_srv.IsValid())
                    resource_descriptor_allocator_.Free(texture.srgb_srv);
                return true;
            }
            result.first->second.resource = std::move(texture.resource);
            result.first->second.srv = texture.srv;
            result.first->second.srgb_srv = texture.srgb_srv;
            result.first->second.width = texture.width;
            result.first->second.height = texture.height;
            result.first->second.mip_levels = texture.mip_levels;
            result.first->second.format = texture.format;
            texture.srv = {};
            texture.srgb_srv = {};
        }
        catch (...)
        {
            if (texture.srv.IsValid())
                resource_descriptor_allocator_.Free(texture.srv);
            if (texture.srgb_srv.IsValid())
                resource_descriptor_allocator_.Free(texture.srgb_srv);
            return false;
        }
        return true;
    }

    bool D3D12DeviceContext::CreateStaticCubeTexture(const std::string& key,
        std::uint32_t width, std::uint32_t height, std::uint16_t mip_levels,
        DXGI_FORMAT format, const std::vector<D3D12TextureSubresourceSource>& subresources,
        const std::vector<float>* cpu_rgba, std::uint32_t cpu_width) noexcept
    {
        if (key.empty() || width == 0 || height != width || mip_levels == 0 ||
            format == DXGI_FORMAT_UNKNOWN || subresources.size() !=
            static_cast<std::size_t>(mip_levels) * 6u)
            return false;
        if (texture_cache_.find(key) != texture_cache_.end()) return true;
        const std::uint32_t actual_cpu_width = cpu_width != 0 ? cpu_width : width;
        if (cpu_rgba != nullptr && (actual_cpu_width == 0 || cpu_rgba->size() <
            static_cast<std::size_t>(actual_cpu_width) * actual_cpu_width * 24u))
            return false;

        StaticTextureResource texture;
        texture.is_cube = true;
        const DXGI_FORMAT resource_format = ToLinearTextureFormat(format);
        if (!D3D12ResourceFactory::CreateTextureCube(device_.Get(), upload_context_,
            width, height, mip_levels, resource_format, subresources, texture.resource))
            return false;
        SetD3D12ObjectNameUtf8(texture.resource.Get(), L"Texture.SkyCube", key.c_str());
        if (!resource_descriptor_allocator_.Allocate(1, texture.srv)) return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        const DXGI_FORMAT srgb_format = ToSrgbTextureFormat(format);
        srv.Format = srgb_format != DXGI_FORMAT_UNKNOWN ? srgb_format : resource_format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MostDetailedMip = 0;
        srv.TextureCube.MipLevels = mip_levels;
        srv.TextureCube.ResourceMinLODClamp = 0.0f;
        device_->CreateShaderResourceView(texture.resource.Get(), &srv, texture.srv.cpu);
        texture.width = width;
        texture.height = height;
        texture.mip_levels = mip_levels;
        texture.format = resource_format;

        bool cpu_inserted = false;
        try
        {
            if (cpu_rgba != nullptr)
            {
                SkyCubeCpuData cpu_data;
                cpu_data.width = actual_cpu_width;
                cpu_data.rgba = *cpu_rgba;
                sky_cpu_cubes_.insert_or_assign(key, std::move(cpu_data));
                cpu_inserted = true;
            }
            auto result = texture_cache_.try_emplace(key);
            if (!result.second)
            {
                if (cpu_inserted) sky_cpu_cubes_.erase(key);
                resource_descriptor_allocator_.Free(texture.srv);
                return true;
            }
            result.first->second.resource = std::move(texture.resource);
            result.first->second.srv = texture.srv;
            result.first->second.width = texture.width;
            result.first->second.height = texture.height;
            result.first->second.mip_levels = texture.mip_levels;
            result.first->second.format = texture.format;
            result.first->second.is_cube = true;
            texture.srv = {};
        }
        catch (...)
        {
            if (cpu_inserted) sky_cpu_cubes_.erase(key);
            if (texture.srv.IsValid()) resource_descriptor_allocator_.Free(texture.srv);
            return false;
        }
        return true;
    }

    bool D3D12DeviceContext::CacheMeshLocalBounds(
        const D3D12StaticSceneSubmission& submission,
        bool allow_static_mesh_cache_replacement) noexcept
    {
        try
        {
            for (const D3D12StaticMeshSource& source : submission.mesh_sources)
            {
                if (source.key.empty()) continue;
                if (source.replace_existing && allow_static_mesh_cache_replacement)
                    static_mesh_bounds_cache_.erase(source.key);
                if (static_mesh_bounds_cache_.find(source.key) == static_mesh_bounds_cache_.end())
                {
                    const D3D12MeshLocalBounds bounds = MakeMeshLocalBounds(source.vertices);
                    if (bounds.valid) static_mesh_bounds_cache_.emplace(source.key, bounds);
                }
            }
            for (const D3D12SkinnedMeshSource& source : submission.skinned_mesh_sources)
            {
                if (source.key.empty()) continue;
                if (skinned_mesh_bounds_cache_.find(source.key) == skinned_mesh_bounds_cache_.end())
                {
                    const D3D12MeshLocalBounds bounds = MakeMeshLocalBounds(source.vertices);
                    if (bounds.valid) skinned_mesh_bounds_cache_.emplace(source.key, bounds);
                }
            }
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    bool D3D12DeviceContext::GetStaticMeshLocalBounds(
        const std::string& key, D3D12MeshLocalBounds& bounds) const noexcept
    {
        const auto found = static_mesh_bounds_cache_.find(key);
        if (found == static_mesh_bounds_cache_.end()) return false;
        bounds = found->second;
        return bounds.valid;
    }

    bool D3D12DeviceContext::GetSkinnedMeshLocalBounds(
        const std::string& key, D3D12MeshLocalBounds& bounds) const noexcept
    {
        const auto found = skinned_mesh_bounds_cache_.find(key);
        if (found == skinned_mesh_bounds_cache_.end()) return false;
        bounds = found->second;
        return bounds.valid;
    }

    bool D3D12DeviceContext::CacheSkinnedMeshLocalBounds(
        const D3D12SkinnedMeshSource& source) noexcept
    {
        if (source.key.empty()) return false;
        if (skinned_mesh_bounds_cache_.find(source.key) != skinned_mesh_bounds_cache_.end())
            return true;
        const D3D12MeshLocalBounds bounds = MakeMeshLocalBounds(source.vertices);
        if (!bounds.valid) return false;
        try
        {
            skinned_mesh_bounds_cache_.emplace(source.key, bounds);
        }
        catch (...)
        {
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
        if (static_mesh_bounds_cache_.find(source.key) == static_mesh_bounds_cache_.end())
        {
            const D3D12MeshLocalBounds bounds = MakeMeshLocalBounds(source.vertices);
            if (!bounds.valid) return false;
            try
            {
                static_mesh_bounds_cache_.emplace(source.key, bounds);
            }
            catch (...)
            {
                return false;
            }
        }
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
        mesh->SetDebugName(source.key);
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
        if (source.is_cube)
        {
            std::string extension = source.source_path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            DecodedDdsImage decoded;
            if (extension == ".dds")
            {
                if (!DecodeDdsCube(source.source_path, decoded))
                {
                    DebugMessage("[DX12] 空のDDS読み込みに失敗しました。\n");
                    remember_decode_failure();
                    return false;
                }
            }
            else if (extension == ".hdr")
            {
                if (!DecodeHdrPanoramaToCube(source.source_path, decoded))
                {
                    DebugMessage("[DX12] 空のHDR読み込みに失敗しました。\n");
                    remember_decode_failure();
                    return false;
                }
            }
            else
            {
                remember_decode_failure();
                return false;
            }
            std::vector<float> cpu_rgba;
            std::uint32_t cpu_width = 0;
            const std::vector<float>* cpu_source =
                CopyCubeMip0ToFloat(decoded, cpu_rgba, cpu_width) ? &cpu_rgba : nullptr;
            try
            {
                sky_source_paths_.insert_or_assign(source.key, source.source_path);
            }
            catch (...)
            {
                return false;
            }
            return CreateStaticCubeTexture(source.key, decoded.width, decoded.height,
                decoded.mip_levels, decoded.format, decoded.subresources, cpu_source, cpu_width);
        }
        StaticTextureResource texture;
        if (!source.rgba.empty())
        {
            const std::uint64_t expected = static_cast<std::uint64_t>(source.width) *
                static_cast<std::uint64_t>(source.height) * 4ull;
            if (source.width == 0 || source.height == 0 || expected != source.rgba.size() ||
                !D3D12ResourceFactory::CreateTexture2DRgba8(device_.Get(), upload_context_,
                    source.rgba.data(), source.width, source.height, source.width * 4u,
                    texture.resource))
                return false;
            texture.width = source.width;
            texture.height = source.height;
            texture.mip_levels = 1;
            texture.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        else
        {
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
                const DXGI_FORMAT resource_format = ToLinearTextureFormat(decoded.format);
                if (!D3D12ResourceFactory::CreateTexture2D(device_.Get(), upload_context_,
                    decoded.width, decoded.height, decoded.mip_levels, resource_format,
                    decoded.subresources, texture.resource))
                    return false;
                texture.width = decoded.width;
                texture.height = decoded.height;
                texture.mip_levels = decoded.mip_levels;
                texture.format = resource_format;
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
        }

        if (!resource_descriptor_allocator_.Allocate(1, texture.srv)) return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = ToLinearTextureFormat(texture.format);
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = texture.mip_levels;
        device_->CreateShaderResourceView(texture.resource.Get(), &srv, texture.srv.cpu);
        const DXGI_FORMAT srgb_format = ToSrgbTextureFormat(texture.format);
        if (srgb_format != DXGI_FORMAT_UNKNOWN)
        {
            if (!resource_descriptor_allocator_.Allocate(1, texture.srgb_srv))
            {
                resource_descriptor_allocator_.Free(texture.srv);
                return false;
            }
            srv.Format = srgb_format;
            device_->CreateShaderResourceView(texture.resource.Get(), &srv, texture.srgb_srv.cpu);
        }
        try
        {
            auto result = texture_cache_.try_emplace(source.key);
            if (!result.second)
            {
                resource_descriptor_allocator_.Free(texture.srv);
                if (texture.srgb_srv.IsValid())
                    resource_descriptor_allocator_.Free(texture.srgb_srv);
                return true;
            }
            result.first->second.resource = std::move(texture.resource);
            result.first->second.srv = texture.srv;
            result.first->second.srgb_srv = texture.srgb_srv;
            result.first->second.width = texture.width;
            result.first->second.height = texture.height;
            result.first->second.mip_levels = texture.mip_levels;
            result.first->second.format = texture.format;
            texture.srv = {};
            texture.srgb_srv = {};
        }
        catch (...)
        {
            if (texture.srv.IsValid())
                resource_descriptor_allocator_.Free(texture.srv);
            if (texture.srgb_srv.IsValid())
                resource_descriptor_allocator_.Free(texture.srgb_srv);
            return false;
        }
        return true;
    }

    bool D3D12DeviceContext::TryGetStaticTextureSize(const std::string& key,
        std::uint32_t& width, std::uint32_t& height) const noexcept
    {
        const auto found = texture_cache_.find(key);
        if (found == texture_cache_.end() || found->second.width == 0 ||
            found->second.height == 0) return false;
        width = found->second.width;
        height = found->second.height;
        return true;
    }

    bool D3D12DeviceContext::EnsureSkyEnvironment(
        const D3D12SkySubmission& sky) noexcept
    {
        last_sky_ibl_status_ = D3D12SkyIblStatus::Ready;
        if (!sky.enabled || sky.texture_key.empty()) return true;
        bool cache_changed = false;
        const auto report_fallback = [this](const std::string& texture_key,
            D3D12SkyIblStatus status, const char* message) noexcept
        {
            last_sky_ibl_status_ = status;
            bool first_report = false;
            try
            {
                first_report = sky_ibl_failures_.emplace(texture_key, status).second;
            }
            catch (...)
            {
            }
            if (!first_report) return;
            DebugMessage(message);
            diagnostics_.PushMessage(D3D12_MESSAGE_SEVERITY_WARNING, message);
        };
        const auto ensure_one = [this, &cache_changed, &report_fallback](
            const std::string& texture_key) -> bool
        {
            if (texture_key.empty()) return true;
            const auto previous_failure = sky_ibl_failures_.find(texture_key);
            if (previous_failure != sky_ibl_failures_.end())
            {
                last_sky_ibl_status_ = previous_failure->second;
                return true;
            }
            const auto source_texture = texture_cache_.find(texture_key);
            if (source_texture == texture_cache_.end() || !source_texture->second.is_cube)
                return true;

            const std::string diffuse_key = texture_key + ":ibl_diffuse";
            const std::string specular_key = texture_key + ":ibl_specular";
            const bool diffuse_cached = texture_cache_.find(diffuse_key) != texture_cache_.end();
            const bool specular_cached = texture_cache_.find(specular_key) != texture_cache_.end();
            if (diffuse_cached && specular_cached)
            {
                sky_cpu_cubes_.erase(texture_key);
                return true;
            }

            const auto source_path = sky_source_paths_.find(texture_key);
            if (source_path == sky_source_paths_.end()) return true;
            DecodedDdsImage diffuse;
            DecodedDdsImage specular;
            bool diffuse_ready = ReadSkyCubeCache(SkyCachePath(source_path->second, ".iem"),
                diffuse);
            bool specular_ready = ReadSkyCubeCache(SkyCachePath(source_path->second, ".pmrem"),
                specular);
            if (!diffuse_ready || !specular_ready)
            {
                const auto cpu_source = sky_cpu_cubes_.find(texture_key);
                if (cpu_source == sky_cpu_cubes_.end())
                {
                    report_fallback(texture_key, D3D12SkyIblStatus::FallbackMissingCpuSource,
                        "[DX12] IBL を焼けなかったので簡易版で描いています（CPU側の空データがありません）。\n");
                    return true;
                }
                DecodedDdsImage generated_diffuse;
                DecodedDdsImage generated_specular;
                if (!BuildIblCubes(cpu_source->second.rgba, cpu_source->second.width,
                    generated_diffuse, generated_specular))
                {
                    report_fallback(texture_key, D3D12SkyIblStatus::FallbackBakeFailed,
                        "[DX12] IBL を焼けなかったので簡易版で描いています（畳み込み処理に失敗しました）。\n");
                    return true;
                }
                if (!diffuse_ready)
                {
                    diffuse = std::move(generated_diffuse);
                    diffuse_ready = true;
                    cache_changed = WriteSkyCubeCache(
                        SkyCachePath(source_path->second, ".iem"), diffuse) || cache_changed;
                }
                if (!specular_ready)
                {
                    specular = std::move(generated_specular);
                    specular_ready = true;
                    cache_changed = WriteSkyCubeCache(
                        SkyCachePath(source_path->second, ".pmrem"), specular) || cache_changed;
                }
            }
            if (!diffuse_cached && diffuse_ready &&
                !CreateStaticCubeTexture(diffuse_key, diffuse.width, diffuse.height,
                    diffuse.mip_levels, diffuse.format, diffuse.subresources))
                return false;
            if (!specular_cached && specular_ready &&
                !CreateStaticCubeTexture(specular_key, specular.width, specular.height,
                    specular.mip_levels, specular.format, specular.subresources))
                return false;
            sky_cpu_cubes_.erase(texture_key);
            return true;
        };
        if (!ensure_one(sky.texture_key)) return false;
        if (!ensure_one(sky.secondary_texture_key)) return false;
        for (const std::string& texture_key : sky.keyframe_texture_keys)
            if (!ensure_one(texture_key)) return false;
        std::unordered_set<std::string> protected_stems;
        std::filesystem::path cache_directory;
        const auto protect_cache_for = [this, &protected_stems, &cache_directory](
            const std::string& texture_key) noexcept
        {
            if (texture_key.empty()) return;
            const auto source_path = sky_source_paths_.find(texture_key);
            if (source_path == sky_source_paths_.end()) return;
            const std::filesystem::path cache_path = SkyCachePath(source_path->second, ".iem");
            if (cache_path.empty()) return;
            if (cache_directory.empty()) cache_directory = cache_path.parent_path();
            try { protected_stems.insert(cache_path.stem().string()); }
            catch (...) {}
        };
        protect_cache_for(sky.texture_key);
        protect_cache_for(sky.secondary_texture_key);
        for (const std::string& texture_key : sky.keyframe_texture_keys)
            protect_cache_for(texture_key);
        if (!sky_cache_prune_initialized_ || cache_changed)
        {
            PruneSkyCache(cache_directory, protected_stems);
            sky_cache_prune_initialized_ = true;
        }
        return true;
    }

    bool D3D12DeviceContext::EnsureStaticShader(
        const D3D12StaticShaderSource& source) noexcept
    {
        if (source.key.empty()) return true;
        if (custom_static_pipelines_.find(source.key) != custom_static_pipelines_.end())
        {
            ++pso_cache_hits_;
            return true;
        }
        if (custom_static_shader_failures_.find(source.key) !=
            custom_static_shader_failures_.end())
            return false;
        if (source.source_path.empty()) return false;
        ++pso_cache_misses_;

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
        if (!CreateStaticPipelineSet(compiled.bytecode, pipelines, source.key))
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
        if (static_mesh_cache_.empty() && skinned_mesh_cache_.empty() &&
            static_mesh_bounds_cache_.empty() && skinned_mesh_bounds_cache_.empty() &&
            texture_cache_.size() <= persistent_textures && static_texture_failures_.empty() &&
            custom_static_pipelines_.empty() && custom_static_shader_failures_.empty() &&
            custom_ui_effect_pipelines_.empty() && custom_ui_effect_shader_diagnostics_.empty() &&
            scene3d_motion_history_.empty() && sky_cpu_cubes_.empty() &&
            sky_source_paths_.empty() && sky_ibl_failures_.empty())
            return true;
        if (!WaitForGpu()) return false;

        static_mesh_cache_.clear();
        skinned_mesh_cache_.clear();
        static_mesh_bounds_cache_.clear();
        skinned_mesh_bounds_cache_.clear();
        scene3d_motion_history_.clear();
        static_texture_failures_.clear();
        custom_static_pipelines_.clear();
        custom_static_shader_failures_.clear();
        custom_ui_effect_pipelines_.clear();
        custom_ui_effect_shader_diagnostics_.clear();
        for (auto it = texture_cache_.begin(); it != texture_cache_.end();)
        {
            if (it->first.rfind("__dx12_", 0) == 0)
            {
                ++it;
                continue;
            }
            if (it->second.srv.IsValid())
                resource_descriptor_allocator_.Free(it->second.srv);
            if (it->second.srgb_srv.IsValid())
                resource_descriptor_allocator_.Free(it->second.srgb_srv);
            it = texture_cache_.erase(it);
        }
        sky_cpu_cubes_.clear();
        sky_source_paths_.clear();
        sky_ibl_failures_.clear();
        last_sky_ibl_status_ = D3D12SkyIblStatus::Ready;
        sky_cache_prune_initialized_ = false;
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
        if (!CacheMeshLocalBounds(submission)) return false;

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
            command_list_->SetGraphicsRootDescriptorTable(4, base_texture->srgb_srv.IsValid()
                ? base_texture->srgb_srv.gpu : base_texture->srv.gpu);                    // t0
            for (std::uint32_t i = 0; i < 8; ++i)
            {
                const StaticTextureResource* texture = material_textures[i];
                const bool color_texture = i == 0u || i == 4u;
                const D3D12_GPU_DESCRIPTOR_HANDLE texture_srv = color_texture &&
                    texture->srgb_srv.IsValid() ? texture->srgb_srv.gpu : texture->srv.gpu;
                command_list_->SetGraphicsRootDescriptorTable(5 + i,
                    texture_srv);                                                        // t40..t47
            }
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
        ReleaseOffscreenTarget(scene3d_deferred_target_);
        ReleaseOffscreenTarget(game_view_target_);
        ReleaseOffscreenTarget(ui_preview_target_);
        for (auto& target : ui_preview_effect_targets_)
            ReleaseOffscreenTarget(target);
        for (auto& target : ui_effect_targets_) ReleaseOffscreenTarget(target);
        for (auto& target : scene_effect_targets_) ReleaseOffscreenTarget(target);
        ReleaseOffscreenTarget(scene_sky_effect_target_);
        for (auto& entry : ui_effect_history_targets_)
            ReleaseOffscreenTarget(entry.second.target);
        ui_effect_history_targets_.clear();
        for (auto& entry : ui_preview_effect_history_targets_)
            ReleaseOffscreenTarget(entry.second.target);
        ui_preview_effect_history_targets_.clear();
        for (auto& entry : scene_effect_history_targets_)
            ReleaseOffscreenTarget(entry.second.target);
        scene_effect_history_targets_.clear();
        scene_effect_history_write_serial_ = 0;
        scene_effect_submission_.Clear();
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
        ReleaseBackBufferCapture();
        // Scene 3DのShadow DSVもdsv_allocator_が所有する。
        // Swap Chain ResizeでDescriptor Heapを再構築する前に解放する。
        ReleaseScene3DRenderTargets();
        ReleaseScene3DShadowTargets();
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
        ReleaseRetiredStaticMeshes(completed);
    }

    // 置換で外した静的メッシュは、次に Signal する Fence を越えるまで解放しない。
    void D3D12DeviceContext::RetireStaticMesh(std::unique_ptr<D3D12MeshBuffer> mesh) noexcept
    {
        if (mesh == nullptr) return;
        try
        {
            retired_static_meshes_.emplace_back(next_fence_value_, std::move(mesh));
        }
        catch (...)
        {
        }
    }

    void D3D12DeviceContext::ReleaseRetiredStaticMeshes(
        std::uint64_t completed_fence_value) noexcept
    {
        std::size_t index = 0;
        while (index < retired_static_meshes_.size())
        {
            if (retired_static_meshes_[index].first > completed_fence_value)
            {
                ++index;
                continue;
            }
            retired_static_meshes_.erase(retired_static_meshes_.begin() +
                static_cast<std::ptrdiff_t>(index));
        }
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
        const auto wait_begin = std::chrono::steady_clock::now();
        if (WaitForSingleObject(fence_event_, INFINITE) != WAIT_OBJECT_0)
        {
            ReportDeviceRemoved(E_FAIL);
            return false;
        }
        const auto wait_end = std::chrono::steady_clock::now();
        ++fence_wait_count_;
        fence_wait_nanoseconds_ += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(wait_end - wait_begin).count());
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

    bool D3D12DeviceContext::RequestBackBufferCapture() noexcept
    {
        if (!frame_open_ || back_buffer_capture_requested_ ||
            back_buffer_capture_recorded_ || back_buffer_capture_readback_ != nullptr)
            return false;
        back_buffer_capture_requested_ = true;
        return true;
    }

    bool D3D12DeviceContext::RecordBackBufferCapture() noexcept
    {
        if (!back_buffer_capture_requested_ || !frame_open_ ||
            device_ == nullptr || command_list_ == nullptr ||
            render_targets_[frame_index_] == nullptr)
            return false;

        const D3D12_RESOURCE_DESC source_desc =
            render_targets_[frame_index_]->GetDesc();
        if (source_desc.Format != kBackBufferFormat || source_desc.Width == 0 ||
            source_desc.Height == 0 || source_desc.DepthOrArraySize != 1 ||
            source_desc.MipLevels != 1 || source_desc.SampleDesc.Count != 1)
            return false;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT row_count = 0;
        UINT64 row_size = 0;
        UINT64 total_bytes = 0;
        device_->GetCopyableFootprints(&source_desc, 0, 1, 0, &footprint,
            &row_count, &row_size, &total_bytes);
        if (row_count != source_desc.Height || row_size < source_desc.Width * 4ull ||
            total_bytes == 0 || total_bytes > (std::numeric_limits<UINT64>::max)())
            return false;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC readback_desc{};
        readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readback_desc.Width = total_bytes;
        readback_desc.Height = 1;
        readback_desc.DepthOrArraySize = 1;
        readback_desc.MipLevels = 1;
        readback_desc.Format = DXGI_FORMAT_UNKNOWN;
        readback_desc.SampleDesc.Count = 1;
        readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT result = device_->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &readback_desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(back_buffer_capture_readback_.ReleaseAndGetAddressOf()));
        if (FAILED(result)) return false;

        if (!resource_state_tracker_.Transition(command_list_.Get(),
            render_targets_[frame_index_].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE))
        {
            back_buffer_capture_readback_.Reset();
            return false;
        }

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = back_buffer_capture_readback_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = render_targets_[frame_index_].Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;
        command_list_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        if (!resource_state_tracker_.Transition(command_list_.Get(),
            render_targets_[frame_index_].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET))
        {
            back_buffer_capture_readback_.Reset();
            return false;
        }

        back_buffer_capture_footprint_ = footprint;
        back_buffer_capture_row_size_ = row_size;
        back_buffer_capture_row_count_ = row_count;
        back_buffer_capture_width_ = static_cast<std::uint32_t>(source_desc.Width);
        back_buffer_capture_height_ = source_desc.Height;
        back_buffer_capture_requested_ = false;
        back_buffer_capture_recorded_ = true;
        return true;
    }

    void D3D12DeviceContext::ReleaseBackBufferCapture() noexcept
    {
        back_buffer_capture_readback_.Reset();
        back_buffer_capture_footprint_ = {};
        back_buffer_capture_row_size_ = 0;
        back_buffer_capture_row_count_ = 0;
        back_buffer_capture_width_ = 0;
        back_buffer_capture_height_ = 0;
        back_buffer_capture_requested_ = false;
        back_buffer_capture_recorded_ = false;
    }

    bool D3D12DeviceContext::ConsumeBackBufferCapture(
        std::vector<std::uint8_t>& rgba, std::uint32_t& width,
        std::uint32_t& height) noexcept
    {
        rgba.clear();
        width = 0;
        height = 0;
        if (!back_buffer_capture_recorded_ || back_buffer_capture_readback_ == nullptr ||
            back_buffer_capture_width_ == 0 || back_buffer_capture_height_ == 0 ||
            back_buffer_capture_row_count_ != back_buffer_capture_height_ ||
            back_buffer_capture_row_size_ <
                static_cast<std::uint64_t>(back_buffer_capture_width_) * 4ull)
        {
            ReleaseBackBufferCapture();
            return false;
        }
        if (!WaitForGpu())
        {
            ReleaseBackBufferCapture();
            return false;
        }

        const std::size_t tight_row =
            static_cast<std::size_t>(back_buffer_capture_width_) * 4u;
        const std::size_t byte_count = tight_row * back_buffer_capture_height_;
        try
        {
            rgba.resize(byte_count);
        }
        catch (...)
        {
            ReleaseBackBufferCapture();
            return false;
        }

        D3D12_RANGE range{ 0, back_buffer_capture_footprint_.Footprint.RowPitch *
            static_cast<SIZE_T>(back_buffer_capture_row_count_) };
        void* mapped = nullptr;
        const HRESULT map_result = back_buffer_capture_readback_->Map(0, &range, &mapped);
        if (FAILED(map_result) || mapped == nullptr)
        {
            ReleaseBackBufferCapture();
            return false;
        }

        const std::uint8_t* source = static_cast<const std::uint8_t*>(mapped) +
            back_buffer_capture_footprint_.Offset;
        const std::size_t source_row_pitch =
            back_buffer_capture_footprint_.Footprint.RowPitch;
        // 実測: Editor 画面が青のとき撮影 PNG が黄色になる。読み戻しは BGRA 並びで届く。
        for (std::uint32_t row = 0; row < back_buffer_capture_height_; ++row)
        {
            std::uint8_t* destination = rgba.data() +
                static_cast<std::size_t>(row) * tight_row;
            const std::uint8_t* source_row =
                source + static_cast<std::size_t>(row) * source_row_pitch;
            for (std::size_t offset = 0; offset + 3 < tight_row; offset += 4)
            {
                destination[offset + 0] = source_row[offset + 2];
                destination[offset + 1] = source_row[offset + 1];
                destination[offset + 2] = source_row[offset + 0];
                destination[offset + 3] = source_row[offset + 3];
            }
        }
        back_buffer_capture_readback_->Unmap(0, nullptr);
        width = back_buffer_capture_width_;
        height = back_buffer_capture_height_;
        ReleaseBackBufferCapture();
        return true;
    }

    bool D3D12DeviceContext::BeginFrame(const float clear_color[4]) noexcept
    {
        if (!IsInitialized() || fatal_error_ || frame_open_ || clear_color == nullptr)
            return false;
        if (!WaitForFrame(frame_index_)) return false;
        const std::uint64_t completed_fence = fence_ ? fence_->GetCompletedValue() : 0;
        diagnostics_.BeginFrame(frame_index_, completed_fence);

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
        if (back_buffer_capture_requested_ && !RecordBackBufferCapture())
        {
            // 撮影失敗で通常の Present まで巻き戻すことはしない。
            // 呼び出し側は Readback の回収失敗として検出する。
            back_buffer_capture_requested_ = false;
        }
        diagnostics_.BeginPass(command_list_.Get(), D3D12GpuPass::Present);
        if (!TransitionCurrentRenderTarget(D3D12_RESOURCE_STATE_PRESENT))
        {
            frame_open_ = false;
            command_list_->Close();
            ReportDeviceRemoved(E_FAIL);
            return false;
        }
        diagnostics_.EndPass(command_list_.Get(), D3D12GpuPass::Present);
        diagnostics_.ResolveQueries(command_list_.Get());
        const HRESULT close_result = command_list_->Close();
        if (FAILED(close_result))
        {
            frame_open_ = false;
            ReportDeviceRemoved(close_result);
            return false;
        }
        ID3D12CommandList* lists[] = { command_list_.Get() };
        command_queue_->ExecuteCommandLists(1, lists);

        const HRESULT present = swap_chain_->Present(present_sync_interval_, 0);
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
        frame_upload_peak_ = (std::max)(frame_upload_peak_,
            frame_resources_[submitted_frame].upload_allocator.Used());
        diagnostics_.SetRuntimeStats(BuildRuntimeStats());
        diagnostics_.MarkSubmitted(submitted_frame, signal_value);
        diagnostics_.DrainInfoQueue();
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

    D3D12RuntimeStats D3D12DeviceContext::BuildRuntimeStats() const noexcept
    {
        D3D12RuntimeStats stats{};
        stats.resource_descriptor_capacity = resource_descriptor_allocator_.Capacity();
        stats.resource_descriptor_used = resource_descriptor_allocator_.Used();
        stats.resource_descriptor_peak = resource_descriptor_allocator_.PeakUsed();
        stats.resource_descriptor_fragmentation = resource_descriptor_allocator_.FragmentationRatio();
        stats.resource_descriptor_failures = resource_descriptor_allocator_.AllocationFailures();
        stats.sampler_descriptor_capacity = sampler_descriptor_allocator_.Capacity();
        stats.sampler_descriptor_used = sampler_descriptor_allocator_.Used();
        stats.sampler_descriptor_peak = sampler_descriptor_allocator_.PeakUsed();
        stats.sampler_descriptor_fragmentation = sampler_descriptor_allocator_.FragmentationRatio();
        stats.sampler_descriptor_failures = sampler_descriptor_allocator_.AllocationFailures();
        stats.frame_upload_used = frame_resources_[frame_index_].upload_allocator.Used();
        stats.frame_upload_capacity = frame_resources_[frame_index_].upload_allocator.Capacity();
        stats.frame_upload_peak = frame_upload_peak_;
        stats.upload_wait_count = upload_context_.WaitCount();
        stats.upload_wait_nanoseconds = upload_context_.WaitNanoseconds();
        stats.fence_wait_count = fence_wait_count_;
        stats.fence_wait_nanoseconds = fence_wait_nanoseconds_;
        stats.mesh_resident = static_cast<std::uint64_t>(static_mesh_cache_.size() + skinned_mesh_cache_.size());
        stats.texture_resident = static_cast<std::uint64_t>(texture_cache_.size() + ui_font_texture_cache_.size());
        stats.pso_hits = pso_cache_hits_;
        stats.pso_misses = pso_cache_misses_;
        std::uint64_t pso_count = validation_pipeline_ ? 1ull : 0ull;
        for (const auto& pipeline : static_bridge_pipelines_.pipelines) if (pipeline) ++pso_count;
        for (const auto& entry : custom_static_pipelines_)
            for (const auto& pipeline : entry.second.pipelines) if (pipeline) ++pso_count;
        for (const auto& pipeline : scene3d_static_gbuffer_pipelines_) if (pipeline) ++pso_count;
        for (const auto& pipeline : scene3d_skinned_gbuffer_pipelines_) if (pipeline) ++pso_count;
        for (const auto& pipeline : scene3d_static_layer_pipelines_) if (pipeline) ++pso_count;
        for (const auto& pipeline : scene3d_skinned_layer_pipelines_) if (pipeline) ++pso_count;
        for (const auto& pipeline : scene3d_static_depth_pipelines_) if (pipeline) ++pso_count;
        for (const auto& pipeline : scene3d_skinned_depth_pipelines_) if (pipeline) ++pso_count;
        for (const auto& pipeline : scene3d_static_forward_blend_pipelines_) if (pipeline) ++pso_count;
        for (const auto& pipeline : scene3d_skinned_forward_blend_pipelines_) if (pipeline) ++pso_count;
        for (const auto& pipeline : scene3d_static_shadow_pipelines_) if (pipeline) ++pso_count;
        for (const auto& pipeline : scene3d_skinned_shadow_pipelines_) if (pipeline) ++pso_count;
        if (scene3d_lighting_pipeline_) ++pso_count;
        if (scene3d_skybox_pipeline_) ++pso_count;
        if (scene3d_postprocess_pipeline_) ++pso_count;
        for (const auto& pipeline : ui_pipelines_) if (pipeline) ++pso_count;
        if (ui_hdr_composite_pipeline_) ++pso_count;
        for (const auto& pipeline : ui_effect_pipelines_) if (pipeline) ++pso_count;
        for (const auto& pipeline : ui_effect_hdr_pipelines_) if (pipeline) ++pso_count;
        if (ui_effect_region_pipeline_) ++pso_count;
        if (ui_effect_region_hdr_pipeline_) ++pso_count;
#ifdef USE_IMGUI
        if (imgui_pipeline_) ++pso_count;
#endif
        stats.pso_count = pso_count;
        const D3D12ShaderCompilerStats shader_stats = GetD3D12ShaderCompilerStats();
        stats.dxc_compile_count = shader_stats.compile_count;
        stats.dxc_failure_count = shader_stats.failure_count;
        stats.dxc_total_milliseconds = shader_stats.total_milliseconds;
        return stats;
    }

    void D3D12DeviceContext::WriteDeviceRemovedReport(HRESULT trigger, HRESULT reason,
        bool forced_validation) noexcept
    {
        try
        {
            std::filesystem::create_directories(std::filesystem::path(L"Saved") / L"Logs");
            std::time_t now = std::time(nullptr);
            std::tm local{};
            localtime_s(&local, &now);
            wchar_t file_name[128]{};
            _snwprintf_s(file_name, _countof(file_name), _TRUNCATE,
                L"dx12_device_removed_%04d%02d%02d_%02d%02d%02d.txt",
                local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                local.tm_hour, local.tm_min, local.tm_sec);
            std::ofstream out(std::filesystem::path(L"Saved") / L"Logs" / file_name,
                std::ios::binary | std::ios::trunc);
            if (!out) return;
            out << "DX12_DEVICE_REMOVED_REPORT 1\n";
            out << "FORCED_VALIDATION " << (forced_validation ? 1 : 0) << '\n';
            out << "TRIGGER 0x" << std::hex << std::uppercase
                << static_cast<unsigned long>(trigger) << '\n';
            out << "REASON 0x" << static_cast<unsigned long>(reason) << std::dec << '\n';
            const D3D12RuntimeStats stats = BuildRuntimeStats();
            out << "FRAME_UPLOAD_USED " << stats.frame_upload_used << '\n';
            out << "RESOURCE_DESCRIPTOR_USED " << stats.resource_descriptor_used << '\n';
            out << "SAMPLER_DESCRIPTOR_USED " << stats.sampler_descriptor_used << '\n';
            out << "MESH_RESIDENT " << stats.mesh_resident << '\n';
            out << "TEXTURE_RESIDENT " << stats.texture_resident << '\n';
            out << "PSO_COUNT " << stats.pso_count << '\n';
            Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
            if (device_ != nullptr && SUCCEEDED(device_.As(&dred)) && dred)
            {
                D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
                if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs)))
                {
                    std::uint32_t node_index = 0;
                    for (const D3D12_AUTO_BREADCRUMB_NODE* node = breadcrumbs.pHeadAutoBreadcrumbNode;
                        node != nullptr; node = node->pNext, ++node_index)
                    {
                        out << "BREADCRUMB_NODE " << node_index << ' ';
                        if (node->pCommandQueueDebugNameA) out << node->pCommandQueueDebugNameA;
                        out << " / ";
                        if (node->pCommandListDebugNameA) out << node->pCommandListDebugNameA;
                        const UINT completed = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
                        out << " completed=" << completed << " count=" << node->BreadcrumbCount << '\n';
                        if (node->pCommandHistory != nullptr)
                        {
                            for (UINT op = 0; op < node->BreadcrumbCount; ++op)
                            {
                                out << "  OP " << op << " code="
                                    << static_cast<unsigned int>(node->pCommandHistory[op])
                                    << " executed=" << (op < completed ? 1 : 0) << '\n';
                            }
                        }
                    }
                }
                D3D12_DRED_PAGE_FAULT_OUTPUT page_fault{};
                if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&page_fault)))
                {
                    out << "PAGE_FAULT_VA 0x" << std::hex << std::uppercase
                        << static_cast<unsigned long long>(page_fault.PageFaultVA)
                        << std::dec << '\n';
                    auto dump_allocations = [&out](const char* label,
                        const D3D12_DRED_ALLOCATION_NODE* head)
                    {
                        std::uint32_t index = 0;
                        for (const D3D12_DRED_ALLOCATION_NODE* node = head;
                            node != nullptr; node = node->pNext, ++index)
                        {
                            out << label << ' ' << index << " type="
                                << static_cast<unsigned int>(node->AllocationType) << " name=";
                            if (node->ObjectNameA) out << node->ObjectNameA;
                            out << '\n';
                        }
                    };
                    dump_allocations("EXISTING_ALLOCATION", page_fault.pHeadExistingAllocationNode);
                    dump_allocations("RECENT_FREED_ALLOCATION", page_fault.pHeadRecentFreedAllocationNode);
                }
            }
        }
        catch (...)
        {
        }
    }

    bool D3D12DeviceContext::ForceDeviceRemovedDiagnostic() noexcept
    {
        if (device_ == nullptr) return false;
        const HRESULT reason = DXGI_ERROR_DEVICE_REMOVED;
        WriteDeviceRemovedReport(reason, reason, true);
        diagnostics_.PushMessage(D3D12_MESSAGE_SEVERITY_WARNING,
            "[DX12] forced device-removed diagnostic path executed");
        return true;
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
        std::fprintf(stderr, "%s", message);
        diagnostics_.PushMessage(D3D12_MESSAGE_SEVERITY_ERROR, message);
        WriteDeviceRemovedReport(trigger, last_device_removed_reason_, false);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12DeviceContext::CurrentRenderTargetView() const noexcept
    {
        return rtv_allocator_.CpuHandle(rtv_allocation_.index + frame_index_);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12DeviceContext::CurrentDepthStencilView() const noexcept
    {
        return dsv_allocator_.CpuHandle(dsv_allocation_.index);
    }

    void D3D12DeviceContext::SetInitializationFailure(const char* stage,
        HRESULT result) noexcept
    {
        if (last_initialization_stage_[0] != '\0') return;
        std::snprintf(last_initialization_stage_, sizeof(last_initialization_stage_),
            "%s", stage != nullptr ? stage : "unknown");
        last_initialization_result_ = result;
        char message[256]{};
        std::snprintf(message, sizeof(message),
            "[DX12] initialization failed at %s (hr=0x%08lx)\n",
            last_initialization_stage_, static_cast<unsigned long>(result));
        DebugMessage(message);
        // GUI subsystem では OutputDebugString が読めないので stderr にも出す。
        std::fprintf(stderr, "%s", message);
    }
}
