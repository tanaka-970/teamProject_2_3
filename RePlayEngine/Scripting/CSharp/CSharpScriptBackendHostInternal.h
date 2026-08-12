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

namespace ReplayEngine::Scripting::CSharp::Detail
{
        using Runtime::RuntimeContext;
        using Runtime::RuntimeStatus;

        constexpr int hdt_load_assembly_and_get_function_pointer = 5;
        constexpr std::size_t text_buffer_size = 16384;
#ifdef _WIN32
        using hostfxr_initialize_for_runtime_config_fn =
            std::int32_t(__cdecl*)(const wchar_t*, const void*, void**);
        using hostfxr_get_runtime_delegate_fn =
            std::int32_t(__cdecl*)(void*, std::int32_t, void**);
        using hostfxr_close_fn = std::int32_t(__cdecl*)(void*);
        using load_assembly_and_get_function_pointer_fn =
            int(__stdcall*)(const wchar_t*, const wchar_t*, const wchar_t*,
                const wchar_t*, void*, void**);

        using set_native_api_fn = int(__cdecl*)(void*);
        using load_assembly_fn = int(__cdecl*)(const char*, char*, int);
        using unload_assembly_fn = int(__cdecl*)(char*, int);
        using describe_type_fn = int(__cdecl*)(std::uint64_t, std::uint64_t, char*, int);
        using create_instance_fn = std::uint64_t(__cdecl*)(std::uint64_t, std::uint64_t,
            Runtime::ObjectHandle, Runtime::ComponentHandle);
        using destroy_instance_fn = int(__cdecl*)(std::uint64_t);
        using invoke_fn = int(__cdecl*)(std::uint64_t, int, float);
        using set_field_fn = int(__cdecl*)(std::uint64_t, const char*, int, const char*);
        using get_field_fn = int(__cdecl*)(std::uint64_t, const char*, char*, int);
        using set_time_fn = int(__cdecl*)(float, float, std::uint64_t);
        using live_instance_count_fn = int(__cdecl*)();
        using last_error_fn = int(__cdecl*)(char*, int, char*, int, int*);

        using find_game_object_callback =
            int(__cdecl*)(std::uint64_t, Runtime::ObjectHandle*);
        using is_game_object_valid_callback =
            int(__cdecl*)(Runtime::ObjectHandle);
        using get_vec3_callback =
            int(__cdecl*)(Runtime::ObjectHandle, DirectX::XMFLOAT3*);
        using set_vec3_callback =
            int(__cdecl*)(Runtime::ObjectHandle, DirectX::XMFLOAT3);
        using get_component_callback =
            int(__cdecl*)(Runtime::ObjectHandle, std::uint32_t, Runtime::ComponentHandle*);
        using destroy_game_object_callback =
            int(__cdecl*)(Runtime::ObjectHandle);
        using destroy_component_callback =
            int(__cdecl*)(Runtime::ComponentHandle);
        using instantiate_callback =
            int(__cdecl*)(const char*, DirectX::XMFLOAT3, DirectX::XMFLOAT3,
                DirectX::XMFLOAT3, Runtime::ObjectHandle, Runtime::ObjectHandle*);
        using scene_callback = int(__cdecl*)(const char*);
        using noarg_scene_callback = int(__cdecl*)();
        using scene_flow_bool_callback = int(__cdecl*)(const char*, int);
        using scene_flow_int_callback = int(__cdecl*)(const char*, std::int64_t);
        using scene_flow_float_callback = int(__cdecl*)(const char*, double);
        struct NativeRaycastHit final
        {
            DirectX::XMFLOAT3 point{};
            DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
            float distance = 0.0f;
            Runtime::ObjectHandle object{};
            std::uint32_t collider_id = 0;
            std::int32_t valid = 0;
        };
        using raycast_callback = int(__cdecl*)(DirectX::XMFLOAT3, DirectX::XMFLOAT3,
            float, int, int, Runtime::ObjectHandle, NativeRaycastHit*);
        using find_motion_player_callback =
            int(__cdecl*)(Runtime::ObjectHandle, const char*, Runtime::ComponentHandle*);
        using motion_component_callback = int(__cdecl*)(Runtime::ComponentHandle);
        using motion_component_float_callback =
            int(__cdecl*)(Runtime::ComponentHandle, float);
        using motion_get_float_callback =
            int(__cdecl*)(Runtime::ComponentHandle, float*);
        using motion_get_bool_callback =
            int(__cdecl*)(Runtime::ComponentHandle, int*);
        using subscribe_event_callback =
            int(__cdecl*)(std::uint64_t, std::uint64_t, Runtime::ObjectHandle,
                std::uint64_t*);
        using unsubscribe_event_callback = int(__cdecl*)(std::uint64_t);
        using poll_event_callback = int(__cdecl*)(std::uint64_t, char*, int);
#endif
}
