// アプリ設定、shutdown 診断、run() メインループ。
// framework_class.h の class framework 内部からのみ include する。

    void configure_standalone_game(std::filesystem::path content_root,
        std::string game_name);
    void set_startup_scene_path(std::filesystem::path scene_path);
    void set_startup_window_size(UINT width, UINT height) noexcept;
    void request_startup_fullscreen() noexcept { startup_fullscreen_requested = true; }
    void request_dx12_framework() noexcept { dx12_framework_requested = true; }
    bool dx12_framework_enabled() const noexcept { return dx12_framework_active; }
    bool standalone_game() const noexcept { return standalone_game_mode; }
    const std::filesystem::path& content_root_path() const noexcept
    {
        return content_root_path_;
    }
    const std::filesystem::path& saved_root_path() const noexcept
    {
        return saved_root_path_;
    }
    std::filesystem::path content_path(const std::filesystem::path& relative) const;
    std::filesystem::path saved_path(const std::filesystem::path& relative = {}) const;
    std::filesystem::path collision_cache_path(const std::string& identity) const;
    Microsoft::WRL::ComPtr<ID3D11Debug> acquire_d3d11_debug() const noexcept;
    Microsoft::WRL::ComPtr<ID3D11InfoQueue> acquire_d3d11_info_queue() const noexcept;

    // 終了理由と主要な進行状況を Saved/engine_log.txt へ追記する。
    // 「なぜ落ちたか」が分からないと原因の切り分けができないため、
    // 例外や異常終了だけでなく正常終了の経路も残す。
    static void set_shutdown_log_folder(std::filesystem::path folder)
    {
        shutdown_log_folder() = std::move(folder);
    }

    static std::filesystem::path& shutdown_log_folder()
    {
        static std::filesystem::path folder{ "Saved" };
        return folder;
    }

    static void log_shutdown_reason(const char* reason)
    {
        std::error_code error;
        const std::filesystem::path folder = shutdown_log_folder();
        std::filesystem::create_directories(folder, error);
        std::ofstream log(folder / "engine_log.txt", std::ios::app);
        if (!log) return;
        const auto now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        tm local{};
        localtime_s(&local, &now);
        char stamp[32]{};
        strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
        log << "[" << stamp << "] " << reason << '\n';
    }

    int run(int show_command = SW_SHOWDEFAULT)
    {
        MSG msg{};
        log_shutdown_reason("=== 起動 ===");
        if (!initialize())
        {
            log_shutdown_reason("initialize() が false を返したため終了");
            return 0;
        }
        log_shutdown_reason("initialize() 完了");

#ifdef USE_IMGUI
        {
            // Editorの入力はDX11/DX12で同じWin32 Platform Backendを使う。
            // Renderer Backendだけを選択的に切り替えることで、UI編集の状態を
            // Scene3D backendの違いで失わない。
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigWindowsMoveFromTitleBarOnly = true;
            if (!io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\meiryo.ttc", 15.0f, nullptr, glyphRangesJapanese))
            {
                io.Fonts->AddFontDefault();
                }
                ImGui_ImplWin32_Init(hwnd);
            if (dx12_framework_active)
            {
                if (!dx12_device_context.InitializeImGui())
                {
                    push_editor_log("Error", "DX12 ImGui Renderer の初期化に失敗しました");
                }
            }
            else
            {
                ImGui_ImplDX11_Init(device.Get(), immediate_context.Get());
            }
            ImGui::StyleColorsDark();
            configure_editor_style();
        }
#endif

        // Device / Shader / Script / Editor の初期化が終わるまで Window は隠す。
        // 先に表示すると、初回描画まで Windows の背景色だけが長時間見えてしまう。
        if (show_command != SW_HIDE)
        {
            ShowWindow(hwnd, show_command);
            UpdateWindow(hwnd);
            if (startup_fullscreen_requested) toggle_fullscreen();
        }

        while (WM_QUIT != msg.message)
        {
            // メッセージを上限付きで処理してから1フレーム進める。
            // Queueが空になるまで待つと、WM_PAINT/Inputが描画を飢えさせる。
            for (int message_count = 0; message_count < 64 &&
                PeekMessage(&msg, NULL, 0, 0, PM_REMOVE); ++message_count)
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                if (msg.message == WM_QUIT) break;
            }
            if (msg.message == WM_QUIT) break;

            tictoc.tick();
            calculate_frame_stats();

            float frame_delta_time = tictoc.time_interval();
            if (profile_benchmark_mode)
            {
                frame_delta_time = 1.0f / 60.0f;

                // Startup Scene が Runtime World へ入れ替わる前は、warmup / capture の
                // フレーム数へ絶対に数えない。BootLogo / LoadingScene は排他 Scene なので、
                // この間 update() は SceneFlow まで到達しない。旧実装はここから 300 frame を
                // 消費できたため、空 World のまま benchmark が完走していた。
                if (!profile_benchmark_scene_ready)
                {
                    ReplayEngine::Rendering::Stats().SetPaused(true);
                }
                else
                {
                    const std::uint64_t capture_begin =
                        static_cast<std::uint64_t>(profile_benchmark_warmup_frames);
                    const std::uint64_t capture_end = capture_begin +
                        static_cast<std::uint64_t>(profile_benchmark_frames);
                    const std::uint64_t frame_index =
                        static_cast<std::uint64_t>(profile_benchmark_frame_index);
                    ReplayEngine::Rendering::Stats().SetPaused(
                        frame_index < capture_begin || frame_index >= capture_end);
                }
            }

            // 描画中の未捕捉例外で静かに落ちると原因が追えないため、
            // ここで捕まえて理由を残す。
            try
            {
                update(frame_delta_time);
                render(frame_delta_time);

                if (profile_benchmark_mode && !profile_benchmark_export_attempted)
                {
                    if (!profile_benchmark_scene_ready)
                    {
                        const bool startup_failed = object_runtime_blocked ||
                            object_runtime_scenes.State() ==
                                ReplayEngine::Runtime::SceneLoadState::Failed ||
                            (object_scene_flow != nullptr &&
                                object_scene_flow->StartupState() ==
                                    ReplayEngine::Runtime::StartupSceneState::Failed);

                        const bool scene_manager_ready =
                            !scene_manager.IsExclusive() && game_scene != nullptr;
                        const bool runtime_world_ready = object_runtime_world_active &&
                            object_runtime_scenes.State() ==
                                ReplayEngine::Runtime::SceneLoadState::Completed &&
                            !object_runtime_scenes.IsBusy();

                        bool loaded_scene_has_objects = false;
                        if (runtime_world_ready)
                        {
                            loaded_scene_has_objects =
                                active_object_scene().GameObjectCount() > 0;
                        }

                        if (!startup_failed && scene_manager_ready &&
                            runtime_world_ready && loaded_scene_has_objects)
                        {
                            // この判定は render() の後。つまりこのフレームで既に
                            // Runtime World + Runtime Camera の通常描画経路を 1 回通っている。
                            // 次フレームから warmup を数え始める。
                            profile_benchmark_scene_ready = true;
                            profile_benchmark_frame_index = 0;
                            profile_benchmark_drain_frames = 0;
                            ReplayEngine::Rendering::Stats().SetPaused(true);
                            log_shutdown_reason(
                                "Profiler benchmark: Startup Scene ready; warmup begins next frame");

                            // プロファイルと同時に撮影する場合は、Runtime World が確定した
                            // 直後に要求する。通常の Smoke Test の終了間際へ任せると、
                            // プロファイル終了後の空シーンを撮影してしまう。
                            if (automated_frame_capture_pending)
                            {
                                automated_frame_capture_pending = false;
                                request_golden(golden_request_kind::capture);
                            }
                        }
                        else
                        {
                            const double startup_seconds =
                                std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() -
                                    profile_benchmark_startup_begin).count();
                            constexpr double startup_timeout_seconds = 120.0;

                            if (!startup_failed && runtime_world_ready &&
                                !loaded_scene_has_objects)
                            {
                                profile_benchmark_startup_failed = true;
                                log_shutdown_reason(
                                    "Profiler benchmark: Startup Scene loaded but contains 0 GameObjects");
                            }
                            else if (startup_failed)
                            {
                                profile_benchmark_startup_failed = true;
                                log_shutdown_reason(
                                    "Profiler benchmark: Startup Scene load failed");
                            }
                            else if (startup_seconds >= startup_timeout_seconds)
                            {
                                profile_benchmark_startup_failed = true;
                                log_shutdown_reason(
                                    "Profiler benchmark: Startup Scene load timed out");
                            }

                            if (profile_benchmark_startup_failed)
                            {
                                profile_benchmark_export_attempted = true;
                                profile_benchmark_export_ok = false;
                                object_exit_confirmed = true;
                                PostMessage(hwnd, WM_CLOSE, 0, 0);
                            }
                        }
                    }
                    else
                    {
                        const std::uint64_t capture_begin =
                            static_cast<std::uint64_t>(profile_benchmark_warmup_frames);
                        const std::uint64_t capture_end = capture_begin +
                            static_cast<std::uint64_t>(profile_benchmark_frames);
                        const std::uint64_t frame_index =
                            static_cast<std::uint64_t>(profile_benchmark_frame_index);

                        // EndFrame 済みなので、このフレームの実測をここで検証できる。
                        // warmup は集計せず、本計測区間だけ最大値を保持する。
                        if (frame_index >= capture_begin && frame_index < capture_end)
                        {
                            const auto& sample =
                                ReplayEngine::Rendering::Stats().LatestSample();
                            profile_benchmark_max_draw_calls = (std::max)(
                                profile_benchmark_max_draw_calls, sample.cpu.draw_calls);
                            profile_benchmark_max_objects = (std::max)(
                                profile_benchmark_max_objects, sample.scene.object_count);
                            profile_benchmark_max_components = (std::max)(
                                profile_benchmark_max_components, sample.scene.component_count);
                        }

                        ++profile_benchmark_frame_index;
                        if (static_cast<std::uint64_t>(profile_benchmark_frame_index) >=
                            capture_end)
                        {
                            // 撮影対象を Runtime World に固定するため、撮影完了まで
                            // プロファイルの終了通知を送らない。
                            if (automated_frame_capture_pending || golden_capture_pending())
                            {
                                ++profile_benchmark_drain_frames;
                                continue;
                            }

                            // paused frame でも BeginFrame/EndFrame は DONOTFLUSH の
                            // Query 回収だけを進める。GPU を待って CPU をブロックしない。
                            const std::size_t pending =
                                ReplayEngine::Rendering::Stats().PendingGpuFrames();
                            constexpr std::uint32_t max_gpu_drain_frames = 120u;
                            if (pending == 0 ||
                                profile_benchmark_drain_frames >= max_gpu_drain_frames)
                            {
                                profile_benchmark_gpu_drain_timeout = pending != 0;
                                profile_benchmark_export_attempted = true;
                                profile_benchmark_export_ok =
                                    ReplayEngine::Rendering::Stats().ExportCsvAndTrace(
                                        profile_benchmark_output_name);

                                // 起動 Scene を測ったつもりで空 World の CSV を成功扱いにしない。
                                // Export 自体は残すので、失敗時も外部から中身を診断できる。
                                const bool benchmark_scene_rendered =
                                    profile_benchmark_max_objects > 0 &&
                                    profile_benchmark_max_components > 0 &&
                                    profile_benchmark_max_draw_calls > 0;
                                if (!benchmark_scene_rendered)
                                {
                                    log_shutdown_reason(
                                        "Profiler benchmark: measured Scene produced 0 objects/components/draw calls");
                                    profile_benchmark_export_ok = false;
                                }
                                if (profile_benchmark_gpu_drain_timeout)
                                {
                                    log_shutdown_reason(
                                        "Profiler benchmark: GPU query drain timeout");
                                    profile_benchmark_export_ok = false;
                                }
                                else if (!profile_benchmark_export_ok)
                                {
                                    log_shutdown_reason(
                                        "Profiler benchmark: CSV/Trace export or Scene validation failed");
                                }
                                else
                                {
                                    log_shutdown_reason(
                                        "Profiler benchmark: CSV/Trace export completed");
                                }
                                // 未保存確認は応答する人がいないため、ここを通ると
                                // ダイアログが出たまま永久に終了できない。
                                // 自動計測は確定済みとして扱い、確認を飛ばす。
                                object_exit_confirmed = true;
                                PostMessage(hwnd, WM_CLOSE, 0, 0);
                            }
                            else
                            {
                                ++profile_benchmark_drain_frames;
                            }
                        }
                    }
                }

                if (automated_smoke_test_frames > 0)
                {
                    const std::uint32_t rendered_frames =
                        ++automated_smoke_test_frames_rendered;
                    if (automated_frame_capture_pending)
                    {
                        // TAA の収束と撮影後の Present・終了処理に必要な時間を残すため、
                        // 終了の 32 フレーム前で 1 回だけ要求する。短い実行では撮り逃しを
                        // 避けることを優先し、総フレーム数の半分で要求する。
                        const std::uint32_t capture_request_frame =
                            automated_smoke_test_frames > 32u
                            ? automated_smoke_test_frames - 32u
                            : automated_smoke_test_frames / 2u;
                        if (rendered_frames >= capture_request_frame)
                        {
                            automated_frame_capture_pending = false;
                            request_golden(golden_request_kind::capture);
                        }
                    }
                    if (rendered_frames >= automated_smoke_test_frames)
                    {
                        automated_smoke_test_frames = 0;
                        PostMessage(hwnd, WM_CLOSE, 0, 0);
                    }
                }
            }
            catch (const std::exception& exception)
            {
                log_shutdown_reason((std::string("例外: ") + exception.what()).c_str());
                throw;
            }
            catch (...)
            {
                log_shutdown_reason("不明な例外");
                throw;
            }
        }
        log_shutdown_reason("メッセージループを抜けた (WM_QUIT)");

        // 終了処理へ入る直前に検査シナリオを流す。
        // ここから先は通常の終了経路をそのまま通るので、
        // 「検査のためだけの特別な解放」は一切挟まらない。
        if (shutdown_regression_requested) run_shutdown_regression_scenario();

#ifdef USE_IMGUI
        // Material Inspector の変更は終了時にも保存する。
        // Save ボタンを押し忘れただけで Shader / Texture / Property が消える
        // Editor にはしない。失敗時だけ終了ログへ残す。
        if (material_editor_loaded && material_editor_dirty &&
            !save_material_editor())
        {
            log_shutdown_reason("Material AutoSave に失敗");
        }
        {
            std::string composer_save_error;
            if (!shader_composer_editor.AutoSaveGraph(composer_save_error))
            {
                const std::string message = "Shader Composer AutoSave に失敗: " + composer_save_error;
                log_shutdown_reason(message.c_str());
            }
        }
        // Landscape Tool は編集中の LandscapeData を非所有で参照するため、
        // ImGui/Scene の破棄より前に Stroke と Collider interactive-edit を必ず閉じる。
        reset_landscape_editor_state(true);
        if (!standalone_game_mode) save_editor_session();
        if (ImGui::GetCurrentContext())
        {
            if (!dx12_framework_active) ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
#endif

        if (is_fullscreen()) toggle_fullscreen();

        const bool uninitialize_ok = uninitialize();
        if (!uninitialize_ok) return 0;
        if (profile_benchmark_mode && !profile_benchmark_export_ok) return 74;
        return static_cast<int>(msg.wParam);
    }

    LRESULT CALLBACK handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
