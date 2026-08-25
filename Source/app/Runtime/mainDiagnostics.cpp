// Runtime main の DXGI Live Object 診断を持つ。
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
#if defined(_DEBUG)
    void AccumulateDXGILiveObjectSummary(const std::string& description,
        DXGILiveObjectFileSummary& summary) noexcept
    {
        // DXGI の ReportLiveObjects は Description に "Live ..." を出す。
        // Summary 行や内部メッセージを数えず、実体の Live 行だけを合格判定に使う。
        if (description.find("Live ") == std::string::npos) return;
        ++summary.live_object_lines;
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
            report << "\n";
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
