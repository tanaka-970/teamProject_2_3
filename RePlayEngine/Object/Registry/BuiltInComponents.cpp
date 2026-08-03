#include "BuiltInComponents.h"

#include "ComponentRegistry.h"
#include "../Component/MissingComponent.h"
#include "../../Reflection/Registry/PropertyRegistry.h"

#include "../../Components/Camera/CameraTargetComponent.h"
#include "../../Components/Core/TransformComponent.h"
#include "../../Components/Gameplay/CharacterMotorComponent.h"
#include "../../Components/Gameplay/HealthComponent.h"
#include "../../Components/Gameplay/PlayerControllerComponent.h"
#include "../../Components/Gameplay/PlayerInputComponent.h"
#include "../../Components/Gameplay/RotatorComponent.h"
#include "../../Components/Gameplay/StageGameplayComponents.h"
#include "../../Components/Physics/BoxColliderComponent.h"
#include "../../Components/Physics/CapsuleColliderComponent.h"
#include "../../Components/Physics/MeshColliderComponent.h"
#include "../../Components/Physics/SphereColliderComponent.h"
#include "../../Components/Rendering/AnimatorComponent.h"
#include "../../Components/Rendering/LightComponents.h"
#include "../../Components/Rendering/MeshRendererComponent.h"
#include "../../Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../Scripting/Core/ScriptComponent.h"

namespace ReplayEngine::Core
{
    namespace
    {
        using Components::AnimatorComponent;
        using Components::BoxColliderComponent;
        using Components::CameraTargetComponent;
        using Components::CapsuleColliderComponent;
        using Components::CharacterMotorComponent;
        using Components::HealthComponent;
        using Components::MeshColliderComponent;
        using Components::MeshRendererComponent;
        using Components::DirectionalLightComponent;
        using Components::PointLightComponent;
        using Components::SpotLightComponent;
        using Components::PlayerControllerComponent;
        using Components::PlayerInputComponent;
        using Components::RotatorComponent;
        using Components::SkinnedMeshRendererComponent;
        using Components::SphereColliderComponent;
        using Components::SpawnPointComponent;
        using Components::CheckpointComponent;
        using Components::GoalComponent;
        using Components::KillVolumeComponent;
        using Components::JumpPadComponent;
        using Components::DamageAreaComponent;
        using Components::TransformComponent;

        using Reflection::MakeAccessorProperty;
        using Reflection::MakeProperty;
        using Reflection::PropertyRegistry;
        using Reflection::PropertyType;
        using Reflection::PropertyValue;

        // 読み込めなかった Component の預かり先。
        //
        // Add Component 一覧には出さない。ユーザーが自分で足すものではなく、
        // 読み込み処理だけが作る内部用の型のため。
        //
        // 設定の意味:
        //   AllowMultipleInstances … 1 つの GameObject が複数の型を読めないことがある
        //   HiddenInEditor         … Add Component へ出さない
        //   removable = true (既定) … ユーザーが明示的に消すことはできる。
        //                             Missing を理由に自動削除はしない。
        //   serializable = true (既定) … 保存対象。ただし書き出されるのは
        //                             "MissingComponent" ではなく預かっている元の型。
        void RegisterMissingComponent()
        {
            ComponentRegistry::Register<MissingComponent>(
                ComponentTypeInfo::Describe("Missing Component", "Internal")
                    .WithTooltip("型が見つからない Component。保存されていた値はそのまま保持され、"
                        "型が使えるようになれば自動的に復元される。")
                    .AllowMultipleInstances()
                    .HiddenInEditor()
                    .InModule("RePlayEngine.BuiltIn"));

            // プロパティは登録しない。
            // 預かっているデータは PropertyRegistry を通さず、
            // MissingComponent::Record として丸ごと保持・書き戻しする。
            // 中途半端に型付けすると、知らない型の値を壊してしまう。
        }

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
                    .Tooltip("Material AssetのGUID。Projectパネルから割り当てる。"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("material_override", &MeshRendererComponent::material_override)
                    .Display("マテリアル上書き")
                    .Tooltip("色と描画方式にRenderer側の値を使う。"));

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
                MakeProperty("receive_shadow", &MeshRendererComponent::receive_shadow)
                    .Display("影を受ける"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("visible", &MeshRendererComponent::visible).Display("表示"));

            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("local_position_offset", &MeshRendererComponent::local_position_offset)
                    .Display("モデル位置オフセット").Step(0.01));
            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("local_rotation_offset", &MeshRendererComponent::local_rotation_offset)
                    .Display("モデル回転オフセット (度)").Step(0.5));
            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("local_scale_multiplier", &MeshRendererComponent::local_scale_multiplier)
                    .Display("モデル縮尺倍率").Step(0.01));
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

        void RegisterScript()
        {
            using Scripting::ScriptComponent;
            using Scripting::ScriptLanguage;
            using Scripting::ScriptLanguageFromInt;
            namespace ScriptNames = Scripting::ScriptNames;

            ComponentRegistry::Register<ScriptComponent>(
                ComponentTypeInfo::Describe("Script", "Scripting")
                    .WithTypeGUID(ScriptComponent::StaticTypeGUID())
                    .InModule(ScriptComponent::module_id)
                    .WithVersion(1)
                    .AllowMultipleInstances()
                    .WithTooltip(
                        "Lua または C# のスクリプトを取り付ける。"
                        "公開変数は選んだスクリプトに応じて Inspector へ出る。"));

            // 保存名へ予約接頭辞を付ける。
            //
            // ユーザーが language / class_name / script_asset という名前の
            // 公開変数を宣言しても、そちらの保存名は field.language のように
            // なるため、構造的に衝突しない。
            //
            // Display() を必ず付けるので、Inspector に接頭辞は出ない。

            PropertyRegistry::Register<ScriptComponent>(
                MakeAccessorProperty<ScriptComponent>(ScriptNames::language,
                    PropertyType::Enum,
                    [](const ScriptComponent& script)
                    {
                        return PropertyValue::MakeEnum(static_cast<int>(script.Language()));
                    },
                    [](ScriptComponent& script, const PropertyValue& value)
                    {
                        script.SetLanguage(ScriptLanguageFromInt(value.AsInt(0)));
                    })
                    .AsEnum({ "Lua", "C#" })
                    .Display("Language"));

            PropertyRegistry::Register<ScriptComponent>(
                MakeAccessorProperty<ScriptComponent>(ScriptNames::asset,
                    PropertyType::AssetReference,
                    [](const ScriptComponent& script)
                    {
                        return PropertyValue::MakeAssetReference(script.ScriptAssetGUID());
                    },
                    [](ScriptComponent& script, const PropertyValue& value)
                    {
                        script.SetScriptAssetGUID(value.AsAssetReference().guid);
                    })
                    .Display("Script")
                    .OfAssetType("Script"));

            PropertyRegistry::Register<ScriptComponent>(
                MakeAccessorProperty<ScriptComponent>(ScriptNames::class_name,
                    PropertyType::String,
                    [](const ScriptComponent& script)
                    {
                        return PropertyValue::MakeString(script.ClassName());
                    },
                    [](ScriptComponent& script, const PropertyValue& value)
                    {
                        script.SetClassName(value.AsString());
                    })
                    .Display("Class")
                    .Tooltip("C# の完全修飾クラス名。Lua では使わない。"));

            // Phase 1 では保存・表示だけ。実際の実行順ソートは行わない。
            // BehaviourComponent::execution_order と同じ扱いで、
            // Scene 全体の実行順対応は別作業として分離する。
            PropertyRegistry::Register<ScriptComponent>(
                MakeAccessorProperty<ScriptComponent>(ScriptNames::execution_order,
                    PropertyType::Int,
                    [](const ScriptComponent& script)
                    {
                        return PropertyValue::MakeInt(
                            static_cast<int>(script.ExecutionOrder()));
                    },
                    [](ScriptComponent& script, const PropertyValue& value)
                    {
                        script.SetExecutionOrder(
                            static_cast<std::int32_t>(value.AsInt(0)));
                    })
                    .Display("Execution Order")
                    .Tooltip("小さいほど先。現時点では保存と表示のみで、"
                        "実際の呼び出し順は GameObject 順・Component 順のまま。"));

            // Script Asset が一時的に見つからない状態でも
            // 「どのスクリプト型だったか」を保てるようにするため保存する。
            // 人が編集するものではないので Inspector には出さない。
            PropertyRegistry::Register<ScriptComponent>(
                MakeAccessorProperty<ScriptComponent>(ScriptNames::type_id,
                    PropertyType::String,
                    [](const ScriptComponent& script)
                    {
                        return PropertyValue::MakeString(script.ScriptType().ToString());
                    },
                    [](ScriptComponent& script, const PropertyValue& value)
                    {
                        Reflection::TypeGUID parsed;
                        if (Reflection::TypeGUID::TryParse(value.AsString(), parsed))
                        {
                            script.RestoreScriptType(parsed);
                        }
                    })
                    .Display("Script Type ID")
                    .HiddenInEditor());
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

        void RegisterSkinnedMeshRenderer()
        {
            ComponentRegistry::Register<SkinnedMeshRendererComponent>(
                ComponentTypeInfo::Describe("Skinned Mesh Renderer", "Rendering")
                    .WithTooltip("アニメーション付きモデルを描画する。Animator のクリップと時刻を提出する。"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("mesh_asset", &SkinnedMeshRendererComponent::mesh_asset)
                    .Display("メッシュ").AsAssetPath()
                    .Tooltip("AssetDatabase の GUID。空なら描画しない。"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("material_asset", &SkinnedMeshRendererComponent::material_asset)
                    .Display("マテリアル").AsAssetPath()
                    .Tooltip("Material AssetのGUID。Projectパネルから割り当てる。"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("material_override", &SkinnedMeshRendererComponent::material_override)
                    .Display("マテリアル上書き")
                    .Tooltip("色と描画方式にRenderer側の値を使う。"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("tint", &SkinnedMeshRendererComponent::tint).Display("色").AsColor());

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("shading_model", &SkinnedMeshRendererComponent::shading_model)
                    .Display("描画方式")
                    .AsEnum({ "FBX標準", "PBR", "トゥーン", "アンリット" }));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("outline", &SkinnedMeshRendererComponent::outline).Display("輪郭線"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("cast_shadow", &SkinnedMeshRendererComponent::cast_shadow)
                    .Display("影を落とす"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("receive_shadow", &SkinnedMeshRendererComponent::receive_shadow)
                    .Display("影を受ける"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("visible", &SkinnedMeshRendererComponent::visible).Display("表示"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("visual_rotation_offset",
                    &SkinnedMeshRendererComponent::visual_rotation_offset)
                    .Display("表示姿勢補正 (度)").Step(0.5)
                    .Tooltip("FBX の基準姿勢を正立させるための補正。論理的な向きとは別物。"));

            // 【重要】この 2 つは以前 PropertyRegistry へ登録されていなかった。
            //   Component のメンバとしては存在し、描画にも使われていたが、
            //   登録が無いと保存も復元も Inspector 表示もされない。
            //   そのため「変換直後は正しい大きさなのに、保存して再起動すると
            //   既定値 1.0 に戻って 100 倍の大きさで表示される」状態になっていた。
            //   モデル固有の縮尺は Scene / Prefab へ保存されなければ意味がないので、
            //   ここへ登録して単一の登録点の約束へ揃える。
            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("local_position_offset",
                    &SkinnedMeshRendererComponent::local_position_offset)
                    .Display("モデル位置オフセット").Step(0.01)
                    .Tooltip("モデルの原点が足元や中心でない場合のずらし。"
                        "GameObject の位置とは別物。"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("local_scale_multiplier",
                    &SkinnedMeshRendererComponent::local_scale_multiplier)
                    .Display("モデル縮尺倍率").Step(0.001)
                    .Tooltip("GameObject の Scale へ掛ける倍率。"
                        "cm 単位で作られた FBX なら 0.01 を入れる。"
                        "1.0 のままだと 100 倍の大きさで表示される。"));

            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("apply_fbx_coordinate_transform",
                    &SkinnedMeshRendererComponent::apply_fbx_coordinate_transform)
                    .Display("FBX 座標系補正"));
        }

        void RegisterAnimator()
        {
            ComponentRegistry::Register<AnimatorComponent>(
                ComponentTypeInfo::Describe("Animator", "Rendering")
                    .WithTooltip("Character Motor の状態から Idle / Walk / Jump を切り替える。"));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("idle_clip", &AnimatorComponent::idle_clip)
                    .Display("待機クリップ").Range(-1.0, 255.0).Step(1.0)
                    .Tooltip("-1 なら切り替えない。"));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("walk_clip", &AnimatorComponent::walk_clip)
                    .Display("移動クリップ").Range(-1.0, 255.0).Step(1.0));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("jump_clip", &AnimatorComponent::jump_clip)
                    .Display("ジャンプクリップ").Range(-1.0, 255.0).Step(1.0));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("playback_speed", &AnimatorComponent::playback_speed)
                    .Display("再生速度").Range(0.0, 5.0).Step(0.01));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("loop", &AnimatorComponent::loop).Display("ループ"));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("walk_speed_threshold", &AnimatorComponent::walk_speed_threshold)
                    .Display("移動とみなす速度").Range(0.0, 10.0).Step(0.01));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("playing", &AnimatorComponent::playing).Display("再生"));
        }

        // すべての Collider に共通するプロパティ。
        //
        // 形状ごとに同じ 5 行を書き写すと、片方だけ直し忘れる事故が起きる。
        // 共通部分はここ 1 か所にまとめ、形状ごとの登録から呼ぶ。
        // 登録先の型が違うだけなのでテンプレートで足りる。
        // 【テンプレート引数を明示している理由】
        //   collider_key などは基底 ColliderComponent のメンバなので、
        //   MakeProperty(&T::collider_key) と書くと引数推論で C = ColliderComponent に
        //   なってしまう。基底は抽象クラスで StaticTypeID を持たないためコンパイルできない。
        //   <T, ...> を明示すると、メンバポインタが基底→派生へ暗黙変換され、
        //   登録先も正しく派生型になる。
        template<class T>
        void RegisterColliderCommon()
        {
            PropertyRegistry::Register<T>(
                MakeProperty<T, int>("collider_key", &T::collider_key)
                    .Display("Collider 番号").ReadOnly().HiddenInEditor()
                    .Tooltip("GameObject の中でこの Collider を指す番号。"
                        "Character Motor の参照に使われる。自動で振られる。"));

            PropertyRegistry::Register<T>(
                MakeProperty<T, DirectX::XMFLOAT3>("center_offset", &T::center_offset)
                    .Display("中心オフセット").Step(0.01));

            PropertyRegistry::Register<T>(
                MakeProperty<T, int>("collision_layer", &T::collision_layer)
                    .Display("レイヤー").AsCollisionLayer()
                    .Tooltip("この Collider が属するレイヤー。"));

            PropertyRegistry::Register<T>(
                MakeProperty<T, int>("collision_mask", &T::collision_mask)
                    .Display("衝突する相手").AsCollisionMask()
                    .Tooltip("衝突を受け付けるレイヤー。"
                        "双方が互いを含んでいるときだけ衝突する。"));

            PropertyRegistry::Register<T>(
                MakeProperty<T, bool>("is_trigger", &T::is_trigger)
                    .Display("トリガー")
                    .Tooltip("true なら通り抜ける。押し戻しと接地からは必ず除外される。"
                        "代わりに OnTriggerEnter / Stay / Exit が届く。"));

            PropertyRegistry::Register<T>(
                MakeProperty<T, bool>("debug_draw", &T::debug_draw)
                    .Display("形状を表示")
                    .Tooltip("Editor の Scene View へ形状を描く。"));
        }

        void RegisterSphereCollider()
        {
            ComponentRegistry::Register<SphereColliderComponent>(
                ComponentTypeInfo::Describe("Sphere Collider", "Physics")
                    .WithTooltip("球状の衝突形状。中心は Owner の Transform から求める。")
                    .AllowMultipleInstances());

            RegisterColliderCommon<SphereColliderComponent>();

            PropertyRegistry::Register<SphereColliderComponent>(
                MakeProperty("radius", &SphereColliderComponent::radius)
                    .Display("半径").Range(0.01, 100.0).Step(0.01));

            PropertyRegistry::Register<SphereColliderComponent>(
                MakeProperty("skin_width", &SphereColliderComponent::skin_width)
                    .Display("接触余白").Range(0.0, 1.0).Step(0.001));

            PropertyRegistry::Register<SphereColliderComponent>(
                MakeProperty("walkable_normal_y", &SphereColliderComponent::walkable_normal_y)
                    .Display("歩ける傾斜のしきい値").Range(-1.0, 1.0).Step(0.01));
        }

        void RegisterBoxCollider()
        {
            ComponentRegistry::Register<BoxColliderComponent>(
                ComponentTypeInfo::Describe("Box Collider", "Physics")
                    .WithTooltip("直方体の衝突形状。"
                        "GameObject の位置・回転・拡大率をすべて反映する。")
                    .AllowMultipleInstances());

            RegisterColliderCommon<BoxColliderComponent>();

            PropertyRegistry::Register<BoxColliderComponent>(
                MakeProperty("size", &BoxColliderComponent::size)
                    .Display("サイズ").Step(0.01)
                    .Tooltip("辺の長さ。半分の長さではない。"));
        }

        void RegisterCapsuleCollider()
        {
            ComponentRegistry::Register<CapsuleColliderComponent>(
                ComponentTypeInfo::Describe("Capsule Collider", "Physics")
                    .WithTooltip("カプセルの衝突形状。キャラクターの移動用に向く。")
                    .AllowMultipleInstances());

            RegisterColliderCommon<CapsuleColliderComponent>();

            PropertyRegistry::Register<CapsuleColliderComponent>(
                MakeProperty("radius", &CapsuleColliderComponent::radius)
                    .Display("半径").Range(0.01, 100.0).Step(0.01));

            PropertyRegistry::Register<CapsuleColliderComponent>(
                MakeProperty("height", &CapsuleColliderComponent::height)
                    .Display("高さ").Range(0.01, 200.0).Step(0.01)
                    .Tooltip("両端の半球を含めた全長。"
                        "直径を下回る値を入れると、判定は直径まで切り上げられ警告が出る。"));

            PropertyRegistry::Register<CapsuleColliderComponent>(
                MakeProperty("axis", &CapsuleColliderComponent::axis)
                    .Display("軸").AsEnum({ "X", "Y", "Z" }));
        }

        void RegisterMeshCollider()
        {
            ComponentRegistry::Register<MeshColliderComponent>(
                ComponentTypeInfo::Describe("Mesh Collider", "Physics")
                    .WithTooltip("三角形メッシュの衝突形状（静的環境用）。"
                        "Cook データは AssetGUID 単位で共有される。")
                    .AllowMultipleInstances());

            RegisterColliderCommon<MeshColliderComponent>();

            PropertyRegistry::Register<MeshColliderComponent>(
                MakeProperty("mesh_source", &MeshColliderComponent::mesh_source)
                    .Display("メッシュの取得元")
                    .AsEnum({ "Renderer のメッシュ", "衝突専用メッシュ" })
                    .Tooltip("Renderer 側を使うか、衝突専用の低ポリメッシュを指定するか。"));

            PropertyRegistry::Register<MeshColliderComponent>(
                MakeProperty("mesh_asset", &MeshColliderComponent::mesh_asset)
                    .Display("衝突専用メッシュ").AsAssetPath()
                    .Tooltip("「衝突専用メッシュ」を選んだときだけ使う AssetGUID。"));

            PropertyRegistry::Register<MeshColliderComponent>(
                MakeProperty("cook_cell_size", &MeshColliderComponent::cook_cell_size)
                    .Display("セルサイズ").Range(0.05, 100.0).Step(0.1)
                    .Tooltip("空間分割の粗さ。小さいほど絞り込みが効くがメモリを使う。"));

            PropertyRegistry::Register<MeshColliderComponent>(
                MakeProperty("double_sided", &MeshColliderComponent::double_sided)
                    .Display("両面に当たる"));

            PropertyRegistry::Register<MeshColliderComponent>(
                MakeProperty("debug_draw_wireframe",
                    &MeshColliderComponent::debug_draw_wireframe)
                    .Display("三角形を表示")
                    .Tooltip("既定は境界ボックスのみ。"
                        "有効にすると衝突三角形そのものを描く（重い）。"));
        }

        void RegisterCharacterMotor()
        {
            ComponentRegistry::Register<CharacterMotorComponent>(
                ComponentTypeInfo::Describe("Character Motor", "Gameplay")
                    .WithTooltip("移動・重力・ジャンプ・接地。プレイヤーと敵の双方から使える。"));

            // 移動用 Collider は明示的に選ぶ。暗黙の自動選択はしない。
            // 一覧には Sphere / Capsule / Box だけが出る（Mesh と Trigger は除外）。
            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("primary_collider_key",
                    &CharacterMotorComponent::primary_collider_key)
                    .Display("移動用 Collider").AsColliderReference()
                    .Tooltip("移動・接地・押し戻しに使う Collider。"
                        "未設定だと地形との当たり判定を行わない。"));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("move_speed", &CharacterMotorComponent::move_speed)
                    .Display("移動速度").Range(0.0, 30.0).Step(0.1));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("acceleration", &CharacterMotorComponent::acceleration)
                    .Display("加速度").Range(0.0, 120.0).Step(0.5));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("deceleration", &CharacterMotorComponent::deceleration)
                    .Display("減速度").Range(0.0, 120.0).Step(0.5));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("air_control", &CharacterMotorComponent::air_control)
                    .Display("空中制御").Range(0.0, 1.0).Step(0.01));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("gravity", &CharacterMotorComponent::gravity)
                    .Display("重力").Range(0.0, 100.0).Step(0.1));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("jump_power", &CharacterMotorComponent::jump_power)
                    .Display("ジャンプ力").Range(0.0, 50.0).Step(0.1));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("maximum_fall_speed", &CharacterMotorComponent::maximum_fall_speed)
                    .Display("最大落下速度").Range(0.0, 200.0).Step(1.0));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("fallback_ground_y", &CharacterMotorComponent::fallback_ground_y)
                    .Display("地形が無い時の床の高さ").Step(0.05));

            PropertyRegistry::Register<CharacterMotorComponent>(
                MakeProperty("vertical_physics", &CharacterMotorComponent::vertical_physics)
                    .Display("重力とジャンプを有効化")
                    .Tooltip("旧 Player は既定で無効だったため、初期値も無効にしてある。"));
        }

        void RegisterPlayerInput()
        {
            ComponentRegistry::Register<PlayerInputComponent>(
                ComponentTypeInfo::Describe("Player Input", "Gameplay")
                    .WithTooltip("入力の取得だけを担当する。Transform も速度も触らない。"));

            PropertyRegistry::Register<PlayerInputComponent>(
                MakeProperty("input_enabled", &PlayerInputComponent::input_enabled)
                    .Display("入力を受け付ける"));

            PropertyRegistry::Register<PlayerInputComponent>(
                MakeProperty("local_player_slot", &PlayerInputComponent::local_player_slot)
                    .Display("プレイヤー番号").Range(0.0, 3.0).Step(1.0));
        }

        void RegisterPlayerController()
        {
            ComponentRegistry::Register<PlayerControllerComponent>(
                ComponentTypeInfo::Describe("Player Controller", "Gameplay")
                    .WithTooltip("Player Input と Character Motor をつなぐ。両方が必要。"));

            PropertyRegistry::Register<PlayerControllerComponent>(
                MakeProperty("turn_speed_degrees", &PlayerControllerComponent::turn_speed_degrees)
                    .Display("旋回速度 (度/秒)").Range(0.0, 1440.0).Step(1.0));

            PropertyRegistry::Register<PlayerControllerComponent>(
                MakeProperty("dash_multiplier", &PlayerControllerComponent::dash_multiplier)
                    .Display("ダッシュ倍率").Range(0.1, 5.0).Step(0.05));

            PropertyRegistry::Register<PlayerControllerComponent>(
                MakeProperty("camera_relative", &PlayerControllerComponent::camera_relative)
                    .Display("カメラ基準で移動"));

            PropertyRegistry::Register<PlayerControllerComponent>(
                MakeProperty("rotate_towards_movement",
                    &PlayerControllerComponent::rotate_towards_movement)
                    .Display("進行方向へ向く"));
        }

        void RegisterCameraTarget()
        {
            ComponentRegistry::Register<CameraTargetComponent>(
                ComponentTypeInfo::Describe("Camera Target", "Camera")
                    .WithTooltip("カメラ追従の対象になる。自分ではカメラを動かさない。"));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("target_offset", &CameraTargetComponent::target_offset)
                    .Display("追従基準のオフセット").Step(0.05)
                    .Tooltip("追従する点そのものをずらす。"
                        "モデルの原点が足元にある場合などに使う。"));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("look_at_offset", &CameraTargetComponent::look_at_offset)
                    .Display("注視点オフセット").Step(0.05)
                    .Tooltip("追従点は変えずに、カメラが見る点だけをずらす。"));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("follow_distance", &CameraTargetComponent::follow_distance)
                    .Display("追従距離").Range(0.5, 100.0).Step(0.1));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("follow_height", &CameraTargetComponent::follow_height)
                    .Display("追従高さ").Range(-10.0, 50.0).Step(0.1));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("follow_lag", &CameraTargetComponent::follow_lag)
                    .Display("追従の速さ").Range(0.1, 60.0).Step(0.1));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("priority", &CameraTargetComponent::priority)
                    .Display("優先度").Range(-100.0, 100.0).Step(1.0));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("field_of_view_degrees",
                    &CameraTargetComponent::field_of_view_degrees)
                    .Display("視野角 (度)").Range(1.0, 179.0).Step(0.5)
                    .Tooltip("この Target が選ばれたときの Runtime Camera の視野角。"));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("near_clip", &CameraTargetComponent::near_clip)
                    .Display("Near Clip").Range(0.001, 10.0).Step(0.01)
                    .Tooltip("この Target が選ばれたときの Runtime Camera の Near Clip。"));

            PropertyRegistry::Register<CameraTargetComponent>(
                MakeProperty("far_clip", &CameraTargetComponent::far_clip)
                    .Display("Far Clip").Range(10.0, 100000.0).Step(10.0)
                    .Tooltip("この Target が選ばれたときの Runtime Camera の Far Clip。"));
        }

        void RegisterLights()
        {
            ComponentRegistry::Register<DirectionalLightComponent>(
                ComponentTypeInfo::Describe("Directional Light", "Lighting")
                    .WithTooltip("GameObjectの回転方向から照らす平行光源。Scene内の先頭1つを使用。"));
            PropertyRegistry::Register<DirectionalLightComponent>(
                MakeProperty("color", &DirectionalLightComponent::color).Display("色").AsColor());
            PropertyRegistry::Register<DirectionalLightComponent>(
                MakeProperty("intensity", &DirectionalLightComponent::intensity)
                    .Display("強さ").Range(0.0, 100.0).Step(0.05));
            PropertyRegistry::Register<DirectionalLightComponent>(
                MakeProperty("cast_shadows", &DirectionalLightComponent::cast_shadows)
                    .Display("影を落とす"));

            ComponentRegistry::Register<PointLightComponent>(
                ComponentTypeInfo::Describe("Point Light", "Lighting")
                    .WithTooltip("Transform位置を中心に全方向へ照らす。"));
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("color", &PointLightComponent::color).Display("色").AsColor());
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("intensity", &PointLightComponent::intensity)
                    .Display("強さ").Range(0.0, 100.0).Step(0.05));
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("range", &PointLightComponent::range)
                    .Display("範囲").Range(0.01, 10000.0).Step(0.1));

            ComponentRegistry::Register<SpotLightComponent>(
                ComponentTypeInfo::Describe("Spot Light", "Lighting")
                    .WithTooltip("Transform位置と回転で円錐状に照らす。"));
            PropertyRegistry::Register<SpotLightComponent>(
                MakeProperty("color", &SpotLightComponent::color).Display("色").AsColor());
            PropertyRegistry::Register<SpotLightComponent>(
                MakeProperty("intensity", &SpotLightComponent::intensity)
                    .Display("強さ").Range(0.0, 100.0).Step(0.05));
            PropertyRegistry::Register<SpotLightComponent>(
                MakeProperty("range", &SpotLightComponent::range)
                    .Display("範囲").Range(0.01, 10000.0).Step(0.1));
            PropertyRegistry::Register<SpotLightComponent>(
                MakeProperty("inner_angle_degrees", &SpotLightComponent::inner_angle_degrees)
                    .Display("内側角度").Range(0.1, 179.0).Step(0.5));
            PropertyRegistry::Register<SpotLightComponent>(
                MakeProperty("outer_angle_degrees", &SpotLightComponent::outer_angle_degrees)
                    .Display("外側角度").Range(0.1, 179.0).Step(0.5));
        }

        void RegisterStageGameplay()
        {
            ComponentRegistry::Register<SpawnPointComponent>(
                ComponentTypeInfo::Describe("Spawn Point", "Gameplay")
                    .WithTooltip("開始地点またはチェックポイント未通過時の復帰地点。"));
            PropertyRegistry::Register<SpawnPointComponent>(
                MakeProperty("spawn_id", &SpawnPointComponent::spawn_id)
                    .Display("スポーンID").Step(1.0));
            PropertyRegistry::Register<SpawnPointComponent>(
                MakeProperty("team", &SpawnPointComponent::team)
                    .Display("チーム").Step(1.0));
            PropertyRegistry::Register<SpawnPointComponent>(
                MakeProperty("priority", &SpawnPointComponent::priority)
                    .Display("優先度").Step(1.0));
            PropertyRegistry::Register<SpawnPointComponent>(
                MakeProperty("debug_draw", &SpawnPointComponent::debug_draw)
                    .Display("Scene Viewに表示"));

            ComponentRegistry::Register<CheckpointComponent>(
                ComponentTypeInfo::Describe("Checkpoint", "Gameplay")
                    .WithTooltip("Triggerへ入った対象の復帰地点を更新する。"));
            PropertyRegistry::Register<CheckpointComponent>(
                MakeProperty("checkpoint_id", &CheckpointComponent::checkpoint_id)
                    .Display("チェックポイントID").Step(1.0));
            PropertyRegistry::Register<CheckpointComponent>(
                MakeProperty("respawn_position_offset",
                    &CheckpointComponent::respawn_position_offset)
                    .Display("復帰位置オフセット").Step(0.05));
            PropertyRegistry::Register<CheckpointComponent>(
                MakeProperty("respawn_rotation", &CheckpointComponent::respawn_rotation)
                    .Display("復帰時の回転").Step(0.5));
            PropertyRegistry::Register<CheckpointComponent>(
                MakeProperty("target_mask", &CheckpointComponent::target_mask)
                    .Display("反応する対象").AsCollisionMask());
            PropertyRegistry::Register<CheckpointComponent>(
                MakeProperty("one_shot", &CheckpointComponent::one_shot)
                    .Display("一度だけ"));

            ComponentRegistry::Register<GoalComponent>(
                ComponentTypeInfo::Describe("Goal", "Gameplay")
                    .WithTooltip("到達イベントを発行する。Scene遷移は行わない。"));
            PropertyRegistry::Register<GoalComponent>(
                MakeProperty("goal_id", &GoalComponent::goal_id)
                    .Display("ゴールID").Step(1.0));
            PropertyRegistry::Register<GoalComponent>(
                MakeProperty("target_mask", &GoalComponent::target_mask)
                    .Display("反応する対象").AsCollisionMask());
            PropertyRegistry::Register<GoalComponent>(
                MakeProperty("one_shot", &GoalComponent::one_shot).Display("一度だけ"));
            PropertyRegistry::Register<GoalComponent>(
                MakeProperty("completion_event", &GoalComponent::completion_event)
                    .Display("完了イベント"));

            ComponentRegistry::Register<KillVolumeComponent>(
                ComponentTypeInfo::Describe("Kill Volume", "Gameplay")
                    .WithTooltip("対象へダメージを与え、設定時は復帰地点へ戻す。"));
            PropertyRegistry::Register<KillVolumeComponent>(
                MakeProperty("target_mask", &KillVolumeComponent::target_mask)
                    .Display("反応する対象").AsCollisionMask());
            PropertyRegistry::Register<KillVolumeComponent>(
                MakeProperty("respawn_at_checkpoint",
                    &KillVolumeComponent::respawn_at_checkpoint)
                    .Display("チェックポイントへ復帰"));
            PropertyRegistry::Register<KillVolumeComponent>(
                MakeProperty("damage_amount", &KillVolumeComponent::damage_amount)
                    .Display("ダメージ").Range(0.0, 1000000.0).Step(1.0));

            ComponentRegistry::Register<JumpPadComponent>(
                ComponentTypeInfo::Describe("Jump Pad", "Gameplay")
                    .WithTooltip("Character Motorへ指定方向の速度を加えるTrigger。"));
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("direction", &JumpPadComponent::direction)
                    .Display("射出方向").Step(0.01));
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("force", &JumpPadComponent::force)
                    .Display("力").Range(0.0, 1000.0).Step(0.1));
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("target_mask", &JumpPadComponent::target_mask)
                    .Display("反応する対象").AsCollisionMask());
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("one_shot", &JumpPadComponent::one_shot).Display("一度だけ"));
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("cooldown", &JumpPadComponent::cooldown)
                    .Display("再使用待ち時間 (秒)").Range(0.0, 60.0).Step(0.05));
            PropertyRegistry::Register<JumpPadComponent>(
                MakeProperty("debug_draw", &JumpPadComponent::debug_draw)
                    .Display("Scene Viewに表示"));

            ComponentRegistry::Register<DamageAreaComponent>(
                ComponentTypeInfo::Describe("Damage Area", "Gameplay")
                    .WithTooltip("Trigger内のHealthへ一定間隔でダメージを与える。"));
            PropertyRegistry::Register<DamageAreaComponent>(
                MakeProperty("damage", &DamageAreaComponent::damage)
                    .Display("ダメージ").Range(0.0, 1000000.0).Step(1.0));
            PropertyRegistry::Register<DamageAreaComponent>(
                MakeProperty("interval", &DamageAreaComponent::interval)
                    .Display("間隔 (秒)").Range(0.0, 60.0).Step(0.05));
            PropertyRegistry::Register<DamageAreaComponent>(
                MakeProperty("target_mask", &DamageAreaComponent::target_mask)
                    .Display("反応する対象").AsCollisionMask());
            PropertyRegistry::Register<DamageAreaComponent>(
                MakeProperty("one_shot", &DamageAreaComponent::one_shot)
                    .Display("一度だけ"));
        }
    }

    void RegisterBuiltInComponents()
    {
        // 並び順がそのまま Add Component 一覧の並びになる。
        //
        // 新しい Component を足すときは、ここへ 1 行足すだけでよい。
        // それだけで Add Component 一覧・Inspector・Scene 保存・読み込み・
        // 複製・Undo/Redo・Prefab のすべてへ反映される。
        // Missing Component の預かり先を最初に登録する。
        // Scene 読み込み中に型が見つからなかった場合、この型が必ず使える必要がある。
        RegisterMissingComponent();

        RegisterTransform();
        RegisterMeshRenderer();
        RegisterLights();
        RegisterSkinnedMeshRenderer();
        RegisterAnimator();
        RegisterSphereCollider();
        RegisterBoxCollider();
        RegisterCapsuleCollider();
        RegisterMeshCollider();
        RegisterCharacterMotor();
        RegisterPlayerInput();
        RegisterPlayerController();
        RegisterCameraTarget();
        RegisterRotator();
        RegisterHealth();
        RegisterStageGameplay();
        RegisterScript();
    }
}
