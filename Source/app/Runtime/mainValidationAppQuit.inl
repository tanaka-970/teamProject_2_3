// 終了要求の宛先が起動モードで変わることを確かめる。
//
// 同じ「終了」でも意味が 3 つある。
//   単体ゲーム        … プロセスを閉じる。編集セッションが無いので確認は出さない。
//   エディター内 Play … Play を止めて編集シーンへ戻す。アプリは閉じない。
//   エディター本体    … 未保存の編集があれば確認してから閉じる。
//
// D3D デバイスは作らない。framework の構築とルーティング関数だけを通す。
namespace ReplayEngine::Editor
{
    struct AppQuitValidation
    {
        // WM_CLOSE がキューへ積まれたかを取り出す。取り出したものは消す。
        static bool TakePostedClose(HWND window)
        {
            MSG message{};
            return PeekMessageW(&message, window, WM_CLOSE, WM_CLOSE, PM_REMOVE) != FALSE;
        }

        static int Run()
        {
            int failures = 0;
            int first_failure = 0;
            int next_code = 2400;
            int total = 0;
            const auto expect = [&](bool condition, const char* what)
            {
                const int code = next_code++;
                ++total;
                if (condition) return;
                ++failures;
                if (first_failure == 0) first_failure = code;
                std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
            };

            const HINSTANCE instance = GetModuleHandleW(nullptr);
            WNDCLASSW window_class{};
            window_class.lpfnWndProc = DefWindowProcW;
            window_class.hInstance = instance;
            window_class.lpszClassName = L"ReplayAppQuitValidation";
            RegisterClassW(&window_class);
            HWND window = CreateWindowW(window_class.lpszClassName,
                L"App quit validation", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240,
                nullptr, nullptr, instance, nullptr);
            if (window == nullptr) return 2399;

            {
                auto application = std::make_unique<framework>(window);
                auto& app = *application;
                app.object_editor_context.AttachScene(&app.object_scene);

                // ---- 単体ゲーム -------------------------------------------
                //
                // 単体ゲームも起動時に既定の編集 Scene を作るため、
                // EditorContext は常に Dirty で始まる。
                // ここで未保存確認へ入ると、ゲームの上に編集用ダイアログが開き、
                // editor_mode まで立って閉じられなくなる。
                app.standalone_game_mode = true;
                app.editor_mode = false;
                app.object_exit_confirmed = false;
                app.object_scene_unsaved_prompt_requested = false;
                app.object_editor_context.MarkDirty();
                TakePostedClose(window);

                app.handle_game_quit_request("game");
                expect(TakePostedClose(window),
                    "単体ゲームの終了要求はそのままウィンドウを閉じる");
                expect(!app.editor_mode,
                    "単体ゲームの終了要求で editor_mode が立たない");
                expect(!app.object_scene_unsaved_prompt_requested,
                    "単体ゲームの終了要求で未保存確認を出さない");

                app.object_exit_confirmed = false;
                app.object_editor_context.MarkDirty();
                expect(app.confirm_object_scene_close(),
                    "単体ゲームはウィンドウの × をそのまま受け入れる");
                expect(!app.object_scene_unsaved_prompt_requested,
                    "単体ゲームの × で未保存確認を出さない");

                // ---- エディター内 Play ------------------------------------
                //
                // ゲームの終了ボタンでエディターごと閉じると、
                // Play 前の編集内容まで巻き込む。止めるのは Play だけ。
                app.standalone_game_mode = false;
                app.editor_mode = true;
                app.object_exit_confirmed = false;
                app.object_scene_unsaved_prompt_requested = false;
                app.object_scene_play_mode = true;
                app.object_editor_context.SetPlayMode(true);
                app.object_editor_context.MarkDirty();
                TakePostedClose(window);

                app.handle_game_quit_request("game");
                expect(!app.object_scene_play_mode,
                    "エディター内ゲームの終了要求は Play を止める");
                expect(!TakePostedClose(window),
                    "エディター内ゲームの終了要求でエディターを閉じない");
                expect(!app.object_scene_unsaved_prompt_requested,
                    "Play を止めるだけなので未保存確認は出さない");
                expect(!app.object_exit_confirmed,
                    "Play の停止を終了確定として扱わない");
                expect(app.object_editor_context.Dirty(),
                    "Play を止めても編集中の未保存状態は残す");

                // ---- 未保存編集を持つエディター本体 -----------------------
                app.object_scene_play_mode = false;
                app.object_editor_context.SetPlayMode(false);
                app.object_editor_context.AttachScene(&app.object_scene);
                app.object_exit_confirmed = false;
                app.object_scene_unsaved_prompt_requested = false;
                app.object_editor_context.MarkDirty();
                TakePostedClose(window);

                app.request_application_quit();
                expect(app.object_scene_unsaved_prompt_requested,
                    "未保存の編集があるエディターは終了前に確認を出す");
                expect(!TakePostedClose(window),
                    "確認へ答えるまでエディターは閉じない");
                expect(!app.confirm_object_scene_close(),
                    "未保存のままウィンドウの × を押しても閉じない");

                // ---- 未保存の無いエディター本体 ---------------------------
                app.object_scene_unsaved_prompt_requested = false;
                app.object_exit_confirmed = false;
                app.object_editor_context.ClearDirty();
                TakePostedClose(window);

                app.request_application_quit();
                expect(TakePostedClose(window),
                    "未保存が無いエディターは確認なしで閉じる");
                expect(!app.object_scene_unsaved_prompt_requested,
                    "未保存が無ければ確認を出さない");
            }

            DestroyWindow(window);
            UnregisterClassW(window_class.lpszClassName, instance);

            if (first_failure == 0)
            {
                std::fprintf(stderr,
                    "app-quit routing OK: %d checks passed\n", total);
                return 0;
            }
            std::fprintf(stderr,
                "app-quit routing FAILED: %d/%d checks failed (first=%d)\n",
                failures, total, first_failure);
            return first_failure;
        }
    };
}
