#include "UIInputFieldComponent.h"

#include "RectTransformComponent.h"
#include "UISelectableComponent.h"
#include "UITextComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Scene/Runtime/Scene.h"

#include <algorithm>

namespace ReplayEngine::Components
{
    namespace
    {
        int Utf8CharacterCount(const std::string& text) noexcept
        {
            int count = 0;
            for (std::size_t i = 0; i < text.size();)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                std::size_t length = 1;
                if ((c & 0xE0u) == 0xC0u) length = 2;
                else if ((c & 0xF0u) == 0xE0u) length = 3;
                else if ((c & 0xF8u) == 0xF0u) length = 4;
                if (i + length > text.size()) length = 1;
                i += length;
                ++count;
            }
            return count;
        }

        std::size_t ByteOffsetForCharacter(const std::string& text, int character_index) noexcept
        {
            if (character_index <= 0) return 0;
            int current = 0;
            std::size_t i = 0;
            while (i < text.size() && current < character_index)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                std::size_t length = 1;
                if ((c & 0xE0u) == 0xC0u) length = 2;
                else if ((c & 0xF0u) == 0xE0u) length = 3;
                else if ((c & 0xF8u) == 0xF0u) length = 4;
                if (i + length > text.size()) length = 1;
                i += length;
                ++current;
            }
            return i;
        }

        UITextComponent* ResolveTextTarget(UIInputFieldComponent& input) noexcept
        {
            Core::GameObject* owner = input.Owner();
            if (owner == nullptr) return nullptr;
            Scene::Scene* scene = owner->GetScene();
            if (input.text_target.IsAssigned() && scene != nullptr)
            {
                Core::GameObject* target_owner = scene->FindGameObjectByID(input.text_target.owner);
                if (target_owner != nullptr)
                {
                    Core::Component* component =
                        target_owner->FindComponentByStableID(input.text_target.component);
                    if (auto* target = dynamic_cast<UITextComponent*>(component)) return target;
                }
            }
            return owner->GetComponent<UITextComponent>();
        }
    }

    void UIInputFieldComponent::OnAttach()
    {
        Core::GameObject* owner = Owner();
        if (owner == nullptr) return;
        owner->AddComponent<RectTransformComponent>();
        owner->AddComponent<UISelectableComponent>();
        UITextComponent* target = owner->AddComponent<UITextComponent>();
        if (target != nullptr && !text_target.IsAssigned())
        {
            text_target.owner = owner->ID();
            text_target.component = target->StableID();
        }
        RefreshVisual();
    }

    void UIInputFieldComponent::OnPropertyChanged(const char* /*property_name*/)
    {
        const int count = CharacterCount();
        caret_index = (std::min)((std::max)(caret_index, 0), count);
        selection_anchor = (std::min)((std::max)(selection_anchor, 0), count);
        RefreshVisual();
    }

    int UIInputFieldComponent::SelectionStart() const noexcept
    {
        return (std::min)(caret_index, selection_anchor);
    }

    int UIInputFieldComponent::SelectionEnd() const noexcept
    {
        return (std::max)(caret_index, selection_anchor);
    }

    int UIInputFieldComponent::CharacterCount() const noexcept
    {
        return Utf8CharacterCount(text);
    }

    void UIInputFieldComponent::RefreshVisual()
    {
        UITextComponent* target = ResolveTextTarget(*this);
        if (target == nullptr) return;

        std::string visible;
        const int count = CharacterCount();
        if (password)
        {
            visible.assign(static_cast<std::size_t>((std::max)(0, count)), '*');
        }
        else
        {
            visible = text;
        }

        if (ime_composing && !ime_composition.empty() && !password)
        {
            const std::size_t byte_offset = ByteOffsetForCharacter(visible, caret_index);
            visible.insert(byte_offset, ime_composition);
        }

        if (visible.empty() && !ime_composing)
        {
            // Unity-style: 空の間は focus 中も placeholder を残し、
            // 最初の文字/IME composition が入った時点で本文へ切り替える。
            target->text = placeholder;
            target->color = placeholder_color;
        }
        else
        {
            target->text = visible;
            target->color = text_color;
        }
    }
}
