#include "BuiltInComponentsInternal.h"

namespace ReplayEngine::Core::Detail
{
        void RegisterMotion()
        {
            ComponentRegistry::Register<MotionPlayerComponent>(
                ComponentTypeInfo::Describe("Motion Player", "Motion")
                    .WithTooltip("Motion AssetをScene更新後に評価し、PropertyRegistry経由で値を書き込む。"));

            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("motion", &MotionPlayerComponent::motion)
                    .Display("Motion Asset")
                    .OfAssetType("Motion")
                    .Animation(Animatable::Step)
                    .Tooltip("再生する .replaymotion Asset。"));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("key", &MotionPlayerComponent::key)
                    .Display("キー")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("play_on_start", &MotionPlayerComponent::play_on_start)
                    .Display("開始時に再生")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("trigger", &MotionPlayerComponent::trigger)
                    .Display("きっかけ")
                    .AsEnum({ "開始時", "押された", "離された", "カーソルが乗った",
                        "カーソルが外れた", "有効になった", "無効になった",
                        "シーン遷移の開始", "シーン遷移の完了", "イベントを受け取った",
                        "手動のみ", "状態が変わった" })
                    .Animation(Animatable::Step)
                    .Tooltip("この Motion を再生するきっかけ。手動のみなら Play 呼び出しで再生する。"));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("trigger_delay", &MotionPlayerComponent::trigger_delay)
                    .Display("開始を遅らせる秒数")
                    .Range(0.0, 10.0).Step(0.01)
                    .Tooltip("きっかけを受けてから再生を始めるまでの秒数。"));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("trigger_source", &MotionPlayerComponent::trigger_source)
                    .Display("対象")
                    .Tooltip("監視する GameObject。未指定なら Motion Player と同じ GameObject。"));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("trigger_state", &MotionPlayerComponent::trigger_state)
                    .Display("対象の状態")
                    .Tooltip("状態トリガーでこの名前になったときだけ再生します。空欄なら全状態。"));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("loop", &MotionPlayerComponent::loop)
                    .Display("ループ (旧)")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("wrap_mode", &MotionPlayerComponent::wrap_mode)
                    .Display("終了処理")
                    .AsEnum({ "一回", "ループ", "ピンポン", "最後で保持" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("auto_stop_on_end", &MotionPlayerComponent::auto_stop_on_end)
                    .Display("終了時に停止")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("ignore_time_scale", &MotionPlayerComponent::ignore_time_scale)
                    .Display("Time Scaleを無視")
                    .Tooltip("Pause中も動かしたいUI/演出では有効にする。"));

            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("blend_in_seconds", &MotionPlayerComponent::blend_in_seconds)
                    .Display("Blend In 秒").Range(0.0, 30.0).Step(0.01));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("speed", &MotionPlayerComponent::speed)
                    .Display("再生速度").Range(-8.0, 8.0).Step(0.05));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("random_seed", &MotionPlayerComponent::random_seed)
                    .Display("乱数の種").Step(1.0)
                    .Tooltip("0 なら再生ごとに変化し、固定値なら同じ Object ごとに再現します。"));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("time_offset_random", &MotionPlayerComponent::time_offset_random)
                    .Display("開始時刻のばらつき").Range(0.0, 10.0).Step(0.01)
                    .Tooltip("開始時刻を指定秒数の範囲で前後にずらします。"));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("speed_random", &MotionPlayerComponent::speed_random)
                    .Display("速度のばらつき").Range(0.0, 1.0).Step(0.01)
                    .Tooltip("再生速度を ± の割合でばらつかせます。"));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("weight", &MotionPlayerComponent::weight)
                    .Display("重み").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeProperty("state", &MotionPlayerComponent::state)
                    .Display("再生状態")
                    .AsEnum({ "停止", "再生", "一時停止" })
                    .RuntimeOnly()
                    .ReadOnly()
                    .NotSerializable());
            PropertyRegistry::Register<MotionPlayerComponent>(
                MakeAccessorProperty<MotionPlayerComponent>("time", PropertyType::Float,
                    [](const MotionPlayerComponent& component)
                    { return PropertyValue::MakeFloat(component.Time()); },
                    [](MotionPlayerComponent& component, const PropertyValue& value)
                    { component.SetTime(value.AsFloat()); })
                    .Display("現在時刻")
                    .Unit("秒")
                    .RuntimeOnly()
                    .NotSerializable());

            // ---- 拡張点: Motion Runtime -------------------------------------
            //
            // ・同じ property への setter 呼び出しは MotionMixer::Apply の 1 回だけに保つ。
            // ・Stop 復帰値は Play 開始時に capture した snapshot から戻す。
            // ・未バインド property と DynamicProperties() に無い property は Apply しない。
        }

        void RegisterEditorNote()
        {
            ComponentRegistry::Register<EditorNoteComponent>(
                ComponentTypeInfo::Describe("Scene Note", "Editor")
                    .WithTooltip("Scene View 上に制作指示・TODO・BUG メモを表示する Editor Annotation。")
                    .AllowMultipleInstances()
                    .EditorOnly());

            PropertyRegistry::Register<EditorNoteComponent>(
                MakeProperty("text", &EditorNoteComponent::text).Display("メモ"));
            PropertyRegistry::Register<EditorNoteComponent>(
                MakeProperty("category", &EditorNoteComponent::category)
                    .Display("カテゴリ")
                    .AsEnum({ "TODO", "BUG", "ART", "PROGRAM", "LEVEL", "IDEA" }));
            PropertyRegistry::Register<EditorNoteComponent>(
                MakeProperty("priority", &EditorNoteComponent::priority)
                    .Display("優先度").AsEnum({ "Low", "Normal", "High", "Critical" }));
            PropertyRegistry::Register<EditorNoteComponent>(
                MakeProperty("completed", &EditorNoteComponent::completed).Display("完了"));
            PropertyRegistry::Register<EditorNoteComponent>(
                MakeProperty("show_in_viewport", &EditorNoteComponent::show_in_viewport)
                    .Display("Scene Viewに表示"));
            PropertyRegistry::Register<EditorNoteComponent>(
                MakeProperty("hide_when_completed", &EditorNoteComponent::hide_when_completed)
                    .Display("完了時に非表示"));
            PropertyRegistry::Register<EditorNoteComponent>(
                MakeProperty("color", &EditorNoteComponent::color)
                    .Display("文字色").AsColor());
            PropertyRegistry::Register<EditorNoteComponent>(
                MakeProperty("text_scale", &EditorNoteComponent::text_scale)
                    .Display("文字サイズ").Range(0.35, 4.0).Step(0.05));
            PropertyRegistry::Register<EditorNoteComponent>(
                MakeProperty("offset", &EditorNoteComponent::offset)
                    .Display("表示オフセット").Step(0.05));
        }
}
