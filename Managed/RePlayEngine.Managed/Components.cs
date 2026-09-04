using System;

namespace ReplayEngine;

// C++ Component の型付き入口。
//
// 値は ComponentAccessor 経由で PropertyRegistry の名前を読み書きするだけ。
// C++ 側でプロパティ名を変えたらここも直す。名前は Scene ファイルのキーと同じ。

public readonly struct CameraComponent : IComponentBinding<CameraComponent>
{
    public static string NativeTypeName => "CameraComponent";
    public static CameraComponent FromHandle(ComponentHandle handle) => new(handle);

    private CameraComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    // 0 = 透視投影、1 = 平行投影。
    public int ProjectionMode
    {
        get => Accessor.GetInt("projection_mode");
        set => Accessor.SetInt("projection_mode", value);
    }
    public float FieldOfView
    {
        get => Accessor.GetFloat("field_of_view_degrees", 60.0f);
        set => Accessor.SetFloat("field_of_view_degrees", value);
    }
    public float OrthographicSize
    {
        get => Accessor.GetFloat("orthographic_size", 5.0f);
        set => Accessor.SetFloat("orthographic_size", value);
    }
    public float NearClip
    {
        get => Accessor.GetFloat("near_clip", 0.1f);
        set => Accessor.SetFloat("near_clip", value);
    }
    public float FarClip
    {
        get => Accessor.GetFloat("far_clip", 1000.0f);
        set => Accessor.SetFloat("far_clip", value);
    }
    public int Priority
    {
        get => Accessor.GetInt("priority");
        set => Accessor.SetInt("priority", value);
    }
    public bool ViewportEnabled
    {
        get => Accessor.GetBool("viewport_enabled");
        set => Accessor.SetBool("viewport_enabled", value);
    }
    public Vector4 ViewportRect
    {
        get => Accessor.GetVector4("viewport_rect");
        set => Accessor.SetVector4("viewport_rect", value);
    }
}

public readonly struct AnimatorComponent : IComponentBinding<AnimatorComponent>
{
    public static string NativeTypeName => "AnimatorComponent";
    public static AnimatorComponent FromHandle(ComponentHandle handle) => new(handle);

    private AnimatorComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Playing
    {
        get => Accessor.GetBool("playing", true);
        set => Accessor.SetBool("playing", value);
    }
    public float PlaybackSpeed
    {
        get => Accessor.GetFloat("playback_speed", 1.0f);
        set => Accessor.SetFloat("playback_speed", value);
    }
    public bool Loop
    {
        get => Accessor.GetBool("loop", true);
        set => Accessor.SetBool("loop", value);
    }
    public int DefaultState
    {
        get => Accessor.GetInt("default_state");
        set => Accessor.SetInt("default_state", value);
    }
    public int IdleClip
    {
        get => Accessor.GetInt("idle_clip");
        set => Accessor.SetInt("idle_clip", value);
    }
    public int WalkClip
    {
        get => Accessor.GetInt("walk_clip");
        set => Accessor.SetInt("walk_clip", value);
    }
    public int JumpClip
    {
        get => Accessor.GetInt("jump_clip");
        set => Accessor.SetInt("jump_clip", value);
    }
    public float WalkSpeedThreshold
    {
        get => Accessor.GetFloat("walk_speed_threshold");
        set => Accessor.SetFloat("walk_speed_threshold", value);
    }

    public string CurrentState => Accessor.GetString("current_state");
    public int CurrentClip => Accessor.GetInt("current_clip", -1);
    public float AnimationTime => Accessor.GetFloat("animation_time");
    public float BlendFactor => Accessor.GetFloat("blend_factor", 1.0f);

    public RuntimeStatus Play(string stateName, float blendTime = 0.0f,
        float startTime = 0.0f) => NativeBridge.InvokeComponentCommand(Handle,
            ComponentCommand.AnimatorPlayState, stateName, blendTime, startTime);
    public RuntimeStatus Play() => NativeBridge.InvokeComponentCommand(Handle,
        ComponentCommand.AnimatorResume);
    public RuntimeStatus Pause() => NativeBridge.InvokeComponentCommand(Handle,
        ComponentCommand.AnimatorPause);
    public RuntimeStatus Resume() => NativeBridge.InvokeComponentCommand(Handle,
        ComponentCommand.AnimatorResume);
    public RuntimeStatus Stop() => NativeBridge.InvokeComponentCommand(Handle,
        ComponentCommand.AnimatorStop);
    public RuntimeStatus SetBool(string name, bool value) => NativeBridge.InvokeComponentCommand(
        Handle, ComponentCommand.AnimatorSetBool, name, integer: value ? 1 : 0);
    public RuntimeStatus SetFloat(string name, float value) => NativeBridge.InvokeComponentCommand(
        Handle, ComponentCommand.AnimatorSetFloat, name, value);
    public RuntimeStatus SetTrigger(string name) => NativeBridge.InvokeComponentCommand(
        Handle, ComponentCommand.AnimatorSetTrigger, name);
    public RuntimeStatus ResetTrigger(string name) => NativeBridge.InvokeComponentCommand(
        Handle, ComponentCommand.AnimatorResetTrigger, name);
}

public readonly struct MeshRendererComponent : IComponentBinding<MeshRendererComponent>
{
    public static string NativeTypeName => "MeshRendererComponent";
    public static MeshRendererComponent FromHandle(ComponentHandle handle) => new(handle);

    private MeshRendererComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Visible
    {
        get => Accessor.GetBool("visible", true);
        set => Accessor.SetBool("visible", value);
    }
    public Color Tint
    {
        get => Accessor.GetColor("tint");
        set => Accessor.SetColor("tint", value);
    }
    public bool CastShadow
    {
        get => Accessor.GetBool("cast_shadow", true);
        set => Accessor.SetBool("cast_shadow", value);
    }
    public bool ReceiveShadow
    {
        get => Accessor.GetBool("receive_shadow", true);
        set => Accessor.SetBool("receive_shadow", value);
    }
    public bool Outline
    {
        get => Accessor.GetBool("outline");
        set => Accessor.SetBool("outline", value);
    }
    public int RenderingLayer
    {
        get => Accessor.GetInt("rendering_layer");
        set => Accessor.SetInt("rendering_layer", value);
    }
    public string MeshAsset
    {
        get => Accessor.GetString("mesh_asset");
        set => Accessor.SetString("mesh_asset", value);
    }
    public AssetReference<ModelAsset> MeshReference
    {
        get => new(MeshAsset);
        set => MeshAsset = value.AssetGuid;
    }
    public string MaterialAsset
    {
        get => Accessor.GetString("material_asset");
        set => Accessor.SetString("material_asset", value);
    }
    public AssetReference<ReplayEngine.MaterialAsset> MaterialReference
    {
        get => new(MaterialAsset);
        set => MaterialAsset = value.AssetGuid;
    }
    public bool MaterialOverride
    {
        get => Accessor.GetBool("material_override");
        set => Accessor.SetBool("material_override", value);
    }
    public Color MaterialBaseColor
    {
        get => Accessor.GetColor("material.base_color");
        set => Accessor.SetColor("material.base_color", value);
    }
    public float Metallic
    {
        get => Accessor.GetFloat("material.metallic");
        set => Accessor.SetFloat("material.metallic", value);
    }
    public float Roughness
    {
        get => Accessor.GetFloat("material.roughness", 0.55f);
        set => Accessor.SetFloat("material.roughness", value);
    }
    public Vector3 EmissiveColor
    {
        get => Accessor.GetVector3("material.emissive_color");
        set => Accessor.SetVector3("material.emissive_color", value);
    }
    public float EmissiveStrength
    {
        get => Accessor.GetFloat("material.emissive_strength");
        set => Accessor.SetFloat("material.emissive_strength", value);
    }
}

public readonly struct SkinnedMeshRendererComponent : IComponentBinding<SkinnedMeshRendererComponent>
{
    public static string NativeTypeName => "SkinnedMeshRendererComponent";
    public static SkinnedMeshRendererComponent FromHandle(ComponentHandle handle) => new(handle);

    private SkinnedMeshRendererComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Visible
    {
        get => Accessor.GetBool("visible", true);
        set => Accessor.SetBool("visible", value);
    }
    public Color Tint
    {
        get => Accessor.GetColor("tint");
        set => Accessor.SetColor("tint", value);
    }
    public bool CastShadow
    {
        get => Accessor.GetBool("cast_shadow", true);
        set => Accessor.SetBool("cast_shadow", value);
    }
    public bool ReceiveShadow
    {
        get => Accessor.GetBool("receive_shadow", true);
        set => Accessor.SetBool("receive_shadow", value);
    }
    public int RenderingLayer
    {
        get => Accessor.GetInt("rendering_layer");
        set => Accessor.SetInt("rendering_layer", value);
    }
    public string MeshAsset
    {
        get => Accessor.GetString("mesh_asset");
        set => Accessor.SetString("mesh_asset", value);
    }
    public string MaterialAsset
    {
        get => Accessor.GetString("material_asset");
        set => Accessor.SetString("material_asset", value);
    }
    public bool MaterialOverride
    {
        get => Accessor.GetBool("material_override");
        set => Accessor.SetBool("material_override", value);
    }
    public Color MaterialBaseColor
    {
        get => Accessor.GetColor("material.base_color");
        set => Accessor.SetColor("material.base_color", value);
    }
    public float Metallic
    {
        get => Accessor.GetFloat("material.metallic");
        set => Accessor.SetFloat("material.metallic", value);
    }
    public float Roughness
    {
        get => Accessor.GetFloat("material.roughness", 0.55f);
        set => Accessor.SetFloat("material.roughness", value);
    }
    public Vector3 EmissiveColor
    {
        get => Accessor.GetVector3("material.emissive_color");
        set => Accessor.SetVector3("material.emissive_color", value);
    }
    public float EmissiveStrength
    {
        get => Accessor.GetFloat("material.emissive_strength");
        set => Accessor.SetFloat("material.emissive_strength", value);
    }
}

public readonly struct PrimitiveMeshRendererComponent : IComponentBinding<PrimitiveMeshRendererComponent>
{
    public static string NativeTypeName => "PrimitiveMeshRendererComponent";
    public static PrimitiveMeshRendererComponent FromHandle(ComponentHandle handle) => new(handle);

    private PrimitiveMeshRendererComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int PrimitiveType
    {
        get => Accessor.GetInt("primitive_type");
        set => Accessor.SetInt("primitive_type", value);
    }
    public bool Visible
    {
        get => Accessor.GetBool("visible", true);
        set => Accessor.SetBool("visible", value);
    }
    public Color Tint
    {
        get => Accessor.GetColor("tint");
        set => Accessor.SetColor("tint", value);
    }
    public bool CastShadow
    {
        get => Accessor.GetBool("cast_shadow", true);
        set => Accessor.SetBool("cast_shadow", value);
    }
    public bool ReceiveShadow
    {
        get => Accessor.GetBool("receive_shadow", true);
        set => Accessor.SetBool("receive_shadow", value);
    }
    public string MaterialAsset
    {
        get => Accessor.GetString("material_asset");
        set => Accessor.SetString("material_asset", value);
    }
    public bool MaterialOverride
    {
        get => Accessor.GetBool("material_override");
        set => Accessor.SetBool("material_override", value);
    }
    public Color MaterialBaseColor
    {
        get => Accessor.GetColor("material.base_color");
        set => Accessor.SetColor("material.base_color", value);
    }
    public float Metallic
    {
        get => Accessor.GetFloat("material.metallic");
        set => Accessor.SetFloat("material.metallic", value);
    }
    public float Roughness
    {
        get => Accessor.GetFloat("material.roughness", 0.55f);
        set => Accessor.SetFloat("material.roughness", value);
    }
    public Vector3 EmissiveColor
    {
        get => Accessor.GetVector3("material.emissive_color");
        set => Accessor.SetVector3("material.emissive_color", value);
    }
    public float EmissiveStrength
    {
        get => Accessor.GetFloat("material.emissive_strength");
        set => Accessor.SetFloat("material.emissive_strength", value);
    }
}

public readonly struct DirectionalLightComponent : IComponentBinding<DirectionalLightComponent>
{
    public static string NativeTypeName => "DirectionalLightComponent";
    public static DirectionalLightComponent FromHandle(ComponentHandle handle) => new(handle);

    private DirectionalLightComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public Color Color
    {
        get => Accessor.GetColor("color");
        set => Accessor.SetColor("color", value);
    }
    public float Intensity
    {
        get => Accessor.GetFloat("intensity", 1.0f);
        set => Accessor.SetFloat("intensity", value);
    }
    public bool CastShadows
    {
        get => Accessor.GetBool("cast_shadows", true);
        set => Accessor.SetBool("cast_shadows", value);
    }
    public float ShadowStrength
    {
        get => Accessor.GetFloat("shadow_strength", 1.0f);
        set => Accessor.SetFloat("shadow_strength", value);
    }
    // 単位はワールドメートル。
    public float ShadowDepthBias
    {
        get => Accessor.GetFloat("shadow_depth_bias", 0.02f);
        set => Accessor.SetFloat("shadow_depth_bias", value);
    }
    public float ShadowDistance
    {
        get => Accessor.GetFloat("shadow_distance", 120.0f);
        set => Accessor.SetFloat("shadow_distance", value);
    }
}

public readonly struct PointLightComponent : IComponentBinding<PointLightComponent>
{
    public static string NativeTypeName => "PointLightComponent";
    public static PointLightComponent FromHandle(ComponentHandle handle) => new(handle);

    private PointLightComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public Color Color
    {
        get => Accessor.GetColor("color");
        set => Accessor.SetColor("color", value);
    }
    public float Intensity
    {
        get => Accessor.GetFloat("intensity", 1.0f);
        set => Accessor.SetFloat("intensity", value);
    }
    public float Range
    {
        get => Accessor.GetFloat("range", 10.0f);
        set => Accessor.SetFloat("range", value);
    }
    public bool CastShadows
    {
        get => Accessor.GetBool("cast_shadows");
        set => Accessor.SetBool("cast_shadows", value);
    }
    public float ShadowStrength
    {
        get => Accessor.GetFloat("shadow_strength", 1.0f);
        set => Accessor.SetFloat("shadow_strength", value);
    }
}

public readonly struct SpotLightComponent : IComponentBinding<SpotLightComponent>
{
    public static string NativeTypeName => "SpotLightComponent";
    public static SpotLightComponent FromHandle(ComponentHandle handle) => new(handle);

    private SpotLightComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public Color Color
    {
        get => Accessor.GetColor("color");
        set => Accessor.SetColor("color", value);
    }
    public float Intensity
    {
        get => Accessor.GetFloat("intensity", 1.0f);
        set => Accessor.SetFloat("intensity", value);
    }
    public float Range
    {
        get => Accessor.GetFloat("range", 12.0f);
        set => Accessor.SetFloat("range", value);
    }
    public float InnerAngleDegrees
    {
        get => Accessor.GetFloat("inner_angle_degrees", 25.0f);
        set => Accessor.SetFloat("inner_angle_degrees", value);
    }
    public float OuterAngleDegrees
    {
        get => Accessor.GetFloat("outer_angle_degrees", 40.0f);
        set => Accessor.SetFloat("outer_angle_degrees", value);
    }
    public bool CastShadows
    {
        get => Accessor.GetBool("cast_shadows");
        set => Accessor.SetBool("cast_shadows", value);
    }
}

public readonly struct RigidbodyComponent : IComponentBinding<RigidbodyComponent>
{
    public static string NativeTypeName => "RigidbodyComponent";
    public static RigidbodyComponent FromHandle(ComponentHandle handle) => new(handle);

    private RigidbodyComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    // 0 = Static、1 = Kinematic、2 = Dynamic。
    public int BodyType
    {
        get => Accessor.GetInt("body_type", 2);
        set => Accessor.SetInt("body_type", value);
    }
    public float Mass
    {
        get => Accessor.GetFloat("mass", 1.0f);
        set => Accessor.SetFloat("mass", value);
    }
    public float LinearDamping
    {
        get => Accessor.GetFloat("linear_damping", 0.05f);
        set => Accessor.SetFloat("linear_damping", value);
    }
    public float AngularDamping
    {
        get => Accessor.GetFloat("angular_damping", 0.05f);
        set => Accessor.SetFloat("angular_damping", value);
    }
    public float GravityScale
    {
        get => Accessor.GetFloat("gravity_scale", 1.0f);
        set => Accessor.SetFloat("gravity_scale", value);
    }
    public float Restitution
    {
        get => Accessor.GetFloat("restitution");
        set => Accessor.SetFloat("restitution", value);
    }
    public float Friction
    {
        get => Accessor.GetFloat("friction", 0.5f);
        set => Accessor.SetFloat("friction", value);
    }
    public bool IsSleeping => Accessor.GetBool("is_sleeping");

    // 速度は Inspector 上は読み取り専用なので、専用の入口から積む。
    public Vector3 Velocity
    {
        get
        {
            var result = NativeBridge.RigidbodyGetLinearVelocity(Handle);
            return result.Succeeded ? result.Value : default;
        }
        set => NativeBridge.RigidbodySetLinearVelocity(Handle, value);
    }

    public Vector3 AngularVelocity
    {
        get
        {
            var result = NativeBridge.RigidbodyGetAngularVelocity(Handle);
            return result.Succeeded ? result.Value : default;
        }
        set => NativeBridge.RigidbodySetAngularVelocity(Handle, value);
    }

    public RuntimeStatus AddForce(Vector3 force) => NativeBridge.RigidbodyAddForce(Handle, force);
    public RuntimeStatus AddTorque(Vector3 torque) => NativeBridge.RigidbodyAddTorque(Handle, torque);
    public RuntimeStatus ClearForces() => NativeBridge.RigidbodyClearForces(Handle);

    // 質量ぶんの速度変化として与える。AddForce は 1 ステップ積分されるので別物。
    public RuntimeStatus AddImpulse(Vector3 impulse)
    {
        var mass = Mass;
        if (!(mass > 0.0f)) return RuntimeStatus.InvalidArgument;
        var velocity = Velocity;
        Velocity = new Vector3(
            velocity.X + impulse.X / mass,
            velocity.Y + impulse.Y / mass,
            velocity.Z + impulse.Z / mass);
        return RuntimeStatus.Ok;
    }

    public RuntimeStatus SetVelocity(Vector3 value)
        => NativeBridge.RigidbodySetLinearVelocity(Handle, value);
    public RuntimeStatus SetAngularVelocity(Vector3 value)
        => NativeBridge.RigidbodySetAngularVelocity(Handle, value);
    public RuntimeStatus Teleport(Vector3 position, Vector3 rotationEuler = default)
        => NativeBridge.RigidbodyTeleport(Handle, position, rotationEuler);
    public Vector3 FreezePosition
    {
        get => Accessor.GetVector3("freeze_position");
        set => Accessor.SetVector3("freeze_position", value);
    }
    public Vector3 FreezeRotation
    {
        get => Accessor.GetVector3("freeze_rotation");
        set => Accessor.SetVector3("freeze_rotation", value);
    }
    public bool StartAsleep
    {
        get => Accessor.GetBool("start_asleep");
        set => Accessor.SetBool("start_asleep", value);
    }
    public bool UseCcd
    {
        get => Accessor.GetBool("use_ccd");
        set => Accessor.SetBool("use_ccd", value);
    }
}

// Sphere / Box / Capsule / Mesh に共通する Collider の設定。
public readonly struct ColliderComponent
{
    public ColliderComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public Vector3 CenterOffset
    {
        get => Accessor.GetVector3("center_offset");
        set => Accessor.SetVector3("center_offset", value);
    }
    public int CollisionLayer
    {
        get => Accessor.GetInt("collision_layer");
        set => Accessor.SetInt("collision_layer", value);
    }
    public int CollisionMask
    {
        get => Accessor.GetInt("collision_mask", -1);
        set => Accessor.SetInt("collision_mask", value);
    }
    public bool IsTrigger
    {
        get => Accessor.GetBool("is_trigger");
        set => Accessor.SetBool("is_trigger", value);
    }
}

public readonly struct SphereColliderComponent : IComponentBinding<SphereColliderComponent>
{
    public static string NativeTypeName => "SphereColliderComponent";
    public static SphereColliderComponent FromHandle(ComponentHandle handle) => new(handle);

    private SphereColliderComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;
    public ColliderComponent Collider => new(Handle);

    public float Radius
    {
        get => Accessor.GetFloat("radius", 0.5f);
        set => Accessor.SetFloat("radius", value);
    }
    public float SkinWidth
    {
        get => Accessor.GetFloat("skin_width");
        set => Accessor.SetFloat("skin_width", value);
    }
    public float WalkableNormalY
    {
        get => Accessor.GetFloat("walkable_normal_y");
        set => Accessor.SetFloat("walkable_normal_y", value);
    }
}

public readonly struct BoxColliderComponent : IComponentBinding<BoxColliderComponent>
{
    public static string NativeTypeName => "BoxColliderComponent";
    public static BoxColliderComponent FromHandle(ComponentHandle handle) => new(handle);

    private BoxColliderComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;
    public ColliderComponent Collider => new(Handle);

    public Vector3 Size
    {
        get => Accessor.GetVector3("size");
        set => Accessor.SetVector3("size", value);
    }
}

public readonly struct CapsuleColliderComponent : IComponentBinding<CapsuleColliderComponent>
{
    public static string NativeTypeName => "CapsuleColliderComponent";
    public static CapsuleColliderComponent FromHandle(ComponentHandle handle) => new(handle);

    private CapsuleColliderComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;
    public ColliderComponent Collider => new(Handle);

    public float Radius
    {
        get => Accessor.GetFloat("radius", 0.5f);
        set => Accessor.SetFloat("radius", value);
    }
    public float Height
    {
        get => Accessor.GetFloat("height", 2.0f);
        set => Accessor.SetFloat("height", value);
    }
    public int Axis
    {
        get => Accessor.GetInt("axis");
        set => Accessor.SetInt("axis", value);
    }
}

public readonly struct AudioSourceComponent : IComponentBinding<AudioSourceComponent>
{
    public static string NativeTypeName => "AudioSourceComponent";
    public static AudioSourceComponent FromHandle(ComponentHandle handle) => new(handle);

    private AudioSourceComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public string ClipPath
    {
        get => Accessor.GetString("clip_path");
        set => Accessor.SetString("clip_path", value);
    }
    public bool Loop
    {
        get => Accessor.GetBool("loop");
        set => Accessor.SetBool("loop", value);
    }
    public float Volume
    {
        get => Accessor.GetFloat("volume", 1.0f);
        set => Accessor.SetFloat("volume", value);
    }
    public float Pitch
    {
        get => Accessor.GetFloat("pitch", 1.0f);
        set => Accessor.SetFloat("pitch", value);
    }
    public bool PlayOnStart
    {
        get => Accessor.GetBool("play_on_start");
        set => Accessor.SetBool("play_on_start", value);
    }
    public bool Spatial
    {
        get => Accessor.GetInt("spatial") == 1;
        set => Accessor.SetInt("spatial", value ? 1 : 0);
    }
    public float MinDistance
    {
        get => Accessor.GetFloat("min_distance", 1.0f);
        set => Accessor.SetFloat("min_distance", value);
    }
    public float MaxDistance
    {
        get => Accessor.GetFloat("max_distance", 30.0f);
        set => Accessor.SetFloat("max_distance", value);
    }
    public bool IsPlaying => Accessor.GetBool("is_playing");
    public RuntimeStatus Play() => NativeBridge.InvokeComponentCommand(Handle,
        ComponentCommand.AudioPlay);
    public RuntimeStatus Stop() => NativeBridge.InvokeComponentCommand(Handle,
        ComponentCommand.AudioStop);
}

public readonly struct ParticleEmitterComponent : IComponentBinding<ParticleEmitterComponent>
{
    public static string NativeTypeName => "ParticleEmitterComponent";
    public static ParticleEmitterComponent FromHandle(ComponentHandle handle) => new(handle);

    private ParticleEmitterComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Emitting
    {
        get => Accessor.GetBool("emitting", true);
        set => Accessor.SetBool("emitting", value);
    }
    public float SpawnRate
    {
        get => Accessor.GetFloat("spawn_rate");
        set => Accessor.SetFloat("spawn_rate", value);
    }
    public float Lifetime
    {
        get => Accessor.GetFloat("lifetime", 1.0f);
        set => Accessor.SetFloat("lifetime", value);
    }
    public float StartSpeed
    {
        get => Accessor.GetFloat("start_speed");
        set => Accessor.SetFloat("start_speed", value);
    }
    public Color StartColor
    {
        get => Accessor.GetColor("start_color");
        set => Accessor.SetColor("start_color", value);
    }
    public Color EndColor
    {
        get => Accessor.GetColor("end_color");
        set => Accessor.SetColor("end_color", value);
    }
    public int MaxParticles
    {
        get => Accessor.GetInt("max_particles");
        set => Accessor.SetInt("max_particles", value);
    }

    public RuntimeStatus Play() => NativeBridge.InvokeComponentCommand(Handle,
        ComponentCommand.ParticlePlay);
    public RuntimeStatus Stop() => NativeBridge.InvokeComponentCommand(Handle,
        ComponentCommand.ParticleStop);
    public RuntimeStatus Emit(int count) => NativeBridge.InvokeComponentCommand(Handle,
        ComponentCommand.ParticleEmit, integer: count);
    public RuntimeStatus Clear() => NativeBridge.InvokeComponentCommand(Handle,
        ComponentCommand.ParticleClear);
}

public readonly struct CharacterMotorComponent : IComponentBinding<CharacterMotorComponent>
{
    public static string NativeTypeName => "CharacterMotorComponent";
    public static CharacterMotorComponent FromHandle(ComponentHandle handle) => new(handle);

    private CharacterMotorComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public float MoveSpeed
    {
        get => Accessor.GetFloat("move_speed");
        set => Accessor.SetFloat("move_speed", value);
    }
    public float Acceleration
    {
        get => Accessor.GetFloat("acceleration");
        set => Accessor.SetFloat("acceleration", value);
    }
    public float Deceleration
    {
        get => Accessor.GetFloat("deceleration");
        set => Accessor.SetFloat("deceleration", value);
    }
    public float Gravity
    {
        get => Accessor.GetFloat("gravity");
        set => Accessor.SetFloat("gravity", value);
    }
    public float JumpPower
    {
        get => Accessor.GetFloat("jump_power");
        set => Accessor.SetFloat("jump_power", value);
    }
    public float AirControl
    {
        get => Accessor.GetFloat("air_control");
        set => Accessor.SetFloat("air_control", value);
    }

    // true を書くと次の更新で 1 回だけ跳ぶ。消費されると自動で false へ戻る。
    public bool RequestJump
    {
        get => Accessor.GetBool("jump_requested");
        set => Accessor.SetBool("jump_requested", value);
    }
    public float FallbackGroundY
    {
        get => Accessor.GetFloat("fallback_ground_y");
        set => Accessor.SetFloat("fallback_ground_y", value);
    }
    public float MaxStepHeight
    {
        get => Accessor.GetFloat("max_step_height");
        set => Accessor.SetFloat("max_step_height", value);
    }
    public float MaximumFallSpeed
    {
        get => Accessor.GetFloat("maximum_fall_speed");
        set => Accessor.SetFloat("maximum_fall_speed", value);
    }
    public int PrimaryColliderKey
    {
        get => Accessor.GetInt("primary_collider_key");
        set => Accessor.SetInt("primary_collider_key", value);
    }
    public bool VerticalPhysics
    {
        get => Accessor.GetBool("vertical_physics");
        set => Accessor.SetBool("vertical_physics", value);
    }
}

public readonly struct HealthComponent : IComponentBinding<HealthComponent>
{
    public static string NativeTypeName => "HealthComponent";
    public static HealthComponent FromHandle(ComponentHandle handle) => new(handle);

    private HealthComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public float MaxHealth
    {
        get => Accessor.GetFloat("max_health");
        set => Accessor.SetFloat("max_health", value);
    }
    public float CurrentHealth
    {
        get => Accessor.GetFloat("current_health");
        set => Accessor.SetFloat("current_health", value);
    }
    public bool Invulnerable
    {
        get => Accessor.GetBool("invulnerable");
        set => Accessor.SetBool("invulnerable", value);
    }
}

public readonly struct LandscapeComponent : IComponentBinding<LandscapeComponent>
{
    public static string NativeTypeName => "LandscapeComponent";
    public static LandscapeComponent FromHandle(ComponentHandle handle) => new(handle);

    private LandscapeComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public float DefaultCellSize
    {
        get => Accessor.GetFloat("default_cell_size");
        set => Accessor.SetFloat("default_cell_size", value);
    }
    public int DefaultResolution
    {
        get => Accessor.GetInt("default_resolution");
        set => Accessor.SetInt("default_resolution", value);
    }
}

public readonly struct RotatorComponent : IComponentBinding<RotatorComponent>
{
    public static string NativeTypeName => "RotatorComponent";
    public static RotatorComponent FromHandle(ComponentHandle handle) => new(handle);

    private RotatorComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public Vector3 Axis
    {
        get => Accessor.GetVector3("axis");
        set => Accessor.SetVector3("axis", value);
    }
    public float DegreesPerSecond
    {
        get => Accessor.GetFloat("degrees_per_second");
        set => Accessor.SetFloat("degrees_per_second", value);
    }
}

public readonly struct JumpPadComponent : IComponentBinding<JumpPadComponent>
{
    public static string NativeTypeName => "JumpPadComponent";
    public static JumpPadComponent FromHandle(ComponentHandle handle) => new(handle);

    private JumpPadComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public float Cooldown
    {
        get => Accessor.GetFloat("cooldown");
        set => Accessor.SetFloat("cooldown", value);
    }
    public bool DebugDraw
    {
        get => Accessor.GetBool("debug_draw");
        set => Accessor.SetBool("debug_draw", value);
    }
    public Vector3 Direction
    {
        get => Accessor.GetVector3("direction");
        set => Accessor.SetVector3("direction", value);
    }
    public float Force
    {
        get => Accessor.GetFloat("force");
        set => Accessor.SetFloat("force", value);
    }
    public bool OneShot
    {
        get => Accessor.GetBool("one_shot");
        set => Accessor.SetBool("one_shot", value);
    }
    public int TargetMask
    {
        get => Accessor.GetInt("target_mask");
        set => Accessor.SetInt("target_mask", value);
    }
}

public readonly struct NavAgentComponent : IComponentBinding<NavAgentComponent>
{
    public static string NativeTypeName => "NavAgentComponent";
    public static NavAgentComponent FromHandle(ComponentHandle handle) => new(handle);

    private NavAgentComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public float MoveSpeed
    {
        get => Accessor.GetFloat("move_speed");
        set => Accessor.SetFloat("move_speed", value);
    }
    public float PathGridSize
    {
        get => Accessor.GetFloat("path_grid_size");
        set => Accessor.SetFloat("path_grid_size", value);
    }
    public float PathMaxRange
    {
        get => Accessor.GetFloat("path_max_range");
        set => Accessor.SetFloat("path_max_range", value);
    }
    public int PathMaxSearchCells
    {
        get => Accessor.GetInt("path_max_search_cells");
        set => Accessor.SetInt("path_max_search_cells", value);
    }
    public float StoppingDistance
    {
        get => Accessor.GetFloat("stopping_distance");
        set => Accessor.SetFloat("stopping_distance", value);
    }
    public float TurnSpeedDegrees
    {
        get => Accessor.GetFloat("turn_speed_degrees");
        set => Accessor.SetFloat("turn_speed_degrees", value);
    }
}

public readonly struct PlayerControllerComponent : IComponentBinding<PlayerControllerComponent>
{
    public static string NativeTypeName => "PlayerControllerComponent";
    public static PlayerControllerComponent FromHandle(ComponentHandle handle) => new(handle);

    private PlayerControllerComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool CameraRelative
    {
        get => Accessor.GetBool("camera_relative");
        set => Accessor.SetBool("camera_relative", value);
    }
    public float DashMultiplier
    {
        get => Accessor.GetFloat("dash_multiplier");
        set => Accessor.SetFloat("dash_multiplier", value);
    }
    public bool RotateTowardsMovement
    {
        get => Accessor.GetBool("rotate_towards_movement");
        set => Accessor.SetBool("rotate_towards_movement", value);
    }
    public float TurnSpeedDegrees
    {
        get => Accessor.GetFloat("turn_speed_degrees");
        set => Accessor.SetFloat("turn_speed_degrees", value);
    }
}

public readonly struct PlayerInputComponent : IComponentBinding<PlayerInputComponent>
{
    public static string NativeTypeName => "PlayerInputComponent";
    public static PlayerInputComponent FromHandle(ComponentHandle handle) => new(handle);

    private PlayerInputComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool InputEnabled
    {
        get => Accessor.GetBool("input_enabled");
        set => Accessor.SetBool("input_enabled", value);
    }
    public int LocalPlayerSlot
    {
        get => Accessor.GetInt("local_player_slot");
        set => Accessor.SetInt("local_player_slot", value);
    }
}

public readonly struct LandscapeRendererComponent : IComponentBinding<LandscapeRendererComponent>
{
    public static string NativeTypeName => "LandscapeRendererComponent";
    public static LandscapeRendererComponent FromHandle(ComponentHandle handle) => new(handle);

    private LandscapeRendererComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool CastShadow
    {
        get => Accessor.GetBool("cast_shadow");
        set => Accessor.SetBool("cast_shadow", value);
    }
    public bool DoubleSided
    {
        get => Accessor.GetBool("double_sided");
        set => Accessor.SetBool("double_sided", value);
    }
    public bool ReceiveShadow
    {
        get => Accessor.GetBool("receive_shadow");
        set => Accessor.SetBool("receive_shadow", value);
    }
    public Color Tint
    {
        get => Accessor.GetColor("tint");
        set => Accessor.SetColor("tint", value);
    }
    public bool Visible
    {
        get => Accessor.GetBool("visible");
        set => Accessor.SetBool("visible", value);
    }
}

public readonly struct SpawnPointComponent : IComponentBinding<SpawnPointComponent>
{
    public static string NativeTypeName => "SpawnPointComponent";
    public static SpawnPointComponent FromHandle(ComponentHandle handle) => new(handle);

    private SpawnPointComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool DebugDraw
    {
        get => Accessor.GetBool("debug_draw");
        set => Accessor.SetBool("debug_draw", value);
    }
    public int Priority
    {
        get => Accessor.GetInt("priority");
        set => Accessor.SetInt("priority", value);
    }
    public int SpawnId
    {
        get => Accessor.GetInt("spawn_id");
        set => Accessor.SetInt("spawn_id", value);
    }
    public int Team
    {
        get => Accessor.GetInt("team");
        set => Accessor.SetInt("team", value);
    }
}

public readonly struct CheckpointComponent : IComponentBinding<CheckpointComponent>
{
    public static string NativeTypeName => "CheckpointComponent";
    public static CheckpointComponent FromHandle(ComponentHandle handle) => new(handle);

    private CheckpointComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int CheckpointId
    {
        get => Accessor.GetInt("checkpoint_id");
        set => Accessor.SetInt("checkpoint_id", value);
    }
    public bool OneShot
    {
        get => Accessor.GetBool("one_shot");
        set => Accessor.SetBool("one_shot", value);
    }
    public Vector3 RespawnPositionOffset
    {
        get => Accessor.GetVector3("respawn_position_offset");
        set => Accessor.SetVector3("respawn_position_offset", value);
    }
    public Vector3 RespawnRotation
    {
        get => Accessor.GetVector3("respawn_rotation");
        set => Accessor.SetVector3("respawn_rotation", value);
    }
    public int TargetMask
    {
        get => Accessor.GetInt("target_mask");
        set => Accessor.SetInt("target_mask", value);
    }
}

public readonly struct GoalComponent : IComponentBinding<GoalComponent>
{
    public static string NativeTypeName => "GoalComponent";
    public static GoalComponent FromHandle(ComponentHandle handle) => new(handle);

    private GoalComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public string CompletionEvent
    {
        get => Accessor.GetString("completion_event");
        set => Accessor.SetString("completion_event", value);
    }
    public int GoalId
    {
        get => Accessor.GetInt("goal_id");
        set => Accessor.SetInt("goal_id", value);
    }
    public bool OneShot
    {
        get => Accessor.GetBool("one_shot");
        set => Accessor.SetBool("one_shot", value);
    }
    public int TargetMask
    {
        get => Accessor.GetInt("target_mask");
        set => Accessor.SetInt("target_mask", value);
    }
}

public readonly struct KillVolumeComponent : IComponentBinding<KillVolumeComponent>
{
    public static string NativeTypeName => "KillVolumeComponent";
    public static KillVolumeComponent FromHandle(ComponentHandle handle) => new(handle);

    private KillVolumeComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int DamageAmount
    {
        get => Accessor.GetInt("damage_amount");
        set => Accessor.SetInt("damage_amount", value);
    }
    public bool RespawnAtCheckpoint
    {
        get => Accessor.GetBool("respawn_at_checkpoint");
        set => Accessor.SetBool("respawn_at_checkpoint", value);
    }
    public int TargetMask
    {
        get => Accessor.GetInt("target_mask");
        set => Accessor.SetInt("target_mask", value);
    }
}

public readonly struct DamageAreaComponent : IComponentBinding<DamageAreaComponent>
{
    public static string NativeTypeName => "DamageAreaComponent";
    public static DamageAreaComponent FromHandle(ComponentHandle handle) => new(handle);

    private DamageAreaComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int Damage
    {
        get => Accessor.GetInt("damage");
        set => Accessor.SetInt("damage", value);
    }
    public float Interval
    {
        get => Accessor.GetFloat("interval");
        set => Accessor.SetFloat("interval", value);
    }
    public bool OneShot
    {
        get => Accessor.GetBool("one_shot");
        set => Accessor.SetBool("one_shot", value);
    }
    public int TargetMask
    {
        get => Accessor.GetInt("target_mask");
        set => Accessor.SetInt("target_mask", value);
    }
}

public readonly struct FollowTargetComponent : IComponentBinding<FollowTargetComponent>
{
    public static string NativeTypeName => "FollowTargetComponent";
    public static FollowTargetComponent FromHandle(ComponentHandle handle) => new(handle);

    private FollowTargetComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public float FollowDistance
    {
        get => Accessor.GetFloat("follow_distance");
        set => Accessor.SetFloat("follow_distance", value);
    }
    public float FollowHeight
    {
        get => Accessor.GetFloat("follow_height");
        set => Accessor.SetFloat("follow_height", value);
    }
    public float FollowLag
    {
        get => Accessor.GetFloat("follow_lag");
        set => Accessor.SetFloat("follow_lag", value);
    }
    public float PitchOffset
    {
        get => Accessor.GetFloat("pitch_offset");
        set => Accessor.SetFloat("pitch_offset", value);
    }
    public bool RotationInputEnabled
    {
        get => Accessor.GetBool("rotation_input_enabled");
        set => Accessor.SetBool("rotation_input_enabled", value);
    }
    public float YawOffset
    {
        get => Accessor.GetFloat("yaw_offset");
        set => Accessor.SetFloat("yaw_offset", value);
    }
    public bool YieldToMotion
    {
        get => Accessor.GetBool("yield_to_motion");
        set => Accessor.SetBool("yield_to_motion", value);
    }
}

public readonly struct CameraTargetComponent : IComponentBinding<CameraTargetComponent>
{
    public static string NativeTypeName => "CameraTargetComponent";
    public static CameraTargetComponent FromHandle(ComponentHandle handle) => new(handle);

    private CameraTargetComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public Vector3 LookAtOffset
    {
        get => Accessor.GetVector3("look_at_offset");
        set => Accessor.SetVector3("look_at_offset", value);
    }
    public int Priority
    {
        get => Accessor.GetInt("priority");
        set => Accessor.SetInt("priority", value);
    }
    public Vector3 TargetOffset
    {
        get => Accessor.GetVector3("target_offset");
        set => Accessor.SetVector3("target_offset", value);
    }
}
