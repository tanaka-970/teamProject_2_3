#include "EditorCameraController.h"

#include <algorithm>

namespace ReplayEngine::Editor
{
    void EditorCameraController::Cancel() noexcept
    {
        mode_ = Mode::None;
        ignore_next_delta_ = false;
        // 押しっぱなし扱いが残らないようにする。
        // Alt+Tab でウィンドウを離れたあと、戻ってきた瞬間に
        // 移動キーが効き続ける事故を防ぐ。
        focus_key_was_down_ = false;
    }

    bool EditorCameraController::ConsumeFocusRequest() noexcept
    {
        const bool requested = focus_requested_;
        focus_requested_ = false;
        return requested;
    }

    bool EditorCameraController::CanBeginInteraction(const EditorCameraInput& input) noexcept
    {
        // 【新しい操作を始めてよい条件】
        //   Scene View の上にカーソルがあり、Editor UI が入力を取っていないこと。
        //
        // ここを通らない例:
        //   Inspector の数値入力中 / テキスト入力中 / メニューやポップアップ操作中 /
        //   Asset Browser の操作中（いずれも ui_wants_mouse か ui_wants_keyboard が立つ） /
        //   Transform・Collider ギズモのドラッグ中。
        if (!input.window_focused) return false;
        if (!input.viewport_hovered && !input.viewport_focused) return false;
        if (input.ui_wants_mouse) return false;
        if (input.ui_wants_keyboard) return false;
        if (input.ui_text_input_active) return false;
        if (input.ui_popup_open) return false;
        if (input.gizmo_dragging) return false;
        return true;
    }

    bool EditorCameraController::Update(EditorViewportCamera& camera,
        const EditorCameraInput& input)
    {
        // ウィンドウが非アクティブなら何もしない。
        // キーの押しっぱなしが残らないよう、掴んでいた操作も解除する。
        if (!input.window_focused)
        {
            Cancel();
            last_mouse_x_ = input.mouse_x;
            last_mouse_y_ = input.mouse_y;
            return false;
        }

        // Esc はいつでも操作を解除できる。
        if (input.escape_pressed && mode_ != Mode::None)
        {
            Cancel();
            last_mouse_x_ = input.mouse_x;
            last_mouse_y_ = input.mouse_y;
            return true;
        }

        const bool can_begin = CanBeginInteraction(input);

        // ---- モード遷移 -------------------------------------------------------
        //
        // 一度掴んだら、カーソルが Viewport の外へ出ても操作を続ける。
        // ドラッグ中に手が滑って外へ出た瞬間に視点が固まるのを防ぐため。
        // 逆に、掴んでいないときは Viewport 上でしか始まらない。
        if (mode_ == Mode::None)
        {
            if (can_begin)
            {
                // 優先順位は Orbit > Pan > Fly。
                // Alt+左ドラッグは Fly の右クリックと衝突しないので取り違えが起きない。
                if (input.alt_down && input.left_mouse_down) mode_ = Mode::Orbit;
                else if (input.middle_mouse_down)            mode_ = Mode::Pan;
                else if (input.right_mouse_down)             mode_ = Mode::Fly;

                if (mode_ != Mode::None)
                {
                    // 掴んだ最初のフレームは差分を捨てる。
                    // これが無いと、掴む前のカーソル位置との差が一気に入って視点が飛ぶ。
                    ignore_next_delta_ = true;

                    if (mode_ == Mode::Orbit)
                    {
                        // Pivot が実質未設定なら、カメラ前方の既定距離を使う。
                        // 選択対象の中心は framework 側が SetOrbitPivot で入れる。
                        if (camera.OrbitDistance() <= EditorViewportCamera::minimum_orbit_distance)
                        {
                            camera.SetOrbitPivotToViewCenter();
                        }
                    }
                }
            }
        }
        else
        {
            // 掴んでいたボタンが離れたら終了。
            const bool still_held =
                (mode_ == Mode::Fly && input.right_mouse_down) ||
                (mode_ == Mode::Pan && input.middle_mouse_down) ||
                (mode_ == Mode::Orbit && input.left_mouse_down);
            if (!still_held) Cancel();
        }

        // ---- マウス相対量 -----------------------------------------------------
        float delta_x = input.mouse_x - last_mouse_x_;
        float delta_y = input.mouse_y - last_mouse_y_;
        last_mouse_x_ = input.mouse_x;
        last_mouse_y_ = input.mouse_y;

        if (ignore_next_delta_)
        {
            delta_x = 0.0f;
            delta_y = 0.0f;
            ignore_next_delta_ = false;
        }

        bool consumed = false;

        // ---- F キーのフォーカス要求 --------------------------------------------
        //
        // 押した瞬間だけ拾う。押しっぱなしで毎フレーム走らないようにする。
        // 実際の移動は framework 側が選択対象の Bounds を求めてから行う。
        if (input.key_focus && !focus_key_was_down_ && can_begin && !input.ui_wants_keyboard)
        {
            focus_requested_ = true;
            consumed = true;
        }
        focus_key_was_down_ = input.key_focus;

        // ---- ホイール ---------------------------------------------------------
        if (input.wheel != 0.0f)
        {
            if (mode_ == Mode::Fly)
            {
                // 右クリック中のホイールは移動速度の変更。
                // Zoom と役割が衝突しないよう、明確に分けてある。
                const float factor = (input.wheel > 0.0f) ? 1.15f : (1.0f / 1.15f);
                // 大規模ワールドでも使えるよう 500 などの上限は置かない。
                // 0 以下になることだけ防ぐ。
                camera.move_speed = std::max(camera.move_speed * factor, 0.001f);
                consumed = true;
            }
            else if (can_begin)
            {
                camera.Zoom(input.wheel);
                consumed = true;
            }
        }

        // ---- RMB を押していないときの keyboard fly --------------------------
        //
        // Unreal の Viewport と同じキー配置を、さらに一段手軽に使えるようにする。
        // Scene View にフォーカスがある間は WASD/QE だけでも移動でき、
        // RMB を押したときだけ mouse-look が追加される。
        if (mode_ == Mode::None && can_begin && !input.alt_down)
        {
            EditorViewportCamera::MoveAxes axes;
            if (input.key_forward) axes.forward += 1.0f;
            if (input.key_back)    axes.forward -= 1.0f;
            if (input.key_right)   axes.right += 1.0f;
            if (input.key_left)    axes.right -= 1.0f;
            if (input.key_up)      axes.up += 1.0f;
            if (input.key_down)    axes.up -= 1.0f;

            if (axes.forward != 0.0f || axes.right != 0.0f || axes.up != 0.0f)
            {
                float multiplier = 1.0f;
                if (input.shift_down)   multiplier *= camera.fast_multiplier;
                if (input.control_down) multiplier *= camera.slow_multiplier;
                const float delta_time = std::min(input.delta_time, maximum_delta_time);
                camera.Fly(axes, multiplier, delta_time);
                consumed = true;
            }
        }

        // ---- 操作の適用 --------------------------------------------------------
        switch (mode_)
        {
        case Mode::Fly:
        {
            if (delta_x != 0.0f || delta_y != 0.0f) camera.Look(delta_x, delta_y);

            EditorViewportCamera::MoveAxes axes;
            if (input.key_forward) axes.forward += 1.0f;
            if (input.key_back)    axes.forward -= 1.0f;
            if (input.key_right)   axes.right += 1.0f;
            if (input.key_left)    axes.right -= 1.0f;
            if (input.key_up)      axes.up += 1.0f;
            if (input.key_down)    axes.up -= 1.0f;

            float multiplier = 1.0f;
            if (input.shift_down)   multiplier *= camera.fast_multiplier;
            if (input.control_down) multiplier *= camera.slow_multiplier;

            // delta_time に上限を掛ける。
            // ブレークポイントで止めた直後の巨大な delta で遠くへ飛ばない。
            const float delta_time = std::min(input.delta_time, maximum_delta_time);
            camera.Fly(axes, multiplier, delta_time);
            consumed = true;
            break;
        }
        case Mode::Pan:
            if (delta_x != 0.0f || delta_y != 0.0f) camera.Pan(delta_x, delta_y);
            consumed = true;
            break;

        case Mode::Orbit:
            if (delta_x != 0.0f || delta_y != 0.0f) camera.Orbit(delta_x, delta_y);
            consumed = true;
            break;

        case Mode::None:
        default:
            break;
        }

        return consumed;
    }
}
