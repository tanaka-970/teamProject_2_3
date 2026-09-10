#include "BuiltInComponentsInternal.h"

namespace ReplayEngine::Core::Detail
{
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

        void RegisterRigidbody()
        {
            ComponentRegistry::Register<RigidbodyComponent>(
                ComponentTypeInfo::Describe("Rigidbody", "Physics")
                    .WithTooltip(
                        "質量・重力・摩擦を使って Collider を動かす動的剛体。"
                        "Transform を直接書き換える場合は次の FixedUpdate で物理値に同期される。")
                    .Recommends<SphereColliderComponent>()
                    .Recommends<BoxColliderComponent>()
                    .Recommends<CapsuleColliderComponent>());

            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("body_type", &RigidbodyComponent::body_type)
                    .Display("種別")
                    .AsEnum({ "静的", "キネマティック", "動的" })
                    .Tooltip("Mesh / Landscape を Dynamic にした場合は警告を出して Kinematic として扱う。"));
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("mass", &RigidbodyComponent::mass)
                    .Display("質量").Range(0.001, 10000.0).Step(0.01));
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("linear_damping", &RigidbodyComponent::linear_damping)
                    .Display("移動の減衰").Range(0.0, 10.0).Step(0.01));
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("angular_damping", &RigidbodyComponent::angular_damping)
                    .Display("回転の減衰").Range(0.0, 10.0).Step(0.01));
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("gravity_scale", &RigidbodyComponent::gravity_scale)
                    .Display("重力の倍率").Range(-10.0, 10.0).Step(0.01));
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("restitution", &RigidbodyComponent::restitution)
                    .Display("反発").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("friction", &RigidbodyComponent::friction)
                    .Display("摩擦").Range(0.0, 2.0).Step(0.01));
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("freeze_position", &RigidbodyComponent::freeze_position)
                    .Display("位置を固定").Tooltip("X / Y / Z が 0.5 以上の軸を固定する。"));
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("freeze_rotation", &RigidbodyComponent::freeze_rotation)
                    .Display("回転を固定").Tooltip("X / Y / Z が 0.5 以上の軸を固定する。"));
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("start_asleep", &RigidbodyComponent::start_asleep)
                    .Display("停止状態で開始"));
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("use_ccd", &RigidbodyComponent::use_ccd)
                    .Display("高速移動の貫通対策")
                    .Tooltip("有効時は球掃引で最初の Collider へ停止します。"));

            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("linear_velocity", &RigidbodyComponent::linear_velocity)
                    .Display("移動速度").RuntimeOnly().ReadOnly().NotSerializable());
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("angular_velocity", &RigidbodyComponent::angular_velocity)
                    .Display("回転速度").RuntimeOnly().ReadOnly().NotSerializable());
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeProperty("is_sleeping", &RigidbodyComponent::is_sleeping)
                    .Display("停止中").RuntimeOnly().ReadOnly().NotSerializable());
            PropertyRegistry::Register<RigidbodyComponent>(
                MakeAccessorProperty<RigidbodyComponent>("status", PropertyType::String,
                    [](const RigidbodyComponent& component)
                    { return PropertyValue::MakeString(component.StatusMessage()); },
                    [](RigidbodyComponent&, const PropertyValue&) {})
                    .Display("状態").ReadOnly().NotSerializable()
                    .Tooltip("Collider が無い場合や Dynamic 非対応形状はここへ警告を表示する。"));
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
                    .Display("衝突専用メッシュ").AsAssetPath().OfAssetType("Model")
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

        void RegisterLandscape()
        {
            ComponentRegistry::Register<LandscapeComponent>(
                ComponentTypeInfo::Describe("Landscape", "Landscape")
                    .WithVersion(2)
                    .WithTooltip("任意三角形Topologyを持つ編集可能な地形。描画と衝突は別Component。")
                    .Recommends<LandscapeRendererComponent>()
                    .Recommends<LandscapeColliderComponent>()
                    .InModule("RePlayEngine.Optional.Landscape"));

            PropertyRegistry::Register<LandscapeComponent>(
                MakeProperty("default_resolution", &LandscapeComponent::default_resolution)
                    .Display("新規解像度").Range(2.0, 513.0).Step(1.0)
                    .Tooltip("新しい平面を生成するときの解像度。既存地形は自動変更しない。"));
            PropertyRegistry::Register<LandscapeComponent>(
                MakeProperty("source_model_asset", &LandscapeComponent::source_model_asset)
                    .Display(u8"作成元モデル").AsAssetPath().OfAssetType("Model")
                    .Tooltip("この地形を作ったモデル。選んだあと下のボタンで作り直す。"));
            PropertyRegistry::Register<LandscapeComponent>(
                MakeProperty("default_cell_size", &LandscapeComponent::default_cell_size)
                    .Display("新規セルサイズ").Range(0.05, 100.0).Step(0.05)
                    .Tooltip("新しい平面を生成するときの格子間隔。"));

            ComponentRegistry::Register<LandscapeRendererComponent>(
                ComponentTypeInfo::Describe("Landscape Renderer", "Landscape")
                    .WithTooltip("Landscape Component の任意Meshを描画する。GPU ResourceはRenderer側が所有。")
                    .Requires<LandscapeComponent>()
                    .InModule("RePlayEngine.Optional.Landscape"));
            PropertyRegistry::Register<LandscapeRendererComponent>(
                MakeProperty("tint", &LandscapeRendererComponent::tint).Display("色").AsColor());
            PropertyRegistry::Register<LandscapeRendererComponent>(
                MakeProperty("visible", &LandscapeRendererComponent::visible).Display("表示"));
            PropertyRegistry::Register<LandscapeRendererComponent>(
                MakeProperty("cast_shadow", &LandscapeRendererComponent::cast_shadow).Display("影を落とす"));
            PropertyRegistry::Register<LandscapeRendererComponent>(
                MakeProperty("receive_shadow", &LandscapeRendererComponent::receive_shadow).Display("影を受ける"));
            PropertyRegistry::Register<LandscapeRendererComponent>(
                MakeProperty("double_sided", &LandscapeRendererComponent::double_sided).Display("両面描画"));

            ComponentRegistry::Register<LandscapeColliderComponent>(
                ComponentTypeInfo::Describe("Landscape Collider", "Landscape")
                    .WithTooltip("Landscapeの任意Topologyをそのまま衝突形状として使う。")
                    .Requires<LandscapeComponent>()
                    .InModule("RePlayEngine.Optional.Landscape"));
            RegisterColliderCommon<LandscapeColliderComponent>();
            PropertyRegistry::Register<LandscapeColliderComponent>(
                MakeProperty("double_sided", &LandscapeColliderComponent::double_sided)
                    .Display("両面に当たる"));
            PropertyRegistry::Register<LandscapeColliderComponent>(
                MakeProperty("collision_cell_size", &LandscapeColliderComponent::collision_cell_size)
                    .Display("衝突セルサイズ").Range(0.05, 128.0));
            PropertyRegistry::Register<LandscapeColliderComponent>(
                MakeProperty("debug_draw_wireframe", &LandscapeColliderComponent::debug_draw_wireframe)
                    .Display("三角形を表示"));
        }
}
