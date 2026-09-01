#include "framework.h"
#include "gltf_model.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Components/Physics/MeshColliderComponent.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../RePlayEngine/Editor/ShaderEditing/MaterialShaderInspector.h"
#include "../../RePlayEngine/Editor/ShaderEditing/ShaderStackEditor.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../RePlayEngine/Scripting/Core/ScriptComponent.h"
#include "../../RePlayEngine/Scripting/Core/ScriptRuntime.h"
#include "../../RePlayEngine/Scripting/Core/ScriptTypeCatalog.h"

#include <commdlg.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <system_error>
namespace
{
    std::string SafeAssetFileName(std::string name)
    {
        for (char& character : name)
        {
            if (character == '<' || character == '>' || character == ':' ||
                character == '"' || character == '/' || character == '\\' ||
                character == '|' || character == '?' || character == '*') character = '_';
        }
        return name.empty() ? "NewMaterial" : name;
    }
}

void framework::begin_material_reorder_history()
{
    if (material_editor_loaded)
        material_editor_history.Begin(material_editor_asset, "Shader Layer を並べ替え");
}

void framework::capture_material_reorder_history(void* owner)
{
    if (owner != nullptr)
        static_cast<framework*>(owner)->begin_material_reorder_history();
}

void framework::refresh_material_editor_preview()
{
    if (!material_editor_loaded || material_editor_guid.empty() ||
        !material_editor_write_time_valid) return;
    cached_material_asset preview;
    preview.material = material_editor_asset;
    preview.write_time = material_editor_write_time;
    object_material_cache.insert_or_assign(material_editor_guid, std::move(preview));
    object_material_failures.erase(material_editor_guid);
}

bool framework::undo_material_editor()
{
    std::string label;
    if (!material_editor_history.Undo(material_editor_asset, label)) return false;
    material_editor_dirty = true;
    refresh_material_editor_preview();
    material_editor_status = "Undo: " + label;
    return true;
}

bool framework::redo_material_editor()
{
    std::string label;
    if (!material_editor_history.Redo(material_editor_asset, label)) return false;
    material_editor_dirty = true;
    refresh_material_editor_preview();
    material_editor_status = "Redo: " + label;
    return true;
}

bool framework::create_material_asset()
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Rendering::MaterialAsset;

    std::filesystem::path folder = std::filesystem::path("resources") / "Materials";
    const std::string base = SafeAssetFileName(new_material_name);
    std::filesystem::path path = folder / (base + MaterialAsset::file_extension);
    for (int suffix = 1; std::filesystem::exists(path) && suffix < 10000; ++suffix)
        path = folder / (base + " (" + std::to_string(suffix) + ")" + MaterialAsset::file_extension);

    MaterialAsset material;
    std::string error;
    if (!MaterialAsset::Save(material, path, error))
    {
        material_editor_status = "Material作成失敗: " + error;
        return false;
    }

    const auto& record = asset_database.Register(path, AssetKind::Material);
    if (!asset_database.Save(error))
    {
        material_editor_status = "Materialは作成しましたがDB保存失敗: " + error;
        return false;
    }
    selected_asset_guid = record.guid;
    material_editor_guid.clear();
    return load_material_editor(record);
}

bool framework::load_material_editor(const ReplayEngine::Assets::AssetRecord& asset)
{
    if (asset.kind != ReplayEngine::Assets::AssetKind::Material) return false;
    std::string error;
    ReplayEngine::Rendering::MaterialAsset loaded;
    if (!ReplayEngine::Rendering::MaterialAsset::Load(asset.source_path, loaded, error))
    {
        material_editor_loaded = false;
        material_editor_status = "Material読込失敗: " + error;
        return false;
    }
    material_editor_asset = std::move(loaded);
    material_editor_guid = asset.guid;
    material_editor_loaded = true;
    material_editor_dirty = false;
    std::error_code write_time_error;
    material_editor_write_time = std::filesystem::last_write_time(
        asset.source_path, write_time_error);
    material_editor_write_time_valid = !write_time_error;
    material_editor_history.Clear();
    material_editor_status = "Materialを読み込みました: " + asset.display_name;
    return true;
}

bool framework::save_material_editor()
{
    if (!material_editor_loaded) return false;
    const ReplayEngine::Assets::AssetRecord* asset =
        asset_database.FindByGuid(material_editor_guid);
    if (asset == nullptr || asset->kind != ReplayEngine::Assets::AssetKind::Material)
    {
        material_editor_status = "MaterialのAssetDatabase登録が見つかりません";
        return false;
    }

    // Schema-driven Inspector は PropertyBag を正として編集する。
    // Phase 6/12 の旧互換 field へも同期してから保存し、
    // fallback 経路と新経路のどちらでも同じ見た目になるようにする。
    material_editor_asset.SyncPropertiesToLegacyFields();

    std::string error;
    if (!ReplayEngine::Rendering::MaterialAsset::Save(
        material_editor_asset, asset->source_path, error))
    {
        material_editor_status = "Material保存失敗: " + error;
        return false;
    }
    object_material_cache.erase(material_editor_guid);
    object_material_failures.erase(material_editor_guid);
    std::error_code write_time_error;
    material_editor_write_time = std::filesystem::last_write_time(
        asset->source_path, write_time_error);
    material_editor_write_time_valid = !write_time_error;
    material_editor_status = "MaterialをAtomic Saveしました: " +
        asset->source_path.generic_u8string();
    material_editor_dirty = false;
    return true;
}

void framework::draw_material_asset_editor()
{
    const ReplayEngine::Assets::AssetRecord* selected = selected_asset_guid.empty()
        ? nullptr : asset_database.FindByGuid(selected_asset_guid);
    if (selected == nullptr || selected->kind != ReplayEngine::Assets::AssetKind::Material)
        return;

    // 別 Material へ移るとき未保存値を黙って捨てない。
    // Unity の Asset 編集に近く、選択変更を「保存確認の罠」にしない。
    if (material_editor_loaded && material_editor_guid != selected->guid &&
        material_editor_dirty)
    {
        if (!save_material_editor()) return;
    }
    if (!material_editor_loaded || material_editor_guid != selected->guid)
        load_material_editor(*selected);
    if (!material_editor_loaded) return;

    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Material Asset", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextDisabled("%s", selected->source_path.generic_u8string().c_str());

    // ---------------------------------------------------------------------
    // Phase 7: Shader GUID + PropertySchema が Inspector の正本。
    //
    // PBR / Toon / Unlit / Pixelate / Project Shader を同じ Picker から選ぶ。
    // Shader 固有 UI はここへ hard-code せず、#pragma property の Schema から
    // MaterialShaderInspector が自動生成する。
    // ---------------------------------------------------------------------
    const std::string inspector_id = "material_schema_" + selected->guid;
    const auto inspector = ReplayEngine::Editor::MaterialShaderInspector::Draw(
        inspector_id.c_str(), material_editor_asset,
        shader_library.Catalog(), asset_database);
    if (inspector.changed)
    {
        material_editor_dirty = true;
        refresh_material_editor_preview();
    }

    // ---------------------------------------------------------------------
    // 既存の Layer Stack は Phase 16 の Asset-driven Stack へ移行するまで保持。
    // Base Shader の選択欄だけ隠し、上の GUID Picker と二重管理しない。
    // ---------------------------------------------------------------------
    ImGui::Separator();
    if (ImGui::TreeNodeEx("Shader Stack", 0))
    {
        const bool was_active = ImGui::IsAnyItemActive();
        bool material_outline_bridge = material_editor_asset.layers.Contains(
            ReplayEngine::Rendering::BuiltInShaderLayers::Outline);
        const auto stack = ReplayEngine::Editor::ShaderStackEditor::Draw(
            ("material_stack_" + selected->guid).c_str(),
            material_editor_asset.shading_model,
            material_outline_bridge,
            material_editor_asset.layers,
            shader_stack_advanced_mode,
            toon.outline.outline_color,
            toon.outline.outline_params,
            material_editor_asset.pixelate_grid,
            material_editor_asset.pixelate_strength,
            false, &shader_library.Catalog(), &asset_database,
            &framework::capture_material_reorder_history, this);

        if (stack.requires_pbr)     use_pbr_skin = true;
        if (stack.requires_toon)    enable_toon_shader = true;
        if (stack.requires_unlit)   enable_unlit_shader = true;
        if (stack.requires_outline) enable_outline_shader = true;

        // Layer 追加/削除/並べ替え/Property 編集は Editor 自身が changed を返す。
        // 古い ImGui の操作中判定も fallback として残す。
        if (stack.changed || ImGui::IsAnyItemActive() || was_active)
            material_editor_dirty = true;
        if (stack.reordered && material_editor_history.InTransaction())
        {
            material_editor_history.Commit(material_editor_asset);
            refresh_material_editor_preview();
        }
        else if (stack.changed)
        {
            refresh_material_editor_preview();
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
    const char* save_label = material_editor_dirty ? "Save Material *" : "Save Material";
    if (ImGui::Button(save_label)) save_material_editor();
    ReplayEngine::Editor::EditorHelp::Item("button.material.save",
        u8"編集中の Material Asset を保存します。アスタリスクは未保存の変更を示します。");
    ImGui::SameLine();
    if (ImGui::Button("Assign to Selected Renderer"))
        place_asset_in_object_scene(*selected, false);
    ReplayEngine::Editor::EditorHelp::Item("button.material.assign_renderer",
        u8"編集中の Material Asset を選択中 Renderer へ割り当てます。");
    ImGui::SameLine();
    ImGui::TextDisabled(material_editor_dirty ? "未保存" : "保存済み");

    ImGui::TextDisabled("%s", material_editor_status.c_str());
}
