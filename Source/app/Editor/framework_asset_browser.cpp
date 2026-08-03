#include "framework.h"
#include "gltf_model.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Components/Physics/MeshColliderComponent.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
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

bool framework::place_asset_in_object_scene(const ReplayEngine::Assets::AssetRecord& asset,
    bool add_mesh_collider)
{
    if (object_scene_play_mode)
    {
        object_editor_context.SetStatus("Play中はAssetを配置できません");
        return false;
    }

    if (asset.kind == ReplayEngine::Assets::AssetKind::Material)
    {
        ReplayEngine::Core::GameObject* target =
            object_editor_context.Selection().ResolvePrimary(object_scene);
        if (target == nullptr)
        {
            object_editor_context.SetStatus(
                "Materialを割り当てるGameObjectを選択してください");
            return false;
        }

        auto* mesh = target->GetComponent<ReplayEngine::Components::MeshRendererComponent>();
        auto* skinned = target->GetComponent<
            ReplayEngine::Components::SkinnedMeshRendererComponent>();
        if (mesh == nullptr && skinned == nullptr)
        {
            object_editor_context.SetStatus(
                "選択GameObjectにMesh Rendererがありません");
            return false;
        }

        object_editor_context.BeginEdit("Material Assetを割り当て");
        if (mesh != nullptr)
        {
            mesh->material_asset = asset.guid;
            mesh->material_override = false;
        }
        if (skinned != nullptr)
        {
            skinned->material_asset = asset.guid;
            skinned->material_override = false;
        }
        object_editor_context.CommitEdit();
        selected_editor_object = editor_selection::game_object;
        object_editor_context.SetStatus("Materialを割り当てました: " + asset.display_name);
        return true;
    }

    // .cs を GameObject へ渡すと Script Component を足す。
    // Add Component の Scripts/C# から選ぶのと同じ経路で、
    // AssignScriptType が Type GUID / Class 名 / Asset GUID をまとめて入れる。
    // ここで生の文字列を組み立てないこと。
    if (asset.kind == ReplayEngine::Assets::AssetKind::Script)
    {
        namespace Scripting = ReplayEngine::Scripting;

        ReplayEngine::Core::GameObject* target =
            object_editor_context.Selection().ResolvePrimary(object_scene);
        if (target == nullptr)
        {
            object_editor_context.SetStatus(
                "Scriptを付けるGameObjectを選択してください");
            return false;
        }
        if (!object_script_runtime)
        {
            object_editor_context.SetStatus("ScriptRuntimeが初期化されていません");
            return false;
        }

        const Scripting::ScriptTypeDescriptor* descriptor = nullptr;
        for (const Scripting::ScriptTypeDescriptor& candidate :
            object_script_runtime->Catalog().All())
        {
            if (candidate.asset_guid == asset.guid)
            {
                descriptor = &candidate;
                break;
            }
        }
        if (descriptor == nullptr)
        {
            object_editor_context.SetStatus(
                "Script Typeが見つかりません。Refresh C# Catalogを実行してください");
            return false;
        }

        object_editor_context.BeginEdit(descriptor->DisplayName() + " を追加");
        ReplayEngine::Core::Component* component =
            target->AddComponent(Scripting::ScriptComponent::StaticTypeID());
        Scripting::ScriptComponent* script_component = component != nullptr
            ? Scripting::ScriptComponent::From(*component) : nullptr;
        if (script_component == nullptr)
        {
            object_editor_context.CancelEdit();
            object_editor_context.SetStatus(
                descriptor->DisplayName() + " を追加できませんでした");
            return false;
        }
        script_component->AssignScriptType(*descriptor);
        object_editor_context.CommitEdit();
        selected_editor_object = editor_selection::game_object;
        object_editor_context.SetStatus(
            descriptor->DisplayName() + " を追加しました");
        return true;
    }

    const std::wstring extension = LowerExtension(asset.source_path);
    if (extension == L".replayprefab")
    {
        object_editor_context.BeginEdit("Prefabを配置");
        std::string error;
        ReplayEngine::Scene::Serialization::SceneLoadReport report;
        const ReplayEngine::Core::ObjectID root =
            ReplayEngine::Scene::Serialization::PrefabSerializer::Instantiate(
                object_scene, asset.source_path, error, &report, asset.guid);
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

bool framework::create_material_asset()
{
    using ReplayEngine::Assets::AssetKind;
    using ReplayEngine::Rendering::MaterialAsset;

    std::filesystem::path folder = std::filesystem::path("resources") / "Materials";
    const std::string base = SafeAssetFileName(new_material_name);
    std::filesystem::path path = folder / (base + MaterialAsset::file_extension);
    for (int suffix = 2; std::filesystem::exists(path) && suffix < 10000; ++suffix)
        path = folder / (base + "_" + std::to_string(suffix) + MaterialAsset::file_extension);

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

    std::string error;
    if (!ReplayEngine::Rendering::MaterialAsset::Save(
        material_editor_asset, asset->source_path, error))
    {
        material_editor_status = "Material保存失敗: " + error;
        return false;
    }
    object_material_cache.erase(material_editor_guid);
    object_material_failures.erase(material_editor_guid);
    material_editor_status = "MaterialをAtomic Saveしました: " +
        asset->source_path.generic_u8string();
    return true;
}

void framework::draw_material_asset_editor()
{
    const ReplayEngine::Assets::AssetRecord* selected = selected_asset_guid.empty()
        ? nullptr : asset_database.FindByGuid(selected_asset_guid);
    if (selected == nullptr || selected->kind != ReplayEngine::Assets::AssetKind::Material)
        return;
    if (!material_editor_loaded || material_editor_guid != selected->guid)
        load_material_editor(*selected);
    if (!material_editor_loaded) return;

    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Material Asset", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextDisabled("%s", selected->source_path.generic_u8string().c_str());
    ImGui::ColorEdit4("Base Color", &material_editor_asset.base_color.x);
    ImGui::SliderFloat("Metallic", &material_editor_asset.metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &material_editor_asset.roughness, 0.0f, 1.0f);
    ImGui::ColorEdit3("Emissive", &material_editor_asset.emissive.x);
    ImGui::DragFloat("Emissive Strength", &material_editor_asset.emissive_strength,
        0.05f, 0.0f, 1000.0f);
    ImGui::SliderFloat("Ambient Occlusion", &material_editor_asset.ambient_occlusion,
        0.0f, 1.0f);
    int alpha = static_cast<int>(material_editor_asset.alpha_mode);
    const char* alpha_modes[]{ "Opaque", "Mask", "Blend" };
    if (ImGui::Combo("Alpha Mode", &alpha, alpha_modes, IM_ARRAYSIZE(alpha_modes)))
        material_editor_asset.alpha_mode =
            static_cast<ReplayEngine::Rendering::MaterialAlphaMode>(alpha);
    if (material_editor_asset.alpha_mode == ReplayEngine::Rendering::MaterialAlphaMode::Mask)
        ImGui::SliderFloat("Alpha Cutoff", &material_editor_asset.alpha_cutoff, 0.0f, 1.0f);
    ImGui::Checkbox("Double Sided", &material_editor_asset.double_sided);
    const char* shading_models[]{ "FBX Default", "PBR", "Toon", "Unlit", "Pixelate" };
    ImGui::Combo("Shading Model", &material_editor_asset.shading_model,
        shading_models, IM_ARRAYSIZE(shading_models));

    const auto texture_guid = [](const char* label, std::string& value)
    {
        char buffer[96]{};
        strncpy_s(buffer, value.c_str(), _TRUNCATE);
        if (ImGui::InputText(label, buffer, IM_ARRAYSIZE(buffer))) value = buffer;
    };
    if (ImGui::TreeNode("Texture AssetGUIDs"))
    {
        texture_guid("Base Color Texture", material_editor_asset.base_color_texture);
        texture_guid("Normal Texture", material_editor_asset.normal_texture);
        texture_guid("Metallic Texture", material_editor_asset.metallic_texture);
        texture_guid("Roughness Texture", material_editor_asset.roughness_texture);
        texture_guid("Emissive Texture", material_editor_asset.emissive_texture);
        texture_guid("AO Texture", material_editor_asset.ambient_occlusion_texture);
        ImGui::TreePop();
    }

    if (ImGui::Button("Save Material")) save_material_editor();
    ImGui::SameLine();
    if (ImGui::Button("Assign to Selected Renderer"))
        place_asset_in_object_scene(*selected, false);
    ImGui::TextDisabled("%s", material_editor_status.c_str());
}

bool framework::browse_model_asset()
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
    return load_model_asset_async(filename);
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

bool framework::load_model_asset_async(const std::wstring& filename)
{
    const std::filesystem::path path(filename);
    async_stage_load_active = true;
    model_asset_status = "モデルを並列ロードしています...";
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
                model_asset_status = "読み込み失敗: " + result.error;
                return;
            }
            load_model_asset_now(result.path.wstring());
        });
    return true;
}

bool framework::load_model_asset_now(const std::wstring& filename)
{
    const std::filesystem::path path(filename);
    const std::wstring extension = LowerExtension(path);
    selected_model_asset_path = WideToUtf8(path.wstring());

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
                model_asset_status = "読み込み失敗: " + candidate->Error();
                return false;
            }

            stage_gltf_model = std::move(candidate);
            outline_per_stage = false;
            model_asset_status = "glTF素材を読み込みました（配置プレビュー中）";
        }
        else if (extension == L".fbx" || extension == L".cereal")
        {
            std::filesystem::path cache = path;
            cache.replace_extension(L".cereal");
            if (!std::filesystem::exists(cache))
            {
                model_asset_status = "同じ場所に実行用.cerealキャッシュが必要です";
                return false;
            }

            auto candidate = skinned_mesh_cache.Load(path, [this, path]
            {
                return std::make_shared<skinned_mesh>(device.Get(), path.string().c_str());
            });
            skinned_meshes[1] = std::move(candidate);
            stage_gltf_model.reset();
            model_asset_status = "FBXキャッシュ素材を読み込みました（配置プレビュー中）";
        }
        else
        {
            model_asset_status = "未対応のモデル形式です";
            return false;
        }
    }
    catch (const std::exception& exception)
    {
        model_asset_status = "読み込み失敗: ";
        model_asset_status += exception.what();
        return false;
    }
    catch (...)
    {
        model_asset_status = "モデル読み込み中に不明なエラーが発生しました";
        return false;
    }

    ReplayEngine::Assets::AssetCache cache;
    ReplayEngine::Assets::AssetCacheEntry cache_entry{};
    std::string cache_error;
    if (cache.StoreSourceFile(path, ReplayEngine::Assets::AssetKind::Model,
        cache_entry, cache_error))
    {
        selected_model_cache_path = WideToUtf8(cache_entry.cache_path.wstring());
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
                model_asset_status += " / resourcesへの取込失敗";
            }
            else
            {
                asset_database.Remove(temporary_guid);
                selected_model_asset_path = WideToUtf8(registered_source.wstring());
                model_asset_status += " / resourcesへ取込済み";
            }
        }
        const auto& record = asset_database.Register(registered_source,
            ReplayEngine::Assets::AssetKind::Model, cache_entry.cache_path);
        selected_model_asset_guid = record.guid;
        std::string database_error;
        asset_database.Save(database_error);
        model_asset_status += " / 共通キャッシュ作成済み";
    }
    else
    {
        selected_model_cache_path.clear();
        model_asset_status += " / キャッシュ失敗: " + cache_error;
    }

    selected_asset_guid = selected_model_asset_guid;
    set_editor_workspace(editor_workspace::placement);
    selected_editor_object = editor_selection::game_object;
    model_asset_status += " / Asset Browserへ登録済み";
    return true;
}

