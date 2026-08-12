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

    NativeApiTable MakeNativeApiTable() noexcept;
}
