using System;

namespace ReplayEngine;

// UI Component の型付き入口。値は Inspector と同じプロパティ名を読み書きする。

public readonly struct RectTransformComponent : IComponentBinding<RectTransformComponent>
{
    public static string NativeTypeName => "RectTransformComponent";
    public static RectTransformComponent FromHandle(ComponentHandle handle) => new(handle);

    private RectTransformComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public Vector2 AnchorMin
    {
        get => Accessor.GetVector2("anchor_min");
        set => Accessor.SetVector2("anchor_min", value);
    }
    public Vector2 AnchorMax
    {
        get => Accessor.GetVector2("anchor_max");
        set => Accessor.SetVector2("anchor_max", value);
    }
    public Vector2 AnchoredPosition
    {
        get => Accessor.GetVector2("anchored_position");
        set => Accessor.SetVector2("anchored_position", value);
    }
    public Vector2 SizeDelta
    {
        get => Accessor.GetVector2("size_delta");
        set => Accessor.SetVector2("size_delta", value);
    }
    public Vector2 Pivot
    {
        get => Accessor.GetVector2("pivot");
        set => Accessor.SetVector2("pivot", value);
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
    public int SortOrder
    {
        get => Accessor.GetInt("sort_order");
        set => Accessor.SetInt("sort_order", value);
    }
    // 解決済みの画面矩形。読み取り専用。
    public Vector4 ResolvedRect => Accessor.GetVector4("resolved_rect");
}

public readonly struct CanvasComponent : IComponentBinding<CanvasComponent>
{
    public static string NativeTypeName => "CanvasComponent";
    public static CanvasComponent FromHandle(ComponentHandle handle) => new(handle);

    private CanvasComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public Vector2 ReferenceResolution
    {
        get => Accessor.GetVector2("reference_resolution");
        set => Accessor.SetVector2("reference_resolution", value);
    }
    public int RenderMode
    {
        get => Accessor.GetInt("render_mode");
        set => Accessor.SetInt("render_mode", value);
    }
    public int ScaleMode
    {
        get => Accessor.GetInt("scale_mode");
        set => Accessor.SetInt("scale_mode", value);
    }
    public float MatchWidthOrHeight
    {
        get => Accessor.GetFloat("match_width_or_height");
        set => Accessor.SetFloat("match_width_or_height", value);
    }
    public int SortOrder
    {
        get => Accessor.GetInt("sort_order");
        set => Accessor.SetInt("sort_order", value);
    }
    public float Opacity
    {
        get => Accessor.GetFloat("opacity", 1.0f);
        set => Accessor.SetFloat("opacity", value);
    }
}

public readonly struct UIImageComponent : IComponentBinding<UIImageComponent>
{
    public static string NativeTypeName => "UIImageComponent";
    public static UIImageComponent FromHandle(ComponentHandle handle) => new(handle);

    private UIImageComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    // Image Asset の GUID。
    public string Sprite
    {
        get => Accessor.GetString("sprite");
        set => Accessor.SetString("sprite", value);
    }
    public string Atlas
    {
        get => Accessor.GetString("atlas");
        set => Accessor.SetString("atlas", value);
    }
    public string AtlasRegion
    {
        get => Accessor.GetString("atlas_region");
        set => Accessor.SetString("atlas_region", value);
    }
    public Color Color
    {
        get => Accessor.GetColor("color");
        set => Accessor.SetColor("color", value);
    }
    public float Opacity
    {
        get => Accessor.GetFloat("opacity", 1.0f);
        set => Accessor.SetFloat("opacity", value);
    }
    // 0..1。ゲージ表示に使う。
    public float FillAmount
    {
        get => Accessor.GetFloat("fill_amount", 1.0f);
        set => Accessor.SetFloat("fill_amount", value);
    }
    public int FillMethod
    {
        get => Accessor.GetInt("fill_method");
        set => Accessor.SetInt("fill_method", value);
    }
    public bool PreserveAspect
    {
        get => Accessor.GetBool("preserve_aspect");
        set => Accessor.SetBool("preserve_aspect", value);
    }
}

public readonly struct UITextComponent : IComponentBinding<UITextComponent>
{
    public static string NativeTypeName => "UITextComponent";
    public static UITextComponent FromHandle(ComponentHandle handle) => new(handle);

    private UITextComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public string Text
    {
        get => Accessor.GetString("text");
        set => Accessor.SetString("text", value);
    }
    // Font Asset の GUID。
    public string Font
    {
        get => Accessor.GetString("font");
        set => Accessor.SetString("font", value);
    }
    public float FontSize
    {
        get => Accessor.GetFloat("font_size", 24.0f);
        set => Accessor.SetFloat("font_size", value);
    }
    public Color Color
    {
        get => Accessor.GetColor("color");
        set => Accessor.SetColor("color", value);
    }
    public float Opacity
    {
        get => Accessor.GetFloat("opacity", 1.0f);
        set => Accessor.SetFloat("opacity", value);
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
    public bool WordWrap
    {
        get => Accessor.GetBool("word_wrap");
        set => Accessor.SetBool("word_wrap", value);
    }
    public string LocalizationKey
    {
        get => Accessor.GetString("localization_key");
        set => Accessor.SetString("localization_key", value);
    }
}

public readonly struct UIButtonComponent : IComponentBinding<UIButtonComponent>
{
    public static string NativeTypeName => "UIButtonComponent";
    public static UIButtonComponent FromHandle(ComponentHandle handle) => new(handle);

    private UIButtonComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Interactable
    {
        get => Accessor.GetBool("interactable", true);
        set => Accessor.SetBool("interactable", value);
    }
    public Color NormalColor
    {
        get => Accessor.GetColor("normal_color");
        set => Accessor.SetColor("normal_color", value);
    }
    public Color HoverColor
    {
        get => Accessor.GetColor("hover_color");
        set => Accessor.SetColor("hover_color", value);
    }
    public Color PressedColor
    {
        get => Accessor.GetColor("pressed_color");
        set => Accessor.SetColor("pressed_color", value);
    }
    public Color DisabledColor
    {
        get => Accessor.GetColor("disabled_color");
        set => Accessor.SetColor("disabled_color", value);
    }
    // 0=Normal 1=Hover 2=Pressed 3=Disabled。読み取り専用。
    public int State => Accessor.GetInt("state");
}

public readonly struct UIScrollViewComponent : IComponentBinding<UIScrollViewComponent>
{
    public static string NativeTypeName => "UIScrollViewComponent";
    public static UIScrollViewComponent FromHandle(ComponentHandle handle) => new(handle);

    private UIScrollViewComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Horizontal
    {
        get => Accessor.GetBool("horizontal");
        set => Accessor.SetBool("horizontal", value);
    }
    public bool Vertical
    {
        get => Accessor.GetBool("vertical", true);
        set => Accessor.SetBool("vertical", value);
    }
    public Vector2 ScrollOffset
    {
        get => Accessor.GetVector2("scroll_offset");
        set => Accessor.SetVector2("scroll_offset", value);
    }
    public float ScrollSensitivity
    {
        get => Accessor.GetFloat("scroll_sensitivity", 1.0f);
        set => Accessor.SetFloat("scroll_sensitivity", value);
    }
    public bool ShowScrollbars
    {
        get => Accessor.GetBool("show_scrollbars", true);
        set => Accessor.SetBool("show_scrollbars", value);
    }
}

public readonly struct UIInputFieldComponent : IComponentBinding<UIInputFieldComponent>
{
    public static string NativeTypeName => "UIInputFieldComponent";
    public static UIInputFieldComponent FromHandle(ComponentHandle handle) => new(handle);

    private UIInputFieldComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public string Text
    {
        get => Accessor.GetString("text");
        set => Accessor.SetString("text", value);
    }
    public string Placeholder
    {
        get => Accessor.GetString("placeholder");
        set => Accessor.SetString("placeholder", value);
    }
    public Color TextColor
    {
        get => Accessor.GetColor("text_color");
        set => Accessor.SetColor("text_color", value);
    }
    public int MaxCharacters
    {
        get => Accessor.GetInt("max_characters");
        set => Accessor.SetInt("max_characters", value);
    }
    public bool Password
    {
        get => Accessor.GetBool("password");
        set => Accessor.SetBool("password", value);
    }
    public bool ReadOnly
    {
        get => Accessor.GetBool("read_only");
        set => Accessor.SetBool("read_only", value);
    }
}

public readonly struct UISelectableComponent : IComponentBinding<UISelectableComponent>
{
    public static string NativeTypeName => "UISelectableComponent";
    public static UISelectableComponent FromHandle(ComponentHandle handle) => new(handle);

    private UISelectableComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Interactable
    {
        get => Accessor.GetBool("interactable", true);
        set => Accessor.SetBool("interactable", value);
    }
    public bool NavigationEnabled
    {
        get => Accessor.GetBool("navigation_enabled", true);
        set => Accessor.SetBool("navigation_enabled", value);
    }
    public int NavigationOrder
    {
        get => Accessor.GetInt("navigation_order");
        set => Accessor.SetInt("navigation_order", value);
    }
    public bool Focused => Accessor.GetBool("focused");
}

public readonly struct UISpriteAnimatorComponent : IComponentBinding<UISpriteAnimatorComponent>
{
    public static string NativeTypeName => "UISpriteAnimatorComponent";
    public static UISpriteAnimatorComponent FromHandle(ComponentHandle handle) => new(handle);

    private UISpriteAnimatorComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public bool Playing
    {
        get => Accessor.GetBool("playing", true);
        set => Accessor.SetBool("playing", value);
    }
    public int Frame
    {
        get => Accessor.GetInt("frame");
        set => Accessor.SetInt("frame", value);
    }
    public float FramesPerSecond
    {
        get => Accessor.GetFloat("frames_per_second", 12.0f);
        set => Accessor.SetFloat("frames_per_second", value);
    }
    public int StartFrame
    {
        get => Accessor.GetInt("start_frame");
        set => Accessor.SetInt("start_frame", value);
    }
    public int EndFrame
    {
        get => Accessor.GetInt("end_frame");
        set => Accessor.SetInt("end_frame", value);
    }

    public RuntimeStatus Play() { Playing = true; return RuntimeStatus.Ok; }
    public RuntimeStatus Stop() { Playing = false; return RuntimeStatus.Ok; }
}

// Scene の非同期読み込み進捗を持つ Component。
public readonly struct SceneLoaderComponent : IComponentBinding<SceneLoaderComponent>
{
    public static string NativeTypeName => "SceneLoaderComponent";
    public static SceneLoaderComponent FromHandle(ComponentHandle handle) => new(handle);

    private SceneLoaderComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    // 0..1。
    public float Progress => Accessor.GetFloat("progress");
    public bool IsLoading => Accessor.GetBool("is_loading");
    public int State => Accessor.GetInt("state");
}

// Scene を跨いで残す指定。付いている GameObject は Scene 遷移で破棄されない。
public readonly struct PersistentComponent : IComponentBinding<PersistentComponent>
{
    public static string NativeTypeName => "PersistentComponent";
    public static PersistentComponent FromHandle(ComponentHandle handle) => new(handle);

    private PersistentComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;
}
