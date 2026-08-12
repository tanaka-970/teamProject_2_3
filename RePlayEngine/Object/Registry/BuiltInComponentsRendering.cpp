#include "BuiltInComponentsInternal.h"

namespace ReplayEngine::Core::Detail
{
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

        void RegisterPrimitiveMeshRenderer()
        {
            ComponentRegistry::Register<PrimitiveMeshRendererComponent>(
                ComponentTypeInfo::Describe("Primitive Mesh Renderer", "Rendering")
                    .WithTooltip("Engine内蔵の Plane / Cube / Sphere / Capsule / Cylinder / Quad を描画する。外部Model Assetは参照しない。"));

            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("primitive_type", &PrimitiveMeshRendererComponent::primitive_type)
                    .Display("プリミティブ")
                    .AsEnum({ "Plane", "Cube", "Sphere", "Capsule", "Cylinder", "Quad" }));

            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("material_asset", &PrimitiveMeshRendererComponent::material_asset)
                    .Display("マテリアル").AsAssetPath()
                    .Tooltip("Material AssetのGUID。Projectパネルから割り当てる。"));

            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("material_override", &PrimitiveMeshRendererComponent::material_override)
                    .Display("マテリアル上書き")
                    .Tooltip("色と描画方式にRenderer側の値を使う。"));

            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("tint", &PrimitiveMeshRendererComponent::tint)
                    .Display("色").AsColor());

            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("shading_model", &PrimitiveMeshRendererComponent::shading_model)
                    .Display("描画方式")
                    .AsEnum({ "FBX標準", "PBR", "トゥーン", "アンリット" }));

            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("outline", &PrimitiveMeshRendererComponent::outline).Display("輪郭線"));
            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("cast_shadow", &PrimitiveMeshRendererComponent::cast_shadow).Display("影を落とす"));
            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("receive_shadow", &PrimitiveMeshRendererComponent::receive_shadow).Display("影を受ける"));
            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("visible", &PrimitiveMeshRendererComponent::visible).Display("表示"));
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
                    .WithTooltip("スケルタルアニメーションの data-driven State / Transition を再生する。"
                        "State が未設定の既存 Scene は従来の Idle / Walk / Jump 設定を使う。"));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeAccessorProperty<AnimatorComponent>("state_count", PropertyType::Int,
                    [](const AnimatorComponent& component)
                    { return PropertyValue::MakeInt(component.StateCount()); },
                    [](AnimatorComponent& component, const PropertyValue& value)
                    { component.SetStateCount(value.AsInt()); })
                .Display("State 数").Range(0.0, 64.0).Step(1.0)
                .Animation(Animatable::None)
                .Tooltip("0 のときだけ旧 Idle / Walk / Jump 互換経路を使う。"));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("default_state", &AnimatorComponent::default_state)
                    .Display("開始 State").Animation(Animatable::None)
                    .Tooltip("空または存在しない名前なら先頭 State から開始する。"));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeAccessorProperty<AnimatorComponent>("transition_count", PropertyType::Int,
                    [](const AnimatorComponent& component)
                    { return PropertyValue::MakeInt(component.TransitionCount()); },
                    [](AnimatorComponent& component, const PropertyValue& value)
                    { component.SetTransitionCount(value.AsInt()); })
                .Display("Transition 数").Range(0.0, 256.0).Step(1.0)
                .Animation(Animatable::None)
                .Tooltip("保存順が評価優先順位。1 フレームに成立させる遷移は 1 本だけ。"));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("playback_speed", &AnimatorComponent::playback_speed)
                    .Display("全体再生速度").Range(0.0, 5.0).Step(0.01));

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("playing", &AnimatorComponent::playing).Display("再生"));

            // 旧 Scene の保存名は絶対に消さない。State Machine を使う Scene では
            // advanced な互換値として残り、states が空なら従来どおり実処理にも使われる。
            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("idle_clip", &AnimatorComponent::idle_clip)
                    .Display("旧: 待機クリップ").Range(-1.0, 255.0).Step(1.0).Advanced());

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("walk_clip", &AnimatorComponent::walk_clip)
                    .Display("旧: 移動クリップ").Range(-1.0, 255.0).Step(1.0).Advanced());

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("jump_clip", &AnimatorComponent::jump_clip)
                    .Display("旧: ジャンプクリップ").Range(-1.0, 255.0).Step(1.0).Advanced());

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("loop", &AnimatorComponent::loop)
                    .Display("旧: ループ").Advanced());

            PropertyRegistry::Register<AnimatorComponent>(
                MakeProperty("walk_speed_threshold", &AnimatorComponent::walk_speed_threshold)
                    .Display("旧: 移動とみなす速度").Range(0.0, 10.0).Step(0.01).Advanced());
        }

        void RegisterPostProcessVolume()
        {
            ComponentRegistry::Register<PostProcessVolumeComponent>(
                ComponentTypeInfo::Describe("Post Process Volume", "Rendering")
                    .WithTooltip("Scene 上の優先度が最も高い Volume から PostEffect の値を反映する。"));

            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("priority", &PostProcessVolumeComponent::priority)
                    .Display("優先度").Range(-100.0, 100.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("bloom_enabled", &PostProcessVolumeComponent::bloom_enabled)
                    .Display("Bloom を使う").Animation(Animatable::Step));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("bloom_threshold", &PostProcessVolumeComponent::bloom_threshold)
                    .Display("Bloom しきい値").Range(0.0, 16.0).Step(0.01));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("bloom_intensity", &PostProcessVolumeComponent::bloom_intensity)
                    .Display("Bloom 強度").Range(0.0, 8.0).Step(0.01));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("vignette_enabled", &PostProcessVolumeComponent::vignette_enabled)
                    .Display("ビネットを使う").Animation(Animatable::Step));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("vignette_intensity", &PostProcessVolumeComponent::vignette_intensity)
                    .Display("ビネット強度").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("ssao_enabled", &PostProcessVolumeComponent::ssao_enabled)
                    .Display("SSAO を使う").Animation(Animatable::Step));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("ssao_radius", &PostProcessVolumeComponent::ssao_radius)
                    .Display("SSAO 半径").Range(0.01, 16.0).Step(0.01));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("ssao_intensity", &PostProcessVolumeComponent::ssao_intensity)
                    .Display("SSAO 強度").Range(0.0, 8.0).Step(0.01));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("ssr_enabled", &PostProcessVolumeComponent::ssr_enabled)
                    .Display("SSR を使う").Animation(Animatable::Step));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("ssr_intensity", &PostProcessVolumeComponent::ssr_intensity)
                    .Display("SSR 強度").Range(0.0, 8.0).Step(0.01));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("taa_enabled", &PostProcessVolumeComponent::taa_enabled)
                    .Display("TAA を使う").Animation(Animatable::Step));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("exposure", &PostProcessVolumeComponent::exposure)
                    .Display("露出").Range(0.01, 8.0).Step(0.01));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("color_filter", &PostProcessVolumeComponent::color_filter)
                    .Display("色フィルタ").AsColor());
        }

        void RegisterParticleEmitter()
        {
            ComponentRegistry::Register<ParticleEmitterComponent>(
                ComponentTypeInfo::Describe("Particle Emitter", "Rendering")
                    .WithTooltip("粒子単体ではなく、発生量や寿命などの Emitter Parameter を Motion から動かす。")
                    .AllowMultipleInstances());

            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("emitting", &ParticleEmitterComponent::emitting)
                    .Display("発生する").Animation(Animatable::Step));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("priority", &ParticleEmitterComponent::priority)
                    .Display("優先度").Range(-100.0, 100.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("spawn_rate", &ParticleEmitterComponent::spawn_rate)
                    .Display("発生量/秒").Range(0.0, 20000.0).Step(1.0));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("lifetime", &ParticleEmitterComponent::lifetime)
                    .Display("寿命").Range(0.01, 60.0).Step(0.01));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("start_speed", &ParticleEmitterComponent::start_speed)
                    .Display("初速").Range(0.0, 200.0).Step(0.05));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("gravity", &ParticleEmitterComponent::gravity)
                    .Display("重力").Range(-100.0, 100.0).Step(0.05));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("drag", &ParticleEmitterComponent::drag)
                    .Display("抵抗").Range(0.0, 20.0).Step(0.01));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("start_size", &ParticleEmitterComponent::start_size)
                    .Display("開始サイズ").Range(0.001, 100.0).Step(0.01));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("end_size", &ParticleEmitterComponent::end_size)
                    .Display("終了サイズ").Range(0.001, 100.0).Step(0.01));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("start_color", &ParticleEmitterComponent::start_color)
                    .Display("開始色").AsColor());
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("end_color", &ParticleEmitterComponent::end_color)
                    .Display("終了色").AsColor());
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("direction", &ParticleEmitterComponent::direction)
                    .Display("発生方向").Step(0.01));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("cone_angle", &ParticleEmitterComponent::cone_angle)
                    .Display("拡散角").Range(0.0, 3.14159).Step(0.01));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("sprite", &ParticleEmitterComponent::sprite)
                    .Display("画像").OfAssetType("Image")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("blend_mode", &ParticleEmitterComponent::blend_mode)
                    .Display("合成").AsEnum({ "通常", "加算", "乗算", "スクリーン" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ParticleEmitterComponent>(
                MakeProperty("max_particles", &ParticleEmitterComponent::max_particles)
                    .Display("最大数").Range(1.0, 10000.0).Step(1.0)
                    .Animation(Animatable::Step));
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
}
