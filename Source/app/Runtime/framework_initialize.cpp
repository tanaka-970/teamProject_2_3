#include "framework.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Scene/BootLogoScene.h"
#include "../../RePlayEngine/Scene/LoadingScene.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <shellapi.h>
#include <vector>
#include <string>

namespace
{
    struct DX12RuntimeOptions final
    {
        bool debug_layer = false;
        bool gpu_validation = false;
        bool warp = false;
        bool break_on_error = false;
        bool dred = false;
        bool force_device_removed = false;
        std::filesystem::path debug_log{};
        std::filesystem::path stats_csv{};
        std::uint32_t frame_dump_count = 0;
    };

    bool ParseOnOffOption(const std::wstring& argument, const wchar_t* prefix,
        bool& value)
    {
        if (prefix == nullptr) return false;
        const std::wstring key(prefix);
        if (argument.rfind(key, 0) != 0) return false;
        const std::wstring option = argument.substr(key.size());
        if (option == L"on") value = true;
        else if (option == L"off") value = false;
        else return false;
        return true;
    }

    DX12RuntimeOptions ReadDX12RuntimeOptions()
    {
        DX12RuntimeOptions options{};
#if defined(_DEBUG) || defined(DEBUG)
        options.debug_layer = true;
        options.dred = true;
#endif
        int argument_count = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
        if (arguments == nullptr) return options;
        for (int index = 1; index < argument_count; ++index)
        {
            const std::wstring argument = arguments[index] != nullptr ? arguments[index] : L"";
            bool parsed = false;
            parsed = ParseOnOffOption(argument, L"--dx12-debug-layer=", options.debug_layer) || parsed;
            parsed = ParseOnOffOption(argument, L"--dx12-gpu-validation=", options.gpu_validation) || parsed;
            parsed = ParseOnOffOption(argument, L"--dx12-break-on-error=", options.break_on_error) || parsed;
            parsed = ParseOnOffOption(argument, L"--dx12-dred=", options.dred) || parsed;
            if (argument == L"--dx12-warp")
            {
                options.warp = true;
                parsed = true;
            }
            if (argument == L"--dx12-force-device-removed")
            {
                options.force_device_removed = true;
                parsed = true;
            }
            constexpr wchar_t kLogPrefix[] = L"--dx12-log=";
            constexpr wchar_t kStatsPrefix[] = L"--dx12-stats-csv=";
            constexpr wchar_t kDumpPrefix[] = L"--dx12-frame-dump=";
            if (argument.rfind(kLogPrefix, 0) == 0)
            {
                options.debug_log = argument.substr(std::size(kLogPrefix) - 1u);
                parsed = true;
            }
            if (argument.rfind(kStatsPrefix, 0) == 0)
            {
                options.stats_csv = argument.substr(std::size(kStatsPrefix) - 1u);
                parsed = true;
            }
            if (argument.rfind(kDumpPrefix, 0) == 0)
            {
                const std::wstring count_text = argument.substr(std::size(kDumpPrefix) - 1u);
                wchar_t* end = nullptr;
                const unsigned long value = std::wcstoul(count_text.c_str(), &end, 10);
                if (end != count_text.c_str() && end != nullptr && *end == L'\0')
                    options.frame_dump_count = static_cast<std::uint32_t>((std::min)(value, 100000ul));
                parsed = true;
            }
            if (!parsed && argument.rfind(L"--dx12-", 0) == 0)
                std::fwprintf(stderr, L"[DX12] unknown runtime option: %ls\n", argument.c_str());
        }
        LocalFree(arguments);
        if (options.gpu_validation) options.debug_layer = true;
        return options;
    }

}

// 【削除済み】lower_copy / find_animation_clip
//   起動時に固定のプレイヤーモデルからクリップ名を探し、
//   旧 Player の clip_idle / clip_walk / clip_jump へ割り当てるための補助だった。
//   クリップの割り当ては AnimatorComponent のプロパティ
//   (idle_clip / walk_clip / jump_clip) が持ち、Scene へ保存される。

bool framework::initialize()
{
    const auto initialize_begin = std::chrono::steady_clock::now();
    auto initialize_stage_begin = initialize_begin;
    const auto record_initialize_stage = [this, &initialize_stage_begin](std::size_t index)
    {
        const auto now = std::chrono::steady_clock::now();
        if (profile_benchmark_mode && index < profile_benchmark_initialize_stage_ms.size())
        {
            profile_benchmark_initialize_stage_ms[index] =
                std::chrono::duration<double, std::milli>(now - initialize_stage_begin).count();
        }
        initialize_stage_begin = now;
    };

    // 製品起動ではDX12を唯一のRendererとする。呼び出し元の指定漏れや
    // 旧移行フラグの値に関係なく、初期化中にD3D11経路へ入らないよう固定する。
    dx12_framework_requested = true;
    dx12_framework_active = false;

    std::string asset_database_error;
    if (!asset_database.Load(asset_database_error))
        object_editor_context.SetStatus("AssetDatabase: " + asset_database_error);
    record_initialize_stage(0);

    if (!standalone_game_mode && !profile_benchmark_mode &&
        automated_smoke_test_frames == 0 && !shutdown_regression_requested &&
        !automated_frame_capture_pending && !automated_exclusive_frame_capture_pending)
    {
        std::string scan_error;
        const std::size_t registered_assets = register_resource_assets(scan_error);
        if (registered_assets > 0)
        {
            push_editor_log("Info", "resources からアセットを自動登録しました: " +
                std::to_string(registered_assets) + "件");
        }
        if (!scan_error.empty())
        {
            object_editor_context.SetStatus("resources 自動登録: " + scan_error);
            push_editor_log("Warning", scan_error);
        }
    }
    record_initialize_stage(1);

    // Input Action Asset は ProjectSettings 読み込み後に initialize_object_scene() で適用する。
    // ここではまだ project_settings が未読込なので参照しない。

    if (!object_audio_system.Initialize())
    {
        push_editor_log("Warning", "Audio は silent mode で起動します");
    }


    {
        const bool ui_font_ok = ui_font_atlas.InitializeCpuOnly();
        if (!ui_font_ok)
            push_editor_log("Warning", "UI FontAtlas を初期化できません。UIText は描画されません");
        lights.data.light_counts = { 0, 0, 0, 0 };
    }

    auto loading_scene = std::make_unique<ReplayEngine::Scene::LoadingScene>();
    // 任意アセットの読み込みは「無ければスキップして続行」に統一する。
    // 実行に必須ではないファイルの不足で起動が止まらないようにするため。
    // 失敗は OutputDebugString へ理由付きで出す（Visual Studio の出力ウィンドウで読める）。
    // キャラクターモデルは SkinnedMeshRendererComponent の
    // mesh_asset (AssetGUID) が指し、resolve_object_mesh() が読み込む。
    // AssetDatabaseのモデルは1件ずつ独立したタスクにして、ロード画面の
    // ワーカー群へそのまま流す。セッション復元やステージ切り替えは
    // ConcurrentResourceCacheのヒットで待たされなくなる。
    for (const auto& record : asset_database.Records())
    {
        if (record.kind != ReplayEngine::Assets::AssetKind::Model) continue;
        const std::filesystem::path source = content_path(record.source_path);
        loading_scene->AddTask("Prewarm " + record.display_name, [this, source]
        {
            prewarm_model_asset(source);
            // 先読みは最適化なので、失敗してもロード全体は成功扱いにする。
            return true;
        });
    }
    // Game 起動ではロゴの裏でロードを進める。Editor 起動では固定長の
    // ロゴ待ちを省き、暗いロード画面から直接セッションを復元する。
    if (object_boot_from_startup_scene)
    {
        scene_manager.SetScene(std::make_unique<ReplayEngine::Scene::BootLogoScene>());
        scene_manager.QueueScene(std::move(loading_scene));
    }
    else
    {
        scene_manager.SetScene(std::move(loading_scene));
    }

    scene_manager.QueueSceneFactory([this]() -> std::unique_ptr<ReplayEngine::Scene::IScene>
    {
        // GameScene が持つのはカメラ操作だけ。
        // 操作キャラクターのモデルもアニメーションクリップも渡さない。
        // それらは Scene 内の GameObject が Component として持っている。
        auto next_scene = std::make_unique<GameScene>(
            static_cast<float>(client_width) / static_cast<float>(client_height));
        game_scene = next_scene.get();
        if (!standalone_game_mode && !object_boot_from_startup_scene)
            restore_editor_session();
        return next_scene;
    });

    // GameObject / Component 基盤の初期化。
    // Component 型の登録・編集用 Scene の準備・既定 Scene ファイルの読み込みを行う。
    // AssetDatabase の読み込み後に呼ぶ必要がある（Asset 参照を解決するため）。
    record_initialize_stage(2);
    initialize_object_scene();
    record_initialize_stage(3);

    const DX12RuntimeOptions dx12_options = ReadDX12RuntimeOptions();
    if (!dx12_options.debug_log.empty())
        dx12_device_context.SetDebugLogPath(dx12_options.debug_log);
    if (!dx12_options.stats_csv.empty())
        dx12_device_context.SetStatsCsvPath(dx12_options.stats_csv);
    dx12_device_context.SetFrameDumpCount(dx12_options.frame_dump_count);
    if (!dx12_device_context.Initialize(hwnd, client_width, client_height,
        dx12_options.debug_layer, dx12_options.warp, false,
        dx12_options.gpu_validation, dx12_options.dred))
    {
        char error_message[256]{};
        std::snprintf(error_message, sizeof(error_message),
            "DX12 framework bootstrap の初期化に失敗しました: %s (hr=0x%08lx)",
            dx12_device_context.LastInitializationStage(),
            static_cast<unsigned long>(dx12_device_context.LastInitializationResult()));
        std::fprintf(stderr, "%s\n", error_message);
        push_editor_log("Error", error_message);
        return false;
    }
    record_initialize_stage(4);
    dx12_device_context.SetBreakOnError(dx12_options.break_on_error);
    if (dx12_options.force_device_removed)
    {
        const bool report_ok = dx12_device_context.ForceDeviceRemovedDiagnostic();
        std::fprintf(stderr, "[DX12] forced device-removed diagnostic: %s\n",
            report_ok ? "report written" : "report failed");
        push_editor_log(report_ok ? "Info" : "Error",
            report_ok ? "DX12 Device Removed 強制診断ログを書き出しました"
                : "DX12 Device Removed 強制診断ログの書き出しに失敗しました");
    }
    dx12_framework_active = true;
    dx12_framework_render_error_reported = false;
    if (!prewarm_loading_scene_gpu_resources())
        push_editor_log("Warning", "Loading Screen Scene のGPU先読みに失敗しました。通常の遅延Uploadで続行します");
    std::fprintf(stderr,
        "[DX12] runtime options: debug=%d gpu_validation=%d warp=%d break_on_error=%d dred=%d force_device_removed=%d frame_dump=%u\n",
        dx12_options.debug_layer ? 1 : 0, dx12_options.gpu_validation ? 1 : 0,
        dx12_options.warp ? 1 : 0, dx12_options.break_on_error ? 1 : 0,
        dx12_options.dred ? 1 : 0, dx12_options.force_device_removed ? 1 : 0,
        dx12_options.frame_dump_count);
    push_editor_log("Info", "DX12 framework bootstrap: DX12 が SwapChain / Present を所有します");

    if (profile_benchmark_mode)
    {
        profile_benchmark_initialize_total_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - initialize_begin).count();
    }
    return true;
}
