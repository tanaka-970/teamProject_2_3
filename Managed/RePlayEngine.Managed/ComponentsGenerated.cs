using System;

namespace ReplayEngine;

// PropertyRegistry から起こした Component の型付き入口。手で書き換えない。
//
// 作り直しかた:
//   x64\Release\3dgp.exe --dump-component-properties
//   python Tools\generate_component_bindings.py
//
// 手で書いてある型（Components.cs / ComponentsRendering.cs /
// ComponentsPhysics.cs / ComponentsGameplay.cs / ComponentsUI.cs）は出さない。

// Transform（Core）
public readonly struct TransformComponent : IComponentBinding<TransformComponent>
{
    public static string NativeTypeName => "TransformComponent";
    public static TransformComponent FromHandle(ComponentHandle handle) => new(handle);

    private TransformComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public Vector3 Position
    {
        get => Accessor.GetVector3("position");
        set => Accessor.SetVector3("position", value);
    }

    public Vector3 Rotation
    {
        get => Accessor.GetVector3("rotation");
        set => Accessor.SetVector3("rotation", value);
    }

    public Vector3 Scale
    {
        get => Accessor.GetVector3("scale");
        set => Accessor.SetVector3("scale", value);
    }
}

// Pivot（Core）
public readonly struct PivotComponent : IComponentBinding<PivotComponent>
{
    public static string NativeTypeName => "PivotComponent";
    public static PivotComponent FromHandle(ComponentHandle handle) => new(handle);

    private PivotComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int Mode
    {
        get => Accessor.GetInt("mode");
        set => Accessor.SetInt("mode", value);
    }

    public Vector3 LocalPoint
    {
        get => Accessor.GetVector3("local_point");
        set => Accessor.SetVector3("local_point", value);
    }

    public ObjectReference Target
    {
        get => Accessor.GetObjectReference("target");
        set => Accessor.SetObjectReference("target", value);
    }
}

// Post Process Volume（Rendering）
public readonly struct PostProcessVolumeComponent : IComponentBinding<PostProcessVolumeComponent>
{
    public static string NativeTypeName => "PostProcessVolumeComponent";
    public static PostProcessVolumeComponent FromHandle(ComponentHandle handle) => new(handle);

    private PostProcessVolumeComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int Priority
    {
        get => Accessor.GetInt("priority");
        set => Accessor.SetInt("priority", value);
    }

    public bool BloomEnabled
    {
        get => Accessor.GetBool("bloom_enabled");
        set => Accessor.SetBool("bloom_enabled", value);
    }

    public float BloomThreshold
    {
        get => Accessor.GetFloat("bloom_threshold");
        set => Accessor.SetFloat("bloom_threshold", value);
    }

    public float BloomIntensity
    {
        get => Accessor.GetFloat("bloom_intensity");
        set => Accessor.SetFloat("bloom_intensity", value);
    }

    public bool LuminanceEnabled
    {
        get => Accessor.GetBool("luminance_enabled");
        set => Accessor.SetBool("luminance_enabled", value);
    }

    public bool FinalPassEnabled
    {
        get => Accessor.GetBool("final_pass_enabled");
        set => Accessor.SetBool("final_pass_enabled", value);
    }

    public bool VignetteEnabled
    {
        get => Accessor.GetBool("vignette_enabled");
        set => Accessor.SetBool("vignette_enabled", value);
    }

    public float VignetteIntensity
    {
        get => Accessor.GetFloat("vignette_intensity");
        set => Accessor.SetFloat("vignette_intensity", value);
    }

    public bool SsaoEnabled
    {
        get => Accessor.GetBool("ssao_enabled");
        set => Accessor.SetBool("ssao_enabled", value);
    }

    public float SsaoRadius
    {
        get => Accessor.GetFloat("ssao_radius");
        set => Accessor.SetFloat("ssao_radius", value);
    }

    public float SsaoIntensity
    {
        get => Accessor.GetFloat("ssao_intensity");
        set => Accessor.SetFloat("ssao_intensity", value);
    }

    public bool SsrEnabled
    {
        get => Accessor.GetBool("ssr_enabled");
        set => Accessor.SetBool("ssr_enabled", value);
    }

    public float SsrIntensity
    {
        get => Accessor.GetFloat("ssr_intensity");
        set => Accessor.SetFloat("ssr_intensity", value);
    }

    public bool TaaEnabled
    {
        get => Accessor.GetBool("taa_enabled");
        set => Accessor.SetBool("taa_enabled", value);
    }

    public float Exposure
    {
        get => Accessor.GetFloat("exposure");
        set => Accessor.SetFloat("exposure", value);
    }

    public Color ColorFilter
    {
        get => Accessor.GetColor("color_filter");
        set => Accessor.SetColor("color_filter", value);
    }
}

// 空（Rendering）
public readonly struct SkyboxComponent : IComponentBinding<SkyboxComponent>
{
    public static string NativeTypeName => "SkyboxComponent";
    public static SkyboxComponent FromHandle(ComponentHandle handle) => new(handle);

    private SkyboxComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public string Cubemap
    {
        get => Accessor.GetString("cubemap");
        set => Accessor.SetString("cubemap", value);
    }

    public int Priority
    {
        get => Accessor.GetInt("priority");
        set => Accessor.SetInt("priority", value);
    }

    public bool SkyEnabled
    {
        get => Accessor.GetBool("sky_enabled");
        set => Accessor.SetBool("sky_enabled", value);
    }

    public float RotationDegrees
    {
        get => Accessor.GetFloat("rotation_degrees");
        set => Accessor.SetFloat("rotation_degrees", value);
    }

    public float Intensity
    {
        get => Accessor.GetFloat("intensity");
        set => Accessor.SetFloat("intensity", value);
    }

    public float ToonEnvironment
    {
        get => Accessor.GetFloat("toon_environment");
        set => Accessor.SetFloat("toon_environment", value);
    }

    public float Time
    {
        get => Accessor.GetFloat("time");
        set => Accessor.SetFloat("time", value);
    }

    public float TimeSpeed
    {
        get => Accessor.GetFloat("time_speed");
        set => Accessor.SetFloat("time_speed", value);
    }

    public bool CloudsEnabled
    {
        get => Accessor.GetBool("clouds_enabled");
        set => Accessor.SetBool("clouds_enabled", value);
    }

    public Vector2 CloudLayer1Speed
    {
        get => Accessor.GetVector2("cloud_layer1_speed");
        set => Accessor.SetVector2("cloud_layer1_speed", value);
    }

    public float CloudLayer1Scale
    {
        get => Accessor.GetFloat("cloud_layer1_scale");
        set => Accessor.SetFloat("cloud_layer1_scale", value);
    }

    public float CloudLayer1Density
    {
        get => Accessor.GetFloat("cloud_layer1_density");
        set => Accessor.SetFloat("cloud_layer1_density", value);
    }

    public Color CloudLayer1Color
    {
        get => Accessor.GetColor("cloud_layer1_color");
        set => Accessor.SetColor("cloud_layer1_color", value);
    }

    public Vector2 CloudLayer2Speed
    {
        get => Accessor.GetVector2("cloud_layer2_speed");
        set => Accessor.SetVector2("cloud_layer2_speed", value);
    }

    public float CloudLayer2Scale
    {
        get => Accessor.GetFloat("cloud_layer2_scale");
        set => Accessor.SetFloat("cloud_layer2_scale", value);
    }

    public float CloudLayer2Density
    {
        get => Accessor.GetFloat("cloud_layer2_density");
        set => Accessor.SetFloat("cloud_layer2_density", value);
    }

    public Color CloudLayer2Color
    {
        get => Accessor.GetColor("cloud_layer2_color");
        set => Accessor.SetColor("cloud_layer2_color", value);
    }

    public bool StarsEnabled
    {
        get => Accessor.GetBool("stars_enabled");
        set => Accessor.SetBool("stars_enabled", value);
    }

    public float StarDensity
    {
        get => Accessor.GetFloat("star_density");
        set => Accessor.SetFloat("star_density", value);
    }

    public float StarIntensity
    {
        get => Accessor.GetFloat("star_intensity");
        set => Accessor.SetFloat("star_intensity", value);
    }

    public Color StarColor
    {
        get => Accessor.GetColor("star_color");
        set => Accessor.SetColor("star_color", value);
    }

    public bool MoonEnabled
    {
        get => Accessor.GetBool("moon_enabled");
        set => Accessor.SetBool("moon_enabled", value);
    }

    public Vector3 MoonDirection
    {
        get => Accessor.GetVector3("moon_direction");
        set => Accessor.SetVector3("moon_direction", value);
    }

    public float MoonSize
    {
        get => Accessor.GetFloat("moon_size");
        set => Accessor.SetFloat("moon_size", value);
    }

    public float MoonIntensity
    {
        get => Accessor.GetFloat("moon_intensity");
        set => Accessor.SetFloat("moon_intensity", value);
    }

    public Color MoonColor
    {
        get => Accessor.GetColor("moon_color");
        set => Accessor.SetColor("moon_color", value);
    }
}

// Screen Effect Stack（Rendering）
public readonly struct ScreenEffectStackComponent : IComponentBinding<ScreenEffectStackComponent>
{
    public static string NativeTypeName => "ScreenEffectStackComponent";
    public static ScreenEffectStackComponent FromHandle(ComponentHandle handle) => new(handle);

    private ScreenEffectStackComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Enabled
    {
        get => Accessor.GetBool("enabled");
        set => Accessor.SetBool("enabled", value);
    }

    public bool UsePreset
    {
        get => Accessor.GetBool("use_preset");
        set => Accessor.SetBool("use_preset", value);
    }

    public string EffectPreset
    {
        get => Accessor.GetString("effect_preset");
        set => Accessor.SetString("effect_preset", value);
    }

    public int EffectCount
    {
        get => Accessor.GetInt("effect_count");
        set => Accessor.SetInt("effect_count", value);
    }

    public int ApplyStage
    {
        get => Accessor.GetInt("apply_stage");
        set => Accessor.SetInt("apply_stage", value);
    }

    public int TargetMode
    {
        get => Accessor.GetInt("target_mode");
        set => Accessor.SetInt("target_mode", value);
    }

    public int TargetRenderingLayer
    {
        get => Accessor.GetInt("target_rendering_layer");
        set => Accessor.SetInt("target_rendering_layer", value);
    }
}

// Model Effect Stack（Rendering）
public readonly struct ModelEffectStackComponent : IComponentBinding<ModelEffectStackComponent>
{
    public static string NativeTypeName => "ModelEffectStackComponent";
    public static ModelEffectStackComponent FromHandle(ComponentHandle handle) => new(handle);

    private ModelEffectStackComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Enabled
    {
        get => Accessor.GetBool("enabled");
        set => Accessor.SetBool("enabled", value);
    }

    public bool UsePreset
    {
        get => Accessor.GetBool("use_preset");
        set => Accessor.SetBool("use_preset", value);
    }

    public string EffectPreset
    {
        get => Accessor.GetString("effect_preset");
        set => Accessor.SetString("effect_preset", value);
    }

    public int EffectCount
    {
        get => Accessor.GetInt("effect_count");
        set => Accessor.SetInt("effect_count", value);
    }

    public int TargetSlotMode
    {
        get => Accessor.GetInt("target_slot_mode");
        set => Accessor.SetInt("target_slot_mode", value);
    }

    public int DepthMode
    {
        get => Accessor.GetInt("depth_mode");
        set => Accessor.SetInt("depth_mode", value);
    }

    public int ExtractMode
    {
        get => Accessor.GetInt("extract_mode");
        set => Accessor.SetInt("extract_mode", value);
    }

    public float MaxBleedPixels
    {
        get => Accessor.GetFloat("max_bleed_pixels");
        set => Accessor.SetFloat("max_bleed_pixels", value);
    }
}

// Normal Adjust（Rendering）
public readonly struct NormalAdjustComponent : IComponentBinding<NormalAdjustComponent>
{
    public static string NativeTypeName => "NormalAdjustComponent";
    public static NormalAdjustComponent FromHandle(ComponentHandle handle) => new(handle);

    private NormalAdjustComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public float Blend
    {
        get => Accessor.GetFloat("blend");
        set => Accessor.SetFloat("blend", value);
    }

    public Vector3 Center
    {
        get => Accessor.GetVector3("center");
        set => Accessor.SetVector3("center", value);
    }

    public float Radius
    {
        get => Accessor.GetFloat("radius");
        set => Accessor.SetFloat("radius", value);
    }

    public float Falloff
    {
        get => Accessor.GetFloat("falloff");
        set => Accessor.SetFloat("falloff", value);
    }

    public string Bone
    {
        get => Accessor.GetString("bone");
        set => Accessor.SetString("bone", value);
    }

    public int TargetSlotMode
    {
        get => Accessor.GetInt("target_slot_mode");
        set => Accessor.SetInt("target_slot_mode", value);
    }

    public int TargetSlotIndex
    {
        get => Accessor.GetInt("target_slot_index");
        set => Accessor.SetInt("target_slot_index", value);
    }
}

// 3D ライン（Rendering）
public readonly struct LineRendererComponent : IComponentBinding<LineRendererComponent>
{
    public static string NativeTypeName => "LineRendererComponent";
    public static LineRendererComponent FromHandle(ComponentHandle handle) => new(handle);

    private LineRendererComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int PointCount
    {
        get => Accessor.GetInt("point_count");
        set => Accessor.SetInt("point_count", value);
    }

    public int Smoothing
    {
        get => Accessor.GetInt("smoothing");
        set => Accessor.SetInt("smoothing", value);
    }

    public bool Closed
    {
        get => Accessor.GetBool("closed");
        set => Accessor.SetBool("closed", value);
    }

    public float WidthStart
    {
        get => Accessor.GetFloat("width_start");
        set => Accessor.SetFloat("width_start", value);
    }

    public float WidthEnd
    {
        get => Accessor.GetFloat("width_end");
        set => Accessor.SetFloat("width_end", value);
    }

    public bool Billboard
    {
        get => Accessor.GetBool("billboard");
        set => Accessor.SetBool("billboard", value);
    }

    public int UvMode
    {
        get => Accessor.GetInt("uv_mode");
        set => Accessor.SetInt("uv_mode", value);
    }

    public float UvTiling
    {
        get => Accessor.GetFloat("uv_tiling");
        set => Accessor.SetFloat("uv_tiling", value);
    }

    public float UvScroll
    {
        get => Accessor.GetFloat("uv_scroll");
        set => Accessor.SetFloat("uv_scroll", value);
    }

    public string Texture
    {
        get => Accessor.GetString("texture");
        set => Accessor.SetString("texture", value);
    }

    public Color FillColor
    {
        get => Accessor.GetColor("fill_color");
        set => Accessor.SetColor("fill_color", value);
    }

    public Color FillColor2
    {
        get => Accessor.GetColor("fill_color_2");
        set => Accessor.SetColor("fill_color_2", value);
    }

    public int FillMode
    {
        get => Accessor.GetInt("fill_mode");
        set => Accessor.SetInt("fill_mode", value);
    }

    public float TrimStart
    {
        get => Accessor.GetFloat("trim_start");
        set => Accessor.SetFloat("trim_start", value);
    }

    public float TrimEnd
    {
        get => Accessor.GetFloat("trim_end");
        set => Accessor.SetFloat("trim_end", value);
    }

    public float TrimOffset
    {
        get => Accessor.GetFloat("trim_offset");
        set => Accessor.SetFloat("trim_offset", value);
    }
}

// 軌跡（Rendering）
public readonly struct TrailComponent : IComponentBinding<TrailComponent>
{
    public static string NativeTypeName => "TrailComponent";
    public static TrailComponent FromHandle(ComponentHandle handle) => new(handle);

    private TrailComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Emitting
    {
        get => Accessor.GetBool("emitting");
        set => Accessor.SetBool("emitting", value);
    }

    public float Lifetime
    {
        get => Accessor.GetFloat("lifetime");
        set => Accessor.SetFloat("lifetime", value);
    }

    public float MinDistance
    {
        get => Accessor.GetFloat("min_distance");
        set => Accessor.SetFloat("min_distance", value);
    }

    public int MaxPoints
    {
        get => Accessor.GetInt("max_points");
        set => Accessor.SetInt("max_points", value);
    }

    public bool WorldSpace
    {
        get => Accessor.GetBool("world_space");
        set => Accessor.SetBool("world_space", value);
    }

    public float WidthStart
    {
        get => Accessor.GetFloat("width_start");
        set => Accessor.SetFloat("width_start", value);
    }

    public float WidthEnd
    {
        get => Accessor.GetFloat("width_end");
        set => Accessor.SetFloat("width_end", value);
    }

    public bool Billboard
    {
        get => Accessor.GetBool("billboard");
        set => Accessor.SetBool("billboard", value);
    }

    public int UvMode
    {
        get => Accessor.GetInt("uv_mode");
        set => Accessor.SetInt("uv_mode", value);
    }

    public float UvTiling
    {
        get => Accessor.GetFloat("uv_tiling");
        set => Accessor.SetFloat("uv_tiling", value);
    }

    public float UvScroll
    {
        get => Accessor.GetFloat("uv_scroll");
        set => Accessor.SetFloat("uv_scroll", value);
    }

    public string Texture
    {
        get => Accessor.GetString("texture");
        set => Accessor.SetString("texture", value);
    }

    public Color FillColor
    {
        get => Accessor.GetColor("fill_color");
        set => Accessor.SetColor("fill_color", value);
    }

    public Color FillColor2
    {
        get => Accessor.GetColor("fill_color_2");
        set => Accessor.SetColor("fill_color_2", value);
    }

    public int FillMode
    {
        get => Accessor.GetInt("fill_mode");
        set => Accessor.SetInt("fill_mode", value);
    }

    public float TrimStart
    {
        get => Accessor.GetFloat("trim_start");
        set => Accessor.SetFloat("trim_start", value);
    }

    public float TrimEnd
    {
        get => Accessor.GetFloat("trim_end");
        set => Accessor.SetFloat("trim_end", value);
    }

    public float TrimOffset
    {
        get => Accessor.GetFloat("trim_offset");
        set => Accessor.SetFloat("trim_offset", value);
    }
}

// Shape Image（UI）
public readonly struct UIShapeImageComponent : IComponentBinding<UIShapeImageComponent>
{
    public static string NativeTypeName => "UIShapeImageComponent";
    public static UIShapeImageComponent FromHandle(ComponentHandle handle) => new(handle);

    private UIShapeImageComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool PathClosed
    {
        get => Accessor.GetBool("path_closed");
        set => Accessor.SetBool("path_closed", value);
    }
}

// Text Animator（UI）
public readonly struct UITextAnimatorComponent : IComponentBinding<UITextAnimatorComponent>
{
    public static string NativeTypeName => "UITextAnimatorComponent";
    public static UITextAnimatorComponent FromHandle(ComponentHandle handle) => new(handle);

    private UITextAnimatorComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public float RangeStart
    {
        get => Accessor.GetFloat("range_start");
        set => Accessor.SetFloat("range_start", value);
    }

    public float RangeEnd
    {
        get => Accessor.GetFloat("range_end");
        set => Accessor.SetFloat("range_end", value);
    }

    public float RangeOffset
    {
        get => Accessor.GetFloat("range_offset");
        set => Accessor.SetFloat("range_offset", value);
    }

    public int RangeShape
    {
        get => Accessor.GetInt("range_shape");
        set => Accessor.SetInt("range_shape", value);
    }

    public float RangeSmoothness
    {
        get => Accessor.GetFloat("range_smoothness");
        set => Accessor.SetFloat("range_smoothness", value);
    }

    public Vector2 PositionOffset
    {
        get => Accessor.GetVector2("position_offset");
        set => Accessor.SetVector2("position_offset", value);
    }

    public float Rotation
    {
        get => Accessor.GetFloat("rotation");
        set => Accessor.SetFloat("rotation", value);
    }

    public Vector2 Scale
    {
        get => Accessor.GetVector2("scale");
        set => Accessor.SetVector2("scale", value);
    }

    public float Opacity
    {
        get => Accessor.GetFloat("opacity");
        set => Accessor.SetFloat("opacity", value);
    }

    public Color Color
    {
        get => Accessor.GetColor("color");
        set => Accessor.SetColor("color", value);
    }

    public float CharacterSpacing
    {
        get => Accessor.GetFloat("character_spacing");
        set => Accessor.SetFloat("character_spacing", value);
    }

    public int RandomSeed
    {
        get => Accessor.GetInt("random_seed");
        set => Accessor.SetInt("random_seed", value);
    }

    public Vector2 RandomPosition
    {
        get => Accessor.GetVector2("random_position");
        set => Accessor.SetVector2("random_position", value);
    }

    public float RandomRotation
    {
        get => Accessor.GetFloat("random_rotation");
        set => Accessor.SetFloat("random_rotation", value);
    }

    public int Anchor
    {
        get => Accessor.GetInt("anchor");
        set => Accessor.SetInt("anchor", value);
    }
}

// Shape（UI）
public readonly struct UIShapeComponent : IComponentBinding<UIShapeComponent>
{
    public static string NativeTypeName => "UIShapeComponent";
    public static UIShapeComponent FromHandle(ComponentHandle handle) => new(handle);

    private UIShapeComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int Shape
    {
        get => Accessor.GetInt("shape");
        set => Accessor.SetInt("shape", value);
    }

    public Color FillColor
    {
        get => Accessor.GetColor("fill_color");
        set => Accessor.SetColor("fill_color", value);
    }

    public Color FillColor2
    {
        get => Accessor.GetColor("fill_color_2");
        set => Accessor.SetColor("fill_color_2", value);
    }

    public Color FillColor3
    {
        get => Accessor.GetColor("fill_color_3");
        set => Accessor.SetColor("fill_color_3", value);
    }

    public Color FillColor4
    {
        get => Accessor.GetColor("fill_color_4");
        set => Accessor.SetColor("fill_color_4", value);
    }

    public float FillStop2
    {
        get => Accessor.GetFloat("fill_stop_2");
        set => Accessor.SetFloat("fill_stop_2", value);
    }

    public float FillStop3
    {
        get => Accessor.GetFloat("fill_stop_3");
        set => Accessor.SetFloat("fill_stop_3", value);
    }

    public float FillStop4
    {
        get => Accessor.GetFloat("fill_stop_4");
        set => Accessor.SetFloat("fill_stop_4", value);
    }

    public int FillMode
    {
        get => Accessor.GetInt("fill_mode");
        set => Accessor.SetInt("fill_mode", value);
    }

    public float FillAngle
    {
        get => Accessor.GetFloat("fill_angle");
        set => Accessor.SetFloat("fill_angle", value);
    }

    public Vector2 FillCenter
    {
        get => Accessor.GetVector2("fill_center");
        set => Accessor.SetVector2("fill_center", value);
    }

    public Color StrokeColor
    {
        get => Accessor.GetColor("stroke_color");
        set => Accessor.SetColor("stroke_color", value);
    }

    public Color StrokeColor2
    {
        get => Accessor.GetColor("stroke_color_2");
        set => Accessor.SetColor("stroke_color_2", value);
    }

    public int StrokeMode
    {
        get => Accessor.GetInt("stroke_mode");
        set => Accessor.SetInt("stroke_mode", value);
    }

    public float StrokeWidth
    {
        get => Accessor.GetFloat("stroke_width");
        set => Accessor.SetFloat("stroke_width", value);
    }

    public float CornerRadius
    {
        get => Accessor.GetFloat("corner_radius");
        set => Accessor.SetFloat("corner_radius", value);
    }

    public float ArcCurvature
    {
        get => Accessor.GetFloat("arc_curvature");
        set => Accessor.SetFloat("arc_curvature", value);
    }

    public int Sides
    {
        get => Accessor.GetInt("sides");
        set => Accessor.SetInt("sides", value);
    }

    public float SuperellipseExponent
    {
        get => Accessor.GetFloat("superellipse_exponent");
        set => Accessor.SetFloat("superellipse_exponent", value);
    }

    public float PolarBaseRadius
    {
        get => Accessor.GetFloat("polar_base_radius");
        set => Accessor.SetFloat("polar_base_radius", value);
    }

    public float PolarAmplitude
    {
        get => Accessor.GetFloat("polar_amplitude");
        set => Accessor.SetFloat("polar_amplitude", value);
    }

    public float PolarLobes
    {
        get => Accessor.GetFloat("polar_lobes");
        set => Accessor.SetFloat("polar_lobes", value);
    }

    public float PolarRotation
    {
        get => Accessor.GetFloat("polar_rotation");
        set => Accessor.SetFloat("polar_rotation", value);
    }

    public float TrimStart
    {
        get => Accessor.GetFloat("trim_start");
        set => Accessor.SetFloat("trim_start", value);
    }

    public float TrimEnd
    {
        get => Accessor.GetFloat("trim_end");
        set => Accessor.SetFloat("trim_end", value);
    }

    public float TrimOffset
    {
        get => Accessor.GetFloat("trim_offset");
        set => Accessor.SetFloat("trim_offset", value);
    }

    public float DashLength
    {
        get => Accessor.GetFloat("dash_length");
        set => Accessor.SetFloat("dash_length", value);
    }

    public float DashGap
    {
        get => Accessor.GetFloat("dash_gap");
        set => Accessor.SetFloat("dash_gap", value);
    }

    public float DashOffset
    {
        get => Accessor.GetFloat("dash_offset");
        set => Accessor.SetFloat("dash_offset", value);
    }

    public bool PathClosed
    {
        get => Accessor.GetBool("path_closed");
        set => Accessor.SetBool("path_closed", value);
    }
}

// Puppet Deform（UI）
public readonly struct UIPuppetDeformComponent : IComponentBinding<UIPuppetDeformComponent>
{
    public static string NativeTypeName => "UIPuppetDeformComponent";
    public static UIPuppetDeformComponent FromHandle(ComponentHandle handle) => new(handle);

    private UIPuppetDeformComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool EnabledDeform
    {
        get => Accessor.GetBool("enabled_deform");
        set => Accessor.SetBool("enabled_deform", value);
    }

    public int GridColumns
    {
        get => Accessor.GetInt("grid_columns");
        set => Accessor.SetInt("grid_columns", value);
    }

    public int GridRows
    {
        get => Accessor.GetInt("grid_rows");
        set => Accessor.SetInt("grid_rows", value);
    }

    public float GlobalStrength
    {
        get => Accessor.GetFloat("global_strength");
        set => Accessor.SetFloat("global_strength", value);
    }
}

// Mask（UI）
public readonly struct UIMaskComponent : IComponentBinding<UIMaskComponent>
{
    public static string NativeTypeName => "UIMaskComponent";
    public static UIMaskComponent FromHandle(ComponentHandle handle) => new(handle);

    private UIMaskComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool EnabledMask
    {
        get => Accessor.GetBool("enabled_mask");
        set => Accessor.SetBool("enabled_mask", value);
    }

    public bool ShowMaskGraphic
    {
        get => Accessor.GetBool("show_mask_graphic");
        set => Accessor.SetBool("show_mask_graphic", value);
    }

    public int MaskMode
    {
        get => Accessor.GetInt("mask_mode");
        set => Accessor.SetInt("mask_mode", value);
    }

    public int ShapeKind
    {
        get => Accessor.GetInt("shape_kind");
        set => Accessor.SetInt("shape_kind", value);
    }

    public int ShapeSides
    {
        get => Accessor.GetInt("shape_sides");
        set => Accessor.SetInt("shape_sides", value);
    }

    public float ShapeInnerRadius
    {
        get => Accessor.GetFloat("shape_inner_radius");
        set => Accessor.SetFloat("shape_inner_radius", value);
    }

    public float ShapeCornerRadius
    {
        get => Accessor.GetFloat("shape_corner_radius");
        set => Accessor.SetFloat("shape_corner_radius", value);
    }

    public float ShapeRotation
    {
        get => Accessor.GetFloat("shape_rotation");
        set => Accessor.SetFloat("shape_rotation", value);
    }

    public Vector2 GroupScale
    {
        get => Accessor.GetVector2("group_scale");
        set => Accessor.SetVector2("group_scale", value);
    }

    public string MaskImage
    {
        get => Accessor.GetString("mask_image");
        set => Accessor.SetString("mask_image", value);
    }

    public ObjectReference MaskObject
    {
        get => Accessor.GetObjectReference("mask_object");
        set => Accessor.SetObjectReference("mask_object", value);
    }

    public bool Invert
    {
        get => Accessor.GetBool("invert");
        set => Accessor.SetBool("invert", value);
    }

    public float Softness
    {
        get => Accessor.GetFloat("softness");
        set => Accessor.SetFloat("softness", value);
    }
}

// Language Switch（UI）
public readonly struct UILanguageSwitchComponent : IComponentBinding<UILanguageSwitchComponent>
{
    public static string NativeTypeName => "UILanguageSwitchComponent";
    public static UILanguageSwitchComponent FromHandle(ComponentHandle handle) => new(handle);

    private UILanguageSwitchComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public string Language
    {
        get => Accessor.GetString("language");
        set => Accessor.SetString("language", value);
    }
}

// Button Property Toggle（UI）
public readonly struct UIButtonPropertyToggleComponent : IComponentBinding<UIButtonPropertyToggleComponent>
{
    public static string NativeTypeName => "UIButtonPropertyToggleComponent";
    public static UIButtonPropertyToggleComponent FromHandle(ComponentHandle handle) => new(handle);

    private UIButtonPropertyToggleComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public ComponentReference Target
    {
        get => Accessor.GetComponentReference("target");
        set => Accessor.SetComponentReference("target", value);
    }

    public string TargetProperty
    {
        get => Accessor.GetString("target_property");
        set => Accessor.SetString("target_property", value);
    }
}

// Effect Stack（UI）
public readonly struct UIEffectStackComponent : IComponentBinding<UIEffectStackComponent>
{
    public static string NativeTypeName => "UIEffectStackComponent";
    public static UIEffectStackComponent FromHandle(ComponentHandle handle) => new(handle);

    private UIEffectStackComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Enabled
    {
        get => Accessor.GetBool("enabled");
        set => Accessor.SetBool("enabled", value);
    }

    public int TargetScope
    {
        get => Accessor.GetInt("target_scope");
        set => Accessor.SetInt("target_scope", value);
    }

    public bool CaptureBackdrop
    {
        get => Accessor.GetBool("capture_backdrop");
        set => Accessor.SetBool("capture_backdrop", value);
    }

    public bool UsePreset
    {
        get => Accessor.GetBool("use_preset");
        set => Accessor.SetBool("use_preset", value);
    }

    public string EffectPreset
    {
        get => Accessor.GetString("effect_preset");
        set => Accessor.SetString("effect_preset", value);
    }

    public int EffectCount
    {
        get => Accessor.GetInt("effect_count");
        set => Accessor.SetInt("effect_count", value);
    }
}

// Motion Player（Motion）
public readonly struct MotionPlayerComponent : IComponentBinding<MotionPlayerComponent>
{
    public static string NativeTypeName => "MotionPlayerComponent";
    public static MotionPlayerComponent FromHandle(ComponentHandle handle) => new(handle);

    private MotionPlayerComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public string Motion
    {
        get => Accessor.GetString("motion");
        set => Accessor.SetString("motion", value);
    }

    public string Key
    {
        get => Accessor.GetString("key");
        set => Accessor.SetString("key", value);
    }

    public bool PlayOnStart
    {
        get => Accessor.GetBool("play_on_start");
        set => Accessor.SetBool("play_on_start", value);
    }

    public int Trigger
    {
        get => Accessor.GetInt("trigger");
        set => Accessor.SetInt("trigger", value);
    }

    public float TriggerDelay
    {
        get => Accessor.GetFloat("trigger_delay");
        set => Accessor.SetFloat("trigger_delay", value);
    }

    public ComponentReference TriggerSource
    {
        get => Accessor.GetComponentReference("trigger_source");
        set => Accessor.SetComponentReference("trigger_source", value);
    }

    public string TriggerState
    {
        get => Accessor.GetString("trigger_state");
        set => Accessor.SetString("trigger_state", value);
    }

    public bool Loop
    {
        get => Accessor.GetBool("loop");
        set => Accessor.SetBool("loop", value);
    }

    public int WrapMode
    {
        get => Accessor.GetInt("wrap_mode");
        set => Accessor.SetInt("wrap_mode", value);
    }

    public bool AutoStopOnEnd
    {
        get => Accessor.GetBool("auto_stop_on_end");
        set => Accessor.SetBool("auto_stop_on_end", value);
    }

    public bool IgnoreTimeScale
    {
        get => Accessor.GetBool("ignore_time_scale");
        set => Accessor.SetBool("ignore_time_scale", value);
    }

    public float BlendInSeconds
    {
        get => Accessor.GetFloat("blend_in_seconds");
        set => Accessor.SetFloat("blend_in_seconds", value);
    }

    public float Speed
    {
        get => Accessor.GetFloat("speed");
        set => Accessor.SetFloat("speed", value);
    }

    public int RandomSeed
    {
        get => Accessor.GetInt("random_seed");
        set => Accessor.SetInt("random_seed", value);
    }

    public float TimeOffsetRandom
    {
        get => Accessor.GetFloat("time_offset_random");
        set => Accessor.SetFloat("time_offset_random", value);
    }

    public float SpeedRandom
    {
        get => Accessor.GetFloat("speed_random");
        set => Accessor.SetFloat("speed_random", value);
    }

    public float Weight
    {
        get => Accessor.GetFloat("weight");
        set => Accessor.SetFloat("weight", value);
    }

    public int State
    {
        get => Accessor.GetInt("state");
    }

    public float Time
    {
        get => Accessor.GetFloat("time");
        set => Accessor.SetFloat("time", value);
    }
}

// Composition Player（Motion）
public readonly struct CompositionPlayerComponent : IComponentBinding<CompositionPlayerComponent>
{
    public static string NativeTypeName => "CompositionPlayerComponent";
    public static CompositionPlayerComponent FromHandle(ComponentHandle handle) => new(handle);

    private CompositionPlayerComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public string Composition
    {
        get => Accessor.GetString("composition");
        set => Accessor.SetString("composition", value);
    }

    public string Key
    {
        get => Accessor.GetString("key");
        set => Accessor.SetString("key", value);
    }

    public bool PlayOnStart
    {
        get => Accessor.GetBool("play_on_start");
        set => Accessor.SetBool("play_on_start", value);
    }

    public bool PlayOnStateChange
    {
        get => Accessor.GetBool("play_on_state_change");
        set => Accessor.SetBool("play_on_state_change", value);
    }

    public ComponentReference StateSource
    {
        get => Accessor.GetComponentReference("state_source");
        set => Accessor.SetComponentReference("state_source", value);
    }

    public string StateName
    {
        get => Accessor.GetString("state_name");
        set => Accessor.SetString("state_name", value);
    }

    public bool Loop
    {
        get => Accessor.GetBool("loop");
        set => Accessor.SetBool("loop", value);
    }

    public bool IgnoreTimeScale
    {
        get => Accessor.GetBool("ignore_time_scale");
        set => Accessor.SetBool("ignore_time_scale", value);
    }

    public bool HoldOnEnd
    {
        get => Accessor.GetBool("hold_on_end");
        set => Accessor.SetBool("hold_on_end", value);
    }

    public float Speed
    {
        get => Accessor.GetFloat("speed");
        set => Accessor.SetFloat("speed", value);
    }

    public float Weight
    {
        get => Accessor.GetFloat("weight");
        set => Accessor.SetFloat("weight", value);
    }

    public int State
    {
        get => Accessor.GetInt("state");
    }

    public float Time
    {
        get => Accessor.GetFloat("time");
        set => Accessor.SetFloat("time", value);
    }
}

// State（Core）
public readonly struct StateComponent : IComponentBinding<StateComponent>
{
    public static string NativeTypeName => "StateComponent";
    public static StateComponent FromHandle(ComponentHandle handle) => new(handle);

    private StateComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int StateCount
    {
        get => Accessor.GetInt("state_count");
        set => Accessor.SetInt("state_count", value);
    }

    public string CurrentState
    {
        get => Accessor.GetString("current_state");
        set => Accessor.SetString("current_state", value);
    }
}

// Property Link（Motion）
public readonly struct PropertyLinkComponent : IComponentBinding<PropertyLinkComponent>
{
    public static string NativeTypeName => "PropertyLinkComponent";
    public static PropertyLinkComponent FromHandle(ComponentHandle handle) => new(handle);

    private PropertyLinkComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public ComponentReference SourceObject
    {
        get => Accessor.GetComponentReference("source_object");
        set => Accessor.SetComponentReference("source_object", value);
    }

    public string SourceProperty
    {
        get => Accessor.GetString("source_property");
        set => Accessor.SetString("source_property", value);
    }

    public ComponentReference TargetObject
    {
        get => Accessor.GetComponentReference("target_object");
        set => Accessor.SetComponentReference("target_object", value);
    }

    public string TargetProperty
    {
        get => Accessor.GetString("target_property");
        set => Accessor.SetString("target_property", value);
    }

    public float SourceMin
    {
        get => Accessor.GetFloat("source_min");
        set => Accessor.SetFloat("source_min", value);
    }

    public float SourceMax
    {
        get => Accessor.GetFloat("source_max");
        set => Accessor.SetFloat("source_max", value);
    }

    public float TargetMin
    {
        get => Accessor.GetFloat("target_min");
        set => Accessor.SetFloat("target_min", value);
    }

    public float TargetMax
    {
        get => Accessor.GetFloat("target_max");
        set => Accessor.SetFloat("target_max", value);
    }

    public bool Invert
    {
        get => Accessor.GetBool("invert");
        set => Accessor.SetBool("invert", value);
    }

    public bool Clamp
    {
        get => Accessor.GetBool("clamp");
        set => Accessor.SetBool("clamp", value);
    }

    public int Easing
    {
        get => Accessor.GetInt("easing");
        set => Accessor.SetInt("easing", value);
    }

    public float Smoothing
    {
        get => Accessor.GetFloat("smoothing");
        set => Accessor.SetFloat("smoothing", value);
    }
}

// Mesh Collider（Physics）
public readonly struct MeshColliderComponent : IComponentBinding<MeshColliderComponent>
{
    public static string NativeTypeName => "MeshColliderComponent";
    public static MeshColliderComponent FromHandle(ComponentHandle handle) => new(handle);

    private MeshColliderComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int ColliderKey
    {
        get => Accessor.GetInt("collider_key");
    }

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
        get => Accessor.GetInt("collision_mask");
        set => Accessor.SetInt("collision_mask", value);
    }

    public bool IsTrigger
    {
        get => Accessor.GetBool("is_trigger");
        set => Accessor.SetBool("is_trigger", value);
    }

    public bool DebugDraw
    {
        get => Accessor.GetBool("debug_draw");
        set => Accessor.SetBool("debug_draw", value);
    }

    public int MeshSource
    {
        get => Accessor.GetInt("mesh_source");
        set => Accessor.SetInt("mesh_source", value);
    }

    public string MeshAsset
    {
        get => Accessor.GetString("mesh_asset");
        set => Accessor.SetString("mesh_asset", value);
    }

    public float CookCellSize
    {
        get => Accessor.GetFloat("cook_cell_size");
        set => Accessor.SetFloat("cook_cell_size", value);
    }

    public bool DoubleSided
    {
        get => Accessor.GetBool("double_sided");
        set => Accessor.SetBool("double_sided", value);
    }

    public bool DebugDrawWireframe
    {
        get => Accessor.GetBool("debug_draw_wireframe");
        set => Accessor.SetBool("debug_draw_wireframe", value);
    }
}

// Landscape Collider（Landscape）
public readonly struct LandscapeColliderComponent : IComponentBinding<LandscapeColliderComponent>
{
    public static string NativeTypeName => "LandscapeColliderComponent";
    public static LandscapeColliderComponent FromHandle(ComponentHandle handle) => new(handle);

    private LandscapeColliderComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int ColliderKey
    {
        get => Accessor.GetInt("collider_key");
    }

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
        get => Accessor.GetInt("collision_mask");
        set => Accessor.SetInt("collision_mask", value);
    }

    public bool IsTrigger
    {
        get => Accessor.GetBool("is_trigger");
        set => Accessor.SetBool("is_trigger", value);
    }

    public bool DebugDraw
    {
        get => Accessor.GetBool("debug_draw");
        set => Accessor.SetBool("debug_draw", value);
    }

    public bool DoubleSided
    {
        get => Accessor.GetBool("double_sided");
        set => Accessor.SetBool("double_sided", value);
    }

    public float CollisionCellSize
    {
        get => Accessor.GetFloat("collision_cell_size");
        set => Accessor.SetFloat("collision_cell_size", value);
    }

    public bool DebugDrawWireframe
    {
        get => Accessor.GetBool("debug_draw_wireframe");
        set => Accessor.SetBool("debug_draw_wireframe", value);
    }
}

// Audio Listener（Audio）
public readonly struct AudioListenerComponent : IComponentBinding<AudioListenerComponent>
{
    public static string NativeTypeName => "AudioListenerComponent";
    public static AudioListenerComponent FromHandle(ComponentHandle handle) => new(handle);

    private AudioListenerComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int Priority
    {
        get => Accessor.GetInt("priority");
        set => Accessor.SetInt("priority", value);
    }
}

// Scene Note（Editor）
public readonly struct EditorNoteComponent : IComponentBinding<EditorNoteComponent>
{
    public static string NativeTypeName => "EditorNoteComponent";
    public static EditorNoteComponent FromHandle(ComponentHandle handle) => new(handle);

    private EditorNoteComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public string Text
    {
        get => Accessor.GetString("text");
        set => Accessor.SetString("text", value);
    }

    public int Category
    {
        get => Accessor.GetInt("category");
        set => Accessor.SetInt("category", value);
    }

    public int Priority
    {
        get => Accessor.GetInt("priority");
        set => Accessor.SetInt("priority", value);
    }

    public bool Completed
    {
        get => Accessor.GetBool("completed");
        set => Accessor.SetBool("completed", value);
    }

    public bool ShowInViewport
    {
        get => Accessor.GetBool("show_in_viewport");
        set => Accessor.SetBool("show_in_viewport", value);
    }

    public bool HideWhenCompleted
    {
        get => Accessor.GetBool("hide_when_completed");
        set => Accessor.SetBool("hide_when_completed", value);
    }

    public Color Color
    {
        get => Accessor.GetColor("color");
        set => Accessor.SetColor("color", value);
    }

    public float TextScale
    {
        get => Accessor.GetFloat("text_scale");
        set => Accessor.SetFloat("text_scale", value);
    }

    public Vector3 Offset
    {
        get => Accessor.GetVector3("offset");
        set => Accessor.SetVector3("offset", value);
    }

    public int Mode
    {
        get => Accessor.GetInt("mode");
        set => Accessor.SetInt("mode", value);
    }

    public int HorizontalAlign
    {
        get => Accessor.GetInt("horizontal_align");
        set => Accessor.SetInt("horizontal_align", value);
    }

    public int VerticalAlign
    {
        get => Accessor.GetInt("vertical_align");
        set => Accessor.SetInt("vertical_align", value);
    }
}

// Script（Scripting）
public readonly struct ScriptComponent : IComponentBinding<ScriptComponent>
{
    public static string NativeTypeName => "ScriptComponent";
    public static ScriptComponent FromHandle(ComponentHandle handle) => new(handle);

    private ScriptComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public int ScriptLanguage
    {
        get => Accessor.GetInt("__script.language");
        set => Accessor.SetInt("__script.language", value);
    }

    public string ScriptAsset
    {
        get => Accessor.GetString("__script.asset");
        set => Accessor.SetString("__script.asset", value);
    }

    public string ScriptClass
    {
        get => Accessor.GetString("__script.class");
        set => Accessor.SetString("__script.class", value);
    }

    public int ScriptExecutionOrder
    {
        get => Accessor.GetInt("__script.execution_order");
        set => Accessor.SetInt("__script.execution_order", value);
    }

    public string ScriptTypeId
    {
        get => Accessor.GetString("__script.type_id");
        set => Accessor.SetString("__script.type_id", value);
    }
}

// Enemy Behaviour（Gameplay）
public readonly struct EnemyBehaviourComponent : IComponentBinding<EnemyBehaviourComponent>
{
    public static string NativeTypeName => "EnemyBehaviourComponent";
    public static EnemyBehaviourComponent FromHandle(ComponentHandle handle) => new(handle);

    private EnemyBehaviourComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public ObjectReference Target
    {
        get => Accessor.GetObjectReference("target");
        set => Accessor.SetObjectReference("target", value);
    }

    public float DetectionRange
    {
        get => Accessor.GetFloat("detection_range");
        set => Accessor.SetFloat("detection_range", value);
    }

    public float FieldOfViewDegrees
    {
        get => Accessor.GetFloat("field_of_view_degrees");
        set => Accessor.SetFloat("field_of_view_degrees", value);
    }

    public float AttackRange
    {
        get => Accessor.GetFloat("attack_range");
        set => Accessor.SetFloat("attack_range", value);
    }

    public float LoseSightDelay
    {
        get => Accessor.GetFloat("lose_sight_delay");
        set => Accessor.SetFloat("lose_sight_delay", value);
    }

    public float EyeHeight
    {
        get => Accessor.GetFloat("eye_height");
        set => Accessor.SetFloat("eye_height", value);
    }

    public float TargetHeight
    {
        get => Accessor.GetFloat("target_height");
        set => Accessor.SetFloat("target_height", value);
    }

    public int VisibilityLayer
    {
        get => Accessor.GetInt("visibility_layer");
        set => Accessor.SetInt("visibility_layer", value);
    }

    public int VisibilityMask
    {
        get => Accessor.GetInt("visibility_mask");
        set => Accessor.SetInt("visibility_mask", value);
    }

    public bool AttackControlsDamageArea
    {
        get => Accessor.GetBool("attack_controls_damage_area");
        set => Accessor.SetBool("attack_controls_damage_area", value);
    }

    public float AttackWindupSeconds
    {
        get => Accessor.GetFloat("attack_windup_seconds");
        set => Accessor.SetFloat("attack_windup_seconds", value);
    }

    public float AttackActiveSeconds
    {
        get => Accessor.GetFloat("attack_active_seconds");
        set => Accessor.SetFloat("attack_active_seconds", value);
    }

    public float AttackRecoverySeconds
    {
        get => Accessor.GetFloat("attack_recovery_seconds");
        set => Accessor.SetFloat("attack_recovery_seconds", value);
    }

    public bool DebugDraw
    {
        get => Accessor.GetBool("debug_draw");
        set => Accessor.SetBool("debug_draw", value);
    }

    public string CurrentState
    {
        get => Accessor.GetString("current_state");
    }

    public string AttackPhase
    {
        get => Accessor.GetString("attack_phase");
    }
}
