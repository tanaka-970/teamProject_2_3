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

#include "gltf_model.h"

#include "../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Editor/Viewport/EditorSelectionBounds.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Rendering/Adapter/RenderItem.h"

#include <algorithm>
#include <cmath>
#include <string>

// ---------------------------------------------------------------------------
// 行列の取得窓口（唯一）
// ---------------------------------------------------------------------------

bool framework::using_editor_camera() const noexcept
{
    // Viewport が 1 つしかないので、モードで切り替える。
    //   Scene View                        -> 編集カメラ
    //   Play Mode / 通常実行               -> Runtime Camera
    //
    // 将来 Scene View と Game View を分ける場合は、この関数の戻り値を
    // 「どちらの View を描いているか」で決めるだけでよい。
    // 呼び出し側はすべてこの関数を通っているため、他は変更不要になる。
    // プロファイル実行は Editor UI の状態に関係なく Runtime Camera を使う。
    // 起動中の Editor セッション復元などで editor_mode が立っても、
    // ベンチマークの描画視点が編集カメラへ戻らないようにする。
    if (!editor_mode || profile_benchmark_mode) return false;
    return active_editor_view == editor_view::scene;
}

DirectX::XMMATRIX framework::viewport_view_matrix() const
{                                                                                                                                                  //
    if (render_matrix_override_active)
        return DirectX::XMLoadFloat4x4(&render_view_override);
    if (render_camera_override != nullptr) return render_camera_override->ViewMatrix();
    if (using_editor_camera()) return editor_camera.ViewMatrix();                                                                                  //
                                                                                                                                                   //
    const ReplayEngine::Components::CameraSelection camera_selection =
        ReplayEngine::Components::ResolveActiveCameraSelection(active_object_scene());
    if (camera_selection.Valid()) return camera_selection.component->ViewMatrix();

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
    // 3D scene itself is rendered to the full D3D client viewport. The Scene View is a
    // transparent ImGui overlay that clips that image; it is NOT a separate render target.
    // Therefore projection, picking and editor overlays must all use the client aspect.
    // Using the Scene View content rect here makes the error grow toward the viewport edges
    // (Landscape brush/edit point, gizmo and selection no longer line up with the image).
    const float aspect = render_camera_aspect > 0.0f
        ? render_camera_aspect
        : ((client_width > 0 && client_height > 0)
            ? static_cast<float>(client_width) / static_cast<float>(client_height)
            : (16.0f / 9.0f));                                                                                                                     //
                                                                                                                                                   //
    if (render_matrix_override_active)
        return DirectX::XMLoadFloat4x4(&render_projection_override);
    if (render_camera_override != nullptr)
        return render_camera_override->ProjectionMatrix(aspect);
    if (using_editor_camera()) return editor_camera.ProjectionMatrix(aspect);                                                                      //
                                                                                                                                                   //
    const ReplayEngine::Components::CameraSelection camera_selection =
        ReplayEngine::Components::ResolveActiveCameraSelection(active_object_scene());
    if (camera_selection.Valid()) return camera_selection.component->ProjectionMatrix(aspect);

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
    if (render_matrix_override_active) return render_eye_override;
    if (render_camera_override != nullptr) return render_camera_override->EyePosition();
    if (using_editor_camera()) return editor_camera.Position();

    const ReplayEngine::Components::CameraSelection camera_selection =
        ReplayEngine::Components::ResolveActiveCameraSelection(active_object_scene());
    if (camera_selection.Valid()) return camera_selection.component->EyePosition();

    if (enable_scene_game && game_scene)
    {
        return game_scene->Gameplay().GetCamera().GetEye();
    }
    return editor_camera.Position();
}

ReplayEngine::Editor::EditorViewportCamera::Ray framework::viewport_picking_ray(
    float mouse_x, float mouse_y) const
{
    // Callers pass Scene-View-local mouse coordinates because that is the convenient input
    // space for editor tools. The actual 3D image, however, is rendered in CLIENT coordinates.
    // Convert through screen space so the ray is built in exactly the same viewport as D3D.
    POINT client_origin{ 0, 0 };
    ClientToScreen(hwnd, &client_origin);

    const float screen_x = scene_view_min_x + mouse_x;
    const float screen_y = scene_view_min_y + mouse_y;
    const float client_x = screen_x - static_cast<float>(client_origin.x);
    const float client_y = screen_y - static_cast<float>(client_origin.y);
    const float width = (std::max)(1.0f, static_cast<float>(client_width));
    const float height = (std::max)(1.0f, static_cast<float>(client_height));

    ReplayEngine::Editor::EditorViewportCamera::Ray ray;
    const DirectX::XMMATRIX view = viewport_view_matrix();
    const DirectX::XMMATRIX projection = viewport_projection_matrix();
    const DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();

    const DirectX::XMVECTOR near_point = DirectX::XMVector3Unproject(
        DirectX::XMVectorSet(client_x, client_y, 0.0f, 1.0f),
        0.0f, 0.0f, width, height, 0.0f, 1.0f, projection, view, world);
    const DirectX::XMVECTOR far_point = DirectX::XMVector3Unproject(
        DirectX::XMVectorSet(client_x, client_y, 1.0f, 1.0f),
        0.0f, 0.0f, width, height, 0.0f, 1.0f, projection, view, world);
    DirectX::XMVECTOR direction = DirectX::XMVectorSubtract(far_point, near_point);
    const float length_sq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(direction));
    DirectX::XMFLOAT3 direction_value{};
    DirectX::XMStoreFloat3(&direction_value, direction);
    if (!std::isfinite(length_sq) || length_sq <= 1.0e-8f ||
        !std::isfinite(direction_value.x) ||
        !std::isfinite(direction_value.y) ||
        !std::isfinite(direction_value.z))
    {
        return editor_camera.BuildPickingRay(client_x, client_y, width, height);
    }

    direction = DirectX::XMVector3Normalize(direction);
    DirectX::XMStoreFloat3(&ray.origin, near_point);
    DirectX::XMStoreFloat3(&ray.direction, direction);
    return ray;
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

    ensure_editor_camera_presets_loaded();
    auto& camera_preset = active_editor_camera_preset();

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
    // Scene View がフォーカスを持っているときは ImGui の keyboard capture だけで
    // WASD/QE を潰さない。TextInput / popup は下で引き続き確実にブロックする。
    input.ui_wants_keyboard = io.WantCaptureKeyboard && !scene_view_focused;
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
    // Preset 側は Win32 を知らない。ここで physical key を enum へ変換する。
    const auto key_down = [](int virtual_key)
    {
        // 上位bitは現在押されている状態、下位bitは前回の問い合わせ以降に
        // 押された履歴。両方を見ることで、1 frame より短い F などのタップも
        // EditorCameraController の edge 判定へ確実に渡す。
        return (::GetAsyncKeyState(virtual_key) & 0x8001) != 0;
    };
    const auto set_key = [&](ReplayEngine::Editor::EditorCameraKey key, int virtual_key)
    {
        input.keys[static_cast<std::size_t>(key)] = key_down(virtual_key);
    };

    input.alt_down = key_down(VK_MENU);
    input.shift_down = key_down(VK_SHIFT);
    input.control_down = key_down(VK_CONTROL);
    input.escape_pressed = key_down(VK_ESCAPE);

    using CameraKey = ReplayEngine::Editor::EditorCameraKey;
    set_key(CameraKey::W, 'W'); set_key(CameraKey::A, 'A');
    set_key(CameraKey::S, 'S'); set_key(CameraKey::D, 'D');
    set_key(CameraKey::Q, 'Q'); set_key(CameraKey::E, 'E');
    set_key(CameraKey::R, 'R'); set_key(CameraKey::F, 'F');
    set_key(CameraKey::G, 'G'); set_key(CameraKey::C, 'C');
    set_key(CameraKey::V, 'V'); set_key(CameraKey::X, 'X');
    set_key(CameraKey::Z, 'Z'); set_key(CameraKey::Space, VK_SPACE);
    set_key(CameraKey::Left, VK_LEFT); set_key(CameraKey::Right, VK_RIGHT);
    set_key(CameraKey::Up, VK_UP); set_key(CameraKey::Down, VK_DOWN);
    set_key(CameraKey::Home, VK_HOME); set_key(CameraKey::End, VK_END);
    set_key(CameraKey::PageUp, VK_PRIOR); set_key(CameraKey::PageDown, VK_NEXT);
    set_key(CameraKey::Num0, VK_NUMPAD0); set_key(CameraKey::Num1, VK_NUMPAD1);
    set_key(CameraKey::Num2, VK_NUMPAD2); set_key(CameraKey::Num3, VK_NUMPAD3);
    set_key(CameraKey::Num4, VK_NUMPAD4); set_key(CameraKey::Num5, VK_NUMPAD5);
    set_key(CameraKey::Num6, VK_NUMPAD6); set_key(CameraKey::Num7, VK_NUMPAD7);
    set_key(CameraKey::Num8, VK_NUMPAD8); set_key(CameraKey::Num9, VK_NUMPAD9);

    input.window_focused = ::GetForegroundWindow() == hwnd;
    input.delta_time = elapsed_time;

    // Gizmo shortcut も preset の一部。Mayaなら W/E/R、Hybridなら Shift+W/E/R など。
    const bool selected_game_object =
        selected_editor_object == editor_selection::game_object &&
        object_editor_context.Selection().Primary().Valid();
    // RMB+WASD で fly している最中に Unity の W/E/R tool shortcut が発火しないよう、
    // mouse navigation 中は Gizmo shortcut を開始しない。
    const bool no_mouse_navigation = !input.left_mouse_down &&
        !input.middle_mouse_down && !input.right_mouse_down;
    const bool move_shortcut_down = selected_game_object && no_mouse_navigation &&
        ReplayEngine::Editor::EditorCameraController::KeyChordHeld(
            camera_preset.gizmo_move, input, false);
    const bool rotate_shortcut_down = selected_game_object && no_mouse_navigation &&
        ReplayEngine::Editor::EditorCameraController::KeyChordHeld(
            camera_preset.gizmo_rotate, input, false);
    const bool scale_shortcut_down = selected_game_object && no_mouse_navigation &&
        ReplayEngine::Editor::EditorCameraController::KeyChordHeld(
            camera_preset.gizmo_scale, input, false);

    if (move_shortcut_down && !gizmo_move_shortcut_was_down)
        transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Translate);
    if (rotate_shortcut_down && !gizmo_rotate_shortcut_was_down)
        transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Rotate);
    if (scale_shortcut_down && !gizmo_scale_shortcut_was_down)
        transform_gizmo.SetOperation(ReplayEngine::Editor::GizmoOperation::Scale);
    gizmo_move_shortcut_was_down = move_shortcut_down;
    gizmo_rotate_shortcut_was_down = rotate_shortcut_down;
    gizmo_scale_shortcut_was_down = scale_shortcut_down;

    // Tool shortcut に使った key は camera movement へ二重投入しない。
    const auto suppress_chord_key = [&](const ReplayEngine::Editor::EditorCameraKeyChord& chord, bool held)
    {
        if (!held || chord.key == CameraKey::None) return;
        input.keys[static_cast<std::size_t>(chord.key)] = false;
    };
    suppress_chord_key(camera_preset.gizmo_move, move_shortcut_down);
    suppress_chord_key(camera_preset.gizmo_rotate, rotate_shortcut_down);
    suppress_chord_key(camera_preset.gizmo_scale, scale_shortcut_down);

    // 診断表示が読む。条件を書き写すと本物とズレるので実物を保持する。
    last_editor_camera_input = input;

    const float move_speed_before_input = editor_camera.move_speed;
    editor_camera_consumed_input = editor_camera_controller.Update(
        editor_camera, input, camera_preset);

    // RMB+Wheel など profile 自身が値を変更した場合、その user preset へ保存する。
    if (editor_camera.move_speed != move_speed_before_input)
    {
        const float changed_speed = editor_camera.move_speed;
        if (!camera_preset.Editable()) make_active_editor_camera_preset_personal_copy();
        editor_camera.move_speed = changed_speed;
        save_active_editor_camera_preset();
    }

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

    // Collider を持たない描画Objectでも、Transform原点の仮箱ではなく
    // 実際に描くGLBのBoundsを使う。RenderItemのworldを使うことで、姿勢補正・
    // ローカル倍率・親Transformも描画結果と完全に同じになる。
    const auto render_bounds_provider = [this](
        const ReplayEngine::Core::GameObject& object,
        EditorNS::WorldBounds& output) -> bool
    {
        using ReplayEngine::Components::MeshRendererComponent;
        using ReplayEngine::Components::SkinnedMeshRendererComponent;
        using ReplayEngine::Rendering::RenderItem;

        const auto accumulate = [this, &output](const RenderItem& item) -> bool
        {
            gltf_model* model = resolve_object_gltf(item.mesh_asset);
            if (model == nullptr || !model->IsLoaded()) return false;

            DirectX::XMFLOAT3 local_minimum{};
            DirectX::XMFLOAT3 local_maximum{};
            if (!model->ComputeBounds(local_minimum, local_maximum)) return false;

            const DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&item.world);
            for (int x = 0; x < 2; ++x)
            {
                for (int y = 0; y < 2; ++y)
                {
                    for (int z = 0; z < 2; ++z)
                    {
                        const DirectX::XMFLOAT3 corner{
                            x == 0 ? local_minimum.x : local_maximum.x,
                            y == 0 ? local_minimum.y : local_maximum.y,
                            z == 0 ? local_minimum.z : local_maximum.z };
                        DirectX::XMFLOAT3 transformed{};
                        DirectX::XMStoreFloat3(&transformed,
                            DirectX::XMVector3TransformCoord(
                                DirectX::XMLoadFloat3(&corner), world));
                        output.Encapsulate(transformed);
                    }
                }
            }
            return true;
        };

        bool found = false;
        if (const auto* renderer = object.GetComponent<SkinnedMeshRendererComponent>())
        {
            RenderItem item{};
            if (renderer->BuildRenderItem(object, item)) found |= accumulate(item);
        }
        if (const auto* renderer = object.GetComponent<MeshRendererComponent>())
        {
            RenderItem item{};
            if (renderer->BuildRenderItem(object, item)) found |= accumulate(item);
        }
        return found;
    };

    const EditorNS::WorldBounds bounds = EditorNS::EditorSelectionBounds::Compute(
        scene, selection, render_bounds_provider);

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
    ensure_editor_camera_presets_loaded();

    ImGui::Indent();
    auto& preset = active_editor_camera_preset();
    ImGui::Text(u8"操作プリセット: %s", preset.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(preset.Editable() ? "[Personal]" : "[Shared]");
    if (ImGui::Button(u8"プリセット管理を開く")) show_camera_preset_manager = true;
    ReplayEngine::Editor::EditorHelp::Item("button.camera.open_preset_manager");
    if (!preset.Editable())
    {
        ImGui::SameLine();
        if (ImGui::Button(u8"自分用に複製")) make_active_editor_camera_preset_personal_copy();
        ReplayEngine::Editor::EditorHelp::Item("button.camera.duplicate_preset");
    }

    bool changed = false;
    if (active_editor_camera_preset().Editable())
    {
        const float speed_before = editor_camera.move_speed;
        changed |= ImGui::DragFloat(u8"移動速度", &editor_camera.move_speed, 0.1f, 0.0f, 0.0f, "%.3f");
        if (!(editor_camera.move_speed > 0.0f) || !std::isfinite(editor_camera.move_speed))
            editor_camera.move_speed = speed_before > 0.0f ? speed_before : 5.0f;
        changed |= ImGui::DragFloat(u8"高速倍率", &editor_camera.fast_multiplier, 0.1f, 0.01f, 100.0f, "%.2f");
        changed |= ImGui::DragFloat(u8"低速倍率", &editor_camera.slow_multiplier, 0.01f, 0.001f, 1.0f, "%.3f");
        changed |= ImGui::DragFloat(u8"回転感度", &editor_camera.mouse_sensitivity, 0.01f, 0.001f, 5.0f, "%.3f");
        changed |= ImGui::DragFloat(u8"平行移動感度", &editor_camera.pan_sensitivity, 0.01f, 0.001f, 20.0f, "%.3f");
        changed |= ImGui::DragFloat(u8"Zoom/Dolly感度", &editor_camera.zoom_sensitivity, 0.01f, 0.001f, 20.0f, "%.3f");
        changed |= ImGui::DragFloat(u8"視野角", &editor_camera.field_of_view_degrees, 0.5f, 5.0f, 170.0f, "%.1f");
        if (changed) save_active_editor_camera_preset();
    }
    else
    {
        ImGui::TextDisabled(u8"Shared preset の値は直接変更しません。複製すると編集できます。");
    }

    ImGui::Spacing();
    if (ImGui::Button(u8"選択対象へフォーカス")) focus_editor_camera_on_selection();
    ReplayEngine::Editor::EditorHelp::Item("button.camera.focus_selection");

    const auto& position = editor_camera.Position();
    ImGui::TextDisabled(u8"位置   %.2f  %.2f  %.2f", position.x, position.y, position.z);
    ImGui::TextDisabled(u8"回転   yaw %.1f°  pitch %.1f°  roll %.1f°",
        DirectX::XMConvertToDegrees(editor_camera.Yaw()),
        DirectX::XMConvertToDegrees(editor_camera.Pitch()),
        DirectX::XMConvertToDegrees(editor_camera.Roll()));
    ImGui::TextDisabled(u8"Orbit 距離  %.2f", editor_camera.OrbitDistance());
    ImGui::TextDisabled(u8"操作キーは Camera preset ごとに自由設定できます");

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
    if (standalone_game_mode) return;

    namespace EditorNS = ReplayEngine::Editor;

    editor_camera_state_key = make_editor_camera_state_key();

    EditorNS::EditorCameraStateStore::State state;
    std::string error;
    const auto path = EditorNS::EditorCameraStateStore::PathForKey(editor_camera_state_key);

    if (EditorNS::EditorCameraStateStore::Load(state, path, error))
    {
        EditorNS::EditorCameraStateStore::Apply(state, editor_camera);
    }
    else
    {
        // 保存が無い / 壊れている。既定位置から始める。
        // ここで失敗しても Scene の読み込みには一切影響しない。
        editor_camera.ResetToDefault();
    }

    // 視点（position/yaw/pitch/roll）は Scene ごと。操作感は user preset が正。
    ensure_editor_camera_presets_loaded();
    active_editor_camera_preset().ApplyCameraSettings(editor_camera);
}

void framework::save_editor_camera_state()
{
    if (standalone_game_mode) return;

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

void framework::load_editor_camera_move_speed_preference()
{
    namespace EditorNS = ReplayEngine::Editor;
    float saved_speed = editor_camera.move_speed;
    std::string error;
    if (EditorNS::EditorCameraStateStore::LoadMoveSpeedPreference(saved_speed, error))
        editor_camera.move_speed = saved_speed;
}

bool framework::save_editor_camera_move_speed_preference()
{
    namespace EditorNS = ReplayEngine::Editor;
    if (!(editor_camera.move_speed > 0.0f) || !std::isfinite(editor_camera.move_speed))
        return false;

    std::string error;
    return EditorNS::EditorCameraStateStore::SaveMoveSpeedPreference(
        editor_camera.move_speed, error);
}
