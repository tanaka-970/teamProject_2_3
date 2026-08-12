// Runtime main のうち「D3D11 / DXGI Live Object 診断」を持つ。
// Report の集計とファイル出力の関数本体は分割前のまま移動している。
#include <time.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_DEBUG)
#include <dxgidebug.h>
#endif

#include "framework.h"
#include "mainInternal.h"

namespace ReplayEngine::Runtime::Detail
{
    const char* D3D11MessageCategoryName(D3D11_MESSAGE_CATEGORY category) noexcept
    {
        switch (category)
        {
        case D3D11_MESSAGE_CATEGORY_APPLICATION_DEFINED: return "APPLICATION_DEFINED";
        case D3D11_MESSAGE_CATEGORY_MISCELLANEOUS: return "MISCELLANEOUS";
        case D3D11_MESSAGE_CATEGORY_INITIALIZATION: return "INITIALIZATION";
        case D3D11_MESSAGE_CATEGORY_CLEANUP: return "CLEANUP";
        case D3D11_MESSAGE_CATEGORY_COMPILATION: return "COMPILATION";
        case D3D11_MESSAGE_CATEGORY_STATE_CREATION: return "STATE_CREATION";
        case D3D11_MESSAGE_CATEGORY_STATE_SETTING: return "STATE_SETTING";
        case D3D11_MESSAGE_CATEGORY_STATE_GETTING: return "STATE_GETTING";
        case D3D11_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "RESOURCE_MANIPULATION";
        case D3D11_MESSAGE_CATEGORY_EXECUTION: return "EXECUTION";
        case D3D11_MESSAGE_CATEGORY_SHADER: return "SHADER";
        default: return "UNKNOWN_CATEGORY";
        }
    }

    const char* D3D11MessageSeverityName(D3D11_MESSAGE_SEVERITY severity) noexcept
    {
        switch (severity)
        {
        case D3D11_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
        case D3D11_MESSAGE_SEVERITY_ERROR: return "ERROR";
        case D3D11_MESSAGE_SEVERITY_WARNING: return "WARNING";
        case D3D11_MESSAGE_SEVERITY_INFO: return "INFO";
        case D3D11_MESSAGE_SEVERITY_MESSAGE: return "MESSAGE";
        default: return "UNKNOWN_SEVERITY";
        }
    }

    bool TryParseUnsigned(const std::string& text, std::size_t offset,
        std::uint64_t& value) noexcept
    {
        while (offset < text.size() &&
            !std::isdigit(static_cast<unsigned char>(text[offset])))
        {
            ++offset;
        }
        if (offset >= text.size()) return false;

        std::uint64_t parsed = 0;
        while (offset < text.size() &&
            std::isdigit(static_cast<unsigned char>(text[offset])))
        {
            parsed = parsed * 10u +
                static_cast<std::uint64_t>(text[offset] - '0');
            ++offset;
        }
        value = parsed;
        return true;
    }

    struct D3D11StoredMessage
    {
        D3D11_MESSAGE_CATEGORY category{};
        D3D11_MESSAGE_SEVERITY severity{};
        D3D11_MESSAGE_ID id{};
        std::string description;
    };

    void AccumulateD3D11LiveObjectSummary(const std::string& description,
        D3D11LiveObjectFileSummary& summary)
    {
        if (description.find("Live ID3D11Device") != std::string::npos)
        {
            ++summary.live_device_lines;
            const std::size_t refcount_position = description.find("Refcount:");
            std::uint64_t parsed_refcount = 0;
            if (refcount_position != std::string::npos &&
                TryParseUnsigned(description, refcount_position + 9u,
                    parsed_refcount))
            {
                summary.live_device_refcount = parsed_refcount;
                summary.live_device_refcount_found = true;
            }
            return;
        }

        if (description.find("Live ID3D11DeviceContext") != std::string::npos)
        {
            ++summary.live_context_lines;
            return;
        }

        if (description.find("Live ID3D11Debug") != std::string::npos ||
            description.find("Live ID3D11InfoQueue") != std::string::npos)
        {
            ++summary.live_debug_interface_lines;
            return;
        }

        const bool typed_live_object_line =
            description.find("Live ") != std::string::npos &&
            description.find(" at ") != std::string::npos &&
            description.find("Refcount:") != std::string::npos;
        if (description.find("Live Object at") != std::string::npos ||
            typed_live_object_line)
        {
            ++summary.live_object_detail_lines;
            return;
        }

        if (description.find("Refcount") != std::string::npos)
        {
            return;
        }

        const std::size_t live_position = description.find("Live");
        const std::size_t object_position = description.find("Object");
        const std::size_t colon_position =
            object_position != std::string::npos ?
            description.find(':', object_position) : std::string::npos;
        if (live_position == std::string::npos ||
            object_position == std::string::npos ||
            colon_position == std::string::npos)
        {
            return;
        }

        std::uint64_t parsed_count = 0;
        if (TryParseUnsigned(description, colon_position + 1u, parsed_count))
        {
            summary.live_object_summary_count = parsed_count;
            summary.live_object_summary_found = true;
        }
    }

    D3D11LiveObjectFileSummary WriteD3D11LiveObjectReportFile(
        ID3D11InfoQueue* info_queue, bool report_available,
        HRESULT report_result)
    {
        const std::filesystem::path validation_folder = ValidationFolder();
        std::error_code directory_error;
        std::filesystem::create_directories(validation_folder, directory_error);

        D3D11LiveObjectFileSummary summary{};
        std::vector<D3D11StoredMessage> messages;
        HRESULT queue_read_result = info_queue != nullptr ? S_OK : E_NOINTERFACE;
        if (info_queue)
        {
            summary.stored_messages =
                info_queue->GetNumStoredMessagesAllowedByRetrievalFilter();
            messages.reserve(static_cast<std::size_t>(
                (std::min)(summary.stored_messages, static_cast<std::uint64_t>(4096))));
            for (std::uint64_t index = 0; index < summary.stored_messages; ++index)
            {
                SIZE_T message_size = 0;
                queue_read_result = info_queue->GetMessage(index, nullptr, &message_size);
                if (FAILED(queue_read_result) || message_size == 0) break;

                std::vector<char> storage(message_size);
                D3D11_MESSAGE* message =
                    reinterpret_cast<D3D11_MESSAGE*>(storage.data());
                queue_read_result = info_queue->GetMessage(index, message, &message_size);
                if (FAILED(queue_read_result)) break;

                D3D11StoredMessage stored{};
                stored.category = message->Category;
                stored.severity = message->Severity;
                stored.id = message->ID;
                if (message->pDescription != nullptr &&
                    message->DescriptionByteLength > 0)
                {
                    stored.description.assign(message->pDescription,
                        message->DescriptionByteLength);
                    while (!stored.description.empty() &&
                        stored.description.back() == '\0')
                    {
                        stored.description.pop_back();
                    }
                }
                AccumulateD3D11LiveObjectSummary(stored.description, summary);
                messages.push_back(std::move(stored));
                ++summary.readable_messages;
            }
        }

        const std::filesystem::path report_path =
            validation_folder / "D3D11LiveObjects.txt";
        std::ofstream report(report_path, std::ios::binary | std::ios::trunc);
        if (report)
        {
            report << "REPLAY_D3D11_LIVE_OBJECT_REPORT 1\n";
            report << "D3D11_DEBUG_AVAILABLE " << (report_available ? 1 : 0) << '\n';
            report << "D3D11_INFO_QUEUE_AVAILABLE " << (info_queue != nullptr ? 1 : 0) << '\n';
            report << "REPORT_HRESULT 0x" << std::hex << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(report_result)
                << std::dec << std::setfill(' ') << '\n';
            report << "INFO_QUEUE_READ_HRESULT 0x" << std::hex << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(queue_read_result)
                << std::dec << std::setfill(' ') << '\n';
            report << "INFO_QUEUE_STORED_MESSAGES " << summary.stored_messages << '\n';
            report << "INFO_QUEUE_READABLE_MESSAGES " << summary.readable_messages << '\n';
            report << "LIVE_OBJECT_DETAIL_LINES " << summary.live_object_detail_lines << '\n';
            report << "LIVE_OBJECT_SUMMARY_COUNT ";
            if (summary.live_object_summary_found) report << summary.live_object_summary_count;
            else report << "UNKNOWN";
            report << '\n';
            report << "LIVE_DEVICE_LINES " << summary.live_device_lines << '\n';
            report << "LIVE_DEVICE_REFCOUNT ";
            if (summary.live_device_refcount_found) report << summary.live_device_refcount;
            else report << "UNKNOWN";
            report << '\n';
            report << "LIVE_CONTEXT_LINES " << summary.live_context_lines << '\n';
            report << "LIVE_DEBUG_INTERFACE_LINES "
                << summary.live_debug_interface_lines;
            report << "\n\n";

            for (std::size_t index = 0; index < messages.size(); ++index)
            {
                const D3D11StoredMessage& message = messages[index];
                report << '[' << index << "] "
                    << D3D11MessageSeverityName(message.severity) << ' '
                    << D3D11MessageCategoryName(message.category)
                    << " #" << static_cast<int>(message.id) << '\n'
                    << message.description << "\n\n";
            }
        }

        if (info_queue) info_queue->ClearStoredMessages();
        return summary;
    }

#if defined(_DEBUG)
    void AccumulateDXGILiveObjectSummary(const std::string& description,
        DXGILiveObjectFileSummary& summary) noexcept
    {
        // DXGI の ReportLiveObjects は Description に "Live ..." を出す。
        // Summary 行や内部メッセージを数えず、実体の Live 行だけを合格判定に使う。
        if (description.find("Live ") == std::string::npos) return;
        ++summary.live_object_lines;
        if (description.find("Live ID3D11Device") != std::string::npos)
        {
            ++summary.live_d3d11_device_lines;
        }
    }

    DXGILiveObjectFileSummary WriteDXGILiveObjectReportFile(
        IDXGIInfoQueue* info_queue, bool report_available, HRESULT report_result)
    {
        const std::filesystem::path validation_folder = ValidationFolder();
        std::error_code directory_error;
        std::filesystem::create_directories(validation_folder, directory_error);

        DXGILiveObjectFileSummary summary{};
        std::vector<std::string> messages;
        HRESULT queue_read_result = info_queue != nullptr ? S_OK : E_NOINTERFACE;
        if (info_queue != nullptr)
        {
            summary.stored_messages =
                info_queue->GetNumStoredMessagesAllowedByRetrievalFilters(DXGI_DEBUG_ALL);
            messages.reserve(static_cast<std::size_t>(
                (std::min)(summary.stored_messages, static_cast<std::uint64_t>(4096))));
            for (std::uint64_t index = 0; index < summary.stored_messages; ++index)
            {
                SIZE_T message_size = 0;
                queue_read_result = info_queue->GetMessage(
                    DXGI_DEBUG_ALL, index, nullptr, &message_size);
                if (FAILED(queue_read_result) || message_size == 0) break;

                std::vector<unsigned char> storage(message_size);
                DXGI_INFO_QUEUE_MESSAGE* message =
                    reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(storage.data());
                queue_read_result = info_queue->GetMessage(
                    DXGI_DEBUG_ALL, index, message, &message_size);
                if (FAILED(queue_read_result)) break;

                std::string description;
                if (message->pDescription != nullptr &&
                    message->DescriptionByteLength > 0)
                {
                    description.assign(message->pDescription,
                        message->DescriptionByteLength);
                    while (!description.empty() && description.back() == '\0')
                    {
                        description.pop_back();
                    }
                }
                AccumulateDXGILiveObjectSummary(description, summary);
                messages.push_back(std::move(description));
                ++summary.readable_messages;
            }
        }

        const std::filesystem::path report_path =
            validation_folder / "DXGILiveObjects.txt";
        std::ofstream report(report_path, std::ios::binary | std::ios::trunc);
        if (report)
        {
            report << "REPLAY_DXGI_LIVE_OBJECT_REPORT 1\n";
            report << "DXGI_DEBUG_AVAILABLE " << (report_available ? 1 : 0) << '\n';
            report << "DXGI_INFO_QUEUE_AVAILABLE " << (info_queue != nullptr ? 1 : 0) << '\n';
            report << "REPORT_HRESULT 0x" << std::hex << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(report_result)
                << std::dec << std::setfill(' ') << '\n';
            report << "INFO_QUEUE_READ_HRESULT 0x" << std::hex << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(queue_read_result)
                << std::dec << std::setfill(' ') << '\n';
            report << "DXGI_INFO_QUEUE_STORED_MESSAGES " << summary.stored_messages << '\n';
            report << "DXGI_INFO_QUEUE_READABLE_MESSAGES " << summary.readable_messages << '\n';
            report << "DXGI_LIVE_OBJECT_LINES " << summary.live_object_lines << '\n';
            report << "DXGI_LIVE_D3D11_DEVICE_LINES "
                << summary.live_d3d11_device_lines << "\n\n";
            for (std::size_t index = 0; index < messages.size(); ++index)
            {
                report << '[' << index << "] " << messages[index] << '\n';
            }
        }
        if (info_queue != nullptr) info_queue->ClearStoredMessages(DXGI_DEBUG_ALL);
        return summary;
    }

    bool AcquireDXGIDebugInterfaces(
        Microsoft::WRL::ComPtr<IDXGIDebug1>& debug,
        Microsoft::WRL::ComPtr<IDXGIInfoQueue>& info_queue,
        HMODULE& module) noexcept
    {
        // Debug interface は Runtime の所有物にしない。
        // Device を作る前からプロセス全体を追跡し、Device 解放後に検査するため
        // WinMain のローカル寿命だけで保持する。
        // GetProcAddress 用に自分の module ref を 1 本持ち、最終レポート後に
        // 必ず FreeLibrary する。GetModuleHandle の借用参照に依存すると、
        // 起動順によって関数ポインタの寿命が変わるため。
        module = ::LoadLibraryW(L"dxgi.dll");
        if (module == nullptr) return false;

        using GetDebugInterface1Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
        const auto get_debug_interface = reinterpret_cast<GetDebugInterface1Fn>(
            ::GetProcAddress(module, "DXGIGetDebugInterface1"));
        if (get_debug_interface == nullptr)
        {
            ::FreeLibrary(module);
            module = nullptr;
            return false;
        }

        debug.Reset();
        info_queue.Reset();
        HRESULT result = get_debug_interface(0, __uuidof(IDXGIDebug1),
            reinterpret_cast<void**>(debug.GetAddressOf()));
        if (FAILED(result) || !debug)
        {
            ::FreeLibrary(module);
            module = nullptr;
            return false;
        }

        result = get_debug_interface(0, __uuidof(IDXGIInfoQueue),
            reinterpret_cast<void**>(info_queue.GetAddressOf()));
        if (FAILED(result) || !info_queue)
        {
            debug.Reset();
            ::FreeLibrary(module);
            module = nullptr;
            return false;
        }

        info_queue->SetMessageCountLimit(DXGI_DEBUG_ALL, UINT64_MAX);
        info_queue->PushEmptyStorageFilter(DXGI_DEBUG_ALL);
        info_queue->ClearStoredMessages(DXGI_DEBUG_ALL);
        return true;
    }
#endif
}
