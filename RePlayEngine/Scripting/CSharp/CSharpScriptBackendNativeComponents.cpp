// v10 Native API。Component の型引き・汎用プロパティ・World Transform・Rigidbody。
//
// Component 型ごとの専用 callback は増やさない。Inspector と同じ PropertyRegistry を
// 名前で読み書きすることで、登録済みのプロパティが自動的に C# から触れるようにする。

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
    namespace
    {
        using Reflection::PropertyType;
        using Reflection::PropertyValue;

        // 宣言された型と欲しい型が違うときは PropertyValue の変換規則へ委ねる。
        bool CoerceProperty(const PropertyValue& source, PropertyType wanted,
            PropertyValue& out)
        {
            if (source.Type() == wanted)
            {
                out = source;
                return true;
            }
            return source.ConvertTo(wanted, out);
        }

        RuntimeStatus ReadProperty(Runtime::ComponentHandle handle, const char* name,
            PropertyType wanted, PropertyValue& out)
        {
            if (g_runtime_context == nullptr) return ContextUnavailable();
            PropertyValue raw;
            const RuntimeStatus status =
                g_runtime_context->GetComponentProperty(handle, CString(name), raw);
            if (status != RuntimeStatus::Ok) return status;
            if (!CoerceProperty(raw, wanted, out)) return RuntimeStatus::TypeMismatch;
            return RuntimeStatus::Ok;
        }

        int WriteProperty(Runtime::ComponentHandle handle, const char* name,
            const PropertyValue& value) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(
                g_runtime_context->SetComponentProperty(handle, CString(name), value));
        }
    }

    // ---- Component の型 -------------------------------------------------------

    int NativeComponentTypeId(const char* type_name, std::uint32_t* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

        const Core::ComponentTypeID type_id =
            g_runtime_context->FindComponentTypeId(CString(type_name));
        if (type_id == Core::invalid_component_type_id)
            return StatusCode(RuntimeStatus::ComponentNotFound);
        *out = static_cast<std::uint32_t>(type_id);
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeGetComponentTypeName(Runtime::ComponentHandle handle, char* output,
        int output_capacity) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        std::string name;
        const RuntimeStatus status =
            g_runtime_context->GetComponentTypeName(handle, name);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        return WriteNativeText(name, output, output_capacity);
    }

    // ---- 汎用プロパティ -------------------------------------------------------

    int NativeGetComponentPropertyBool(Runtime::ComponentHandle handle,
        const char* name, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        PropertyValue value;
        const RuntimeStatus status = ReadProperty(handle, name, PropertyType::Bool, value);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        *out = value.AsBool() ? 1 : 0;
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeSetComponentPropertyBool(Runtime::ComponentHandle handle,
        const char* name, int value) noexcept
    {
        return WriteProperty(handle, name, PropertyValue::MakeBool(value != 0));
    }

    int NativeGetComponentPropertyInt(Runtime::ComponentHandle handle,
        const char* name, std::int64_t* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        PropertyValue value;
        const RuntimeStatus status = ReadProperty(handle, name, PropertyType::Int64, value);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        *out = value.AsInt64();
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeSetComponentPropertyInt(Runtime::ComponentHandle handle,
        const char* name, std::int64_t value) noexcept
    {
        // Enum / Int / Int64 のどれで宣言されていても書けるよう、宣言型に合わせる。
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        PropertyValue current;
        const RuntimeStatus status =
            g_runtime_context->GetComponentProperty(handle, CString(name), current);
        if (status != RuntimeStatus::Ok) return StatusCode(status);

        PropertyValue written = PropertyValue::MakeInt64(value);
        if (current.Type() != PropertyType::Int64)
        {
            PropertyValue converted;
            if (written.ConvertTo(current.Type(), converted)) written = converted;
        }
        return WriteProperty(handle, name, written);
    }

    int NativeGetComponentPropertyDouble(Runtime::ComponentHandle handle,
        const char* name, double* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0.0;
        PropertyValue value;
        const RuntimeStatus status = ReadProperty(handle, name, PropertyType::Double, value);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        *out = value.AsDouble();
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeSetComponentPropertyDouble(Runtime::ComponentHandle handle,
        const char* name, double value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        PropertyValue current;
        const RuntimeStatus status =
            g_runtime_context->GetComponentProperty(handle, CString(name), current);
        if (status != RuntimeStatus::Ok) return StatusCode(status);

        PropertyValue written = PropertyValue::MakeDouble(value);
        if (current.Type() != PropertyType::Double)
        {
            PropertyValue converted;
            if (written.ConvertTo(current.Type(), converted)) written = converted;
        }
        return WriteProperty(handle, name, written);
    }

    int NativeGetComponentPropertyString(Runtime::ComponentHandle handle,
        const char* name, char* output, int output_capacity) noexcept
    {
        PropertyValue value;
        const RuntimeStatus status = ReadProperty(handle, name, PropertyType::String, value);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        return WriteNativeText(value.AsString(), output, output_capacity);
    }

    int NativeSetComponentPropertyString(Runtime::ComponentHandle handle,
        const char* name, const char* value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        PropertyValue current;
        const RuntimeStatus status =
            g_runtime_context->GetComponentProperty(handle, CString(name), current);
        if (status != RuntimeStatus::Ok) return StatusCode(status);

        PropertyValue written = PropertyValue::MakeString(CString(value));
        if (current.Type() != PropertyType::String)
        {
            PropertyValue converted;
            if (!written.ConvertTo(current.Type(), converted))
                return StatusCode(RuntimeStatus::TypeMismatch);
            written = converted;
        }
        return WriteProperty(handle, name, written);
    }

    int NativeGetComponentPropertyVec2(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT2* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = DirectX::XMFLOAT2{ 0.0f, 0.0f };
        PropertyValue value;
        const RuntimeStatus status = ReadProperty(handle, name, PropertyType::Vector2, value);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        *out = value.AsVector2();
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeSetComponentPropertyVec2(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT2 value) noexcept
    {
        return WriteProperty(handle, name, PropertyValue::MakeVector2(value));
    }

    int NativeGetComponentPropertyVec3(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT3* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        PropertyValue value;
        const RuntimeStatus status = ReadProperty(handle, name, PropertyType::Vector3, value);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        *out = value.AsVector3();
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeSetComponentPropertyVec3(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT3 value) noexcept
    {
        return WriteProperty(handle, name, PropertyValue::MakeVector3(value));
    }

    int NativeGetComponentPropertyVec4(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT4* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f };
        PropertyValue value;
        const RuntimeStatus status = ReadProperty(handle, name, PropertyType::Vector4, value);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        *out = value.AsVector4();
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeSetComponentPropertyVec4(Runtime::ComponentHandle handle,
        const char* name, DirectX::XMFLOAT4 value) noexcept
    {
        // Color / Quaternion も内部表現は Vector4。宣言型に合わせて書き戻す。
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        PropertyValue current;
        const RuntimeStatus status =
            g_runtime_context->GetComponentProperty(handle, CString(name), current);
        if (status != RuntimeStatus::Ok) return StatusCode(status);

        PropertyValue written = PropertyValue::MakeVector4(value);
        if (current.Type() == PropertyType::Color)
            written = PropertyValue::MakeColor(value);
        else if (current.Type() == PropertyType::Quaternion)
            written = PropertyValue::MakeQuaternion(value);
        return WriteProperty(handle, name, written);
    }

    // ---- World Transform ------------------------------------------------------

    int NativeSetWorldPosition(Runtime::ObjectHandle handle, DirectX::XMFLOAT3 value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetWorldPosition(handle, value));
    }

    int NativeGetWorldRotation(Runtime::ObjectHandle handle, DirectX::XMFLOAT4* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->GetWorldRotationQuaternion(handle, *out));
    }

    int NativeSetWorldRotation(Runtime::ObjectHandle handle, DirectX::XMFLOAT4 value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetWorldRotationQuaternion(handle, value));
    }

    int NativeGetWorldScale(Runtime::ObjectHandle handle, DirectX::XMFLOAT3* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = DirectX::XMFLOAT3{ 1.0f, 1.0f, 1.0f };
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->GetWorldScale(handle, *out));
    }

    int NativeSetWorldScale(Runtime::ObjectHandle handle, DirectX::XMFLOAT3 value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetWorldScale(handle, value));
    }

    int NativeGetWorldAxes(Runtime::ObjectHandle handle, DirectX::XMFLOAT3* forward,
        DirectX::XMFLOAT3* right, DirectX::XMFLOAT3* up) noexcept
    {
        if (forward == nullptr || right == nullptr || up == nullptr)
            return StatusCode(RuntimeStatus::InvalidArgument);
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->GetWorldAxes(handle, *forward, *right, *up));
    }

    int NativeLookAt(Runtime::ObjectHandle handle, DirectX::XMFLOAT3 target,
        DirectX::XMFLOAT3 world_up) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->LookAt(handle, target, world_up));
    }

    // ---- Rigidbody ------------------------------------------------------------

    int NativeRigidbodyAddForce(Runtime::ComponentHandle handle,
        DirectX::XMFLOAT3 force) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->RigidbodyAddForce(handle, force));
    }

    int NativeRigidbodyAddTorque(Runtime::ComponentHandle handle,
        DirectX::XMFLOAT3 torque) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->RigidbodyAddTorque(handle, torque));
    }

    int NativeRigidbodyClearForces(Runtime::ComponentHandle handle) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->RigidbodyClearForces(handle));
    }

    int NativeRigidbodyTeleport(Runtime::ComponentHandle handle, DirectX::XMFLOAT3 position,
        DirectX::XMFLOAT3 rotation_euler) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(
            g_runtime_context->RigidbodyTeleport(handle, position, rotation_euler));
    }

    int NativeRigidbodyGetLinearVelocity(Runtime::ComponentHandle handle,
        DirectX::XMFLOAT3* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->RigidbodyGetLinearVelocity(handle, *out));
    }

    int NativeRigidbodySetLinearVelocity(Runtime::ComponentHandle handle,
        DirectX::XMFLOAT3 value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->RigidbodySetLinearVelocity(handle, value));
    }

    int NativeRigidbodyGetAngularVelocity(Runtime::ComponentHandle handle,
        DirectX::XMFLOAT3* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->RigidbodyGetAngularVelocity(handle, *out));
    }

    int NativeRigidbodySetAngularVelocity(Runtime::ComponentHandle handle,
        DirectX::XMFLOAT3 value) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->RigidbodySetAngularVelocity(handle, value));
    }

    // ---- v11 生デバイス入力 ------------------------------------------------

    namespace
    {
        int InputBool(int* out, RuntimeStatus status, bool value) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = value ? 1 : 0;
            return StatusCode(status);
        }
    }

    int NativeInputKeyHeld(int key, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status = g_runtime_context->InputKeyHeld(key, value);
        return InputBool(out, status, value);
    }

    int NativeInputKeyPressed(int key, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status = g_runtime_context->InputKeyPressed(key, value);
        return InputBool(out, status, value);
    }

    int NativeInputKeyReleased(int key, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status = g_runtime_context->InputKeyReleased(key, value);
        return InputBool(out, status, value);
    }

    int NativeInputMouseHeld(int button, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status = g_runtime_context->InputMouseButtonHeld(button, value);
        return InputBool(out, status, value);
    }

    int NativeInputMousePressed(int button, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status =
            g_runtime_context->InputMouseButtonPressed(button, value);
        return InputBool(out, status, value);
    }

    int NativeInputMouseReleased(int button, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status =
            g_runtime_context->InputMouseButtonReleased(button, value);
        return InputBool(out, status, value);
    }

    int NativeInputPointerPosition(float* out_x, float* out_y) noexcept
    {
        if (out_x == nullptr || out_y == nullptr)
            return StatusCode(RuntimeStatus::InvalidArgument);
        *out_x = 0.0f;
        *out_y = 0.0f;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->InputPointerPosition(*out_x, *out_y));
    }

    int NativeInputWheelDelta(float* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0.0f;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->InputWheelDelta(*out));
    }

    int NativeInputPadConnected(int player_slot, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status =
            g_runtime_context->InputGamepadConnected(player_slot, value);
        return InputBool(out, status, value);
    }

    int NativeInputPadButtonHeld(int player_slot, int button, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status =
            g_runtime_context->InputGamepadButtonHeld(player_slot, button, value);
        return InputBool(out, status, value);
    }

    int NativeInputPadButtonPressed(int player_slot, int button, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status =
            g_runtime_context->InputGamepadButtonPressed(player_slot, button, value);
        return InputBool(out, status, value);
    }

    int NativeInputPadButtonReleased(int player_slot, int button, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool value = false;
        const RuntimeStatus status =
            g_runtime_context->InputGamepadButtonReleased(player_slot, button, value);
        return InputBool(out, status, value);
    }

    int NativeInputPadAxis(int player_slot, int axis, float* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0.0f;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->InputGamepadAxis(player_slot, axis, *out));
    }

    int NativeInputSetVibration(int player_slot, float low, float high) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(
            g_runtime_context->InputSetGamepadVibration(player_slot, low, high));
    }

    // ---- v11 Scene / 診断 --------------------------------------------------

    int NativeInstantiatePrefabTracked(const char* asset_guid, DirectX::XMFLOAT3 position,
        DirectX::XMFLOAT3 rotation_euler, DirectX::XMFLOAT3 scale,
        Runtime::ObjectHandle parent, std::uint64_t* out_request) noexcept
    {
        if (out_request == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out_request = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        RuntimeContext::SpawnRequestID request = 0;
        const RuntimeStatus status = g_runtime_context->InstantiatePrefabDeferredTracked(
            CString(asset_guid), position, rotation_euler, scale, parent, request);
        *out_request = request;
        return StatusCode(status);
    }

    int NativeTakeSpawnResult(std::uint64_t request, Runtime::ObjectHandle* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = Runtime::ObjectHandle::None();
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->TryTakeSpawnResult(request, *out));
    }

    int NativeGetCurrentSceneGuid(char* output, int output_capacity) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return WriteNativeText(g_runtime_context->CurrentSceneGuid(), output,
            output_capacity);
    }

    int NativeQuitApplication(const char* reason) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->QuitApplication(CString(reason)));
    }

    int NativeEventDroppedCount(std::uint64_t subscription, std::uint64_t* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        const auto it = g_event_subscriptions.find(subscription);
        if (it == g_event_subscriptions.end())
            return StatusCode(RuntimeStatus::InvalidArgument);
        *out = it->second.dropped;
        return StatusCode(RuntimeStatus::Ok);
    }
}
