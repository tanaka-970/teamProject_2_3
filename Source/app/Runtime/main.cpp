#include <time.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_DEBUG)
#include <dxgidebug.h>
#endif

#include "framework.h"
#include "../../../RePlayEngine/Assets/AssetDatabase.h"
#include "../../../RePlayEngine/Components/Audio/AudioListenerComponent.h"
#include "../../../RePlayEngine/Components/Audio/AudioSourceComponent.h"
#include "../../../RePlayEngine/Components/Camera/CameraComponent.h"
#include "../../../RePlayEngine/Components/Camera/CameraTargetComponent.h"
#include "../../../RePlayEngine/Components/Camera/FollowTargetComponent.h"
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
#include "../../../RePlayEngine/Components/Landscape/LandscapeComponent.h"
#include "../../../RePlayEngine/Components/Landscape/LandscapeRendererComponent.h"
#include "../../../RePlayEngine/Components/Landscape/LandscapeColliderComponent.h"
#include "../../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderAssetValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderBuiltInValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderMaterialValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderRenderValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderTextureValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderEditorValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderLightingValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderLayerValidation.h"
#include "../../../RePlayEngine/Rendering/Shaders/ShaderPassValidation.h"
#include "../../../RePlayEngine/Rendering/ShaderComposer/ShaderComposerValidation.h"
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

    const char* D3D11MessageCategoryName(D3D11_MESSAGE_CATEGORY category) noexcept
    {
        switch (category)
        {
        case D3D11_MESSAGE_CATEGORY_APPLICATION_DEFINED: return "APPLICATION_DEFINED";
        case D3D11_MESSAGE_CATEGORY_MISCELLANEOUS: return "MISCELLANEOUS";
        case D3D11_MESSAGE_CATEGORY_INITIALIZATION: return "INITIALIZATION";
        case D3D11_MESSAGE_CATEGORY_CLEANUP: return "CLEANUP";
        case D3D11_MESSAGE_CATEGORY_COMPILATION: return "COMPILATION";
        case D3D11_MESSAGE_CATEGORY_STATE_CREATION: return "STATE_CREATION";
        case D3D11_MESSAGE_CATEGORY_STATE_SETTING: return "STATE_SETTING";
        case D3D11_MESSAGE_CATEGORY_STATE_GETTING: return "STATE_GETTING";
        case D3D11_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "RESOURCE_MANIPULATION";
        case D3D11_MESSAGE_CATEGORY_EXECUTION: return "EXECUTION";
        case D3D11_MESSAGE_CATEGORY_SHADER: return "SHADER";
        default: return "UNKNOWN_CATEGORY";
        }
    }

    const char* D3D11MessageSeverityName(D3D11_MESSAGE_SEVERITY severity) noexcept
    {
        switch (severity)
        {
        case D3D11_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
        case D3D11_MESSAGE_SEVERITY_ERROR: return "ERROR";
        case D3D11_MESSAGE_SEVERITY_WARNING: return "WARNING";
        case D3D11_MESSAGE_SEVERITY_INFO: return "INFO";
        case D3D11_MESSAGE_SEVERITY_MESSAGE: return "MESSAGE";
        default: return "UNKNOWN_SEVERITY";
        }
    }

    bool TryParseUnsigned(const std::string& text, std::size_t offset,
        std::uint64_t& value) noexcept
    {
        while (offset < text.size() &&
            !std::isdigit(static_cast<unsigned char>(text[offset])))
        {
            ++offset;
        }
        if (offset >= text.size()) return false;

        std::uint64_t parsed = 0;
        while (offset < text.size() &&
            std::isdigit(static_cast<unsigned char>(text[offset])))
        {
            parsed = parsed * 10u +
                static_cast<std::uint64_t>(text[offset] - '0');
            ++offset;
        }
        value = parsed;
        return true;
    }

    struct D3D11StoredMessage
    {
        D3D11_MESSAGE_CATEGORY category{};
        D3D11_MESSAGE_SEVERITY severity{};
        D3D11_MESSAGE_ID id{};
        std::string description;
    };

    struct D3D11LiveObjectFileSummary
    {
        std::uint64_t stored_messages = 0;
        std::uint64_t readable_messages = 0;
        std::uint64_t live_object_detail_lines = 0;
        std::uint64_t live_object_summary_count = 0;
        bool live_object_summary_found = false;
        std::uint64_t live_device_lines = 0;
        std::uint64_t live_device_refcount = 0;
        bool live_device_refcount_found = false;
        std::uint64_t live_context_lines = 0;
        std::uint64_t live_debug_interface_lines = 0;
    };

    void AccumulateD3D11LiveObjectSummary(const std::string& description,
        D3D11LiveObjectFileSummary& summary)
    {
        if (description.find("Live ID3D11Device") != std::string::npos)
        {
            ++summary.live_device_lines;
            const std::size_t refcount_position = description.find("Refcount:");
            std::uint64_t parsed_refcount = 0;
            if (refcount_position != std::string::npos &&
                TryParseUnsigned(description, refcount_position + 9u,
                    parsed_refcount))
            {
                summary.live_device_refcount = parsed_refcount;
                summary.live_device_refcount_found = true;
            }
            return;
        }

        if (description.find("Live ID3D11DeviceContext") != std::string::npos)
        {
            ++summary.live_context_lines;
            return;
        }

        if (description.find("Live ID3D11Debug") != std::string::npos ||
            description.find("Live ID3D11InfoQueue") != std::string::npos)
        {
            ++summary.live_debug_interface_lines;
            return;
        }

        const bool typed_live_object_line =
            description.find("Live ") != std::string::npos &&
            description.find(" at ") != std::string::npos &&
            description.find("Refcount:") != std::string::npos;
        if (description.find("Live Object at") != std::string::npos ||
            typed_live_object_line)
        {
            ++summary.live_object_detail_lines;
            return;
        }

        if (description.find("Refcount") != std::string::npos)
        {
            return;
        }

        const std::size_t live_position = description.find("Live");
        const std::size_t object_position = description.find("Object");
        const std::size_t colon_position =
            object_position != std::string::npos ?
            description.find(':', object_position) : std::string::npos;
        if (live_position == std::string::npos ||
            object_position == std::string::npos ||
            colon_position == std::string::npos)
        {
            return;
        }

        std::uint64_t parsed_count = 0;
        if (TryParseUnsigned(description, colon_position + 1u, parsed_count))
        {
            summary.live_object_summary_count = parsed_count;
            summary.live_object_summary_found = true;
        }
    }

    D3D11LiveObjectFileSummary WriteD3D11LiveObjectReportFile(
        ID3D11InfoQueue* info_queue, bool report_available,
        HRESULT report_result)
    {
        const std::filesystem::path validation_folder =
            std::filesystem::path("Saved") / "Validation";
        std::error_code directory_error;
        std::filesystem::create_directories(validation_folder, directory_error);

        D3D11LiveObjectFileSummary summary{};
        std::vector<D3D11StoredMessage> messages;
        HRESULT queue_read_result = info_queue != nullptr ? S_OK : E_NOINTERFACE;
        if (info_queue)
        {
            summary.stored_messages =
                info_queue->GetNumStoredMessagesAllowedByRetrievalFilter();
            messages.reserve(static_cast<std::size_t>(
                (std::min)(summary.stored_messages, static_cast<std::uint64_t>(4096))));
            for (std::uint64_t index = 0; index < summary.stored_messages; ++index)
            {
                SIZE_T message_size = 0;
                queue_read_result = info_queue->GetMessage(index, nullptr, &message_size);
                if (FAILED(queue_read_result) || message_size == 0) break;

                std::vector<char> storage(message_size);
                D3D11_MESSAGE* message =
                    reinterpret_cast<D3D11_MESSAGE*>(storage.data());
                queue_read_result = info_queue->GetMessage(index, message, &message_size);
                if (FAILED(queue_read_result)) break;

                D3D11StoredMessage stored{};
                stored.category = message->Category;
                stored.severity = message->Severity;
                stored.id = message->ID;
                if (message->pDescription != nullptr &&
                    message->DescriptionByteLength > 0)
                {
                    stored.description.assign(message->pDescription,
                        message->DescriptionByteLength);
                    while (!stored.description.empty() &&
                        stored.description.back() == '\0')
                    {
                        stored.description.pop_back();
                    }
                }
                AccumulateD3D11LiveObjectSummary(stored.description, summary);
                messages.push_back(std::move(stored));
                ++summary.readable_messages;
            }
        }

        const std::filesystem::path report_path =
            validation_folder / "D3D11LiveObjects.txt";
        std::ofstream report(report_path, std::ios::binary | std::ios::trunc);
        if (report)
        {
            report << "REPLAY_D3D11_LIVE_OBJECT_REPORT 1\n";
            report << "D3D11_DEBUG_AVAILABLE " << (report_available ? 1 : 0) << '\n';
            report << "D3D11_INFO_QUEUE_AVAILABLE " << (info_queue != nullptr ? 1 : 0) << '\n';
            report << "REPORT_HRESULT 0x" << std::hex << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(report_result)
                << std::dec << std::setfill(' ') << '\n';
            report << "INFO_QUEUE_READ_HRESULT 0x" << std::hex << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(queue_read_result)
                << std::dec << std::setfill(' ') << '\n';
            report << "INFO_QUEUE_STORED_MESSAGES " << summary.stored_messages << '\n';
            report << "INFO_QUEUE_READABLE_MESSAGES " << summary.readable_messages << '\n';
            report << "LIVE_OBJECT_DETAIL_LINES " << summary.live_object_detail_lines << '\n';
            report << "LIVE_OBJECT_SUMMARY_COUNT ";
            if (summary.live_object_summary_found) report << summary.live_object_summary_count;
            else report << "UNKNOWN";
            report << '\n';
            report << "LIVE_DEVICE_LINES " << summary.live_device_lines << '\n';
            report << "LIVE_DEVICE_REFCOUNT ";
            if (summary.live_device_refcount_found) report << summary.live_device_refcount;
            else report << "UNKNOWN";
            report << '\n';
            report << "LIVE_CONTEXT_LINES " << summary.live_context_lines << '\n';
            report << "LIVE_DEBUG_INTERFACE_LINES "
                << summary.live_debug_interface_lines;
            report << "\n\n";

            for (std::size_t index = 0; index < messages.size(); ++index)
            {
                const D3D11StoredMessage& message = messages[index];
                report << '[' << index << "] "
                    << D3D11MessageSeverityName(message.severity) << ' '
                    << D3D11MessageCategoryName(message.category)
                    << " #" << static_cast<int>(message.id) << '\n'
                    << message.description << "\n\n";
            }
        }

        if (info_queue) info_queue->ClearStoredMessages();
        return summary;
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
        if (!data.Initialize(17, 17, 1.0f))
        {
            std::fprintf(stderr, "Landscape v2 initialization failed\n");
            return 20;
        }
        if (data.VertexCount() != 289 || data.FaceCount() != 512 || data.Chunks().size() != 1)
        {
            std::fprintf(stderr, "Landscape v2 base topology mismatch: V=%zu F=%zu chunks=%zu\n",
                data.VertexCount(), data.FaceCount(), data.Chunks().size());
            return 21;
        }

        LandscapeRenderer renderer;
        LandscapeCollision collision;
        if (renderer.UpdateDirtyChunks(data) != 1 || collision.UpdateDirtyChunks(data) != 1)
        {
            std::fprintf(stderr, "Landscape v2 initial render/collision build failed\n");
            return 22;
        }

        LandscapeBrush brush;
        brush.radius = 2.5f;
        brush.strength = 2.0f;
        brush.falloff = 0.35f;
        brush.direction = LandscapeSculptDirection::LocalY;
        LandscapeEditorTool tool;
        if (!tool.BeginStroke(data, LandscapeBrushMode::Raise, brush) ||
            !tool.ApplySample({ 8.0f, 0.0f, 8.0f }, 1.0f))
        {
            std::fprintf(stderr, "Landscape v2 sculpt failed\n");
            return 23;
        }
        std::unique_ptr<LandscapeUndoCommand> stroke = tool.EndStroke();
        const float raised_height = data.HeightAt(8, 8);
        if (stroke == nullptr || stroke->ChangedSampleCount() == 0 || raised_height <= 0.0f)
        {
            std::fprintf(stderr, "Landscape v2 sculpt produced no undo data\n");
            return 24;
        }
        stroke->Undo(data);
        if (std::fabs(data.HeightAt(8, 8)) > 0.00001f)
        {
            std::fprintf(stderr, "Landscape v2 sculpt undo failed\n");
            return 25;
        }
        stroke->Redo(data);
        if (std::fabs(data.HeightAt(8, 8) - raised_height) > 0.00001f)
        {
            std::fprintf(stderr, "Landscape v2 sculpt redo failed\n");
            return 26;
        }

        // Arbitrary topology: Subdivide / Inset / Extrude / Cut / Tunnel.
        const std::size_t base_faces = data.FaceCount();
        if (!data.SubdivideFace(0) || data.FaceCount() != base_faces + 3)
        {
            std::fprintf(stderr, "Landscape face subdivide failed\n");
            return 27;
        }
        if (!data.InsetFace(1, 0.25f) || !data.ExtrudeFace(2, 1.25f))
        {
            std::fprintf(stderr, "Landscape inset/extrude failed\n");
            return 28;
        }
        const std::size_t before_cut = data.FaceCount();
        if (!data.DeleteFace(3) || data.FaceCount() + 1 != before_cut)
        {
            std::fprintf(stderr, "Landscape Cut Hole failed\n");
            return 29;
        }

        // Edge Bridge は Face 操作とは別 fresh mesh で、離れた2 edgeを接続できることを確認。
        LandscapeData bridge;
        if (!bridge.Initialize(3, 3, 1.0f) ||
            !bridge.BridgeEdges(0, 1, 3, 4) || bridge.FaceCount() != 10)
        {
            std::fprintf(stderr, "Landscape Edge Bridge failed\n");
            return 30;
        }

        // 洞窟/Tunnel は別の fresh grid で検証。入口 face を消し、同一 XZ 制約を持たない
        // arbitrary mesh が生成されることを vertex/face 増加と 3D raycast で確認する。
        LandscapeData tunnel;
        if (!tunnel.Initialize(5, 5, 1.0f)) return 31;
        const std::size_t tunnel_vertices_before = tunnel.VertexCount();
        const std::size_t tunnel_faces_before = tunnel.FaceCount();
        if (!tunnel.CreateTunnelFromFace(12, 4.0f, 4, 0.8f) ||
            tunnel.VertexCount() <= tunnel_vertices_before ||
            tunnel.FaceCount() <= tunnel_faces_before)
        {
            std::fprintf(stderr, "Landscape Cave/Tunnel topology generation failed\n");
            return 32;
        }
        bool has_below_surface = false;
        for (const LandscapeVertex& vertex : tunnel.Vertices())
        {
            if (vertex.position.y < -0.1f) { has_below_surface = true; break; }
        }
        if (!has_below_surface)
        {
            std::fprintf(stderr, "Landscape Tunnel did not create internal 3D geometry\n");
            return 33;
        }

        LandscapeRayHit ray_hit{};
        if (!tunnel.Raycast({ 2.0f, 5.0f, 2.0f }, { 0.0f, -1.0f, 0.0f },
            100.0f, ray_hit) || !ray_hit.hit)
        {
            std::fprintf(stderr, "Landscape arbitrary mesh raycast failed\n");
            return 34;
        }

        // Renderer/Collision cache は topology revision 後も更新できる。
        LandscapeRenderer topology_renderer;
        LandscapeCollision topology_collision;
        if (topology_renderer.UpdateDirtyChunks(tunnel) != 1 ||
            topology_collision.UpdateDirtyChunks(tunnel) != 1)
        {
            std::fprintf(stderr, "Landscape topology render/collision rebuild failed\n");
            return 35;
        }

        const std::filesystem::path validation_folder =
            std::filesystem::path("Saved") / "Validation";
        std::error_code directory_error;
        std::filesystem::create_directories(validation_folder, directory_error);
        const std::filesystem::path output_path =
            validation_folder / "LandscapeV2.replaylandscape";
        std::string error;
        if (!tunnel.Save(output_path, error))
        {
            std::fprintf(stderr, "Landscape v2 save failed: %s\n", error.c_str());
            return 36;
        }
        LandscapeData loaded;
        if (!LandscapeData::Load(output_path, loaded, error) ||
            loaded.VertexCount() != tunnel.VertexCount() ||
            loaded.FaceCount() != tunnel.FaceCount())
        {
            std::fprintf(stderr, "Landscape v2 reload failed: %s\n", error.c_str());
            return 37;
        }

        // v1 height-field file migration -> v2 arbitrary mesh.
        const std::filesystem::path v1_path = validation_folder / "LandscapeV1Migration.replaylandscape";
        {
            std::ofstream legacy(v1_path, std::ios::binary | std::ios::trunc);
            legacy << "REPLAY_LANDSCAPE 1\n3 3 1\n";
            for (int index = 0; index < 9; ++index)
                legacy << (index == 4 ? 2.0f : 0.0f) << (index + 1 == 9 ? '\n' : ' ');
        }
        LandscapeData migrated;
        if (!LandscapeData::Load(v1_path, migrated, error) ||
            migrated.VertexCount() != 9 || migrated.FaceCount() != 8 ||
            std::fabs(migrated.HeightAt(1, 1) - 2.0f) > 0.00001f)
        {
            std::fprintf(stderr, "Landscape v1 -> v2 migration failed: %s\n", error.c_str());
            return 38;
        }

        // Component-oriented Scene round trip. Ground is an ordinary GameObject with
        // Landscape + Renderer + Collider; custom mesh data must survive SceneData.
        ReplayEngine::Core::RegisterBuiltInComponents();
        ReplayEngine::Scene::Scene scene("LandscapeComponentValidation");
        auto* ground = scene.CreateGameObject("Ground");
        if (ground == nullptr) return 39;
        auto* component = ground->AddComponent<ReplayEngine::Components::LandscapeComponent>();
        auto* render_component = ground->AddComponent<ReplayEngine::Components::LandscapeRendererComponent>();
        auto* collider_component = ground->AddComponent<ReplayEngine::Components::LandscapeColliderComponent>();
        if (component == nullptr || render_component == nullptr || collider_component == nullptr ||
            !component->Data().InitializeMesh(loaded.Vertices(), loaded.Indices(),
                loaded.CellSize(), loaded.Width(), loaded.Height()))
        {
            std::fprintf(stderr, "Landscape Component composition failed\n");
            return 40;
        }
        collider_component->RefreshGeometryIfChanged();
        if (!collider_component->ReadyForQuery() || collider_component->Cooked() == nullptr ||
            !collider_component->Cooked()->Valid())
        {
            std::fprintf(stderr, "Landscape Collider spatial cook failed\n");
            return 41;
        }
        namespace Serialization = ReplayEngine::Scene::Serialization;
        Serialization::SceneData scene_data;
        Serialization::CaptureScene(scene, scene_data);
        ReplayEngine::Scene::Scene restored("RestoredLandscape");
        Serialization::SceneLoadReport report;
        if (!Serialization::ApplySceneData(scene_data, restored, report))
        {
            std::fprintf(stderr, "Landscape SceneData apply failed\n");
            return 42;
        }
        auto* restored_ground = restored.FindGameObjectByName("Ground");
        auto* restored_landscape = restored_ground != nullptr
            ? restored_ground->GetComponent<ReplayEngine::Components::LandscapeComponent>() : nullptr;
        if (restored_landscape == nullptr ||
            restored_landscape->Data().VertexCount() != loaded.VertexCount() ||
            restored_landscape->Data().FaceCount() != loaded.FaceCount() ||
            restored_ground->GetComponent<ReplayEngine::Components::LandscapeRendererComponent>() == nullptr ||
            restored_ground->GetComponent<ReplayEngine::Components::LandscapeColliderComponent>() == nullptr)
        {
            std::fprintf(stderr, "Landscape Component Scene round-trip mismatch\n");
            return 43;
        }

        std::fprintf(stderr,
            "Landscape v2 OK: arbitrary mesh, sculpt+undo, topology+bridge, cave/tunnel, raycast, spatial collision cook, save/reload, v1 migration, Component Scene round-trip OK\n");
        return 0;
    }


#if defined(_DEBUG)
    struct DXGILiveObjectFileSummary
    {
        std::uint64_t stored_messages = 0;
        std::uint64_t readable_messages = 0;
        std::uint64_t live_object_lines = 0;
        std::uint64_t live_d3d11_device_lines = 0;
    };

    void AccumulateDXGILiveObjectSummary(const std::string& description,
        DXGILiveObjectFileSummary& summary) noexcept
    {
        // DXGI の ReportLiveObjects は Description に "Live ..." を出す。
        // Summary 行や内部メッセージを数えず、実体の Live 行だけを合格判定に使う。
        if (description.find("Live ") == std::string::npos) return;
        ++summary.live_object_lines;
        if (description.find("Live ID3D11Device") != std::string::npos)
        {
            ++summary.live_d3d11_device_lines;
        }
    }

    DXGILiveObjectFileSummary WriteDXGILiveObjectReportFile(
        IDXGIInfoQueue* info_queue, bool report_available, HRESULT report_result)
    {
        const std::filesystem::path validation_folder =
            std::filesystem::path("Saved") / "Validation";
        std::error_code directory_error;
        std::filesystem::create_directories(validation_folder, directory_error);

        DXGILiveObjectFileSummary summary{};
        std::vector<std::string> messages;
        HRESULT queue_read_result = info_queue != nullptr ? S_OK : E_NOINTERFACE;
        if (info_queue != nullptr)
        {
            summary.stored_messages =
                info_queue->GetNumStoredMessagesAllowedByRetrievalFilters(DXGI_DEBUG_ALL);
            messages.reserve(static_cast<std::size_t>(
                (std::min)(summary.stored_messages, static_cast<std::uint64_t>(4096))));
            for (std::uint64_t index = 0; index < summary.stored_messages; ++index)
            {
                SIZE_T message_size = 0;
                queue_read_result = info_queue->GetMessage(
                    DXGI_DEBUG_ALL, index, nullptr, &message_size);
                if (FAILED(queue_read_result) || message_size == 0) break;

                std::vector<unsigned char> storage(message_size);
                DXGI_INFO_QUEUE_MESSAGE* message =
                    reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(storage.data());
                queue_read_result = info_queue->GetMessage(
                    DXGI_DEBUG_ALL, index, message, &message_size);
                if (FAILED(queue_read_result)) break;

                std::string description;
                if (message->pDescription != nullptr &&
                    message->DescriptionByteLength > 0)
                {
                    description.assign(message->pDescription,
                        message->DescriptionByteLength);
                    while (!description.empty() && description.back() == '\0')
                    {
                        description.pop_back();
                    }
                }
                AccumulateDXGILiveObjectSummary(description, summary);
                messages.push_back(std::move(description));
                ++summary.readable_messages;
            }
        }

        const std::filesystem::path report_path =
            validation_folder / "DXGILiveObjects.txt";
        std::ofstream report(report_path, std::ios::binary | std::ios::trunc);
        if (report)
        {
            report << "REPLAY_DXGI_LIVE_OBJECT_REPORT 1\n";
            report << "DXGI_DEBUG_AVAILABLE " << (report_available ? 1 : 0) << '\n';
            report << "DXGI_INFO_QUEUE_AVAILABLE " << (info_queue != nullptr ? 1 : 0) << '\n';
            report << "REPORT_HRESULT 0x" << std::hex << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(report_result)
                << std::dec << std::setfill(' ') << '\n';
            report << "INFO_QUEUE_READ_HRESULT 0x" << std::hex << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(queue_read_result)
                << std::dec << std::setfill(' ') << '\n';
            report << "DXGI_INFO_QUEUE_STORED_MESSAGES " << summary.stored_messages << '\n';
            report << "DXGI_INFO_QUEUE_READABLE_MESSAGES " << summary.readable_messages << '\n';
            report << "DXGI_LIVE_OBJECT_LINES " << summary.live_object_lines << '\n';
            report << "DXGI_LIVE_D3D11_DEVICE_LINES "
                << summary.live_d3d11_device_lines << "\n\n";
            for (std::size_t index = 0; index < messages.size(); ++index)
            {
                report << '[' << index << "] " << messages[index] << '\n';
            }
        }
        if (info_queue != nullptr) info_queue->ClearStoredMessages(DXGI_DEBUG_ALL);
        return summary;
    }

    bool AcquireDXGIDebugInterfaces(
        Microsoft::WRL::ComPtr<IDXGIDebug1>& debug,
        Microsoft::WRL::ComPtr<IDXGIInfoQueue>& info_queue,
        HMODULE& module) noexcept
    {
        // Debug interface は Runtime の所有物にしない。
        // Device を作る前からプロセス全体を追跡し、Device 解放後に検査するため
        // WinMain のローカル寿命だけで保持する。
        // GetProcAddress 用に自分の module ref を 1 本持ち、最終レポート後に
        // 必ず FreeLibrary する。GetModuleHandle の借用参照に依存すると、
        // 起動順によって関数ポインタの寿命が変わるため。
        module = ::LoadLibraryW(L"dxgi.dll");
        if (module == nullptr) return false;

        using GetDebugInterface1Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
        const auto get_debug_interface = reinterpret_cast<GetDebugInterface1Fn>(
            ::GetProcAddress(module, "DXGIGetDebugInterface1"));
        if (get_debug_interface == nullptr)
        {
            ::FreeLibrary(module);
            module = nullptr;
            return false;
        }

        debug.Reset();
        info_queue.Reset();
        HRESULT result = get_debug_interface(0, __uuidof(IDXGIDebug1),
            reinterpret_cast<void**>(debug.GetAddressOf()));
        if (FAILED(result) || !debug)
        {
            ::FreeLibrary(module);
            module = nullptr;
            return false;
        }

        result = get_debug_interface(0, __uuidof(IDXGIInfoQueue),
            reinterpret_cast<void**>(info_queue.GetAddressOf()));
        if (FAILED(result) || !info_queue)
        {
            debug.Reset();
            ::FreeLibrary(module);
            module = nullptr;
            return false;
        }

        info_queue->SetMessageCountLimit(DXGI_DEBUG_ALL, UINT64_MAX);
        info_queue->PushEmptyStorageFilter(DXGI_DEBUG_ALL);
        info_queue->ClearStoredMessages(DXGI_DEBUG_ALL);
        return true;
    }
#endif

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
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "target_offset") != nullptr,
            "CameraTargetComponent exposes target_offset");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "look_at_offset") != nullptr,
            "CameraTargetComponent exposes look_at_offset");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "priority") != nullptr,
            "CameraTargetComponent exposes priority");
        expect(Reflection::PropertyRegistry::Find(camera_target_type,
            "field_of_view_degrees") == nullptr,
            "CameraTargetComponent no longer owns field_of_view_degrees");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "near_clip") == nullptr,
            "CameraTargetComponent no longer owns near_clip");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "far_clip") == nullptr,
            "CameraTargetComponent no longer owns far_clip");
        expect(Reflection::PropertyRegistry::Find(camera_target_type, "follow_distance") == nullptr,
            "CameraTargetComponent no longer owns follow_distance");

        const Core::ComponentTypeID camera_type =
            Components::CameraComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(camera_type, "projection_mode") != nullptr,
            "CameraComponent exposes projection_mode");
        expect(Reflection::PropertyRegistry::Find(camera_type,
            "field_of_view_degrees") != nullptr,
            "CameraComponent exposes field_of_view_degrees");
        expect(Reflection::PropertyRegistry::Find(camera_type, "orthographic_size") != nullptr,
            "CameraComponent exposes orthographic_size");
        expect(Reflection::PropertyRegistry::Find(camera_type, "near_clip") != nullptr,
            "CameraComponent exposes near_clip");
        expect(Reflection::PropertyRegistry::Find(camera_type, "far_clip") != nullptr,
            "CameraComponent exposes far_clip");
        expect(Reflection::PropertyRegistry::Find(camera_type, "priority") != nullptr,
            "CameraComponent exposes priority");
        expect(Reflection::PropertyRegistry::Find(camera_type, "viewport_rect") != nullptr,
            "CameraComponent exposes viewport_rect");

        const Core::ComponentTypeID follow_type =
            Components::FollowTargetComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(follow_type, "follow_distance") != nullptr,
            "FollowTargetComponent exposes follow_distance");
        expect(Reflection::PropertyRegistry::Find(follow_type, "follow_height") != nullptr,
            "FollowTargetComponent exposes follow_height");
        expect(Reflection::PropertyRegistry::Find(follow_type, "follow_lag") != nullptr,
            "FollowTargetComponent exposes follow_lag");
        expect(Reflection::PropertyRegistry::Find(follow_type,
            "rotation_input_enabled") != nullptr,
            "FollowTargetComponent exposes rotation_input_enabled");

        const Core::ComponentTypeID listener_type =
            Components::AudioListenerComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(listener_type, "priority") != nullptr,
            "AudioListenerComponent exposes priority");

        const Core::ComponentTypeID audio_source_type =
            Components::AudioSourceComponent::StaticTypeID();
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "clip_path") != nullptr,
            "AudioSourceComponent exposes clip_path");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "loop") != nullptr,
            "AudioSourceComponent exposes loop");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "volume") != nullptr,
            "AudioSourceComponent exposes volume");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "pitch") != nullptr,
            "AudioSourceComponent exposes pitch");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "play_on_start") != nullptr,
            "AudioSourceComponent exposes play_on_start");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "spatial") != nullptr,
            "AudioSourceComponent exposes spatial");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "min_distance") != nullptr,
            "AudioSourceComponent exposes min_distance");
        expect(Reflection::PropertyRegistry::Find(audio_source_type, "max_distance") != nullptr,
            "AudioSourceComponent exposes max_distance");

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

        Core::GameObject* camera_low = world.CreateGameObject("LowPriorityCamera");
        Core::GameObject* camera_high = world.CreateGameObject("HighPriorityCamera");
        expect(camera_low != nullptr && camera_high != nullptr,
            "camera objects can be created");
        if (camera_low == nullptr || camera_high == nullptr)
        {
            return first_failure != 0 ? first_failure : 862;
        }

        auto* low_camera = camera_low->AddComponent<Components::CameraComponent>();
        auto* high_camera = camera_high->AddComponent<Components::CameraComponent>();
        expect(low_camera != nullptr && high_camera != nullptr,
            "CameraComponent can be attached");
        if (low_camera == nullptr || high_camera == nullptr)
        {
            return first_failure != 0 ? first_failure : 863;
        }

        low_camera->priority = 2;
        high_camera->priority = 9;
        Components::CameraSelection camera_selection =
            Components::ResolveActiveCameraSelection(world);
        expect(camera_selection.object == camera_high &&
            camera_selection.component == high_camera,
            "highest priority active CameraComponent is selected");
        high_camera->SetEnabled(false);
        camera_selection = Components::ResolveActiveCameraSelection(world);
        expect(camera_selection.object == camera_low &&
            camera_selection.component == low_camera,
            "disabled CameraComponent falls back to next priority");

        camera_low->GetTransform().SetLocalPosition({ 1.0f, 2.0f, 3.0f });
        const DirectX::XMFLOAT3 eye = low_camera->EyePosition();
        expect(close(eye.x, 1.0f) && close(eye.y, 2.0f) && close(eye.z, 3.0f),
            "CameraComponent eye position comes from Transform");

        camera_low->GetTransform().SetLocalRotationEuler(
            { 0.0f, DirectX::XM_PIDIV2, 0.0f });
        const DirectX::XMFLOAT3 forward = low_camera->Forward();
        expect(close(forward.x, 1.0f) && close(forward.z, 0.0f),
            "CameraComponent forward direction comes from Transform rotation");

        DirectX::XMFLOAT4X4 default_projection{};
        DirectX::XMStoreFloat4x4(&default_projection,
            low_camera->ProjectionMatrix(16.0f / 9.0f));

        low_camera->field_of_view_degrees = 30.0f;
        low_camera->near_clip = 0.5f;
        low_camera->far_clip = 200.0f;
        DirectX::XMFLOAT4X4 narrow_projection{};
        DirectX::XMStoreFloat4x4(&narrow_projection,
            low_camera->ProjectionMatrix(16.0f / 9.0f));

        expect(narrow_projection._22 > default_projection._22,
            "field_of_view_degrees rebuilds the Runtime Camera projection");
        const float expected_33 = 200.0f / (200.0f - 0.5f);
        const float expected_43 = -(0.5f * 200.0f) / (200.0f - 0.5f);
        expect(close(narrow_projection._33, expected_33),
            "near_clip/far_clip update projection depth scale");
        expect(close(narrow_projection._43, expected_43),
            "near_clip/far_clip update projection depth offset");

        DirectX::XMFLOAT4X4 resized_projection{};
        DirectX::XMStoreFloat4x4(&resized_projection,
            low_camera->ProjectionMatrix(4.0f / 3.0f));
        expect(close(resized_projection._22, narrow_projection._22),
            "projection preserves vertical field_of_view_degrees when aspect changes");
        expect(!close(resized_projection._11, narrow_projection._11),
            "projection reapplies the new aspect ratio");

        low_camera->projection_mode =
            static_cast<int>(Components::CameraProjectionMode::Orthographic);
        low_camera->orthographic_size = 20.0f;
        DirectX::XMFLOAT4X4 orthographic_projection{};
        DirectX::XMStoreFloat4x4(&orthographic_projection,
            low_camera->ProjectionMatrix(2.0f));
        expect(close(orthographic_projection._11, 2.0f / 40.0f),
            "orthographic projection uses orthographic_size and aspect width");
        expect(close(orthographic_projection._22, 2.0f / 20.0f),
            "orthographic projection uses orthographic_size height");

        Core::GameObject* listener_low = world.CreateGameObject("LowPriorityListener");
        Core::GameObject* listener_high = world.CreateGameObject("HighPriorityListener");
        expect(listener_low != nullptr && listener_high != nullptr,
            "audio listener objects can be created");
        if (listener_low == nullptr || listener_high == nullptr)
        {
            return first_failure != 0 ? first_failure : 864;
        }

        auto* low_listener =
            listener_low->AddComponent<Components::AudioListenerComponent>();
        auto* high_listener =
            listener_high->AddComponent<Components::AudioListenerComponent>();
        expect(low_listener != nullptr && high_listener != nullptr,
            "AudioListenerComponent can be attached");
        if (low_listener == nullptr || high_listener == nullptr)
        {
            return first_failure != 0 ? first_failure : 865;
        }

        low_listener->priority = 3;
        high_listener->priority = 7;
        Components::AudioListenerSelection listener_selection =
            Components::ResolveAudioListenerSelection(world);
        expect(listener_selection.object == listener_high &&
            listener_selection.component == high_listener,
            "highest priority active AudioListenerComponent is selected");

        listener_low->GetTransform().SetLocalPosition({ -1.0f, 4.0f, 8.0f });
        const DirectX::XMFLOAT3 listener_position = low_listener->Position();
        expect(close(listener_position.x, -1.0f) &&
            close(listener_position.y, 4.0f) &&
            close(listener_position.z, 8.0f),
            "AudioListenerComponent position comes from Transform");

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

        // シェーダ基盤。フェーズ 11（照明モデルをShader Asset宣言へ分離）。
        if (command == "--validate-shader-lighting")
        {
            return ReplayEngine::Rendering::Validation::RunShaderLightingValidation();
        }

        // シェーダ基盤。フェーズ 6（Material -> Catalog -> Render binding）。
        if (command == "--validate-shader-render")
        {
            return ReplayEngine::Rendering::Validation::RunShaderRenderValidation();
        }

        // シェーダ基盤。フェーズ 12（Texture AssetGUID / t40+ / default）。
        if (command == "--validate-shader-texture")
        {
            return ReplayEngine::Rendering::Validation::RunShaderTextureValidation();
        }

        // シェーダ基盤。フェーズ 7（Shader Picker / Schema Inspector / 保存保持）。
        if (command == "--validate-shader-editor")
        {
            return ReplayEngine::Rendering::Validation::RunShaderEditorValidation();
        }

        // シェーダ基盤。フェーズ 10（Layer Shader Asset / GUID Stack）。
        if (command == "--validate-shader-layer")
        {
            return ReplayEngine::Rendering::Validation::RunShaderLayerValidation();
        }

        // シェーダ基盤。フェーズ 16（Material Layer / Shader-owned Pass 分離）。
        if (command == "--validate-shader-pass")
        {
            return ReplayEngine::Rendering::Validation::RunShaderPassValidation();
        }

        // Shader Composer v1 (graph save/load -> HLSL -> normal ShaderAsset compile).
        if (command == "--validate-shader-composer")
        {
            return ReplayEngine::Rendering::Validation::RunShaderComposerValidation();
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
	wcex.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = APPLICATION_NAME;
	wcex.hIconSm = 0;
	RegisterClassExW(&wcex);

	RECT rc{ 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindowExW(0, APPLICATION_NAME, L"", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
		NULL, NULL, instance, NULL);

    int exit_code = 0;
    Microsoft::WRL::ComPtr<ID3D11Debug> d3d11_debug;
    Microsoft::WRL::ComPtr<ID3D11InfoQueue> d3d11_info_queue;
    D3D11LiveObjectFileSummary d3d11_live_report_summary{};
    HRESULT d3d11_live_report_result = E_NOINTERFACE;
    bool d3d11_live_report_available = false;
#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<IDXGIDebug1> dxgi_debug;
    Microsoft::WRL::ComPtr<IDXGIInfoQueue> dxgi_info_queue;
    HMODULE dxgi_debug_module = nullptr;
    DXGILiveObjectFileSummary dxgi_live_report_summary{};
    HRESULT dxgi_live_report_result = E_NOINTERFACE;
    const bool dxgi_live_report_available =
        AcquireDXGIDebugInterfaces(dxgi_debug, dxgi_info_queue, dxgi_debug_module);
#endif
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
	    exit_code = application.run(
            automated_smoke_test_frames > 0 ? SW_HIDE : cmd_show);
        d3d11_debug = application.acquire_d3d11_debug();
        d3d11_info_queue = application.acquire_d3d11_info_queue();
    }

    // ReportLiveDeviceObjects が出す行だけを測りたいので、直前に古い警告を捨てる。
    // 出力後は WriteD3D11LiveObjectReportFile が全件を書いてから Clear する。
    if (d3d11_info_queue) d3d11_info_queue->ClearStoredMessages();
    if (d3d11_debug)
    {
        d3d11_live_report_available = true;
        d3d11_live_report_result = d3d11_debug->ReportLiveDeviceObjects(
            D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
        d3d11_live_report_summary = WriteD3D11LiveObjectReportFile(
            d3d11_info_queue.Get(), d3d11_live_report_available,
            d3d11_live_report_result);
        std::fprintf(stderr, "D3D11 Live Object Report: %s (0x%08lx)\n",
            SUCCEEDED(d3d11_live_report_result) ? "completed" : "failed",
            static_cast<unsigned long>(d3d11_live_report_result));
        std::fprintf(stderr,
            "D3D11 Live Object Details: %llu lines, summary ",
            static_cast<unsigned long long>(
                d3d11_live_report_summary.live_object_detail_lines));
        if (d3d11_live_report_summary.live_object_summary_found)
        {
            std::fprintf(stderr, "%llu",
                static_cast<unsigned long long>(
                    d3d11_live_report_summary.live_object_summary_count));
        }
        else
        {
            std::fprintf(stderr, "unknown");
        }
        std::fprintf(stderr,
            " (%llu info queue messages, Saved/Validation/D3D11LiveObjects.txt)\n",
            static_cast<unsigned long long>(
                d3d11_live_report_summary.readable_messages));
        if (FAILED(d3d11_live_report_result) && exit_code == 0) exit_code = 73;
    }
    else
    {
        d3d11_live_report_summary = WriteD3D11LiveObjectReportFile(
            d3d11_info_queue.Get(), d3d11_live_report_available,
            d3d11_live_report_result);
    }
    d3d11_info_queue.Reset();
    d3d11_debug.Reset();

#if defined(_DEBUG)
    // D3D11 Debug 自身が Device を生かす参照を手放したあとで、
    // プロセス全体を追跡する DXGI から最終確認する。
    if (dxgi_live_report_available && dxgi_debug && dxgi_info_queue)
    {
        dxgi_info_queue->ClearStoredMessages(DXGI_DEBUG_ALL);
        dxgi_live_report_result = dxgi_debug->ReportLiveObjects(
            DXGI_DEBUG_ALL, static_cast<DXGI_DEBUG_RLO_FLAGS>(
                DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
        dxgi_live_report_summary = WriteDXGILiveObjectReportFile(
            dxgi_info_queue.Get(), true, dxgi_live_report_result);
        std::fprintf(stderr,
            "DXGI Live Object Report: %llu live lines (%s)\n",
            static_cast<unsigned long long>(dxgi_live_report_summary.live_object_lines),
            SUCCEEDED(dxgi_live_report_result) ? "completed" : "failed");
        if (FAILED(dxgi_live_report_result) && exit_code == 0) exit_code = 74;
        if (shutdown_regression_requested &&
            dxgi_live_report_summary.live_object_lines != 0 && exit_code == 0)
        {
            exit_code = 75;
        }
    }
    else
    {
        dxgi_live_report_summary = WriteDXGILiveObjectReportFile(
            dxgi_info_queue.Get(), false, dxgi_live_report_result);
    }
    dxgi_info_queue.Reset();
    dxgi_debug.Reset();
    if (dxgi_debug_module != nullptr)
    {
        ::FreeLibrary(dxgi_debug_module);
        dxgi_debug_module = nullptr;
    }
#endif

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
            report << std::dec << std::setfill(' ');
            report << "D3D11_LIVE_OBJECT_DETAIL_LINES "
                << d3d11_live_report_summary.live_object_detail_lines << '\n';
            report << "D3D11_LIVE_OBJECT_SUMMARY_COUNT ";
            if (d3d11_live_report_summary.live_object_summary_found)
            {
                report << d3d11_live_report_summary.live_object_summary_count;
            }
            else
            {
                report << "UNKNOWN";
            }
            report << '\n';
        }
    }
	if (SUCCEEDED(com_result)) CoUninitialize();
	return exit_code;
}
