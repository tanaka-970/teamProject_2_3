#include "framework.h"
#include "gltf_model.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Components/Physics/MeshColliderComponent.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Object/GameObject/GameObject.h"
#include "../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"

#include <commdlg.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <system_error>

namespace
{
    std::string WideToUtf8(const std::wstring& text)
    {
        if (text.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) return {};
        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring LowerExtension(const std::filesystem::path& path)
    {
        std::wstring extension = path.extension().wstring();
        for (wchar_t& character : extension)
        {
            character = static_cast<wchar_t>(towlower(character));
        }
        return extension;
    }

    bool CopyGltfDependencies(const std::filesystem::path& source,
        const std::filesystem::path& destination_folder, std::error_code& error)
    {
        std::ifstream stream(source, std::ios::binary);
        if (!stream) return false;
        const std::string json((std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
        const std::regex uri_expression(R"re("uri"\s*:\s*"([^"]+)")re");
        for (std::sregex_iterator current(json.begin(), json.end(), uri_expression), end;
            current != end; ++current)
        {
            const std::string uri = (*current)[1].str();
            if (uri.rfind("data:", 0) == 0 || uri.find("://") != std::string::npos) continue;
            const auto relative = std::filesystem::u8path(uri).lexically_normal();
            if (relative.empty() || relative.is_absolute() || relative.generic_string().rfind("../", 0) == 0)
                continue;
            const auto dependency_source = source.parent_path() / relative;
            const auto dependency_destination = destination_folder / relative;
            std::filesystem::create_directories(dependency_destination.parent_path(), error);
            if (error) return false;
            std::filesystem::copy_file(dependency_source, dependency_destination,
                std::filesystem::copy_options::overwrite_existing, error);
            if (error) return false;
        }
        return true;
    }
}

bool framework::place_asset_in_object_scene(const ReplayEngine::Assets::AssetRecord& asset,
    bool add_mesh_collider)
{
    if (object_scene_play_mode)
    {
        object_editor_context.SetStatus("Play中はAssetを配置できません");
        return false;
    }

    const std::wstring extension = LowerExtension(asset.source_path);
    if (extension == L".replayprefab")
    {
        object_editor_context.BeginEdit("Prefabを配置");
        std::string error;
        ReplayEngine::Scene::Serialization::SceneLoadReport report;
        const ReplayEngine::Core::ObjectID root =
            ReplayEngine::Scene::Serialization::PrefabSerializer::Instantiate(
                object_scene, asset.source_path, error, &report);
        if (!root.Valid())
        {
            object_editor_context.CancelEdit();
            object_editor_context.SetStatus("Prefab配置失敗: " + error);
            return false;
        }
        object_editor_context.CommitEdit();
        object_editor_context.Selection().Select(root, false);
        selected_editor_object = editor_selection::game_object;
        object_editor_context.SetStatus("Prefabを配置しました: " + asset.display_name);
        return true;
    }

    if (asset.kind != ReplayEngine::Assets::AssetKind::Model)
    {
        object_editor_context.SetStatus("このAsset TypeはScene Viewへ配置できません");
        return false;
    }

    object_editor_context.BeginEdit("Mesh Assetを配置");
    ReplayEngine::Core::GameObject* object = object_scene.CreateGameObject(
        asset.display_name.empty() ? asset.source_path.stem().u8string() : asset.display_name);
    if (object == nullptr)
    {
        object_editor_context.CancelEdit();
        object_editor_context.SetStatus("GameObjectを作成できませんでした");
        return false;
    }

    auto* renderer = object->AddComponent<ReplayEngine::Components::MeshRendererComponent>();
    if (renderer == nullptr)
    {
        object_scene.DestroyGameObject(object);
        object_scene.ProcessPendingOperations();
        object_editor_context.CancelEdit();
        object_editor_context.SetStatus("Mesh Rendererを追加できませんでした");
        return false;
    }
    renderer->mesh_asset = asset.guid;

    const DirectX::XMFLOAT3 eye = editor_camera.Position();
    const DirectX::XMFLOAT3 forward = editor_camera.Forward();
    DirectX::XMFLOAT3 position{
        eye.x + forward.x * 5.0f,
        eye.y + forward.y * 5.0f,
        eye.z + forward.z * 5.0f
    };
    if (transform_gizmo.SnapEnabled())
    {
        const float step = transform_gizmo.SnapStep();
        position.x = std::round(position.x / step) * step;
        position.y = std::round(position.y / step) * step;
        position.z = std::round(position.z / step) * step;
    }
    object->GetTransform().SetLocalPosition(position);

    if (add_mesh_collider)
    {
        auto* collider = object->AddComponent<ReplayEngine::Components::MeshColliderComponent>();
        if (collider != nullptr)
        {
            collider->mesh_source =
                ReplayEngine::Components::MeshColliderComponent::MeshSource_Renderer;
            collider->collision_layer = ReplayEngine::Physics::CollisionLayers::Environment;
            collider->is_trigger = false;
        }
    }

    object_editor_context.CommitEdit();
    object_editor_context.Selection().Select(object->ID(), false);
    selected_editor_object = editor_selection::game_object;
    object_editor_context.SetStatus("Assetを配置しました: " + asset.display_name);
    return true;
}

bool framework::browse_stage_asset()
{
    wchar_t filename[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(_countof(filename));
    dialog.lpstrFilter =
        L"対応モデル (*.fbx;*.cereal;*.glb;*.gltf)\0*.fbx;*.cereal;*.glb;*.gltf\0"
        L"FBXキャッシュ (*.fbx;*.cereal)\0*.fbx;*.cereal\0"
        L"glTF 2.0 (*.glb;*.gltf)\0*.glb;*.gltf\0"
        L"すべてのファイル (*.*)\0*.*\0\0";
    dialog.nFilterIndex = 1;
    dialog.lpstrTitle = L"配置するモデルを選択";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&dialog)) return false;
    return load_stage_asset(filename);
}

bool framework::prewarm_model_asset(const std::filesystem::path& path)
{
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error)) return false;

    const std::wstring extension = LowerExtension(path);
    try
    {
        if (extension == L".glb" || extension == L".gltf")
        {
            const auto model = gltf_model_cache.Load(path, [this, path]
            {
                return std::make_shared<gltf_model>(device.Get(), path.string());
            });
            return model && model->IsLoaded();
        }
        if (extension == L".fbx" || extension == L".cereal")
        {
            // .fbxはランタイム変換を避け、隣の.cerealキャッシュがある場合だけ載せる。
            auto cache = path;
            cache.replace_extension(L".cereal");
            if (!std::filesystem::exists(cache, error)) return false;
            return skinned_mesh_cache.Load(path, [this, path]
            {
                return std::make_shared<skinned_mesh>(device.Get(), path.string().c_str());
            }) != nullptr;
        }
    }
    catch (...)
    {
        // 先読みは失敗しても本来のロードで再試行できるため、ここで握り潰す。
        return false;
    }
    return false;
}

bool framework::load_stage_asset(const std::wstring& filename)
{
    const std::filesystem::path path(filename);
    async_stage_load_active = true;
    stage_asset_status = "モデルを並列ロードしています...";
    async_asset_manager.QueueTask(path, ReplayEngine::Assets::AssetKind::Model,
        [this, path](ReplayEngine::Assets::AsyncAssetResult& result)
        {
            const std::wstring extension = LowerExtension(path);
            if (extension == L".glb" || extension == L".gltf")
            {
                auto candidate = gltf_model_cache.Load(path, [this, path]
                {
                    return std::make_shared<gltf_model>(device.Get(), path.string());
                });
                if (!candidate || !candidate->IsLoaded())
                    result.error = candidate ? candidate->Error() : "glTFモデルを生成できません";
                return;
            }
            if (extension == L".fbx" || extension == L".cereal")
            {
                auto cache = path;
                cache.replace_extension(L".cereal");
                if (!std::filesystem::exists(cache))
                {
                    result.error = "同じ場所に実行用.cerealキャッシュが必要です";
                    return;
                }
                skinned_mesh_cache.Load(path, [this, path]
                {
                    return std::make_shared<skinned_mesh>(device.Get(), path.string().c_str());
                });
                return;
            }
            result.error = "未対応のモデル形式です";
        },
        [this](ReplayEngine::Assets::AsyncAssetResult&& result)
        {
            async_stage_load_active = false;
            if (!result.Succeeded())
            {
                stage_asset_status = "読み込み失敗: " + result.error;
                return;
            }
            load_stage_asset_now(result.path.wstring());
        });
    return true;
}

bool framework::load_stage_asset_now(const std::wstring& filename)
{
    const std::filesystem::path path(filename);
    const std::wstring extension = LowerExtension(path);
    selected_stage_asset_path = WideToUtf8(path.wstring());

    try
    {
        if (extension == L".glb" || extension == L".gltf")
        {
            auto candidate = gltf_model_cache.Load(path, [this, path]
            {
                return std::make_shared<gltf_model>(device.Get(), path.string());
            });
            if (!candidate->IsLoaded())
            {
                stage_asset_status = "読み込み失敗: " + candidate->Error();
                return false;
            }

            stage_gltf_model = std::move(candidate);
            if (game_scene) game_scene->Gameplay().GetStage().SetModel(nullptr);
            outline_per_stage = false;
            stage_asset_status = "glTF素材を読み込みました（配置プレビュー中）";
        }
        else if (extension == L".fbx" || extension == L".cereal")
        {
            std::filesystem::path cache = path;
            cache.replace_extension(L".cereal");
            if (!std::filesystem::exists(cache))
            {
                stage_asset_status = "同じ場所に実行用.cerealキャッシュが必要です";
                return false;
            }

            auto candidate = skinned_mesh_cache.Load(path, [this, path]
            {
                return std::make_shared<skinned_mesh>(device.Get(), path.string().c_str());
            });
            skinned_meshes[1] = std::move(candidate);
            stage_gltf_model.reset();
            if (game_scene) game_scene->Gameplay().GetStage().SetModel(skinned_meshes[1].get());
            stage_asset_status = "FBXキャッシュ素材を読み込みました（配置プレビュー中）";
        }
        else
        {
            stage_asset_status = "未対応のモデル形式です";
            return false;
        }
    }
    catch (const std::exception& exception)
    {
        stage_asset_status = "読み込み失敗: ";
        stage_asset_status += exception.what();
        return false;
    }
    catch (...)
    {
        stage_asset_status = "モデル読み込み中に不明なエラーが発生しました";
        return false;
    }

    ReplayEngine::Assets::AssetCache cache;
    ReplayEngine::Assets::AssetCacheEntry cache_entry{};
    std::string cache_error;
    if (cache.StoreSourceFile(path, ReplayEngine::Assets::AssetKind::Model,
        cache_entry, cache_error))
    {
        selected_stage_cache_path = WideToUtf8(cache_entry.cache_path.wstring());
        std::filesystem::path registered_source = path;
        const auto normalized_source = ReplayEngine::Assets::AssetDatabase::NormalizeProjectPath(path);
        if (normalized_source.is_absolute())
        {
            const auto& temporary_record = asset_database.Register(path,
                ReplayEngine::Assets::AssetKind::Model, cache_entry.cache_path);
            const std::string temporary_guid = temporary_record.guid;
            const auto imported_folder = std::filesystem::path("resources") / "Imported" /
                temporary_guid.substr(0, 8);
            std::error_code import_error;
            std::filesystem::create_directories(imported_folder, import_error);
            registered_source = imported_folder / path.filename();
            std::filesystem::copy_file(path, registered_source,
                std::filesystem::copy_options::overwrite_existing, import_error);
            if (!import_error && extension == L".gltf")
                CopyGltfDependencies(path, imported_folder, import_error);
            if (!import_error && (extension == L".fbx" || extension == L".cereal"))
            {
                auto source_cereal = path;
                source_cereal.replace_extension(L".cereal");
                auto imported_cereal = registered_source;
                imported_cereal.replace_extension(L".cereal");
                if (std::filesystem::exists(source_cereal))
                    std::filesystem::copy_file(source_cereal, imported_cereal,
                        std::filesystem::copy_options::overwrite_existing, import_error);
            }
            if (import_error)
            {
                registered_source = path;
                stage_asset_status += " / resourcesへの取込失敗";
            }
            else
            {
                asset_database.Remove(temporary_guid);
                selected_stage_asset_path = WideToUtf8(registered_source.wstring());
                stage_asset_status += " / resourcesへ取込済み";
            }
        }
        const auto& record = asset_database.Register(registered_source,
            ReplayEngine::Assets::AssetKind::Model, cache_entry.cache_path);
        selected_stage_asset_guid = record.guid;
        std::string database_error;
        asset_database.Save(database_error);
        stage_asset_status += " / 共通キャッシュ作成済み";
    }
    else
    {
        selected_stage_cache_path.clear();
        stage_asset_status += " / キャッシュ失敗: " + cache_error;
    }

    enable_stage_render = true;
    stage_asset_placed = false;
    active_stage_placement_id = 0;
    set_editor_workspace(editor_workspace::placement);
    selected_editor_object = editor_selection::stage;
    return true;
}

void framework::draw_stage_placement_controls()
{
    const bool has_asset = skinned_meshes[1] != nullptr || stage_gltf_model != nullptr;
    if (active_editor_workspace != editor_workspace::placement)
    {
        ImGui::TextDisabled("配置操作は配置モードのテーブルで行います");
        if (ImGui::Button("配置モードへ切り替える"))
        {
            set_editor_workspace(editor_workspace::placement);
        }
        return;
    }

    ImGui::TextColored({ 0.35f, 0.75f, 1.0f, 1.0f }, "オブジェクト配置モード");
    ImGui::TextDisabled("モデルごとにEntityと識別子を作り、シーンへ記録します。");
    if (ImGui::Button("別のモデルを取り込む...")) browse_stage_asset();
    ImGui::SameLine();
    if (ImGui::Button("Prefabを配置...")) load_prefab();

    if (ImGui::CollapsingHeader("配置済みオブジェクト", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const auto& entity : editor_scene_document.Entities())
        {
            if (!entity.transform) continue;
            ImGui::PushID(static_cast<int>(entity.id));
            const bool selected = entity.id == selected_scene_entity_id;
            const std::string label = entity.name + "  [" + entity.identifier + "]";
            if (ImGui::Selectable(label.c_str(), selected)) select_scene_entity(entity.id, ImGui::GetIO().KeyShift);
            ImGui::PopID();
        }
        if (!selected_scene_entity_ids.empty())
        {
            if (ImGui::Button("選択を複製  Ctrl+D")) duplicate_selected_entities();
            ImGui::SameLine();
            ImGui::TextDisabled("Shiftで複数選択");
        }
    }

    if (!has_asset)
    {
        ImGui::TextDisabled("配置するモデルが選択されていません");
        if (ImGui::Button("モデルファイルを選択...")) browse_stage_asset();
        return;
    }

    if (game_scene) game_scene->Gameplay().DrawStageGUI();
    if (stage_asset_placed && active_stage_placement_id != 0) sync_selected_entity_to_stage();
    if (!stage_asset_placed)
    {
        ImGui::TextDisabled("現在はプレビューです。衝突対象にはなりません");
        if (ImGui::Button("この位置へ配置を確定"))
        {
            const auto before = editor_scene_document;
            const auto* asset_record = asset_database.FindByGuid(selected_stage_asset_guid);
            const std::string asset_name = asset_record && !asset_record->display_name.empty()
                ? asset_record->display_name
                : std::filesystem::path(selected_stage_asset_path).stem().u8string();
            auto& entity = editor_scene_document.CreateEntity(asset_name);
            entity.transform.emplace();
            if (game_scene)
            {
                const Stage& stage = game_scene->Gameplay().GetStage();
                entity.transform->position = stage.GetPosition();
                entity.transform->rotation = stage.GetAngle();
                entity.transform->scale = stage.GetScale();
            }
            entity.model_renderer.emplace();
            entity.model_renderer->asset_guid = selected_stage_asset_guid;
            entity.model_renderer->asset_name = asset_name;
            entity.model_renderer->shading_model = shading_per_stage;
            entity.model_renderer->outline = outline_per_stage;
            store_stage_shader_layers(*entity.model_renderer);
            active_stage_placement_id = entity.id;
            select_scene_entity(entity.id, false);
            scene_undo_stack.Commit("モデルを配置", before, editor_scene_document);
            stage_asset_placed = true;
            enable_stage_render = true;
            stage_asset_status = "シーンへオブジェクトを配置済み: " + entity.name +
                " [" + entity.identifier + "]";
            selected_editor_object = editor_selection::scene_entity;
        }
    }
    else
    {
        ImGui::TextColored({ 0.45f, 0.85f, 0.55f, 1.0f }, "シーンへ配置済み");
        if (ImGui::Button("同じモデルを続けて配置"))
        {
            active_stage_placement_id = 0;
            selected_scene_entity_id = 0;
            selected_scene_entity_ids.clear();
            stage_asset_placed = false;
            selected_editor_object = editor_selection::stage;
            stage_asset_status = "同じモデルを配置プレビュー中";
        }
        ImGui::SameLine();
        if (ImGui::Button("配置確定を解除してプレビューへ戻す"))
        {
            const auto before = editor_scene_document;
            editor_scene_document.DestroyEntity(active_stage_placement_id);
            scene_undo_stack.Commit("配置を削除", before, editor_scene_document);
            active_stage_placement_id = 0;
            selected_scene_entity_id = 0;
            selected_scene_entity_ids.clear();
            stage_asset_placed = false;
            stage_asset_status = "配置プレビュー中";
        }
    }
}
