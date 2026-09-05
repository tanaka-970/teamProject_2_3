using System;

namespace ReplayEngine;

// Camera と Audio の型付き入口。他は ComponentsRendering / Physics / Gameplay / UI にある。

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
