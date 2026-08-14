#pragma once

#include "CSharpScriptBackend.h"

#include "../Core/ScriptValue.h"
#include "../../Runtime/API/RuntimeContext.h"
#include "../../Runtime/Core/RuntimeResult.h"
#include "../../Runtime/Events/EventBus.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <deque>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif
#include <string_view>

#include "CSharpScriptBackendHostInternal.h"

namespace ReplayEngine::Scripting::CSharp::Detail
{
        struct NativeEventSubscription final
        {
            Runtime::ScopedSubscription token;
            std::deque<std::string> pending;
        };

        struct NativeApiTable final
        {
            find_game_object_callback find_game_object = nullptr;
            is_game_object_valid_callback is_game_object_valid = nullptr;
            get_vec3_callback get_local_position = nullptr;
            set_vec3_callback set_local_position = nullptr;
            get_vec3_callback get_local_rotation_euler = nullptr;
            set_vec3_callback set_local_rotation_euler = nullptr;
            get_vec3_callback get_local_scale = nullptr;
            set_vec3_callback set_local_scale = nullptr;
            get_component_callback get_component = nullptr;
            destroy_game_object_callback destroy_game_object = nullptr;
            destroy_component_callback destroy_component = nullptr;
            instantiate_callback instantiate = nullptr;
            scene_callback load_scene = nullptr;
            noarg_scene_callback reload_scene = nullptr;
            noarg_scene_callback return_to_previous_scene = nullptr;
            subscribe_event_callback subscribe_event = nullptr;
            unsubscribe_event_callback unsubscribe_event = nullptr;
            poll_event_callback poll_event = nullptr;

            // v2 additions. ABI table is mirrored in RePlayEngine.Managed/NativeBridge.cs.
            scene_callback trigger_scene_flow = nullptr;
            scene_flow_bool_callback set_scene_flow_bool = nullptr;
            scene_flow_int_callback set_scene_flow_int = nullptr;
            scene_flow_float_callback set_scene_flow_float = nullptr;
            raycast_callback raycast = nullptr;

            // v3 additions. MotionPlayer API は table 末尾へだけ足す。
            find_motion_player_callback find_motion_player = nullptr;
            motion_component_callback motion_play = nullptr;
            motion_component_float_callback motion_play_from = nullptr;
            motion_component_callback motion_pause = nullptr;
            motion_component_callback motion_resume = nullptr;
            motion_component_callback motion_stop = nullptr;
            motion_component_callback motion_reverse = nullptr;
            motion_component_float_callback motion_set_time = nullptr;
            motion_component_float_callback motion_set_speed = nullptr;
            motion_component_float_callback motion_set_weight = nullptr;
            motion_get_bool_callback motion_is_playing = nullptr;
            motion_get_float_callback motion_get_time = nullptr;
            motion_get_float_callback motion_get_duration = nullptr;

            // v4 additions. Runtime Service / Component / Runtime UI API。
            available_callback input_available = nullptr;
            input_bool_callback input_held = nullptr;
            input_bool_callback input_pressed = nullptr;
            input_bool_callback input_released = nullptr;
            input_axis_callback input_axis = nullptr;
            input_pointer_callback input_pointer_delta_x = nullptr;
            input_pointer_callback input_pointer_delta_y = nullptr;
            available_callback audio_available = nullptr;
            audio_play_callback audio_play = nullptr;
            audio_stop_callback audio_stop = nullptr;
            audio_update_callback audio_update = nullptr;
            available_callback save_available = nullptr;
            save_set_bool_callback save_set_bool = nullptr;
            save_set_int_callback save_set_int = nullptr;
            save_set_double_callback save_set_double = nullptr;
            save_set_string_callback save_set_string = nullptr;
            save_get_bool_callback save_get_bool = nullptr;
            save_get_int_callback save_get_int = nullptr;
            save_get_double_callback save_get_double = nullptr;
            save_get_string_callback save_get_string = nullptr;
            save_has_key_callback save_has_key = nullptr;
            save_key_callback save_delete_key = nullptr;
            save_slot_callback save_game = nullptr;
            save_slot_callback load_game = nullptr;
            save_slot_callback delete_save = nullptr;
            available_callback runtime_ui_available = nullptr;
            create_ui_element_callback create_ui_element = nullptr;
            set_ui_text_callback set_ui_text = nullptr;
            get_ui_text_callback get_ui_text = nullptr;
            set_ui_color_callback set_ui_image_color = nullptr;
            set_ui_rect_callback set_ui_rect = nullptr;
            set_ui_button_callback set_ui_button_interactable = nullptr;
            add_component_callback add_component = nullptr;
            get_components_callback get_components = nullptr;
            set_component_enabled_callback set_component_enabled = nullptr;
            get_component_enabled_callback get_component_enabled = nullptr;
        };
    inline int StatusCode(RuntimeStatus status) noexcept
    {
        return Runtime::ToErrorCode(status);
    }

    inline RuntimeStatus ContextUnavailable() noexcept
    {
        return RuntimeStatus::ServiceUnavailable;
    }

    inline std::string CString(const char* text)
    {
        return text != nullptr ? std::string(text) : std::string();
    }

    inline int WriteNativeText(std::string_view text, char* output, int output_capacity)
    {
        if (output == nullptr || output_capacity <= 0)
        {
            return StatusCode(RuntimeStatus::InvalidArgument);
        }

        const std::size_t capacity =
            static_cast<std::size_t>(output_capacity);
        const std::size_t count = (std::min)(text.size(), capacity - 1);
        if (count != 0)
        {
            std::memcpy(output, text.data(), count);
        }
        output[count] = '\0';
        return StatusCode(RuntimeStatus::Ok);
    }

    extern RuntimeContext* g_runtime_context;
    extern std::unordered_map<std::uint64_t, NativeEventSubscription> g_event_subscriptions;
    extern std::uint64_t g_next_event_subscription;

    int NativeFindGameObject(std::uint64_t object_id,
        Runtime::ObjectHandle* out) noexcept;
    int NativeIsGameObjectValid(Runtime::ObjectHandle handle) noexcept;
    int NativeGetLocalPosition(Runtime::ObjectHandle handle,
        DirectX::XMFLOAT3* out) noexcept;
    int NativeSetLocalPosition(Runtime::ObjectHandle handle,
        DirectX::XMFLOAT3 value) noexcept;
    int NativeGetLocalRotationEuler(Runtime::ObjectHandle handle,
        DirectX::XMFLOAT3* out) noexcept;
    int NativeSetLocalRotationEuler(Runtime::ObjectHandle handle,
        DirectX::XMFLOAT3 value) noexcept;
    int NativeGetLocalScale(Runtime::ObjectHandle handle,
        DirectX::XMFLOAT3* out) noexcept;
    int NativeSetLocalScale(Runtime::ObjectHandle handle,
        DirectX::XMFLOAT3 value) noexcept;
    int NativeGetComponent(Runtime::ObjectHandle handle, std::uint32_t type_id,
        Runtime::ComponentHandle* out) noexcept;
    int NativeDestroyGameObject(Runtime::ObjectHandle handle) noexcept;
    int NativeDestroyComponent(Runtime::ComponentHandle handle) noexcept;
    int NativeInstantiate(const char* asset_guid, DirectX::XMFLOAT3 position,
        DirectX::XMFLOAT3 rotation_euler, DirectX::XMFLOAT3 scale,
        Runtime::ObjectHandle parent, Runtime::ObjectHandle* out) noexcept;
    int NativeLoadScene(const char* asset_guid) noexcept;
    int NativeReloadScene() noexcept;
    int NativeReturnToPreviousScene() noexcept;
    int NativeTriggerSceneFlow(const char* event_name) noexcept;
    int NativeSetSceneFlowBool(const char* key, int value) noexcept;
    int NativeSetSceneFlowInt(const char* key, std::int64_t value) noexcept;
    int NativeSetSceneFlowFloat(const char* key, double value) noexcept;
    int NativeRaycast(DirectX::XMFLOAT3 origin, DirectX::XMFLOAT3 direction,
        float max_distance, int layer, int mask, Runtime::ObjectHandle ignore,
        NativeRaycastHit* out) noexcept;
    int NativeFindMotionPlayer(Runtime::ObjectHandle owner, const char* key,
        Runtime::ComponentHandle* out) noexcept;
    int NativeMotionPlay(Runtime::ComponentHandle player) noexcept;
    int NativeMotionPlayFrom(Runtime::ComponentHandle player, float seconds) noexcept;
    int NativeMotionPause(Runtime::ComponentHandle player) noexcept;
    int NativeMotionResume(Runtime::ComponentHandle player) noexcept;
    int NativeMotionStop(Runtime::ComponentHandle player) noexcept;
    int NativeMotionReverse(Runtime::ComponentHandle player) noexcept;
    int NativeMotionSetTime(Runtime::ComponentHandle player, float seconds) noexcept;
    int NativeMotionSetSpeed(Runtime::ComponentHandle player, float speed) noexcept;
    int NativeMotionSetWeight(Runtime::ComponentHandle player, float weight) noexcept;
    int NativeMotionIsPlaying(Runtime::ComponentHandle player, int* out) noexcept;
    int NativeMotionGetTime(Runtime::ComponentHandle player, float* out) noexcept;
    int NativeMotionGetDuration(Runtime::ComponentHandle player, float* out) noexcept;
    int NativeSubscribeEvent(std::uint64_t high, std::uint64_t low,
        Runtime::ObjectHandle owner, std::uint64_t* out) noexcept;
    int NativeUnsubscribeEvent(std::uint64_t subscription) noexcept;
    int NativePollEvent(std::uint64_t subscription, char* output,
        int output_capacity) noexcept;

    int NativeInputAvailable() noexcept;
    int NativeInputHeld(const char* action, int player_slot, int* out) noexcept;
    int NativeInputPressed(const char* action, int player_slot, int* out) noexcept;
    int NativeInputReleased(const char* action, int player_slot, int* out) noexcept;
    int NativeInputAxis(const char* axis, int player_slot, float* out) noexcept;
    int NativeInputPointerDeltaX(float* out) noexcept;
    int NativeInputPointerDeltaY(float* out) noexcept;
    int NativeAudioAvailable() noexcept;
    int NativeAudioPlay(const char* clip_path, int loop, float volume, float pitch,
        int spatial_mode, DirectX::XMFLOAT3 position, float min_distance,
        float max_distance, std::uint64_t* out) noexcept;
    int NativeAudioStop(std::uint64_t voice) noexcept;
    int NativeAudioUpdate(std::uint64_t voice, const char* clip_path, int loop,
        float volume, float pitch, int spatial_mode, DirectX::XMFLOAT3 position,
        float min_distance, float max_distance) noexcept;
    int NativeSaveAvailable() noexcept;
    int NativeSaveSetBool(const char* slot, const char* key, int value) noexcept;
    int NativeSaveSetInt(const char* slot, const char* key, std::int64_t value) noexcept;
    int NativeSaveSetDouble(const char* slot, const char* key, double value) noexcept;
    int NativeSaveSetString(const char* slot, const char* key, const char* value) noexcept;
    int NativeSaveGetBool(const char* slot, const char* key, int* out) noexcept;
    int NativeSaveGetInt(const char* slot, const char* key, std::int64_t* out) noexcept;
    int NativeSaveGetDouble(const char* slot, const char* key, double* out) noexcept;
    int NativeSaveGetString(const char* slot, const char* key, char* output,
        int output_capacity) noexcept;
    int NativeSaveHasKey(const char* slot, const char* key, int* out) noexcept;
    int NativeSaveDeleteKey(const char* slot, const char* key) noexcept;
    int NativeSaveGame(const char* slot) noexcept;
    int NativeLoadGame(const char* slot) noexcept;
    int NativeDeleteSave(const char* slot) noexcept;
    int NativeRuntimeUIAvailable() noexcept;
    int NativeCreateUIElement(const char* name, Runtime::ObjectHandle parent,
        Runtime::ObjectHandle* out) noexcept;
    int NativeSetUIText(Runtime::ObjectHandle object, const char* text) noexcept;
    int NativeGetUIText(Runtime::ObjectHandle object, char* output,
        int output_capacity) noexcept;
    int NativeSetUIImageColor(Runtime::ObjectHandle object,
        DirectX::XMFLOAT4 color) noexcept;
    int NativeSetUIRect(Runtime::ObjectHandle object, DirectX::XMFLOAT2 position,
        DirectX::XMFLOAT2 size, DirectX::XMFLOAT2 scale, float rotation,
        int sort_order) noexcept;
    int NativeSetUIButtonInteractable(Runtime::ObjectHandle object, int interactable) noexcept;
    int NativeAddComponent(Runtime::ObjectHandle object, std::uint32_t type_id,
        Runtime::ComponentHandle* out) noexcept;
    int NativeGetComponents(Runtime::ObjectHandle object, std::uint32_t type_id,
        Runtime::ComponentHandle* output, int capacity, int* count) noexcept;
    int NativeSetComponentEnabled(Runtime::ComponentHandle component, int enabled) noexcept;
    int NativeGetComponentEnabled(Runtime::ComponentHandle component, int* out) noexcept;

    NativeApiTable MakeNativeApiTable() noexcept;
}
