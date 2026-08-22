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
            table.input_available = &NativeInputAvailable;
            table.input_held = &NativeInputHeld;
            table.input_pressed = &NativeInputPressed;
            table.input_released = &NativeInputReleased;
            table.input_axis = &NativeInputAxis;
            table.input_pointer_delta_x = &NativeInputPointerDeltaX;
            table.input_pointer_delta_y = &NativeInputPointerDeltaY;
            table.audio_available = &NativeAudioAvailable;
            table.audio_play = &NativeAudioPlay;
            table.audio_stop = &NativeAudioStop;
            table.audio_update = &NativeAudioUpdate;
            table.save_available = &NativeSaveAvailable;
            table.save_set_bool = &NativeSaveSetBool;
            table.save_set_int = &NativeSaveSetInt;
            table.save_set_double = &NativeSaveSetDouble;
            table.save_set_string = &NativeSaveSetString;
            table.save_get_bool = &NativeSaveGetBool;
            table.save_get_int = &NativeSaveGetInt;
            table.save_get_double = &NativeSaveGetDouble;
            table.save_get_string = &NativeSaveGetString;
            table.save_has_key = &NativeSaveHasKey;
            table.save_delete_key = &NativeSaveDeleteKey;
            table.save_game = &NativeSaveGame;
            table.load_game = &NativeLoadGame;
            table.delete_save = &NativeDeleteSave;
            table.runtime_ui_available = &NativeRuntimeUIAvailable;
            table.create_ui_element = &NativeCreateUIElement;
            table.set_ui_text = &NativeSetUIText;
            table.get_ui_text = &NativeGetUIText;
            table.set_ui_image_color = &NativeSetUIImageColor;
            table.set_ui_rect = &NativeSetUIRect;
            table.set_ui_button_interactable = &NativeSetUIButtonInteractable;
            table.add_component = &NativeAddComponent;
            table.get_components = &NativeGetComponents;
            table.set_component_enabled = &NativeSetComponentEnabled;
            table.get_component_enabled = &NativeGetComponentEnabled;
            table.get_script_bool = &NativeGetScriptBool;
            table.set_script_bool = &NativeSetScriptBool;
            table.get_script_int = &NativeGetScriptInt;
            table.set_script_int = &NativeSetScriptInt;
            table.get_script_double = &NativeGetScriptDouble;
            table.set_script_double = &NativeSetScriptDouble;
            table.get_script_string = &NativeGetScriptString;
            table.set_script_string = &NativeSetScriptString;
            table.ui_get_focus = &NativeGetUIFocus;
            table.ui_set_focus = &NativeSetUIFocus;
            table.ui_find_focus = &NativeFindUIFocus;
            table.publish_event = &NativePublishEvent;

            table.log_info = &NativeLogInfo;
            table.log_warning = &NativeLogWarning;
            table.log_error = &NativeLogError;
            table.create_game_object = &NativeCreateGameObject;
            table.get_world_position = &NativeGetWorldPosition;
            table.set_parent = &NativeSetParent;
            table.get_parent = &NativeGetParent;
            table.get_children = &NativeGetChildren;
            table.get_name = &NativeGetName;
            table.set_name = &NativeSetName;
            table.get_game_object_enabled = &NativeGetGameObjectEnabled;
            table.set_game_object_enabled = &NativeSetGameObjectEnabled;

            table.query_ground = &NativeQueryGround;
            table.sweep_sphere = &NativeSweepSphere;
            table.instantiate_prefab_deferred = &NativeInstantiatePrefabDeferred;
            table.flush_deferred_operations = &NativeFlushDeferredOperations;
            table.pending_deferred_operation_count = &NativePendingDeferredOperationCount;
            table.has_component = &NativeHasComponent;
            table.get_time_scale = &NativeGetTimeScale;
            table.get_scene_transition_in_progress = &NativeGetSceneTransitionInProgress;
            table.physics_available = &NativePhysicsAvailable;
            table.scene_flow_available = &NativeSceneFlowAvailable;

            table.poll_event_with_payload = &NativePollEventWithPayload;
            table.publish_event_with_payload = &NativePublishEventWithPayload;
            table.find_game_object_by_name = &NativeFindGameObjectByName;

            table.component_type_id = &NativeComponentTypeId;
            table.get_component_type_name = &NativeGetComponentTypeName;
            table.get_component_property_bool = &NativeGetComponentPropertyBool;
            table.set_component_property_bool = &NativeSetComponentPropertyBool;
            table.get_component_property_int = &NativeGetComponentPropertyInt;
            table.set_component_property_int = &NativeSetComponentPropertyInt;
            table.get_component_property_double = &NativeGetComponentPropertyDouble;
            table.set_component_property_double = &NativeSetComponentPropertyDouble;
            table.get_component_property_string = &NativeGetComponentPropertyString;
            table.set_component_property_string = &NativeSetComponentPropertyString;
            table.get_component_property_vec2 = &NativeGetComponentPropertyVec2;
            table.set_component_property_vec2 = &NativeSetComponentPropertyVec2;
            table.get_component_property_vec3 = &NativeGetComponentPropertyVec3;
            table.set_component_property_vec3 = &NativeSetComponentPropertyVec3;
            table.get_component_property_vec4 = &NativeGetComponentPropertyVec4;
            table.set_component_property_vec4 = &NativeSetComponentPropertyVec4;
            table.set_world_position = &NativeSetWorldPosition;
            table.get_world_rotation = &NativeGetWorldRotation;
            table.set_world_rotation = &NativeSetWorldRotation;
            table.get_world_scale = &NativeGetWorldScale;
            table.set_world_scale = &NativeSetWorldScale;
            table.get_world_axes = &NativeGetWorldAxes;
            table.look_at = &NativeLookAt;
            table.rigidbody_add_force = &NativeRigidbodyAddForce;
            table.rigidbody_add_torque = &NativeRigidbodyAddTorque;
            table.rigidbody_clear_forces = &NativeRigidbodyClearForces;
            table.rigidbody_teleport = &NativeRigidbodyTeleport;
            table.rigidbody_get_linear_velocity = &NativeRigidbodyGetLinearVelocity;
            table.rigidbody_set_linear_velocity = &NativeRigidbodySetLinearVelocity;
            table.rigidbody_get_angular_velocity = &NativeRigidbodyGetAngularVelocity;
            table.rigidbody_set_angular_velocity = &NativeRigidbodySetAngularVelocity;

            // 自己記述ヘッダー。C# 側はこれを見て表の食い違いをその場で弾く。
            table.header.abi_version = kNativeApiAbiVersion;
            table.header.struct_size = static_cast<std::uint32_t>(sizeof(NativeApiTable));
            table.header.entry_count = static_cast<std::uint32_t>(
                (sizeof(NativeApiTable) - sizeof(NativeApiHeader)) / sizeof(void*));
            table.header.reserved = 0;
            return table;
        }

}
