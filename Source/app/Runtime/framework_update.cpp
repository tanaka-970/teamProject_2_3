#include "framework.h"

void framework::update(float elapsed_time)
{
    ReplayEngine::Rendering::Stats().BeginFrame();
    REPLAY_PROFILE_SCOPE("Update");
    // 基準画像を撮る間はワールドを止める。
    //
    // 止めないとアニメ・粒子・物理が毎フレーム進み、
    // 同じシーンを撮っても毎回違う絵になる。
    // 差分が出続ける検査は、無い検査より悪い。
    // 「あると思って見ていない」状態になるから。
    if (golden_capture_pending()) elapsed_time = 0.0f;

    // OS / XInput の状態はフレーム先頭で 1 回だけ採取する。
    // ImGui が文字入力を取っている間はキーボードを Gameplay へ公開しない。
    bool keyboard_captured = false;
    bool mouse_captured = false;
#ifdef USE_IMGUI
    if (ImGui::GetCurrentContext() &&
        (editor_mode || (standalone_game_mode && show_render_stats)))
    {
        // Play中にGame/Scene Viewをフォーカスしている場合は、
        // ImGuiのWindowフォーカスだけを理由にGameplayキーボードを遮断しない。
        // InputTextなど他のEditor UIにフォーカスがある場合は従来どおり捕捉する。
        const bool game_view_owns_keyboard =
            editor_mode && object_scene_play_mode && scene_view_focused;
        keyboard_captured = ImGui::GetIO().WantCaptureKeyboard &&
            !game_view_owns_keyboard;
        // Standalone Profiler を開いている間は Profiler がマウスを所有する。
        // Editor だけは従来どおり Scene View 上の操作を通す。
        mouse_captured = ImGui::GetIO().WantCaptureMouse &&
            (!editor_mode || !scene_view_hovered);
    }
#endif
    {
        REPLAY_PROFILE_SCOPE("Input");
        game_input.BeginFrame(keyboard_captured, mouse_captured);
    }

    {
        REPLAY_PROFILE_SCOPE("AssetPump");
        async_asset_manager.PumpMainThread();
    }
    if (scene_manager.IsExclusive())
    {
        {
            REPLAY_PROFILE_SCOPE("SceneManagerExclusive");
            scene_manager.Update(elapsed_time);
        }
        return;
    }

    if (!editor_mode || object_scene_play_mode)
    {
        REPLAY_PROFILE_SCOPE("SceneManager");
        scene_manager.Update(elapsed_time);
    }

    // GameObject / Component 基盤の更新。
    // 既存の scene_manager (画面遷移) とは別系統で、二重更新にはならない。
    // 内部で Edit Mode 中は Component を止める判定を行っている。
    {
        REPLAY_PROFILE_SCOPE("SceneUpdate");
        update_object_scene(elapsed_time);
    }

    // Win32入力とEditor UIはDX12 ImGui Rendererへ統一する。
    if (dx12_framework_active)
    {
#ifdef USE_IMGUI
        if (!editor_mode)
        {
            if (show_render_stats && ImGui::GetCurrentContext() &&
                dx12_device_context.ImGuiReady())
            {
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
                imgui_frame_active = true;
                REPLAY_PROFILE_SCOPE("ProfilerBuildUI");
                draw_render_stats_overlay();
            }
            return;
        }
        if (ImGui::GetCurrentContext())
        {
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            imgui_frame_active = true;
            {
                REPLAY_PROFILE_SCOPE("EditorCamera");
                update_editor_camera(elapsed_time);
            }
            {
                REPLAY_PROFILE_SCOPE("EditorBuildUI");
                draw_editor();
                draw_render_stats_overlay();
            }
        }
#endif
        return;
    }
}
