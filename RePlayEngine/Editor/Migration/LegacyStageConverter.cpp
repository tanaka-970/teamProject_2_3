#include "LegacyStageConverter.h"

#include "../../Components/Physics/MeshColliderComponent.h"
#include "../../Components/Rendering/MeshRendererComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Physics/CollisionLayers.h"
#include "../../Scene/Runtime/Scene.h"
#include "../../Scene/Serialization/SceneData.h"
#include "../../Scene/Serialization/SceneSerializer.h"

#include <DirectXMath.h>

namespace ReplayEngine::Editor
{
    namespace
    {
        DirectX::XMFLOAT3 DegreesToRadians(const DirectX::XMFLOAT3& degrees) noexcept
        {
            return {
                DirectX::XMConvertToRadians(degrees.x),
                DirectX::XMConvertToRadians(degrees.y),
                DirectX::XMConvertToRadians(degrees.z)
            };
        }
    }

    bool LegacyStageConverter::Convert(const Scene::SceneDocument& source,
        Scene::Scene& destination, Result& result, std::string& error)
    {
        result = Result{};
        error.clear();

        Scene::LegacyStageMigrationState& migration =
            destination.Services().LegacyStageMigration();
        if (migration.IsMigrated(Scene::LegacyStageMigrationState::stage_source_id))
        {
            result.already_converted = true;
            result.stage_root = migration.MigratedObject(
                Scene::LegacyStageMigrationState::stage_source_id);
            return true;
        }

        Core::GameObject* root = destination.CreateGameObject(
            source.SceneName().empty() ? "StageRoot" : source.SceneName() + "_StageRoot");
        if (root == nullptr)
        {
            error = "StageRootを作成できませんでした。";
            return false;
        }
        result.stage_root = root->ID();

        for (const Scene::SceneEntity& entity : source.Entities())
        {
            if (entity.id == 0)
            {
                result.warnings.push_back(entity.name + ": 移行元IDが0のため記録できません。");
                continue;
            }

            Core::GameObject* object = destination.CreateGameObject(
                entity.name.empty() ? std::string("StageObject") : entity.name);
            if (object == nullptr)
            {
                destination.DestroyGameObject(root);
                destination.ProcessPendingOperations();
                error = "GameObject作成中に失敗したため変換を取り消しました。";
                return false;
            }

            object->SetEnabled(entity.active);
            object->SetParent(root, false);
            if (entity.transform)
            {
                object->GetTransform().SetLocalPosition(entity.transform->position);
                object->GetTransform().SetLocalRotationEuler(
                    DegreesToRadians(entity.transform->rotation));
                object->GetTransform().SetLocalScale(entity.transform->scale);
            }

            if (entity.model_renderer)
            {
                auto* renderer = object->AddComponent<Components::MeshRendererComponent>();
                if (renderer != nullptr)
                {
                    renderer->mesh_asset = entity.model_renderer->asset_guid;
                    renderer->tint = entity.model_renderer->tint;
                    renderer->shading_model = entity.model_renderer->shading_model;
                    renderer->outline = entity.model_renderer->outline;
                    renderer->visible = entity.model_renderer->visible;
                    ++result.renderer_count;
                    if (renderer->mesh_asset.empty())
                        result.warnings.push_back(object->Name() + ": Mesh Assetが未解決です。");
                }
            }

            if (entity.mesh_collider && entity.mesh_collider->enabled)
            {
                auto* collider = object->AddComponent<Components::MeshColliderComponent>();
                if (collider != nullptr)
                {
                    collider->mesh_source = Components::MeshColliderComponent::MeshSource_Renderer;
                    collider->cook_cell_size = entity.mesh_collider->cell_size;
                    collider->collision_layer = Physics::CollisionLayers::Environment;
                    collider->collision_mask = Physics::CollisionLayers::all_layers_mask;
                    collider->is_trigger = false;
                    ++result.collider_count;
                }
            }

            Scene::LegacyStageMigrationState::SourceID source_id = entity.id;
            migration.MarkMigrated(source_id, object->ID());
            Mapping mapping;
            mapping.source_id = entity.id;
            mapping.object_id = object->ID();
            result.mappings.push_back(mapping);
        }

        // 旧Stage本体のDraw/Collisionを止めるための固定SourceID。
        // EntityId 1と一致する旧データでも、最終的な変換先はStageRootでよい。
        migration.MarkMigrated(Scene::LegacyStageMigrationState::stage_source_id, root->ID());
        destination.Services().SetCollisionBackendMode(2); // Scene Colliders Only
        return true;
    }

    bool LegacyStageConverter::SaveAndVerify(const Scene::Scene& scene,
        const std::filesystem::path& path, std::string& error)
    {
        Scene::Serialization::SceneData captured;
        Scene::Serialization::CaptureScene(scene, captured);
        if (!Scene::Serialization::SceneSerializer::SaveToFile(captured, path, error))
            return false;

        Scene::Serialization::SceneData reloaded;
        if (!Scene::Serialization::SceneSerializer::LoadFromFile(reloaded, path, error))
            return false;

        if (reloaded.version != Scene::Serialization::SceneData::current_version ||
            reloaded.objects.size() != captured.objects.size())
        {
            error = "変換Sceneの再読込検証でObject数またはVersionが一致しません。";
            return false;
        }
        return true;
    }
}
