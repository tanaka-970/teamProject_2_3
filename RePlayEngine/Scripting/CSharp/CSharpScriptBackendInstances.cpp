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
#include "CSharpScriptBackendHostInternal.h"
#include "CSharpScriptBackendInternal.h"
#include "CSharpScriptBackendNativeInternal.h"
namespace ReplayEngine::Scripting::CSharp
{
    using namespace Detail;

// Script type／instance／Invoke／Field 操作の関数本体

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

}
