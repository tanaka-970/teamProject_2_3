using System;

namespace ReplayEngine;

// 描画 Component の型付き入口。値は Inspector と同じプロパティ名を読み書きする。

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
