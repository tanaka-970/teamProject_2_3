#include "framework.h"

void framework::update(float elapsed_time)
{
    // 基準画像を撮る間はワールドを止める。
    //
    // 止めないとアニメ・粒子・物理が毎フレーム進み、
    // 同じシーンを撮っても毎回違う絵になる。
    // 差分が出続ける検査は、無い検査より悪い。
    // 「あると思って見ていない」状態になるから。
    if (golden_capture_pending()) elapsed_time = 0.0f;

    async_asset_manager.PumpMainThread();
    if (scene_manager.IsExclusive())
    {
        scene_manager.Update(elapsed_time);
        return;
    }

    if (!editor_mode || object_scene_play_mode)
    {
        scene_manager.Update(elapsed_time);
    }

    // GameObject / Component 基盤の更新。
    // 既存の scene_manager (画面遷移) とは別系統で、二重更新にはならない。
    // 内部で Edit Mode 中は Component を止める判定を行っている。
    update_object_scene(elapsed_time);

#ifdef USE_IMGUI
    if (!editor_mode) return;
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    imgui_frame_active = true;

    // Scene View の編集カメラ。
    //
    // draw_editor() より先に呼ぶ理由:
    //   カメラがこのフレームでマウス／キーを消費したかを
    //   editor_camera_consumed_input で先に確定させる必要がある。
    //   Gizmo と矩形選択（draw_editor から呼ばれる）はその結果を見て、
    //   カメラ操作中は動かないようにしている。
    //
    // Runtime Camera へは一切書き込まない。読むのは描画行列を返すときだけ。
    update_editor_camera(elapsed_time);

    draw_editor();
    draw_render_stats_overlay();
#endif
}
