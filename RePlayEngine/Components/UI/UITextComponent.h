#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

#include <DirectXMath.h>

#include <string>
#include <vector>

namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Components
{

    class UITextComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UITextComponent)

    public:
        enum HorizontalAlign : int
        {
            Left = 0,
            Center = 1,
            Right = 2,
        };

        enum VerticalAlign : int
        {
            Top = 0,
            Middle = 1,
            Bottom = 2,
        };

        struct GlyphQuad
        {
            DirectX::XMFLOAT2 position{ 0.0f, 0.0f };
            DirectX::XMFLOAT2 size{ 0.0f, 0.0f };
            DirectX::XMFLOAT4 uv{ 0.0f, 0.0f, 0.0f, 0.0f };
            int character_index = 0;
            float advance = 0.0f;
        };

        UITextComponent() = default;

        void OnAttach() override;

        std::string text;
        Reflection::AssetReference font;
        float font_size = 24.0f;
        DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float opacity = 1.0f;
        float character_spacing = 0.0f;
        float line_spacing = 1.0f;
        int horizontal_align = Center;
        int vertical_align = Middle;
        bool word_wrap = true;

        Reflection::ComponentReference number_source;
        std::string number_source_property;
        std::string number_format;
        int number_digits = 0;
        float outline_width = 0.0f;
        DirectX::XMFLOAT4 outline_color{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT2 shadow_offset{ 0.0f, 0.0f };
        DirectX::XMFLOAT4 shadow_color{ 0.0f, 0.0f, 0.0f, 0.0f };

        // PropertyLink の評価後に、指定された数値を Text へ反映する。
        void UpdateNumberDisplay(const ReplayEngine::Scene::Scene& scene);

        const std::vector<GlyphQuad>& Glyphs() const noexcept { return glyphs_; }
        std::vector<GlyphQuad>& MutableGlyphs() noexcept { return glyphs_; }

        // ---- 拡張点: Text Animator ----------------------------------------
        //
        // 【今は入れていない理由】
        //   Phase 1 は Motion から動かされる側のメッシュ構造だけを確定する段階。
        //   Range Selector、文字単位の Transform、文字間隔の時間変化は Phase 8 で扱う。
        //
        // 【入れるときにここへ足す】
        //   ・Text Animator が GlyphQuad::character_index を範囲選択に使う
        //   ・UIRenderer::EmitText の直前で glyph ごとの Position/Scale/Color を合成する
        //   ・改行や UTF-8 複数バイト文字も character_index は表示文字単位で進める
        //
        // 【壊してはいけない前提】
        //   ・UIText は 1 文字 1 クアッドで描く
        //   ・バイト位置ではなく表示文字の index を保存する

    private:
        std::vector<GlyphQuad> glyphs_;
    };
}
