#include "FontAtlas.h"

#include "../Components/UI/UITextComponent.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "ThirdParty/stb_truetype.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace ReplayEngine::UI
{
    namespace
    {
        std::uint32_t DecodeUtf8(const std::string& text, std::size_t& offset)
        {
            const unsigned char c0 = static_cast<unsigned char>(text[offset++]);
            if (c0 < 0x80) return c0;
            if ((c0 & 0xE0) == 0xC0 && offset < text.size())
            {
                const unsigned char c1 = static_cast<unsigned char>(text[offset++]);
                return ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
            }
            if ((c0 & 0xF0) == 0xE0 && offset + 1 < text.size())
            {
                const unsigned char c1 = static_cast<unsigned char>(text[offset++]);
                const unsigned char c2 = static_cast<unsigned char>(text[offset++]);
                return ((c0 & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
            }
            if ((c0 & 0xF8) == 0xF0 && offset + 2 < text.size())
            {
                const unsigned char c1 = static_cast<unsigned char>(text[offset++]);
                const unsigned char c2 = static_cast<unsigned char>(text[offset++]);
                const unsigned char c3 = static_cast<unsigned char>(text[offset++]);
                return ((c0 & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) |
                    ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
            }
            return 0x25A1u;
        }

        bool ReadBinaryFile(const std::filesystem::path& path,
            std::vector<unsigned char>& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff size = stream.tellg();
            if (size <= 0) return false;
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(size));
            stream.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(size));
            return stream.good();
        }

        bool IsFullWidth(std::uint32_t codepoint) noexcept
        {
            return codepoint >= 0x1100u;
        }
    }

    bool FontAtlas::Initialize(ID3D11Device* device)
    {
        Release();
        if (device == nullptr) return false;
        device_ = device;
        if (LoadDefaultFont() && BuildDefaultAtlas(device))
            return true;
        return EnsureTexture(device);
    }

    void FontAtlas::Release() noexcept
    {
        glyphs_.clear();
        baked_glyphs_.clear();
        font_data_.clear();
        baked_font_size_ = 64.0f;
        real_atlas_ = false;
        texture_.Reset();
        device_.Reset();
    }

    const FontAtlas::GlyphInfo& FontAtlas::Glyph(std::uint32_t codepoint, float font_size)
    {
        EnsureGlyph(codepoint, font_size);
        return glyphs_.find(codepoint)->second;
    }

    bool FontAtlas::EnsureTexture(ID3D11Device* device)
    {
        if (texture_) return true;

        const std::uint32_t pixel = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 1;
        desc.Height = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = &pixel;
        init.SysMemPitch = sizeof(pixel);

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device->CreateTexture2D(&desc, &init, texture.GetAddressOf())))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        return SUCCEEDED(device->CreateShaderResourceView(texture.Get(), &srv, texture_.GetAddressOf()));
    }

    bool FontAtlas::LoadDefaultFont()
    {
        font_data_.clear();
        const std::filesystem::path candidates[] =
        {
            L"C:\\Windows\\Fonts\\meiryo.ttc",
            L"C:\\Windows\\Fonts\\YuGothM.ttc",
            L"C:\\Windows\\Fonts\\arial.ttf",
            L"C:\\Windows\\Fonts\\segoeui.ttf",
        };
        for (const std::filesystem::path& path : candidates)
        {
            if (ReadBinaryFile(path, font_data_))
                return true;
        }
        return false;
    }

    bool FontAtlas::BuildDefaultAtlas(ID3D11Device* device)
    {
        if (device == nullptr || font_data_.empty()) return false;

        constexpr int atlas_width = 512;
        constexpr int atlas_height = 512;
        std::vector<unsigned char> alpha(atlas_width * atlas_height);
        std::array<stbtt_bakedchar, 96> baked{};

        const int offset = stbtt_GetFontOffsetForIndex(font_data_.data(), 0);
        const int bake_result = stbtt_BakeFontBitmap(font_data_.data(),
            offset >= 0 ? offset : 0,
            baked_font_size_,
            alpha.data(), atlas_width, atlas_height,
            32, static_cast<int>(baked.size()), baked.data());
        if (bake_result <= 0) return false;

        std::vector<std::uint32_t> rgba(static_cast<std::size_t>(atlas_width) * atlas_height);
        for (std::size_t index = 0; index < rgba.size(); ++index)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(alpha[index]);
            rgba[index] = (a << 24) | 0x00FFFFFFu;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = atlas_width;
        desc.Height = atlas_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = rgba.data();
        init.SysMemPitch = atlas_width * sizeof(std::uint32_t);

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device->CreateTexture2D(&desc, &init, texture.GetAddressOf())))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        texture_.Reset();
        if (FAILED(device->CreateShaderResourceView(texture.Get(), &srv, texture_.GetAddressOf())))
            return false;

        glyphs_.clear();
        baked_glyphs_.clear();
        for (std::size_t index = 0; index < baked.size(); ++index)
        {
            const stbtt_bakedchar& b = baked[index];
            GlyphInfo glyph{};
            glyph.uv = {
                static_cast<float>(b.x0) / static_cast<float>(atlas_width),
                static_cast<float>(b.y0) / static_cast<float>(atlas_height),
                static_cast<float>(b.x1 - b.x0) / static_cast<float>(atlas_width),
                static_cast<float>(b.y1 - b.y0) / static_cast<float>(atlas_height)
            };
            glyph.size = {
                static_cast<float>(b.x1 - b.x0),
                static_cast<float>(b.y1 - b.y0)
            };
            glyph.bearing = { 0.0f, 0.0f };
            glyph.advance = b.xadvance > 0.0f ? b.xadvance : glyph.size.x;
            baked_glyphs_[static_cast<std::uint32_t>(32 + index)] = glyph;
        }
        real_atlas_ = true;
        return true;
    }

    bool FontAtlas::EnsureGlyph(std::uint32_t codepoint, float font_size)
    {
        if (real_atlas_ && codepoint >= 32u && codepoint < 128u)
        {
            const auto found = baked_glyphs_.find(codepoint);
            if (found != baked_glyphs_.end())
            {
                const float scale = (std::max)(font_size, 1.0f) / baked_font_size_;
                GlyphInfo glyph = found->second;
                glyph.size.x *= scale;
                glyph.size.y *= scale;
                glyph.advance *= scale;
                glyphs_[codepoint] = glyph;
                return true;
            }
        }

        const float size = (std::max)(font_size, 1.0f);
        GlyphInfo glyph{};
        const float width_rate = IsFullWidth(codepoint) ? 0.92f : 0.54f;
        glyph.size = { size * width_rate, size };
        glyph.bearing = { 0.0f, 0.0f };
        glyph.advance = glyph.size.x;
        glyph.uv = { 0.0f, 0.0f, 1.0f, 1.0f };
        glyphs_[codepoint] = glyph;
        return true;
    }

    void FontAtlas::BuildGlyphs(Components::UITextComponent& text_component,
        float width, float height)
    {
        std::vector<Components::UITextComponent::GlyphQuad>& glyphs =
            text_component.MutableGlyphs();
        glyphs.clear();

        const float font_size = (std::max)(1.0f, text_component.font_size);
        const float line_height = font_size * (std::max)(0.1f, text_component.line_spacing);
        const float wrap_width = text_component.word_wrap ? (std::max)(1.0f, width) : 1.0e9f;

        std::vector<std::size_t> line_starts;
        std::vector<float> line_widths;
        line_starts.push_back(0);

        float x = 0.0f;
        float y = 0.0f;
        int character_index = 0;
        std::size_t offset = 0;
        while (offset < text_component.text.size())
        {
            const std::size_t before = offset;
            const std::uint32_t codepoint = DecodeUtf8(text_component.text, offset);
            if (codepoint == '\r') continue;
            if (codepoint == '\n')
            {
                line_widths.push_back(x);
                x = 0.0f;
                y += line_height;
                line_starts.push_back(glyphs.size());
                ++character_index;
                continue;
            }

            const GlyphInfo& glyph = Glyph(codepoint, font_size);
            const float advance = glyph.advance + text_component.character_spacing;
            if (text_component.word_wrap && x > 0.0f && x + advance > wrap_width)
            {
                line_widths.push_back(x);
                x = 0.0f;
                y += line_height;
                line_starts.push_back(glyphs.size());
            }

            Components::UITextComponent::GlyphQuad quad{};
            quad.position = { x + glyph.bearing.x, y + glyph.bearing.y };
            quad.size = glyph.size;
            quad.uv = glyph.uv;
            quad.character_index = character_index;
            quad.advance = glyph.advance;
            glyphs.push_back(quad);

            x += advance;
            ++character_index;
            (void)before;
        }
        line_widths.push_back(x);

        const float total_height = line_height * static_cast<float>((std::max)(std::size_t{ 1 }, line_widths.size()));
        float vertical_offset = 0.0f;
        if (text_component.vertical_align == Components::UITextComponent::Middle)
            vertical_offset = (height - total_height) * 0.5f;
        else if (text_component.vertical_align == Components::UITextComponent::Bottom)
            vertical_offset = height - total_height;

        for (std::size_t line = 0; line < line_widths.size(); ++line)
        {
            const std::size_t start = line_starts[(std::min)(line, line_starts.size() - 1)];
            const std::size_t end = line + 1 < line_starts.size()
                ? line_starts[line + 1] : glyphs.size();
            float horizontal_offset = 0.0f;
            if (text_component.horizontal_align == Components::UITextComponent::Center)
                horizontal_offset = (width - line_widths[line]) * 0.5f;
            else if (text_component.horizontal_align == Components::UITextComponent::Right)
                horizontal_offset = width - line_widths[line];

            for (std::size_t index = start; index < end; ++index)
            {
                glyphs[index].position.x += horizontal_offset;
                glyphs[index].position.y = height - vertical_offset -
                    (glyphs[index].position.y + glyphs[index].size.y);
            }
        }
    }
}
