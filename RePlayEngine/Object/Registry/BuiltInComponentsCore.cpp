#include "BuiltInComponentsInternal.h"

namespace ReplayEngine::Core::Detail
{
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

        void RegisterPivot()
        {
            ComponentRegistry::Register<PivotComponent>(
                ComponentTypeInfo::Describe(u8"Pivot", u8"Core")
                    .WithTooltip(u8"回転・拡縮の基準点。編集しても Transform の原点や配置は動かない。"));

            PropertyRegistry::Register<PivotComponent>(
                MakeProperty("mode", &PivotComponent::mode)
                    .Display(u8"基準点の決め方")
                    .AsEnum({ u8"自分の原点", u8"境界の中心", u8"境界の面",
                        u8"ローカル座標", u8"ワールド座標", u8"別オブジェクト" })
                    .Animation(Animatable::Step)
                    .Tooltip(u8"回転・拡縮のときだけ使う基準点の解決方法。"));

            PropertyRegistry::Register<PivotComponent>(
                MakeProperty("local_point", &PivotComponent::local_point)
                    .Display(u8"基準点")
                    .Step(0.01)
                    .Animation(Animatable::Interpolatable)
                    .Tooltip(u8"ローカル/ワールド基準点、または境界面を選ぶ方向。"));

            PropertyRegistry::Register<PivotComponent>(
                MakeProperty("target", &PivotComponent::target)
                    .Display(u8"対象オブジェクト")
                    .Animation(Animatable::None)
                    .Tooltip(u8"別オブジェクトを基準点にするときの参照。"));
        }

        void RegisterPropertyLink()
        {
            ComponentRegistry::Register<PropertyLinkComponent>(
                ComponentTypeInfo::Describe("Property Link", "Motion")
                    .WithTooltip("Motion Mixer 後に数値プロパティを別の Component へ接続します。循環接続は無効化します。"));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("source_object", &PropertyLinkComponent::source_object)
                    .Display("接続元 Component"));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("source_property", &PropertyLinkComponent::source_property)
                    .Display("接続元プロパティ"));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("target_object", &PropertyLinkComponent::target_object)
                    .Display("接続先 Component"));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("target_property", &PropertyLinkComponent::target_property)
                    .Display("接続先プロパティ"));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("source_min", &PropertyLinkComponent::source_min)
                    .Display("元の最小値").Step(0.01)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("source_max", &PropertyLinkComponent::source_max)
                    .Display("元の最大値").Step(0.01)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("target_min", &PropertyLinkComponent::target_min)
                    .Display("先の最小値").Step(0.01)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("target_max", &PropertyLinkComponent::target_max)
                    .Display("先の最大値").Step(0.01)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("invert", &PropertyLinkComponent::invert)
                    .Display("反転"));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("clamp", &PropertyLinkComponent::clamp)
                    .Display("範囲内に制限"));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("easing", &PropertyLinkComponent::easing)
                    .Display("イージング")
                    .AsEnum({ "Linear", "Step", "EaseInQuad", "EaseOutQuad",
                        "EaseInOutQuad", "EaseInCubic", "EaseOutCubic",
                        "EaseInOutCubic", "EaseInBack", "EaseOutBack",
                        "EaseInOutBack", "EaseInElastic", "EaseOutElastic",
                        "EaseInOutElastic", "CustomBezier" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<PropertyLinkComponent>(
                MakeProperty("smoothing", &PropertyLinkComponent::smoothing)
                    .Display("平滑化").Range(0.0, 60.0).Step(0.01)
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("0 は即時反映。大きいほどゆっくり追従します。"));
        }

        void RegisterScenePersistence()
        {
            ComponentRegistry::Register<PersistentComponent>(
                ComponentTypeInfo::Describe("Persistent", "Scene")
                    .WithTooltip("Scene 直下のルートを Scene 遷移後も同じ実体として保持します。"));

            ComponentRegistry::Register<SceneLoaderComponent>(
                ComponentTypeInfo::Describe("Scene Loader", "Scene")
                    .WithTooltip("既存の SceneFlowService / RuntimeSceneService の進捗を公開します。"));
            PropertyRegistry::Register<SceneLoaderComponent>(
                MakeProperty<SceneLoaderComponent>("progress", &SceneLoaderComponent::progress)
                    .Display("進捗")
                    .Range(0.0f, 1.0f)
                    .RuntimeOnly().ReadOnly().NotSerializable());
            PropertyRegistry::Register<SceneLoaderComponent>(
                MakeProperty<SceneLoaderComponent>("is_loading", &SceneLoaderComponent::is_loading)
                    .Display("読込中")
                    .RuntimeOnly().ReadOnly().NotSerializable());
            PropertyRegistry::Register<SceneLoaderComponent>(
                MakeProperty<SceneLoaderComponent>("state", &SceneLoaderComponent::state)
                    .Display("状態")
                    .AsEnum({ "Idle", "Loading", "ReadyToSwap", "Swapping", "Completed", "Failed" })
                    .RuntimeOnly().ReadOnly().NotSerializable());
        }

        void RegisterState()
        {
            ComponentRegistry::Register<StateComponent>(
                ComponentTypeInfo::Describe("State", "Core")
                    .WithTooltip("名前付きの状態を保持し、変更を Motion Player へ通知します。"));
            PropertyRegistry::Register<StateComponent>(
                MakeAccessorProperty<StateComponent>("state_count", PropertyType::Int,
                    [](const StateComponent& component)
                    { return PropertyValue::MakeInt(component.StateCount()); },
                    [](StateComponent& component, const PropertyValue& value)
                    { component.SetStateCount(value.AsInt(2)); })
                    .Display("状態の数").Range(2.0, 16.0).Step(1.0));
            PropertyRegistry::Register<StateComponent>(
                MakeAccessorProperty<StateComponent>("current_state", PropertyType::String,
                    [](const StateComponent& component)
                    { return PropertyValue::MakeString(component.CurrentState()); },
                    [](StateComponent& component, const PropertyValue& value)
                    { component.SetCurrentState(value.AsString()); })
                    .Display("現在の状態").RuntimeOnly().NotSerializable());
        }
}
