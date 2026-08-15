// Asset Browser のうち「Asset の Scene 配置」だけを持つ。
//
//   framework_asset_browser.cpp          … Asset の Scene 配置（このファイル）
//   framework_asset_browserInternal.h    … 分割内部の拡張子ヘルパ
//   framework_asset_browser_material.cpp … Material Asset の作成・編集
//   framework_asset_browser_model.cpp    … Model Asset の読み込み・取り込み

#include "framework.h"
#include "framework_asset_browserInternal.h"
#include "gltf_model.h"
#include "skinned_mesh.h"
#include "../../RePlayEngine/Assets/AssetCache.h"
#include "../../RePlayEngine/Components/Physics/MeshColliderComponent.h"
#include "../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../RePlayEngine/Components/Rendering/AnimatorComponent.h"
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
using framework_asset_browser::Detail::LowerExtension;

bool framework::place_asset_in_object_scene(const ReplayEngine::Assets::AssetRecord& asset,
    bool add_mesh_collider, const DirectX::XMFLOAT3* drop_world_position,
    ReplayEngine::Core::ObjectID drop_target)
{
    if (object_scene_play_mode)
    {
        object_editor_context.SetStatus("Play中はAssetを配置できません");
        return false;
    }

    if (asset.kind == ReplayEngine::Assets::AssetKind::Material)
    {
        ReplayEngine::Core::GameObject* target = drop_target.Valid()
            ? object_scene.FindGameObjectByID(drop_target)
            : object_editor_context.Selection().ResolvePrimary(object_scene);
        if (target == nullptr)
        {
            object_editor_context.SetStatus(
                "MaterialをGameObject上へドロップするか、割り当て先を選択してください");
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

        ReplayEngine::Core::GameObject* target = drop_target.Valid()
            ? object_scene.FindGameObjectByID(drop_target)
            : object_editor_context.Selection().ResolvePrimary(object_scene);
        if (target == nullptr)
        {
            object_editor_context.SetStatus(
                "ScriptをGameObject上へドロップするか、追加先を選択してください");
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
        if (drop_world_position != nullptr)
        {
            if (auto* root_object = object_scene.FindGameObjectByID(root))
                root_object->GetTransform().SetWorldPosition(*drop_world_position);
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

    // Unity/Unreal と同じく、ファイル拡張子ではなく内容で Renderer を選ぶ。
    // Skin/Animation の無い GLB は軽い MeshRenderer のままにする。
    gltf_model* gltf = resolve_object_gltf(asset.guid);
    const bool animated_gltf = gltf != nullptr &&
        (gltf->HasSkins() || gltf->HasAnimations());
    bool renderer_added = false;
    if (animated_gltf)
    {
        auto* renderer = object->AddComponent<
            ReplayEngine::Components::SkinnedMeshRendererComponent>();
        if (renderer != nullptr)
        {
            renderer->mesh_asset = asset.guid;
            renderer->visual_rotation_offset = { 0.0f, 0.0f, 0.0f };
            renderer->apply_fbx_coordinate_transform = false;
            auto* animator = object->AddComponent<
                ReplayEngine::Components::AnimatorComponent>();
            if (animator != nullptr) animator->idle_clip = 0;
            renderer_added = true;
        }
    }
    else
    {
        auto* renderer = object->AddComponent<
            ReplayEngine::Components::MeshRendererComponent>();
        if (renderer != nullptr)
        {
            renderer->mesh_asset = asset.guid;
            renderer_added = true;
        }
    }
    if (!renderer_added)
    {
        object_scene.DestroyGameObject(object);
        object_scene.ProcessPendingOperations();
        object_editor_context.CancelEdit();
        object_editor_context.SetStatus("Mesh Rendererを追加できませんでした");
        return false;
    }
    DirectX::XMFLOAT3 position{};
    if (drop_world_position != nullptr)
    {
        position = *drop_world_position;
    }
    else
    {
        const DirectX::XMFLOAT3 eye = editor_camera.Position();
        const DirectX::XMFLOAT3 forward = editor_camera.Forward();
        position = { eye.x + forward.x * 5.0f, eye.y + forward.y * 5.0f,
            eye.z + forward.z * 5.0f };
    }
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
    object_editor_context.SetStatus("Assetを配置しました: " + asset.display_name +
        (add_mesh_collider ? " + Mesh Collider" : ""));
    return true;
}
