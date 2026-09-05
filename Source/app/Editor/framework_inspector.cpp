#include "framework.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerControllerComponent.h"
#include "../../RePlayEngine/Components/Gameplay/PlayerInputComponent.h"
#include "../../RePlayEngine/Components/Rendering/MaterialOverrideDynamicProperties.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../RePlayEngine/Assets/AssetDatabase.h"
#include "../../RePlayEngine/Editor/ReorderableList.h"
#include "../../RePlayEngine/Editor/Style/EditorStyle.h"

#include <array>
#include <functional>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>


namespace
{
    std::vector<std::string> material_subset_names(const skinned_mesh& mesh_asset)
    {
        // Slot は Object 全体の通し番号。描画側の material_slot_cursor と同じ順序・
        // 同じ個数で並べる。mesh 間の最大値にすると、glTF のように 1 primitive が
        // 1 mesh + subset 1 個で入る形式で行が 1 つしか出ない。
        const std::size_t limit =
            static_cast<std::size_t>(ReplayEngine::Components::max_mesh_material_slots);
        std::vector<std::string> names;
        for (const skinned_mesh::mesh& mesh : mesh_asset.meshes)
        {
            if (mesh.subsets.empty())
            {
                if (names.size() >= limit) break;
                names.emplace_back();
                continue;
            }
            for (const skinned_mesh::mesh::subset& subset : mesh.subsets)
            {
                if (names.size() >= limit) break;
                names.push_back(subset.material_name);
            }
            if (names.size() >= limit) break;
        }
        return names;
    }

    template<class T>
    void initialize_material_slots(T& renderer, const std::vector<std::string>& default_names)
    {
        using namespace ReplayEngine::Components;
        const int old_count = ClampedMaterialSlotCount(renderer);
        const std::size_t old_size = (std::min)(renderer.material_slots.size(),
            static_cast<std::size_t>(old_count));
        const int visible_count = static_cast<int>((std::min)(default_names.size(),
            static_cast<std::size_t>(max_mesh_material_slots)));
        const int required = (std::max)(old_count, visible_count);
        EnsureMaterialSlotStorage(renderer, required);
        for (std::size_t index = old_size;
            index < renderer.material_slots.size() && index < default_names.size(); ++index)
        {
            renderer.material_slots[index].name = default_names[index];
        }
    }

    bool draw_material_asset_slot(const char* label,
        const ReplayEngine::Assets::AssetDatabase* database,
        const std::string& current, std::string& selected, bool editable,
        const char* empty_preview,
        ReplayEngine::Assets::AssetKind kind =
            ReplayEngine::Assets::AssetKind::Material)
    {
        selected = current;
        const ReplayEngine::Assets::AssetRecord* record = database != nullptr && !current.empty()
            ? database->FindByGuid(current) : nullptr;
        const char* preview = current.empty() ? empty_preview :
            (record != nullptr && (database == nullptr || !database->IsMissing(current))
                ? record->display_name.c_str() : u8"Missing Asset");
        if (!editable)
        {
            ImGui::TextUnformatted(preview);
            return false;
        }
        bool changed = false;
        if (ImGui::BeginCombo(label, preview))
        {
            if (ImGui::Selectable(empty_preview, current.empty()))
            {
                selected.clear();
                changed = true;
            }
            if (database != nullptr)
            {
                for (const ReplayEngine::Assets::AssetRecord& candidate : database->Records())
                {
                    if (candidate.kind != kind) continue;
                    if (database->IsMissing(candidate.guid)) continue;
                    const bool is_selected = candidate.guid == current;
                    ImGui::PushID(candidate.guid.c_str());
                    if (ImGui::Selectable(candidate.display_name.c_str(), is_selected))
                    {
                        selected = candidate.guid;
                        changed = true;
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // テクスチャの見本を引く。Project ブラウザと同じ経路で、結果は向こうで持ち回される。
    using SlotTexturePreview = std::function<void*(const std::filesystem::path&)>;

    template<class T>
    void draw_material_slot_rows(ReplayEngine::Editor::EditorContext& context,
        T& renderer, const std::vector<std::string>& default_names,
        const char* title, bool has_fbx_fallback,
        const SlotTexturePreview& texture_preview, float preview_scale)
    {
        using namespace ReplayEngine::Components;
        if (default_names.empty()) return;
        const bool editable = context.CanEdit();
        const int stored_count = ClampedMaterialSlotCount(renderer);
        ImGui::Separator();
        ImGui::TextUnformatted(title);
        ImGui::TextDisabled(u8"空スロットは既存の「マテリアル」へフォールバックします");
        ImGui::PushID(&renderer);
        const std::size_t row_count = (std::min)(default_names.size(),
            static_cast<std::size_t>(max_mesh_material_slots));
        ReplayEngine::Editor::ReorderRequest move_request{};
        for (std::size_t index = 0; index < row_count; ++index)
        {
            const bool stored = index < static_cast<std::size_t>(stored_count) &&
                index < renderer.material_slots.size();
            const std::string current_name = stored
                ? renderer.material_slots[index].name : default_names[index];
            const std::string current_asset = stored
                ? renderer.material_slots[index].asset : std::string{};
            const std::string item_id = "MaterialSlot" + std::to_string(index);
            const std::string item_title = "Slot " + std::to_string(index + 1) +
                (current_name.empty() ? std::string() : "  " + current_name);
            const ReplayEngine::Editor::ReorderableItemResult reorder =
                ReplayEngine::Editor::DrawReorderableItem(
                    &renderer, item_id.c_str(), index, row_count, item_title.c_str(),
                    false, true, editable, [] {});
            if (reorder.request.Valid() && !move_request.Valid())
                move_request = reorder.request;
            if (!reorder.opened) continue;

            ImGui::PushID(item_id.c_str());
            ImGui::Indent();
            ImGui::TextDisabled(u8"名前");
            const std::string hint = std::to_string(index) + u8" 番";

            std::vector<char> name_buffer((std::max)(static_cast<std::size_t>(4096),
                current_name.size() + static_cast<std::size_t>(4096)), '\0');
            if (!current_name.empty())
                std::memcpy(name_buffer.data(), current_name.data(), current_name.size());
            ImGui::SetNextItemWidth(-1.0f);
            ImGuiInputTextFlags name_flags = ImGuiInputTextFlags_EnterReturnsTrue;
            if (!editable) name_flags |= ImGuiInputTextFlags_ReadOnly;
            const bool enter_confirmed = ImGui::InputTextWithHint("##SlotName", hint.c_str(),
                name_buffer.data(), name_buffer.size(), name_flags);
            const bool focus_confirmed = ImGui::IsItemDeactivatedAfterEdit();
            if (editable && (enter_confirmed || focus_confirmed))
            {
                context.BeginEdit(u8"マテリアルスロット名を変更");
                initialize_material_slots(renderer, default_names);
                renderer.material_slots[index].name.assign(name_buffer.data());
                renderer.OnPropertyChanged(nullptr);
                context.CommitEdit();
            }

            ImGui::TextDisabled(u8"マテリアル");
            std::string selected_asset;
            if (draw_material_asset_slot("##SlotMaterial", context.GetAssetDatabase(),
                current_asset, selected_asset, editable, u8"(material_asset を使用)"))
            {
                context.BeginEdit(u8"マテリアルスロットを変更");
                initialize_material_slots(renderer, default_names);
                renderer.material_slots[index].asset = std::move(selected_asset);
                renderer.OnPropertyChanged(nullptr);
                context.CommitEdit();
            }
            if (current_asset.empty() && renderer.material_asset.empty() && has_fbx_fallback)
                ImGui::TextDisabled(u8"共通も未設定なら FBX 材質");

            // モデルが持つテクスチャの差し替え。空ならモデルのものをそのまま使う。
            const struct
            {
                const char* label;
                const char* id;
                std::string ReplayEngine::Components::MeshMaterialSlot::* member;
            } slot_texture_rows[] = {
                { u8"ベースカラー（色）", "##SlotBaseColorTexture",
                    &ReplayEngine::Components::MeshMaterialSlot::base_color_texture },
                { u8"法線マップ（凹凸）", "##SlotNormalTexture",
                    &ReplayEngine::Components::MeshMaterialSlot::normal_texture },
                { u8"ORM（AO・粗さ・金属）", "##SlotOrmTexture",
                    &ReplayEngine::Components::MeshMaterialSlot::orm_texture },
                { u8"エミッシブ（発光）", "##SlotEmissiveTexture",
                    &ReplayEngine::Components::MeshMaterialSlot::emissive_texture },
            };
            for (const auto& texture_row : slot_texture_rows)
            {
                const std::string current_texture = stored
                    ? renderer.material_slots[index].*texture_row.member : std::string{};
                std::string selected_texture;
                // 選んだ画像を小さく出す。折りたたんだスロットはここまで来ない。
                void* preview = nullptr;
                if (!current_texture.empty() && texture_preview &&
                    context.GetAssetDatabase() != nullptr)
                {
                    const ReplayEngine::Assets::AssetRecord* texture_record =
                        context.GetAssetDatabase()->FindByGuid(current_texture);
                    if (texture_record != nullptr)
                        preview = texture_preview(texture_record->source_path);
                }
                const float preview_extent = ImGui::GetTextLineHeight() * preview_scale;
                const ImVec2 preview_size(preview_extent, preview_extent);
                ImGui::TextUnformatted(texture_row.label);
                if (preview != nullptr)
                {
                    ImGui::Image(reinterpret_cast<ImTextureID>(preview), preview_size);
                    // 柄を見分けたいので、乗せている間だけ大きく出す。
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Image(reinterpret_cast<ImTextureID>(preview),
                            ImVec2(320.0f, 320.0f));
                        ImGui::EndTooltip();
                    }
                }
                else
                {
                    ImGui::Dummy(preview_size);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                if (draw_material_asset_slot(texture_row.id, context.GetAssetDatabase(),
                    current_texture, selected_texture, editable, u8"(モデルのまま)",
                    ReplayEngine::Assets::AssetKind::Image))
                {
                    context.BeginEdit(u8"スロットのテクスチャを変更");
                    initialize_material_slots(renderer, default_names);
                    renderer.material_slots[index].*texture_row.member =
                        std::move(selected_texture);
                    renderer.OnPropertyChanged(nullptr);
                    context.CommitEdit();
                }
            }
            ImGui::Unindent();
            ImGui::PopID();
        }
        if (const char* dragging = ReplayEngine::Editor::ActiveReorderLabel(&renderer))
        {
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_DragDropTarget],
                u8"移動中: %s", dragging);
        }
        if (move_request.Valid() && editable)
        {
            context.BeginEdit(u8"マテリアルスロットを並べ替え");
            initialize_material_slots(renderer, default_names);
            if (move_request.source < renderer.material_slots.size() &&
                move_request.destination < renderer.material_slots.size())
            {
                if (move_request.source < move_request.destination)
                {
                    std::rotate(renderer.material_slots.begin() +
                        static_cast<std::ptrdiff_t>(move_request.source),
                        renderer.material_slots.begin() +
                        static_cast<std::ptrdiff_t>(move_request.source + 1),
                        renderer.material_slots.begin() +
                        static_cast<std::ptrdiff_t>(move_request.destination + 1));
                }
                else
                {
                    std::rotate(renderer.material_slots.begin() +
                        static_cast<std::ptrdiff_t>(move_request.destination),
                        renderer.material_slots.begin() +
                        static_cast<std::ptrdiff_t>(move_request.source),
                        renderer.material_slots.begin() +
                        static_cast<std::ptrdiff_t>(move_request.source + 1));
                }
                renderer.OnPropertyChanged(nullptr);
                context.CommitEdit();
                context.SetStatus("マテリアルスロットの順序を変更しました");
            }
            else context.CancelEdit();
        }
        ImGui::PopID();
    }
}

// 地形をモデルから作る入口。欄は PropertyRegistry のアセット欄をそのまま使う。
void framework::draw_landscape_model_inspector(ReplayEngine::Core::Component& component)
{
    auto* landscape = dynamic_cast<ReplayEngine::Components::LandscapeComponent*>(&component);
    if (landscape == nullptr) return;
    ReplayEngine::Core::GameObject* object = component.Owner();
    if (object == nullptr || !object_editor_context.CanEdit()) return;

    if (landscape->source_model_asset.empty())
    {
        ImGui::TextDisabled(u8"作成元モデルを選ぶと、その形で地形を作り直せます。");
        return;
    }
    if (ImGui::Button(u8"このモデルで地形を作り直す"))
    {
        std::string message;
        object_editor_context.BeginEdit(u8"モデルから地形を作る");
        if (build_landscape_from_model(*object, landscape->source_model_asset, message))
        {
            object_editor_context.CommitEdit();
            landscape_selected_face = -1;
        }
        else object_editor_context.CancelEdit();
        object_editor_context.SetStatus(message);
    }
    ImGui::SameLine();
    ImGui::TextDisabled(u8"今の形は消えます。骨付きモデルは使えません。");
}
void framework::draw_material_slot_inspector()
{
    using namespace ReplayEngine::Components;
    ReplayEngine::Scene::Scene* scene = object_editor_context.GetScene();
    if (scene == nullptr || object_editor_context.Selection().Count() != 1) return;
    ReplayEngine::Core::GameObject* object =
        object_editor_context.Selection().ResolvePrimary(*scene);
    if (object == nullptr) return;

    if (auto* renderer = object->GetComponent<MeshRendererComponent>())
    {
        if (!renderer->mesh_asset.empty())
        {
            if (skinned_mesh* mesh_asset = resolve_object_mesh(renderer->mesh_asset))
            {
                const std::vector<std::string> names = material_subset_names(*mesh_asset);
                draw_material_slot_rows(object_editor_context, *renderer, names,
                    u8"Mesh Renderer マテリアルスロット", true,
                    [this](const std::filesystem::path& texture_path)
                    { return dx12_device_context.ImGuiTextureForPath(texture_path); },
                    ui_texture_preview_scale);
            }
        }
    }
    if (auto* renderer = object->GetComponent<SkinnedMeshRendererComponent>())
    {
        if (!renderer->mesh_asset.empty())
        {
            if (skinned_mesh* mesh_asset = resolve_object_mesh(renderer->mesh_asset))
            {
                const std::vector<std::string> names = material_subset_names(*mesh_asset);
                draw_material_slot_rows(object_editor_context, *renderer, names,
                    u8"Skinned Mesh Renderer マテリアルスロット", true,
                    [this](const std::filesystem::path& texture_path)
                    { return dx12_device_context.ImGuiTextureForPath(texture_path); },
                    ui_texture_preview_scale);
            }
        }
    }
    if (auto* renderer = object->GetComponent<PrimitiveMeshRendererComponent>())
    {
        const std::vector<std::string> names(1);
        draw_material_slot_rows(object_editor_context, *renderer, names,
            u8"Primitive Mesh Renderer マテリアルスロット", false,
            [this](const std::filesystem::path& texture_path)
            { return dx12_device_context.ImGuiTextureForPath(texture_path); },
            ui_texture_preview_scale);
    }
}

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
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void framework::draw_inspector()
{
    REPLAY_PROFILE_SCOPE("Editor/Inspector");
    ReplayEngine::Editor::PanelTabColorScope panel_tab_color("Editor");
    ImGui::Begin("インスペクター");
    project_settings_file_undo_enabled = false;

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
        ImGui::ColorEdit4("Scene / Game 背景色", &background_color.x);
        ImGui::Checkbox("背景画像", &draw_background_image);
        ImGui::Checkbox("ゲームシーン", &enable_scene_game);
        ImGui::Checkbox("パーティクル", &enable_particles);
        ImGui::Checkbox("軌跡", &enable_trail);
        ImGui::Separator();
        draw_project_settings_panel();
        ImGui::Separator();
        draw_editor_camera_gate_diagnostics();
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
    {
        // GameObject / Component の編集は専用パネルへ委譲する。
        // ここに Component 型ごとの分岐は書かない。
        // 表示内容は ComponentRegistry と PropertyRegistry から自動生成される。
        bool show_game_template_components =
            project_settings.ShowGameTemplateComponents();
        object_inspector_panel.SetComponentExtraDrawer(
            [this](ReplayEngine::Editor::EditorContext&, ReplayEngine::Core::Component& component)
            { draw_landscape_model_inspector(component); });
        if (object_inspector_panel.DrawContents(object_editor_context,
            show_game_template_components))
        {
            project_settings.SetShowGameTemplateComponents(
                show_game_template_components);
            save_project_settings();
        }
        draw_material_slot_inspector();
        break;
    }

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
            ImGui::DragFloat("CSM Bias (m)", &csm.constants.params.x, 0.002f, 0, 0.5f, "%.3f");
            ImGui::DragFloat("Normal Bias", &csm.constants.params.y, 0.005f, 0, 1);
            ImGui::DragFloat("Filter", &csm.constants.params.z, 0.05f, 0, 8);
            // params.w は毎フレーム作り直されるので、UI はユーザー設定側を触る。
            ImGui::Checkbox("Enable CSM", &csm_enabled_setting);
            if (!(csm.constants.params.w > 0.5f) && csm_enabled_setting)
            {
                ImGui::TextDisabled("影を落とす Directional Light が無いため停止中");
            }
            ImGui::TextDisabled("バイアス・濃さ・最遠距離は Directional Light の");
            ImGui::TextDisabled("Inspector が正本です (Light がある間は上書きされます)");
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
        ReplayEngine::Editor::EditorHelp::Item("button.inspector.open_shader_adjustments",
            u8"マテリアルとシェーダーの調整テーブルを開きます。");
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
            ReplayEngine::Editor::EditorHelp::Item("control.rendering.outline_pass",
                u8"輪郭線レイヤを持つマテリアルの追加パスを描くか");
            ImGui::Checkbox("PBR影パス", &enable_pbr_shadow_shader);
            ReplayEngine::Editor::EditorHelp::Item("control.rendering.pbr_shadow_pass",
                u8"PBR の影用の追加パスを描くか");
        }
        {
            int output = render_graph.OutputIndex();
            if (ImGui::Combo(u8"描画出力 (Ctrl+F2)", &output, ReplayEngine::Rendering::RenderGraph::Names()))
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
