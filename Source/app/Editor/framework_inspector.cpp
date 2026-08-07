#include "framework.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerControllerComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerInputComponent.h"

#include <string>

void framework::draw_shader_adjustment_workspace()
{
    ImGui::TextUnformatted("シェーダー調整");
    ImGui::TextDisabled(
        "Sceneの材質はGameObjectのMesh/Skinned Mesh RendererまたはMaterial Assetで編集します。");
    ImGui::Separator();

    if (ImGui::BeginTabBar("ShaderAdjustmentTabs"))
    {
        if (ImGui::BeginTabItem(u8"デバッグメッシュ"))
        {
            // ここは「デバッグ用の静的メッシュ」専用の表示に限定する。
            //
            // 以前はここに本番と同じ見た目のシェーダ編集欄があったが、
            // 編集していたのは shading_per_static[0]（デバッグメッシュ）だけで、
            // 選択中の GameObject には一切効かなかった。
            // 同じ操作が 2 箇所にあり、片方が偽物という状態だったため撤去した。
            //
            // マテリアルの編集は Inspector の Component Card 1 箇所へ集約した。
            // オブジェクトごとに違うシェーダを掛けたい場合も、
            // その GameObject を選んで Component Card から設定する。
            ImGui::Checkbox("デバッグ静的メッシュを表示", &enable_static_meshes);
            ImGui::Separator();
            ImGui::TextDisabled(u8"マテリアルの編集はここではありません。");
            ImGui::TextDisabled(
                u8"Hierarchy で GameObject を選び、Inspector の");
            ImGui::TextDisabled(
                u8"Mesh Renderer / Material から編集してください。");
            ImGui::TextDisabled(
                u8"オブジェクトごとに別のシェーダを設定できます。");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("画面効果"))
        {
            draw_screen_effect_stack();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("描画確認"))
        {
            ImGui::TextUnformatted("レンダラー: Deferred（固定）");
            ImGui::TextDisabled("輪郭線と半透明表現はDeferred照明後に追加パスで合成します。");

            // PBR / トゥーン / アンリットのチェックはここからも撤去した。
            // 実体はマテリアルの指定を無言で Unlit へ降格させる
            // グローバルスイッチで、絵柄が変わらない原因になっていた。
            // 絵柄はマテリアルだけが決める。
            ImGui::TextDisabled("絵柄はマテリアルごとに設定します。");
            ImGui::TextDisabled("プロジェクト → Material を選ぶと編集できます。");
            ImGui::Separator();
            ImGui::Checkbox("輪郭線パス", &enable_outline_shader);
            ImGui::Checkbox("PBR影パス", &enable_pbr_shadow_shader);
            int output = render_graph.OutputIndex();
            if (ImGui::Combo("描画出力 (F2)", &output, ReplayEngine::Rendering::RenderGraph::Names()))
                render_graph.SetOutput(output);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void framework::draw_inspector()
{
    ImGui::Begin("インスペクター");

    const char* tables[] = {
        "基本", "配置", "モデリング", "アニメーション", "レンダリング", "シェーダー調整"
    };
    int table = static_cast<int>(active_editor_workspace);
    ImGui::TextDisabled("編集テーブル");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##RightUpperWorkspace", &table, tables, IM_ARRAYSIZE(tables)))
        set_editor_workspace(static_cast<editor_workspace>(table));
    ImGui::Separator();
    if (active_editor_workspace == editor_workspace::shader_adjustment)
    {
        draw_shader_adjustment_workspace();
        ImGui::End();
        return;
    }

    switch (selected_editor_object)
    {
    case editor_selection::world:
        ImGui::TextUnformatted("ワールド");
        ImGui::Separator();
        ImGui::ColorEdit4("背景色", &background_color.x);
        ImGui::Checkbox("背景画像", &draw_background_image);
        ImGui::Checkbox("ゲームシーン", &enable_scene_game);
        ImGui::Checkbox("パーティクル", &enable_particles);
        ImGui::Checkbox("軌跡", &enable_trail);
        ImGui::Separator();
        draw_project_settings_panel();
        ImGui::Separator();
        draw_controlled_character_diagnostics();
        break;

    case editor_selection::camera:
        ImGui::TextUnformatted("カメラ");
        ImGui::Separator();

        // Scene View 用の編集カメラ。ゲーム内のカメラとは別物。
        draw_editor_camera_settings();
        ImGui::Separator();

        ImGui::TextUnformatted("Runtime Camera（ゲーム内）");
        if (enable_scene_game && game_scene)
        {
            const auto& camera = game_scene->Gameplay().GetCamera();
            const auto& eye = camera.GetEye();
            const auto& focus = camera.GetFocus();
            ImGui::Text("位置     %.3f  %.3f  %.3f", eye.x, eye.y, eye.z);
            ImGui::Text("注視点   %.3f  %.3f  %.3f", focus.x, focus.y, focus.z);
            game_scene->Gameplay().DrawCameraGUI();
        }
        else
        {
            ImGui::DragFloat3("位置", &camera_position.x, 0.05f, -100.0f, 100.0f, "%.3f");
        }
        break;

    case editor_selection::game_object:
        // GameObject / Component の編集は専用パネルへ委譲する。
        // ここに Component 型ごとの分岐は書かない。
        // 表示内容は ComponentRegistry と PropertyRegistry から自動生成される。
        object_inspector_panel.DrawContents(object_editor_context);
        break;

    case editor_selection::directional_light:
        ImGui::TextUnformatted("平行光源 / PBR");
        ImGui::Separator();
        ImGui::DragFloat3("方向", &light_direction.x, 0.01f, -1.0f, 1.0f);
        ImGui::ColorEdit3("色", &pbr.light.directional_color.x);
        ImGui::SliderFloat("強さ", &pbr.light.directional_color.w, 0, 10);
        ImGui::SliderFloat("IBL Diffuse", &pbr.light.ibl_params.x, 0, 4);
        ImGui::SliderFloat("IBL Specular", &pbr.light.ibl_params.y, 0, 4);
        ImGui::SliderFloat("AO Strength", &pbr.light.ibl_params.z, 0, 1);
        ImGui::SliderFloat("PBR Exposure", &pbr.light.ibl_params.w, 0, 4);
        ImGui::SliderFloat("Shadow Strength", &pbr.light.shadow_params.x, 0, 1);
        ImGui::SliderFloat("Shadow Bias", &pbr.light.shadow_params.y, 0, 0.01f, "%.5f");
        ImGui::SliderFloat("Shadow Filter", &pbr.light.shadow_params.z, 0, 4);
        {
            bool enabled = pbr.light.shadow_params.w > 0.5f;
            if (ImGui::Checkbox("PBR Shadow", &enabled)) pbr.light.shadow_params.w = enabled ? 1.0f : 0.0f;
        }
        if (ImGui::CollapsingHeader("Cascaded Shadow Map"))
        {
            ImGui::DragFloat4("Splits", &csm.constants.split_distances.x, 0.5f, 1, 500);
            ImGui::DragFloat("CSM Bias", &csm.constants.params.x, 0.0005f, 0, 0.05f, "%.5f");
            ImGui::DragFloat("Normal Bias", &csm.constants.params.y, 0.005f, 0, 1);
            ImGui::DragFloat("Filter", &csm.constants.params.z, 0.05f, 0, 8);
            bool enabled = csm.constants.params.w > 0.5f;
            if (ImGui::Checkbox("Enable CSM", &enabled)) csm.constants.params.w = enabled ? 1.0f : 0.0f;
        }
        break;

    case editor_selection::point_lights:
    {
        ImGui::TextUnformatted("点光源");
        ImGui::Separator();
        int count = lights.data.light_counts.x;
        const int old_count = count;
        if (ImGui::DragInt("個数", &count, 0.1f, 0, lights_manager::POINT_LIGHT_MAX))
        {
            lights.data.light_counts.x = count;
            for (int i = old_count; i < count; ++i)
            {
                lights.data.point_lights[i].position = { -4.0f + i * 2.5f, 3, -24, 14 };
                lights.data.point_lights[i].color = { 0.55f, 0.75f, 1, 1.8f };
            }
        }
        for (int i = 0; i < count; ++i)
        {
            ImGui::PushID(i);
            if (ImGui::CollapsingHeader(("Point Light " + std::to_string(i)).c_str()))
            {
                ImGui::DragFloat4("Position / Radius", &lights.data.point_lights[i].position.x, 0.05f);
                ImGui::ColorEdit4("Color / Intensity", &lights.data.point_lights[i].color.x);
            }
            ImGui::PopID();
        }
        break;
    }

    case editor_selection::rendering:
        ImGui::TextUnformatted("描画設定");
        ImGui::Separator();
        {
            bool fullscreen = is_fullscreen();
            if (ImGui::Checkbox("全画面表示 (F11 / Alt+Enter)", &fullscreen)) toggle_fullscreen();
        }
        ImGui::TextUnformatted("レンダラー: Deferred（固定）");
        ImGui::TextDisabled("輪郭線・半透明はDeferred照明後の追加パスとして合成します");
        ImGui::TextDisabled("Forward+は将来、別レンダラーとして追加できます");
        ImGui::Checkbox("静的メッシュ", &enable_static_meshes);
        if (ImGui::Button("シェーダー調整テーブルを開く"))
            set_editor_workspace(editor_workspace::shader_adjustment);
        // 【削除した項目について】
        //
        // ここには PBR / トゥーン / アンリット のチェックボックスがあったが、
        // 実体は「マテリアルの指定を無視して Unlit へ降格させる」
        // グローバルスイッチだった。
        //
        // マテリアルでトゥーンを選んでいても、ここのチェックが外れていれば
        // 無言で Unlit になる。しかも理由は画面にもログにも出ない。
        // 「マテリアルを変えたのに絵が変わらない」の原因がこれだった。
        //
        // 絵柄はマテリアルだけが決める形に変えたので、この 3 つは
        // 何の効果も持たなくなった。押しても何も起きないコントロールを
        // 残すのは害しかないため撤去する。
        //
        // 輪郭線パスと PBR 影パスは今も描画側で参照されているので残す。
        if (ImGui::CollapsingHeader("追加パス"))
        {
            ImGui::TextDisabled("絵柄（PBR / トゥーン / アンリット）は");
            ImGui::TextDisabled("マテリアルごとに設定します。");
            ImGui::TextDisabled("プロジェクト → Material を選ぶと編集できます。");
            ImGui::Separator();
            ImGui::Checkbox("輪郭線パス", &enable_outline_shader);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("輪郭線レイヤを持つマテリアルの追加パスを描くか");
            ImGui::Checkbox("PBR影パス", &enable_pbr_shadow_shader);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("PBR の影用の追加パスを描くか");
        }
        {
            int output = render_graph.OutputIndex();
            if (ImGui::Combo("Render Output (F2)", &output, ReplayEngine::Rendering::RenderGraph::Names()))
            {
                render_graph.SetOutput(output);
                if (render_graph.RequiresDeferred()) enable_deferred = true;
            }
        }
        draw_screen_space_settings();
        break;

    case editor_selection::post_process:
        ImGui::TextUnformatted("ポスト処理");
        ImGui::Separator();
        draw_screen_effect_stack();
        break;
    }
    ImGui::End();
}
