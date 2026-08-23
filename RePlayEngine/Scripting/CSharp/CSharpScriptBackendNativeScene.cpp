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
#include <limits>
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

        // 名前で探す。見つからなければ空 Handle と InvalidHandle を返す。
        // 例外にしないのは FindGameObject(ID) と同じ流儀。
        int NativeFindGameObjectByName(const char* name,
            Runtime::ObjectHandle* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = Runtime::ObjectHandle::None();
            if (name == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

            *out = g_runtime_context->FindByName(CString(name));
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

namespace ReplayEngine::Scripting::CSharp::Detail
{
    int NativeLogInfo(const char* message, Runtime::ObjectHandle source) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        g_runtime_context->LogInfo(CString(message), source);
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeLogWarning(const char* message, Runtime::ObjectHandle source) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        g_runtime_context->LogWarning(CString(message), source);
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeLogError(const char* message, Runtime::ObjectHandle source) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        g_runtime_context->LogError(CString(message), source);
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeCreateGameObject(const char* name, Runtime::ObjectHandle* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = Runtime::ObjectHandle::None();
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->CreateGameObject(CString(name), *out));
    }

    int NativeGetWorldPosition(Runtime::ObjectHandle handle, DirectX::XMFLOAT3* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->GetWorldPosition(handle, *out));
    }

    int NativeSetParent(Runtime::ObjectHandle child, Runtime::ObjectHandle parent,
        int preserve_world_transform) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetParent(
            child, parent, preserve_world_transform != 0));
    }

    int NativeGetParent(Runtime::ObjectHandle handle, Runtime::ObjectHandle* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = Runtime::ObjectHandle::None();
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->GetParent(handle, *out));
    }

    int NativeGetChildren(Runtime::ObjectHandle handle, Runtime::ObjectHandle* output,
        int capacity, int* count) noexcept
    {
        if (count == nullptr || capacity < 0)
            return StatusCode(RuntimeStatus::InvalidArgument);
        *count = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

        std::vector<Runtime::ObjectHandle> children;
        const RuntimeStatus status = g_runtime_context->GetChildren(handle, children);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        if (children.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            return StatusCode(RuntimeStatus::UnsupportedOperation);

        *count = static_cast<int>(children.size());
        if (output == nullptr || capacity == 0) return StatusCode(RuntimeStatus::Ok);
        if (capacity < *count) return StatusCode(RuntimeStatus::InvalidArgument);
        std::copy(children.begin(), children.end(), output);
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeGetName(Runtime::ObjectHandle handle, char* output, int output_capacity) noexcept
    {
        if (output == nullptr || output_capacity <= 0)
            return StatusCode(RuntimeStatus::InvalidArgument);
        output[0] = '\0';
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        std::string name;
        const RuntimeStatus status = g_runtime_context->GetName(handle, name);
        return status == RuntimeStatus::Ok
            ? WriteNativeText(name, output, output_capacity) : StatusCode(status);
    }

    int NativeSetName(Runtime::ObjectHandle handle, const char* name) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetName(handle, CString(name)));
    }

    int NativeGetGameObjectEnabled(Runtime::ObjectHandle handle, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        bool enabled = false;
        const RuntimeStatus status = g_runtime_context->IsEnabled(handle, enabled);
        if (status == RuntimeStatus::Ok) *out = enabled ? 1 : 0;
        return StatusCode(status);
    }

    int NativeSetGameObjectEnabled(Runtime::ObjectHandle handle, int enabled) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->SetEnabled(handle, enabled != 0));
    }
}

namespace ReplayEngine::Scripting::CSharp::Detail
{
    int NativeQueryGround(DirectX::XMFLOAT3 origin, float radius, float up_offset,
        float down_distance, float walkable_normal_y, Runtime::ObjectHandle ignore,
        NativeGroundHit* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = NativeGroundHit{};
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

        Scene::GroundHit native{};
        const RuntimeStatus status = g_runtime_context->QueryGround(origin, radius,
            up_offset, down_distance, walkable_normal_y, ignore, native);
        if (status != RuntimeStatus::Ok) return StatusCode(status);

        out->position = native.position;
        out->normal = native.normal;
        out->collider_id = native.source.collider;
        out->valid = native.valid ? 1 : 0;
        if (native.valid && native.source.object.Valid())
            out->object = g_runtime_context->FindByObjectID(native.source.object);
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeSweepSphere(DirectX::XMFLOAT3 start, DirectX::XMFLOAT3 end, float radius,
        float maximum_normal_y, Runtime::ObjectHandle ignore,
        NativeSphereSweepHit* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = NativeSphereSweepHit{};
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

        Scene::SphereSweepHit native{};
        const RuntimeStatus status = g_runtime_context->SweepSphere(start, end, radius,
            maximum_normal_y, ignore, native);
        if (status != RuntimeStatus::Ok) return StatusCode(status);

        out->center = native.center;
        out->normal = native.normal;
        out->fraction = native.fraction;
        out->collider_id = native.source.collider;
        out->valid = native.valid ? 1 : 0;
        if (native.valid && native.source.object.Valid())
            out->object = g_runtime_context->FindByObjectID(native.source.object);
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeInstantiatePrefabDeferred(const char* asset_guid, DirectX::XMFLOAT3 position,
        DirectX::XMFLOAT3 rotation_euler, DirectX::XMFLOAT3 scale,
        Runtime::ObjectHandle parent) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        return StatusCode(g_runtime_context->InstantiatePrefabDeferred(CString(asset_guid),
            position, rotation_euler, scale, parent));
    }

    int NativeFlushDeferredOperations() noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        g_runtime_context->FlushDeferredOperations();
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativePendingDeferredOperationCount(std::uint64_t* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        *out = static_cast<std::uint64_t>(g_runtime_context->PendingDeferredOperationCount());
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeHasComponent(Runtime::ObjectHandle object, std::uint32_t type_id, int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        if (!g_runtime_context->IsValid(object)) return StatusCode(RuntimeStatus::InvalidHandle);
        *out = g_runtime_context->HasComponent(object, type_id) ? 1 : 0;
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeGetTimeScale(float* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0.0f;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        *out = g_runtime_context->TimeScale();
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativeGetSceneTransitionInProgress(int* out) noexcept
    {
        if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
        *out = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        *out = g_runtime_context->SceneTransitionInProgress() ? 1 : 0;
        return StatusCode(RuntimeStatus::Ok);
    }

    int NativePhysicsAvailable() noexcept
    {
        return g_runtime_context != nullptr && g_runtime_context->PhysicsAvailable() ? 1 : 0;
    }

    int NativeSceneFlowAvailable() noexcept
    {
        return g_runtime_context != nullptr && g_runtime_context->SceneFlowAvailable() ? 1 : 0;
    }

    int NativePhysicsQuery(NativePhysicsQueryRequest request,
        NativePhysicsQueryHit* output, int capacity, int* count) noexcept
    {
        if (count == nullptr || capacity < 0 || (capacity > 0 && output == nullptr))
            return StatusCode(RuntimeStatus::InvalidArgument);
        *count = 0;
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());

        Runtime::PhysicsQueryRequest native_request;
        native_request.kind = static_cast<Runtime::PhysicsQueryKind>(request.kind);
        native_request.point_a = request.point_a;
        native_request.point_b = request.point_b;
        native_request.direction = request.direction;
        native_request.rotation = request.rotation;
        native_request.half_extents = request.half_extents;
        native_request.radius = request.radius;
        native_request.max_distance = request.max_distance;
        native_request.layer = request.layer;
        native_request.mask = request.mask;
        native_request.ignore = request.ignore;

        std::vector<Scene::PhysicsQueryHit> hits;
        const RuntimeStatus status = g_runtime_context->PhysicsQuery(native_request, hits);
        if (status != RuntimeStatus::Ok) return StatusCode(status);
        *count = static_cast<int>(hits.size());
        const int written = (std::min)(capacity, *count);
        for (int index = 0; index < written; ++index)
        {
            const Scene::PhysicsQueryHit& source = hits[static_cast<std::size_t>(index)];
            NativePhysicsQueryHit& target = output[index];
            target.point = source.point;
            target.normal = source.normal;
            target.distance = source.distance;
            target.fraction = source.fraction;
            target.collider_id = source.source.collider;
            target.valid = source.valid ? 1 : 0;
            if (source.source.object.Valid())
                target.object = g_runtime_context->FindByObjectID(source.source.object);
        }
        return StatusCode(RuntimeStatus::Ok);
    }
}
