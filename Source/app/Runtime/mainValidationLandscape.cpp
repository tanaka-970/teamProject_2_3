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
#include "../../../RePlayEngine/Scene/Services/SceneCollisionWorld.h"
#include "../../../RePlayEngine/Physics/CollisionLayers.h"
#include "../../../RePlayEngine/Object/Registry/BuiltInComponents.h"
#include "../../../RePlayEngine/Rendering/Materials/MaterialAsset.h"
#include "../../../RePlayEngine/Scene/Runtime/Scene.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneData.h"
#include "../../../RePlayEngine/Scene/Serialization/PrefabSerializer.h"
#include "../../../RePlayEngine/Scene/Serialization/SceneSerializer.h"


#include "mainValidationLandscapeRegression.inl"
#include "mainValidationLandscapeEditor.inl"

namespace ReplayEngine::Runtime::Detail
{
    int RunHeadlessLandscapeValidation(const char* command_line)
    {
        std::istringstream arguments(command_line != nullptr ? command_line : "");
        std::string command;
        if (!(arguments >> command)) return -1;
        if (command == "--validate-landscape-editor")
            return ReplayEngine::Editor::LandscapeEditorValidation::Run();
        if (command != "--validate-landscape") return -1;

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
        if (!collider_component->ReadyForQuery() || collider_component->CookedChunkCount() == 0)
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

        // 立っている地形が壁として返るか。足元の床 1 枚に隠されないことを確かめる。
        {
            using ReplayEngine::Landscape::LandscapeVertex;
            std::vector<LandscapeVertex> wall_vertices;
            std::vector<std::uint32_t> wall_indices;
            const auto push_quad = [&](float x0, float y0, float x1, float y1)
            {
                const std::uint32_t base = static_cast<std::uint32_t>(wall_vertices.size());
                for (int corner = 0; corner < 4; ++corner)
                {
                    LandscapeVertex vertex;
                    const bool far_side = corner == 1 || corner == 2;
                    const bool high_side = corner >= 2;
                    vertex.position = { high_side ? x1 : x0, high_side ? y1 : y0,
                        far_side ? 5.0f : -5.0f };
                    wall_vertices.push_back(vertex);
                }
                wall_indices.insert(wall_indices.end(),
                    { base, base + 1, base + 2, base, base + 2, base + 3 });
            };
            // 平らな床と、法線 y が 0.17 ほどの急斜面。急斜面は壁の条件を満たす。
            push_quad(-5.0f, 0.0f, 2.0f, 0.0f);
            push_quad(2.0f, 0.0f, 2.5f, 3.0f);

            ReplayEngine::Scene::Scene wall_scene("LandscapeWallValidation");
            auto* wall_ground = wall_scene.CreateGameObject("Ground");
            auto* wall_landscape = wall_ground != nullptr
                ? wall_ground->AddComponent<ReplayEngine::Components::LandscapeComponent>() : nullptr;
            auto* wall_collider = wall_ground != nullptr
                ? wall_ground->AddComponent<ReplayEngine::Components::LandscapeColliderComponent>() : nullptr;
            if (wall_landscape == nullptr || wall_collider == nullptr ||
                !wall_landscape->Data().InitializeMesh(wall_vertices, wall_indices, 1.0f, 0, 0))
            {
                std::fprintf(stderr, "Landscape wall scene setup failed\n");
                return 44;
            }
            wall_collider->RefreshGeometryIfChanged();

            ReplayEngine::Scene::SceneCollisionWorld wall_world;
            wall_world.AttachScene(&wall_scene);
            wall_scene.Services().SetPhysics(&wall_world);
            wall_world.Refresh();

            // 床の上に立ったまま、急斜面へ横から向かう。
            const float radius = 0.38f;
            const DirectX::XMFLOAT3 start{ 0.0f, radius - 0.01f, 0.0f };
            const DirectX::XMFLOAT3 end{ 2.2f, radius - 0.01f, 0.0f };
            ReplayEngine::Scene::CollisionQueryFilter filter;
            filter.layer = ReplayEngine::Physics::CollisionLayers::Default;
            filter.mask = ReplayEngine::Physics::CollisionLayers::all_layers_mask;

            ReplayEngine::Scene::SphereSweepHit any_hit{};
            const bool any_found = wall_world.SweepSphereFiltered(
                start, end, radius, 1.0f, filter, any_hit);

            ReplayEngine::Scene::SphereSweepHit wall_hit{};
            const bool wall_found = wall_world.SweepSphereFiltered(
                start, end, radius, 0.249f, filter, wall_hit);

            wall_scene.Services().SetPhysics(nullptr);
            wall_world.DetachScene();

            if (!any_found)
            {
                std::fprintf(stderr, "Landscape wall setup produced no hit at all\n");
                return 45;
            }
            if (!wall_found)
            {
                std::fprintf(stderr,
                    "Landscape wall not reported: standing contact hides the slope "
                    "(any_hit normal.y=%.3f)\n", any_hit.normal.y);
                return 46;
            }
            if (wall_hit.normal.y > 0.249f)
            {
                std::fprintf(stderr, "Landscape wall hit is not a wall: normal.y=%.3f\n",
                    wall_hit.normal.y);
                return 47;
            }
        }

        {
            LandscapeData raycast_landscape;
            if (!raycast_landscape.Initialize(129, 129, 1.0f))
            {
                std::fprintf(stderr, "Landscape chunk raycast setup failed\n");
                return 48;
            }

            const DirectX::XMFLOAT3 ray_origin{ 17.25f, 10.0f, 23.25f };
            const DirectX::XMFLOAT3 ray_direction{ 0.0f, -1.0f, 0.0f };
            LandscapeRayHit chunk_hit{};
            if (!raycast_landscape.Raycast(ray_origin, ray_direction, 100.0f, chunk_hit))
            {
                std::fprintf(stderr, "Landscape chunk raycast produced no hit\n");
                return 49;
            }
            const std::size_t tested_triangles =
                raycast_landscape.LastRaycastTriangleTestCount();
            const std::size_t all_triangles = raycast_landscape.FaceCount();

            raycast_landscape.Chunks().clear();
            LandscapeRayHit full_scan_hit{};
            if (!raycast_landscape.Raycast(ray_origin, ray_direction, 100.0f, full_scan_hit) ||
                raycast_landscape.LastRaycastTriangleTestCount() != all_triangles ||
                chunk_hit.face_index != full_scan_hit.face_index ||
                chunk_hit.distance != full_scan_hit.distance ||
                chunk_hit.position.x != full_scan_hit.position.x ||
                chunk_hit.position.y != full_scan_hit.position.y ||
                chunk_hit.position.z != full_scan_hit.position.z ||
                chunk_hit.normal.x != full_scan_hit.normal.x ||
                chunk_hit.normal.y != full_scan_hit.normal.y ||
                chunk_hit.normal.z != full_scan_hit.normal.z)
            {
                std::fprintf(stderr,
                    "Landscape chunk raycast result differs from full scan\n");
                return 50;
            }

            const std::size_t triangle_limit = all_triangles / 4u;
            if (tested_triangles >= triangle_limit)
            {
                std::fprintf(stderr,
                    "Landscape chunk raycast tested %zu of %zu triangles; limit is below %zu\n",
                    tested_triangles, all_triangles, triangle_limit);
                return 51;
            }
        }

        // 地形衝突は編集した近傍だけを再 cook し、全面 cook と同じ結果を返す。
        {
            ReplayEngine::Scene::Scene chunk_scene("LandscapeChunkCookValidation");
            auto* chunk_ground = chunk_scene.CreateGameObject("Ground");
            auto* chunk_landscape = chunk_ground != nullptr
                ? chunk_ground->AddComponent<ReplayEngine::Components::LandscapeComponent>() : nullptr;
            auto* chunk_collider = chunk_ground != nullptr
                ? chunk_ground->AddComponent<ReplayEngine::Components::LandscapeColliderComponent>() : nullptr;
            if (chunk_landscape == nullptr || chunk_collider == nullptr ||
                !chunk_landscape->Data().Initialize(129, 129, 1.0f))
            {
                std::fprintf(stderr, "Landscape chunk collision cook setup failed\n");
                return 54;
            }

            chunk_collider->RefreshGeometryIfChanged();
            const std::size_t all_faces = chunk_landscape->Data().FaceCount();
            const std::size_t all_chunks = chunk_landscape->Data().Chunks().size();
            if (all_faces != 32768u || all_chunks != 9u ||
                chunk_collider->LastRecookedChunkCount() != all_chunks ||
                chunk_collider->LastRecookedTriangleCount() != all_faces)
            {
                std::fprintf(stderr,
                    "Landscape initial chunk cook count mismatch: chunks=%zu/%zu triangles=%zu/%zu\n",
                    chunk_collider->LastRecookedChunkCount(), all_chunks,
                    chunk_collider->LastRecookedTriangleCount(), all_faces);
                return 55;
            }

            std::vector<std::uint64_t> old_revisions;
            std::vector<const ReplayEngine::Physics::CookedMeshCollisionData*> old_cooks;
            for (const auto& chunk : chunk_collider->CookedChunks())
            {
                old_revisions.push_back(chunk.source_revision);
                old_cooks.push_back(chunk.cooked.get());
            }

            chunk_collider->BeginInteractiveEdit();
            if (!chunk_landscape->Data().SetHeight(16, 16, 1.0f)) return 56;
            chunk_collider->RefreshGeometryIfChanged();
            if (chunk_collider->LastRecookedChunkCount() != 0 ||
                chunk_collider->LastRecookedTriangleCount() != 0)
            {
                std::fprintf(stderr, "Landscape collision cooked during interactive edit\n");
                return 57;
            }
            chunk_collider->EndInteractiveEdit();
            chunk_collider->RefreshGeometryIfChanged();

            if (chunk_collider->LastRecookedChunkCount() != 1u ||
                chunk_collider->LastRecookedTriangleCount() != 3613u)
            {
                std::fprintf(stderr,
                    "Landscape local edit recooked %zu chunks and %zu triangles; expected 1 and 3613\n",
                    chunk_collider->LastRecookedChunkCount(),
                    chunk_collider->LastRecookedTriangleCount());
                return 58;
            }
            std::size_t cached_faces = 0;
            for (std::size_t index = 0; index < all_chunks; ++index)
            {
                cached_faces += chunk_collider->CookedChunks()[index].cooked->TriangleCount();
                const bool revision_changed =
                    chunk_landscape->Data().Chunks()[index].revision != old_revisions[index];
                const bool cook_changed =
                    chunk_collider->CookedChunks()[index].cooked.get() != old_cooks[index];
                if (revision_changed != cook_changed)
                {
                    std::fprintf(stderr, "Landscape unchanged collision chunk was not reused\n");
                    return 59;
                }
            }
            if (cached_faces != all_faces)
            {
                std::fprintf(stderr, "Landscape chunk collision triangle set is incomplete\n");
                return 59;
            }

            ReplayEngine::Scene::SceneCollisionWorld chunk_world;
            chunk_world.AttachScene(&chunk_scene);
            chunk_world.Refresh();
            const DirectX::XMFLOAT3 sweep_start{ 16.25f, 5.0f, 16.25f };
            const DirectX::XMFLOAT3 sweep_end{ 16.25f, -2.0f, 16.25f };
            constexpr float sweep_radius = 0.2f;
            ReplayEngine::Scene::CollisionQueryFilter filter;
            filter.layer = ReplayEngine::Physics::CollisionLayers::Default;
            filter.mask = ReplayEngine::Physics::CollisionLayers::all_layers_mask;
            ReplayEngine::Scene::SphereSweepHit chunk_sweep{};
            const bool chunk_found = chunk_world.SweepSphereFiltered(
                sweep_start, sweep_end, sweep_radius, 1.0f, filter, chunk_sweep);
            chunk_world.DetachScene();

            ReplayEngine::Scene::Scene full_scene("LandscapeFullCookValidation");
            auto* full_ground = full_scene.CreateGameObject("Ground");
            auto* full_landscape = full_ground != nullptr
                ? full_ground->AddComponent<ReplayEngine::Components::LandscapeComponent>() : nullptr;
            auto* full_collider = full_ground != nullptr
                ? full_ground->AddComponent<ReplayEngine::Components::LandscapeColliderComponent>() : nullptr;
            if (full_landscape == nullptr || full_collider == nullptr ||
                !full_landscape->Data().Initialize(129, 129, 1.0f) ||
                !full_landscape->Data().SetHeight(16, 16, 1.0f)) return 60;
            LandscapeChunk full_chunk;
            full_chunk.coord = { 0, 0 };
            full_chunk.bounds_min = full_landscape->Data().BoundsMin();
            full_chunk.bounds_max = full_landscape->Data().BoundsMax();
            full_chunk.revision = full_landscape->Data().Revision();
            full_chunk.indices = full_landscape->Data().Indices();
            full_landscape->Data().Chunks().assign(1, std::move(full_chunk));
            full_collider->RefreshGeometryIfChanged();

            ReplayEngine::Scene::SceneCollisionWorld full_world;
            full_world.AttachScene(&full_scene);
            full_world.Refresh();
            ReplayEngine::Scene::SphereSweepHit full_sweep{};
            const bool full_found = full_world.SweepSphereFiltered(
                sweep_start, sweep_end, sweep_radius, 1.0f, filter, full_sweep);
            full_world.DetachScene();
            if (chunk_found != full_found || !chunk_found ||
                chunk_sweep.center.x != full_sweep.center.x ||
                chunk_sweep.center.y != full_sweep.center.y ||
                chunk_sweep.center.z != full_sweep.center.z ||
                chunk_sweep.position.x != full_sweep.position.x ||
                chunk_sweep.position.y != full_sweep.position.y ||
                chunk_sweep.position.z != full_sweep.position.z ||
                chunk_sweep.normal.x != full_sweep.normal.x ||
                chunk_sweep.normal.y != full_sweep.normal.y ||
                chunk_sweep.normal.z != full_sweep.normal.z ||
                chunk_sweep.fraction != full_sweep.fraction ||
                chunk_sweep.started_overlapping != full_sweep.started_overlapping)
            {
                std::fprintf(stderr,
                    "Landscape chunk collision query differs from full cook\n");
                return 61;
            }
        }

        {
            ReplayEngine::Components::LandscapeRendererComponent load_renderer;
            const DirectX::XMFLOAT3 camera{ 0.0f, 0.0f, 0.0f };
            constexpr std::size_t chunk_count = 3;
            const DirectX::XMFLOAT3 bounds_min[chunk_count] = {
                { -1.0f, -1.0f, -1.0f }, { 1.5f, -1.0f, -1.0f }, { 2.1f, -1.0f, -1.0f } };
            const DirectX::XMFLOAT3 bounds_max[chunk_count] = {
                { 1.0f, 1.0f, 1.0f }, { 5.0f, 1.0f, 1.0f }, { 3.0f, 1.0f, 1.0f } };
            const auto submitted_count = [&](bool resident)
            {
                std::size_t count = 0;
                for (std::size_t index = 0; index < chunk_count; ++index)
                {
                    if (load_renderer.ShouldSubmitChunk(camera, bounds_min[index],
                        bounds_max[index], resident))
                        ++count;
                }
                return count;
            };

            load_renderer.load_range = 2.0f;
            if (submitted_count(false) != 2 ||
                !load_renderer.ShouldSubmitChunk(camera, bounds_min[2], bounds_max[2], true))
            {
                std::fprintf(stderr, "Landscape chunk load range submission count mismatch\n");
                return 52;
            }
            load_renderer.load_range = 0.0f;
            if (submitted_count(false) != chunk_count)
            {
                std::fprintf(stderr, "Landscape unlimited chunk submission count mismatch\n");
                return 53;
            }
        }

        if (!RunLandscapeEditorRegression()) return 70;

        std::fprintf(stderr,
            "Landscape v2 OK: arbitrary mesh, sculpt+undo, topology+bridge, cave/tunnel, raycast, chunk load range, chunk collision cook, save/reload, v1 migration, Component Scene round-trip OK\n");
        return 0;
    }

}
