// Runtime main のうち「window_procedure / WinMain とアプリケーション実行」を持つ。
// 起動設定、検証ディスパッチ、Runtime smoke、終了診断の呼び出し順は変更しない。
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

using ReplayEngine::Runtime::Detail::ParseShutdownRegression;
using ReplayEngine::Runtime::Detail::ParseStartupSceneBoot;
using ReplayEngine::Runtime::Detail::ResolveExecutableLayout;
using ReplayEngine::Runtime::Detail::ExecutableLayout;
using ReplayEngine::Runtime::Detail::LoadGameLaunchConfig;
using ReplayEngine::Runtime::Detail::GameLaunchConfig;
using ReplayEngine::Runtime::Detail::ParseAutomatedSmokeTestFrames;
using ReplayEngine::Runtime::Detail::D3D11LiveObjectFileSummary;
using ReplayEngine::Runtime::Detail::WriteD3D11LiveObjectReportFile;
using ReplayEngine::Runtime::Detail::ValidationFolder;
using ReplayEngine::Runtime::Detail::RunHeadlessLargeSceneValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessSceneValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessMaterialValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessLandscapeValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessPrefabValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessHandleValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessCameraComponentValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessPlayerSpeedValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessSerializationValidation;
#if defined(_DEBUG)
using ReplayEngine::Runtime::Detail::DXGILiveObjectFileSummary;
using ReplayEngine::Runtime::Detail::AcquireDXGIDebugInterfaces;
using ReplayEngine::Runtime::Detail::WriteDXGILiveObjectReportFile;
#endif

LRESULT CALLBACK window_procedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	framework* p{ reinterpret_cast<framework*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)) };
	return p ? p->handle_message(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(_In_ HINSTANCE instance, _In_opt_  HINSTANCE prev_instance, _In_ LPSTR cmd_line, _In_ int cmd_show)
{
    const int large_scene_validation_result = RunHeadlessLargeSceneValidation(cmd_line);
    if (large_scene_validation_result >= 0) return large_scene_validation_result;
    const int validation_result = RunHeadlessSceneValidation(cmd_line);
    if (validation_result >= 0) return validation_result;
    const int material_validation_result = RunHeadlessMaterialValidation(cmd_line);
    if (material_validation_result >= 0) return material_validation_result;
    const int landscape_validation_result = RunHeadlessLandscapeValidation(cmd_line);
    if (landscape_validation_result >= 0) return landscape_validation_result;
    const int prefab_validation_result = RunHeadlessPrefabValidation(cmd_line);
    if (prefab_validation_result >= 0) return prefab_validation_result;
    const int handle_validation_result = RunHeadlessHandleValidation(cmd_line);
    if (handle_validation_result >= 0) return handle_validation_result;
    const int camera_component_validation_result =
        RunHeadlessCameraComponentValidation(cmd_line);
    if (camera_component_validation_result >= 0) return camera_component_validation_result;
    const int player_speed_validation_result = RunHeadlessPlayerSpeedValidation(cmd_line);
    if (player_speed_validation_result >= 0) return player_speed_validation_result;
    const int serialization_validation_result = RunHeadlessSerializationValidation(cmd_line);
    if (serialization_validation_result >= 0) return serialization_validation_result;
    const std::uint32_t automated_smoke_test_frames =
        ParseAutomatedSmokeTestFrames(cmd_line);
    const bool shutdown_regression_requested = ParseShutdownRegression(cmd_line);

    // WICの画像読み込みはCOMを使うため、エンジンの生存期間中は初期化状態を維持する。
    // シーン切り替え後もWICファクトリを確実に利用できるようにする。
	const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // 読み込みの基準は exe の隣に置かれた配布フォルダ。
    // Visual Studio 配置(x64/Debug)だけは project root へ戻して従来の編集起動を保つ。
    const ExecutableLayout executable_layout = ResolveExecutableLayout();
    if (!executable_layout.content_root.empty())
        SetCurrentDirectoryW(executable_layout.content_root.wstring().c_str());
    const GameLaunchConfig game_launch =
        LoadGameLaunchConfig(executable_layout.executable_directory);
    for (const std::string& warning : game_launch.warnings)
    {
        const std::string message = "[ReplayGame] " + warning + "\n";
        OutputDebugStringA(message.c_str());
        std::fprintf(stderr, "%s", message.c_str());
    }

	srand(static_cast<unsigned int>(time(nullptr)));

#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = window_procedure;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = instance;
	wcex.hIcon = 0;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = APPLICATION_NAME;
	wcex.hIconSm = 0;
	RegisterClassExW(&wcex);

	RECT rc{ 0, 0,
        static_cast<LONG>(game_launch.window_width),
        static_cast<LONG>(game_launch.window_height) };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindowExW(0, APPLICATION_NAME, L"", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
		NULL, NULL, instance, NULL);

    int exit_code = 0;
    Microsoft::WRL::ComPtr<ID3D11Debug> d3d11_debug;
    Microsoft::WRL::ComPtr<ID3D11InfoQueue> d3d11_info_queue;
    D3D11LiveObjectFileSummary d3d11_live_report_summary{};
    HRESULT d3d11_live_report_result = E_NOINTERFACE;
    bool d3d11_live_report_available = false;
#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<IDXGIDebug1> dxgi_debug;
    Microsoft::WRL::ComPtr<IDXGIInfoQueue> dxgi_info_queue;
    HMODULE dxgi_debug_module = nullptr;
    DXGILiveObjectFileSummary dxgi_live_report_summary{};
    HRESULT dxgi_live_report_result = E_NOINTERFACE;
    const bool dxgi_live_report_available =
        AcquireDXGIDebugInterfaces(dxgi_debug, dxgi_info_queue, dxgi_debug_module);
#endif
    {
	    framework application(hwnd);
        application.configure_content_root(executable_layout.content_root);
        if (game_launch.file_found)
        {
            application.configure_standalone_game(
                executable_layout.content_root, game_launch.name);
            if (!game_launch.startup_scene.empty())
                application.set_startup_scene_path(game_launch.startup_scene);
        }
        application.set_startup_window_size(
            game_launch.window_width, game_launch.window_height);
        if (game_launch.fullscreen) application.request_startup_fullscreen();
        application.set_automated_smoke_test_frames(automated_smoke_test_frames);
        if (ParseStartupSceneBoot(cmd_line) || game_launch.file_found)
            application.request_startup_scene_boot();
        if (shutdown_regression_requested)
        {
            // 数フレーム描画してから終了させる。
            // 一度も描画せずに終わると、描画経路で作られるリソースを通らない。
            application.set_automated_smoke_test_frames(
                automated_smoke_test_frames > 0 ? automated_smoke_test_frames : 60u);
            application.request_shutdown_regression();
        }
	    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&application));
	    exit_code = application.run(
            automated_smoke_test_frames > 0 ? SW_HIDE : cmd_show);
        d3d11_debug = application.acquire_d3d11_debug();
        d3d11_info_queue = application.acquire_d3d11_info_queue();
    }

    // ReportLiveDeviceObjects が出す行だけを測りたいので、直前に古い警告を捨てる。
    // 出力後は WriteD3D11LiveObjectReportFile が全件を書いてから Clear する。
    if (d3d11_info_queue) d3d11_info_queue->ClearStoredMessages();
    if (d3d11_debug)
    {
        d3d11_live_report_available = true;
        d3d11_live_report_result = d3d11_debug->ReportLiveDeviceObjects(
            D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
        d3d11_live_report_summary = WriteD3D11LiveObjectReportFile(
            d3d11_info_queue.Get(), d3d11_live_report_available,
            d3d11_live_report_result);
        std::fprintf(stderr, "D3D11 Live Object Report: %s (0x%08lx)\n",
            SUCCEEDED(d3d11_live_report_result) ? "completed" : "failed",
            static_cast<unsigned long>(d3d11_live_report_result));
        std::fprintf(stderr,
            "D3D11 Live Object Details: %llu lines, summary ",
            static_cast<unsigned long long>(
                d3d11_live_report_summary.live_object_detail_lines));
        if (d3d11_live_report_summary.live_object_summary_found)
        {
            std::fprintf(stderr, "%llu",
                static_cast<unsigned long long>(
                    d3d11_live_report_summary.live_object_summary_count));
        }
        else
        {
            std::fprintf(stderr, "unknown");
        }
        std::fprintf(stderr,
            " (%llu info queue messages, Saved/Validation/D3D11LiveObjects.txt)\n",
            static_cast<unsigned long long>(
                d3d11_live_report_summary.readable_messages));
        if (FAILED(d3d11_live_report_result) && exit_code == 0) exit_code = 73;
    }
    else
    {
        d3d11_live_report_summary = WriteD3D11LiveObjectReportFile(
            d3d11_info_queue.Get(), d3d11_live_report_available,
            d3d11_live_report_result);
    }
    d3d11_info_queue.Reset();
    d3d11_debug.Reset();

#if defined(_DEBUG)
    // D3D11 Debug 自身が Device を生かす参照を手放したあとで、
    // プロセス全体を追跡する DXGI から最終確認する。
    if (dxgi_live_report_available && dxgi_debug && dxgi_info_queue)
    {
        dxgi_info_queue->ClearStoredMessages(DXGI_DEBUG_ALL);
        dxgi_live_report_result = dxgi_debug->ReportLiveObjects(
            DXGI_DEBUG_ALL, static_cast<DXGI_DEBUG_RLO_FLAGS>(
                DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
        dxgi_live_report_summary = WriteDXGILiveObjectReportFile(
            dxgi_info_queue.Get(), true, dxgi_live_report_result);
        std::fprintf(stderr,
            "DXGI Live Object Report: %llu live lines (%s)\n",
            static_cast<unsigned long long>(dxgi_live_report_summary.live_object_lines),
            SUCCEEDED(dxgi_live_report_result) ? "completed" : "failed");
        if (FAILED(dxgi_live_report_result) && exit_code == 0) exit_code = 74;
        if (shutdown_regression_requested &&
            dxgi_live_report_summary.live_object_lines != 0 && exit_code == 0)
        {
            exit_code = 75;
        }
    }
    else
    {
        dxgi_live_report_summary = WriteDXGILiveObjectReportFile(
            dxgi_info_queue.Get(), false, dxgi_live_report_result);
    }
    dxgi_info_queue.Reset();
    dxgi_debug.Reset();
    if (dxgi_debug_module != nullptr)
    {
        ::FreeLibrary(dxgi_debug_module);
        dxgi_debug_module = nullptr;
    }
#endif

	if (automated_smoke_test_frames > 0)
    {
        std::fprintf(stderr, "Runtime smoke test: %u rendered frames, exit code %d\n",
            automated_smoke_test_frames, exit_code);
        const std::filesystem::path validation_folder = ValidationFolder();
        std::error_code directory_error;
        std::filesystem::create_directories(validation_folder, directory_error);
        std::ofstream report(validation_folder / "RuntimeSmoke.txt",
            std::ios::binary | std::ios::trunc);
        if (report)
        {
            report << "REPLAY_RUNTIME_SMOKE 1\n";
            report << "RENDERED_FRAMES " << automated_smoke_test_frames << '\n';
            report << "EXIT_CODE " << exit_code << '\n';
            report << "D3D11_DEBUG_AVAILABLE " << (d3d11_live_report_available ? 1 : 0) << '\n';
            report << "D3D11_LIVE_REPORT_HRESULT 0x" << std::hex << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(
                    d3d11_live_report_result) << '\n';
            report << std::dec << std::setfill(' ');
            report << "D3D11_LIVE_OBJECT_DETAIL_LINES "
                << d3d11_live_report_summary.live_object_detail_lines << '\n';
            report << "D3D11_LIVE_OBJECT_SUMMARY_COUNT ";
            if (d3d11_live_report_summary.live_object_summary_found)
            {
                report << d3d11_live_report_summary.live_object_summary_count;
            }
            else
            {
                report << "UNKNOWN";
            }
            report << '\n';
        }
    }
	if (SUCCEEDED(com_result)) CoUninitialize();
	return exit_code;
}

