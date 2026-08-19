// Runtime main のうち「アセット・Prefab のヘッドレス検証」を持つ。
//
//   mainValidationScene.cpp        … Material / Prefab 検証（このファイル）
//   mainValidationLandscape.cpp    … Landscape 検証
//   mainValidationSceneWorld.cpp   … 大規模Scene / Scene / 永続化検証
//
// 各検証関数の本体は分割前のまま移動し、検証の呼び出し順と結果コードは変更しない。
#include "framework.h"
#include "mainInternal.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../../../RePlayEngine/Assets/AssetDatabase.h"
#include "../../../RePlayEngine/Components/Core/PersistentComponent.h"
#include "../../../RePlayEngine/Components/Core/PropertyLinkComponent.h"
#include "../../../RePlayEngine/Components/Gameplay/StageGameplayComponents.h"
#include "../../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../../RePlayEngine/Editor/Core/EditorContext.h"
#include "../../../RePlayEngine/Editor/Validation/SceneValidator.h"
#include "../../../RePlayEngine/Landscape/LandscapeCollision.h"
#include "../../../RePlayEngine/Landscape/LandscapeData.h"
#include "../../../RePlayEngine/Landscape/LandscapeEditorTool.h"
#include "../../../RePlayEngine/Landscape/LandscapeRenderer.h"
#include "../../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneSerializer.h"

namespace ReplayEngine::Runtime::Detail
{
    int RunHeadlessMaterialValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-material") return -1;

        using ReplayEngine::Rendering::MaterialAlphaMode;
        using ReplayEngine::Rendering::MaterialAsset;
        const std::filesystem::path folder = ValidationFolder();
        const std::filesystem::path material_path = folder / "MaterialFoundation.replaymaterial";

        MaterialAsset material;
        material.base_color = { 0.2f, 0.4f, 0.8f, 0.9f };
        material.base_color_texture = "texture-base-guid";
        material.normal_texture = "texture-normal-guid";
        material.metallic = 0.7f;
        material.roughness = 0.25f;
        material.emissive = { 0.1f, 0.2f, 0.3f };
        material.emissive_strength = 2.5f;
        material.ambient_occlusion = 0.85f;
        material.alpha_mode = MaterialAlphaMode::Mask;
        material.alpha_cutoff = 0.42f;
        material.double_sided = true;
        material.shading_model = 2;

        std::string error;
        if (!MaterialAsset::Save(material, material_path, error))
        {
            std::fprintf(stderr, "Material first save failed: %s\n", error.c_str());
            return 50;
        }
        material.roughness = 0.3f;
        if (!MaterialAsset::Save(material, material_path, error))
        {
            std::fprintf(stderr, "Material atomic replace failed: %s\n", error.c_str());
            return 51;
        }
        std::filesystem::path backup_path = material_path;
        backup_path += L".bak";

        MaterialAsset loaded;
        if (!std::filesystem::exists(backup_path) ||
            !MaterialAsset::Load(material_path, loaded, error) ||
            std::fabs(loaded.roughness - 0.3f) > 0.00001f ||
            loaded.base_color_texture != material.base_color_texture ||
            loaded.normal_texture != material.normal_texture ||
            loaded.alpha_mode != MaterialAlphaMode::Mask || !loaded.double_sided)
        {
            std::fprintf(stderr, "Material round-trip/backup failed: %s\n", error.c_str());
            return 52;
        }

        ReplayEngine::Assets::AssetDatabase assets(folder / "MaterialAssetDatabase.replaydb");
        assets.Load(error);
        const auto& record = assets.Register(material_path,
            ReplayEngine::Assets::AssetKind::Material);
        const std::string material_guid = record.guid;
        if (!assets.Save(error))
        {
            std::fprintf(stderr, "Material AssetDatabase save failed: %s\n", error.c_str());
            return 53;
        }
        ReplayEngine::Assets::AssetDatabase reloaded_assets(
            folder / "MaterialAssetDatabase.replaydb");
        if (!reloaded_assets.Load(error) ||
            reloaded_assets.FindByGuid(material_guid) == nullptr ||
            reloaded_assets.FindByGuid(material_guid)->kind !=
                ReplayEngine::Assets::AssetKind::Material)
        {
            std::fprintf(stderr, "Material AssetDatabase reload failed: %s\n", error.c_str());
            return 54;
        }

        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Scene::Scene scene("MaterialScene");
        ReplayEngine::Core::GameObject* object = scene.CreateGameObject("MaterialMesh");
        auto* renderer = object != nullptr ? object->AddComponent<
            ReplayEngine::Components::MeshRendererComponent>() : nullptr;
        if (renderer == nullptr)
        {
            std::fprintf(stderr, "Material Renderer creation failed\n");
            return 55;
        }
        renderer->mesh_asset = "mesh-guid";
        renderer->material_asset = material_guid;

        ReplayEngine::Scene::Serialization::SceneData data;
        ReplayEngine::Scene::Serialization::CaptureScene(scene, data);
        ReplayEngine::Scene::Scene restored;
        ReplayEngine::Scene::Serialization::SceneLoadReport report;
        if (!ReplayEngine::Scene::Serialization::ApplySceneData(data, restored, report) ||
            restored.GameObjectCount() != 1 ||
            restored.GameObjectAt(0)->GetComponent<
                ReplayEngine::Components::MeshRendererComponent>() == nullptr ||
            restored.GameObjectAt(0)->GetComponent<
                ReplayEngine::Components::MeshRendererComponent>()->material_asset != material_guid)
        {
            std::fprintf(stderr, "Material Renderer Scene round-trip failed\n");
            return 56;
        }

        std::fprintf(stderr,
            "Material OK: atomic save/backup, full property round-trip, AssetGUID database, Renderer Scene link OK\n");
        return 0;
    }

    int RunHeadlessPrefabValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-prefab") return -1;

        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Scene::Scene scene("PrefabValidation");
        ReplayEngine::Core::GameObject* root = scene.CreateGameObject("PrefabRoot");
        ReplayEngine::Core::GameObject* child = scene.CreateGameObject("PrefabChild");
        if (root == nullptr || child == nullptr || !child->SetParent(root, false) ||
            root->AddComponent<ReplayEngine::Components::PointLightComponent>() == nullptr ||
            child->AddComponent<ReplayEngine::Components::SpawnPointComponent>() == nullptr)
        {
            std::fprintf(stderr, "Prefab source hierarchy creation failed\n");
            return 30;
        }
        root->GetTransform().SetLocalPosition({ 1.0f, 2.0f, 3.0f });
        child->GetTransform().SetLocalPosition({ 0.0f, 4.0f, 0.0f });

        namespace Serialization = ReplayEngine::Scene::Serialization;
        const std::filesystem::path prefab_path =
            ValidationFolder() / "PrefabFoundation.replayprefab";
        constexpr const char* source_guid = "validation-prefab-guid";
        std::string error;
        const ReplayEngine::Core::ObjectID original_root = root->ID();
        const ReplayEngine::Core::ObjectID original_child = child->ID();
        if (!Serialization::PrefabSerializer::Save(scene, original_root, prefab_path, error) ||
            !Serialization::PrefabSerializer::LinkInstance(scene, original_root, source_guid, error))
        {
            std::fprintf(stderr, "Prefab save/link failed: %s\n", error.c_str());
            return 31;
        }
        if (!root->IsPrefabRoot() || !child->IsPrefabInstance() ||
            child->PrefabInstanceRoot() != original_root ||
            root->PrefabLocalID() == 0 || child->PrefabLocalID() == 0 ||
            root->PrefabLocalID() == child->PrefabLocalID())
        {
            std::fprintf(stderr, "Prefab instance identity is invalid\n");
            return 32;
        }

        Serialization::PrefabOverrideSummary summary =
            Serialization::PrefabSerializer::InspectOverrides(
                scene, original_root, prefab_path, source_guid);
        if (summary.missing_source || summary.has_overrides)
        {
            std::fprintf(stderr, "Fresh Prefab incorrectly reports overrides\n");
            return 33;
        }
        root->GetTransform().SetLocalPosition({ 9.0f, 2.0f, 3.0f });
        summary = Serialization::PrefabSerializer::InspectOverrides(
            scene, original_root, prefab_path, source_guid);
        if (!summary.has_overrides)
        {
            std::fprintf(stderr, "Prefab transform override was not detected\n");
            return 34;
        }

        Serialization::SceneLoadReport report;
        if (!Serialization::PrefabSerializer::RevertOverrides(
            scene, original_root, prefab_path, source_guid, error, &report))
        {
            std::fprintf(stderr, "Prefab revert failed: %s\n", error.c_str());
            return 35;
        }
        root = scene.FindGameObjectByID(original_root);
        child = scene.FindGameObjectByID(original_child);
        if (root == nullptr || child == nullptr ||
            std::fabs(root->GetTransform().LocalPosition().x - 1.0f) > 0.00001f ||
            !root->IsPrefabRoot() || child->PrefabInstanceRoot() != original_root)
        {
            std::fprintf(stderr, "Prefab revert did not preserve Scene ObjectIDs/state\n");
            return 36;
        }

        const ReplayEngine::Core::ObjectID first = Serialization::PrefabSerializer::Instantiate(
            scene, prefab_path, error, &report, source_guid);
        const ReplayEngine::Core::ObjectID second = Serialization::PrefabSerializer::Instantiate(
            scene, prefab_path, error, &report, source_guid);
        ReplayEngine::Core::GameObject* first_root = scene.FindGameObjectByID(first);
        ReplayEngine::Core::GameObject* second_root = scene.FindGameObjectByID(second);
        if (!first.Valid() || !second.Valid() || first == second ||
            first_root == nullptr || second_root == nullptr ||
            !first_root->IsPrefabRoot() || !second_root->IsPrefabRoot() ||
            first_root->Children().size() != 1 || second_root->Children().size() != 1 ||
            first_root->Children().front()->GetComponent<
                ReplayEngine::Components::SpawnPointComponent>() == nullptr)
        {
            std::fprintf(stderr, "Repeated Prefab instantiation/remap failed: %s\n", error.c_str());
            return 37;
        }

        first_root->SetName("PrefabRootApplied");
        if (!Serialization::PrefabSerializer::ApplyOverrides(
            scene, first, prefab_path, source_guid, error))
        {
            std::fprintf(stderr, "Prefab apply failed: %s\n", error.c_str());
            return 38;
        }
        summary = Serialization::PrefabSerializer::InspectOverrides(
            scene, first, prefab_path, source_guid);
        if (summary.has_overrides || summary.missing_source)
        {
            std::fprintf(stderr, "Applied Prefab still reports overrides\n");
            return 39;
        }

        if (!Serialization::PrefabSerializer::Unpack(scene, second, error))
        {
            std::fprintf(stderr, "Prefab unpack failed: %s\n", error.c_str());
            return 40;
        }
        second_root = scene.FindGameObjectByID(second);
        if (second_root == nullptr || second_root->IsPrefabInstance() ||
            second_root->Children().front()->IsPrefabInstance())
        {
            std::fprintf(stderr, "Prefab unpack left instance metadata\n");
            return 41;
        }

        std::fprintf(stderr,
            "Prefab OK: recursive save, GUID/local IDs, override detect, revert with stable Scene IDs, repeated remap, apply/unpack OK\n");
        return 0;
    }

}
