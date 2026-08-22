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

        // 関数ポインタ表の互換番号。**末尾へ関数を足したら必ず 1 上げる。**
        // C# 側の NativeBridge.NativeApiAbiVersion と一致していないと表を拒否する。
        inline constexpr std::uint32_t kNativeApiAbiVersion = 10;

        // 表の先頭に必ず置く自己記述ヘッダー。
        // 順番が 1 つずれても別関数を呼ばずに、その場で不一致として弾くために使う。
        struct NativeApiHeader final
        {
            std::uint32_t abi_version = kNativeApiAbiVersion;
            std::uint32_t struct_size = 0;
            std::uint32_t entry_count = 0;
            std::uint32_t reserved = 0;
        };

        struct NativeApiTable final
        {
            NativeApiHeader header{};

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

            // v5 additions. Must remain mirrored at the tail of NativeBridge.NativeApi.
            get_script_bool_callback get_script_bool = nullptr;
            set_script_bool_callback set_script_bool = nullptr;
            get_script_int_callback get_script_int = nullptr;
            set_script_int_callback set_script_int = nullptr;
            get_script_double_callback get_script_double = nullptr;
            set_script_double_callback set_script_double = nullptr;
            get_script_string_callback get_script_string = nullptr;
            set_script_string_callback set_script_string = nullptr;
            ui_get_focus_callback ui_get_focus = nullptr;
            ui_set_focus_callback ui_set_focus = nullptr;
            ui_find_focus_callback ui_find_focus = nullptr;
            publish_event_callback publish_event = nullptr;

            // v6 additions. Object / hierarchy / log API. Tail-only and mirrored in C#.
            log_callback log_info = nullptr;
            log_callback log_warning = nullptr;
            log_callback log_error = nullptr;
            create_game_object_callback create_game_object = nullptr;
            get_vec3_callback get_world_position = nullptr;
            set_parent_callback set_parent = nullptr;
            get_parent_callback get_parent = nullptr;
            get_children_callback get_children = nullptr;
            get_name_callback get_name = nullptr;
            set_name_callback set_name = nullptr;
            get_object_enabled_callback get_game_object_enabled = nullptr;
            set_object_enabled_callback set_game_object_enabled = nullptr;

            // v7 additions. Physics / deferred / runtime state. Mirrored at C# tail.
            query_ground_callback query_ground = nullptr;
            sweep_sphere_callback sweep_sphere = nullptr;
            instantiate_deferred_callback instantiate_prefab_deferred = nullptr;
            noarg_scene_callback flush_deferred_operations = nullptr;
            pending_deferred_count_callback pending_deferred_operation_count = nullptr;
            has_component_callback has_component = nullptr;
            get_float_value_callback get_time_scale = nullptr;
            get_bool_value_callback get_scene_transition_in_progress = nullptr;
            available_callback physics_available = nullptr;
            available_callback scene_flow_available = nullptr;

            // v8 additions. Event payload snapshot / publish. Mirrored at C# tail.
            poll_event_payload_callback poll_event_with_payload = nullptr;
            publish_event_payload_callback publish_event_with_payload = nullptr;

            // v9 addition. Name lookup. Mirrored at C# tail.
            find_by_name_callback find_game_object_by_name = nullptr;

            // v10 additions. Component 型・汎用プロパティ・World Transform・Rigidbody。
            component_type_id_callback component_type_id = nullptr;
            component_type_name_callback get_component_type_name = nullptr;
            get_property_bool_callback get_component_property_bool = nullptr;
            set_property_bool_callback set_component_property_bool = nullptr;
            get_property_int_callback get_component_property_int = nullptr;
            set_property_int_callback set_component_property_int = nullptr;
            get_property_double_callback get_component_property_double = nullptr;
            set_property_double_callback set_component_property_double = nullptr;
            get_property_string_callback get_component_property_string = nullptr;
            set_property_string_callback set_component_property_string = nullptr;
            get_property_vec2_callback get_component_property_vec2 = nullptr;
            set_property_vec2_callback set_component_property_vec2 = nullptr;
            get_property_vec3_callback get_component_property_vec3 = nullptr;
            set_property_vec3_callback set_component_property_vec3 = nullptr;
            get_property_vec4_callback get_component_property_vec4 = nullptr;
            set_property_vec4_callback set_component_property_vec4 = nullptr;
            set_vec3_callback set_world_position = nullptr;
            get_vec4_callback get_world_rotation = nullptr;
            set_vec4_callback set_world_rotation = nullptr;
            get_vec3_callback get_world_scale = nullptr;
            set_vec3_callback set_world_scale = nullptr;
            get_world_axes_callback get_world_axes = nullptr;
            look_at_callback look_at = nullptr;
            rigidbody_vec3_callback rigidbody_add_force = nullptr;
            rigidbody_vec3_callback rigidbody_add_torque = nullptr;
            rigidbody_void_callback rigidbody_clear_forces = nullptr;
            rigidbody_teleport_callback rigidbody_teleport = nullptr;
            rigidbody_get_vec3_callback rigidbody_get_linear_velocity = nullptr;
            rigidbody_vec3_callback rigidbody_set_linear_velocity = nullptr;
            rigidbody_get_vec3_callback rigidbody_get_angular_velocity = nullptr;
            rigidbody_vec3_callback rigidbody_set_angular_velocity = nullptr;
        };

        // ヘッダー以降がすべて関数ポインタであることを、表を作る側で必ず確かめる。
        static_assert((sizeof(NativeApiTable) - sizeof(NativeApiHeader)) %
            sizeof(void*) == 0, "NativeApiTable must hold only function pointers");
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
    int NativeFindGameObjectByName(const char* name,
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

    int NativeGetScriptBool(Runtime::ComponentHandle component, const char* field, int* out) noexcept;
    int NativeSetScriptBool(Runtime::ComponentHandle component, const char* field, int value) noexcept;
    int NativeGetScriptInt(Runtime::ComponentHandle component, const char* field, int* out) noexcept;
    int NativeSetScriptInt(Runtime::ComponentHandle component, const char* field, int value) noexcept;
    int NativeGetScriptDouble(Runtime::ComponentHandle component, const char* field, double* out) noexcept;
    int NativeSetScriptDouble(Runtime::ComponentHandle component, const char* field, double value) noexcept;
    int NativeGetScriptString(Runtime::ComponentHandle component, const char* field, char* output,
        int output_capacity) noexcept;
    int NativeSetScriptString(Runtime::ComponentHandle component, const char* field, const char* value) noexcept;
    int NativeGetUIFocus(Runtime::ObjectHandle* out) noexcept;
    int NativeSetUIFocus(Runtime::ObjectHandle object) noexcept;
    int NativeFindUIFocus(Runtime::ObjectHandle from, int direction, Runtime::ObjectHandle* out) noexcept;
    int NativePublishEvent(std::uint64_t high, std::uint64_t low, const char* type_name,
        Runtime::ObjectHandle source, Runtime::ObjectHandle target) noexcept;


    int NativeLogInfo(const char* message, Runtime::ObjectHandle source) noexcept;
    int NativeLogWarning(const char* message, Runtime::ObjectHandle source) noexcept;
    int NativeLogError(const char* message, Runtime::ObjectHandle source) noexcept;
    int NativeCreateGameObject(const char* name, Runtime::ObjectHandle* out) noexcept;
    int NativeGetWorldPosition(Runtime::ObjectHandle handle, DirectX::XMFLOAT3* out) noexcept;
    int NativeSetParent(Runtime::ObjectHandle child, Runtime::ObjectHandle parent,
        int preserve_world_transform) noexcept;
    int NativeGetParent(Runtime::ObjectHandle handle, Runtime::ObjectHandle* out) noexcept;
    int NativeGetChildren(Runtime::ObjectHandle handle, Runtime::ObjectHandle* output,
        int capacity, int* count) noexcept;
    int NativeGetName(Runtime::ObjectHandle handle, char* output, int output_capacity) noexcept;
    int NativeSetName(Runtime::ObjectHandle handle, const char* name) noexcept;
    int NativeGetGameObjectEnabled(Runtime::ObjectHandle handle, int* out) noexcept;
    int NativeSetGameObjectEnabled(Runtime::ObjectHandle handle, int enabled) noexcept;


    int NativeQueryGround(DirectX::XMFLOAT3 origin, float radius, float up_offset,
        float down_distance, float walkable_normal_y, Runtime::ObjectHandle ignore,
        NativeGroundHit* out) noexcept;
    int NativeSweepSphere(DirectX::XMFLOAT3 start, DirectX::XMFLOAT3 end, float radius,
        float maximum_normal_y, Runtime::ObjectHandle ignore,
        NativeSphereSweepHit* out) noexcept;
    int NativeInstantiatePrefabDeferred(const char* asset_guid, DirectX::XMFLOAT3 position,
        DirectX::XMFLOAT3 rotation_euler, DirectX::XMFLOAT3 scale,
        Runtime::ObjectHandle parent) noexcept;
    int NativeFlushDeferredOperations() noexcept;
    int NativePendingDeferredOperationCount(std::uint64_t* out) noexcept;
    int NativeHasComponent(Runtime::ObjectHandle object, std::uint32_t type_id, int* out) noexcept;
    int NativeGetTimeScale(float* out) noexcept;
    int NativeGetSceneTransitionInProgress(int* out) noexcept;
    int NativePhysicsAvailable() noexcept;
    int NativeSceneFlowAvailable() noexcept;


    int NativePollEventWithPayload(std::uint64_t subscription, char* output,
        int output_capacity, int* required_capacity) noexcept;
    int NativePublishEventWithPayload(std::uint64_t high, std::uint64_t low,
        const char* type_name, Runtime::ObjectHandle source, Runtime::ObjectHandle target,
        const char* payload_text) noexcept;

    // v10 Component 型・汎用プロパティ・World Transform・Rigidbody。
    int NativeComponentTypeId(const char* type_name, std::uint32_t* out) noexcept;
    int NativeGetComponentTypeName(Runtime::ComponentHandle handle, char* output,
        int output_capacity) noexcept;
    int NativeGetComponentPropertyBool(Runtime::ComponentHandle handle,
        const char* name, int* out) noexcept;
    int NativeSetComponentPropertyBool(Runtime::ComponentHandle handle,
        const char* name, int value) noexcept;
    int NativeGetComponentPropertyInt(Runtime::ComponentHandle handle,
        const char* name, std::int64_t* out) noexcept;
    int NativeSetComponentPropertyInt(Runtime::ComponentHandle handle,
        const char* name, std::int64_t value) noexcept;
    int NativeGetComponentPropertyDouble(Runtime::ComponentHandle handle,
        const char* name, double* out) noexcept;
    int NativeSetComponentPropertyDouble(Runtime::ComponentHandle handle,
        const char* name, double value) noexcept;
    int NativeGetComponentPropertyString(Runtime::ComponentHandle handle,
        const char* name, char* output, int output_capacity) noexcept;
    int NativeSetComponentPropertyString(Runtime::ComponentHandle handle,
        const char* name, const char* value) noexcept;
    int NativeGetComponentPropertyVec2(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT2* out) noexcept;
    int NativeSetComponentPropertyVec2(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT2 value) noexcept;
    int NativeGetComponentPropertyVec3(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT3* out) noexcept;
    int NativeSetComponentPropertyVec3(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT3 value) noexcept;
    int NativeGetComponentPropertyVec4(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT4* out) noexcept;
    int NativeSetComponentPropertyVec4(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT4 value) noexcept;

    int NativeSetWorldPosition(Runtime::ObjectHandle handle, DirectX::XMFLOAT3 value) noexcept;
    int NativeGetWorldRotation(Runtime::ObjectHandle handle, DirectX::XMFLOAT4* out) noexcept;
    int NativeSetWorldRotation(Runtime::ObjectHandle handle, DirectX::XMFLOAT4 value) noexcept;
    int NativeGetWorldScale(Runtime::ObjectHandle handle, DirectX::XMFLOAT3* out) noexcept;
    int NativeSetWorldScale(Runtime::ObjectHandle handle, DirectX::XMFLOAT3 value) noexcept;
    int NativeGetWorldAxes(Runtime::ObjectHandle handle, DirectX::XMFLOAT3* forward,
        DirectX::XMFLOAT3* right, DirectX::XMFLOAT3* up) noexcept;
    int NativeLookAt(Runtime::ObjectHandle handle, DirectX::XMFLOAT3 target,
        DirectX::XMFLOAT3 world_up) noexcept;

    int NativeRigidbodyAddForce(Runtime::ComponentHandle handle, DirectX::XMFLOAT3 force) noexcept;
    int NativeRigidbodyAddTorque(Runtime::ComponentHandle handle, DirectX::XMFLOAT3 torque) noexcept;
    int NativeRigidbodyClearForces(Runtime::ComponentHandle handle) noexcept;
    int NativeRigidbodyTeleport(Runtime::ComponentHandle handle, DirectX::XMFLOAT3 position,
        DirectX::XMFLOAT3 rotation_euler) noexcept;
    int NativeRigidbodyGetLinearVelocity(Runtime::ComponentHandle handle,
        DirectX::XMFLOAT3* out) noexcept;
    int NativeRigidbodySetLinearVelocity(Runtime::ComponentHandle handle,
        DirectX::XMFLOAT3 value) noexcept;
    int NativeRigidbodyGetAngularVelocity(Runtime::ComponentHandle handle,
        DirectX::XMFLOAT3* out) noexcept;
    int NativeRigidbodySetAngularVelocity(Runtime::ComponentHandle handle,
        DirectX::XMFLOAT3 value) noexcept;

    NativeApiTable MakeNativeApiTable() noexcept;
}
