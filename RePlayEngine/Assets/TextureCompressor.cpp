#include "TextureCompressor.h"

#include "ParallelLoader.h"

// stb_image の実体は gltf_model.cpp 側(tiny_gltf.h)で定義済みなので、
// ここでは宣言だけを取り込む。
#include "../../tinygltf-release/stb_image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

namespace ReplayEngine::Assets
{
    namespace
    {
        // --- DDS ヘッダ ---------------------------------------------------
        constexpr std::uint32_t kDdsMagic = 0x20534444u;      // "DDS "
        constexpr std::uint32_t kHeaderSize = 124;
        constexpr std::uint32_t kPixelFormatSize = 32;

        constexpr std::uint32_t kCaps = 0x1;
        constexpr std::uint32_t kHeight = 0x2;
        constexpr std::uint32_t kWidth = 0x4;
        constexpr std::uint32_t kPixelFormat = 0x1000;
        constexpr std::uint32_t kMipMapCount = 0x20000;
        constexpr std::uint32_t kLinearSize = 0x80000;

        constexpr std::uint32_t kFourCcFlag = 0x4;
        constexpr std::uint32_t kCapsTexture = 0x1000;
        constexpr std::uint32_t kCapsMipMap = 0x400000;
        constexpr std::uint32_t kCapsComplex = 0x8;

        std::uint32_t MakeFourCc(const char text[5])
        {
            return static_cast<std::uint32_t>(text[0]) |
                (static_cast<std::uint32_t>(text[1]) << 8) |
                (static_cast<std::uint32_t>(text[2]) << 16) |
                (static_cast<std::uint32_t>(text[3]) << 24);
        }

        void WriteDdsHeader(std::ofstream& stream, int width, int height,
            int mip_count, std::uint32_t four_cc, int block_bytes)
        {
            const std::uint32_t blocks_wide =
                static_cast<std::uint32_t>((std::max)(1, (width + 3) / 4));
            const std::uint32_t blocks_high =
                static_cast<std::uint32_t>((std::max)(1, (height + 3) / 4));

            std::uint32_t flags = kCaps | kHeight | kWidth | kPixelFormat | kLinearSize;
            std::uint32_t caps = kCapsTexture;
            if (mip_count > 1)
            {
                flags |= kMipMapCount;
                caps |= kCapsMipMap | kCapsComplex;
            }

            const auto write = [&stream](std::uint32_t value)
            {
                stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
            };

            write(kDdsMagic);
            write(kHeaderSize);
            write(flags);
            write(static_cast<std::uint32_t>(height));
            write(static_cast<std::uint32_t>(width));
            write(blocks_wide * blocks_high * static_cast<std::uint32_t>(block_bytes));
            write(0);                                    // depth
            write(static_cast<std::uint32_t>(mip_count));
            for (int i = 0; i < 11; ++i) write(0);       // reserved1
            write(kPixelFormatSize);
            write(kFourCcFlag);
            write(four_cc);
            write(0);                                    // RGBBitCount
            write(0); write(0); write(0); write(0);      // masks
            write(caps);
            write(0); write(0); write(0); write(0);      // caps2..reserved2
        }

        // --- RGB565 ------------------------------------------------------
        std::uint16_t PackRgb565(float r, float g, float b)
        {
            const int red = std::clamp(static_cast<int>(std::lround(r)), 0, 255) >> 3;
            const int green = std::clamp(static_cast<int>(std::lround(g)), 0, 255) >> 2;
            const int blue = std::clamp(static_cast<int>(std::lround(b)), 0, 255) >> 3;
            return static_cast<std::uint16_t>((red << 11) | (green << 5) | blue);
        }

        void UnpackRgb565(std::uint16_t packed, float out[3])
        {
            // 上位ビットを複製して8bitへ広げる(GPUの復元と揃える)。
            const int red = (packed >> 11) & 0x1F;
            const int green = (packed >> 5) & 0x3F;
            const int blue = packed & 0x1F;
            out[0] = static_cast<float>(red) * 255.0f / 31.0f;
            out[1] = static_cast<float>(green) * 255.0f / 63.0f;
            out[2] = static_cast<float>(blue) * 255.0f / 31.0f;
        }

        // --- BC1 カラーブロック ------------------------------------------
        // 4x4の16画素から代表2色を選ぶ。色空間上の主軸(principal axis)を
        // べき乗法で求め、その両端を端点にする。単純な最小/最大値より
        // 誤差が小さく、グラデーションが縞にならない。
        void EncodeColorBlock(const std::uint8_t* pixels, int stride,
            std::uint8_t out[8])
        {
            float colors[16][3]{};
            float mean[3]{};
            for (int i = 0; i < 16; ++i)
            {
                const std::uint8_t* pixel = pixels + static_cast<size_t>(i) * stride;
                colors[i][0] = static_cast<float>(pixel[0]);
                colors[i][1] = static_cast<float>(pixel[1]);
                colors[i][2] = static_cast<float>(pixel[2]);
                mean[0] += colors[i][0];
                mean[1] += colors[i][1];
                mean[2] += colors[i][2];
            }
            mean[0] /= 16.0f; mean[1] /= 16.0f; mean[2] /= 16.0f;

            // 共分散行列(対称なので6成分)。
            float xx = 0, xy = 0, xz = 0, yy = 0, yz = 0, zz = 0;
            for (const auto& color : colors)
            {
                const float dx = color[0] - mean[0];
                const float dy = color[1] - mean[1];
                const float dz = color[2] - mean[2];
                xx += dx * dx; xy += dx * dy; xz += dx * dz;
                yy += dy * dy; yz += dy * dz; zz += dz * dz;
            }

            // 分散最大の軸から始めてべき乗法で主軸へ寄せる。
            float axis[3]{ 0.0f, 0.0f, 0.0f };
            if (xx >= yy && xx >= zz) axis[0] = 1.0f;
            else if (yy >= zz)        axis[1] = 1.0f;
            else                      axis[2] = 1.0f;

            for (int iteration = 0; iteration < 8; ++iteration)
            {
                const float nx = xx * axis[0] + xy * axis[1] + xz * axis[2];
                const float ny = xy * axis[0] + yy * axis[1] + yz * axis[2];
                const float nz = xz * axis[0] + yz * axis[1] + zz * axis[2];
                const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (length < 1.0e-6f) break;    // 単色ブロック
                axis[0] = nx / length;
                axis[1] = ny / length;
                axis[2] = nz / length;
            }

            // 主軸へ射影して端点を取る。
            float minimum = 1.0e30f, maximum = -1.0e30f;
            for (const auto& color : colors)
            {
                const float t = (color[0] - mean[0]) * axis[0] +
                                (color[1] - mean[1]) * axis[1] +
                                (color[2] - mean[2]) * axis[2];
                minimum = (std::min)(minimum, t);
                maximum = (std::max)(maximum, t);
            }

            const float end_a[3]{
                mean[0] + axis[0] * minimum,
                mean[1] + axis[1] * minimum,
                mean[2] + axis[2] * minimum };
            const float end_b[3]{
                mean[0] + axis[0] * maximum,
                mean[1] + axis[1] * maximum,
                mean[2] + axis[2] * maximum };

            std::uint16_t packed_a = PackRgb565(end_a[0], end_a[1], end_a[2]);
            std::uint16_t packed_b = PackRgb565(end_b[0], end_b[1], end_b[2]);

            // BC1は color0 > color1 のときだけ4色補間になる。
            // 等しい場合は3色モードだがindex0=color0なので単色として正しく出る。
            if (packed_a < packed_b) std::swap(packed_a, packed_b);
            const std::uint16_t color0 = packed_a;
            const std::uint16_t color1 = packed_b;

            float palette[4][3]{};
            UnpackRgb565(color0, palette[0]);
            UnpackRgb565(color1, palette[1]);
            for (int channel = 0; channel < 3; ++channel)
            {
                palette[2][channel] = (palette[0][channel] * 2.0f + palette[1][channel]) / 3.0f;
                palette[3][channel] = (palette[0][channel] + palette[1][channel] * 2.0f) / 3.0f;
            }

            std::uint32_t indices = 0;
            for (int i = 0; i < 16; ++i)
            {
                int best = 0;
                float best_distance = 1.0e30f;
                for (int candidate = 0; candidate < 4; ++candidate)
                {
                    const float dr = colors[i][0] - palette[candidate][0];
                    const float dg = colors[i][1] - palette[candidate][1];
                    const float db = colors[i][2] - palette[candidate][2];
                    const float distance = dr * dr + dg * dg + db * db;
                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best = candidate;
                    }
                }
                indices |= static_cast<std::uint32_t>(best) << (i * 2);
            }

            out[0] = static_cast<std::uint8_t>(color0 & 0xFF);
            out[1] = static_cast<std::uint8_t>(color0 >> 8);
            out[2] = static_cast<std::uint8_t>(color1 & 0xFF);
            out[3] = static_cast<std::uint8_t>(color1 >> 8);
            out[4] = static_cast<std::uint8_t>(indices & 0xFF);
            out[5] = static_cast<std::uint8_t>((indices >> 8) & 0xFF);
            out[6] = static_cast<std::uint8_t>((indices >> 16) & 0xFF);
            out[7] = static_cast<std::uint8_t>((indices >> 24) & 0xFF);
        }

        // --- 単チャンネルブロック (BC3のアルファ / BC5のR,G) --------------
        // alpha0 > alpha1 の8段補間モードを使う。
        void EncodeSingleChannelBlock(const std::uint8_t* pixels, int stride,
            std::uint8_t out[8])
        {
            std::uint8_t minimum = 255, maximum = 0;
            std::uint8_t values[16]{};
            for (int i = 0; i < 16; ++i)
            {
                const std::uint8_t value = pixels[static_cast<size_t>(i) * stride];
                values[i] = value;
                minimum = (std::min)(minimum, value);
                maximum = (std::max)(maximum, value);
            }

            const std::uint8_t alpha0 = maximum;
            const std::uint8_t alpha1 = minimum;

            float palette[8]{};
            palette[0] = static_cast<float>(alpha0);
            palette[1] = static_cast<float>(alpha1);
            if (alpha0 > alpha1)
            {
                for (int i = 2; i < 8; ++i)
                {
                    palette[i] = (static_cast<float>(8 - i) * alpha0 +
                                  static_cast<float>(i - 1) * alpha1) / 7.0f;
                }
            }
            else
            {
                // 単色。全インデックス0で復元できる。
                for (int i = 2; i < 8; ++i) palette[i] = static_cast<float>(alpha0);
            }

            std::uint64_t indices = 0;
            for (int i = 0; i < 16; ++i)
            {
                int best = 0;
                float best_distance = 1.0e30f;
                for (int candidate = 0; candidate < 8; ++candidate)
                {
                    const float distance =
                        std::abs(static_cast<float>(values[i]) - palette[candidate]);
                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best = candidate;
                    }
                }
                indices |= static_cast<std::uint64_t>(best) << (i * 3);
            }

            out[0] = alpha0;
            out[1] = alpha1;
            for (int byte = 0; byte < 6; ++byte)
                out[2 + byte] = static_cast<std::uint8_t>((indices >> (byte * 8)) & 0xFF);
        }

        // --- 画像1レベルをブロック列へ ------------------------------------
        struct Surface
        {
            std::vector<std::uint8_t> pixels;   // RGBA8
            int width = 0;
            int height = 0;
        };

        // 4x4ブロックを切り出す。端はクランプして埋める。
        void GatherBlock(const Surface& surface, int block_x, int block_y,
            std::uint8_t out[16 * 4])
        {
            for (int y = 0; y < 4; ++y)
            {
                const int source_y = (std::min)(block_y * 4 + y, surface.height - 1);
                for (int x = 0; x < 4; ++x)
                {
                    const int source_x = (std::min)(block_x * 4 + x, surface.width - 1);
                    const size_t source =
                        (static_cast<size_t>(source_y) * surface.width + source_x) * 4;
                    std::memcpy(out + (static_cast<size_t>(y) * 4 + x) * 4,
                        surface.pixels.data() + source, 4);
                }
            }
        }

        std::vector<std::uint8_t> EncodeSurface(const Surface& surface,
            TextureCompressor::Format format)
        {
            const int blocks_wide = (std::max)(1, (surface.width + 3) / 4);
            const int blocks_high = (std::max)(1, (surface.height + 3) / 4);
            const int block_bytes = format == TextureCompressor::Format::BC1 ? 8 : 16;

            std::vector<std::uint8_t> output(
                static_cast<size_t>(blocks_wide) * blocks_high * block_bytes);

            for (int block_y = 0; block_y < blocks_high; ++block_y)
            {
                for (int block_x = 0; block_x < blocks_wide; ++block_x)
                {
                    std::uint8_t block[16 * 4]{};
                    GatherBlock(surface, block_x, block_y, block);
                    std::uint8_t* destination = output.data() +
                        (static_cast<size_t>(block_y) * blocks_wide + block_x) * block_bytes;

                    switch (format)
                    {
                    case TextureCompressor::Format::BC1:
                        EncodeColorBlock(block, 4, destination);
                        break;
                    case TextureCompressor::Format::BC3:
                        // 先にアルファ、続いてカラー。
                        EncodeSingleChannelBlock(block + 3, 4, destination);
                        EncodeColorBlock(block, 4, destination + 8);
                        break;
                    case TextureCompressor::Format::BC5:
                        // R と G をそれぞれ単チャンネルブロックとして格納。
                        EncodeSingleChannelBlock(block + 0, 4, destination);
                        EncodeSingleChannelBlock(block + 1, 4, destination + 8);
                        break;
                    default:
                        break;
                    }
                }
            }
            return output;
        }

        // 2x2ボックスフィルタで1段縮小する。
        Surface Downsample(const Surface& source)
        {
            Surface result;
            result.width = (std::max)(1, source.width / 2);
            result.height = (std::max)(1, source.height / 2);
            result.pixels.resize(static_cast<size_t>(result.width) * result.height * 4);

            for (int y = 0; y < result.height; ++y)
            {
                const int y0 = (std::min)(y * 2, source.height - 1);
                const int y1 = (std::min)(y * 2 + 1, source.height - 1);
                for (int x = 0; x < result.width; ++x)
                {
                    const int x0 = (std::min)(x * 2, source.width - 1);
                    const int x1 = (std::min)(x * 2 + 1, source.width - 1);
                    const size_t taps[4]{
                        (static_cast<size_t>(y0) * source.width + x0) * 4,
                        (static_cast<size_t>(y0) * source.width + x1) * 4,
                        (static_cast<size_t>(y1) * source.width + x0) * 4,
                        (static_cast<size_t>(y1) * source.width + x1) * 4 };
                    const size_t out = (static_cast<size_t>(y) * result.width + x) * 4;
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        const unsigned sum =
                            source.pixels[taps[0] + channel] +
                            source.pixels[taps[1] + channel] +
                            source.pixels[taps[2] + channel] +
                            source.pixels[taps[3] + channel];
                        result.pixels[out + channel] =
                            static_cast<std::uint8_t>((sum + 2) / 4);
                    }
                }
            }
            return result;
        }
    }

    bool TextureCompressor::LooksLikeNormalMap(const std::filesystem::path& path)
    {
        std::string name = path.stem().string();
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return name.find("normal") != std::string::npos ||
               name.find("_nrm") != std::string::npos ||
               name.find("_norm") != std::string::npos;
    }

    TextureCompressor::Result TextureCompressor::Compress(
        const std::filesystem::path& source,
        const std::filesystem::path& destination_hint,
        Format format)
    {
        Result result{};

        std::error_code error;
        if (!std::filesystem::exists(source, error))
        {
            result.error = "入力が見つかりません";
            return result;
        }
        result.source_bytes = std::filesystem::file_size(source, error);

        int width = 0, height = 0, components = 0;
        // RGBA固定で受け取る。BC1/BC5でも4chあれば扱いやすい。
        unsigned char* decoded = stbi_load(source.string().c_str(),
            &width, &height, &components, 4);
        if (!decoded || width <= 0 || height <= 0)
        {
            if (decoded) stbi_image_free(decoded);
            result.error = "画像をデコードできません";
            return result;
        }

        Surface surface;
        surface.width = width;
        surface.height = height;
        surface.pixels.assign(decoded,
            decoded + static_cast<size_t>(width) * height * 4);
        stbi_image_free(decoded);

        // 形式の自動判定。
        if (format == Format::Auto)
        {
            if (LooksLikeNormalMap(source)) format = Format::BC5;
            else
            {
                // アルファが実際に使われているかを見る。
                bool has_alpha = false;
                for (size_t i = 3; i < surface.pixels.size(); i += 4)
                {
                    if (surface.pixels[i] < 250) { has_alpha = true; break; }
                }
                format = has_alpha ? Format::BC3 : Format::BC1;
            }
        }
        result.format = format;
        result.width = width;
        result.height = height;

        const char* four_cc_text =
            format == Format::BC1 ? "DXT1" : (format == Format::BC3 ? "DXT5" : "ATI2");
        const int block_bytes = format == Format::BC1 ? 8 : 16;

        // ミップチェーンを1x1まで作る。
        std::vector<Surface> chain;
        chain.push_back(std::move(surface));
        while ((std::max)(chain.back().width, chain.back().height) > 1)
            chain.push_back(Downsample(chain.back()));
        result.mip_count = static_cast<int>(chain.size());

        auto destination = destination_hint;
        if (destination.empty())
        {
            destination = source;
            destination.replace_extension(".dds");
        }
        std::filesystem::create_directories(destination.parent_path(), error);

        std::ofstream stream(destination, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            result.error = "出力を開けません";
            return result;
        }

        WriteDdsHeader(stream, width, height, result.mip_count,
            MakeFourCc(four_cc_text), block_bytes);
        for (const Surface& level : chain)
        {
            const auto encoded = EncodeSurface(level, format);
            stream.write(reinterpret_cast<const char*>(encoded.data()),
                static_cast<std::streamsize>(encoded.size()));
        }
        stream.close();

        result.output_bytes = std::filesystem::file_size(destination, error);
        result.succeeded = true;
        return result;
    }

    TextureCompressor::BatchResult TextureCompressor::CompressDirectory(
        const std::filesystem::path& directory, bool overwrite, Format format)
    {
        BatchResult batch{};
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) return batch;

        const auto start = std::chrono::steady_clock::now();

        std::vector<std::filesystem::path> targets;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error))
        {
            if (!entry.is_regular_file()) continue;
            std::string extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" &&
                extension != ".tga" && extension != ".bmp") continue;

            auto dds = entry.path();
            dds.replace_extension(".dds");
            if (!overwrite && std::filesystem::exists(dds, error))
            {
                ++batch.skipped;
                continue;
            }
            targets.push_back(entry.path());
        }

        std::vector<Result> results(targets.size());
        // 1枚ずつ完全に独立しているのでそのまま並列化できる。
        ParallelLoader::Run(targets.size(), [&](std::size_t index)
        {
            results[index] = Compress(targets[index], {}, format);
        });

        for (const Result& result : results)
        {
            if (result.succeeded)
            {
                ++batch.converted;
                batch.source_bytes += result.source_bytes;
                batch.output_bytes += result.output_bytes;
            }
            else ++batch.failed;
        }

        batch.elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        return batch;
    }
}
