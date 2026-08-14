#include "SceneValidator.h"

#include "../../Assets/AssetDatabase.h"
#include "../../Components/Gameplay/CharacterMotorComponent.h"
#include "../../Components/Gameplay/PlayerControllerComponent.h"
#include "../../Components/Gameplay/StageGameplayComponents.h"
#include "../../Components/Landscape/LandscapeComponent.h"
#include "../../Components/Landscape/LandscapeColliderComponent.h"
#include "../../Components/Landscape/LandscapeRendererComponent.h"
#include "../../Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../Components/Physics/ColliderComponent.h"
#include "../../Components/Physics/MeshColliderComponent.h"
#include "../../Components/Rendering/MeshRendererComponent.h"
#include "../../Components/Rendering/LightComponents.h"
#include "../../Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../Object/GameObject/GameObject.h"
#include "../../Physics/CollisionLayers.h"
#include "../../Scene/Runtime/Scene.h"

#include <cmath>
#include <unordered_set>

namespace ReplayEngine::Editor
{
    namespace
    {
        bool Finite(const DirectX::XMFLOAT3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        void Add(std::vector<ValidationIssue>& issues, ValidationSeverity severity,
            const char* code, std::string message, std::string suggestion,
            Core::ObjectID object = Core::ObjectID::Invalid())
        {
            ValidationIssue issue;
            issue.severity = severity;
            issue.code = code;
            issue.message = std::move(message);
            issue.suggestion = std::move(suggestion);
            issue.object = object;
            issues.push_back(std::move(issue));
        }

        bool HasTrigger(const Core::GameObject& object)
        {
            for (std::size_t index = 0; index < object.ComponentCount(); ++index)
            {
                const auto* collider = dynamic_cast<const Components::ColliderComponent*>(
                    object.ComponentAt(index));
                if (collider != nullptr && !collider->PendingDestroy() && collider->is_trigger)
                    return true;
            }
            return false;
        }

        bool IsTriggerGameplay(const Core::Component* component)
        {
            return dynamic_cast<const Components::CheckpointComponent*>(component) != nullptr ||
                dynamic_cast<const Components::GoalComponent*>(component) != nullptr ||
                dynamic_cast<const Components::KillVolumeComponent*>(component) != nullptr ||
                dynamic_cast<const Components::JumpPadComponent*>(component) != nullptr ||
                dynamic_cast<const Components::DamageAreaComponent*>(component) != nullptr;
        }
    }

    std::vector<ValidationIssue> SceneValidator::Validate(const Scene::Scene& scene,
        const Assets::AssetDatabase* assets)
    {
        std::vector<ValidationIssue> issues;
        std::unordered_set<Core::ObjectID::ValueType> ids;
        int collider_count = 0;
        int directional_light_count = 0;
        int point_light_count = 0;
        int spot_light_count = 0;

        const Core::ObjectID controlled = scene.Services().ControlledObject();
        if (!controlled.Valid())
        {
            Add(issues, ValidationSeverity::Warning, "SCENE_CONTROLLED_UNSET",
                "操作対象が設定されていません。",
                "HierarchyまたはInspectorから明示的に操作対象を設定してください。");
        }
        else if (scene.FindGameObjectByID(controlled) == nullptr)
        {
            Add(issues, ValidationSeverity::Error, "SCENE_CONTROLLED_MISSING",
                "操作対象のGameObjectが存在しません。",
                "有効なGameObjectを操作対象へ設定するか参照をクリアしてください。",
                controlled);
        }

        for (std::size_t object_index = 0; object_index < scene.GameObjectCount(); ++object_index)
        {
            const Core::GameObject* object = scene.GameObjectAt(object_index);
            if (object == nullptr || object->PendingDestroy()) continue;

            if (!ids.insert(object->ID().Value()).second)
            {
                Add(issues, ValidationSeverity::Error, "OBJECT_ID_DUPLICATE",
                    object->Name() + ": ObjectIDが重複しています。",
                    "保存前にIDを再採番してください。", object->ID());
            }

            const auto& transform = object->GetTransform();
            const DirectX::XMFLOAT3 position = transform.LocalPosition();
            const DirectX::XMFLOAT3 rotation = transform.LocalRotationEuler();
            const DirectX::XMFLOAT3 scale = transform.LocalScale();
            if (!Finite(position) || !Finite(rotation) || !Finite(scale))
            {
                Add(issues, ValidationSeverity::Error, "TRANSFORM_NON_FINITE",
                    object->Name() + ": TransformにNaNまたはInfinityがあります。",
                    "Transformを有限の値へ戻してください。", object->ID());
            }
            if (std::abs(scale.x) < 0.00001f || std::abs(scale.y) < 0.00001f ||
                std::abs(scale.z) < 0.00001f)
            {
                Add(issues, ValidationSeverity::Warning, "TRANSFORM_ZERO_SCALE",
                    object->Name() + ": Scaleが0です。",
                    "描画と衝突が不定にならないよう0以外へ設定してください。", object->ID());
            }

            std::unordered_set<int> collider_keys;
            bool gameplay_needs_trigger = false;
            bool has_character_motor = false;
            bool has_mesh_collider = false;
            bool has_landscape = false;
            bool has_landscape_renderer = false;
            bool has_landscape_collider = false;
            bool has_visible_primitive = false;
            for (std::size_t component_index = 0;
                component_index < object->ComponentCount(); ++component_index)
            {
                const Core::Component* component = object->ComponentAt(component_index);
                if (component == nullptr || component->PendingDestroy()) continue;
                if (IsTriggerGameplay(component)) gameplay_needs_trigger = true;
                if (dynamic_cast<const Components::LandscapeComponent*>(component))
                    has_landscape = true;
                if (dynamic_cast<const Components::LandscapeRendererComponent*>(component))
                    has_landscape_renderer = true;
                if (dynamic_cast<const Components::LandscapeColliderComponent*>(component))
                    has_landscape_collider = true;
                if (const auto* primitive =
                    dynamic_cast<const Components::PrimitiveMeshRendererComponent*>(component))
                    has_visible_primitive = has_visible_primitive || primitive->visible;

                if (dynamic_cast<const Components::DirectionalLightComponent*>(component))
                    ++directional_light_count;
                if (dynamic_cast<const Components::PointLightComponent*>(component))
                    ++point_light_count;
                if (dynamic_cast<const Components::SpotLightComponent*>(component))
                    ++spot_light_count;

                if (const auto* collider = dynamic_cast<const Components::ColliderComponent*>(component))
                {
                    ++collider_count;
                    if (collider->collider_key <= 0 ||
                        !collider_keys.insert(collider->collider_key).second)
                    {
                        Add(issues, ValidationSeverity::Error, "COLLIDER_KEY_INVALID",
                            object->Name() + ": Collider番号が未設定または重複しています。",
                            "Colliderを付け直すか番号を再採番してください。", object->ID());
                    }
                    if (!Physics::CollisionLayers::ValidLayer(collider->collision_layer))
                    {
                        Add(issues, ValidationSeverity::Error, "COLLIDER_LAYER_INVALID",
                            object->Name() + ": Collision Layerが範囲外です。",
                            "Projectで定義されたLayerを選択してください。", object->ID());
                    }
                    if (collider->collision_mask == 0)
                    {
                        Add(issues, ValidationSeverity::Warning, "COLLIDER_MASK_EMPTY",
                            object->Name() + ": Collision Maskが空です。",
                            "意図的でなければ衝突対象Layerを選択してください。", object->ID());
                    }
                }

                if (const auto* motor = dynamic_cast<const Components::CharacterMotorComponent*>(component))
                {
                    has_character_motor = true;
                    Components::ColliderComponent* primary = motor->ResolvePrimaryCollider();
                    if (primary == nullptr)
                    {
                        Add(issues, ValidationSeverity::Error, "MOTOR_PRIMARY_MISSING",
                            object->Name() + ": Character Motorの移動用Colliderが無効です。",
                            "Trigger OFFのSphereまたはCapsule Colliderを指定してください。",
                            object->ID());
                    }
                    else if (primary->is_trigger || !primary->UsableAsCharacterShape())
                    {
                        Add(issues, ValidationSeverity::Error, "MOTOR_PRIMARY_INVALID",
                            object->Name() + ": 移動用ColliderにTriggerまたは静的Meshが指定されています。",
                            "SphereまたはCapsule Colliderへ変更してください。", object->ID());
                    }
                }

                const std::string* asset_guid = nullptr;
                if (const auto* renderer = dynamic_cast<const Components::MeshRendererComponent*>(component))
                    asset_guid = &renderer->mesh_asset;
                else if (const auto* renderer =
                    dynamic_cast<const Components::SkinnedMeshRendererComponent*>(component))
                    asset_guid = &renderer->mesh_asset;
                if (asset_guid != nullptr && !asset_guid->empty() && assets != nullptr &&
                    assets->FindByGuid(*asset_guid) == nullptr)
                {
                    Add(issues, ValidationSeverity::Error, "ASSET_MESH_MISSING",
                        object->Name() + ": Mesh Assetが見つかりません。",
                        "Project Browserで参照を修正または再Importしてください。", object->ID());
                }

                if (const auto* mesh = dynamic_cast<const Components::MeshColliderComponent*>(component))
                {
                    has_mesh_collider = true;
                    const std::string guid = mesh->ResolveMeshAssetGuid();
                    if (guid.empty())
                    {
                        Add(issues, ValidationSeverity::Warning, "MESH_COLLIDER_SOURCE_MISSING",
                            object->Name() + ": Mesh Colliderのソースがありません。",
                            "Renderer Meshまたは衝突専用Meshを指定してください。", object->ID());
                    }
                }
            }

            if (has_landscape_renderer && !has_landscape)
            {
                Add(issues, ValidationSeverity::Error, "LANDSCAPE_RENDERER_ORPHAN",
                    object->Name() + ": Landscape Renderer に Landscape データがありません。",
                    "Landscape Component を追加するか、不要な Renderer を削除してください。",
                    object->ID());
            }
            if (has_landscape_collider && !has_landscape)
            {
                Add(issues, ValidationSeverity::Error, "LANDSCAPE_COLLIDER_ORPHAN",
                    object->Name() + ": Landscape Collider に Landscape データがありません。",
                    "Landscape Component を追加するか、不要な Collider を削除してください。",
                    object->ID());
            }
            if (has_landscape && !has_landscape_renderer)
            {
                Add(issues, ValidationSeverity::Warning, "LANDSCAPE_RENDERER_MISSING",
                    object->Name() + ": Landscape はありますが表示用 Renderer がありません。",
                    "Inspector または Scene View の Landscape ツールから Renderer を追加してください。",
                    object->ID());
            }
            if (has_landscape && !has_landscape_collider)
            {
                Add(issues, ValidationSeverity::Warning, "LANDSCAPE_COLLIDER_MISSING",
                    object->Name() + ": Landscape に衝突判定がありません。",
                    "衝突が必要なら Landscape Collider を追加してください。", object->ID());
            }
            if (has_landscape && has_visible_primitive)
            {
                Add(issues, ValidationSeverity::Warning, "LANDSCAPE_PRIMITIVE_OVERLAP",
                    object->Name() + ": Primitive と Landscape が同じ GameObject で表示されています。",
                    "Landscape を使う場合は Primitive の表示をOFFにしてください。", object->ID());
            }

            if (object->ID() == controlled && has_character_motor && has_mesh_collider)
            {
                Add(issues, ValidationSeverity::Error, "CONTROLLED_MESH_COLLIDER",
                    object->Name() +
                        ": 操作対象にMesh Colliderが付いています。移動形状と二重衝突する可能性があります。",
                    "操作対象はSphere/Capsule ColliderをPrimaryにし、Mesh Colliderは環境側へ移してください。",
                    object->ID());
            }

            if (gameplay_needs_trigger && !HasTrigger(*object))
            {
                Add(issues, ValidationSeverity::Error, "GAMEPLAY_TRIGGER_MISSING",
                    object->Name() + ": Gameplay ComponentにTrigger Colliderがありません。",
                    "Box/Sphere/Capsule Colliderを追加しTriggerを有効にしてください。", object->ID());
            }

            if (object->IsPrefabInstance() && assets != nullptr &&
                assets->FindByGuid(object->PrefabSourceGUID()) == nullptr)
            {
                Add(issues, ValidationSeverity::Error, "PREFAB_SOURCE_MISSING",
                    object->Name() + ": Prefab Sourceが見つかりません。",
                    "Assetを再登録するかPrefabをUnpackしてください。", object->ID());
            }
        }

        if (collider_count == 0)
        {
            Add(issues, ValidationSeverity::Warning, "SCENE_COLLIDER_EMPTY",
                "Scene Colliderが0件です。", "床・壁などへColliderを追加してください。");
        }

        if (directional_light_count > 1)
        {
            Add(issues, ValidationSeverity::Warning, "LIGHT_DIRECTIONAL_MULTIPLE",
                "Directional Lightが複数あります。先頭の有効な1つだけを使用します。",
                "不要なDirectional Lightを無効化または削除してください。");
        }
        if (point_light_count > 8 || spot_light_count > 4)
        {
            Add(issues, ValidationSeverity::Warning, "LIGHT_LIMIT_EXCEEDED",
                "GPUへ送れるPoint/Spot Light数を超えています。",
                "Pointは8、Spotは4以下にしてください。");
        }

        return issues;
    }
}
