// Runtime main のうち「window_procedure / WinMain とアプリケーション実行」を持つ。
// 起動設定、検証ディスパッチ、Runtime smoke、終了診断の呼び出し順は変更しない。
#include <time.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
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
using ReplayEngine::Runtime::Detail::ParseCaptureFrame;
using ReplayEngine::Runtime::Detail::ParseCaptureExclusiveFrame;
using ReplayEngine::Runtime::Detail::ResolveExecutableLayout;
using ReplayEngine::Runtime::Detail::ExecutableLayout;
using ReplayEngine::Runtime::Detail::LoadGameLaunchConfig;
using ReplayEngine::Runtime::Detail::GameLaunchConfig;
using ReplayEngine::Runtime::Detail::ParseAutomatedSmokeTestFrames;
using ReplayEngine::Runtime::Detail::ParseProfileBenchmark;
using ReplayEngine::Runtime::Detail::ProfileBenchmarkConfig;
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
using ReplayEngine::Runtime::Detail::RunHeadlessDX12Validation;
using ReplayEngine::Runtime::Detail::RunHeadlessDXCValidation;
using ReplayEngine::Runtime::Detail::RunHeadlessEditorHelpValidation;
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
    const int editor_help_validation_result = RunHeadlessEditorHelpValidation(cmd_line);
    if (editor_help_validation_result >= 0) return editor_help_validation_result;
    const int dx12_validation_result = RunHeadlessDX12Validation(cmd_line);
    if (dx12_validation_result >= 0) return dx12_validation_result;
    const int dxc_validation_result = RunHeadlessDXCValidation(cmd_line);
    if (dxc_validation_result >= 0) return dxc_validation_result;
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
    const ProfileBenchmarkConfig profile_benchmark =
        ParseProfileBenchmark(cmd_line);
    if (!profile_benchmark.valid)
    {
        std::fprintf(stderr, "Profiler/screen-space command error: %s\n",
            profile_benchmark.error.c_str());
        return 75;
    }
    for (const std::string& warning : profile_benchmark.screen_space_warnings)
        std::fprintf(stderr, "%s\n", warning.c_str());
    const bool shutdown_regression_requested = ParseShutdownRegression(cmd_line);
    // Renderer選択は製品仕様としてDX12に固定する。旧移行フラグは互換的に
    // 受け付けても意味を持たず、通常起動・Editor・Capture・Validationを同じ経路へ揃える。
    const bool dx12_framework_requested = true;
    std::string capture_frame_name;
    const bool capture_frame_requested =
        ParseCaptureFrame(cmd_line, capture_frame_name);
    std::string capture_exclusive_frame_name;
    const bool capture_exclusive_frame_requested = !capture_frame_requested &&
        ParseCaptureExclusiveFrame(cmd_line, capture_exclusive_frame_name);

    // WICの画像読み込みはCOMを使うため、エンジンの生存期間中は初期化状態を維持する。
    // シーン切り替え後もWICファクトリを確実に利用できるようにする。
	const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // 読み込みの基準は exe の隣に置かれた配布フォルダ。
    // Visual Studio 配置(x64/Debug)だけは project root へ戻して従来の編集起動を保つ。
    const ExecutableLayout executable_layout = ResolveExecutableLayout();
    if (!executable_layout.content_root.empty())
        SetCurrentDirectoryW(executable_layout.content_root.wstring().c_str());
    std::filesystem::path resolved_profile_scene_path;
    if (profile_benchmark.requested)
    {
        resolved_profile_scene_path = profile_benchmark.scene.is_absolute()
            ? profile_benchmark.scene.lexically_normal()
            : (executable_layout.content_root / profile_benchmark.scene).lexically_normal();
        std::error_code profile_scene_error;
        if (!std::filesystem::is_regular_file(
            resolved_profile_scene_path, profile_scene_error) || profile_scene_error)
        {
            std::fprintf(stderr, "Profiler benchmark scene not found: %s\n",
                profile_benchmark.scene.u8string().c_str());
            if (SUCCEEDED(com_result)) CoUninitialize();
            return 76;
        }
    }
    const GameLaunchConfig game_launch =
        LoadGameLaunchConfig(executable_layout.executable_directory);
    for (const std::string& warning : game_launch.warnings)
    {
        const std::string message = "[ReplayGame] " + warning + "\n";
        OutputDebugStringA(message.c_str());
        std::fprintf(stderr, "%s", message.c_str());
    }

    if (profile_benchmark.requested)
        srand(0x5245504Cu); // "REPL": benchmark は毎回同じ C RNG 列を使う。
    else
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
	const wchar_t* window_title = dx12_framework_requested
		? L"3dgp - DX12" : APPLICATION_NAME;
	HWND hwnd = CreateWindowExW(0, APPLICATION_NAME, window_title,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
		NULL, NULL, instance, NULL);

    int exit_code = 0;
    bool capture_frame_ok = false;
    bool capture_exclusive_frame_attempted = false;
    std::string capture_frame_summary;
    std::uint32_t dx12_live_object_lines = 0;
    std::uint32_t dx12_live_object_detail_lines = 0;
    bool dx12_live_report_ok = true;
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
        application.request_dx12_framework();
        application.configure_screen_space_overrides(profile_benchmark.screen_space_overrides);
        if (game_launch.file_found && !profile_benchmark.requested)
        {
            application.configure_standalone_game(
                executable_layout.content_root, game_launch.name);
            if (!game_launch.startup_scene.empty())
                application.set_startup_scene_path(game_launch.startup_scene);
        }
        if (profile_benchmark.requested)
        {
            // 検証に使ったのと同じ絶対パスを Startup Scene へ渡す。
            // CWD / AssetDatabase の相対パス表現に benchmark の正否を依存させない。
            application.set_startup_scene_path(resolved_profile_scene_path);
            application.configure_profile_benchmark(profile_benchmark.frames,
                profile_benchmark.warmup_frames, profile_benchmark.output_name,
                profile_benchmark.render_output);
        }
        application.set_startup_window_size(
            game_launch.window_width, game_launch.window_height);
        if (game_launch.fullscreen) application.request_startup_fullscreen();
        application.set_automated_smoke_test_frames(automated_smoke_test_frames);
        if (ParseStartupSceneBoot(cmd_line) || game_launch.file_found ||
            profile_benchmark.requested)
        {
            application.request_startup_scene_boot();
        }
        if (shutdown_regression_requested)
        {
            // 数フレーム描画してから終了させる。
            // 一度も描画せずに終わると、描画経路で作られるリソースを通らない。
            // Profile benchmarkと同時指定した場合は、Profile側の終了条件を優先し、
            // 終了シナリオだけを同じメッセージループの後段で実行する。
            if (!profile_benchmark.requested)
            {
                application.set_automated_smoke_test_frames(
                    automated_smoke_test_frames > 0 ? automated_smoke_test_frames : 60u);
            }
            application.request_shutdown_regression();
        }

    if (capture_frame_requested)
        {
            // プロファイル撮影はプロファイル側で Runtime World の準備完了を
            // 起点に予約する。別の Smoke Test の終了時刻へ混ぜない。
            if (!profile_benchmark.requested)
            {
                application.set_automated_smoke_test_frames(
                    automated_smoke_test_frames > 0 ? automated_smoke_test_frames : 240u);
            }
            application.request_automated_frame_capture(capture_frame_name);
        }

        if (capture_exclusive_frame_requested)
        {
            if (!profile_benchmark.requested)
            {
                application.set_automated_smoke_test_frames(
                    automated_smoke_test_frames > 0 ? automated_smoke_test_frames : 240u);
            }
            application.request_automated_exclusive_frame_capture(
                capture_exclusive_frame_name);
        }
	    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&application));

	    // 通常の自動検証は従来どおり隠すが、撮影は実際に人が見る表示経路を通す。
	    const bool hide_automated_window = !capture_frame_requested &&
            !capture_exclusive_frame_requested &&
            (automated_smoke_test_frames > 0 || shutdown_regression_requested ||
                profile_benchmark.requested);
	    // DX12フレームワークの実機確認は明示的な起動要求なので、起動元が
	    // SW_HIDEを渡しても画面を表示し、実描画を確認できるようにする。
        const int requested_show_command = SW_SHOWNORMAL;
	    exit_code = application.run(
            hide_automated_window ? SW_HIDE : requested_show_command);
        if (capture_frame_requested)
        {
            capture_frame_ok = application.golden_last_capture_ok();
            capture_frame_summary = application.golden_last_capture_summary();
        }
        else if (capture_exclusive_frame_requested)
        {
            capture_exclusive_frame_attempted =
                application.automated_exclusive_frame_capture_attempted();
            capture_frame_ok = !capture_exclusive_frame_attempted ||
                application.golden_last_capture_ok();
            capture_frame_summary = application.golden_last_capture_summary();
        }
        dx12_live_object_lines = application.dx12_shutdown_live_object_lines();
        dx12_live_object_detail_lines = application.dx12_shutdown_live_object_detail_lines();
        dx12_live_report_ok = application.dx12_shutdown_live_object_report_ok();
    }

    if (shutdown_regression_requested &&
        (!dx12_live_report_ok || dx12_live_object_lines != 0 ||
            dx12_live_object_detail_lines != 0) && exit_code == 0)
    {
        exit_code = 75;
    }

#if defined(_DEBUG)
    // DX12 Device の解放後にプロセス全体を追跡する DXGI から最終確認する。
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

	if (capture_frame_requested)
    {
        const char* summary = capture_frame_summary.empty()
            ? u8"撮影結果を取得できませんでした"
            : capture_frame_summary.c_str();
        std::fprintf(stderr, "frame capture: RESULT %s (%s)\n",
            capture_frame_ok ? "OK" : "NG", summary);
        if (!capture_frame_ok && exit_code == 0) exit_code = 1470;
    }
	if (capture_exclusive_frame_requested)
    {
        if (!capture_exclusive_frame_attempted)
        {
            std::fprintf(stderr,
                "exclusive frame capture: RESULT SKIPPED (exclusive Scene was not rendered)\n");
        }
        else
        {
            const char* summary = capture_frame_summary.empty()
                ? u8"撮影結果を取得できませんでした"
                : capture_frame_summary.c_str();
            std::fprintf(stderr, "exclusive frame capture: RESULT %s (%s)\n",
                capture_frame_ok ? "OK" : "NG", summary);
            if (!capture_frame_ok && exit_code == 0) exit_code = 1470;
        }
    }
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
            report << "DX12_LIVE_REPORT_OK " << (dx12_live_report_ok ? 1 : 0) << '\n';
            report << "D3D12_LIVE_OBJECT_LINES " << dx12_live_object_lines << '\n';
            report << "D3D12_LIVE_OBJECT_DETAIL_LINES "
                << dx12_live_object_detail_lines << '\n';
        }
    }
	if (SUCCEEDED(com_result)) CoUninitialize();
	return exit_code;
}
