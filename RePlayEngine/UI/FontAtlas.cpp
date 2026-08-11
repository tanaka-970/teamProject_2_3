#include "FontAtlas.h"

#include "../Assets/AssetDatabase.h"
#include "../Components/UI/UITextComponent.h"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "ThirdParty/stb_truetype.h"

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
        constexpr int atlas_width = 2048;
        constexpr int atlas_height = 2048;
        constexpr int atlas_padding = FontAtlas::AtlasPaddingPixels();
        static_assert(atlas_padding >= FontAtlas::SdfSpreadPixels(),
            "SDF spread must fit inside every glyph's atlas padding");

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

        struct DistanceVector final
        {
            int x = 0;
            int y = 0;
            bool valid = false;
        };

        int DistanceSquared(const DistanceVector& value) noexcept
        {
            if (!value.valid) return (std::numeric_limits<int>::max)();
            return value.x * value.x + value.y * value.y;
        }

        void RelaxDistance(DistanceVector& target, const DistanceVector& source,
            int offset_x, int offset_y) noexcept
        {
            if (!source.valid) return;
            const DistanceVector candidate{
                source.x + offset_x, source.y + offset_y, true };
            if (DistanceSquared(candidate) < DistanceSquared(target))
                target = candidate;
        }

        // 8SSEDT の前進・後退走査。二値マスクの最近接テクセルを保持して
        // 距離の二乗を比較するので、斜め方向も単なる 8 方向の膨張にならない。
        void DistanceTransform8(const std::vector<unsigned char>& bitmap,
            int width, int height, bool feature_is_foreground,
            std::vector<float>& distances)
        {
            const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
            std::vector<DistanceVector> nearest(pixel_count);
            for (std::size_t index = 0; index < pixel_count; ++index)
            {
                const bool foreground = bitmap[index] > 127;
                nearest[index].valid = feature_is_foreground == foreground;
            }

            const auto at = [width](int x, int y) noexcept
            {
                return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x);
            };

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    DistanceVector& current = nearest[at(x, y)];
                    if (x > 0) RelaxDistance(current, nearest[at(x - 1, y)], -1, 0);
                    if (y > 0) RelaxDistance(current, nearest[at(x, y - 1)], 0, -1);
                    if (x > 0 && y > 0)
                        RelaxDistance(current, nearest[at(x - 1, y - 1)], -1, -1);
                    if (x + 1 < width && y > 0)
                        RelaxDistance(current, nearest[at(x + 1, y - 1)], 1, -1);
                }
            }

            for (int y = height - 1; y >= 0; --y)
            {
                for (int x = width - 1; x >= 0; --x)
                {
                    DistanceVector& current = nearest[at(x, y)];
                    if (x + 1 < width) RelaxDistance(current, nearest[at(x + 1, y)], 1, 0);
                    if (y + 1 < height) RelaxDistance(current, nearest[at(x, y + 1)], 0, 1);
                    if (x + 1 < width && y + 1 < height)
                        RelaxDistance(current, nearest[at(x + 1, y + 1)], 1, 1);
                    if (x > 0 && y + 1 < height)
                        RelaxDistance(current, nearest[at(x - 1, y + 1)], -1, 1);
                }
            }

            distances.resize(pixel_count);
            for (std::size_t index = 0; index < pixel_count; ++index)
            {
                const int squared = DistanceSquared(nearest[index]);
                distances[index] = squared == (std::numeric_limits<int>::max)()
                    ? std::numeric_limits<float>::infinity()
                    : std::sqrt(static_cast<float>(squared));
            }
        }

        // ---- 拡張点: 文字の輪郭品質 -------------------------------------------
        //
        // 【今の状態】
        //   2026-08-12 に SDF 化し、被覆率で境界を補正して 240px 表示でも
        //   輪郭が滑らかになった。実用として許容できる水準ではあるが、
        //   拡大すると縁にわずかな粗さが残る。依頼者からも「まぁ許す」という
        //   評価で、完成ではない。
        //
        // 【今やっていない理由】
        //   単チャンネル SDF は、鋭い角（漢字のとめ・はね、明朝の飾り）が
        //   わずかに丸まる。これを解くには MSDF（多チャンネル SDF）が要り、
        //   生成側の作り替えになる。他に未実装の演出機能が残っている段階で
        //   ここへ時間をかけるより、先に機能を揃えるべきと判断した。
        //
        // 【直すときにここへ足す】
        //   ・アトラスを 3 チャンネルにし、シェーダー側で中央値を取る（MSDF）
        //   ・baked_font_size_ をさらに上げる。ただしアトラスが 2048x2048 なので
        //     日本語のグリフ数と相談になる。溢れたら警告して打ち切る経路が要る
        //   ・BuildSignedDistanceField の被覆率補正は、境界の 1 テクセルだけを
        //     見ている。斜めの輪郭では隣接テクセルも考慮した方が精度が出る
        //
        // 【壊してはいけない前提】
        //   ・outline_width などのプロパティ名と意味を変えない（既存シーンが壊れる）
        //   ・縁取りも影も無いテキストは 1 サンプルの経路のままにする
        //   ・アトラスの余白 >= spread。足りないと隣のグリフを拾う（実際に起きた）
        //
        // 【未検証】
        //   ・小さいサイズ（24px 程度）での見え方
        //   ・影（shadow_offset / shadow_color）
        //   ・Text Animator による回転・拡縮との併用
        void BuildSignedDistanceField(const std::vector<unsigned char>& bitmap,
            int width, int height, float spread, std::vector<unsigned char>& encoded)
        {
            std::vector<float> distance_to_foreground;
            std::vector<float> distance_to_background;
            DistanceTransform8(bitmap, width, height, true, distance_to_foreground);
            DistanceTransform8(bitmap, width, height, false, distance_to_background);

            encoded.resize(bitmap.size());
            for (std::size_t index = 0; index < bitmap.size(); ++index)
            {
                const bool foreground = bitmap[index] > 127;
                // テクセル中心から境界までの距離にするため、最近接中心間の
                // 距離から半テクセルを引く。これで 0 が輪郭の位置になる。
                const float center_distance = foreground
                    ? distance_to_background[index]
                    : distance_to_foreground[index];
                // Space のような空グリフには foreground が存在しない。
                // 無限距離をそのまま正規化すると「内側」として白くなるため、
                // feature が無い側は明示的に spread の外へ置く。
                const float finite_distance = std::isfinite(center_distance)
                    ? (std::max)(0.0f, center_distance - 0.5f) : spread;
                float signed_distance = (foreground ? 1.0f : -1.0f) *
                    finite_distance;

                // 二値化した白黒だけで距離を測ると、輪郭がテクセル単位に
                // 丸まって階段状になる。ラスタライズ時の被覆率（アンチ
                // エイリアスの濃さ）は、そのテクセル内のどこに輪郭があるかを
                // 表しているので、境界付近だけその分をずらして補正する。
                // 焼いた解像度より大きく表示したときのギザギザが消える。
                const float coverage = static_cast<float>(bitmap[index]) / 255.0f;
                if (finite_distance <= 1.5f)
                {
                    signed_distance = coverage - 0.5f;
                }
                const float normalized = 0.5f +
                    signed_distance / ((std::max)(spread, 0.0001f) * 2.0f);
                const float clamped = (std::max)(0.0f, (std::min)(1.0f, normalized));
                encoded[index] = static_cast<unsigned char>(
                    std::lround(clamped * 255.0f));
            }
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

    bool FontAtlas::RebuildFace(FaceAtlas& face)
    {
        if (device_ == nullptr || !face.valid_font || face.font_data.empty()) return false;
        stbtt_fontinfo info{};
        const int offset = stbtt_GetFontOffsetForIndex(face.font_data.data(), 0);
        if (!stbtt_InitFont(&info, face.font_data.data(), offset >= 0 ? offset : 0)) return false;

        const float scale = stbtt_ScaleForPixelHeight(&info, baked_font_size_);
        // 0 は SDF の -spread として扱う。各グリフのセルを書き込むときに
        // そのグリフだけの距離変換を行うため、隣のグリフは距離計算へ入らない。
        std::vector<unsigned char> sdf_alpha(
            static_cast<std::size_t>(atlas_width) * atlas_height, 0);
        std::unordered_map<std::uint32_t, GlyphInfo> baked;
        int pen_x = 0;
        int pen_y = 0;
        int row_height = 0;
        bool overflowed = false;

        for (std::uint32_t codepoint : face.requested_codepoints)
        {
            int advance = 0, left_bearing = 0;
            stbtt_GetCodepointHMetrics(&info, static_cast<int>(codepoint), &advance, &left_bearing);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetCodepointBitmapBox(&info, static_cast<int>(codepoint), scale, scale,
                &x0, &y0, &x1, &y1);
            const int glyph_width = (std::max)(0, x1 - x0);
            const int glyph_height = (std::max)(0, y1 - y0);
            const int cell_width = glyph_width + atlas_padding * 2;
            const int cell_height = glyph_height + atlas_padding * 2;
            if (pen_x + cell_width > atlas_width)
            {
                pen_x = 0;
                pen_y += row_height;
                row_height = 0;
            }
            if (pen_y + cell_height > atlas_height)
            {
                overflowed = true;
                break;
            }

            const int glyph_x = pen_x + atlas_padding;
            const int glyph_y = pen_y + atlas_padding;
            std::vector<unsigned char> glyph_bitmap(
                static_cast<std::size_t>(glyph_width) * glyph_height, 0);
            if (glyph_width > 0 && glyph_height > 0)
            {
                stbtt_MakeCodepointBitmap(&info,
                    glyph_bitmap.data(), glyph_width, glyph_height, glyph_width,
                    scale, scale,
                    static_cast<int>(codepoint));
            }
            std::vector<unsigned char> cell_bitmap(
                static_cast<std::size_t>(cell_width) * cell_height, 0);
            for (int y = 0; y < glyph_height; ++y)
            {
                for (int x = 0; x < glyph_width; ++x)
                {
                    cell_bitmap[static_cast<std::size_t>(y + atlas_padding) *
                        cell_width + x + atlas_padding] = glyph_bitmap[
                            static_cast<std::size_t>(y) * glyph_width + x];
                }
            }
            std::vector<unsigned char> cell_sdf;
            BuildSignedDistanceField(cell_bitmap, cell_width, cell_height,
                static_cast<float>(FontAtlas::SdfSpreadPixels()), cell_sdf);
            for (int y = 0; y < cell_height; ++y)
            {
                const std::size_t source_offset = static_cast<std::size_t>(y) * cell_width;
                const std::size_t target_offset = static_cast<std::size_t>(pen_y + y) *
                    atlas_width + pen_x;
                std::copy_n(cell_sdf.data() + source_offset, cell_width,
                    sdf_alpha.data() + target_offset);
            }
            GlyphInfo glyph{};
            glyph.uv = { static_cast<float>(glyph_x) / atlas_width,
                static_cast<float>(glyph_y) / atlas_height,
                static_cast<float>(glyph_width) / atlas_width,
                static_cast<float>(glyph_height) / atlas_height };
            glyph.size = { static_cast<float>(glyph_width), static_cast<float>(glyph_height) };
            glyph.bearing = { static_cast<float>(x0), 0.0f };
            glyph.advance = static_cast<float>(advance) * scale;
            glyph.bake_scale = scale;
            baked[codepoint] = glyph;
            pen_x += cell_width;
            row_height = (std::max)(row_height, cell_height);
        }

        if (overflowed || baked.empty())
        {
            // 日本語などでアトラスが尽きた場合は、隣のグリフを拾う余白を
            // 削って詰めるより、現在のフェイスを無効として fallback に落とす。
            OutputDebugStringA("FontAtlas: glyph atlas is full; using fallback font.\n");
            return false;
        }
        std::vector<std::uint32_t> rgba(static_cast<std::size_t>(atlas_width) * atlas_height);
        for (std::size_t index = 0; index < rgba.size(); ++index)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(sdf_alpha[index]);
            rgba[index] = (a << 24) | 0x00FFFFFFu;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = atlas_width; desc.Height = atlas_height; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{}; init.pSysMem = rgba.data();
        init.SysMemPitch = atlas_width * sizeof(std::uint32_t);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device_->CreateTexture2D(&desc, &init, texture.GetAddressOf()))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = desc.Format; srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        face.texture.Reset();
        if (FAILED(device_->CreateShaderResourceView(texture.Get(), &srv, face.texture.GetAddressOf())))
            return false;
        face.baked_glyphs = std::move(baked);
        face.scaled_glyphs.clear();
        return true;
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
