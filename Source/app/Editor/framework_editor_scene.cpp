// Editor のうち「Scene View 前半（Recovery / Unsaved prompt / Toolbar）」を持つ。
//
//   framework_editor_scene.cpp     … Recovery / Unsaved prompt / Toolbar（このファイル）
//   framework_editor_scene_view.cpp … Scene View / Search / Hierarchy
//
// 各関数の本体は分割前のまま移動し、Editor の操作順序は変更しない。
#include "framework.h"

#include "../../RePlayEngine/Components/Core/PivotComponent.h"
#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerControllerComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerInputComponent.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpProject.h"
#include "../../RePlayEngine/Scripting/CSharp/CSharpScriptBackend.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../RePlayEngine/Scene/Serialization/SceneSerializer.h"
#include "shader.h"
#include "texture.h"
#include "skinned_mesh.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

void framework::draw_object_scene_recovery_prompt()
{
    if (!object_recovery_available) return;
    if (!object_recovery_prompt_opened)
    {
        ImGui::OpenPopup("Scene Recovery");
        object_recovery_prompt_opened = true;
    }

    namespace Serialization = ReplayEngine::Scene::Serialization;
    if (!ImGui::BeginPopupModal("Scene Recovery", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::TextUnformatted("A newer autosave was found.");
    ImGui::TextWrapped("Scene: %s", object_scene_path.string().c_str());
    ImGui::TextWrapped("Autosave: %s", object_recovery_path.string().c_str());

    Serialization::SceneData preview;
    std::string preview_error;
    const bool readable = Serialization::SceneSerializer::LoadFromFile(
        preview, object_recovery_path, preview_error);
    if (readable)
        ImGui::Text("Version %d / %zu GameObjects", preview.version, preview.objects.size());
    else
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.30f, 1.0f), "%s", preview_error.c_str());

    if (ImGui::Button("Recover", ImVec2(110.0f, 0.0f)) && readable)
    {
        if (object_scene_play_mode) exit_object_play_mode();
        detach_collision_world();
        Serialization::SceneLoadReport report;
        Serialization::ApplySceneData(preview, object_scene, report);
        object_editor_context.ResetSceneState();
        object_scene.Start();
        object_editor_context.AttachScene(&object_scene);
        object_editor_context.SetScenePath(object_scene_path);
        object_editor_context.MarkDirty();
        attach_collision_world(object_scene);
        object_editor_context.SetStatus("Autosaveを復旧しました。Saveで本Sceneへ反映してください");
        object_recovery_available = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep for later", ImVec2(110.0f, 0.0f)))
    {
        object_recovery_available = false;
        object_editor_context.SetStatus("Autosaveを保持しました");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(110.0f, 0.0f)))
    {
        std::error_code error;
        std::filesystem::remove(object_recovery_path, error);
        object_recovery_available = false;
        object_editor_context.SetStatus(error ? "Autosaveを削除できませんでした" : "Autosaveを破棄しました");
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
void framework::draw_unsaved_object_scene_prompt()
{
    if (object_scene_unsaved_prompt_requested)
    {
        ImGui::OpenPopup(u8"未保存のシーン");
        object_scene_unsaved_prompt_requested = false;
        object_unsaved_prompt_open = true;
    }

    if (!ImGui::BeginPopupModal(u8"未保存のシーン", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        // 開いていたものが閉じられた場合だけ後始末する。
        //
        // Esc で閉じると、どのボタンも押されないまま予約だけが残る。
        // 残したままにすると、次に別の操作を要求したときに
        // 古い予約（たとえばアプリ終了）が実行されてしまう。
        // 閉じた = 選ばなかった、として取り消す。
        if (object_unsaved_prompt_open)
        {
            object_unsaved_prompt_open = false;
            pending_object_scene_action = object_scene_action::none;
            pending_object_scene_path.clear();
        }
        return;
    }

    // 終了なのか、別のシーンへ移るだけなのかでボタンの意味が変わる。
    // どちらも「続行」と書くと、押した結果がアプリ終了なのか分からない。
    const bool exiting = pending_object_scene_action == object_scene_action::exit_application;
    const char* save_label = exiting ? u8"保存して終了" : u8"保存して続行";
    const char* discard_label = exiting ? u8"破棄して終了" : u8"破棄して続行";

    ImGui::TextUnformatted(u8"現在のシーンには未保存の変更があります。");
    ImGui::TextWrapped(u8"%s", object_editor_context.DisplayTitle().c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted(exiting
        ? u8"保存してから終了しますか？"
        : u8"保存してから続行しますか？");

    if (!object_scene_save_failure.empty())
    {
        // 保存に失敗したまま黙って閉じない。閉じると
        // 「保存できていないのに終了した」ように見える。
        // ただし理由を出さないと、押しても無反応にしか見えない。
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
            u8"保存できませんでした");
        ImGui::TextWrapped(u8"%s", object_scene_save_failure.c_str());
        ImGui::TextDisabled(u8"別名で保存するか、破棄して終了を選んでください。");
        ImGui::Spacing();
    }

    if (ImGui::Button(save_label, ImVec2(140.0f, 0.0f)))
    {
        if (save_object_scene(false))
        {
            object_scene_save_failure.clear();
            // 終了を確定させてから実行する。
            // 実行後に Dirty が立て直されても、もう確認へは戻らない。
            if (exiting) object_exit_confirmed = true;
            object_unsaved_prompt_open = false;
            ImGui::CloseCurrentPopup();
            execute_pending_object_scene_action();
        }
        else
        {
            // 失敗の理由をこのダイアログへ出す。
            // ステータス行はプロジェクトタブにしか出ず、
            // ここからは見えないため気付けなかった。
            object_scene_save_failure = object_editor_context.Status();
            if (object_scene_save_failure.empty())
            {
                object_scene_save_failure = u8"理由は不明です。";
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"別名で保存", ImVec2(120.0f, 0.0f)))
    {
        if (save_object_scene(true))
        {
            object_scene_save_failure.clear();
            if (exiting) object_exit_confirmed = true;
            object_unsaved_prompt_open = false;
            ImGui::CloseCurrentPopup();
            execute_pending_object_scene_action();
        }
        else
        {
            object_scene_save_failure = object_editor_context.Status();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(discard_label, ImVec2(140.0f, 0.0f)))
    {
        // ここは「変更を捨てる」が仕事。編集内容は保存しない。
        object_scene_save_failure.clear();
        discard_object_scene_autosave();
        object_editor_context.ClearDirty();
        if (exiting) object_exit_confirmed = true;
        object_unsaved_prompt_open = false;
        ImGui::CloseCurrentPopup();
        execute_pending_object_scene_action();
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"キャンセル", ImVec2(100.0f, 0.0f)))
    {
        object_scene_save_failure.clear();
        pending_object_scene_action = object_scene_action::none;
        pending_object_scene_path.clear();
        object_unsaved_prompt_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void framework::draw_editor_toolbar()
{
    // New / Open / Save / Undo / Redo はツールバーへ置かない。
    //
    // File / Edit メニューと Ctrl+N/O/S/Z/Y に同じものがあり、
    // ツールバーに並べても幅を食うだけで得るものが無い。
    // ここへ残すのは「今どの状態か」が見た目に要るものだけにする。
    //   ギズモの操作種別 … 今どのモードかが分からないと操作できない
    //   実行 / 停止      … 今 Play 中かどうかが一目で要る
    //
    // 未保存かどうかはウィンドウタイトルとシーン名の * で分かる。
    // モードごとに Scene View のギズモの形が変わる。
    //   Move   … 軸線 + 先端の丸
    //   Rotate … 軸まわりの円
    //   Scale  … 軸線 + 先端の四角
    //
    // 選択中のモードはボタンの色でも示す。
    // 形だけだと Scene View を見ていないと分からず、
    // ツールバーを見ても «今どれか» が読み取れなかった。
    const auto gizmo_mode_button = [&](const char* label,
        ReplayEngine::Editor::GizmoOperation mode, const char* tooltip)
    {
        const bool active = transform_gizmo.Operation() == mode;
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        }
        if (ImGui::Button(label)) transform_gizmo.SetOperation(mode);
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    };

    gizmo_mode_button("Move", ReplayEngine::Editor::GizmoOperation::Translate,
        u8"移動（既定: Shift+W）\n"
        u8"軸線の先端が丸。軸をドラッグするとその方向へ動く。\n"
        u8"ドラッグ中に Esc で取り消し。");
    ImGui::SameLine();
    gizmo_mode_button("Rotate", ReplayEngine::Editor::GizmoOperation::Rotate,
        u8"回転（既定: Shift+E）\n"
        u8"軸まわりの円。円周を掴んで、円に沿って引くと回る。\n"
        u8"ドラッグ中に Esc で取り消し。");
    ImGui::SameLine();
    gizmo_mode_button("Scale", ReplayEngine::Editor::GizmoOperation::Scale,
        u8"拡縮（既定: Shift+R）\n"
        u8"軸線の先端が四角。軸をドラッグするとその軸だけ伸縮する。\n"
        u8"ドラッグ中に Esc で取り消し。");
    ImGui::SameLine();
    bool snap = transform_gizmo.SnapEnabled();
    if (ImGui::Checkbox("Snap", &snap)) transform_gizmo.SetSnapEnabled(snap);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            u8"ドラッグ量を一定の刻みに丸める。\n"
            u8"移動・回転・拡縮のどのモードにも効く。");
    }
    ImGui::SameLine();
    if (ImGui::Button(gizmo_local_space ? "Local" : "World"))
        gizmo_local_space = !gizmo_local_space;
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            u8"ギズモの軸の向きを切り替える。\n"
            u8"World … ワールド座標の軸に固定する。\n"
            u8"Local … 選択しているオブジェクトの回転に追従する。\n"
            u8"傾いた物を «その物にとっての前» へ動かしたいときは Local。");
    }

    ReplayEngine::Core::GameObject* pivot_object =
        object_editor_context.Selection().ResolvePrimary(active_object_scene());
    const bool has_pivot = pivot_object != nullptr &&
        pivot_object->GetComponent<ReplayEngine::Components::PivotComponent>() != nullptr;
    ImGui::SameLine();
    if (ImGui::Button(pivot_edit_mode ? u8"Pivot:ON" : u8"Pivot"))
        pivot_edit_mode = has_pivot ? !pivot_edit_mode : false;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(has_pivot
            ? u8"Pivot 編集補助。Transform は動かさず基準点だけを編集する。"
            : u8"選択オブジェクトへ Pivot Component を追加すると使える。");
    if (pivot_edit_mode && has_pivot)
    {
        ImGui::SameLine();
        if (ImGui::Button(u8"面Snap")) snap_primary_pivot_to_mesh(0);
        ImGui::SameLine();
        if (ImGui::Button(u8"頂点Snap")) snap_primary_pivot_to_mesh(1);
        ImGui::SameLine();
        if (ImGui::Button(u8"辺Snap")) snap_primary_pivot_to_mesh(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"CookedMeshCollision の実三角形へ正確に吸着する。");
    }

    ImGui::SameLine();
    bool auxiliary_views = editor_auxiliary_views;
    if (ImGui::Checkbox(u8"補助View", &auxiliary_views))
        editor_auxiliary_views = auxiliary_views;
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            u8"Scene View の右側へ Front / Side / Top を重ねて表示する。\n"
            u8"メイン View は全面のままなので Picking / Gizmo の座標は変わらない。");
    }

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // 実行ボタンは色と大きさで他から切り離す。
    //
    // 以前は同じ見た目のボタンが 8 個並ぶ中に "Play" が紛れており、
    // しかもメニューバーにも同名の "Play" があった。
    // どちらを押せばよいか画面から判断できず、実際に迷子になった。
    const ImVec2 transport_size(96.0f, 0.0f);
    if (!object_scene_play_mode)
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.16f, 0.62f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.74f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.12f, 0.50f, 0.22f, 1.0f));
        if (ImGui::Button(u8"▶ 実行 (F5)", transport_size)) enter_object_play_mode();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(u8"ゲームを実行します。\n"
                u8"実行用のコピーが動くので、編集中のシーンは変わりません。\n"
                u8"C# スクリプトの Update はここから先でしか動きません。");
        }
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.70f, 0.52f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.84f, 0.63f, 0.20f, 1.0f));
        if (ImGui::Button(object_scene_paused ? u8"▶ 再開" : u8"❚❚ 一時停止", transport_size))
            object_scene_paused = !object_scene_paused;
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.72f, 0.22f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.30f, 0.26f, 1.0f));
        if (ImGui::Button(u8"■ 停止", transport_size)) exit_object_play_mode();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(u8"編集モードへ戻ります。\n"
                u8"実行中に動いた位置や生成した物はすべて破棄されます。");
        }
    }

    if (ImGui::GetContentRegionAvail().x > 280.0f)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        if (focus_search_requested)
        {
            ImGui::SetKeyboardFocusHere();
            focus_search_requested = false;
        }
        ImGui::InputTextWithHint("##FeatureSearch", "Search...", editor_search_text,
            IM_ARRAYSIZE(editor_search_text));
        // 倒すのは draw_editor() の先頭でやっている。
        // ここは描かれたフレームで立て直すだけ。
        search_input_active = ImGui::IsItemActive();
    }
}
