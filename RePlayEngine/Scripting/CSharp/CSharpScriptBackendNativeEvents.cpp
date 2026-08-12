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

// Native Event callback の関数本体

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

}
