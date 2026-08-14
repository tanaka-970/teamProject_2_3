#include "CSharpScriptBackendNativeInternal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>

namespace ReplayEngine::Scripting::CSharp::Detail
{
    namespace
    {
        int WriteTextExact(std::string_view text, char* output, int capacity) noexcept
        {
            if (output == nullptr || capacity <= 0 ||
                text.size() + 1 > static_cast<std::size_t>(capacity))
                return StatusCode(RuntimeStatus::InvalidArgument);
            if (!text.empty()) std::memcpy(output, text.data(), text.size());
            output[text.size()] = '\0';
            return StatusCode(RuntimeStatus::Ok);
        }

        int Available(RuntimeContext* context, bool available) noexcept
        {
            return context != nullptr && available ? 1 : 0;
        }
    }

    // ---- Input --------------------------------------------------------------

    int NativeInputAvailable() noexcept
    {
        return Available(g_runtime_context,
            g_runtime_context != nullptr && g_runtime_context->InputActionAvailable());
    }

    int NativeInputHeld(const char* action, int player_slot, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status = g_runtime_context->InputHeld(
            CString(action), player_slot, value);
        if (status == RuntimeStatus::Ok) *out = value ? 1 : 0;
        return StatusCode(status);
    }

    int NativeInputPressed(const char* action, int player_slot, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status = g_runtime_context->InputPressed(
            CString(action), player_slot, value);
        if (status == RuntimeStatus::Ok) *out = value ? 1 : 0;
        return StatusCode(status);
    }

    int NativeInputReleased(const char* action, int player_slot, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status = g_runtime_context->InputReleased(
            CString(action), player_slot, value);
        if (status == RuntimeStatus::Ok) *out = value ? 1 : 0;
        return StatusCode(status);
    }

    int NativeInputAxis(const char* axis, int player_slot, float* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0.0f;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->InputAxis(CString(axis), player_slot, *out));
    }

    int NativeInputPointerDeltaX(float* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0.0f;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->InputPointerDeltaX(*out));
    }

    int NativeInputPointerDeltaY(float* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0.0f;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->InputPointerDeltaY(*out));
    }

    // ---- Audio --------------------------------------------------------------

    int NativeAudioAvailable() noexcept
    {
        return Available(g_runtime_context,
            g_runtime_context != nullptr && g_runtime_context->AudioAvailable());
    }

    int NativeAudioPlay(const char* clip_path, int loop, float volume, float pitch,
        int spatial_mode, DirectX::XMFLOAT3 position, float min_distance,
        float max_distance, std::uint64_t* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->PlayAudio(CString(clip_path), loop != 0,
            volume, pitch, spatial_mode, position, min_distance, max_distance, *out));
    }

    int NativeAudioStop(std::uint64_t voice) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->StopAudio(voice));
    }

    int NativeAudioUpdate(std::uint64_t voice, const char* clip_path, int loop,
        float volume, float pitch, int spatial_mode, DirectX::XMFLOAT3 position,
        float min_distance, float max_distance) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->UpdateAudio(voice, CString(clip_path),
            loop != 0, volume, pitch, spatial_mode, position, min_distance, max_distance));
    }

    // ---- SaveGame -----------------------------------------------------------

    int NativeSaveAvailable() noexcept
    {
        return Available(g_runtime_context,
            g_runtime_context != nullptr && g_runtime_context->SaveGameAvailable());
    }

    int NativeSaveSetBool(const char* slot, const char* key, int value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SaveBool(CString(slot), CString(key), value != 0));
    }

    int NativeSaveSetInt(const char* slot, const char* key, std::int64_t value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SaveInt(CString(slot), CString(key), value));
    }

    int NativeSaveSetDouble(const char* slot, const char* key, double value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SaveDouble(CString(slot), CString(key), value));
    }

    int NativeSaveSetString(const char* slot, const char* key, const char* value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SaveString(CString(slot), CString(key), CString(value)));
    }

    int NativeSaveGetBool(const char* slot, const char* key, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status = g_runtime_context->LoadBool(
            CString(slot), CString(key), value);
        if (status == RuntimeStatus::Ok) *out = value ? 1 : 0;
        return StatusCode(status);
    }

    int NativeSaveGetInt(const char* slot, const char* key, std::int64_t* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->LoadInt(CString(slot), CString(key), *out));
    }

    int NativeSaveGetDouble(const char* slot, const char* key, double* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0.0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->LoadDouble(CString(slot), CString(key), *out));
    }

    int NativeSaveGetString(const char* slot, const char* key,
        char* output, int output_capacity) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        std::string value;
        const RuntimeStatus status = g_runtime_context->LoadString(
            CString(slot), CString(key), value);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        return WriteTextExact(value, output, output_capacity);
    }

    int NativeSaveHasKey(const char* slot, const char* key, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status = g_runtime_context->HasSaveKey(
            CString(slot), CString(key), value);
        if (status == RuntimeStatus::Ok) *out = value ? 1 : 0;
        return StatusCode(status);
    }

    int NativeSaveDeleteKey(const char* slot, const char* key) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->DeleteSaveKey(CString(slot), CString(key)));
    }

    int NativeSaveGame(const char* slot) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SaveGame(CString(slot)));
    }

    int NativeLoadGame(const char* slot) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->LoadGame(CString(slot)));
    }

    int NativeDeleteSave(const char* slot) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->DeleteSave(CString(slot)));
    }

    // ---- Runtime UI ---------------------------------------------------------

    int NativeRuntimeUIAvailable() noexcept
    {
        return Available(g_runtime_context,
            g_runtime_context != nullptr && g_runtime_context->RuntimeUIAvailable());
    }

    int NativeCreateUIElement(const char* name, Runtime::ObjectHandle parent,
        Runtime::ObjectHandle* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = Runtime::ObjectHandle::None();
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->CreateUIElement(CString(name), parent, *out));
    }

    int NativeSetUIText(Runtime::ObjectHandle object, const char* text) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetUIText(object, CString(text)));
    }

    int NativeGetUIText(Runtime::ObjectHandle object, char* output,
        int output_capacity) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        std::string text;
        const RuntimeStatus status = g_runtime_context->GetUIText(object, text);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        return WriteTextExact(text, output, output_capacity);
    }

    int NativeSetUIImageColor(Runtime::ObjectHandle object,
        DirectX::XMFLOAT4 color) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetUIImageColor(object, color));
    }

    int NativeSetUIRect(Runtime::ObjectHandle object, DirectX::XMFLOAT2 position,
        DirectX::XMFLOAT2 size, DirectX::XMFLOAT2 scale, float rotation,
        int sort_order) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetUIRect(object, position, size,
            scale, rotation, sort_order));
    }

    int NativeSetUIButtonInteractable(Runtime::ObjectHandle object, int interactable) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetUIButtonInteractable(object,
            interactable != 0));
    }

    // ---- Runtime Component --------------------------------------------------

    int NativeAddComponent(Runtime::ObjectHandle object, std::uint32_t type_id,
        Runtime::ComponentHandle* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = Runtime::ComponentHandle::None();
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->AddComponent(object, type_id, *out));
    }

    int NativeGetComponents(Runtime::ObjectHandle object, std::uint32_t type_id,
        Runtime::ComponentHandle* output, int capacity, int* count) noexcept
    {
        if (count == nullptr || capacity < 0 || (capacity > 0 && output == nullptr))
            return StatusCode(RuntimeStatus::InvalidArgument);
        *count = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

        if (!g_runtime_context->IsValid(object))
            return StatusCode(RuntimeStatus::InvalidHandle);
        const std::vector<Runtime::ComponentHandle> values =
            g_runtime_context->GetComponents(object, type_id);
        if (values.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            return StatusCode(RuntimeStatus::InvalidArgument);
        const int required = static_cast<int>(values.size());
        *count = required;
        if (capacity == 0 && required == 0) return StatusCode(RuntimeStatus::Ok);
        if (capacity < required) return StatusCode(RuntimeStatus::InvalidArgument);
        std::copy(values.begin(), values.end(), output);
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeSetComponentEnabled(Runtime::ComponentHandle component, int enabled) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetComponentEnabled(component, enabled != 0));
    }

    int NativeGetComponentEnabled(Runtime::ComponentHandle component, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status = g_runtime_context->IsComponentEnabled(component, value);
        if (status == RuntimeStatus::Ok) *out = value ? 1 : 0;
        return StatusCode(status);
    }
}
