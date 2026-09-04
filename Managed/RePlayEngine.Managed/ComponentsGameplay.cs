using System;

namespace ReplayEngine;

// Gameplay Component の型付き入口。値は Inspector と同じプロパティ名を読み書きする。

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
