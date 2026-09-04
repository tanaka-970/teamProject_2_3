using System;
using System.Collections.Generic;

namespace ReplayEngine;

// PropertyRegistry から起こした Component の一覧。手で書き換えない。
//
// エンジン側では Validation & Diagnostics の Component API タブに同じものが出る。
// C# からはこう引ける:
//   foreach (var entry in ComponentCatalog.All) ...
//   var motion = ComponentCatalog.Find("MotionPlayerComponent");
//
// 作り直しかた:
//   x64\Release\3dgp.exe --dump-component-properties
//   python Tools\generate_component_bindings.py

public readonly struct ComponentPropertyEntry
{
    public ComponentPropertyEntry(string name, string type, bool readOnly, bool availableInCSharp)
    {
        Name = name;
        Type = type;
        ReadOnly = readOnly;
        AvailableInCSharp = availableInCSharp;
    }

    // C++ 側の登録名。ComponentAccessor へ渡すのはこの名前。
    public string Name { get; }

    // PropertyRegistry の型名（float / vec3 / enum など）。
    public string Type { get; }
    public bool ReadOnly { get; }

    // ComponentAccessor に窓口があるか。array だけ false。
    public bool AvailableInCSharp { get; }
}

public readonly struct ComponentCatalogEntry
{
    public ComponentCatalogEntry(string typeName, string displayName, string category,
        IReadOnlyList<ComponentPropertyEntry> properties)
    {
        TypeName = typeName;
        DisplayName = displayName;
        Category = category;
        Properties = properties;
    }

    public string TypeName { get; }
    public string DisplayName { get; }
    public string Category { get; }
    public IReadOnlyList<ComponentPropertyEntry> Properties { get; }
}

public static class ComponentCatalog
{
    public static IReadOnlyList<ComponentCatalogEntry> All => Entries;

    public static ComponentCatalogEntry? Find(string typeName)
    {
        foreach (ComponentCatalogEntry entry in Entries)
            if (entry.TypeName == typeName) return entry;
        return null;
    }

    private static readonly ComponentCatalogEntry[] Entries =
    {
        new("MissingComponent", "Missing Component", "Internal", new ComponentPropertyEntry[]
        {
        }),
        new("TransformComponent", "Transform", "Core", new ComponentPropertyEntry[]
        {
            new("position", "vec3", false, true),
            new("rotation", "vec3", false, true),
            new("scale", "vec3", false, true),
        }),
        new("PivotComponent", "Pivot", "Core", new ComponentPropertyEntry[]
        {
            new("mode", "enum", false, true),
            new("local_point", "vec3", false, true),
            new("target", "objref", false, true),
        }),
        new("MeshRendererComponent", "Mesh Renderer", "Rendering", new ComponentPropertyEntry[]
        {
            new("mesh_asset", "asset", false, true),
            new("material_asset", "asset", false, true),
            new("material_slot_count", "int", false, true),
            new("material_override", "bool", false, true),
            new("tint", "color", false, true),
            new("shading_model", "enum", false, true),
            new("outline", "bool", false, true),
            new("cast_shadow", "bool", false, true),
            new("receive_shadow", "bool", false, true),
            new("shadow_alpha_clip", "bool", false, true),
            new("shadow_alpha_cutoff", "float", false, true),
            new("rendering_layer", "int", false, true),
            new("visible", "bool", false, true),
            new("local_position_offset", "vec3", false, true),
            new("local_rotation_offset", "vec3", false, true),
            new("local_scale_multiplier", "vec3", false, true),
        }),
        new("PrimitiveMeshRendererComponent", "Primitive Mesh Renderer", "Rendering", new ComponentPropertyEntry[]
        {
            new("primitive_type", "enum", false, true),
            new("material_asset", "asset", false, true),
            new("material_slot_count", "int", false, true),
            new("material_override", "bool", false, true),
            new("tint", "color", false, true),
            new("shading_model", "enum", false, true),
            new("outline", "bool", false, true),
            new("cast_shadow", "bool", false, true),
            new("receive_shadow", "bool", false, true),
            new("shadow_alpha_clip", "bool", false, true),
            new("shadow_alpha_cutoff", "float", false, true),
            new("rendering_layer", "int", false, true),
            new("visible", "bool", false, true),
        }),
        new("PostProcessVolumeComponent", "Post Process Volume", "Rendering", new ComponentPropertyEntry[]
        {
            new("priority", "int", false, true),
            new("bloom_enabled", "bool", false, true),
            new("bloom_threshold", "float", false, true),
            new("bloom_intensity", "float", false, true),
            new("luminance_enabled", "bool", false, true),
            new("final_pass_enabled", "bool", false, true),
            new("vignette_enabled", "bool", false, true),
            new("vignette_intensity", "float", false, true),
            new("ssao_enabled", "bool", false, true),
            new("ssao_radius", "float", false, true),
            new("ssao_intensity", "float", false, true),
            new("ssr_enabled", "bool", false, true),
            new("ssr_intensity", "float", false, true),
            new("taa_enabled", "bool", false, true),
            new("exposure", "float", false, true),
            new("color_filter", "color", false, true),
        }),
        new("SkyboxComponent", "空", "Rendering", new ComponentPropertyEntry[]
        {
            new("cubemap", "assetref", false, true),
            new("keyframes", "array", false, false),
            new("priority", "int", false, true),
            new("sky_enabled", "bool", false, true),
            new("rotation_degrees", "float", false, true),
            new("intensity", "float", false, true),
            new("toon_environment", "float", false, true),
            new("time", "float", false, true),
            new("time_speed", "float", false, true),
            new("clouds_enabled", "bool", false, true),
            new("cloud_layer1_speed", "vec2", false, true),
            new("cloud_layer1_scale", "float", false, true),
            new("cloud_layer1_density", "float", false, true),
            new("cloud_layer1_color", "color", false, true),
            new("cloud_layer2_speed", "vec2", false, true),
            new("cloud_layer2_scale", "float", false, true),
            new("cloud_layer2_density", "float", false, true),
            new("cloud_layer2_color", "color", false, true),
            new("stars_enabled", "bool", false, true),
            new("star_density", "float", false, true),
            new("star_intensity", "float", false, true),
            new("star_color", "color", false, true),
            new("moon_enabled", "bool", false, true),
            new("moon_direction", "vec3", false, true),
            new("moon_size", "float", false, true),
            new("moon_intensity", "float", false, true),
            new("moon_color", "color", false, true),
        }),
        new("ScreenEffectStackComponent", "Screen Effect Stack", "Rendering", new ComponentPropertyEntry[]
        {
            new("enabled", "bool", false, true),
            new("use_preset", "bool", false, true),
            new("effect_preset", "assetref", false, true),
            new("effect_count", "int", false, true),
            new("apply_stage", "enum", false, true),
            new("target_mode", "enum", false, true),
            new("target_rendering_layer", "int", false, true),
        }),
        new("ModelEffectStackComponent", "Model Effect Stack", "Rendering", new ComponentPropertyEntry[]
        {
            new("enabled", "bool", false, true),
            new("use_preset", "bool", false, true),
            new("effect_preset", "assetref", false, true),
            new("effect_count", "int", false, true),
            new("target_slot_mode", "enum", false, true),
            new("depth_mode", "enum", false, true),
            new("extract_mode", "enum", false, true),
            new("max_bleed_pixels", "float", false, true),
        }),
        new("NormalAdjustComponent", "Normal Adjust", "Rendering", new ComponentPropertyEntry[]
        {
            new("blend", "float", false, true),
            new("center", "vec3", false, true),
            new("radius", "float", false, true),
            new("falloff", "float", false, true),
            new("bone", "string", false, true),
            new("target_slot_mode", "enum", false, true),
            new("target_slot_index", "int", false, true),
        }),
        new("ParticleEmitterComponent", "Particle Emitter", "Rendering", new ComponentPropertyEntry[]
        {
            new("emitting", "bool", false, true),
            new("priority", "int", false, true),
            new("spawn_rate", "float", false, true),
            new("lifetime", "float", false, true),
            new("start_speed", "float", false, true),
            new("gravity", "float", false, true),
            new("drag", "float", false, true),
            new("start_size", "float", false, true),
            new("end_size", "float", false, true),
            new("start_color", "color", false, true),
            new("end_color", "color", false, true),
            new("direction", "vec3", false, true),
            new("cone_angle", "float", false, true),
            new("sprite", "assetref", false, true),
            new("blend_mode", "enum", false, true),
            new("max_particles", "int", false, true),
        }),
        new("LineRendererComponent", "3D ライン", "Rendering", new ComponentPropertyEntry[]
        {
            new("point_count", "int", false, true),
            new("smoothing", "int", false, true),
            new("closed", "bool", false, true),
            new("width_start", "float", false, true),
            new("width_end", "float", false, true),
            new("billboard", "bool", false, true),
            new("uv_mode", "enum", false, true),
            new("uv_tiling", "float", false, true),
            new("uv_scroll", "float", false, true),
            new("texture", "assetref", false, true),
            new("fill_color", "color", false, true),
            new("fill_color_2", "color", false, true),
            new("fill_mode", "enum", false, true),
            new("trim_start", "float", false, true),
            new("trim_end", "float", false, true),
            new("trim_offset", "float", false, true),
        }),
        new("TrailComponent", "軌跡", "Rendering", new ComponentPropertyEntry[]
        {
            new("emitting", "bool", false, true),
            new("lifetime", "float", false, true),
            new("min_distance", "float", false, true),
            new("max_points", "int", false, true),
            new("world_space", "bool", false, true),
            new("width_start", "float", false, true),
            new("width_end", "float", false, true),
            new("billboard", "bool", false, true),
            new("uv_mode", "enum", false, true),
            new("uv_tiling", "float", false, true),
            new("uv_scroll", "float", false, true),
            new("texture", "assetref", false, true),
            new("fill_color", "color", false, true),
            new("fill_color_2", "color", false, true),
            new("fill_mode", "enum", false, true),
            new("trim_start", "float", false, true),
            new("trim_end", "float", false, true),
            new("trim_offset", "float", false, true),
        }),
        new("DirectionalLightComponent", "Directional Light", "Lighting", new ComponentPropertyEntry[]
        {
            new("color", "color", false, true),
            new("intensity", "float", false, true),
            new("cast_shadows", "bool", false, true),
            new("shadow_strength", "float", false, true),
            new("shadow_depth_bias", "float", false, true),
            new("shadow_normal_bias", "float", false, true),
            new("shadow_distance", "float", false, true),
        }),
        new("PointLightComponent", "Point Light", "Lighting", new ComponentPropertyEntry[]
        {
            new("color", "color", false, true),
            new("intensity", "float", false, true),
            new("range", "float", false, true),
            new("cast_shadows", "bool", false, true),
            new("shadow_strength", "float", false, true),
            new("shadow_depth_bias", "float", false, true),
            new("shadow_normal_bias", "float", false, true),
            new("shadow_near_plane", "float", false, true),
        }),
        new("SpotLightComponent", "Spot Light", "Lighting", new ComponentPropertyEntry[]
        {
            new("color", "color", false, true),
            new("intensity", "float", false, true),
            new("range", "float", false, true),
            new("inner_angle_degrees", "float", false, true),
            new("outer_angle_degrees", "float", false, true),
            new("cast_shadows", "bool", false, true),
            new("shadow_strength", "float", false, true),
            new("shadow_depth_bias", "float", false, true),
            new("shadow_normal_bias", "float", false, true),
            new("shadow_near_plane", "float", false, true),
        }),
        new("RectTransformComponent", "Rect Transform", "UI", new ComponentPropertyEntry[]
        {
            new("anchor_min", "vec2", false, true),
            new("anchor_max", "vec2", false, true),
            new("anchored_position", "vec2", false, true),
            new("size_delta", "vec2", false, true),
            new("pivot", "vec2", false, true),
            new("rotation", "float", false, true),
            new("scale", "vec2", false, true),
            new("sort_order", "int", false, true),
            new("resolved_rect", "vec4", true, true),
        }),
        new("CanvasComponent", "Canvas", "UI", new ComponentPropertyEntry[]
        {
            new("reference_resolution", "vec2", false, true),
            new("render_mode", "enum", false, true),
            new("scale_mode", "enum", false, true),
            new("match_width_or_height", "float", false, true),
            new("sort_order", "int", false, true),
            new("opacity", "float", false, true),
        }),
        new("UIImageComponent", "Image", "UI", new ComponentPropertyEntry[]
        {
            new("sprite", "assetref", false, true),
            new("atlas", "assetref", false, true),
            new("atlas_region", "string", false, true),
            new("color", "color", false, true),
            new("fill_color_2", "color", false, true),
            new("fill_mode", "enum", false, true),
            new("fill_angle", "float", false, true),
            new("fill_center", "vec2", false, true),
            new("stroke_mode", "enum", false, true),
            new("stroke_color_2", "color", false, true),
            new("opacity", "float", false, true),
            new("fill_amount", "float", false, true),
            new("fill_method", "enum", false, true),
            new("fill_reverse", "bool", false, true),
            new("blend_mode", "enum", false, true),
            new("uv_offset", "vec2", false, true),
            new("uv_scale", "vec2", false, true),
            new("nine_slice", "vec4", false, true),
            new("preserve_aspect", "bool", false, true),
        }),
        new("UIShapeImageComponent", "Shape Image", "UI", new ComponentPropertyEntry[]
        {
            new("path_closed", "bool", false, true),
            new("path_points", "array", false, false),
            new("path_in_handles", "array", false, false),
            new("path_out_handles", "array", false, false),
        }),
        new("UISpriteAnimatorComponent", "Sprite Animator", "UI", new ComponentPropertyEntry[]
        {
            new("columns", "int", false, true),
            new("rows", "int", false, true),
            new("start_frame", "int", false, true),
            new("end_frame", "int", false, true),
            new("frames_per_second", "float", false, true),
            new("play_mode", "enum", false, true),
            new("playing", "bool", false, true),
            new("frame", "float", false, true),
        }),
        new("UITextComponent", "Text", "UI", new ComponentPropertyEntry[]
        {
            new("text", "string", false, true),
            new("font", "assetref", false, true),
            new("font_size", "float", false, true),
            new("color", "color", false, true),
            new("opacity", "float", false, true),
            new("character_spacing", "float", false, true),
            new("line_spacing", "float", false, true),
            new("horizontal_align", "enum", false, true),
            new("vertical_align", "enum", false, true),
            new("word_wrap", "bool", false, true),
            new("rich_text", "bool", false, true),
            new("localization_key", "string", false, true),
            new("number_source", "compref", false, true),
            new("number_source_property", "string", false, true),
            new("number_format", "string", false, true),
            new("number_digits", "int", false, true),
            new("outline_width", "float", false, true),
            new("outline_color", "color", false, true),
            new("shadow_offset", "vec2", false, true),
            new("shadow_color", "color", false, true),
        }),
        new("UITextAnimatorComponent", "Text Animator", "UI", new ComponentPropertyEntry[]
        {
            new("range_start", "float", false, true),
            new("range_end", "float", false, true),
            new("range_offset", "float", false, true),
            new("range_shape", "enum", false, true),
            new("range_smoothness", "float", false, true),
            new("position_offset", "vec2", false, true),
            new("rotation", "float", false, true),
            new("scale", "vec2", false, true),
            new("opacity", "float", false, true),
            new("color", "color", false, true),
            new("character_spacing", "float", false, true),
            new("random_seed", "int", false, true),
            new("random_position", "vec2", false, true),
            new("random_rotation", "float", false, true),
            new("anchor", "enum", false, true),
        }),
        new("UIShapeComponent", "Shape", "UI", new ComponentPropertyEntry[]
        {
            new("shape", "enum", false, true),
            new("fill_color", "color", false, true),
            new("fill_color_2", "color", false, true),
            new("fill_color_3", "color", false, true),
            new("fill_color_4", "color", false, true),
            new("fill_stop_2", "float", false, true),
            new("fill_stop_3", "float", false, true),
            new("fill_stop_4", "float", false, true),
            new("fill_mode", "enum", false, true),
            new("fill_angle", "float", false, true),
            new("fill_center", "vec2", false, true),
            new("stroke_color", "color", false, true),
            new("stroke_color_2", "color", false, true),
            new("stroke_mode", "enum", false, true),
            new("stroke_width", "float", false, true),
            new("corner_radius", "float", false, true),
            new("arc_curvature", "float", false, true),
            new("sides", "int", false, true),
            new("superellipse_exponent", "float", false, true),
            new("polar_base_radius", "float", false, true),
            new("polar_amplitude", "float", false, true),
            new("polar_lobes", "float", false, true),
            new("polar_rotation", "float", false, true),
            new("trim_start", "float", false, true),
            new("trim_end", "float", false, true),
            new("trim_offset", "float", false, true),
            new("dash_length", "float", false, true),
            new("dash_gap", "float", false, true),
            new("dash_offset", "float", false, true),
            new("path_closed", "bool", false, true),
            new("path_points", "array", false, false),
            new("path_in_handles", "array", false, false),
            new("path_out_handles", "array", false, false),
        }),
        new("UIPuppetDeformComponent", "Puppet Deform", "UI", new ComponentPropertyEntry[]
        {
            new("enabled_deform", "bool", false, true),
            new("grid_columns", "int", false, true),
            new("grid_rows", "int", false, true),
            new("global_strength", "float", false, true),
            new("pin_bind_positions", "array", false, false),
            new("pin_positions", "array", false, false),
            new("pin_radii", "array", false, false),
        }),
        new("UIButtonComponent", "Button", "UI", new ComponentPropertyEntry[]
        {
            new("interactable", "bool", false, true),
            new("target_image", "compref", false, true),
            new("normal_color", "color", false, true),
            new("hover_color", "color", false, true),
            new("pressed_color", "color", false, true),
            new("disabled_color", "color", false, true),
            new("normal_motion", "assetref", false, true),
            new("hover_motion", "assetref", false, true),
            new("pressed_motion", "assetref", false, true),
            new("disabled_motion", "assetref", false, true),
            new("navigation_enabled", "bool", false, true),
            new("navigation_order", "int", false, true),
            new("state_blend_seconds", "float", false, true),
            new("state", "enum", true, true),
        }),
        new("UISelectableComponent", "Selectable", "UI", new ComponentPropertyEntry[]
        {
            new("interactable", "bool", false, true),
            new("navigation_enabled", "bool", false, true),
            new("navigation_order", "int", false, true),
            new("navigation_bias", "float", false, true),
            new("navigate_up", "compref", false, true),
            new("navigate_down", "compref", false, true),
            new("navigate_left", "compref", false, true),
            new("navigate_right", "compref", false, true),
            new("override_focus_style", "bool", false, true),
            new("focus_outline_enabled", "bool", false, true),
            new("focus_outline_color", "color", false, true),
            new("focus_outline_width", "float", false, true),
            new("focus_corner_radius", "float", false, true),
            new("focused", "bool", true, true),
        }),
        new("UIHorizontalLayoutGroupComponent", "Horizontal Layout Group", "UI", new ComponentPropertyEntry[]
        {
            new("padding", "vec4", false, true),
            new("spacing", "float", false, true),
            new("alignment", "enum", false, true),
            new("control_child_width", "bool", false, true),
            new("control_child_height", "bool", false, true),
        }),
        new("UIVerticalLayoutGroupComponent", "Vertical Layout Group", "UI", new ComponentPropertyEntry[]
        {
            new("padding", "vec4", false, true),
            new("spacing", "float", false, true),
            new("alignment", "enum", false, true),
            new("control_child_width", "bool", false, true),
            new("control_child_height", "bool", false, true),
        }),
        new("UIGridLayoutGroupComponent", "Grid Layout Group", "UI", new ComponentPropertyEntry[]
        {
            new("padding", "vec4", false, true),
            new("spacing", "vec2", false, true),
            new("cell_size", "vec2", false, true),
            new("alignment", "enum", false, true),
            new("constraint", "enum", false, true),
            new("constraint_count", "int", false, true),
        }),
        new("UIScrollViewComponent", "Scroll View", "UI", new ComponentPropertyEntry[]
        {
            new("content", "compref", false, true),
            new("horizontal", "bool", false, true),
            new("vertical", "bool", false, true),
            new("clamp_when_content_fits", "bool", false, true),
            new("scroll_sensitivity", "float", false, true),
            new("scroll_offset", "vec2", false, true),
            new("show_scrollbars", "bool", false, true),
            new("scrollbar_width", "float", false, true),
            new("scrollbar_track_color", "color", false, true),
            new("scrollbar_thumb_color", "color", false, true),
            new("scrollbar_corner_radius", "float", false, true),
            new("horizontal_overflow", "bool", true, true),
            new("vertical_overflow", "bool", true, true),
            new("horizontal_normalized", "float", true, true),
            new("vertical_normalized", "float", true, true),
        }),
        new("UISliderComponent", "Slider", "UI", new ComponentPropertyEntry[]
        {
            new("minimum", "float", false, true),
            new("maximum", "float", false, true),
            new("value", "float", false, true),
            new("whole_numbers", "bool", false, true),
            new("direction", "enum", false, true),
            new("interactable", "bool", false, true),
            new("fill_image", "compref", false, true),
            new("handle_rect", "compref", false, true),
            new("keyboard_step", "float", false, true),
            new("normalized_value", "float", true, true),
        }),
        new("UIInputFieldComponent", "Input Field", "UI", new ComponentPropertyEntry[]
        {
            new("text", "string", false, true),
            new("text_target", "compref", false, true),
            new("placeholder", "string", false, true),
            new("text_color", "color", false, true),
            new("placeholder_color", "color", false, true),
            new("selection_color", "color", false, true),
            new("caret_color", "color", false, true),
            new("caret_width", "float", false, true),
            new("caret_blink_seconds", "float", false, true),
            new("max_characters", "int", false, true),
            new("password", "bool", false, true),
            new("read_only", "bool", false, true),
            new("caret_index", "int", true, true),
            new("selection_start", "int", true, true),
            new("selection_end", "int", true, true),
            new("ime_composing", "bool", true, true),
        }),
        new("UIMaskComponent", "Mask", "UI", new ComponentPropertyEntry[]
        {
            new("enabled_mask", "bool", false, true),
            new("show_mask_graphic", "bool", false, true),
            new("mask_mode", "enum", false, true),
            new("shape_kind", "enum", false, true),
            new("shape_sides", "int", false, true),
            new("shape_inner_radius", "float", false, true),
            new("shape_corner_radius", "float", false, true),
            new("shape_rotation", "float", false, true),
            new("group_scale", "vec2", false, true),
            new("mask_image", "assetref", false, true),
            new("mask_object", "objref", false, true),
            new("matte_objects", "array", false, false),
            new("matte_operations", "array", false, false),
            new("invert", "bool", false, true),
            new("softness", "float", false, true),
        }),
        new("UILanguageSwitchComponent", "Language Switch", "UI", new ComponentPropertyEntry[]
        {
            new("language", "string", false, true),
        }),
        new("UIButtonPropertyToggleComponent", "Button Property Toggle", "UI", new ComponentPropertyEntry[]
        {
            new("target", "compref", false, true),
            new("target_property", "string", false, true),
        }),
        new("UIEffectStackComponent", "Effect Stack", "UI", new ComponentPropertyEntry[]
        {
            new("enabled", "bool", false, true),
            new("target_scope", "enum", false, true),
            new("capture_backdrop", "bool", false, true),
            new("use_preset", "bool", false, true),
            new("effect_preset", "assetref", false, true),
            new("effect_count", "int", false, true),
        }),
        new("MotionPlayerComponent", "Motion Player", "Motion", new ComponentPropertyEntry[]
        {
            new("motion", "assetref", false, true),
            new("key", "string", false, true),
            new("play_on_start", "bool", false, true),
            new("trigger", "enum", false, true),
            new("trigger_delay", "float", false, true),
            new("trigger_source", "compref", false, true),
            new("trigger_state", "string", false, true),
            new("loop", "bool", false, true),
            new("wrap_mode", "enum", false, true),
            new("auto_stop_on_end", "bool", false, true),
            new("ignore_time_scale", "bool", false, true),
            new("blend_in_seconds", "float", false, true),
            new("speed", "float", false, true),
            new("random_seed", "int", false, true),
            new("time_offset_random", "float", false, true),
            new("speed_random", "float", false, true),
            new("weight", "float", false, true),
            new("state", "enum", true, true),
            new("time", "float", false, true),
        }),
        new("CompositionPlayerComponent", "Composition Player", "Motion", new ComponentPropertyEntry[]
        {
            new("composition", "assetref", false, true),
            new("key", "string", false, true),
            new("play_on_start", "bool", false, true),
            new("play_on_state_change", "bool", false, true),
            new("state_source", "compref", false, true),
            new("state_name", "string", false, true),
            new("loop", "bool", false, true),
            new("ignore_time_scale", "bool", false, true),
            new("hold_on_end", "bool", false, true),
            new("speed", "float", false, true),
            new("weight", "float", false, true),
            new("state", "enum", true, true),
            new("time", "float", false, true),
        }),
        new("StateComponent", "State", "Core", new ComponentPropertyEntry[]
        {
            new("state_count", "int", false, true),
            new("current_state", "string", false, true),
        }),
        new("PropertyLinkComponent", "Property Link", "Motion", new ComponentPropertyEntry[]
        {
            new("source_object", "compref", false, true),
            new("source_property", "string", false, true),
            new("target_object", "compref", false, true),
            new("target_property", "string", false, true),
            new("source_min", "float", false, true),
            new("source_max", "float", false, true),
            new("target_min", "float", false, true),
            new("target_max", "float", false, true),
            new("invert", "bool", false, true),
            new("clamp", "bool", false, true),
            new("easing", "enum", false, true),
            new("smoothing", "float", false, true),
        }),
        new("PersistentComponent", "Persistent", "Scene", new ComponentPropertyEntry[]
        {
        }),
        new("SceneLoaderComponent", "Scene Loader", "Scene", new ComponentPropertyEntry[]
        {
            new("progress", "float", true, true),
            new("is_loading", "bool", true, true),
            new("state", "enum", true, true),
        }),
        new("SkinnedMeshRendererComponent", "Skinned Mesh Renderer", "Rendering", new ComponentPropertyEntry[]
        {
            new("mesh_asset", "asset", false, true),
            new("material_asset", "asset", false, true),
            new("material_slot_count", "int", false, true),
            new("material_override", "bool", false, true),
            new("tint", "color", false, true),
            new("shading_model", "enum", false, true),
            new("outline", "bool", false, true),
            new("cast_shadow", "bool", false, true),
            new("receive_shadow", "bool", false, true),
            new("shadow_alpha_clip", "bool", false, true),
            new("shadow_alpha_cutoff", "float", false, true),
            new("rendering_layer", "int", false, true),
            new("visible", "bool", false, true),
            new("visual_rotation_offset", "vec3", false, true),
            new("local_position_offset", "vec3", false, true),
            new("local_scale_multiplier", "vec3", false, true),
            new("apply_fbx_coordinate_transform", "bool", false, true),
        }),
        new("AnimatorComponent", "Animator", "Rendering", new ComponentPropertyEntry[]
        {
            new("state_count", "int", false, true),
            new("default_state", "string", false, true),
            new("transition_count", "int", false, true),
            new("playback_speed", "float", false, true),
            new("playing", "bool", false, true),
            new("current_state", "string", true, true),
            new("current_clip", "int", true, true),
            new("animation_time", "float", true, true),
            new("blend_factor", "float", true, true),
            new("idle_clip", "int", false, true),
            new("walk_clip", "int", false, true),
            new("jump_clip", "int", false, true),
            new("loop", "bool", false, true),
            new("walk_speed_threshold", "float", false, true),
        }),
        new("RigidbodyComponent", "Rigidbody", "Physics", new ComponentPropertyEntry[]
        {
            new("body_type", "enum", false, true),
            new("mass", "float", false, true),
            new("linear_damping", "float", false, true),
            new("angular_damping", "float", false, true),
            new("gravity_scale", "float", false, true),
            new("restitution", "float", false, true),
            new("friction", "float", false, true),
            new("freeze_position", "vec3", false, true),
            new("freeze_rotation", "vec3", false, true),
            new("start_asleep", "bool", false, true),
            new("use_ccd", "bool", false, true),
            new("linear_velocity", "vec3", true, true),
            new("angular_velocity", "vec3", true, true),
            new("is_sleeping", "bool", true, true),
            new("status", "string", true, true),
        }),
        new("SphereColliderComponent", "Sphere Collider", "Physics", new ComponentPropertyEntry[]
        {
            new("collider_key", "int", true, true),
            new("center_offset", "vec3", false, true),
            new("collision_layer", "layer", false, true),
            new("collision_mask", "layermask", false, true),
            new("is_trigger", "bool", false, true),
            new("debug_draw", "bool", false, true),
            new("radius", "float", false, true),
            new("skin_width", "float", false, true),
            new("walkable_normal_y", "float", false, true),
        }),
        new("BoxColliderComponent", "Box Collider", "Physics", new ComponentPropertyEntry[]
        {
            new("collider_key", "int", true, true),
            new("center_offset", "vec3", false, true),
            new("collision_layer", "layer", false, true),
            new("collision_mask", "layermask", false, true),
            new("is_trigger", "bool", false, true),
            new("debug_draw", "bool", false, true),
            new("size", "vec3", false, true),
        }),
        new("CapsuleColliderComponent", "Capsule Collider", "Physics", new ComponentPropertyEntry[]
        {
            new("collider_key", "int", true, true),
            new("center_offset", "vec3", false, true),
            new("collision_layer", "layer", false, true),
            new("collision_mask", "layermask", false, true),
            new("is_trigger", "bool", false, true),
            new("debug_draw", "bool", false, true),
            new("radius", "float", false, true),
            new("height", "float", false, true),
            new("axis", "enum", false, true),
        }),
        new("MeshColliderComponent", "Mesh Collider", "Physics", new ComponentPropertyEntry[]
        {
            new("collider_key", "int", true, true),
            new("center_offset", "vec3", false, true),
            new("collision_layer", "layer", false, true),
            new("collision_mask", "layermask", false, true),
            new("is_trigger", "bool", false, true),
            new("debug_draw", "bool", false, true),
            new("mesh_source", "enum", false, true),
            new("mesh_asset", "asset", false, true),
            new("cook_cell_size", "float", false, true),
            new("double_sided", "bool", false, true),
            new("debug_draw_wireframe", "bool", false, true),
        }),
        new("LandscapeComponent", "Landscape", "Landscape", new ComponentPropertyEntry[]
        {
            new("default_resolution", "int", false, true),
            new("default_cell_size", "float", false, true),
        }),
        new("LandscapeRendererComponent", "Landscape Renderer", "Landscape", new ComponentPropertyEntry[]
        {
            new("tint", "color", false, true),
            new("visible", "bool", false, true),
            new("cast_shadow", "bool", false, true),
            new("receive_shadow", "bool", false, true),
            new("double_sided", "bool", false, true),
        }),
        new("LandscapeColliderComponent", "Landscape Collider", "Landscape", new ComponentPropertyEntry[]
        {
            new("collider_key", "int", true, true),
            new("center_offset", "vec3", false, true),
            new("collision_layer", "layer", false, true),
            new("collision_mask", "layermask", false, true),
            new("is_trigger", "bool", false, true),
            new("debug_draw", "bool", false, true),
            new("double_sided", "bool", false, true),
            new("collision_cell_size", "float", false, true),
            new("debug_draw_wireframe", "bool", false, true),
        }),
        new("CharacterMotorComponent", "Character Motor", "Gameplay", new ComponentPropertyEntry[]
        {
            new("primary_collider_key", "colliderref", false, true),
            new("move_speed", "float", false, true),
            new("acceleration", "float", false, true),
            new("deceleration", "float", false, true),
            new("air_control", "float", false, true),
            new("gravity", "float", false, true),
            new("jump_power", "float", false, true),
            new("maximum_fall_speed", "float", false, true),
            new("jump_requested", "bool", false, true),
            new("fallback_ground_y", "float", false, true),
            new("max_step_height", "float", false, true),
            new("vertical_physics", "bool", false, true),
        }),
        new("PlayerInputComponent", "Player Input", "Gameplay", new ComponentPropertyEntry[]
        {
            new("input_enabled", "bool", false, true),
            new("local_player_slot", "int", false, true),
        }),
        new("PlayerControllerComponent", "Player Controller", "Gameplay", new ComponentPropertyEntry[]
        {
            new("turn_speed_degrees", "float", false, true),
            new("dash_multiplier", "float", false, true),
            new("camera_relative", "bool", false, true),
            new("rotate_towards_movement", "bool", false, true),
        }),
        new("AudioListenerComponent", "Audio Listener", "Audio", new ComponentPropertyEntry[]
        {
            new("priority", "int", false, true),
        }),
        new("AudioSourceComponent", "Audio Source", "Audio", new ComponentPropertyEntry[]
        {
            new("clip_path", "string", false, true),
            new("loop", "bool", false, true),
            new("volume", "float", false, true),
            new("pitch", "float", false, true),
            new("play_on_start", "bool", false, true),
            new("spatial", "enum", false, true),
            new("min_distance", "float", false, true),
            new("max_distance", "float", false, true),
            new("is_playing", "bool", true, true),
        }),
        new("CameraComponent", "Camera", "Camera", new ComponentPropertyEntry[]
        {
            new("projection_mode", "enum", false, true),
            new("field_of_view_degrees", "float", false, true),
            new("orthographic_size", "float", false, true),
            new("near_clip", "float", false, true),
            new("far_clip", "float", false, true),
            new("priority", "int", false, true),
            new("viewport_enabled", "bool", false, true),
            new("viewport_rect", "vec4", false, true),
        }),
        new("FollowTargetComponent", "Follow Target", "Camera", new ComponentPropertyEntry[]
        {
            new("follow_distance", "float", false, true),
            new("follow_height", "float", false, true),
            new("follow_lag", "float", false, true),
            new("rotation_input_enabled", "bool", false, true),
            new("yield_to_motion", "bool", false, true),
            new("yaw_offset", "float", false, true),
            new("pitch_offset", "float", false, true),
        }),
        new("CameraTargetComponent", "Camera Target", "Camera", new ComponentPropertyEntry[]
        {
            new("target_offset", "vec3", false, true),
            new("look_at_offset", "vec3", false, true),
            new("priority", "int", false, true),
        }),
        new("RotatorComponent", "Rotator", "Gameplay", new ComponentPropertyEntry[]
        {
            new("axis", "vec3", false, true),
            new("degrees_per_second", "float", false, true),
        }),
        new("HealthComponent", "Health", "Gameplay", new ComponentPropertyEntry[]
        {
            new("max_health", "int", false, true),
            new("current_health", "int", false, true),
            new("invulnerable", "bool", false, true),
        }),
        new("SpawnPointComponent", "Spawn Point", "Gameplay", new ComponentPropertyEntry[]
        {
            new("spawn_id", "int", false, true),
            new("team", "int", false, true),
            new("priority", "int", false, true),
            new("debug_draw", "bool", false, true),
        }),
        new("CheckpointComponent", "Checkpoint", "Gameplay", new ComponentPropertyEntry[]
        {
            new("checkpoint_id", "int", false, true),
            new("respawn_position_offset", "vec3", false, true),
            new("respawn_rotation", "vec3", false, true),
            new("target_mask", "layermask", false, true),
            new("one_shot", "bool", false, true),
        }),
        new("GoalComponent", "Goal", "Gameplay", new ComponentPropertyEntry[]
        {
            new("goal_id", "int", false, true),
            new("target_mask", "layermask", false, true),
            new("one_shot", "bool", false, true),
            new("completion_event", "string", false, true),
        }),
        new("KillVolumeComponent", "Kill Volume", "Gameplay", new ComponentPropertyEntry[]
        {
            new("target_mask", "layermask", false, true),
            new("respawn_at_checkpoint", "bool", false, true),
            new("damage_amount", "int", false, true),
        }),
        new("JumpPadComponent", "Jump Pad", "Gameplay", new ComponentPropertyEntry[]
        {
            new("direction", "vec3", false, true),
            new("force", "float", false, true),
            new("target_mask", "layermask", false, true),
            new("one_shot", "bool", false, true),
            new("cooldown", "float", false, true),
            new("debug_draw", "bool", false, true),
        }),
        new("DamageAreaComponent", "Damage Area", "Gameplay", new ComponentPropertyEntry[]
        {
            new("damage", "int", false, true),
            new("interval", "float", false, true),
            new("target_mask", "layermask", false, true),
            new("one_shot", "bool", false, true),
        }),
        new("EditorNoteComponent", "Scene Note", "Editor", new ComponentPropertyEntry[]
        {
            new("text", "string", false, true),
            new("category", "enum", false, true),
            new("priority", "enum", false, true),
            new("completed", "bool", false, true),
            new("show_in_viewport", "bool", false, true),
            new("hide_when_completed", "bool", false, true),
            new("color", "color", false, true),
            new("text_scale", "float", false, true),
            new("offset", "vec3", false, true),
            new("mode", "enum", false, true),
            new("horizontal_align", "enum", false, true),
            new("vertical_align", "enum", false, true),
        }),
        new("ScriptComponent", "Script", "Scripting", new ComponentPropertyEntry[]
        {
            new("__script.language", "enum", false, true),
            new("__script.asset", "assetref", false, true),
            new("__script.class", "string", false, true),
            new("__script.execution_order", "int", false, true),
            new("__script.type_id", "string", false, true),
        }),
        new("NavAgentComponent", "Nav Agent", "Navigation", new ComponentPropertyEntry[]
        {
            new("move_speed", "float", false, true),
            new("turn_speed_degrees", "float", false, true),
            new("stopping_distance", "float", false, true),
            new("path_grid_size", "float", false, true),
            new("path_max_range", "float", false, true),
            new("path_max_search_cells", "int", false, true),
        }),
        new("EnemyBehaviourComponent", "Enemy Behaviour", "Gameplay", new ComponentPropertyEntry[]
        {
            new("target", "objref", false, true),
            new("patrol_waypoints", "array", false, false),
            new("detection_range", "float", false, true),
            new("field_of_view_degrees", "float", false, true),
            new("attack_range", "float", false, true),
            new("lose_sight_delay", "float", false, true),
            new("eye_height", "float", false, true),
            new("target_height", "float", false, true),
            new("visibility_layer", "layer", false, true),
            new("visibility_mask", "layermask", false, true),
            new("attack_controls_damage_area", "bool", false, true),
            new("attack_windup_seconds", "float", false, true),
            new("attack_active_seconds", "float", false, true),
            new("attack_recovery_seconds", "float", false, true),
            new("debug_draw", "bool", false, true),
            new("current_state", "string", true, true),
            new("attack_phase", "string", true, true),
        }),
    };
}
