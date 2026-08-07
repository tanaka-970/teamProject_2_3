// Scene View の編集カメラと framework の接続部。
//
// 【この 1 ファイルにまとめている理由】
//   ImGui / Win32 から入力を読むのはここだけ。
//   RePlayEngine 側の EditorViewportCamera / EditorCameraController は
//   ImGui も windows.h も知らないため、g++ の検証ハーネスで数値だけ検証できる。
//   その境界がどこかを一目で分かるようにしてある。
//
// 【Runtime Camera と分離している根拠】
//   このファイルは game_scene->Gameplay().GetCamera() へ書き込みを行わない。
//   読むのは「Play 中に描画へ使う行列」を返すときだけで、
//   編集カメラの値を Runtime Camera へ写す処理も、その逆も存在しない。
//   controlledObjectId / CameraTargetComponent / PlayerControlSystem へは
//   このファイルから一切触れない。

#include "framework.h"

#include "../../RePlayEngine/Editor/Viewport/EditorSelectionBounds.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"

#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// 行列の取得窓口（唯一）
// ---------------------------------------------------------------------------

bool framework::using_editor_camera() const noexcept
{
    // Viewport が 1 つしかないので、モードで切り替える。
    //   Edit Mode（F3 で停止中）          -> 編集カメラ
    //   Play Mode / 通常実行               -> Runtime Camera
    //
    // 将来 Scene View と Game View を分ける場合は、この関数の戻り値を
    // 「どちらの View を描いているか」で決めるだけでよい。
    // 呼び出し側はすべてこの関数を通っているため、他は変更不要になる。
    if (!editor_mode) return false;
    return active_editor_view == editor_view::scene;
}

DirectX::XMMATRIX framework::viewport_view_matrix() const
{                                                                                                                                                  //
    if (using_editor_camera()) return editor_camera.ViewMatrix();                                                                                  //
                                                                                                                                                   //
    if (enable_scene_game && game_scene)                                                                                                           //
    {                                                                                                                                              //
        return DirectX::XMLoadFloat4x4(&game_scene->Gameplay().GetCamera().GetView());                                                             //
    }                                                                                                                                              //
                                                                                                                                                   //
    // GameScene がまだ作られていない起動直後。編集カメラで代用する。                                                                              //
    return editor_camera.ViewMatrix();                                                                                                             //
}                                                                                                                                                  //
                                                                                                                                                   
DirectX::XMMATRIX framework::viewport_projection_matrix() const                                                                                    //
{                                                                                                                                                  //
    const float aspect = (client_height > 0)                                                                                                       //
        ? static_cast<float>(client_width) / static_cast<float>(client_height)                                                                     //
        : (16.0f / 9.0f);                                                                                                                          //
                                                                                                                                                   //
    if (using_editor_camera()) return editor_camera.ProjectionMatrix(aspect);                                                                      //
                                                                                                                                                   //
    if (enable_scene_game && game_scene)                                                                                                           //
    {                                                                                                                                              //
        return DirectX::XMLoadFloat4x4(&game_scene->Gameplay().GetCamera().GetProjection());                                                       //
    }                                                                                                                                              //
                                                                                                                                                   //
    return editor_camera.ProjectionMatrix(aspect);                                                                                                 //
}                                                                                                                                                  //
                                                                                                                                                   //
DirectX::XMFLOAT3 framework::viewport_eye_position() const
{
    if (using_editor_camera()) return editor_camera.Position();

    if (enable_scene_game && game_scene)
    {
        return game_scene->Gameplay().GetCamera().GetEye();
    }
    return editor_camera.Position();
}

ReplayEngine::Editor::EditorViewportCamera::Ray framework::viewport_picking_ray(
    float mouse_x, float mouse_y) const
{
    // Picking は必ず編集カメラの行列から作る。
    // Runtime Camera の行列で拾うと、Edit Mode で見えている位置と一致しない。
    return editor_camera.BuildPickingRay(mouse_x, mouse_y,
        static_cast<float>(client_width), static_cast<float>(client_height));
}

// ---------------------------------------------------------------------------
// 入力
// ---------------------------------------------------------------------------

void framework::update_editor_camera(float elapsed_time)
{
    editor_camera_consumed_input = false;

#ifdef USE_IMGUI
    // 編集カメラを使っていないフレームでは入力を受けない。
    // Play 中に WASD が編集カメラへ入って、操作キャラクターと二重に動かない。
    if (!using_editor_camera())
    {
        editor_camera_controller.Cancel();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();

    ReplayEngine::Editor::EditorCameraInput input;

    // ---- Scene View の Hover / Focus 判定 ---------------------------------
    //
    // このプロジェクトの Scene View は「ImGui のウィンドウが乗っていない
    // 中央の領域」そのもの。DockSpace は PassthruCentralNode なので、
    // ImGui がマウスを取っていない = Scene View の上にいる、と判定できる。
    POINT client_origin{ 0, 0 };
    ClientToScreen(hwnd, &client_origin);
    const ImVec2 mouse = ImGui::GetMousePos();
    const float local_x = mouse.x - static_cast<float>(client_origin.x);
    const float local_y = mouse.y - static_cast<float>(client_origin.y);
    const bool inside_client =
        local_x >= 0.0f && local_y >= 0.0f &&
        local_x < static_cast<float>(client_width) &&
        local_y < static_cast<float>(client_height);

    input.viewport_hovered = inside_client && scene_view_hovered;
    input.viewport_focused = scene_view_focused && ::GetForegroundWindow() == hwnd;

    // ---- Editor UI が入力を取っているか -----------------------------------
    input.ui_wants_mouse = io.WantCaptureMouse && !scene_view_hovered;
    input.ui_wants_keyboard = io.WantCaptureKeyboard;
    input.ui_text_input_active = io.WantTextInput || search_input_active;
    input.ui_popup_open = ImGui::IsPopupOpen(static_cast<const char*>(nullptr),
        ImGuiPopupFlags_AnyPopup);

    // Transform Gizmo / Collider Gizmo のドラッグ判定。
    //
    // このプロジェクトのギズモは 3D ハンドルではなく ImGui の DragFloat3 で
    // 実装されている（framework_scene_document.cpp の
    // draw_transform_gizmo_controls）。そのためドラッグ中は必ず
    // IsAnyItemActive() が true になる。専用のフラグを新設するより、
    // 実際に操作されている状態を直接見るほうが取りこぼしが無い。
    // 将来 3D ハンドルを入れる場合も、その操作は ImGui のアイテムとして
    // 扱われるか viewport_drag_selecting と同じ形の状態になる。
    input.gizmo_dragging = ImGui::IsAnyItemActive() || viewport_drag_selecting;

    // ---- マウス ------------------------------------------------------------
    input.right_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    input.middle_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    input.left_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    input.mouse_x = mouse.x;
    input.mouse_y = mouse.y;
    input.wheel = io.MouseWheel;

    // ---- キー --------------------------------------------------------------
    //
    // GetAsyncKeyState を使うのは、この ImGui のバージョンに
    // ImGuiKey ベースの安定した問い合わせが無いため。
    // ウィンドウが前面にないフレームでは window_focused が false になり、
    // 下の値は使われないので、Alt+Tab 後に押しっぱなしにはならない。
    const auto key_down = [](int virtual_key)
    {
        return (::GetAsyncKeyState(virtual_key) & 0x8000) != 0;
    };

    input.alt_down = key_down(VK_MENU);
    input.shift_down = key_down(VK_SHIFT);
    input.control_down = key_down(VK_CONTROL);
    input.escape_pressed = key_down(VK_ESCAPE);

    input.key_forward = key_down('W');
    input.key_back = key_down('S');
    input.key_left = key_down('A');
    input.key_right = key_down('D');
    input.key_up = key_down('E');
    input.key_down = key_down('Q');
    input.key_focus = key_down('F');

    input.window_focused = ::GetForegroundWindow() == hwnd;
    input.delta_time = elapsed_time;

    editor_camera_consumed_input = editor_camera_controller.Update(editor_camera, input);

    // ---- マウスロック -------------------------------------------------------
    //
    // 掴んでいる間はカーソルを隠す。ImGui 側にも「今は描かない」と伝える。
    // 解除時は ImGui が通常のカーソル描画へ戻すので、位置が飛ぶことはない
    // （カーソルは動かしておらず、相対移動量だけを読んでいるため）。
    if (editor_camera_controller.MouseCaptured())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    // F キーのフォーカス要求。Bounds の計算は Scene が要るのでここで行う。
    if (editor_camera_controller.ConsumeFocusRequest())
    {
        focus_editor_camera_on_selection();
    }
#else
    (void)elapsed_time;
#endif
}

void framework::focus_editor_camera_on_selection()
{
    namespace EditorNS = ReplayEngine::Editor;

    const ReplayEngine::Scene::Scene& scene = active_object_scene();
    const std::vector<ReplayEngine::Core::ObjectID> selection =
        object_editor_context.Selection().All();

    // 選択が無くてもクラッシュさせない。何もしないで戻る。
    if (selection.empty()) return;

    const EditorNS::WorldBounds bounds =
        EditorNS::EditorSelectionBounds::Compute(scene, selection);

    // Bounds が取れない場合も落ちない。Compute は最低でも Transform 位置を含めるが、
    // 対象がすべて消えていた場合は valid = false のまま返る。
    if (!bounds.valid) return;

    // Scene のデータは何も変更していないので、Undo 履歴へは積まない。
    editor_camera.FocusOnBounds(bounds.minimum, bounds.maximum);
}

// ---------------------------------------------------------------------------
// 設定 UI
// ---------------------------------------------------------------------------

void framework::draw_editor_camera_settings()
{
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("Scene Camera")) return;

    ImGui::Indent();

    ImGui::DragFloat("移動速度", &editor_camera.move_speed, 0.1f, 0.05f, 500.0f, "%.2f");
    ImGui::DragFloat("高速倍率", &editor_camera.fast_multiplier, 0.1f, 1.0f, 50.0f, "%.2f");
    ImGui::DragFloat("低速倍率", &editor_camera.slow_multiplier, 0.01f, 0.01f, 1.0f, "%.2f");
    ImGui::DragFloat("マウス感度", &editor_camera.mouse_sensitivity, 0.01f, 0.01f, 2.0f, "%.2f");
    ImGui::DragFloat("平行移動感度", &editor_camera.pan_sensitivity, 0.01f, 0.01f, 10.0f, "%.2f");
    ImGui::DragFloat("ズーム感度", &editor_camera.zoom_sensitivity, 0.01f, 0.01f, 10.0f, "%.2f");
    ImGui::DragFloat("視野角", &editor_camera.field_of_view_degrees, 0.5f, 10.0f, 120.0f, "%.1f");
    ImGui::DragFloat("Near Clip", &editor_camera.near_clip, 0.01f, 0.001f, 10.0f, "%.3f");
    ImGui::DragFloat("Far Clip", &editor_camera.far_clip, 10.0f, 10.0f, 100000.0f, "%.0f");

    ImGui::Spacing();
    if (ImGui::Button("既定値へ戻す")) editor_camera.ResetSettingsToDefault();
    ImGui::SameLine();
    if (ImGui::Button("選択対象へフォーカス")) focus_editor_camera_on_selection();

    // 現在の姿勢は読み取り専用。生の View 行列は出さない。
    ImGui::Spacing();
    const auto& position = editor_camera.Position();
    ImGui::TextDisabled("位置   %.2f  %.2f  %.2f", position.x, position.y, position.z);
    ImGui::TextDisabled("回転   yaw %.1f°  pitch %.1f°",
        DirectX::XMConvertToDegrees(editor_camera.Yaw()),
        DirectX::XMConvertToDegrees(editor_camera.Pitch()));
    ImGui::TextDisabled("Orbit 距離  %.2f", editor_camera.OrbitDistance());

    ImGui::TextDisabled("右ドラッグ: 移動 / 中ドラッグ: 平行移動");
    ImGui::TextDisabled("Alt+左ドラッグ: 回り込み / ホイール: ズーム / F: フォーカス");

    ImGui::Unindent();
#endif
}

// ---------------------------------------------------------------------------
// 状態の保存・復元
// ---------------------------------------------------------------------------
//
// Scene v9 のゲームデータへは一切書き込まない。別ファイルへ置く。

std::string framework::make_editor_camera_state_key() const
{
    namespace EditorNS = ReplayEngine::Editor;

    // Scene が AssetDatabase へ登録されていればその GUID を使う。
    // ファイル名を変えても GUID は変わらないので、視点が失われない。
    if (const auto* record = asset_database.FindByPath(object_scene_path))
    {
        if (!record->guid.empty()) return record->guid;
    }

    // 登録が無い Scene はパス由来の安定キーを使う。
    return EditorNS::EditorCameraStateStore::KeyFromScenePath(object_scene_path);
}

void framework::load_editor_camera_state()
{
    namespace EditorNS = ReplayEngine::Editor;

    editor_camera_state_key = make_editor_camera_state_key();

    EditorNS::EditorCameraStateStore::State state;
    std::string error;
    const auto path = EditorNS::EditorCameraStateStore::PathForKey(editor_camera_state_key);

    if (EditorNS::EditorCameraStateStore::Load(state, path, error))
    {
        EditorNS::EditorCameraStateStore::Apply(state, editor_camera);
        return;
    }

    // 保存が無い / 壊れている。既定位置から始める。
    // ここで失敗しても Scene の読み込みには一切影響しない。
    editor_camera.ResetToDefault();
}

void framework::save_editor_camera_state()
{
    namespace EditorNS = ReplayEngine::Editor;

    if (editor_camera_state_key.empty())
    {
        editor_camera_state_key = make_editor_camera_state_key();
    }

    const auto state = EditorNS::EditorCameraStateStore::Capture(editor_camera);
    const auto path = EditorNS::EditorCameraStateStore::PathForKey(editor_camera_state_key);

    std::string error;
    // 保存に失敗しても何も壊れない。次回は既定位置から始まるだけ。
    EditorNS::EditorCameraStateStore::Save(state, path, error);
}
