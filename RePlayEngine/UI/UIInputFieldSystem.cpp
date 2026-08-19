#include "UIInputFieldSystem.h"

#include "UIFocusManager.h"
#include "../Components/UI/UIInputFieldComponent.h"
#include "../Components/UI/UISelectableComponent.h"
#include "../Object/GameObject/GameObject.h"
#include "../Runtime/API/RuntimeContext.h"
#include "../Runtime/Events/EventBus.h"
#include "../Scene/Runtime/Scene.h"

#include <imm.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "imm32.lib")

namespace ReplayEngine::UI
{
    namespace
    {
        using Components::UIInputFieldComponent;
        using Components::UISelectableComponent;

        std::string Utf16ToUtf8(const wchar_t* text, int length)
        {
            if (text == nullptr || length <= 0) return {};
            const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                text, length, nullptr, 0, nullptr, nullptr);
            if (required <= 0) return {};
            std::string result(static_cast<std::size_t>(required), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length,
                result.data(), required, nullptr, nullptr) != required)
            {
                return {};
            }
            return result;
        }

        std::string Utf16ToUtf8(const std::wstring& text)
        {
            return Utf16ToUtf8(text.data(), static_cast<int>(text.size()));
        }

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

        std::size_t ByteOffset(const std::string& text, int character_index) noexcept
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

        UIInputFieldComponent* FocusedInput(Scene::Scene& scene) noexcept
        {
            UISelectableComponent* focus = UIFocusManager::Current(scene);
            if (focus == nullptr || focus->Owner() == nullptr) return nullptr;
            return focus->Owner()->GetComponent<UIInputFieldComponent>();
        }

        void ClampCaret(UIInputFieldComponent& input) noexcept
        {
            const int count = Utf8CharacterCount(input.text);
            input.caret_index = (std::min)((std::max)(input.caret_index, 0), count);
            input.selection_anchor = (std::min)((std::max)(input.selection_anchor, 0), count);
        }

        void EraseSelection(UIInputFieldComponent& input)
        {
            if (!input.HasSelection()) return;
            const int start = input.SelectionStart();
            const int end = input.SelectionEnd();
            const std::size_t b0 = ByteOffset(input.text, start);
            const std::size_t b1 = ByteOffset(input.text, end);
            input.text.erase(b0, b1 - b0);
            input.caret_index = start;
            input.selection_anchor = start;
        }

        void InsertUtf8(UIInputFieldComponent& input, const std::string& insertion)
        {
            if (input.read_only || insertion.empty()) return;
            EraseSelection(input);
            const int incoming = Utf8CharacterCount(insertion);
            int allowed = incoming;
            if (input.max_characters > 0)
            {
                allowed = (std::min)(incoming,
                    (std::max)(0, input.max_characters - Utf8CharacterCount(input.text)));
            }
            if (allowed <= 0) return;
            const std::size_t bytes = ByteOffset(insertion, allowed);
            const std::size_t at = ByteOffset(input.text, input.caret_index);
            input.text.insert(at, insertion.substr(0, bytes));
            input.caret_index += allowed;
            input.selection_anchor = input.caret_index;
        }

        void MoveCaret(UIInputFieldComponent& input, int target, bool extend) noexcept
        {
            const int count = Utf8CharacterCount(input.text);
            target = (std::min)((std::max)(target, 0), count);
            if (!extend) input.selection_anchor = target;
            input.caret_index = target;
        }

        std::wstring ReadClipboardText(HWND hwnd)
        {
            std::wstring result;
            if (!OpenClipboard(hwnd)) return result;
            HANDLE data = GetClipboardData(CF_UNICODETEXT);
            if (data != nullptr)
            {
                const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
                if (text != nullptr)
                {
                    result = text;
                    GlobalUnlock(data);
                }
            }
            CloseClipboard();
            return result;
        }

        void WriteClipboardText(HWND hwnd, const std::string& utf8)
        {
            const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            if (required <= 0 || !OpenClipboard(hwnd)) return;
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE,
                (static_cast<std::size_t>(required) + 1u) * sizeof(wchar_t));
            if (memory == nullptr)
            {
                CloseClipboard();
                return;
            }
            wchar_t* output = static_cast<wchar_t*>(GlobalLock(memory));
            if (output == nullptr)
            {
                GlobalFree(memory);
                CloseClipboard();
                return;
            }
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                static_cast<int>(utf8.size()), output, required);
            output[required] = L'\0';
            GlobalUnlock(memory);
            EmptyClipboard();
            if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) GlobalFree(memory);
            CloseClipboard();
        }

        std::string SelectedText(const UIInputFieldComponent& input)
        {
            if (!input.HasSelection()) return {};
            const std::size_t b0 = ByteOffset(input.text, input.SelectionStart());
            const std::size_t b1 = ByteOffset(input.text, input.SelectionEnd());
            return input.text.substr(b0, b1 - b0);
        }

        std::wstring ImeString(HWND hwnd, LPARAM lparam_flag)
        {
            HIMC context = ImmGetContext(hwnd);
            if (context == nullptr) return {};
            const LONG byte_count = ImmGetCompositionStringW(context,
                static_cast<DWORD>(lparam_flag), nullptr, 0);
            std::wstring result;
            if (byte_count > 0)
            {
                const int wchar_count = byte_count / static_cast<LONG>(sizeof(wchar_t));
                result.resize(static_cast<std::size_t>(wchar_count));
                ImmGetCompositionStringW(context, static_cast<DWORD>(lparam_flag),
                    result.data(), byte_count);
            }
            ImmReleaseContext(hwnd, context);
            return result;
        }

        void Publish(Scene::Scene& scene, UIInputFieldComponent& input,
            Reflection::TypeGUID type, const char* name)
        {
            Runtime::RuntimeContext* runtime = scene.Services().Runtime();
            Core::GameObject* owner = input.Owner();
            if (runtime == nullptr || owner == nullptr) return;
            Runtime::EventRecord record;
            record.type = type;
            record.type_name = name;
            record.source = runtime->Resolver().MakeHandle(owner);
            record.frame_index = runtime->FrameIndex();
            record.payload.Set("text", Reflection::PropertyValue::MakeString(input.text));
            record.payload.Set("input_component",
                Reflection::PropertyValue::MakeUInt64(input.StableID()));
            runtime->Events().Publish(std::move(record));
        }
    }

    bool UIInputFieldSystem::HasFocusedInput(Scene::Scene& scene) noexcept
    {
        UIInputFieldComponent* input = FocusedInput(scene);
        return input != nullptr && input->ActiveInHierarchy();
    }

    bool UIInputFieldSystem::HandleWindowMessage(Scene::Scene& scene, HWND hwnd,
        UINT message, WPARAM wparam, LPARAM lparam)
    {
        UIInputFieldComponent* input = FocusedInput(scene);
        if (input == nullptr || !input->ActiveInHierarchy()) return false;

        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool handled = false;

        if (message == WM_IME_STARTCOMPOSITION)
        {
            input->ime_composing = true;
            input->ime_composition.clear();
            handled = true;
        }
        else if (message == WM_IME_COMPOSITION)
        {
            if ((lparam & GCS_RESULTSTR) != 0)
            {
                const std::string result = Utf16ToUtf8(ImeString(hwnd, GCS_RESULTSTR));
                InsertUtf8(*input, result);
                input->ime_composition.clear();
                input->ime_composing = false;
            }
            else if ((lparam & GCS_COMPSTR) != 0)
            {
                input->ime_composition = Utf16ToUtf8(ImeString(hwnd, GCS_COMPSTR));
                input->ime_composing = true;
            }
            handled = true;
        }
        else if (message == WM_IME_ENDCOMPOSITION)
        {
            input->ime_composing = false;
            input->ime_composition.clear();
            handled = true;
        }
        else if (message == WM_CHAR)
        {
            const wchar_t unit = static_cast<wchar_t>(wparam);
            if (unit == L'\r' || unit == L'\n' || unit == 27)
            {
                // Enter/Escape は WM_KEYDOWN で通知する。WM_CHAR 側でも
                // 発火すると同じ操作でイベントが 2 回届くため、ここでは消費だけする。
            }
            else if (unit == L'\b')
            {
                // WM_KEYDOWN で処理する。
            }
            else if (!control && unit >= 0x20)
            {
                if (unit >= 0xD800 && unit <= 0xDBFF)
                {
                    input->pending_high_surrogate = static_cast<std::uint16_t>(unit);
                }
                else
                {
                    std::wstring sequence;
                    if (unit >= 0xDC00 && unit <= 0xDFFF &&
                        input->pending_high_surrogate != 0)
                    {
                        sequence.push_back(static_cast<wchar_t>(input->pending_high_surrogate));
                        sequence.push_back(unit);
                    }
                    else
                    {
                        sequence.push_back(unit);
                    }
                    input->pending_high_surrogate = 0;
                    InsertUtf8(*input, Utf16ToUtf8(sequence));
                }
            }
            handled = true;
        }
        else if (message == WM_KEYDOWN)
        {
            switch (wparam)
            {
            case VK_LEFT:
                MoveCaret(*input, input->caret_index - 1, shift); handled = true; break;
            case VK_RIGHT:
                MoveCaret(*input, input->caret_index + 1, shift); handled = true; break;
            case VK_HOME:
                MoveCaret(*input, 0, shift); handled = true; break;
            case VK_END:
                MoveCaret(*input, Utf8CharacterCount(input->text), shift); handled = true; break;
            case VK_BACK:
                if (!input->read_only)
                {
                    if (input->HasSelection()) EraseSelection(*input);
                    else if (input->caret_index > 0)
                    {
                        input->selection_anchor = input->caret_index - 1;
                        EraseSelection(*input);
                    }
                }
                handled = true;
                break;
            case VK_DELETE:
                if (!input->read_only)
                {
                    if (input->HasSelection()) EraseSelection(*input);
                    else if (input->caret_index < Utf8CharacterCount(input->text))
                    {
                        input->selection_anchor = input->caret_index + 1;
                        EraseSelection(*input);
                    }
                }
                handled = true;
                break;
            case 'A':
                if (control)
                {
                    input->selection_anchor = 0;
                    input->caret_index = Utf8CharacterCount(input->text);
                    handled = true;
                }
                break;
            case 'C':
                if (control)
                {
                    WriteClipboardText(hwnd, SelectedText(*input));
                    handled = true;
                }
                break;
            case 'X':
                if (control)
                {
                    WriteClipboardText(hwnd, SelectedText(*input));
                    if (!input->read_only) EraseSelection(*input);
                    handled = true;
                }
                break;
            case 'V':
                if (control)
                {
                    InsertUtf8(*input, Utf16ToUtf8(ReadClipboardText(hwnd)));
                    handled = true;
                }
                break;
            case VK_RETURN:
                Publish(scene, *input, Runtime::EngineEvents::InputFieldSubmitted,
                    "InputFieldSubmitted");
                handled = true;
                break;
            case VK_ESCAPE:
                input->ime_composing = false;
                input->ime_composition.clear();
                input->selection_anchor = input->caret_index;
                Publish(scene, *input, Runtime::EngineEvents::InputFieldCanceled,
                    "InputFieldCanceled");
                handled = true;
                break;
            }
        }

        if (handled)
        {
            ClampCaret(*input);
            input->RefreshVisual();
        }
        return handled;
    }

    void UIInputFieldSystem::Refresh(Scene::Scene& scene)
    {
        for (std::size_t i = 0; i < scene.GameObjectCount(); ++i)
        {
            Core::GameObject* object = scene.GameObjectAt(i);
            if (object == nullptr || object->PendingDestroy()) continue;
            if (UIInputFieldComponent* input = object->GetComponent<UIInputFieldComponent>())
            {
                ClampCaret(*input);
                input->RefreshVisual();
            }
        }
    }
}
