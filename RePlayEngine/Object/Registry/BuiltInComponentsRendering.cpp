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
                MakeAccessorProperty<MeshRendererComponent>("material_slot_count", PropertyType::Int,
                    [](const MeshRendererComponent& component)
                    { return PropertyValue::MakeInt(ClampedMaterialSlotCount(component)); },
                    [](MeshRendererComponent& component, const PropertyValue& value)
                    { SetMaterialSlotCount(component, value.AsInt()); })
                    .HiddenInEditor().NotAnimatable());

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
                MakeProperty("shadow_alpha_clip", &MeshRendererComponent::shadow_alpha_clip)
                    .Display("影をアルファで抜く"));
            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("shadow_alpha_cutoff", &MeshRendererComponent::shadow_alpha_cutoff)
                    .Display("影の抜きしきい値").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<MeshRendererComponent>(
                MakeProperty("rendering_layer", &MeshRendererComponent::rendering_layer)
                    .Display("Rendering Layer").Range(0.0, 31.0).Step(1.0));

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
                MakeAccessorProperty<PrimitiveMeshRendererComponent>("material_slot_count", PropertyType::Int,
                    [](const PrimitiveMeshRendererComponent& component)
                    { return PropertyValue::MakeInt(ClampedMaterialSlotCount(component)); },
                    [](PrimitiveMeshRendererComponent& component, const PropertyValue& value)
                    { SetMaterialSlotCount(component, value.AsInt()); })
                    .HiddenInEditor().NotAnimatable());

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
                MakeProperty("shadow_alpha_clip", &PrimitiveMeshRendererComponent::shadow_alpha_clip)
                    .Display("影をアルファで抜く"));
            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("shadow_alpha_cutoff", &PrimitiveMeshRendererComponent::shadow_alpha_cutoff)
                    .Display("影の抜きしきい値").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<PrimitiveMeshRendererComponent>(
                MakeProperty("rendering_layer", &PrimitiveMeshRendererComponent::rendering_layer)
                    .Display("Rendering Layer").Range(0.0, 31.0).Step(1.0));
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
                MakeAccessorProperty<SkinnedMeshRendererComponent>("material_slot_count", PropertyType::Int,
                    [](const SkinnedMeshRendererComponent& component)
                    { return PropertyValue::MakeInt(ClampedMaterialSlotCount(component)); },
                    [](SkinnedMeshRendererComponent& component, const PropertyValue& value)
                    { SetMaterialSlotCount(component, value.AsInt()); })
                    .HiddenInEditor().NotAnimatable());

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
                MakeProperty("shadow_alpha_clip", &SkinnedMeshRendererComponent::shadow_alpha_clip)
                    .Display("影をアルファで抜く"));
            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("shadow_alpha_cutoff", &SkinnedMeshRendererComponent::shadow_alpha_cutoff)
                    .Display("影の抜きしきい値").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<SkinnedMeshRendererComponent>(
                MakeProperty("rendering_layer", &SkinnedMeshRendererComponent::rendering_layer)
                    .Display("Rendering Layer").Range(0.0, 31.0).Step(1.0));

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

            PropertyRegistry::Register<AnimatorComponent>(
                MakeAccessorProperty<AnimatorComponent>("current_state", PropertyType::String,
                    [](const AnimatorComponent& component)
                    { return PropertyValue::MakeString(component.CurrentStateName()); },
                    [](AnimatorComponent&, const PropertyValue&) {})
                .Display("現在の State").RuntimeOnly().ReadOnly().NotSerializable());
            PropertyRegistry::Register<AnimatorComponent>(
                MakeAccessorProperty<AnimatorComponent>("current_clip", PropertyType::Int,
                    [](const AnimatorComponent& component)
                    { return PropertyValue::MakeInt(component.CurrentClip()); },
                    [](AnimatorComponent&, const PropertyValue&) {})
                .Display("現在のクリップ").RuntimeOnly().ReadOnly().NotSerializable());
            PropertyRegistry::Register<AnimatorComponent>(
                MakeAccessorProperty<AnimatorComponent>("animation_time", PropertyType::Float,
                    [](const AnimatorComponent& component)
                    { return PropertyValue::MakeFloat(component.AnimationTime()); },
                    [](AnimatorComponent&, const PropertyValue&) {})
                .Display("再生時間").RuntimeOnly().ReadOnly().NotSerializable());
            PropertyRegistry::Register<AnimatorComponent>(
                MakeAccessorProperty<AnimatorComponent>("blend_factor", PropertyType::Float,
                    [](const AnimatorComponent& component)
                    { return PropertyValue::MakeFloat(component.BlendFactor()); },
                    [](AnimatorComponent&, const PropertyValue&) {})
                .Display("ブレンド率").RuntimeOnly().ReadOnly().NotSerializable());

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
                    .WithTooltip("Scene 上の優先度が最も高い Volume から PostEffect の値を反映する。")
                    .InModule("RePlayEngine.Optional.Effects"));

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
                MakeProperty("luminance_enabled", &PostProcessVolumeComponent::luminance_enabled)
                    .Display(u8"輝度抽出を使う").Animation(Animatable::Step));
            PropertyRegistry::Register<PostProcessVolumeComponent>(
                MakeProperty("final_pass_enabled", &PostProcessVolumeComponent::final_pass_enabled)
                    .Display(u8"最終合成を使う").Animation(Animatable::Step));
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
                    .AllowMultipleInstances()
                    .InModule("RePlayEngine.Optional.Effects"));

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

        template<typename ComponentType>
        void RegisterLineStrokeProperties()
        {
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("width_start", &ComponentType::width_start)
                    .Display("始点の太さ").Range(0.0, 1000.0).Step(0.001)
                    .Tooltip("ライン始点の幅（ワールド単位）。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("width_end", &ComponentType::width_end)
                    .Display("終点の太さ").Range(0.0, 1000.0).Step(0.001)
                    .Tooltip("途中の幅は始点から終点へ線形補間する。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("billboard", &ComponentType::billboard)
                    .Display("カメラへ向ける")
                    .Tooltip("無効なら進行方向とワールド上方向から帯の向きを決める。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("uv_mode", &ComponentType::uv_mode)
                    .Display("UV の張り方")
                    .AsEnum({ "全長へ伸縮", "距離で繰り返し" })
                    .Animation(Animatable::Step)
                    .Tooltip("全長を 0..1 に伸縮するか、ワールド距離で反復するかを選ぶ。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("uv_tiling", &ComponentType::uv_tiling)
                    .Display("繰り返し").Range(0.0001, 10000.0).Step(0.001)
                    .Tooltip("距離で繰り返すときの 1 周期の長さ。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("uv_scroll", &ComponentType::uv_scroll)
                    .Display("UV スクロール").Step(0.001)
                    .Tooltip("Motion で動かすとテクスチャが線に沿って流れる。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("texture", &ComponentType::texture)
                    .Display("テクスチャ").OfAssetType("Image")
                    .Animation(Animatable::Step)
                    .Tooltip("リボンへ貼る画像。未設定なら白テクスチャを使う。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("fill_color", &ComponentType::fill_color)
                    .Display("色").AsColor()
                    .Tooltip("単色、または線形グラデーションの始点色。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("fill_color_2", &ComponentType::fill_color_2)
                    .Display("色 2").AsColor()
                    .Tooltip("線形グラデーションの終点色。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("fill_mode", &ComponentType::fill_mode)
                    .Display("塗り方").AsEnum({ "単色", "線形" })
                    .Animation(Animatable::Step)
                    .Tooltip("線形は線に沿った U 方向で色 1 から色 2 へ補間する。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("trim_start", &ComponentType::trim_start)
                    .Display("Trim 開始").Range(0.0, 1.0).Step(0.001)
                    .Tooltip("全長を 0..1 としたときの描画開始位置。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("trim_end", &ComponentType::trim_end)
                    .Display("Trim 終了").Range(0.0, 1.0).Step(0.001)
                    .Tooltip("Motion で 0 から 1 へ動かすと線を書き出せる。"));
            PropertyRegistry::Register<ComponentType>(
                MakeProperty("trim_offset", &ComponentType::trim_offset)
                    .Display("Trim オフセット").Step(0.001)
                    .Tooltip("Trim 区間を全長に沿って循環移動する。"));
        }

        void RegisterLineRenderers()
        {
            ComponentRegistry::Register<LineRendererComponent>(
                ComponentTypeInfo::Describe("3D ライン", "Rendering")
                    .WithTooltip("ローカル座標の点を自由に置き、Motion で各点を動かせる 3D リボン。")
                    .AllowMultipleInstances());
            PropertyRegistry::Register<LineRendererComponent>(
                MakeProperty("point_count", &LineRendererComponent::point_count)
                    .Display("点の数").Range(0.0, 2147483647.0).Step(1.0)
                    .Animation(Animatable::None)
                    .Tooltip("固定上限はない。増やすと points[i] が動的に増える。"));
            PropertyRegistry::Register<LineRendererComponent>(
                MakeProperty("smoothing", &LineRendererComponent::smoothing)
                    .Display("曲線の滑らかさ").Range(0.0, 255.0).Step(1.0)
                    .Animation(Animatable::Step)
                    .Tooltip("1 以上で制御点を必ず通る Catmull-Rom 曲線にする。"));
            PropertyRegistry::Register<LineRendererComponent>(
                MakeProperty("closed", &LineRendererComponent::closed)
                    .Display("閉じる").Animation(Animatable::Step)
                    .Tooltip("最後の点と最初の点をつないで閉じた線にする。"));
            RegisterLineStrokeProperties<LineRendererComponent>();

            ComponentRegistry::Register<TrailComponent>(
                ComponentTypeInfo::Describe("軌跡", "Rendering")
                    .WithTooltip("Transform 確定後の移動履歴から、自動で同じ 3D リボンを生成する。")
                    .AllowMultipleInstances()
                    .InModule("RePlayEngine.Optional.Effects"));
            PropertyRegistry::Register<TrailComponent>(
                MakeProperty("emitting", &TrailComponent::emitting)
                    .Display("発生中").Animation(Animatable::Step)
                    .Tooltip("無効にすると新しい点を止め、既存の軌跡だけを寿命まで残す。"));
            PropertyRegistry::Register<TrailComponent>(
                MakeProperty("lifetime", &TrailComponent::lifetime)
                    .Display("寿命（秒）").Range(0.0, 3600.0).Step(0.001)
                    .Tooltip("点数ではなく寿命で自然な上限を決める。"));
            PropertyRegistry::Register<TrailComponent>(
                MakeProperty("min_distance", &TrailComponent::min_distance)
                    .Display("最小間隔").Range(0.0, 10000.0).Step(0.001)
                    .Tooltip("前の点からこの距離以上動いたときだけ点を足す。"));
            PropertyRegistry::Register<TrailComponent>(
                MakeProperty("max_points", &TrailComponent::max_points)
                    .Display("点数の上限").Range(0.0, 2147483647.0).Step(1.0)
                    .Animation(Animatable::Step)
                    .Tooltip("0 は上限なし。暴走を抑えたい場面だけ指定する保険。"));
            PropertyRegistry::Register<TrailComponent>(
                MakeProperty("world_space", &TrailComponent::world_space)
                    .Display("ワールド空間に残す").Animation(Animatable::Step)
                    .Tooltip("有効なら親が動いても軌跡は通過位置に残る。"));
            RegisterLineStrokeProperties<TrailComponent>();

            // ---- 拡張点: 3D ラインの表現 ---------------------------------
            //
            // 【今は入れていない理由】
            //   破線、端キャップ、点ごとの色・太さは、共通リボン生成と
            //   Motion 駆動の形を先に安定させるため今回の最小核から外した。
            // 【入れるときにここへ足す】
            //   両 Component へ同名 Property を追加し、LineStrokeRenderer の
            //   共通頂点生成だけを拡張する。旧 trail とは統合しない。
            // 【壊してはいけない前提】
            //   points と履歴に固定長配列を導入せず、Line / Trail の描画実装を分岐させない。
        }

        void RegisterLights()
        {
            ComponentRegistry::Register<DirectionalLightComponent>(
                ComponentTypeInfo::Describe("Directional Light", "Lighting")
                    .WithTooltip("GameObjectの回転方向から照らす平行光源。Scene内の先頭1つを使用。")
                    .WithVersion(2));
            PropertyRegistry::Register<DirectionalLightComponent>(
                MakeProperty("color", &DirectionalLightComponent::color).Display("色").AsColor());
            PropertyRegistry::Register<DirectionalLightComponent>(
                MakeProperty("intensity", &DirectionalLightComponent::intensity)
                    .Display("強さ").Range(0.0, 100.0).Step(0.05));
            // ---- 影 (Unity の Light > Shadow Type / Unreal の Cast Shadows) ----
            PropertyRegistry::Register<DirectionalLightComponent>(
                MakeProperty("cast_shadows", &DirectionalLightComponent::cast_shadows)
                    .Display("影を落とす"));
            PropertyRegistry::Register<DirectionalLightComponent>(
                MakeProperty("shadow_strength", &DirectionalLightComponent::shadow_strength)
                    .Display("影の濃さ").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<DirectionalLightComponent>(
                MakeProperty("shadow_depth_bias", &DirectionalLightComponent::shadow_depth_bias)
                    .Display("深度バイアス (m)").Range(0.0, 0.5).Step(0.001));
            PropertyRegistry::Register<DirectionalLightComponent>(
                MakeProperty("shadow_normal_bias", &DirectionalLightComponent::shadow_normal_bias)
                    .Display("法線バイアス").Range(0.0, 6.0).Step(0.05));
            PropertyRegistry::Register<DirectionalLightComponent>(
                MakeProperty("shadow_distance", &DirectionalLightComponent::shadow_distance)
                    .Display("影の最遠距離").Range(1.0, 2000.0).Step(1.0));

            ComponentRegistry::Register<PointLightComponent>(
                ComponentTypeInfo::Describe("Point Light", "Lighting")
                    .WithTooltip("Transform位置を中心に全方向へ照らす。")
                    .WithVersion(2));
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("color", &PointLightComponent::color).Display("色").AsColor());
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("intensity", &PointLightComponent::intensity)
                    .Display("強さ").Range(0.0, 100.0).Step(0.05));
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("range", &PointLightComponent::range)
                    .Display("範囲").Range(0.01, 10000.0).Step(0.1));
            // ---- 影 ---- Point は 1 灯で 6 面描くため既定は OFF。
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("cast_shadows", &PointLightComponent::cast_shadows)
                    .Display("影を落とす"));
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("shadow_strength", &PointLightComponent::shadow_strength)
                    .Display("影の濃さ").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("shadow_depth_bias", &PointLightComponent::shadow_depth_bias)
                    .Display("深度バイアス (m)").Range(0.0, 0.5).Step(0.001));
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("shadow_normal_bias", &PointLightComponent::shadow_normal_bias)
                    .Display("法線バイアス").Range(0.0, 6.0).Step(0.05));
            PropertyRegistry::Register<PointLightComponent>(
                MakeProperty("shadow_near_plane", &PointLightComponent::shadow_near_plane)
                    .Display("影のニア").Range(0.01, 10.0).Step(0.01));

            ComponentRegistry::Register<SpotLightComponent>(
                ComponentTypeInfo::Describe("Spot Light", "Lighting")
                    .WithTooltip("Transform位置と回転で円錐状に照らす。")
                    .WithVersion(2));
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
            // ---- 影 ----
            PropertyRegistry::Register<SpotLightComponent>(
                MakeProperty("cast_shadows", &SpotLightComponent::cast_shadows)
                    .Display("影を落とす"));
            PropertyRegistry::Register<SpotLightComponent>(
                MakeProperty("shadow_strength", &SpotLightComponent::shadow_strength)
                    .Display("影の濃さ").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<SpotLightComponent>(
                MakeProperty("shadow_depth_bias", &SpotLightComponent::shadow_depth_bias)
                    .Display("深度バイアス (m)").Range(0.0, 0.5).Step(0.001));
            PropertyRegistry::Register<SpotLightComponent>(
                MakeProperty("shadow_normal_bias", &SpotLightComponent::shadow_normal_bias)
                    .Display("法線バイアス").Range(0.0, 6.0).Step(0.05));
            PropertyRegistry::Register<SpotLightComponent>(
                MakeProperty("shadow_near_plane", &SpotLightComponent::shadow_near_plane)
                    .Display("影のニア").Range(0.01, 10.0).Step(0.01));
        }
        void RegisterEffectStacks()
        {
            ComponentRegistry::Register<ScreenEffectStackComponent>(
                ComponentTypeInfo::Describe("Screen Effect Stack", "Rendering")
                    .WithTooltip("3D シーン全体へ Effect Chain を適用します。UI は対象外です。")
                    .InModule("RePlayEngine.Optional.Effects"));
            PropertyRegistry::Register<ScreenEffectStackComponent>(
                MakeProperty("enabled", &ScreenEffectStackComponent::enabled)
                    .Display(u8"効果を適用")
                    .Tooltip(u8"ヘッダー左のチェックとは別。両方が入っていないと効果は出ません。")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ScreenEffectStackComponent>(
                MakeProperty("use_preset", &ScreenEffectStackComponent::use_preset)
                    .Display("Preset を使用").Animation(Animatable::Step));
            PropertyRegistry::Register<ScreenEffectStackComponent>(
                MakeProperty("effect_preset", &ScreenEffectStackComponent::effect_preset)
                    .Display("Effect Preset").OfAssetType("EffectPreset")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ScreenEffectStackComponent>(
                MakeProperty("effect_count", &ScreenEffectStackComponent::effect_count)
                    .Display("Effect 数").Range(0.0, 16.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ScreenEffectStackComponent>(
                MakeProperty("apply_stage", &ScreenEffectStackComponent::apply_stage)
                    .Display("適用位置")
                    .AsEnum({ "PostProcess 後", "PostProcess 前" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ScreenEffectStackComponent>(
                MakeProperty("target_mode", &ScreenEffectStackComponent::target_mode)
                    .Display("適用対象")
                    .AsEnum({ "画面全体", "背景だけ", "Rendering Layer" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ScreenEffectStackComponent>(
                MakeProperty("target_rendering_layer", &ScreenEffectStackComponent::target_rendering_layer)
                    .Display("Rendering Layer").Range(0.0, 31.0).Step(1.0)
                    .Tooltip("Rendering Layer 対象時に Effect を掛ける layer 番号。")
                    .Animation(Animatable::Step));

            ComponentRegistry::Register<ModelEffectStackComponent>(
                ComponentTypeInfo::Describe("Model Effect Stack", "Rendering")
                    .WithTooltip("この GameObject のモデルだけへ Effect Chain を適用します。")
                    .InModule("RePlayEngine.Optional.Effects")
                    .AllowMultipleInstances());
            PropertyRegistry::Register<ModelEffectStackComponent>(
                MakeProperty("enabled", &ModelEffectStackComponent::enabled)
                    .Display(u8"効果を適用")
                    .Tooltip(u8"ヘッダー左のチェックとは別。両方が入っていないと効果は出ません。")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ModelEffectStackComponent>(
                MakeProperty("use_preset", &ModelEffectStackComponent::use_preset)
                    .Display("Preset を使用").Animation(Animatable::Step));
            PropertyRegistry::Register<ModelEffectStackComponent>(
                MakeProperty("effect_preset", &ModelEffectStackComponent::effect_preset)
                    .Display("Effect Preset").OfAssetType("EffectPreset")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ModelEffectStackComponent>(
                MakeProperty("effect_count", &ModelEffectStackComponent::effect_count)
                    .Display("Effect 数").Range(0.0, 16.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ModelEffectStackComponent>(
                MakeProperty("target_slot_mode", &ModelEffectStackComponent::target_slot_mode)
                    .Display(u8"対象")
                    .AsEnum({ u8"モデル全体", u8"マテリアルスロット" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ModelEffectStackComponent>(
                MakeProperty("depth_mode", &ModelEffectStackComponent::depth_mode)
                    .Display("深度モード")
                    .AsEnum({ "Preserve Depth", "Overlay" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ModelEffectStackComponent>(
                MakeProperty("extract_mode", &ModelEffectStackComponent::extract_mode)
                    .Display(u8"合成方法")
                    .AsEnum({ u8"自動", u8"その場", u8"切り抜き" })
                    .Tooltip(u8"切り抜きは GBuffer から除外するため SSAO・SSR などの照明結果が変わります。")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<ModelEffectStackComponent>(
                MakeProperty("max_bleed_pixels", &ModelEffectStackComponent::max_bleed_pixels)
                    .Display(u8"最大はみ出し (px)").Range(0.0, 1024.0).Step(1.0)
                    .Tooltip(u8"エフェクトがモデルの画面矩形から各辺へはみ出せる上限 (px)。")
                    .Animation(Animatable::Interpolatable));
        }

}
