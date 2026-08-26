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
#include <cmath>
#include <iomanip>
#include <iterator>
#include <locale>
#include <limits>
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


        std::string HexEncodeEventPayload(std::string_view text)
        {
            static constexpr char digits[] = "0123456789ABCDEF";
            std::string result;
            result.reserve(text.size() * 2);
            for (const unsigned char c : text)
            {
                result.push_back(digits[(c >> 4) & 0x0f]);
                result.push_back(digits[c & 0x0f]);
            }
            return result;
        }

        int HexEventPayloadDigit(char c) noexcept
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        }

        bool HexDecodeEventPayload(std::string_view text, std::string& out)
        {
            out.clear();
            if ((text.size() % 2) != 0) return false;
            out.reserve(text.size() / 2);
            for (std::size_t i = 0; i < text.size(); i += 2)
            {
                const int high = HexEventPayloadDigit(text[i]);
                const int low = HexEventPayloadDigit(text[i + 1]);
                if (high < 0 || low < 0) return false;
                out.push_back(static_cast<char>((high << 4) | low));
            }
            return true;
        }

        std::string EventPayloadDoubleText(double value)
        {
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            stream << std::setprecision(17) << value;
            return stream.str();
        }

        RuntimeStatus DecodeEventPayload(std::string_view text,
            Reflection::PropertyBag& payload)
        {
            std::size_t offset = 0;
            while (offset < text.size())
            {
                const std::size_t end = text.find('\n', offset);
                std::string_view line = end == std::string_view::npos
                    ? text.substr(offset) : text.substr(offset, end - offset);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                offset = end == std::string_view::npos ? text.size() : end + 1;
                if (line.empty()) continue;

                const std::size_t first = line.find(':');
                const std::size_t second = first == std::string_view::npos
                    ? std::string_view::npos : line.find(':', first + 1);
                if (first != 1 || second == std::string_view::npos)
                    return RuntimeStatus::InvalidArgument;

                std::string key;
                if (!HexDecodeEventPayload(line.substr(first + 1, second - first - 1), key))
                    return RuntimeStatus::InvalidArgument;
                const std::string_view value_text = line.substr(second + 1);

                switch (line[0])
                {
                case 'b':
                    if (value_text == "1")
                        payload.Set(std::move(key), Reflection::PropertyValue::MakeBool(true));
                    else if (value_text == "0")
                        payload.Set(std::move(key), Reflection::PropertyValue::MakeBool(false));
                    else
                        return RuntimeStatus::InvalidArgument;
                    break;
                case 'i':
                {
                    int value = 0;
                    const auto parsed = std::from_chars(value_text.data(),
                        value_text.data() + value_text.size(), value);
                    if (parsed.ec != std::errc{} || parsed.ptr != value_text.data() + value_text.size())
                        return RuntimeStatus::InvalidArgument;
                    payload.Set(std::move(key), Reflection::PropertyValue::MakeInt(value));
                    break;
                }
                case 'd':
                {
                    std::istringstream stream{ std::string(value_text) };
                    stream.imbue(std::locale::classic());
                    double value = 0.0;
                    stream >> value;
                    if (!stream || !std::isfinite(value))
                        return RuntimeStatus::InvalidArgument;
                    stream >> std::ws;
                    if (!stream.eof()) return RuntimeStatus::InvalidArgument;
                    payload.Set(std::move(key), Reflection::PropertyValue::MakeDouble(value));
                    break;
                }
                case 's':
                {
                    std::string value;
                    if (!HexDecodeEventPayload(value_text, value))
                        return RuntimeStatus::InvalidArgument;
                    payload.Set(std::move(key), Reflection::PropertyValue::MakeString(std::move(value)));
                    break;
                }
                default:
                    return RuntimeStatus::InvalidArgument;
                }
            }
            return RuntimeStatus::Ok;
        }

        std::string EncodeEventRecord(const Runtime::EventRecord& record)
        {
            std::ostringstream stream;
            stream << "type=" << record.type.ToString() << '\n';
            stream << "name=" << EscapeEventValue(record.type_name) << '\n';
            stream << "frame=" << record.frame_index << '\n';
            AppendHandle(stream, "source", record.source);
            AppendHandle(stream, "target", record.target);

            for (const Reflection::PropertyBag::Entry& entry : record.payload.Entries())
            {
                const std::string key = HexEncodeEventPayload(entry.name);
                switch (entry.value.Type())
                {
                case Reflection::PropertyType::Bool:
                    stream << "payload=b:" << key << ':'
                        << (entry.value.AsBool() ? '1' : '0') << '\n';
                    break;
                case Reflection::PropertyType::Int:
                    stream << "payload=i:" << key << ':' << entry.value.AsInt() << '\n';
                    break;
                case Reflection::PropertyType::Float:
                    stream << "payload=d:" << key << ':'
                        << EventPayloadDoubleText(static_cast<double>(entry.value.AsFloat())) << '\n';
                    break;
                case Reflection::PropertyType::Double:
                    stream << "payload=d:" << key << ':'
                        << EventPayloadDoubleText(entry.value.AsDouble()) << '\n';
                    break;
                case Reflection::PropertyType::String:
                    stream << "payload=s:" << key << ':'
                        << HexEncodeEventPayload(entry.value.AsString()) << '\n';
                    break;
                default:
                    break;
                }
            }
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

        int NativePublishEvent(std::uint64_t high, std::uint64_t low, const char* type_name,
            Runtime::ObjectHandle source, Runtime::ObjectHandle target) noexcept
        {
            if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
            const Reflection::TypeGUID type{ high, low };
            if (!type.IsValid()) return StatusCode(RuntimeStatus::InvalidArgument);

            Runtime::EventRecord record;
            record.type = type;
            record.type_name = CString(type_name);
            record.source = source;
            record.target = target;
            return StatusCode(g_runtime_context->PublishEvent(std::move(record)));
        }

}

namespace ReplayEngine::Scripting::CSharp::Detail
{
    int NativePollEventWithPayload(std::uint64_t subscription, char* output,
        int output_capacity, int* required_capacity) noexcept
    {
        if (required_capacity == nullptr || output_capacity < 0)
            return StatusCode(RuntimeStatus::InvalidArgument);
        *required_capacity = 0;
        if (output != nullptr && output_capacity > 0) output[0] = '\0';

        auto it = g_event_subscriptions.find(subscription);
        if (it == g_event_subscriptions.end())
            return StatusCode(RuntimeStatus::InvalidHandle);
        if (it->second.pending.empty())
            return StatusCode(RuntimeStatus::Ok);

        const std::string& event_text = it->second.pending.front();
        if (event_text.size() >= static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            return StatusCode(RuntimeStatus::UnsupportedOperation);
        *required_capacity = static_cast<int>(event_text.size() + 1);

        if (output == nullptr || output_capacity == 0)
            return StatusCode(RuntimeStatus::Ok);
        if (output_capacity < *required_capacity)
            return StatusCode(RuntimeStatus::InvalidArgument);

        const int status = WriteNativeText(event_text, output, output_capacity);
        if (status == StatusCode(RuntimeStatus::Ok)) it->second.pending.pop_front();
        return status;
    }

    int NativePublishEventWithPayload(std::uint64_t high, std::uint64_t low,
        const char* type_name, Runtime::ObjectHandle source, Runtime::ObjectHandle target,
        const char* payload_text) noexcept
    {
        if (g_runtime_context == nullptr) return StatusCode(ContextUnavailable());
        const Reflection::TypeGUID type{ high, low };
        if (!type.IsValid()) return StatusCode(RuntimeStatus::InvalidArgument);

        Runtime::EventRecord record;
        record.type = type;
        record.type_name = CString(type_name);
        record.source = source;
        record.target = target;
        const RuntimeStatus payload_status = DecodeEventPayload(CString(payload_text), record.payload);
        if (payload_status != RuntimeStatus::Ok) return StatusCode(payload_status);
        return StatusCode(g_runtime_context->PublishEvent(std::move(record)));
    }
}
