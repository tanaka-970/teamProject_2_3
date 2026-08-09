#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Components { class UITextComponent; }

namespace ReplayEngine::UI
{
    class FontAtlas final
    {
    public:
        struct GlyphInfo
        {
            DirectX::XMFLOAT4 uv{ 0.0f, 0.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT2 size{ 8.0f, 16.0f };
            DirectX::XMFLOAT2 bearing{ 0.0f, 0.0f };
            float advance = 8.0f;
        };

        bool Initialize(ID3D11Device* device);
        void Release() noexcept;

        ID3D11ShaderResourceView* Texture() const noexcept { return texture_.Get(); }

        const GlyphInfo& Glyph(std::uint32_t codepoint, float font_size);
        void BuildGlyphs(Components::UITextComponent& text_component,
            float width, float height);

    private:
        bool EnsureTexture(ID3D11Device* device);
        bool LoadDefaultFont();
        bool BuildDefaultAtlas(ID3D11Device* device);
        bool EnsureGlyph(std::uint32_t codepoint, float font_size);

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture_;
        std::vector<unsigned char> font_data_;
        std::unordered_map<std::uint32_t, GlyphInfo> glyphs_;
        float baked_font_size_ = 64.0f;
        bool real_atlas_ = false;
    };
}
