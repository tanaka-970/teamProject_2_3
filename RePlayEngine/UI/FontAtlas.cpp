// FontAtlas のうち、Face 選択・Glyph 管理と Text layout だけを持つ。
//
//   FontAtlas.cpp     ... Face / Glyph 管理と layout（このファイル）
//   FontAtlasSdf.cpp  ... SDF 生成と Atlas texture の再構築

#include "FontAtlas.h"

#include "../Assets/AssetDatabase.h"
#include "../Components/UI/UITextComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

namespace ReplayEngine::UI
{
    namespace
    {
        constexpr const char* fallback_face_key = "__replay_default_font__";
        std::uint32_t DecodeUtf8(const std::string& text, std::size_t& offset)
        {
            const unsigned char c0 = static_cast<unsigned char>(text[offset++]);
            if (c0 < 0x80) return c0;
            if ((c0 & 0xE0) == 0xC0 && offset < text.size())
            {
                const unsigned char c1 = static_cast<unsigned char>(text[offset++]);
                if ((c1 & 0xC0) != 0x80) return 0x25A1u;
                return ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
            }
            if ((c0 & 0xF0) == 0xE0 && offset + 1 < text.size())
            {
                const unsigned char c1 = static_cast<unsigned char>(text[offset++]);
                const unsigned char c2 = static_cast<unsigned char>(text[offset++]);
                if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return 0x25A1u;
                return ((c0 & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
            }
            if ((c0 & 0xF8) == 0xF0 && offset + 2 < text.size())
            {
                const unsigned char c1 = static_cast<unsigned char>(text[offset++]);
                const unsigned char c2 = static_cast<unsigned char>(text[offset++]);
                const unsigned char c3 = static_cast<unsigned char>(text[offset++]);
                if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
                    return 0x25A1u;
                return ((c0 & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) |
                    ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
            }
            return 0x25A1u;
        }

        bool ReadBinaryFile(const std::filesystem::path& path, std::vector<unsigned char>& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff size = stream.tellg();
            if (size <= 0) return false;
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(size));
            stream.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
            return stream.good();
        }

        bool ContainsCodepoint(const std::vector<std::uint32_t>& values, std::uint32_t codepoint)
        {
            return std::find(values.begin(), values.end(), codepoint) != values.end();
        }

    }

    bool FontAtlas::Initialize(ID3D11Device* device)
    {
        Release();
        if (device == nullptr) return false;
        device_ = device;
        active_face_key_ = fallback_face_key;
        return EnsureFallbackFace();
    }

    void FontAtlas::Release() noexcept
    {
        faces_.clear();
        active_face_key_.clear();
        device_.Reset();
        baked_font_size_ = 128.0f;
    }

    FontAtlas::FaceAtlas* FontAtlas::ActiveFace() noexcept
    {
        const auto found = faces_.find(active_face_key_);
        return found != faces_.end() ? &found->second : nullptr;
    }

    const FontAtlas::FaceAtlas* FontAtlas::ActiveFace() const noexcept
    {
        const auto found = faces_.find(active_face_key_);
        return found != faces_.end() ? &found->second : nullptr;
    }

    ID3D11ShaderResourceView* FontAtlas::Texture() const noexcept
    {
        const FaceAtlas* face = ActiveFace();
        return face != nullptr ? face->texture.Get() : nullptr;
    }

    bool FontAtlas::EnsureWhiteTexture(FaceAtlas& face)
    {
        if (face.texture || device_ == nullptr) return face.texture != nullptr;
        const std::uint32_t pixel = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 1; desc.Height = 1; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{}; init.pSysMem = &pixel; init.SysMemPitch = sizeof(pixel);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device_->CreateTexture2D(&desc, &init, texture.GetAddressOf()))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = desc.Format; srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        face.texture.Reset();
        return SUCCEEDED(device_->CreateShaderResourceView(texture.Get(), &srv, face.texture.GetAddressOf()));
    }

    bool FontAtlas::EnsureFallbackFace()
    {
        FaceAtlas& face = faces_[fallback_face_key];
        if (!face.font_data.empty() || face.texture) return true;
        const std::filesystem::path candidates[] =
        {
            L"C:\\Windows\\Fonts\\meiryo.ttc",
            L"C:\\Windows\\Fonts\\YuGothM.ttc",
            L"C:\\Windows\\Fonts\\msgothic.ttc",
            L"C:\\Windows\\Fonts\\segoeui.ttf",
        };
        for (const std::filesystem::path& path : candidates)
        {
            if (ReadBinaryFile(path, face.font_data))
            {
                face.valid_font = true;
                break;
            }
        }
        for (std::uint32_t cp = 32; cp < 128; ++cp) face.requested_codepoints.push_back(cp);
        if (face.valid_font && RebuildFace(face)) return true;
        return EnsureWhiteTexture(face);
    }

    bool FontAtlas::SelectFace(const std::string& font_guid,
        const Assets::AssetDatabase* asset_database)
    {
        if (font_guid.empty() || asset_database == nullptr)
        {
            active_face_key_ = fallback_face_key;
            return EnsureFallbackFace();
        }

        auto found = faces_.find(font_guid);
        if (found != faces_.end())
        {
            active_face_key_ = font_guid;
            return found->second.texture != nullptr || found->second.valid_font;
        }

        FaceAtlas face;
        const Assets::AssetRecord* record = asset_database->FindByGuid(font_guid);
        if (record == nullptr || record->kind != Assets::AssetKind::Font ||
            !ReadBinaryFile(record->source_path, face.font_data))
        {
            // Asset が一時的に見つからなくても参照値は UIText に残す。
            // 描画だけ fallback にし、Asset が戻れば次回起動で復帰できる。
            active_face_key_ = fallback_face_key;
            return EnsureFallbackFace();
        }
        face.valid_font = true;
        for (std::uint32_t cp = 32; cp < 128; ++cp) face.requested_codepoints.push_back(cp);
        faces_.emplace(font_guid, std::move(face));
        active_face_key_ = font_guid;
        FaceAtlas& inserted = faces_.find(font_guid)->second;
        if (!RebuildFace(inserted)) return EnsureWhiteTexture(inserted);
        return true;
    }

    bool FontAtlas::EnsureCodepoints(FaceAtlas& face, const std::string& text)
    {
        bool changed = false;
        std::size_t offset = 0;
        while (offset < text.size())
        {
            const std::uint32_t codepoint = DecodeUtf8(text, offset);
            if (codepoint == '\r' || codepoint == '\n') continue;
            if (!ContainsCodepoint(face.requested_codepoints, codepoint))
            {
                face.requested_codepoints.push_back(codepoint);
                changed = true;
            }
        }
        if (!changed) return true;
        return face.valid_font ? RebuildFace(face) : EnsureWhiteTexture(face);
    }

    const FontAtlas::GlyphInfo& FontAtlas::Glyph(std::uint32_t codepoint, float font_size)
    {
        FaceAtlas* face = ActiveFace();
        if (face == nullptr)
        {
            active_face_key_ = fallback_face_key;
            EnsureFallbackFace();
            face = ActiveFace();
        }
        auto baked = face->baked_glyphs.find(codepoint);
        if (baked == face->baked_glyphs.end())
        {
            if (!ContainsCodepoint(face->requested_codepoints, codepoint))
                face->requested_codepoints.push_back(codepoint);
            if (face->valid_font) RebuildFace(*face);
            baked = face->baked_glyphs.find(codepoint);
        }
        if (baked == face->baked_glyphs.end())
        {
            // フォントがグリフを持たない場合も layout を止めない。
            GlyphInfo missing{};
            const float size = (std::max)(font_size, 1.0f);
            missing.size = { size * 0.92f, size };
            missing.advance = missing.size.x;
            face->scaled_glyphs[codepoint] = missing;
            return face->scaled_glyphs.find(codepoint)->second;
        }
        const float size_scale = (std::max)(font_size, 1.0f) / baked_font_size_;
        GlyphInfo scaled = baked->second;
        scaled.size.x *= size_scale;
        scaled.size.y *= size_scale;
        scaled.bearing.x *= size_scale;
        scaled.bearing.y *= size_scale;
        scaled.advance *= size_scale;
        face->scaled_glyphs[codepoint] = scaled;
        return face->scaled_glyphs.find(codepoint)->second;
    }

    void FontAtlas::BuildGlyphs(Components::UITextComponent& text_component,
        float width, float height, const Assets::AssetDatabase* asset_database)
    {
        SelectFace(text_component.font.guid, asset_database);
        FaceAtlas* face = ActiveFace();
        if (face != nullptr && !EnsureCodepoints(*face, text_component.text))
        {
            // カスタムフォントが収まりきらない場合も、文字列の描画自体は
            // 止めず、既定フォントへ切り替える。参照値は UIText 側に残す。
            active_face_key_ = fallback_face_key;
            EnsureFallbackFace();
            face = ActiveFace();
            if (face != nullptr) EnsureCodepoints(*face, text_component.text);
        }

        std::vector<Components::UITextComponent::GlyphQuad>& glyphs = text_component.MutableGlyphs();
        glyphs.clear();
        const float font_size = (std::max)(1.0f, text_component.font_size);
        const float line_height = font_size * (std::max)(0.1f, text_component.line_spacing);
        const float wrap_width = text_component.word_wrap ? (std::max)(1.0f, width) : 1.0e9f;
        std::vector<std::size_t> line_starts{ 0 };
        std::vector<float> line_widths;
        float x = 0.0f, y = 0.0f;
        int character_index = 0;
        std::size_t offset = 0;
        while (offset < text_component.text.size())
        {
            const std::uint32_t codepoint = DecodeUtf8(text_component.text, offset);
            if (codepoint == '\r') continue;
            if (codepoint == '\n')
            {
                line_widths.push_back(x); x = 0.0f; y += line_height;
                line_starts.push_back(glyphs.size()); ++character_index; continue;
            }
            const GlyphInfo& glyph = Glyph(codepoint, font_size);
            const float advance = glyph.advance + text_component.character_spacing;
            if (text_component.word_wrap && x > 0.0f && x + advance > wrap_width)
            {
                line_widths.push_back(x); x = 0.0f; y += line_height;
                line_starts.push_back(glyphs.size());
            }
            Components::UITextComponent::GlyphQuad quad{};
            quad.position = { x + glyph.bearing.x, y + glyph.bearing.y };
            quad.size = glyph.size; quad.uv = glyph.uv;
            quad.character_index = character_index; quad.advance = glyph.advance;
            glyphs.push_back(quad);
            x += advance; ++character_index;
        }
        line_widths.push_back(x);
        const float total_height = line_height * static_cast<float>((std::max)(std::size_t{1}, line_widths.size()));
        float vertical_offset = 0.0f;
        if (text_component.vertical_align == Components::UITextComponent::Middle)
            vertical_offset = (height - total_height) * 0.5f;
        else if (text_component.vertical_align == Components::UITextComponent::Bottom)
            vertical_offset = height - total_height;
        for (std::size_t line = 0; line < line_widths.size(); ++line)
        {
            const std::size_t start = line_starts[(std::min)(line, line_starts.size() - 1)];
            const std::size_t end = line + 1 < line_starts.size() ? line_starts[line + 1] : glyphs.size();
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
