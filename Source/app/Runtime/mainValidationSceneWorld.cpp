// Runtime main のうち「大規模Scene・Scene・永続化のヘッドレス検証」を持つ。
// Scene 検証の関数本体は分割前のまま移動し、結果コードと検証順序は変更しない。
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
        const std::filesystem::path output =
            ValidationFolder() / "LargeScene1000.replayscene";
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
        const std::filesystem::path roundtrip_path = ValidationFolder() /
            (scene_path.stem().string() + ".roundtrip.replayscene");
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

    int RunHeadlessScenePersistenceValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command) || command != "--validate-scene-persistence") return -1;

        ReplayEngine::Core::RegisterBuiltInComponents();
        namespace Serialization = ReplayEngine::Scene::Serialization;
        ReplayEngine::Scene::Scene source("PersistenceSource");
        ReplayEngine::Core::GameObject* root = source.CreateGameObject("PersistentRoot");
        ReplayEngine::Core::GameObject* child = source.CreateGameObject("PersistentChild");
        bool ok = root != nullptr && child != nullptr && child->SetParent(root, false);
        std::vector<std::string> lines;
        Serialization::SceneData captured;
        Serialization::SceneData loaded;
        std::string error;
        std::stringstream stream;
        if (ok)
        {
            root->GetTransform().SetLocalPosition({ 1.0f, 2.0f, 3.0f });
            child->GetTransform().SetLocalPosition({ 0.0f, 4.0f, 0.0f });
            Serialization::CaptureScene(source, captured);
            ok = Serialization::SceneSerializer::WriteText(captured, stream, error);
            stream.seekg(0);
            ok = ok && Serialization::SceneSerializer::ReadText(loaded, stream, error);
            lines.push_back(std::string("TEXT_ROUND_TRIP ") + (ok ? "OK" : "NG"));
        }
        else
        {
            lines.push_back("SCENE_SETUP NG");
        }

        ReplayEngine::Scene::Scene restored("PersistenceRestored");
        restored.CreateGameObject("StaleObject");
        Serialization::SceneLoadReport report;
        if (ok)
        {
            ok = Serialization::ApplySceneData(loaded, restored, report);
            ReplayEngine::Core::GameObject* restored_root = restored.FindGameObjectByID(root->ID());
            ReplayEngine::Core::GameObject* restored_child = restored.FindGameObjectByID(child->ID());
            // ObjectID は Scene ごとに 1 から振り直されるため、別 Scene の ID は衝突する。
            // 差し替え前のオブジェクトが消えたかは ID で確認できないので、名前と総数で確認する。
            const bool hierarchy_ok = restored_root != nullptr && restored_child != nullptr &&
                restored_root->Name() == "PersistentRoot" &&
                restored_child->Parent() == restored_root &&
                restored.FindGameObjectByName("StaleObject") == nullptr &&
                restored.GameObjectCount() == loaded.objects.size();
            ok = ok && hierarchy_ok && report.skipped_components == 0;
            lines.push_back(std::string("REPLACE_SCENE ") + (hierarchy_ok ? "OK" : "NG"));

            Serialization::SceneLoadReport second_report;
            const bool repeated = Serialization::ApplySceneData(loaded, restored, second_report) &&
                restored.GameObjectCount() == loaded.objects.size();
            ok = ok && repeated;
            lines.push_back(std::string("REPEAT_LOAD ") + (repeated ? "OK" : "NG"));
        }

        bool persistent_id_collision = false;
        {
            ReplayEngine::Scene::Scene scene_a("PersistenceCollisionSource");
            ReplayEngine::Core::GameObject* carry = scene_a.CreateGameObject("Carry");
            ReplayEngine::Core::GameObject* carry_child =
                scene_a.CreateGameObject("CarryChild");
            auto* persistent = carry != nullptr
                ? carry->AddComponent<ReplayEngine::Components::PersistentComponent>() : nullptr;
            auto* carry_link = carry != nullptr
                ? carry->AddComponent<ReplayEngine::Components::PropertyLinkComponent>() : nullptr;
            auto* child_link = carry_child != nullptr
                ? carry_child->AddComponent<ReplayEngine::Components::PropertyLinkComponent>() : nullptr;
            const bool collision_setup = carry != nullptr && carry_child != nullptr &&
                persistent != nullptr && carry_link != nullptr && child_link != nullptr &&
                carry_child->SetParent(carry, false);

            ReplayEngine::Scene::Scene scene_b_source("PersistenceCollisionDestination");
            ReplayEngine::Core::GameObject* scene_b_first =
                scene_b_source.CreateGameObject("SceneBFirst");
            ReplayEngine::Core::GameObject* scene_b_second =
                scene_b_source.CreateGameObject("SceneBSecond");
            Serialization::SceneData b_data;
            if (scene_b_first != nullptr && scene_b_second != nullptr)
            {
                Serialization::CaptureScene(scene_b_source, b_data);
            }

            if (collision_setup && scene_b_first != nullptr && scene_b_second != nullptr)
            {
                // PropertyLinkComponent の target_object で Carry から CarryChild を参照する。
                carry_link->target_object.owner = carry_child->ID();
                carry_link->target_object.component = child_link->StableID();
                carry_link->target_property = "target_min";

                auto detached = scene_a.DetachPersistentRoots();
                ReplayEngine::Scene::Scene scene_b("PersistenceCollisionRestored");
                Serialization::SceneLoadReport collision_report;
                const bool applied = Serialization::ApplySceneData(
                    b_data, scene_b, collision_report);
                if (applied) scene_b.AdoptPersistentRoots(std::move(detached));

                ReplayEngine::Core::GameObject* restored_first =
                    scene_b.FindGameObjectByName("SceneBFirst");
                ReplayEngine::Core::GameObject* restored_carry =
                    scene_b.FindGameObjectByName("Carry");
                ReplayEngine::Core::GameObject* restored_child =
                    scene_b.FindGameObjectByName("CarryChild");
                auto* restored_carry_link = restored_carry != nullptr
                    ? restored_carry->GetComponent<ReplayEngine::Components::PropertyLinkComponent>()
                    : nullptr;
                auto* restored_child_link = restored_child != nullptr
                    ? restored_child->GetComponent<ReplayEngine::Components::PropertyLinkComponent>()
                    : nullptr;

                std::unordered_set<ReplayEngine::Core::ObjectID> object_ids;
                bool unique_ids = true;
                for (std::size_t index = 0; index < scene_b.GameObjectCount(); ++index)
                {
                    ReplayEngine::Core::GameObject* object = scene_b.GameObjectAt(index);
                    if (object == nullptr || !object_ids.insert(object->ID()).second)
                    {
                        unique_ids = false;
                        break;
                    }
                }

                const bool reference_remapped = restored_carry_link != nullptr &&
                    restored_child_link != nullptr &&
                    restored_carry_link->target_object.owner == restored_child->ID() &&
                    restored_carry_link->target_object.component == restored_child_link->StableID();
                const bool count_ok = scene_b.GameObjectCount() == b_data.objects.size() + 2;
                persistent_id_collision = applied && restored_first != nullptr &&
                    restored_carry != nullptr && restored_child != nullptr && count_ok &&
                    unique_ids && reference_remapped;
            }
        }
        ok = ok && persistent_id_collision;
        lines.push_back(std::string("PERSISTENT_ID_COLLISION ") +
            (persistent_id_collision ? "OK" : "NG"));

        WriteValidationResultFile("ScenePersistence.txt",
            "REPLAY_SCENE_PERSISTENCE_VALIDATION", ok, lines);
        std::fprintf(stderr, "scene-persistence validation: RESULT %s\n", ok ? "OK" : "NG");
        return ok ? 0 : 1460;
    }
}
