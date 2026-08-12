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
#include "CSharpScriptBackendNativeInternal.h"
namespace ReplayEngine::Scripting::CSharp::Detail
{

// Native API table と共有状態の関数本体

        RuntimeContext* g_runtime_context = nullptr;
        std::unordered_map<std::uint64_t, NativeEventSubscription> g_event_subscriptions;
        std::uint64_t g_next_event_subscription = 1;

        NativeApiTable MakeNativeApiTable() noexcept
        {
            NativeApiTable table;
            table.find_game_object = &NativeFindGameObject;
            table.is_game_object_valid = &NativeIsGameObjectValid;
            table.get_local_position = &NativeGetLocalPosition;
            table.set_local_position = &NativeSetLocalPosition;
            table.get_local_rotation_euler = &NativeGetLocalRotationEuler;
            table.set_local_rotation_euler = &NativeSetLocalRotationEuler;
            table.get_local_scale = &NativeGetLocalScale;
            table.set_local_scale = &NativeSetLocalScale;
            table.get_component = &NativeGetComponent;
            table.destroy_game_object = &NativeDestroyGameObject;
            table.destroy_component = &NativeDestroyComponent;
            table.instantiate = &NativeInstantiate;
            table.load_scene = &NativeLoadScene;
            table.reload_scene = &NativeReloadScene;
            table.return_to_previous_scene = &NativeReturnToPreviousScene;
            table.subscribe_event = &NativeSubscribeEvent;
            table.unsubscribe_event = &NativeUnsubscribeEvent;
            table.poll_event = &NativePollEvent;
            table.trigger_scene_flow = &NativeTriggerSceneFlow;
            table.set_scene_flow_bool = &NativeSetSceneFlowBool;
            table.set_scene_flow_int = &NativeSetSceneFlowInt;
            table.set_scene_flow_float = &NativeSetSceneFlowFloat;
            table.raycast = &NativeRaycast;
            table.find_motion_player = &NativeFindMotionPlayer;
            table.motion_play = &NativeMotionPlay;
            table.motion_play_from = &NativeMotionPlayFrom;
            table.motion_pause = &NativeMotionPause;
            table.motion_resume = &NativeMotionResume;
            table.motion_stop = &NativeMotionStop;
            table.motion_reverse = &NativeMotionReverse;
            table.motion_set_time = &NativeMotionSetTime;
            table.motion_set_speed = &NativeMotionSetSpeed;
            table.motion_set_weight = &NativeMotionSetWeight;
            table.motion_is_playing = &NativeMotionIsPlaying;
            table.motion_get_time = &NativeMotionGetTime;
            table.motion_get_duration = &NativeMotionGetDuration;
            return table;
        }

}
