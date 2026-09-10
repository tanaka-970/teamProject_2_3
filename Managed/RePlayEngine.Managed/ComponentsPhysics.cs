using System;

namespace ReplayEngine;

// 物理 Component の型付き入口。値は Inspector と同じプロパティ名を読み書きする。

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
