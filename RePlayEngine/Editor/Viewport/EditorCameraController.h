#pragma once

#include "EditorViewportCamera.h"

namespace ReplayEngine::Editor
{
    // EditorViewportCamera を操作する入力側。
    //
    // 【入力 API に依存しない理由】
    //   ImGui や windows.h をここへ持ち込むと、g++ の検証ハーネスで動かせなくなる。
    //   framework 側が ImGui / Win32 から状態を読んで EditorCameraInput を埋め、
    //   このクラスは「その状態ならどう動くか」だけを決める。
    //   優先順位の判定も操作モードの遷移も、描画なしで検証できる。
    //
    // 【Runtime への影響が無い理由】
    //   触るのは渡された EditorViewportCamera 1 つだけ。
    //   Scene も GameObject も controlledObjectId も参照しない。
    struct EditorCameraInput
    {
        // ---- Scene View の状態 ----------------------------------------------
        bool viewport_hovered = false;
        bool viewport_focused = false;

        // ---- Editor UI が入力を取っているか ----------------------------------
        // どれか 1 つでも true なら、新しいカメラ操作を開始しない。
        bool ui_wants_mouse = false;      // ImGui::GetIO().WantCaptureMouse
        bool ui_wants_keyboard = false;   // ImGui::GetIO().WantCaptureKeyboard
        bool ui_text_input_active = false;// ImGui::GetIO().WantTextInput
        bool ui_popup_open = false;
        bool gizmo_dragging = false;      // Transform / Collider ギズモのドラッグ中

        // ---- マウス -----------------------------------------------------------
        bool right_mouse_down = false;
        bool middle_mouse_down = false;
        bool left_mouse_down = false;

        // 画面座標（ピクセル）。相対量はコントローラー側で差分を取る。
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;

        float wheel = 0.0f;

        // ---- 修飾キー ---------------------------------------------------------
        bool alt_down = false;
        bool shift_down = false;
        bool control_down = false;
        bool escape_pressed = false;

        // ---- 移動キー ---------------------------------------------------------
        bool key_forward = false;   // W
        bool key_back = false;      // S
        bool key_left = false;      // A
        bool key_right = false;     // D
        bool key_up = false;        // E
        bool key_down = false;      // Q
        bool key_focus = false;     // F（押した瞬間だけ true）

        // ---- ウィンドウ -------------------------------------------------------
        // false の間は一切動かさない。Alt+Tab 中に移動キーが効かないようにする。
        bool window_focused = true;

        float delta_time = 0.0f;
    };

    class EditorCameraController final
    {
    public:
        // 今どの操作をしているか。
        enum class Mode
        {
            None,
            Fly,     // 右クリック押下中
            Pan,     // 中ボタンドラッグ
            Orbit,   // Alt + 左ドラッグ
        };

        // 入力を 1 フレーム分適用する。
        //
        // 戻り値は「カメラ操作でマウス／キーを消費したか」。
        // true の間は Gizmo や選択処理を動かさないこと。
        bool Update(EditorViewportCamera& camera, const EditorCameraInput& input);

        Mode CurrentMode() const noexcept { return mode_; }

        // マウスを掴んでいるか。framework 側でカーソルを隠す判断に使う。
        bool MouseCaptured() const noexcept { return mode_ != Mode::None; }

        // 操作を強制的に終える。ウィンドウが非アクティブになったときなどに呼ぶ。
        void Cancel() noexcept;

        // F キーでフォーカスが要求されたか。
        // framework 側が選択対象の Bounds を求めてから camera へ渡す。
        bool ConsumeFocusRequest() noexcept;

        // カメラ用 delta_time の上限。
        // ブレークポイント後などの巨大な delta で遠くへ飛ばないようにする。
        static constexpr float maximum_delta_time = 1.0f / 15.0f;

    private:
        // 新しい操作を始めてよいか。
        static bool CanBeginInteraction(const EditorCameraInput& input) noexcept;

        Mode mode_ = Mode::None;

        // 相対移動量を出すための前フレーム位置。
        float last_mouse_x_ = 0.0f;
        float last_mouse_y_ = 0.0f;

        // 掴んだ直後の 1 フレームは差分を捨てる。
        // これを入れないと、掴む前のカーソル位置との差が一気に適用されて視点が飛ぶ。
        bool ignore_next_delta_ = false;

        bool focus_requested_ = false;
        bool focus_key_was_down_ = false;
    };
}
