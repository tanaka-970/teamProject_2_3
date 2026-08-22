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

    public RuntimeStatus Play() { Playing = true; return RuntimeStatus.Ok; }
    public RuntimeStatus Stop() { Playing = false; return RuntimeStatus.Ok; }
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
    public string MaterialAsset
    {
        get => Accessor.GetString("material_asset");
        set => Accessor.SetString("material_asset", value);
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

    public RuntimeStatus Play() { Emitting = true; return RuntimeStatus.Ok; }
    public RuntimeStatus Stop() { Emitting = false; return RuntimeStatus.Ok; }
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
