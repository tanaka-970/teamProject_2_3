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

namespace ReplayEngine::Scripting::CSharp
{
    namespace
    {
        using Runtime::RuntimeContext;
        using Runtime::RuntimeStatus;

        constexpr int hdt_load_assembly_and_get_function_pointer = 5;
        constexpr std::size_t text_buffer_size = 16384;

        RuntimeContext* g_runtime_context = nullptr;

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
        using subscribe_event_callback =
            int(__cdecl*)(std::uint64_t, std::uint64_t, Runtime::ObjectHandle,
                std::uint64_t*);
        using unsubscribe_event_callback = int(__cdecl*)(std::uint64_t);
        using poll_event_callback = int(__cdecl*)(std::uint64_t, char*, int);
#endif

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
        };

        int StatusCode(RuntimeStatus status) noexcept
        {
            return Runtime::ToErrorCode(status);
        }

        RuntimeStatus ContextUnavailable() noexcept
        {
            return RuntimeStatus::ServiceUnavailable;
        }

        std::unordered_map<std::uint64_t, NativeEventSubscription> g_event_subscriptions;
        std::uint64_t g_next_event_subscription = 1;

        std::string CString(const char* text)
        {
            return text != nullptr ? std::string(text) : std::string();
        }

        int WriteNativeText(std::string_view text, char* output, int output_capacity)
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

        std::string EscapeEventValue(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            for (const char c : text)
            {
                switch (c)
                {
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '=': result += "\\="; break;
                default: result.push_back(c); break;
                }
            }
            return result;
        }

        void AppendHandle(std::ostringstream& stream, const char* prefix,
            Runtime::ObjectHandle handle)
        {
            stream << prefix << "_world=" << handle.world << '\n';
            stream << prefix << "_object=" << handle.object.Value() << '\n';
            stream << prefix << "_generation=" << handle.generation << '\n';
        }

        std::string EncodeEventRecord(const Runtime::EventRecord& record)
        {
            std::ostringstream stream;
            stream << "type=" << record.type.ToString() << '\n';
            stream << "name=" << EscapeEventValue(record.type_name) << '\n';
            stream << "frame=" << record.frame_index << '\n';
            AppendHandle(stream, "source", record.source);
            AppendHandle(stream, "target", record.target);
            return stream.str();
        }

        int NativeFindGameObject(std::uint64_t object_id,
            Runtime::ObjectHandle* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = Runtime::ObjectHandle::None();
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

            *out = g_runtime_context->FindByObjectID(Core::ObjectID(object_id));
            return StatusCode(out->IsEmpty() ? RuntimeStatus::InvalidHandle
                : RuntimeStatus::Ok);
        }

        int NativeIsGameObjectValid(Runtime::ObjectHandle handle) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->IsValid(handle)
                ? RuntimeStatus::Ok : RuntimeStatus::InvalidHandle);
        }

        int NativeGetLocalPosition(Runtime::ObjectHandle handle,
            DirectX::XMFLOAT3* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->GetLocalPosition(handle, *out));
        }

        int NativeSetLocalPosition(Runtime::ObjectHandle handle,
            DirectX::XMFLOAT3 value) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetLocalPosition(handle, value));
        }

        int NativeGetLocalRotationEuler(Runtime::ObjectHandle handle,
            DirectX::XMFLOAT3* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->GetLocalRotationEuler(handle, *out));
        }

        int NativeSetLocalRotationEuler(Runtime::ObjectHandle handle,
            DirectX::XMFLOAT3 value) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetLocalRotationEuler(handle, value));
        }

        int NativeGetLocalScale(Runtime::ObjectHandle handle,
            DirectX::XMFLOAT3* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->GetLocalScale(handle, *out));
        }

        int NativeSetLocalScale(Runtime::ObjectHandle handle,
            DirectX::XMFLOAT3 value) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetLocalScale(handle, value));
        }

        int NativeGetComponent(Runtime::ObjectHandle handle, std::uint32_t type_id,
            Runtime::ComponentHandle* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = Runtime::ComponentHandle::None();
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

            *out = g_runtime_context->GetComponent(handle, type_id);
            return StatusCode(out->IsEmpty() ? RuntimeStatus::ComponentNotFound
                : RuntimeStatus::Ok);
        }

        int NativeDestroyGameObject(Runtime::ObjectHandle handle) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->DestroyGameObject(handle));
        }

        int NativeDestroyComponent(Runtime::ComponentHandle handle) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->DestroyComponent(handle));
        }

        int NativeInstantiate(const char* asset_guid, DirectX::XMFLOAT3 position,
            DirectX::XMFLOAT3 rotation_euler, DirectX::XMFLOAT3 scale,
            Runtime::ObjectHandle parent, Runtime::ObjectHandle* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = Runtime::ObjectHandle::None();
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->InstantiatePrefab(CString(asset_guid),
                position, rotation_euler, scale, parent, *out));
        }

        int NativeLoadScene(const char* asset_guid) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->LoadScene(CString(asset_guid)));
        }

        int NativeReloadScene() noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->ReloadCurrentScene());
        }

        int NativeReturnToPreviousScene() noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->ReturnToPreviousScene());
        }

        int NativeTriggerSceneFlow(const char* event_name) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->TriggerSceneFlow(CString(event_name)));
        }

        int NativeSetSceneFlowBool(const char* key, int value) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetSceneFlowBool(CString(key), value != 0));
        }

        int NativeSetSceneFlowInt(const char* key, std::int64_t value) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetSceneFlowInt(CString(key), value));
        }

        int NativeSetSceneFlowFloat(const char* key, double value) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetSceneFlowFloat(CString(key), value));
        }

        int NativeRaycast(DirectX::XMFLOAT3 origin, DirectX::XMFLOAT3 direction,
            float max_distance, int layer, int mask, Runtime::ObjectHandle ignore,
            NativeRaycastHit* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = NativeRaycastHit{};
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

            ReplayEngine::Scene::RaycastHit native{};
            const RuntimeStatus status = g_runtime_context->Raycast(origin, direction,
                max_distance, layer, mask, ignore, native);
            if (Runtime::Failed(status)) return StatusCode(status);

            out->point = native.point;
            out->normal = native.normal;
            out->distance = native.distance;
            out->collider_id = native.source.collider;
            out->valid = native.valid ? 1 : 0;
            if (native.valid && native.source.object.Valid())
                out->object = g_runtime_context->FindByObjectID(native.source.object);
            return StatusCode(RuntimeStatus::Ok);
        }

        int NativeSubscribeEvent(std::uint64_t high, std::uint64_t low,
            Runtime::ObjectHandle owner, std::uint64_t* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = Runtime::invalid_subscription_id;
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

            const Reflection::TypeGUID type{ high, low };
            if (!type.IsValid()) return StatusCode(RuntimeStatus::InvalidArgument);

            const std::uint64_t id = g_next_event_subscription++;
            Runtime::ScopedSubscription token = g_runtime_context->Events().Subscribe(
                type,
                [id](const Runtime::EventRecord& record)
                {
                    auto it = g_event_subscriptions.find(id);
                    if (it == g_event_subscriptions.end()) return;
                    it->second.pending.push_back(EncodeEventRecord(record));
                },
                owner);
            if (!token.Valid()) return StatusCode(RuntimeStatus::UnsupportedOperation);

            NativeEventSubscription state;
            state.token = std::move(token);
            g_event_subscriptions.emplace(id, std::move(state));
            *out = id;
            return StatusCode(RuntimeStatus::Ok);
        }

        int NativeUnsubscribeEvent(std::uint64_t subscription) noexcept
        {
            const auto removed = g_event_subscriptions.erase(subscription);
            return StatusCode(removed != 0
                ? RuntimeStatus::Ok : RuntimeStatus::InvalidHandle);
        }

        int NativePollEvent(std::uint64_t subscription, char* output,
            int output_capacity) noexcept
        {
            if (output == nullptr || output_capacity <= 0)
            {
                return StatusCode(RuntimeStatus::InvalidArgument);
            }
            output[0] = '\0';

            auto it = g_event_subscriptions.find(subscription);
            if (it == g_event_subscriptions.end())
            {
                return StatusCode(RuntimeStatus::InvalidHandle);
            }
            if (it->second.pending.empty())
            {
                return StatusCode(RuntimeStatus::Ok);
            }

            const std::string event_text = std::move(it->second.pending.front());
            it->second.pending.pop_front();
            return WriteNativeText(event_text, output, output_capacity);
        }

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
            return table;
        }

        std::string ToUtf8(const std::wstring& text)
        {
            if (text.empty()) return std::string();
            const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (size <= 0) return std::string();
            std::string result(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                result.data(), size, nullptr, nullptr);
            return result;
        }

        std::wstring ToWide(const std::string& text)
        {
            if (text.empty()) return std::wstring();
            const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                static_cast<int>(text.size()), nullptr, 0);
            if (size <= 0) return std::wstring();
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                result.data(), size);
            return result;
        }

        std::vector<char> Utf8CString(const std::filesystem::path& path)
        {
            std::string text = path.generic_u8string();
            text.push_back('\0');
            return std::vector<char>(text.begin(), text.end());
        }

        std::vector<char> Utf8CString(const std::string& text)
        {
            std::vector<char> result(text.begin(), text.end());
            result.push_back('\0');
            return result;
        }

        std::filesystem::path NormalizeRoot(std::filesystem::path root)
        {
            std::error_code error;
            if (root.empty()) root = std::filesystem::current_path(error);
            if (error) return root.lexically_normal();
            return std::filesystem::absolute(root, error).lexically_normal();
        }

        std::filesystem::path FindHostFxr()
        {
            std::vector<std::filesystem::path> roots;
#ifdef _WIN32
            wchar_t buffer[MAX_PATH]{};
            const DWORD env_size = GetEnvironmentVariableW(L"DOTNET_ROOT",
                buffer, static_cast<DWORD>(std::size(buffer)));
            if (env_size > 0 && env_size < std::size(buffer))
            {
                roots.emplace_back(buffer);
            }
            roots.emplace_back(L"C:\\Program Files\\dotnet");
            roots.emplace_back(L"C:\\Program Files (x86)\\dotnet");
#endif

            std::filesystem::path best;
            for (const std::filesystem::path& root : roots)
            {
                const std::filesystem::path fxr = root / "host" / "fxr";
                std::error_code error;
                if (!std::filesystem::exists(fxr, error) || error) continue;

                for (std::filesystem::directory_iterator it(fxr, error), end;
                    !error && it != end; it.increment(error))
                {
                    if (!it->is_directory()) continue;
                    const std::filesystem::path candidate =
                        it->path() / "hostfxr.dll";
                    if (!std::filesystem::exists(candidate, error) || error) continue;
                    if (best.empty() || it->path().filename().wstring() >
                        best.parent_path().filename().wstring())
                    {
                        best = candidate;
                    }
                }
            }
            return best;
        }

        std::string ReadOutputBuffer(const std::array<char, text_buffer_size>& buffer)
        {
            return std::string(buffer.data());
        }

        int HexValue(char c) noexcept
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        std::string Unescape(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '%' && index + 2 < text.size())
                {
                    const int high = HexValue(text[index + 1]);
                    const int low = HexValue(text[index + 2]);
                    if (high >= 0 && low >= 0)
                    {
                        result.push_back(static_cast<char>((high << 4) | low));
                        index += 2;
                        continue;
                    }
                }
                result.push_back(text[index]);
            }
            return result;
        }

        std::vector<std::string> Split(std::string_view text, char separator)
        {
            std::vector<std::string> result;
            std::size_t start = 0;
            while (start <= text.size())
            {
                const std::size_t next = text.find(separator, start);
                const std::size_t count = next == std::string_view::npos
                    ? text.size() - start
                    : next - start;
                result.emplace_back(text.substr(start, count));
                if (next == std::string_view::npos) break;
                start = next + 1;
            }
            return result;
        }

        bool MapManagedFieldType(const std::string& text, ScriptValueType& out)
        {
            using Reflection::PropertyType;
            if (text == "bool") { out = PropertyType::Bool; return true; }
            if (text == "int") { out = PropertyType::Int; return true; }
            if (text == "int64") { out = PropertyType::Int64; return true; }
            if (text == "uint64") { out = PropertyType::UInt64; return true; }
            if (text == "float") { out = PropertyType::Float; return true; }
            if (text == "double") { out = PropertyType::Double; return true; }
            if (text == "string") { out = PropertyType::String; return true; }
            if (text == "vector2" || text == "vec2") { out = PropertyType::Vector2; return true; }
            if (text == "vector3" || text == "vec3") { out = PropertyType::Vector3; return true; }
            if (text == "vector4" || text == "vec4") { out = PropertyType::Vector4; return true; }
            if (text == "quaternion" || text == "quat") { out = PropertyType::Quaternion; return true; }
            if (text == "color") { out = PropertyType::Color; return true; }
            if (text == "object") { out = PropertyType::ObjectReference; return true; }
            if (text == "component") { out = PropertyType::ComponentReference; return true; }
            return false;
        }

        double ParseDouble(const std::string& text, double fallback = 0.0)
        {
            char* end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            return end != text.c_str() ? value : fallback;
        }

        std::uint64_t ParseUInt64(const std::string& text,
            std::uint64_t fallback = 0)
        {
            char* end = nullptr;
            const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
            return end != text.c_str() ? static_cast<std::uint64_t>(value) : fallback;
        }

        std::int64_t ParseInt64(const std::string& text,
            std::int64_t fallback = 0)
        {
            char* end = nullptr;
            const long long value = std::strtoll(text.c_str(), &end, 10);
            return end != text.c_str() ? static_cast<std::int64_t>(value) : fallback;
        }

        ScriptValue ParseValue(ScriptValueType type, const std::string& text)
        {
            using Reflection::ComponentReference;
            using Reflection::PropertyType;

            const std::vector<std::string> parts = Split(text, ',');
            const auto part = [&parts](std::size_t index) -> std::string
            {
                return index < parts.size() ? parts[index] : std::string();
            };
            const auto f = [&part](std::size_t index) -> float
            {
                return static_cast<float>(ParseDouble(part(index)));
            };

            switch (type)
            {
            case PropertyType::Bool:
            {
                std::string lower = text;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return ScriptValue::MakeBool(lower == "true" || lower == "1");
            }
            case PropertyType::Int:
                return ScriptValue::MakeInt(static_cast<int>(ParseInt64(text)));
            case PropertyType::Int64:
                return ScriptValue::MakeInt64(ParseInt64(text));
            case PropertyType::UInt64:
                return ScriptValue::MakeUInt64(ParseUInt64(text));
            case PropertyType::Float:
                return ScriptValue::MakeFloat(static_cast<float>(ParseDouble(text)));
            case PropertyType::Double:
                return ScriptValue::MakeDouble(ParseDouble(text));
            case PropertyType::String:
                return ScriptValue::MakeString(text);
            case PropertyType::Vector2:
                return ScriptValue::MakeVector2({ f(0), f(1) });
            case PropertyType::Vector3:
                return ScriptValue::MakeVector3({ f(0), f(1), f(2) });
            case PropertyType::Vector4:
                return ScriptValue::MakeVector4({ f(0), f(1), f(2), f(3) });
            case PropertyType::Quaternion:
                return ScriptValue::MakeQuaternion({ f(0), f(1), f(2), f(3) });
            case PropertyType::Color:
                return ScriptValue::MakeColor({ f(0), f(1), f(2), f(3) });
            case PropertyType::ObjectReference:
                return ScriptValue::MakeObjectReference(Core::ObjectID(ParseUInt64(text)));
            case PropertyType::ComponentReference:
            {
                ComponentReference reference;
                reference.owner = Core::ObjectID(ParseUInt64(part(0)));
                reference.component =
                    static_cast<Core::ComponentStableID>(ParseUInt64(part(1)));
                return ScriptValue::MakeComponentReference(reference);
            }
            default:
                return ScriptFieldSchema::MakeTypeDefault(type);
            }
        }

        std::string FloatText(float value)
        {
            std::ostringstream stream;
            stream.precision(9);
            stream << value;
            return stream.str();
        }

        std::string DoubleText(double value)
        {
            std::ostringstream stream;
            stream.precision(17);
            stream << value;
            return stream.str();
        }

        std::string ValueToText(const ScriptValue& value)
        {
            using Reflection::PropertyType;

            switch (value.Type())
            {
            case PropertyType::Bool:
                return value.AsBool() ? "true" : "false";
            case PropertyType::Int:
                return std::to_string(value.AsInt());
            case PropertyType::Int64:
                return std::to_string(value.AsInt64());
            case PropertyType::UInt64:
                return std::to_string(value.AsUInt64());
            case PropertyType::Float:
                return FloatText(value.AsFloat());
            case PropertyType::Double:
                return DoubleText(value.AsDouble());
            case PropertyType::String:
                return value.AsString();
            case PropertyType::Vector2:
            {
                const DirectX::XMFLOAT2 v = value.AsVector2();
                return FloatText(v.x) + "," + FloatText(v.y);
            }
            case PropertyType::Vector3:
            {
                const DirectX::XMFLOAT3 v = value.AsVector3();
                return FloatText(v.x) + "," + FloatText(v.y) + "," + FloatText(v.z);
            }
            case PropertyType::Vector4:
            case PropertyType::Quaternion:
            case PropertyType::Color:
            {
                const DirectX::XMFLOAT4 v = value.AsVector4();
                return FloatText(v.x) + "," + FloatText(v.y) + "," +
                    FloatText(v.z) + "," + FloatText(v.w);
            }
            case PropertyType::ObjectReference:
                return std::to_string(value.AsObjectReference().Value());
            case PropertyType::ComponentReference:
            {
                const Reflection::ComponentReference reference =
                    value.AsComponentReference();
                return std::to_string(reference.owner.Value()) + "," +
                    std::to_string(reference.component);
            }
            default:
                return std::string();
            }
        }

        bool ParseSchemaText(ScriptTypeID type_id, std::uint32_t revision,
            const std::string& text, ScriptFieldSchemaRef& out_schema,
            std::unordered_map<std::string, ScriptValueType>& out_field_types,
            std::string& error)
        {
            std::vector<ScriptFieldDefinition> fields;
            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line))
            {
                if (line.empty()) continue;
                const std::vector<std::string> parts = Split(line, '\t');
                if (parts.size() < 6 || parts[0] != "FIELD") continue;

                ScriptValueType type = Reflection::PropertyType::Float;
                if (!MapManagedFieldType(parts[2], type))
                {
                    error = "Unsupported C# field type: " + parts[2];
                    return false;
                }

                ScriptFieldDefinition definition =
                    ScriptFieldDefinition::Make(Unescape(parts[1]), type);
                definition.display_name = Unescape(parts[3]);
                definition.tooltip = Unescape(parts[4]);
                definition.default_value = ParseValue(type, Unescape(parts[5]));

                out_field_types[definition.SavedName()] = type;
                fields.push_back(std::move(definition));
            }

            std::vector<std::string> rejected;
            out_schema = ScriptFieldSchema::Build(type_id, revision,
                std::move(fields), &rejected);
            if (!rejected.empty())
            {
                error = rejected.front();
            }
            return true;
        }

        void* ProcAddress(void* module, const char* name)
        {
#ifdef _WIN32
            return reinterpret_cast<void*>(GetProcAddress(
                static_cast<HMODULE>(module), name));
#else
            (void)module;
            (void)name;
            return nullptr;
#endif
        }
    }

    CSharpScriptBackend::CSharpScriptBackend(std::filesystem::path project_root)
        : project_root_(NormalizeRoot(std::move(project_root)))
    {
    }

    CSharpScriptBackend::~CSharpScriptBackend()
    {
        Shutdown();
    }

    bool CSharpScriptBackend::Initialize()
    {
        if (initialized_) return true;

        std::string error;
        if (!CSharpProject::EnsureProjectFiles(project_root_, error))
        {
            SetLastError(error);
            return false;
        }

        const CSharpBuildResult managed_build =
            CSharpProject::BuildManagedApi(project_root_);
        if (!managed_build.succeeded)
        {
            last_build_ = managed_build;
            SetLastError(managed_build.output_text.empty()
                ? "Managed API build failed." : managed_build.output_text);
            return false;
        }

        if (!LoadHost()) return false;
        if (!LoadManagedApi()) return false;
        if (!ResolveManagedEntryPoints()) return false;
        if (!SetNativeApi()) return false;

        initialized_ = true;

        // User script のコンパイル失敗で Editor 起動を止めない。
        // 成功していた旧 Assembly がある場合は Managed 側が保持する。
        CompileAndReload(nullptr);
        return true;
    }

    void CSharpScriptBackend::Shutdown()
    {
        g_event_subscriptions.clear();

        if (set_native_api_ != nullptr)
        {
            set_native_api_fn set_api =
                reinterpret_cast<set_native_api_fn>(set_native_api_);
            set_api(nullptr);
        }

        if (assembly_loaded_)
        {
            std::string output;
            UnloadGameAssembly(output);
        }

        type_states_.clear();
        instance_types_.clear();
        if (g_runtime_context == runtime_context_) g_runtime_context = nullptr;
        runtime_context_ = nullptr;

#ifdef _WIN32
        if (hostfxr_library_ != nullptr)
        {
            FreeLibrary(static_cast<HMODULE>(hostfxr_library_));
        }
#endif
        hostfxr_library_ = nullptr;
        host_context_ = nullptr;
        load_assembly_and_get_function_pointer_ = nullptr;

        set_native_api_ = nullptr;
        load_assembly_ = nullptr;
        unload_assembly_ = nullptr;
        describe_type_ = nullptr;
        create_instance_ = nullptr;
        destroy_instance_ = nullptr;
        invoke_ = nullptr;
        set_field_ = nullptr;
        get_field_ = nullptr;
        set_time_ = nullptr;
        live_instance_count_ = nullptr;
        last_error_function_ = nullptr;

        initialized_ = false;
        assembly_loaded_ = false;
    }

    void CSharpScriptBackend::BindRuntimeContext(Runtime::RuntimeContext* context) noexcept
    {
        runtime_context_ = context;
        g_runtime_context = context;
    }

    bool CSharpScriptBackend::LoadHost()
    {
#ifndef _WIN32
        SetLastError("hostfxr backend is only implemented on Windows in this project.");
        return false;
#else
        if (hostfxr_library_ != nullptr) return true;

        const std::filesystem::path hostfxr = FindHostFxr();
        if (hostfxr.empty())
        {
            SetLastError("hostfxr.dll was not found. Install .NET 8 SDK/runtime.");
            return false;
        }

        HMODULE module = LoadLibraryW(hostfxr.wstring().c_str());
        if (module == nullptr)
        {
            SetLastError("hostfxr.dll could not be loaded: " +
                hostfxr.generic_u8string());
            return false;
        }

        hostfxr_library_ = module;
        return true;
#endif
    }

    bool CSharpScriptBackend::LoadManagedApi()
    {
#ifndef _WIN32
        return false;
#else
        hostfxr_initialize_for_runtime_config_fn initialize =
            reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
                ProcAddress(hostfxr_library_, "hostfxr_initialize_for_runtime_config"));
        hostfxr_get_runtime_delegate_fn get_delegate =
            reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
                ProcAddress(hostfxr_library_, "hostfxr_get_runtime_delegate"));
        hostfxr_close_fn close =
            reinterpret_cast<hostfxr_close_fn>(
                ProcAddress(hostfxr_library_, "hostfxr_close"));

        if (initialize == nullptr || get_delegate == nullptr || close == nullptr)
        {
            SetLastError("hostfxr exports are incomplete.");
            return false;
        }

        const std::filesystem::path runtime_config =
            CSharpProject::ManagedApiRuntimeConfigPath(project_root_);
        if (!std::filesystem::exists(runtime_config))
        {
            SetLastError("Managed API runtimeconfig is missing: " +
                runtime_config.generic_u8string());
            return false;
        }

        void* context = nullptr;
        const std::int32_t init_result =
            initialize(runtime_config.wstring().c_str(), nullptr, &context);
        if (init_result < 0 || context == nullptr)
        {
            SetLastError("hostfxr_initialize_for_runtime_config failed: " +
                std::to_string(init_result));
            return false;
        }

        void* load_delegate = nullptr;
        const std::int32_t delegate_result = get_delegate(context,
            hdt_load_assembly_and_get_function_pointer, &load_delegate);
        close(context);

        if (delegate_result < 0 || load_delegate == nullptr)
        {
            SetLastError("hostfxr_get_runtime_delegate failed: " +
                std::to_string(delegate_result));
            return false;
        }

        load_assembly_and_get_function_pointer_ = load_delegate;
        return true;
#endif
    }

    bool CSharpScriptBackend::ResolveManagedEntryPoints()
    {
#ifndef _WIN32
        return false;
#else
        if (load_assembly_and_get_function_pointer_ == nullptr) return false;

        load_assembly_and_get_function_pointer_fn load_function =
            reinterpret_cast<load_assembly_and_get_function_pointer_fn>(
                load_assembly_and_get_function_pointer_);
        const std::filesystem::path assembly =
            CSharpProject::ManagedApiAssemblyPath(project_root_);
        const std::wstring assembly_path = assembly.wstring();
        const wchar_t* type_name = L"ReplayEngine.NativeBridge, RePlayEngine.Managed";
        const wchar_t* unmanaged_callers_only =
            reinterpret_cast<const wchar_t*>(static_cast<intptr_t>(-1));

        const auto resolve = [&](const wchar_t* method, void** target) -> bool
        {
            *target = nullptr;
            const int result = load_function(assembly_path.c_str(), type_name,
                method, unmanaged_callers_only, nullptr, target);
            if (result < 0 || *target == nullptr)
            {
                SetLastError("Managed API entry point was not resolved: " +
                    ToUtf8(std::wstring(method)) + " (" + std::to_string(result) + ")");
                return false;
            }
            return true;
        };

        return resolve(L"SetNativeApi", &set_native_api_) &&
            resolve(L"LoadAssembly", &load_assembly_) &&
            resolve(L"UnloadAssembly", &unload_assembly_) &&
            resolve(L"DescribeType", &describe_type_) &&
            resolve(L"CreateInstance", &create_instance_) &&
            resolve(L"DestroyInstance", &destroy_instance_) &&
            resolve(L"Invoke", &invoke_) &&
            resolve(L"SetField", &set_field_) &&
            resolve(L"GetField", &get_field_) &&
            resolve(L"SetTime", &set_time_) &&
            resolve(L"LiveInstanceCount", &live_instance_count_) &&
            resolve(L"LastError", &last_error_function_);
#endif
    }

    bool CSharpScriptBackend::SetNativeApi()
    {
        if (set_native_api_ == nullptr) return false;
        NativeApiTable table = MakeNativeApiTable();
        const int result = reinterpret_cast<set_native_api_fn>(set_native_api_)(&table);
        if (result == 0)
        {
            SetLastError("Managed API rejected native callback table.");
            return false;
        }
        return true;
    }

    std::filesystem::path CSharpScriptBackend::ShadowCopyAssembly(
        const std::filesystem::path& assembly_path, std::string& error) const
    {
        std::error_code filesystem_error;
        if (!std::filesystem::exists(assembly_path, filesystem_error) ||
            filesystem_error)
        {
            error = "C# build output is missing: " +
                assembly_path.generic_u8string();
            return {};
        }

        const auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const std::filesystem::path cache_root =
            project_root_ / "Saved" / "ManagedAssemblyCache";

        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const std::filesystem::path folder =
                cache_root / (std::to_string(ticks) + "_" + std::to_string(attempt));
            std::filesystem::create_directories(folder, filesystem_error);
            if (filesystem_error)
            {
                error = "C# Assembly cache folder create failed: " +
                    folder.generic_u8string();
                return {};
            }

            const std::filesystem::path destination = folder / assembly_path.filename();
            std::filesystem::copy_file(assembly_path, destination,
                std::filesystem::copy_options::overwrite_existing, filesystem_error);
            if (filesystem_error)
            {
                if (std::filesystem::exists(destination)) continue;
                error = "C# Assembly shadow copy failed: " +
                    filesystem_error.message();
                return {};
            }

            const std::array<std::string, 2> sidecars = { ".pdb", ".deps.json" };
            for (const std::string& extension : sidecars)
            {
                std::filesystem::path source_sidecar = assembly_path;
                source_sidecar.replace_extension(extension);
                if (!std::filesystem::exists(source_sidecar, filesystem_error) ||
                    filesystem_error)
                {
                    filesystem_error.clear();
                    continue;
                }

                std::filesystem::path destination_sidecar = destination;
                destination_sidecar.replace_extension(extension);
                std::filesystem::copy_file(source_sidecar, destination_sidecar,
                    std::filesystem::copy_options::overwrite_existing,
                    filesystem_error);
                filesystem_error.clear();
            }

            return destination;
        }

        error = "C# Assembly shadow copy cache path could not be allocated.";
        return {};
    }

    bool CSharpScriptBackend::LoadGameAssembly(
        const std::filesystem::path& assembly_path, std::string& output)
    {
        if (load_assembly_ == nullptr) return false;

        std::string copy_error;
        const std::filesystem::path load_path =
            ShadowCopyAssembly(assembly_path, copy_error);
        if (load_path.empty())
        {
            output = copy_error;
            SetLastError(copy_error);
            return false;
        }

        std::array<char, text_buffer_size> buffer{};
        std::vector<char> path = Utf8CString(load_path);
        const int result = reinterpret_cast<load_assembly_fn>(load_assembly_)(
            path.data(), buffer.data(), static_cast<int>(buffer.size()));
        output = ReadOutputBuffer(buffer);
        if (result == 0)
        {
            RefreshLastError();
            if (last_error_.empty()) SetLastError(output);
            return false;
        }

        assembly_loaded_ = true;
        g_event_subscriptions.clear();
        type_states_.clear();
        instance_types_.clear();
        last_error_.clear();
        last_error_file_.clear();
        last_error_line_ = 0;
        return true;
    }

    bool CSharpScriptBackend::UnloadGameAssembly(std::string& output)
    {
        if (unload_assembly_ == nullptr) return false;

        std::array<char, text_buffer_size> buffer{};
        const int result = reinterpret_cast<unload_assembly_fn>(unload_assembly_)(
            buffer.data(), static_cast<int>(buffer.size()));
        output = ReadOutputBuffer(buffer);
        if (result == 0)
        {
            RefreshLastError();
            if (last_error_.empty()) SetLastError(output);
            return false;
        }

        assembly_loaded_ = false;
        type_states_.clear();
        instance_types_.clear();
        return true;
    }

    bool CSharpScriptBackend::CompileAndReload(CSharpBuildResult* out_build)
    {
        last_build_ = CSharpProject::BuildGameScripts(project_root_);
        if (out_build != nullptr) *out_build = last_build_;

        if (!last_build_.succeeded)
        {
            SetLastError(last_build_.output_text.empty()
                ? "C# build failed." : last_build_.output_text);
            return false;
        }

        std::string output;
        if (!LoadGameAssembly(last_build_.output_assembly, output))
        {
            if (last_error_.empty()) SetLastError(output);
            return false;
        }

        return true;
    }

    bool CSharpScriptBackend::ReloadLastBuiltAssembly()
    {
        if (!last_build_.succeeded || last_build_.output_assembly.empty())
        {
            SetLastError("No successful C# build is available for reload.");
            return false;
        }
        std::error_code error;
        if (!std::filesystem::exists(last_build_.output_assembly, error) || error)
        {
            SetLastError("Last C# build output is missing: " +
                last_build_.output_assembly.generic_u8string());
            return false;
        }

        std::string output;
        return LoadGameAssembly(last_build_.output_assembly, output);
    }

    ScriptLoadResult CSharpScriptBackend::LoadType(
        const ScriptTypeDescriptor& descriptor, std::uint32_t schema_revision)
    {
        if (!initialized_)
        {
            return ScriptLoadResult::Failure("C# Backend is not initialized.");
        }
        if (!assembly_loaded_)
        {
            return ScriptLoadResult::Failure("C# Assembly is not loaded.");
        }
        if (describe_type_ == nullptr)
        {
            return ScriptLoadResult::Failure("C# DescribeType entry point is missing.");
        }

        std::array<char, text_buffer_size> buffer{};
        const int result = reinterpret_cast<describe_type_fn>(describe_type_)(
            descriptor.type_id.high, descriptor.type_id.low,
            buffer.data(), static_cast<int>(buffer.size()));
        const std::string text = ReadOutputBuffer(buffer);
        if (result == 0)
        {
            RefreshLastError();
            return ScriptLoadResult::Failure(last_error_.empty() ? text : last_error_,
                last_error_file_, last_error_line_);
        }

        TypeState state;
        std::string schema_error;
        if (!ParseSchemaText(descriptor.type_id, schema_revision, text,
            state.schema, state.field_types, schema_error))
        {
            return ScriptLoadResult::Failure(schema_error);
        }

        type_states_[descriptor.type_id] = state;
        return ScriptLoadResult::Success(std::move(state.schema));
    }

    bool CSharpScriptBackend::CanInstantiate(ScriptTypeID type_id) const
    {
        return initialized_ && assembly_loaded_ &&
            type_states_.find(type_id) != type_states_.end();
    }

    ScriptInstanceHandle CSharpScriptBackend::CreateInstance(
        const ScriptInstanceRequest& request)
    {
        if (!CanInstantiate(request.type_id) || create_instance_ == nullptr)
        {
            SetLastError("C# Behaviour type cannot be instantiated.");
            return invalid_script_instance_handle;
        }

        const ScriptInstanceHandle handle =
            reinterpret_cast<create_instance_fn>(create_instance_)(
                request.type_id.high, request.type_id.low,
                request.owner_handle, request.component_handle);
        if (handle == invalid_script_instance_handle)
        {
            RefreshLastError();
            return invalid_script_instance_handle;
        }

        instance_types_[handle] = request.type_id;
        return handle;
    }

    void CSharpScriptBackend::DestroyInstance(ScriptInstanceHandle instance)
    {
        if (instance == invalid_script_instance_handle) return;
        if (destroy_instance_ != nullptr)
        {
            reinterpret_cast<destroy_instance_fn>(destroy_instance_)(instance);
        }
        instance_types_.erase(instance);
    }

    ScriptInvokeResult CSharpScriptBackend::Invoke(ScriptInstanceHandle instance,
        ScriptCallback callback, const ScriptArguments& arguments)
    {
        if (!initialized_ || invoke_ == nullptr) return ScriptInvokeResult::BackendUnavailable;
        if (instance == invalid_script_instance_handle) return ScriptInvokeResult::NoInstance;

        if (runtime_context_ != nullptr && set_time_ != nullptr)
        {
            const Runtime::RuntimeTime& time = runtime_context_->Time();
            reinterpret_cast<set_time_fn>(set_time_)(time.delta_time,
                time.fixed_delta_time, time.frame_index);
        }

        const float delta_time = ScriptCallbackTakesDeltaTime(callback)
            ? arguments.delta_time : 0.0f;
        const int result = reinterpret_cast<invoke_fn>(invoke_)(
            instance, static_cast<int>(callback), delta_time);

        switch (result)
        {
        case 0: return ScriptInvokeResult::Ok;
        case 1: return ScriptInvokeResult::NotImplemented;
        case 2: return ScriptInvokeResult::NoInstance;
        case 3: return ScriptInvokeResult::BackendUnavailable;
        default:
            RefreshLastError();
            return ScriptInvokeResult::RuntimeError;
        }
    }

    bool CSharpScriptBackend::SetField(ScriptInstanceHandle instance,
        const std::string& saved_name, const ScriptValue& value)
    {
        if (set_field_ == nullptr) return false;

        const std::string text = ValueToText(value);
        std::vector<char> name = Utf8CString(saved_name);
        std::vector<char> value_text = Utf8CString(text);
        const int result = reinterpret_cast<set_field_fn>(set_field_)(
            instance, name.data(), static_cast<int>(value.Type()), value_text.data());
        if (result == 0) RefreshLastError();
        return result != 0;
    }

    bool CSharpScriptBackend::GetField(ScriptInstanceHandle instance,
        const std::string& saved_name, ScriptValue& out) const
    {
        if (get_field_ == nullptr) return false;

        std::array<char, text_buffer_size> buffer{};
        std::vector<char> name = Utf8CString(saved_name);
        const int result = reinterpret_cast<get_field_fn>(get_field_)(
            instance, name.data(), buffer.data(), static_cast<int>(buffer.size()));
        if (result == 0)
        {
            RefreshLastError();
            return false;
        }

        const auto instance_type = instance_types_.find(instance);
        if (instance_type == instance_types_.end()) return false;
        const auto type_state = type_states_.find(instance_type->second);
        if (type_state == type_states_.end()) return false;
        const auto field_type = type_state->second.field_types.find(saved_name);
        if (field_type == type_state->second.field_types.end()) return false;

        out = ParseValue(field_type->second, ReadOutputBuffer(buffer));
        return true;
    }

    std::size_t CSharpScriptBackend::LiveInstanceCount() const noexcept
    {
        if (live_instance_count_ == nullptr) return instance_types_.size();
        const int count =
            reinterpret_cast<live_instance_count_fn>(live_instance_count_)();
        return count < 0 ? 0u : static_cast<std::size_t>(count);
    }

    void CSharpScriptBackend::RefreshLastError() const
    {
        if (last_error_function_ == nullptr) return;

        std::array<char, text_buffer_size> message{};
        std::array<char, 1024> file{};
        int line = 0;
        reinterpret_cast<last_error_fn>(last_error_function_)(
            message.data(), static_cast<int>(message.size()),
            file.data(), static_cast<int>(file.size()), &line);
        last_error_ = message.data();
        last_error_file_ = file.data();
        last_error_line_ = line;
    }

    void CSharpScriptBackend::SetLastError(std::string message,
        std::string file, int line) const
    {
        last_error_ = std::move(message);
        last_error_file_ = std::move(file);
        last_error_line_ = line;
    }
}
