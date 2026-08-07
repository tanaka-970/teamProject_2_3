#include <time.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "framework.h"
#include "../../../RePlayEngine/Assets/AssetDatabase.h"
#include "../../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../../RePlayEngine/Components/Gameplay/CharacterMotorComponent.h"
#include "../../../RePlayEngine/Components/Gameplay/StageGameplayComponents.h"
#include "../../../RePlayEngine/Components/Rendering/LightComponents.h"
#include "../../../RePlayEngine/Components/Rendering/MeshRendererComponent.h"
#include "../../../RePlayEngine/Editor/Core/EditorContext.h"
#include "../../../RePlayEngine/Editor/Validation/AnimationUndoValidation.h"
#include "../../../RePlayEngine/Editor/Validation/EditorIntegrationValidation.h"
#include "../../../RePlayEngine/Editor/Validation/SceneValidator.h"
#include "../../../RePlayEngine/Landscape/LandscapeCollision.h"
#include "../../../RePlayEngine/Landscape/LandscapeData.h"
#include "../../../RePlayEngine/Landscape/LandscapeEditorTool.h"
#include "../../../RePlayEngine/Landscape/LandscapeRenderer.h"
#include "../../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderAssetValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderBuiltInValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderMaterialValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderCompileValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/BehaviourValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/HandleValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/RuntimeSceneValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/SceneFlowValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/SerializationValidation.h"
#include "../../../RePlayEngine/Runtime/Validation/StressValidation.h"
#include "../../../RePlayEngine/Scripting/Validation/CSharpScriptValidation.h"
#include "../../../RePlayEngine/Scripting/Validation/ScriptCoreValidation.h"
#include "../../game/Behaviours/ValidationBehaviours.h"
#include "../../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneSerializer.h"

namespace
{
    // --game が指定されたら、Startup Scene から Runtime を開始する。
    //
    // 既定（引数なし）は Editor 起動。
    // Editor 起動で Runtime World を有効にすると、編集対象が空の World へ
    // すり替わり、配置した内容と保存内容が食い違う。
    // --validate-shutdown : 終了時のリソース解放を確かめる。
    //
    // D3D デバイスが要るのでヘッドレス Validation には載せられない。
    // smoke test と同じ「N フレーム描画して終了」の経路を使い、
    // 終了後の Live Object Report で合否を見る。
    bool ParseShutdownRegression(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string token;
        while (arguments >> token)
        {
            if (token == "--validate-shutdown") return true;
        }
        return false;
    }

    bool ParseStartupSceneBoot(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string token;
        while (arguments >> token)
        {
            if (token == "--game") return true;
        }
        return false;
    }

    std::uint32_t ParseAutomatedSmokeTestFrames(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        int frames = 0;
        if (!(arguments >> command) || command != "--smoke-test") return 0;
        if (!(arguments >> frames)) frames = 120;
        return static_cast<std::uint32_t>((std::clamp)(frames, 30, 3600));
    }

    int RunHeadlessMaterialValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-material") return -1;

        using ReplayEngine::Rendering::MaterialAlphaMode;
        using ReplayEngine::Rendering::MaterialAsset;
        const std::filesystem::path folder = std::filesystem::path("Saved") / "Validation";
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
        const std::filesystem::path prefab_path = std::filesystem::path("Saved") /
            "Validation" / "PrefabFoundation.replayprefab";
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

    int RunHeadlessLandscapeValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-landscape") return -1;

        using namespace ReplayEngine::Landscape;
        LandscapeData data;
        if (!data.Initialize(65, 65, 1.0f))
        {
            std::fprintf(stderr, "Landscape initialization failed\n");
            return 20;
        }

        LandscapeRenderer renderer;
        LandscapeCollision collision;
        const int initial_render_chunks = renderer.UpdateDirtyChunks(data);
        const int initial_collision_chunks = collision.UpdateDirtyChunks(data);
        if (initial_render_chunks != 4 || initial_collision_chunks != 4)
        {
            std::fprintf(stderr, "Initial chunk build failed: render=%d collision=%d\n",
                initial_render_chunks, initial_collision_chunks);
            return 21;
        }

        LandscapeBrush brush;
        brush.radius = 3.0f;
        brush.strength = 2.0f;
        brush.falloff = 0.5f;
        LandscapeEditorTool tool;
        if (!tool.BeginStroke(data, LandscapeBrushMode::Raise, brush) ||
            !tool.ApplySample(8.0f, 8.0f, 1.0f))
        {
            std::fprintf(stderr, "Landscape brush stroke failed\n");
            return 22;
        }
        std::unique_ptr<LandscapeUndoCommand> stroke = tool.EndStroke();
        const float raised_height = data.HeightAt(8, 8);
        if (stroke == nullptr || stroke->ChangedSampleCount() == 0 || raised_height <= 0.0f)
        {
            std::fprintf(stderr, "Landscape stroke produced no undoable samples\n");
            return 23;
        }

        const int edited_render_chunks = renderer.UpdateDirtyChunks(data);
        const int edited_collision_chunks = collision.UpdateDirtyChunks(data);
        if (edited_render_chunks != 1 || edited_collision_chunks != 1)
        {
            std::fprintf(stderr, "Landscape dirty update was not localized: render=%d collision=%d\n",
                edited_render_chunks, edited_collision_chunks);
            return 24;
        }

        stroke->Undo(data);
        if (std::fabs(data.HeightAt(8, 8)) > 0.00001f)
        {
            std::fprintf(stderr, "Landscape undo failed\n");
            return 25;
        }
        stroke->Redo(data);
        if (std::fabs(data.HeightAt(8, 8) - raised_height) > 0.00001f)
        {
            std::fprintf(stderr, "Landscape redo failed\n");
            return 26;
        }

        const std::filesystem::path output_path = std::filesystem::path("Saved") /
            "Validation" / "LandscapeFoundation.replaylandscape";
        std::string error;
        if (!data.Save(output_path, error))
        {
            std::fprintf(stderr, "Landscape save failed: %s\n", error.c_str());
            return 27;
        }
        LandscapeData loaded;
        if (!LandscapeData::Load(output_path, loaded, error) ||
            loaded.Width() != data.Width() || loaded.Height() != data.Height() ||
            std::fabs(loaded.HeightAt(8, 8) - raised_height) > 0.00001f)
        {
            std::fprintf(stderr, "Landscape reload verification failed: %s\n", error.c_str());
            return 28;
        }

        std::fprintf(stderr,
            "Landscape OK: %zu samples, %zu chunks, %zu edited samples, localized 1-chunk rebuild, undo/redo/save/reload OK\n",
            data.SampleCount(), data.Chunks().size(), stroke->ChangedSampleCount());
        return 0;
    }

    int RunHeadlessLargeSceneValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-large-scene") return -1;

        using clock = std::chrono::steady_clock;
        const auto total_begin = clock::now();
        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Scene::Scene scene("LargeSceneValidation");
        std::vector<ReplayEngine::Core::ObjectID> ids;
        ids.reserve(1000);

        ReplayEngine::Core::GameObject* group_root = nullptr;
        for (std::size_t index = 0; index < 1000; ++index)
        {
            const std::string name = index % 10 == 0
                ? u8"検索対象_" + std::to_string(index)
                : "LargeObject_" + std::to_string(index);
            ReplayEngine::Core::GameObject* object = scene.CreateGameObject(name);
            if (object == nullptr)
            {
                std::fprintf(stderr, "Large Scene object creation failed at %zu\n", index);
                return 60;
            }
            object->GetTransform().SetLocalPosition({
                static_cast<float>(index % 50),
                static_cast<float>((index / 50) % 10),
                static_cast<float>(index / 500) });
            if (index % 100 == 0) group_root = object;
            else if (group_root == nullptr || !object->SetParent(group_root, false))
            {
                std::fprintf(stderr, "Large Scene hierarchy creation failed at %zu\n", index);
                return 61;
            }
            ids.push_back(object->ID());
        }

        ReplayEngine::Editor::EditorContext context;
        context.AttachScene(&scene);
        for (std::size_t index = 0; index < ids.size(); index += 37)
            context.Selection().Select(ids[index], true);
        if (context.Selection().Count() != 28)
        {
            std::fprintf(stderr, "Large Scene multi-selection failed: %zu\n",
                context.Selection().Count());
            return 62;
        }

        ReplayEngine::Core::GameObject* edited = scene.FindGameObjectByID(ids[777]);
        if (edited == nullptr) return 63;
        const DirectX::XMFLOAT3 original_position = edited->GetTransform().LocalPosition();
        context.BeginEdit("Large Scene Transform");
        edited->GetTransform().SetLocalPosition({ 123.0f, 45.0f, 6.0f });
        context.CommitEdit();
        if (!context.Dirty() || !context.Undo())
        {
            std::fprintf(stderr, "Large Scene transform undo setup failed\n");
            return 64;
        }
        edited = scene.FindGameObjectByID(ids[777]);
        if (edited == nullptr ||
            std::fabs(edited->GetTransform().LocalPosition().x - original_position.x) > 0.00001f ||
            !context.Redo())
        {
            std::fprintf(stderr, "Large Scene transform undo/redo failed\n");
            return 65;
        }
        edited = scene.FindGameObjectByID(ids[777]);
        if (edited == nullptr ||
            std::fabs(edited->GetTransform().LocalPosition().x - 123.0f) > 0.00001f)
        {
            std::fprintf(stderr, "Large Scene transform redo value failed\n");
            return 66;
        }

        std::size_t search_matches = 0;
        for (std::size_t index = 0; index < scene.GameObjectCount(); ++index)
        {
            const ReplayEngine::Core::GameObject* object = scene.GameObjectAt(index);
            if (object != nullptr && object->Name().find(u8"検索対象") != std::string::npos)
                ++search_matches;
        }
        if (search_matches != 100)
        {
            std::fprintf(stderr, "Large Scene search failed: %zu\n", search_matches);
            return 67;
        }

        namespace Serialization = ReplayEngine::Scene::Serialization;
        Serialization::SceneData captured;
        Serialization::CaptureScene(scene, captured);
        const std::filesystem::path output = std::filesystem::path("Saved") /
            "Validation" / "LargeScene1000.replayscene";
        std::string error;
        const auto save_begin = clock::now();
        if (!Serialization::SceneSerializer::SaveToFile(captured, output, error))
        {
            std::fprintf(stderr, "Large Scene save failed: %s\n", error.c_str());
            return 68;
        }
        const auto save_end = clock::now();

        Serialization::SceneData loaded;
        if (!Serialization::SceneSerializer::LoadFromFile(loaded, output, error) ||
            loaded.objects.size() != 1000)
        {
            std::fprintf(stderr, "Large Scene load failed: %s\n", error.c_str());
            return 69;
        }
        ReplayEngine::Scene::Scene restored;
        Serialization::SceneLoadReport report;
        if (!Serialization::ApplySceneData(loaded, restored, report) ||
            restored.GameObjectCount() != 1000)
        {
            std::fprintf(stderr, "Large Scene apply failed\n");
            return 70;
        }
        const auto load_end = clock::now();

        const auto issues = ReplayEngine::Editor::SceneValidator::Validate(restored, nullptr);
        const std::size_t error_count = static_cast<std::size_t>(std::count_if(
            issues.begin(), issues.end(), [](const ReplayEngine::Editor::ValidationIssue& issue)
            {
                return issue.severity == ReplayEngine::Editor::ValidationSeverity::Error;
            }));
        if (error_count != 0)
        {
            std::fprintf(stderr, "Large Scene validation reported %zu errors\n", error_count);
            return 71;
        }

        const auto total_end = clock::now();
        const auto save_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            save_end - save_begin).count();
        const auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            load_end - save_end).count();
        const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            total_end - total_begin).count();
        if (total_ms > 30000)
        {
            std::fprintf(stderr, "Large Scene validation exceeded 30 seconds: %lld ms\n",
                static_cast<long long>(total_ms));
            return 72;
        }

        std::fprintf(stderr,
            "Large Scene OK: 1000 GameObjects, hierarchy/search(100)/selection(28)/transform undo-redo/save-load/validation OK; save=%lld ms load=%lld ms total=%lld ms\n",
            static_cast<long long>(save_ms), static_cast<long long>(load_ms),
            static_cast<long long>(total_ms));
        return 0;
    }

    int RunHeadlessSceneValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        std::string scene_path_text;
        if (!(arguments >> command) || command != "--validate-scene") return -1;
        if (!(arguments >> std::quoted(scene_path_text)) || scene_path_text.empty())
        {
            std::fprintf(stderr, "--validate-scene requires a path\n");
            return 2;
        }

        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Assets::AssetDatabase assets;
        std::string asset_error;
        assets.Load(asset_error);

        namespace Serialization = ReplayEngine::Scene::Serialization;
        Serialization::SceneData source;
        std::string error;
        const std::filesystem::path scene_path(scene_path_text);
        if (!Serialization::SceneSerializer::LoadFromFile(source, scene_path, error))
        {
            std::fprintf(stderr, "Scene load failed: %s\n", error.c_str());
            return 3;
        }

        ReplayEngine::Scene::Scene scene;
        Serialization::SceneLoadReport load_report;
        Serialization::ApplySceneData(source, scene, load_report);
        const auto issues = ReplayEngine::Editor::SceneValidator::Validate(scene, &assets);
        int errors = 0;
        for (const ReplayEngine::Editor::ValidationIssue& issue : issues)
        {
            if (issue.severity == ReplayEngine::Editor::ValidationSeverity::Error) ++errors;
            std::fprintf(stderr, "[%s] %s: %s\n",
                issue.severity == ReplayEngine::Editor::ValidationSeverity::Error ? "ERROR" :
                issue.severity == ReplayEngine::Editor::ValidationSeverity::Warning ? "WARN" : "INFO",
                issue.code.c_str(), issue.message.c_str());
        }

        Serialization::SceneData captured;
        Serialization::CaptureScene(scene, captured);
        const std::filesystem::path roundtrip_path = std::filesystem::path("Saved") /
            "Validation" / (scene_path.stem().string() + ".roundtrip.replayscene");
        if (!Serialization::SceneSerializer::SaveToFile(captured, roundtrip_path, error))
        {
            std::fprintf(stderr, "Round-trip save failed: %s\n", error.c_str());
            return 4;
        }
        Serialization::SceneData roundtrip;
        if (!Serialization::SceneSerializer::LoadFromFile(roundtrip, roundtrip_path, error) ||
            roundtrip.objects.size() != captured.objects.size() ||
            roundtrip.version != Serialization::SceneData::current_version)
        {
            std::fprintf(stderr, "Round-trip verification failed: %s\n", error.c_str());
            return 5;
        }

        std::fprintf(stderr, "Validated %zu objects, %zu warnings, %d errors; round-trip v%d OK\n",
            source.objects.size(), issues.size() - static_cast<std::size_t>(errors), errors,
            roundtrip.version);
        return errors == 0 ? 0 : 6;
    }

    // ObjectHandle / ComponentHandle 基盤の検証。
    // D3D11 も Window も使わないため、ビルド直後にそのまま実行できる。
    int RunHeadlessHandleValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-handles") return -1;

        return ReplayEngine::Runtime::Validation::RunHandleValidation();
    }

    // Stabilization Phase A-2。
    //
    // CameraTargetComponent の値が Runtime Camera へ反映されることを、
    // D3D11 / Window なしで検証する。終了コード帯は 860-899。
    int RunHeadlessCameraComponentValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-camera-component") return -1;

        namespace Components = ReplayEngine::Components;
        namespace Core = ReplayEngine::Core;
        namespace Reflection = ReplayEngine::Reflection;
        namespace Scene = ReplayEngine::Scene;

        int next_code = 860;
        int first_failure = 0;
        int failures = 0;
        int total = 0;

        const auto expect = [&](bool condition, const char* what)
        {
            const int code = next_code++;
            ++total;
            if (condition) return;
            ++failures;
            if (first_failure == 0) first_failure = code;
            std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
        };

        const auto close = [](float a, float b)
        {
            return std::fabs(a - b) <= 0.0005f;
        };

        Core::RegisterBuiltInComponents();

        const Core::ComponentTypeID camera_target_type =
            Components::CameraTargetComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(camera_target_type,
            "field_of_view_degrees") != nullptr,
            "CameraTargetComponent exposes field_of_view_degrees");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "near_clip") != nullptr,
            "CameraTargetComponent exposes near_clip");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "far_clip") != nullptr,
            "CameraTargetComponent exposes far_clip");

        Scene::Scene world("CameraComponentValidation");
        Core::GameObject* low = world.CreateGameObject("LowPriorityTarget");
        Core::GameObject* high = world.CreateGameObject("HighPriorityTarget");
        expect(low != nullptr && high != nullptr, "camera target objects can be created");
        if (low == nullptr || high == nullptr)
        {
            return first_failure != 0 ? first_failure : 860;
        }

        auto* low_target = low->AddComponent<Components::CameraTargetComponent>();
        auto* high_target = high->AddComponent<Components::CameraTargetComponent>();
        expect(low_target != nullptr && high_target != nullptr,
            "CameraTargetComponent can be attached");
        if (low_target == nullptr || high_target == nullptr)
        {
            return first_failure != 0 ? first_failure : 861;
        }

        low_target->priority = 1;
        high_target->priority = 5;

        Components::CameraTargetSelection selection =
            Components::ResolveCameraTargetSelection(world, Core::ObjectID::Invalid());
        expect(selection.object == high && selection.component == high_target &&
            !selection.used_controlled_object,
            "without controlled object, highest priority active camera target is selected");

        selection = Components::ResolveCameraTargetSelection(world, low->ID());
        expect(selection.object == low && selection.component == low_target &&
            selection.used_controlled_object,
            "controlled object camera target wins over priority fallback");

        low_target->SetEnabled(false);
        selection = Components::ResolveCameraTargetSelection(world, low->ID());
        expect(selection.object == high && selection.component == high_target &&
            !selection.used_controlled_object,
            "disabled controlled camera target falls back to highest priority target");

        high_target->SetEnabled(false);
        selection = Components::ResolveCameraTargetSelection(world, low->ID());
        expect(!selection.Valid(), "no active camera target produces no selection");

        SceneGame gameplay;
        gameplay.Initialize(16.0f / 9.0f);
        const DirectX::XMFLOAT4X4 default_projection =
            gameplay.GetCamera().GetProjection();

        gameplay.ApplyCameraSettings(30.0f, 0.5f, 200.0f);
        const DirectX::XMFLOAT4X4 narrow_projection =
            gameplay.GetCamera().GetProjection();

        expect(narrow_projection._22 > default_projection._22,
            "field_of_view_degrees rebuilds the Runtime Camera projection");
        const float expected_33 = 200.0f / (200.0f - 0.5f);
        const float expected_43 = -(0.5f * 200.0f) / (200.0f - 0.5f);
        expect(close(narrow_projection._33, expected_33),
            "near_clip/far_clip update projection depth scale");
        expect(close(narrow_projection._43, expected_43),
            "near_clip/far_clip update projection depth offset");

        gameplay.SetAspect(4.0f / 3.0f);
        const DirectX::XMFLOAT4X4 resized_projection =
            gameplay.GetCamera().GetProjection();
        expect(close(resized_projection._22, narrow_projection._22),
            "SetAspect preserves the active field_of_view_degrees");
        expect(!close(resized_projection._11, narrow_projection._11),
            "SetAspect reapplies the projection with the new aspect ratio");

        if (first_failure == 0)
        {
            std::fprintf(stderr, "camera-component OK: %d checks passed\n", total);
            return 0;
        }
        std::fprintf(stderr, "camera-component FAILED: %d/%d checks failed (first=%d)\n",
            failures, total, first_failure);
        return first_failure;
    }

    // Stabilization Phase A-3。
    //
    // PlayerControllerComponent の Dash 倍率と CharacterMotorComponent::move_speed が
    // 実際の水平速度へ反映されることを、固定更新だけで検証する。
    // 終了コード帯は 900-939。
    int RunHeadlessPlayerSpeedValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-player-speed") return -1;

        namespace Components = ReplayEngine::Components;
        namespace Core = ReplayEngine::Core;
        namespace Reflection = ReplayEngine::Reflection;
        namespace Scene = ReplayEngine::Scene;

        int next_code = 900;
        int first_failure = 0;
        int failures = 0;
        int total = 0;

        const auto expect = [&](bool condition, const char* what)
        {
            const int code = next_code++;
            ++total;
            if (condition) return;
            ++failures;
            if (first_failure == 0) first_failure = code;
            std::fprintf(stderr, "  [FAIL %d] %s\n", code, what);
        };

        const auto close = [](float a, float b)
        {
            return std::fabs(a - b) <= 0.0005f;
        };

        Core::RegisterBuiltInComponents();

        const Core::ComponentTypeID motor_type =
            Components::CharacterMotorComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(motor_type, "move_speed") != nullptr,
            "CharacterMotorComponent exposes move_speed");

        Scene::Scene world("PlayerSpeedValidation");
        Core::GameObject* actor = world.CreateGameObject("Actor");
        expect(actor != nullptr, "player speed test object can be created");
        if (actor == nullptr) return first_failure != 0 ? first_failure : 900;

        auto* motor = actor->AddComponent<Components::CharacterMotorComponent>();
        expect(motor != nullptr, "CharacterMotorComponent can be attached");
        if (motor == nullptr) return first_failure != 0 ? first_failure : 901;

        motor->move_speed = 6.0f;
        motor->acceleration = 1000.0f;
        motor->deceleration = 1000.0f;
        motor->vertical_physics = false;

        world.Start();

        constexpr float fixed_delta = 1.0f / 60.0f;
        motor->Move(DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f });
        world.FixedUpdate(fixed_delta);
        expect(close(motor->PlanarSpeed(), 6.0f),
            "move_speed caps the actual planar speed");

        motor->Move(DirectX::XMFLOAT3{ 1.0f, 0.0f, 1.0f });
        world.FixedUpdate(fixed_delta);
        expect(close(motor->PlanarSpeed(), 6.0f),
            "diagonal movement does not exceed move_speed");

        motor->Move(DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f }, 2.0f);
        world.FixedUpdate(fixed_delta);
        expect(close(motor->PlanarSpeed(), 12.0f),
            "speed_multiplier raises the actual planar speed cap");

        motor->Move(DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f }, 0.5f);
        world.FixedUpdate(fixed_delta);
        expect(close(motor->PlanarSpeed(), 3.0f),
            "speed_multiplier lowers the actual planar speed cap");

        motor->Move(DirectX::XMFLOAT3{ 1.0f, 0.0f, 0.0f }, 0.0f);
        world.FixedUpdate(fixed_delta);
        expect(close(motor->PlanarSpeed(), 0.0f),
            "zero speed_multiplier behaves like no movement request");

        if (first_failure == 0)
        {
            std::fprintf(stderr, "player-speed OK: %d checks passed\n", total);
            return 0;
        }
        std::fprintf(stderr, "player-speed FAILED: %d/%d checks failed (first=%d)\n",
            failures, total, first_failure);
        return first_failure;
    }

    // Phase 2 (Serialization Foundation) の検証。
    // どれもファイルを触らず、メモリ上の文字列で往復を確かめる。
    // 既存の Scene / Prefab 原本は一切変更しない。
    int RunHeadlessSerializationValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command)) return -1;

        namespace Validation = ReplayEngine::Runtime::Validation;
        if (command == "--validate-serialization")
        {
            return Validation::RunSerializationValidation();
        }
        if (command == "--validate-missing-component")
        {
            return Validation::RunMissingComponentValidation();
        }
        if (command == "--validate-scene-version")
        {
            return Validation::RunSceneVersionValidation();
        }

        // Phase 3-5。Behaviour を使う検証は、先にゲーム側の登録を通しておく。
        if (command == "--validate-behaviour")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunBehaviourValidation();
        }
        if (command == "--validate-events")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunEventValidation();
        }
        if (command == "--validate-runtime-api")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunRuntimeApiValidation();
        }
        if (command == "--validate-collision")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunCollisionValidation();
        }

        // Phase 6。Runtime Scene の読み込みと入れ替え。
        // 検証用の Scene ファイルは Saved/Validation/RuntimeScene/ へ
        // その場で書き出すため、既存の Scene 原本には触れない。
        if (command == "--validate-runtime-scene")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunRuntimeSceneValidation();
        }

        // Phase 7。Scene Flow / Startup Scene / SceneTransitionBehaviour。
        //
        // 2 段に分けている理由:
        //   前半 (460-507) は Engine 側だけで完結する検証。
        //   後半 (508-519) は Game Module の SceneTransitionBehaviour を触る。
        //   Engine 側の Validation から Game の型を参照すると
        //   「Engine が特定のゲームを知っている」依存ができるため、
        //   呼び分けをここで行う。
        if (command == "--validate-scene-flow")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            const int engine_result = Validation::RunSceneFlowValidation();
            if (engine_result != 0) return engine_result;
            return Game::RunSceneTransitionValidation(508);
        }

        // Phase 8。Editor 統合。
        //
        // ImGui の操作そのものは自動化していない。
        // UI が呼ぶのと同じ内部 API を叩き、データが壊れないことを確かめる。
        if (command == "--validate-editor-integration")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return ReplayEngine::Editor::Validation::RunEditorIntegrationValidation();
        }

        // Phase 9。反復と大量データの耐久検査。
        if (command == "--validate-stress")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            Game::RegisterGameBehaviours();
            return Validation::RunStressValidation();
        }

        // Pre-Scripting Stabilization / Phase A-1。
        //
        // Undo / Redo のあとも Scene の実行状態が保たれることを、
        // Component の更新回数で直接確かめる。
        // 終了コード帯は 800-859。
        if (command == "--validate-animation-undo")
        {
            ReplayEngine::Core::RegisterBuiltInComponents();
            return ReplayEngine::Editor::Validation::RunAnimationUndoValidation();
        }

        // Script Phase 1。共通スクリプト基盤。
        //
        // Lua も .NET も使わない。MockScriptBackend の 2 種類のスクリプト型で、
        // Schema の共有・ライフサイクル順序・保存・複製・値の保護を確かめる。
        // 終了コード帯は 620-799。
        {
            namespace ScriptValidation = ReplayEngine::Scripting::Validation;

            if (command == "--validate-script-core")
            {
                ReplayEngine::Core::RegisterBuiltInComponents();
                return ScriptValidation::RunScriptCoreValidation();
            }
            if (command == "--validate-script-lifecycle")
            {
                ReplayEngine::Core::RegisterBuiltInComponents();
                return ScriptValidation::RunScriptLifecycleValidation();
            }
            if (command == "--validate-script-serialization")
            {
                ReplayEngine::Core::RegisterBuiltInComponents();
                return ScriptValidation::RunScriptSerializationValidation();
            }
            if (command == "--validate-csharp-scripting")
            {
                ReplayEngine::Core::RegisterBuiltInComponents();
                return ScriptValidation::RunCSharpScriptValidation();
            }
        }

        // シェーダ基盤。フェーズ 1（実行時コンパイル）。
        //
        // D3D デバイスを作らずに走る。D3DCompile はデバイス非依存なので、
        // ヘッドレスで検証できる。終了コード帯は 900-949。
        if (command == "--validate-shader-compile")
        {
            return ReplayEngine::Rendering::Validation::RunShaderCompileValidation();
        }

        // シェーダ基盤。フェーズ 2（pragma 解析 / GUID 採番 / Catalog）。
        // 終了コード帯は 950-999。
        if (command == "--validate-shader-asset")
        {
            return ReplayEngine::Rendering::Validation::RunShaderAssetValidation();
        }

        // シェーダ基盤。フェーズ 4（組み込み 5 種の移植）。
        //
        // 実プロジェクトの Shader/ を走査するので、
        // カレントディレクトリがプロジェクト直下であること。
        // 終了コードは 1200 から連番。
        if (command == "--validate-shader-builtin")
        {
            return ReplayEngine::Rendering::Validation::RunShaderBuiltInValidation();
        }

        // シェーダ基盤。フェーズ 5（MaterialAsset v3 / 旧版移行）。
        if (command == "--validate-shader-material")
        {
            return ReplayEngine::Rendering::Validation::RunShaderMaterialValidation();
        }

        return -1;
    }
}

LRESULT CALLBACK window_procedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	framework* p{ reinterpret_cast<framework*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)) };
	return p ? p->handle_message(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
}

int WINAPI WinMain(_In_ HINSTANCE instance, _In_opt_  HINSTANCE prev_instance, _In_ LPSTR cmd_line, _In_ int cmd_show)
{
    const int large_scene_validation_result = RunHeadlessLargeSceneValidation(cmd_line);
    if (large_scene_validation_result >= 0) return large_scene_validation_result;
    const int validation_result = RunHeadlessSceneValidation(cmd_line);
    if (validation_result >= 0) return validation_result;
    const int material_validation_result = RunHeadlessMaterialValidation(cmd_line);
    if (material_validation_result >= 0) return material_validation_result;
    const int landscape_validation_result = RunHeadlessLandscapeValidation(cmd_line);
    if (landscape_validation_result >= 0) return landscape_validation_result;
    const int prefab_validation_result = RunHeadlessPrefabValidation(cmd_line);
    if (prefab_validation_result >= 0) return prefab_validation_result;
    const int handle_validation_result = RunHeadlessHandleValidation(cmd_line);
    if (handle_validation_result >= 0) return handle_validation_result;
    const int camera_component_validation_result =
        RunHeadlessCameraComponentValidation(cmd_line);
    if (camera_component_validation_result >= 0) return camera_component_validation_result;
    const int player_speed_validation_result = RunHeadlessPlayerSpeedValidation(cmd_line);
    if (player_speed_validation_result >= 0) return player_speed_validation_result;
    const int serialization_validation_result = RunHeadlessSerializationValidation(cmd_line);
    if (serialization_validation_result >= 0) return serialization_validation_result;
    const std::uint32_t automated_smoke_test_frames =
        ParseAutomatedSmokeTestFrames(cmd_line);
    const bool shutdown_regression_requested = ParseShutdownRegression(cmd_line);

    // WICの画像読み込みはCOMを使うため、エンジンの生存期間中は初期化状態を維持する。
    // シーン切り替え後もWICファクトリを確実に利用できるようにする。
	const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // 相対パスの基準を実行ファイルの場所へ統一する。
    // Visual Studioの作業ディレクトリ設定に依存せず、直接起動でも同じ動作にする。
	std::array<wchar_t, 32768> executable_path{};
	const DWORD path_length = GetModuleFileNameW(nullptr, executable_path.data(),
		static_cast<DWORD>(executable_path.size()));
	if (path_length > 0 && path_length < executable_path.size())
	{
		std::wstring directory(executable_path.data(), path_length);
		const size_t separator = directory.find_last_of(L"\\/");
		if (separator != std::wstring::npos)
		{
			directory.resize(separator);
			const std::wstring packaged_resources = directory + L"\\resources";
			const DWORD attributes = GetFileAttributesW(packaged_resources.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
			{
        // Visual Studioの配置は「プロジェクト/x64/構成/3dgp.exe」となる。
				directory += L"\\..\\..";
			}
			SetCurrentDirectoryW(directory.c_str());
		}
	}

	srand(static_cast<unsigned int>(time(nullptr)));

#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = window_procedure;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = instance;
	wcex.hIcon = 0;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = APPLICATION_NAME;
	wcex.hIconSm = 0;
	RegisterClassExW(&wcex);

	RECT rc{ 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindowExW(0, APPLICATION_NAME, L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
		NULL, NULL, instance, NULL);
	ShowWindow(hwnd, automated_smoke_test_frames > 0 ? SW_HIDE : cmd_show);

    int exit_code = 0;
    Microsoft::WRL::ComPtr<ID3D11Debug> d3d11_debug;
    HRESULT d3d11_live_report_result = E_NOINTERFACE;
    bool d3d11_live_report_available = false;
    {
	    framework application(hwnd);
        application.set_automated_smoke_test_frames(automated_smoke_test_frames);
        if (ParseStartupSceneBoot(cmd_line)) application.request_startup_scene_boot();
        if (shutdown_regression_requested)
        {
            // 数フレーム描画してから終了させる。
            // 一度も描画せずに終わると、描画経路で作られるリソースを通らない。
            application.set_automated_smoke_test_frames(
                automated_smoke_test_frames > 0 ? automated_smoke_test_frames : 60u);
            application.request_shutdown_regression();
        }
	    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&application));
	    exit_code = application.run();
        d3d11_debug = application.acquire_d3d11_debug();
    }

    if (d3d11_debug)
    {
        d3d11_live_report_available = true;
        d3d11_live_report_result = d3d11_debug->ReportLiveDeviceObjects(
            D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
        std::fprintf(stderr, "D3D11 Live Object Report: %s (0x%08lx)\n",
            SUCCEEDED(d3d11_live_report_result) ? "completed" : "failed",
            static_cast<unsigned long>(d3d11_live_report_result));
        if (FAILED(d3d11_live_report_result) && exit_code == 0) exit_code = 73;
        d3d11_debug.Reset();
    }

	if (automated_smoke_test_frames > 0)
    {
        std::fprintf(stderr, "Runtime smoke test: %u rendered frames, exit code %d\n",
            automated_smoke_test_frames, exit_code);
        const std::filesystem::path validation_folder =
            std::filesystem::path("Saved") / "Validation";
        std::error_code directory_error;
        std::filesystem::create_directories(validation_folder, directory_error);
        std::ofstream report(validation_folder / "RuntimeSmoke.txt",
            std::ios::binary | std::ios::trunc);
        if (report)
        {
            report << "REPLAY_RUNTIME_SMOKE 1\n";
            report << "RENDERED_FRAMES " << automated_smoke_test_frames << '\n';
            report << "EXIT_CODE " << exit_code << '\n';
            report << "D3D11_DEBUG_AVAILABLE " << (d3d11_live_report_available ? 1 : 0) << '\n';
            report << "D3D11_LIVE_REPORT_HRESULT 0x" << std::hex << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(
                    d3d11_live_report_result) << '\n';
        }
    }
	if (SUCCEEDED(com_result)) CoUninitialize();
	return exit_code;
}
