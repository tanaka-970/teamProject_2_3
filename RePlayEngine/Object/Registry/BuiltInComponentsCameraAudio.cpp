#include "BuiltInComponentsInternal.h"

namespace ReplayEngine::Core::Detail
{
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
                MakeProperty("priority", &CameraTargetComponent::priority)
                    .Display("優先度").Range(-100.0, 100.0).Step(1.0));
        }

        void RegisterCamera()
        {
            ComponentRegistry::Register<CameraComponent>(
                ComponentTypeInfo::Describe("Camera", "Camera")
                    .WithTooltip("GameObject の Transform を姿勢として使う Runtime Camera。"));

            PropertyRegistry::Register<CameraComponent>(
                MakeProperty("projection_mode", &CameraComponent::projection_mode)
                    .Display("投影方式")
                    .AsEnum({ "Perspective", "Orthographic" }));

            PropertyRegistry::Register<CameraComponent>(
                MakeProperty("field_of_view_degrees",
                    &CameraComponent::field_of_view_degrees)
                    .Display("視野角 (度)").Range(1.0, 179.0).Step(0.5));

            PropertyRegistry::Register<CameraComponent>(
                MakeProperty("orthographic_size", &CameraComponent::orthographic_size)
                    .Display("Orthographic Size").Range(0.01, 10000.0).Step(0.1));

            PropertyRegistry::Register<CameraComponent>(
                MakeProperty("near_clip", &CameraComponent::near_clip)
                    .Display("Near Clip").Range(0.001, 10.0).Step(0.01));

            PropertyRegistry::Register<CameraComponent>(
                MakeProperty("far_clip", &CameraComponent::far_clip)
                    .Display("Far Clip").Range(10.0, 100000.0).Step(10.0));

            PropertyRegistry::Register<CameraComponent>(
                MakeProperty("priority", &CameraComponent::priority)
                    .Display("優先度").Range(-100.0, 100.0).Step(1.0));

            PropertyRegistry::Register<CameraComponent>(
                MakeProperty("viewport_enabled", &CameraComponent::viewport_enabled)
                    .Display("分割 Viewport を使う")
                    .Animation(Animatable::Step)
                    .Tooltip("有効な Camera が 1 台でもあれば、各 Camera を viewport_rect へ合成する。"));

            PropertyRegistry::Register<CameraComponent>(
                MakeProperty("viewport_rect", &CameraComponent::viewport_rect)
                    .Display("Viewport (x y w h)").Range(0.0, 1.0).Step(0.01)
                    .Tooltip("画面左上を (0,0)、右下を (1,1) とする正規化矩形。"));
        }

        void RegisterFollowTarget()
        {
            ComponentRegistry::Register<FollowTargetComponent>(
                ComponentTypeInfo::Describe("Follow Target", "Camera")
                    .WithTooltip("同じ GameObject の Camera を Camera Target へ追従させる。")
                    .Requires<CameraComponent>());

            PropertyRegistry::Register<FollowTargetComponent>(
                MakeProperty("follow_distance", &FollowTargetComponent::follow_distance)
                    .Display("追従距離").Range(0.5, 100.0).Step(0.1));

            PropertyRegistry::Register<FollowTargetComponent>(
                MakeProperty("follow_height", &FollowTargetComponent::follow_height)
                    .Display("追従高さ").Range(-10.0, 50.0).Step(0.1));

            PropertyRegistry::Register<FollowTargetComponent>(
                MakeProperty("follow_lag", &FollowTargetComponent::follow_lag)
                    .Display("追従の速さ").Range(0.0, 60.0).Step(0.1));

            PropertyRegistry::Register<FollowTargetComponent>(
                MakeProperty("rotation_input_enabled",
                    &FollowTargetComponent::rotation_input_enabled)
                    .Display("回転入力を使う"));

            PropertyRegistry::Register<FollowTargetComponent>(
                MakeProperty("yield_to_motion", &FollowTargetComponent::yield_to_motion)
                    .Display("Motion 中は追従を止める")
                    .Tooltip("Motion が同じフレームに Transform を書いた場合は、追従側の上書きを避ける。"));

            PropertyRegistry::Register<FollowTargetComponent>(
                MakeProperty("yaw_offset", &FollowTargetComponent::yaw_offset)
                    .Display("水平回転オフセット").Step(0.01));

            PropertyRegistry::Register<FollowTargetComponent>(
                MakeProperty("pitch_offset", &FollowTargetComponent::pitch_offset)
                    .Display("垂直回転オフセット").Range(-1.4, 1.4).Step(0.01));
        }

        void RegisterAudioListener()
        {
            ComponentRegistry::Register<AudioListenerComponent>(
                ComponentTypeInfo::Describe("Audio Listener", "Audio")
                    .WithTooltip("Transform の位置と回転を 3D Audio の聞く位置として使う。"));

            PropertyRegistry::Register<AudioListenerComponent>(
                MakeProperty("priority", &AudioListenerComponent::priority)
                    .Display("優先度").Range(-100.0, 100.0).Step(1.0));
        }

        void RegisterAudioSource()
        {
            ComponentRegistry::Register<AudioSourceComponent>(
                ComponentTypeInfo::Describe("Audio Source", "Audio")
                    .WithTooltip("PCM .wav を直接パス指定で再生する。")
                    .AllowMultipleInstances());

            PropertyRegistry::Register<AudioSourceComponent>(
                MakeProperty("clip_path", &AudioSourceComponent::clip_path)
                    .Display("Clip Path")
                    .Tooltip("PCM .wav のファイルパス。AssetDatabase へは統合しない。"));

            PropertyRegistry::Register<AudioSourceComponent>(
                MakeProperty("loop", &AudioSourceComponent::loop)
                    .Display("Loop"));

            PropertyRegistry::Register<AudioSourceComponent>(
                MakeProperty("volume", &AudioSourceComponent::volume)
                    .Display("Volume").Range(0.0, 4.0).Step(0.01));

            PropertyRegistry::Register<AudioSourceComponent>(
                MakeProperty("pitch", &AudioSourceComponent::pitch)
                    .Display("Pitch").Range(0.25, 4.0).Step(0.01));

            PropertyRegistry::Register<AudioSourceComponent>(
                MakeProperty("play_on_start", &AudioSourceComponent::play_on_start)
                    .Display("Play On Start"));

            PropertyRegistry::Register<AudioSourceComponent>(
                MakeProperty("spatial", &AudioSourceComponent::spatial)
                    .Display("Spatial")
                    .AsEnum({ "2D", "3D" }));

            PropertyRegistry::Register<AudioSourceComponent>(
                MakeProperty("min_distance", &AudioSourceComponent::min_distance)
                    .Display("Min Distance").Range(0.0, 100000.0).Step(0.1));

            PropertyRegistry::Register<AudioSourceComponent>(
                MakeProperty("max_distance", &AudioSourceComponent::max_distance)
                    .Display("Max Distance").Range(0.001, 100000.0).Step(0.1));
        }
}
