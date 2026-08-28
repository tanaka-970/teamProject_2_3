#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Assets { class AssetDatabase; }
namespace ReplayEngine::Components { class UITextComponent; }

namespace ReplayEngine::UI
{
    class FontAtlas final
    {
    public:
        // SDF の外側へ参照できる距離。グリフ間の余白も必ずこの値以上にする。
        static constexpr int SdfSpreadPixels() noexcept { return 8; }
        static constexpr int AtlasPaddingPixels() noexcept { return SdfSpreadPixels(); }

        struct GlyphInfo
        {
            DirectX::XMFLOAT4 uv{ 0.0f, 0.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT2 size{ 8.0f, 16.0f };
            DirectX::XMFLOAT2 bearing{ 0.0f, 0.0f };
            float advance = 8.0f;
            // 表示サイズへ戻すときに、どの倍率で焼いたかを保持する。
            float bake_scale = 1.0f;
        };

        bool InitializeCpuOnly();
        void Release() noexcept;

        // DX12 backend が同じCPU正本からAtlasをUploadするための読み取り専用スナップショット。
        // 呼び出し側は返されたRGBAをGPUへコピーするだけで、FontAtlasへGPU所有権を持ち込まない。
        // Atlas 本体をコピーせずに現在のキーと版だけを取る。
        // 毎フレームの取得で 2048x2048 の RGBA を複製しないため。
        bool ActiveAtlasRevision(std::string& key, std::uint64_t& revision) const;
        bool CopyActiveAtlas(std::string& key, std::vector<std::uint8_t>& rgba,
            std::uint32_t& width, std::uint32_t& height,
            std::uint64_t& revision) const;

        const GlyphInfo& Glyph(std::uint32_t codepoint, float font_size);
        void BuildGlyphs(Components::UITextComponent& text_component,
            float width, float height, const Assets::AssetDatabase* asset_database);

    private:
        struct FaceAtlas final
        {
            std::vector<std::uint8_t> rgba;
            std::uint32_t atlas_width = 0;
            std::uint32_t atlas_height = 0;
            std::uint64_t revision = 0;
            std::vector<unsigned char> font_data;
            std::vector<std::uint32_t> requested_codepoints;
            std::unordered_map<std::uint32_t, GlyphInfo> baked_glyphs;
            std::unordered_map<std::uint32_t, GlyphInfo> scaled_glyphs;
            bool valid_font = false;
        };

        bool EnsureFallbackFace();
        bool SelectFace(const std::string& font_guid,
            const Assets::AssetDatabase* asset_database);
        bool EnsureCodepoints(FaceAtlas& face, const std::string& text);
        bool RebuildFace(FaceAtlas& face);
        bool EnsureWhiteTexture(FaceAtlas& face);
        FaceAtlas* ActiveFace() noexcept;
        const FaceAtlas* ActiveFace() const noexcept;

        std::unordered_map<std::string, FaceAtlas> faces_;
        std::string active_face_key_;
        // SDF は焼いた解像度が輪郭の細かさの上限になる。64 だと 240px 表示で
        // 格子が階段として見えたため 128 へ上げた。アトラスは 2048x2048 なので
        // 収まりきらない場合は BuildFace 側が警告を出して打ち切る。
        float baked_font_size_ = 128.0f;
    };
}
