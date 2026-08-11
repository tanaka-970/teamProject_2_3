#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl.h>

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
        static constexpr int AtlasPaddingPixels() noexcept { return 8; }

        struct GlyphInfo
        {
            DirectX::XMFLOAT4 uv{ 0.0f, 0.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT2 size{ 8.0f, 16.0f };
            DirectX::XMFLOAT2 bearing{ 0.0f, 0.0f };
            float advance = 8.0f;
        };

        bool Initialize(ID3D11Device* device);
        void Release() noexcept;

        ID3D11ShaderResourceView* Texture() const noexcept;

        const GlyphInfo& Glyph(std::uint32_t codepoint, float font_size);
        void BuildGlyphs(Components::UITextComponent& text_component,
            float width, float height, const Assets::AssetDatabase* asset_database);

    private:
        struct FaceAtlas final
        {
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;
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

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        std::unordered_map<std::string, FaceAtlas> faces_;
        std::string active_face_key_;
        float baked_font_size_ = 64.0f;
    };
}
