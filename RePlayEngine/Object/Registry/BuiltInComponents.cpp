#include "BuiltInComponents.h"

#include "ComponentRegistry.h"
#include "../../Reflection/Registry/PropertyRegistry.h"

#include "../../Components/Core/TransformComponent.h"
#include "../../Components/Gameplay/HealthComponent.h"
#include "../../Components/Gameplay/RotatorComponent.h"
#include "../../Components/Rendering/MeshRendererComponent.h"

namespace ReplayEngine::Core
{
    namespace
    {
        using Components::HealthComponent;
        using Components::MeshRendererComponent;
        using Components::RotatorComponent;
        using Components::TransformComponent;

        using Reflection::MakeAccessorProperty;
        using Reflection::MakeProperty;
        using Reflection::PropertyRegistry;
        using Reflection::PropertyType;
        using Reflection::PropertyValue;

        void RegisterTransform()
        {
            ComponentRegistry::Register<TransformComponent>(
                ComponentTypeInfo::Describe("Transform", "Core")
                    .WithTooltip("位置・回転・拡大率。GameObject が実体を持つ。")
                    .AsBuiltIn()
                    .NotRemovable()
                    // Transform は GameObject 側の情報として保存される。
                    // ここでも保存すると同じ値がファイル内に 2 度出てしまう。
                    .NotSerializable());

            PropertyRegistry::Register<TransformComponent>(
                MakeAccessorProperty<TransformComponent>("position", PropertyType::Vector3,
                    [](const TransformComponent& component)
                    { return PropertyValue::MakeVector3(component.Position()); },
                    [](TransformComponent& component, const PropertyValue& value)
                    { component.SetPosition(value.AsVector3()); })
                .Display("位置").Step(0.05).NotSerializable());

            PropertyRegistry::Register<TransformComponent>(
                MakeAccessorProperty<TransformComponent>("rotation", PropertyType::Vector3,
                    [](const TransformComponent& component)
                    { return PropertyValue::MakeVector3(component.RotationDegrees()); },
                    [](TransformComponent& component, const PropertyValue& value)
                    { component.SetRotationDegrees(value.AsVector3()); })
                .Display("回転 (度)").Step(0.5).NotSerializable());

            PropertyRegistry::Register<TransformComponent>(
                MakeAccessorProperty<TransformComponent>("scale", PropertyType::Vector3,
                    [](const TransformComponent& component)
                    { return PropertyValue::MakeVector3(component.Scale()); },
                    [](TransformComponent& component, const PropertyValue& value)
                    { component.SetScale(value.AsVector3()); })
                .Display("拡大率").Step(0.01).NotSerializable());
        }

        void RegisterMeshRenderer()
        {
            ComponentRegistry::Register<MeshRendererComponent>(
                ComponentTypeInfo::Describe("Mesh Renderer", "Rendering")
                    .WithTooltip("Asset を指定して既存レンダラーへ描画を依頼する。"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("mesh_asset", &MeshRendererComponent::mesh_asset)
                    .Display("メッシュ").AsAssetPath()
                    .Tooltip("AssetDatabase の GUID。プロジェクトパネルから指定する。"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("material_asset", &MeshRendererComponent::material_asset)
                    .Display("マテリアル").AsAssetPath()
                    .Tooltip("将来 Material を分離するための枠。現時点では未使用。"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("tint", &MeshRendererComponent::tint)
                    .Display("色").AsColor());

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("shading_model", &MeshRendererComponent::shading_model)
                    .Display("描画方式")
                    .AsEnum({ "FBX標準", "PBR", "トゥーン", "アンリット" }));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("outline", &MeshRendererComponent::outline).Display("輪郭線"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("cast_shadow", &MeshRendererComponent::cast_shadow)
                    .Display("影を落とす"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("visible", &MeshRendererComponent::visible).Display("表示"));
        }

        void RegisterRotator()
        {
            ComponentRegistry::Register<RotatorComponent>(
                ComponentTypeInfo::Describe("Rotator", "Gameplay")
                    .WithTooltip("GameObject を一定速度で回し続ける。動作確認用。"));

            PropertyRegistry::Register<RotatorComponent>(
                MakeProperty("axis", &RotatorComponent::axis)
                    .Display("回転軸").Step(0.01));

            PropertyRegistry::Register<RotatorComponent>(
                MakeProperty("degrees_per_second", &RotatorComponent::degrees_per_second)
                    .Display("回転速度 (度/秒)")
                    .Range(-1440.0, 1440.0).Step(1.0));
        }

        void RegisterHealth()
        {
            ComponentRegistry::Register<HealthComponent>(
                ComponentTypeInfo::Describe("Health", "Gameplay")
                    .WithTooltip("体力。将来はプレイヤーと敵で共有する。"));

            PropertyRegistry::Register<HealthComponent>(
                MakeProperty("max_health", &HealthComponent::max_health)
                    .Display("最大体力").Range(1.0, 100000.0).Step(1.0));

            PropertyRegistry::Register<HealthComponent>(
                MakeProperty("current_health", &HealthComponent::current_health)
                    .Display("現在体力").Range(0.0, 100000.0).Step(1.0));

            PropertyRegistry::Register<HealthComponent>(
                MakeProperty("invulnerable", &HealthComponent::invulnerable)
                    .Display("無敵"));
        }
    }

    void RegisterBuiltInComponents()
    {
        // 並び順がそのまま Add Component 一覧の並びになる。
        RegisterTransform();
        RegisterMeshRenderer();
        RegisterRotator();
        RegisterHealth();
    }
}
