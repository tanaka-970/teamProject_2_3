#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ReplayEngine::Assets
{
    // PNG/JPG を BC 圧縮 DDS へ変換する。
    //
    // 目的:
    //   Sponza の 2K テクスチャを PNG のまま読むと、デコードに約2.7秒かかり
    //   VRAM も RGBA8+ミップで1枚21MB(参照72枚で約1.5GB)を食う。
    //   BC 圧縮 DDS にすれば GPU がそのまま読める形式なのでデコード不要になり、
    //   VRAM も BC1 で 1/8、BC5 で 1/4 に収まる。
    //
    //   Source/core/texture.cpp の load_texture_from_file は
    //   「同名の .dds があればそちらを優先」する実装なので、
    //   foo.png の隣に foo.dds を置くだけで自動的に切り替わる。
    //
    // 形式の選び方:
    //   BaseColor / ORM -> BC1 (RGB 4bpp)。アルファ不要なので最小。
    //   Normal          -> BC5 (RG 8bpp)。XYを高精度に保ちZはシェーダーで再構築。
    //                      BC1だと法線が破綻するため必須。
    //   アルファ付き    -> BC3 (RGBA 8bpp)。
    class TextureCompressor final
    {
    public:
        enum class Format { Auto, BC1, BC3, BC5 };

        struct Result
        {
            bool succeeded = false;
            Format format = Format::BC1;
            std::uint64_t source_bytes = 0;
            std::uint64_t output_bytes = 0;
            int width = 0;
            int height = 0;
            int mip_count = 0;
            std::string error;
        };

        // source を読み、destination へ DDS を書く。
        // destination が空なら source の拡張子を .dds に変えたパスを使う。
        static Result Compress(const std::filesystem::path& source,
            const std::filesystem::path& destination = {},
            Format format = Format::Auto);

        // フォルダ内の画像をまとめて変換する。既に .dds があるものは飛ばす。
        // 画像ごとに独立しているのでParallelLoaderで並列化する。
        struct BatchResult
        {
            int converted = 0;
            int skipped = 0;
            int failed = 0;
            std::uint64_t source_bytes = 0;
            std::uint64_t output_bytes = 0;
            double elapsed_ms = 0.0;
        };
        static BatchResult CompressDirectory(const std::filesystem::path& directory,
            bool overwrite = false, Format format = Format::Auto);

        // ファイル名から法線マップらしさを判定する。
        static bool LooksLikeNormalMap(const std::filesystem::path& path);
    };
}
