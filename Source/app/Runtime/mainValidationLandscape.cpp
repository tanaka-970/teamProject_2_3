// Runtime main のうち「Landscape のヘッドレス検証」を持つ。
// Landscape topology / serialization / collision の検証関数本体はそのまま移動している。
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

        const std::filesystem::path validation_folder = ValidationFolder();
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

}
