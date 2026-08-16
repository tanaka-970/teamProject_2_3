#pragma once

#include "../../Object/Component/Component.h"
#include "../../Reflection/Property/References.h"

#include <DirectXMath.h>

#include <cstdint>
#include <string>

namespace ReplayEngine::Components
{
    // Runtime の編集文字列を既存 UITextComponent へ投影する入力欄。
    // 描画用 Glyph は UITextComponent / FontAtlas の既存経路をそのまま使う。
    class UIInputFieldComponent final : public Core::Component
    {
        REPLAY_COMPONENT_BODY(UIInputFieldComponent)
    public:
        UIInputFieldComponent() = default;

        void OnAttach() override;
        void OnPropertyChanged(const char* property_name) override;

        // 永続値。
        std::string text;
        Reflection::ComponentReference text_target;
        std::string placeholder = "Input text...";
        DirectX::XMFLOAT4 text_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 placeholder_color{ 0.65f, 0.65f, 0.65f, 0.75f };
        DirectX::XMFLOAT4 selection_color{ 0.20f, 0.50f, 1.00f, 0.45f };
        DirectX::XMFLOAT4 caret_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float caret_width = 1.5f;
        float caret_blink_seconds = 0.5f;
        int max_characters = 0; // 0 = 無制限
        bool password = false;
        bool read_only = false;

        // Runtime 派生値。Scene には保存しない。
        int caret_index = 0;            // UTF-8 byte index ではなく表示文字 index
        int selection_anchor = 0;       // caret と同じなら選択なし
        bool ime_composing = false;
        std::string ime_composition;
        std::uint16_t pending_high_surrogate = 0;

        bool HasSelection() const noexcept { return caret_index != selection_anchor; }
        int SelectionStart() const noexcept;
        int SelectionEnd() const noexcept;
        int CharacterCount() const noexcept;

        // text / placeholder / password / IME composition を UIText へ反映する。
        void RefreshVisual();
    };
}
