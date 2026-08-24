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
    public AssetReference<ImageAsset> SpriteReference
    {
        get => new(Sprite);
        set => Sprite = value.AssetGuid;
    }
    public string Atlas
    {
        get => Accessor.GetString("atlas");
        set => Accessor.SetString("atlas", value);
    }
    public AssetReference<SpriteAtlasAsset> AtlasReference
    {
        get => new(Atlas);
        set => Atlas = value.AssetGuid;
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
    public int StrokeMode
    {
        get => Accessor.GetInt("stroke_mode");
        set => Accessor.SetInt("stroke_mode", value);
    }
    public Color StrokeColor2
    {
        get => Accessor.GetColor("stroke_color_2");
        set => Accessor.SetColor("stroke_color_2", value);
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
    public bool FillReverse
    {
        get => Accessor.GetBool("fill_reverse");
        set => Accessor.SetBool("fill_reverse", value);
    }
    public int BlendMode
    {
        get => Accessor.GetInt("blend_mode");
        set => Accessor.SetInt("blend_mode", value);
    }
    public Vector2 UVOffset
    {
        get => Accessor.GetVector2("uv_offset");
        set => Accessor.SetVector2("uv_offset", value);
    }
    public Vector2 UVScale
    {
        get => Accessor.GetVector2("uv_scale");
        set => Accessor.SetVector2("uv_scale", value);
    }
    public Vector4 NineSlice
    {
        get => Accessor.GetVector4("nine_slice");
        set => Accessor.SetVector4("nine_slice", value);
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
    public float CharacterSpacing
    {
        get => Accessor.GetFloat("character_spacing");
        set => Accessor.SetFloat("character_spacing", value);
    }
    public float LineSpacing
    {
        get => Accessor.GetFloat("line_spacing", 1.0f);
        set => Accessor.SetFloat("line_spacing", value);
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
    public bool RichText
    {
        get => Accessor.GetBool("rich_text");
        set => Accessor.SetBool("rich_text", value);
    }
    public string LocalizationKey
    {
        get => Accessor.GetString("localization_key");
        set => Accessor.SetString("localization_key", value);
    }
    public ComponentReference NumberSource
    {
        get => Accessor.GetComponentReference("number_source");
        set => Accessor.SetComponentReference("number_source", value);
    }
    public string NumberSourceProperty
    {
        get => Accessor.GetString("number_source_property");
        set => Accessor.SetString("number_source_property", value);
    }
    public string NumberFormat
    {
        get => Accessor.GetString("number_format");
        set => Accessor.SetString("number_format", value);
    }
    public int NumberDigits
    {
        get => Accessor.GetInt("number_digits");
        set => Accessor.SetInt("number_digits", value);
    }
    public float OutlineWidth
    {
        get => Accessor.GetFloat("outline_width");
        set => Accessor.SetFloat("outline_width", value);
    }
    public Color OutlineColor
    {
        get => Accessor.GetColor("outline_color");
        set => Accessor.SetColor("outline_color", value);
    }
    public Vector2 ShadowOffset
    {
        get => Accessor.GetVector2("shadow_offset");
        set => Accessor.SetVector2("shadow_offset", value);
    }
    public Color ShadowColor
    {
        get => Accessor.GetColor("shadow_color");
        set => Accessor.SetColor("shadow_color", value);
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
    public ComponentReference TargetImage
    {
        get => Accessor.GetComponentReference("target_image");
        set => Accessor.SetComponentReference("target_image", value);
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
    public string NormalMotion
    {
        get => Accessor.GetString("normal_motion");
        set => Accessor.SetString("normal_motion", value);
    }
    public string HoverMotion
    {
        get => Accessor.GetString("hover_motion");
        set => Accessor.SetString("hover_motion", value);
    }
    public string PressedMotion
    {
        get => Accessor.GetString("pressed_motion");
        set => Accessor.SetString("pressed_motion", value);
    }
    public string DisabledMotion
    {
        get => Accessor.GetString("disabled_motion");
        set => Accessor.SetString("disabled_motion", value);
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
    public float StateBlendSeconds
    {
        get => Accessor.GetFloat("state_blend_seconds");
        set => Accessor.SetFloat("state_blend_seconds", value);
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
    public ComponentReference Content
    {
        get => Accessor.GetComponentReference("content");
        set => Accessor.SetComponentReference("content", value);
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
    public bool ClampWhenContentFits
    {
        get => Accessor.GetBool("clamp_when_content_fits", true);
        set => Accessor.SetBool("clamp_when_content_fits", value);
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
    public float ScrollbarWidth
    {
        get => Accessor.GetFloat("scrollbar_width", 8.0f);
        set => Accessor.SetFloat("scrollbar_width", value);
    }
    public Color ScrollbarTrackColor
    {
        get => Accessor.GetColor("scrollbar_track_color");
        set => Accessor.SetColor("scrollbar_track_color", value);
    }
    public Color ScrollbarThumbColor
    {
        get => Accessor.GetColor("scrollbar_thumb_color");
        set => Accessor.SetColor("scrollbar_thumb_color", value);
    }
    public float ScrollbarCornerRadius
    {
        get => Accessor.GetFloat("scrollbar_corner_radius", 4.0f);
        set => Accessor.SetFloat("scrollbar_corner_radius", value);
    }
    public bool HorizontalOverflow => Accessor.GetBool("horizontal_overflow");
    public bool VerticalOverflow => Accessor.GetBool("vertical_overflow");
    public float HorizontalNormalized => Accessor.GetFloat("horizontal_normalized");
    public float VerticalNormalized => Accessor.GetFloat("vertical_normalized");
}

public enum UISliderDirection
{
    LeftToRight = 0,
    RightToLeft = 1,
    BottomToTop = 2,
    TopToBottom = 3,
}

public readonly struct UISliderComponent : IComponentBinding<UISliderComponent>
{
    public static string NativeTypeName => "UISliderComponent";
    public static UISliderComponent FromHandle(ComponentHandle handle) => new(handle);

    private UISliderComponent(ComponentHandle handle) => Accessor = new ComponentAccessor(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;

    public float Minimum { get => Accessor.GetFloat("minimum"); set => Accessor.SetFloat("minimum", value); }
    public float Maximum { get => Accessor.GetFloat("maximum", 1.0f); set => Accessor.SetFloat("maximum", value); }
    public float Value { get => Accessor.GetFloat("value"); set => Accessor.SetFloat("value", value); }
    public float NormalizedValue => Accessor.GetFloat("normalized_value");
    public bool WholeNumbers { get => Accessor.GetBool("whole_numbers"); set => Accessor.SetBool("whole_numbers", value); }
    public UISliderDirection Direction
    {
        get => (UISliderDirection)Accessor.GetInt("direction");
        set => Accessor.SetInt("direction", (int)value);
    }
    public bool Interactable { get => Accessor.GetBool("interactable", true); set => Accessor.SetBool("interactable", value); }
    public ComponentReference FillImage { get => Accessor.GetComponentReference("fill_image"); set => Accessor.SetComponentReference("fill_image", value); }
    public ComponentReference HandleRect { get => Accessor.GetComponentReference("handle_rect"); set => Accessor.SetComponentReference("handle_rect", value); }
    public float KeyboardStep { get => Accessor.GetFloat("keyboard_step", 0.1f); set => Accessor.SetFloat("keyboard_step", value); }
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
    public ComponentReference TextTarget
    {
        get => Accessor.GetComponentReference("text_target");
        set => Accessor.SetComponentReference("text_target", value);
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
    public Color PlaceholderColor
    {
        get => Accessor.GetColor("placeholder_color");
        set => Accessor.SetColor("placeholder_color", value);
    }
    public Color SelectionColor
    {
        get => Accessor.GetColor("selection_color");
        set => Accessor.SetColor("selection_color", value);
    }
    public Color CaretColor
    {
        get => Accessor.GetColor("caret_color");
        set => Accessor.SetColor("caret_color", value);
    }
    public float CaretWidth
    {
        get => Accessor.GetFloat("caret_width", 1.5f);
        set => Accessor.SetFloat("caret_width", value);
    }
    public float CaretBlinkSeconds
    {
        get => Accessor.GetFloat("caret_blink_seconds", 0.5f);
        set => Accessor.SetFloat("caret_blink_seconds", value);
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
    public int CaretIndex => Accessor.GetInt("caret_index");
    public int SelectionStart => Accessor.GetInt("selection_start");
    public int SelectionEnd => Accessor.GetInt("selection_end");
    public bool IsImeComposing => Accessor.GetBool("ime_composing");
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
    public float NavigationBias
    {
        get => Accessor.GetFloat("navigation_bias", 2.0f);
        set => Accessor.SetFloat("navigation_bias", value);
    }
    public ComponentReference NavigateUp
    {
        get => Accessor.GetComponentReference("navigate_up");
        set => Accessor.SetComponentReference("navigate_up", value);
    }
    public ComponentReference NavigateDown
    {
        get => Accessor.GetComponentReference("navigate_down");
        set => Accessor.SetComponentReference("navigate_down", value);
    }
    public ComponentReference NavigateLeft
    {
        get => Accessor.GetComponentReference("navigate_left");
        set => Accessor.SetComponentReference("navigate_left", value);
    }
    public ComponentReference NavigateRight
    {
        get => Accessor.GetComponentReference("navigate_right");
        set => Accessor.SetComponentReference("navigate_right", value);
    }
    public bool OverrideFocusStyle
    {
        get => Accessor.GetBool("override_focus_style");
        set => Accessor.SetBool("override_focus_style", value);
    }
    public bool FocusOutlineEnabled
    {
        get => Accessor.GetBool("focus_outline_enabled", true);
        set => Accessor.SetBool("focus_outline_enabled", value);
    }
    public Color FocusOutlineColor
    {
        get => Accessor.GetColor("focus_outline_color");
        set => Accessor.SetColor("focus_outline_color", value);
    }
    public float FocusOutlineWidth
    {
        get => Accessor.GetFloat("focus_outline_width", 2.0f);
        set => Accessor.SetFloat("focus_outline_width", value);
    }
    public float FocusCornerRadius
    {
        get => Accessor.GetFloat("focus_corner_radius", 4.0f);
        set => Accessor.SetFloat("focus_corner_radius", value);
    }
    public bool Focused => Accessor.GetBool("focused");
}

public readonly struct UIHorizontalLayoutGroupComponent : IComponentBinding<UIHorizontalLayoutGroupComponent>
{
    public static string NativeTypeName => "UIHorizontalLayoutGroupComponent";
    public static UIHorizontalLayoutGroupComponent FromHandle(ComponentHandle handle) => new(handle);
    private UIHorizontalLayoutGroupComponent(ComponentHandle handle) => Accessor = new(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;
    public Vector4 Padding { get => Accessor.GetVector4("padding"); set => Accessor.SetVector4("padding", value); }
    public float Spacing { get => Accessor.GetFloat("spacing"); set => Accessor.SetFloat("spacing", value); }
    public int Alignment { get => Accessor.GetInt("alignment"); set => Accessor.SetInt("alignment", value); }
    public bool ControlChildWidth { get => Accessor.GetBool("control_child_width"); set => Accessor.SetBool("control_child_width", value); }
    public bool ControlChildHeight { get => Accessor.GetBool("control_child_height", true); set => Accessor.SetBool("control_child_height", value); }
}

public readonly struct UIVerticalLayoutGroupComponent : IComponentBinding<UIVerticalLayoutGroupComponent>
{
    public static string NativeTypeName => "UIVerticalLayoutGroupComponent";
    public static UIVerticalLayoutGroupComponent FromHandle(ComponentHandle handle) => new(handle);
    private UIVerticalLayoutGroupComponent(ComponentHandle handle) => Accessor = new(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;
    public Vector4 Padding { get => Accessor.GetVector4("padding"); set => Accessor.SetVector4("padding", value); }
    public float Spacing { get => Accessor.GetFloat("spacing"); set => Accessor.SetFloat("spacing", value); }
    public int Alignment { get => Accessor.GetInt("alignment"); set => Accessor.SetInt("alignment", value); }
    public bool ControlChildWidth { get => Accessor.GetBool("control_child_width", true); set => Accessor.SetBool("control_child_width", value); }
    public bool ControlChildHeight { get => Accessor.GetBool("control_child_height"); set => Accessor.SetBool("control_child_height", value); }
}

public readonly struct UIGridLayoutGroupComponent : IComponentBinding<UIGridLayoutGroupComponent>
{
    public static string NativeTypeName => "UIGridLayoutGroupComponent";
    public static UIGridLayoutGroupComponent FromHandle(ComponentHandle handle) => new(handle);
    private UIGridLayoutGroupComponent(ComponentHandle handle) => Accessor = new(handle);
    public ComponentAccessor Accessor { get; }
    public ComponentHandle Handle => Accessor.Handle;
    public bool IsValid => Accessor.IsValid;
    public Vector4 Padding { get => Accessor.GetVector4("padding"); set => Accessor.SetVector4("padding", value); }
    public Vector2 Spacing { get => Accessor.GetVector2("spacing"); set => Accessor.SetVector2("spacing", value); }
    public Vector2 CellSize { get => Accessor.GetVector2("cell_size"); set => Accessor.SetVector2("cell_size", value); }
    public int Alignment { get => Accessor.GetInt("alignment"); set => Accessor.SetInt("alignment", value); }
    public int Constraint { get => Accessor.GetInt("constraint"); set => Accessor.SetInt("constraint", value); }
    public int ConstraintCount { get => Accessor.GetInt("constraint_count", 1); set => Accessor.SetInt("constraint_count", value); }
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
