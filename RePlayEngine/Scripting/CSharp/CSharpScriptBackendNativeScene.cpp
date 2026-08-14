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

// Native Scene／Object／Raycast callback の関数本体

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

}
