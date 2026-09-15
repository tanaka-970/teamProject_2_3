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

// Native Motion callback の関数本体

        int NativeFindMotionPlayer(Runtime::ObjectHandle owner, const char* key,
            Runtime::ComponentHandle* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = Runtime::ComponentHandle::None();
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->FindMotionPlayer(owner,
                CString(key), *out));
        }

        int NativeMotionPlay(Runtime::ComponentHandle player) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->MotionPlay(player));
        }

        int NativeMotionPlayFrom(Runtime::ComponentHandle player, float seconds) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->MotionPlayFrom(player, seconds));
        }

        int NativeMotionPause(Runtime::ComponentHandle player) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->MotionPause(player));
        }

        int NativeMotionResume(Runtime::ComponentHandle player) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->MotionResume(player));
        }

        int NativeMotionStop(Runtime::ComponentHandle player) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->MotionStop(player));
        }

        int NativeMotionReverse(Runtime::ComponentHandle player) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->MotionReverse(player));
        }

        int NativeMotionSetTime(Runtime::ComponentHandle player, float seconds) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetMotionTime(player, seconds));
        }

        int NativeMotionSetSpeed(Runtime::ComponentHandle player, float speed) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetMotionSpeed(player, speed));
        }

        int NativeMotionSetWeight(Runtime::ComponentHandle player, float weight) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetMotionWeight(player, weight));
        }

        int NativeMotionIsPlaying(Runtime::ComponentHandle player, int* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = 0;
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            bool value = false;
            const RuntimeStatus status = g_runtime_context->IsMotionPlaying(player, value);
            if (Runtime::Failed(status)) return StatusCode(status);
            *out = value ? 1 : 0;
            return StatusCode(RuntimeStatus::Ok);
        }

        int NativeMotionGetTime(Runtime::ComponentHandle player, float* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = 0.0f;
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->GetMotionTime(player, *out));
        }

        int NativeMotionGetDuration(Runtime::ComponentHandle player, float* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = 0.0f;
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->GetMotionDuration(player, *out));
        }


// Native Composition callback の関数本体

        int NativeFindCompositionPlayer(Runtime::ObjectHandle owner, const char* key,
            Runtime::ComponentHandle* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = Runtime::ComponentHandle::None();
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->FindCompositionPlayer(owner,
                CString(key), *out));
        }

        int NativeCompositionPlay(Runtime::ComponentHandle player) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->CompositionPlay(player));
        }

        int NativeCompositionPause(Runtime::ComponentHandle player) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->CompositionPause(player));
        }

        int NativeCompositionResume(Runtime::ComponentHandle player) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->CompositionResume(player));
        }

        int NativeCompositionStop(Runtime::ComponentHandle player) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->CompositionStop(player));
        }

        int NativeCompositionSetTime(Runtime::ComponentHandle player, float seconds) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetCompositionTime(player, seconds));
        }

        int NativeCompositionSetSpeed(Runtime::ComponentHandle player, float speed) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetCompositionSpeed(player, speed));
        }

        int NativeCompositionSetWeight(Runtime::ComponentHandle player, float weight) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->SetCompositionWeight(player, weight));
        }

        int NativeCompositionIsPlaying(Runtime::ComponentHandle player, int* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = 0;
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            bool value = false;
            const int status = StatusCode(g_runtime_context->IsCompositionPlaying(player, value));
            *out = value ? 1 : 0;
            return status;
        }

        int NativeCompositionGetTime(Runtime::ComponentHandle player, float* out) noexcept
        {
            if (out == nullptr) return StatusCode(RuntimeStatus::InvalidArgument);
            *out = 0.0f;
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            return StatusCode(g_runtime_context->GetCompositionTime(player, *out));
        }
}
