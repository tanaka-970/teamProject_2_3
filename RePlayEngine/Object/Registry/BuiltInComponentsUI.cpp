#include "BuiltInComponentsInternal.h"

namespace ReplayEngine::Core::Detail
{
        void RegisterUI()
        {
            ComponentRegistry::Register<RectTransformComponent>(
                ComponentTypeInfo::Describe("Rect Transform", "UI")
                    .WithTooltip("Canvas 上の矩形。保存値は anchor / anchored_position / size_delta / pivot だけです。"));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("anchor_min", &RectTransformComponent::anchor_min)
                    .Display("アンカー最小").Step(0.001));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("anchor_max", &RectTransformComponent::anchor_max)
                    .Display("アンカー最大").Step(0.001));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("anchored_position", &RectTransformComponent::anchored_position)
                    .Display("位置").Step(0.5)
                    .Tooltip("アンカー基準からの相対位置です。"));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("size_delta", &RectTransformComponent::size_delta)
                    .Display("サイズ差分").Step(0.5)
                    .Tooltip("アンカーが一点なら矩形サイズ、範囲なら親サイズとの差分です。"));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("pivot", &RectTransformComponent::pivot)
                    .Display("ピボット").Step(0.001));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("rotation", &RectTransformComponent::rotation)
                    .Display("回転 (度)").Step(0.5));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("scale", &RectTransformComponent::scale)
                    .Display("拡大率").Step(0.01));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("sort_order", &RectTransformComponent::sort_order)
                    .Display("描画順").Step(1.0)
                    .Animation(Animatable::Step)
                    .Tooltip("同じ Canvas 内の兄弟 UI の描画順です。値が大きいほど手前に描きます。"));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeAccessorProperty<RectTransformComponent>("resolved_rect", PropertyType::Vector4,
                    [](const RectTransformComponent& component)
                    { return PropertyValue::MakeVector4(component.ResolvedRect()); },
                    [](RectTransformComponent&, const PropertyValue&) {})
                .Display("確定矩形").ReadOnly().RuntimeOnly().NotSerializable().Advanced()
                .Tooltip("UILayout が毎フレーム計算した結果です。Scene には保存しません。"));

            ComponentRegistry::Register<CanvasComponent>(
                ComponentTypeInfo::Describe("Canvas", "UI")
                    .WithTooltip("Screen Space / World Space の UI ルートです。配下の UI を描画順にまとめます。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("reference_resolution", &CanvasComponent::reference_resolution)
                    .Display("基準解像度").Step(1.0));
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("render_mode", &CanvasComponent::render_mode)
                    .Display("描画モード")
                    .AsEnum({ "Screen Space Overlay", "World Space" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("scale_mode", &CanvasComponent::scale_mode)
                    .Display("スケール方式")
                    .AsEnum({ "固定ピクセル", "画面サイズに合わせる" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("match_width_or_height", &CanvasComponent::match_width_or_height)
                    .Display("幅/高さの一致").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("sort_order", &CanvasComponent::sort_order)
                    .Display("描画順").Step(1.0));
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("opacity", &CanvasComponent::opacity)
                    .Display("不透明度").Range(0.0, 1.0).Step(0.01));

            ComponentRegistry::Register<UIImageComponent>(
                ComponentTypeInfo::Describe("Image", "UI")
                    .WithTooltip("矩形画像を描きます。Blend は既存の描画ステートを再利用します。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("sprite", &UIImageComponent::sprite)
                    .Display("画像").OfAssetType("Image")
                    .Animation(Animatable::Step)
                    .Tooltip("AssetDatabase の Image GUID です。Atlas 未指定時に使います。"));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("atlas", &UIImageComponent::atlas)
                    .Display("Sprite Atlas").OfAssetType("SpriteAtlas")
                    .Animation(Animatable::Step)
                    .Tooltip("不均一 Sprite Atlas。指定時は Region の Image/UV/Pivot を使います。"));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("atlas_region", &UIImageComponent::atlas_region)
                    .Display("Atlas Region")
                    .Animation(Animatable::Step)
                    .Tooltip("Sprite Atlas 内の名前付き Region。Motion の Step Key で差し替えできます。"));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("color", &UIImageComponent::color)
                    .Display("色").AsColor());
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_color_2", &UIImageComponent::fill_color_2)
                    .Display("塗りの終端色").AsColor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_mode", &UIImageComponent::fill_mode)
                    .Display("塗りの種類")
                    .AsEnum({ "単色", "線形", "放射" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_angle", &UIImageComponent::fill_angle)
                    .Display("塗りの向き").Range(-360.0, 360.0).Step(1.0)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_center", &UIImageComponent::fill_center)
                    .Display("塗りの中心").Step(0.001)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("stroke_mode", &UIImageComponent::stroke_mode)
                    .Display("線の色の種類")
                    .AsEnum({ "単色", "長さに沿う" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("stroke_color_2", &UIImageComponent::stroke_color_2)
                    .Display("線の終端色").AsColor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("opacity", &UIImageComponent::opacity)
                    .Display("不透明度").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_amount", &UIImageComponent::fill_amount)
                    .Display("塗り量").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_method", &UIImageComponent::fill_method)
                    .Display("塗り方向")
                    .AsEnum({ "水平", "垂直", "円形 360" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("blend_mode", &UIImageComponent::blend_mode)
                    .Display("ブレンド")
                    .AsEnum({ "通常", "加算", "乗算", "スクリーン" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("uv_offset", &UIImageComponent::uv_offset)
                    .Display("UV オフセット").Step(0.001).Advanced()
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("Sprite Sheet のフレーム移動を Motion から駆動できます。Step Key なら1フレーム切替になります。"));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("uv_scale", &UIImageComponent::uv_scale)
                    .Display("UV スケール").Step(0.001).Advanced()
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("Sprite Sheet の1フレーム範囲を Motion から変更できます。"));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("nine_slice", &UIImageComponent::nine_slice)
                    .Display("9 スライス").Step(1.0).Advanced()
                    .Tooltip("Phase 1 では保存だけ行います。描画分割は Sprite Editor 後に接続します。"));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("preserve_aspect", &UIImageComponent::preserve_aspect)
                    .Display("比率を維持"));

            ComponentRegistry::Register<UISpriteAnimatorComponent>(
                ComponentTypeInfo::Describe("Sprite Animator", "UI")
                    .WithTooltip("Sprite Sheet の行列と frame から Image の UV を更新します。")
                    .Requires<RectTransformComponent>()
                    .Recommends<UIImageComponent>());
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("columns", &UISpriteAnimatorComponent::columns)
                    .Display("列数").Range(1.0, 256.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("rows", &UISpriteAnimatorComponent::rows)
                    .Display("行数").Range(1.0, 256.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("start_frame", &UISpriteAnimatorComponent::start_frame)
                    .Display("開始フレーム").Range(0.0, 65535.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("end_frame", &UISpriteAnimatorComponent::end_frame)
                    .Display("終了フレーム").Range(-1.0, 65535.0).Step(1.0)
                    .Animation(Animatable::Step)
                    .Tooltip("-1 なら Sprite Sheet 全体の最後まで使います。"));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("frames_per_second",
                    &UISpriteAnimatorComponent::frames_per_second)
                    .Display("FPS").Range(0.0, 240.0).Step(0.1));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("play_mode", &UISpriteAnimatorComponent::play_mode)
                    .Display("再生方式")
                    .AsEnum({ "一回", "ループ", "ピンポン", "逆再生" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("playing", &UISpriteAnimatorComponent::playing)
                    .Display("再生中").Animation(Animatable::Step));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("frame", &UISpriteAnimatorComponent::frame)
                    .Display("フレーム").Range(0.0, 65535.0).Step(1.0)
                    .Animation(Animatable::Interpolatable));

            ComponentRegistry::Register<UITextComponent>(
                ComponentTypeInfo::Describe("Text", "UI")
                    .WithTooltip("文字列を 1 文字 1 クアッドで描きます。character_index は Text Animator 用に保持します。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("text", &UITextComponent::text)
                    .Display("テキスト").Animation(Animatable::Step));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("font", &UITextComponent::font)
                    .Display("フォント").OfAssetType("Font")
                    .Animation(Animatable::Step)
                    .Tooltip(u8"Project Browser へ取り込んだ TTF / OTF / TTC を選ぶ。"
                        u8"未指定・Missing の場合は日本語対応のシステムフォントへフォールバックする。"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("font_size", &UITextComponent::font_size)
                    .Display("文字サイズ").Range(1.0, 512.0).Step(1.0));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("color", &UITextComponent::color)
                    .Display("色").AsColor());
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("opacity", &UITextComponent::opacity)
                    .Display("不透明度").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("character_spacing", &UITextComponent::character_spacing)
                    .Display("文字間隔").Step(0.5));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("line_spacing", &UITextComponent::line_spacing)
                    .Display("行間倍率").Range(0.1, 4.0).Step(0.01));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("horizontal_align", &UITextComponent::horizontal_align)
                    .Display("横揃え")
                    .AsEnum({ "左", "中央", "右" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("vertical_align", &UITextComponent::vertical_align)
                    .Display("縦揃え")
                    .AsEnum({ "上", "中央", "下" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("word_wrap", &UITextComponent::word_wrap)
                    .Display("折り返し"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("rich_text", &UITextComponent::rich_text)
                    .Display("Rich Text")
                    .Tooltip("Unity 互換: <color>, <size>, <b>, <i>。既定OFFなので既存Textの '<' は変化しません。"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("localization_key", &UITextComponent::localization_key)
                    .Display("Localization Key")
                    .Tooltip("空なら text をそのまま表示。キーが見つからない場合も text を fallback として表示します。"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("number_source", &UITextComponent::number_source)
                    .Display("数値の接続元"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("number_source_property", &UITextComponent::number_source_property)
                    .Display("数値プロパティ"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("number_format", &UITextComponent::number_format)
                    .Display("数値書式")
                    .Tooltip("例: {0:0}%"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("number_digits", &UITextComponent::number_digits)
                    .Display("小数桁").Range(0.0, 4.0).Step(1.0));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("outline_width", &UITextComponent::outline_width)
                    .Display("縁取り幅").Range(0.0, 32.0).Step(0.5)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("outline_color", &UITextComponent::outline_color)
                    .Display("縁取り色").AsColor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("shadow_offset", &UITextComponent::shadow_offset)
                    .Display("影オフセット").Step(0.5)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("shadow_color", &UITextComponent::shadow_color)
                    .Display("影色").AsColor()
                    .Animation(Animatable::Interpolatable));

            ComponentRegistry::Register<UITextAnimatorComponent>(
                ComponentTypeInfo::Describe("Text Animator", "UI")
                    .WithTooltip("UIText の文字ごとに Range Selector と Transform を重ねる。")
                    .Requires<RectTransformComponent>()
                    .Recommends<UITextComponent>()
                    .AllowMultipleInstances());
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("range_start", &UITextAnimatorComponent::range_start)
                    .Display("範囲開始").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("range_end", &UITextAnimatorComponent::range_end)
                    .Display("範囲終了").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("range_offset", &UITextAnimatorComponent::range_offset)
                    .Display("範囲オフセット").Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("range_shape", &UITextAnimatorComponent::range_shape)
                    .Display("範囲形状")
                    .AsEnum({ "矩形", "上り", "下り", "三角", "丸", "滑らか" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("range_smoothness", &UITextAnimatorComponent::range_smoothness)
                    .Display("境界の滑らかさ").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("position_offset", &UITextAnimatorComponent::position_offset)
                    .Display("位置オフセット").Step(0.1));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("rotation", &UITextAnimatorComponent::rotation)
                    .Display("回転 (度)").Step(0.5));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("scale", &UITextAnimatorComponent::scale)
                    .Display("拡大率").Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("opacity", &UITextAnimatorComponent::opacity)
                    .Display("不透明度").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("color", &UITextAnimatorComponent::color)
                    .Display("色").AsColor());
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("character_spacing", &UITextAnimatorComponent::character_spacing)
                    .Display("文字間隔").Step(0.1));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("random_seed", &UITextAnimatorComponent::random_seed)
                    .Display("乱数の種").Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("random_position", &UITextAnimatorComponent::random_position)
                    .Display("ランダム位置").Step(0.1));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("random_rotation", &UITextAnimatorComponent::random_rotation)
                    .Display("ランダム回転").Step(0.5));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("anchor", &UITextAnimatorComponent::anchor)
                    .Display("基準点")
                    .AsEnum({ "中央", "ベースライン左", "ベースライン中央", "左上", "下中央" })
                    .Animation(Animatable::Step));

            ComponentRegistry::Register<UIShapeComponent>(
                ComponentTypeInfo::Describe("Shape", "UI")
                    .WithTooltip("矩形・円・線・多角形を RectTransform 上に描く。Trim と Dash は Motion から動かせる。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("shape", &UIShapeComponent::shape)
                    .Display("形")
                    .AsEnum({ "矩形", "円", "線", "多角形", "ベジェ",
                        "スーパー楕円", "極座標式", "自由ベジェ Path" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_color", &UIShapeComponent::fill_color)
                    .Display("塗り色").AsColor());
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_color_2", &UIShapeComponent::fill_color_2)
                    .Display("塗りの終端色").AsColor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_color_3", &UIShapeComponent::fill_color_3)
                    .Display("塗り色 3").AsColor()
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("色 3 の位置が 0 以上のときだけ使う。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_color_4", &UIShapeComponent::fill_color_4)
                    .Display("塗り色 4").AsColor()
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("色 4 の位置が 0 以上のときだけ使う。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_stop_2", &UIShapeComponent::fill_stop_2)
                    .Display("色 2 の位置").Range(0.0, 1.0).Step(0.001)
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("既存の 2 色グラデーションでは 1 のまま使う。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_stop_3", &UIShapeComponent::fill_stop_3)
                    .Display("色 3 の位置").Range(-1.0, 1.0).Step(0.001)
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("-1 は未設定。0 以上にすると色 3 を有効にする。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_stop_4", &UIShapeComponent::fill_stop_4)
                    .Display("色 4 の位置").Range(-1.0, 1.0).Step(0.001)
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("-1 は未設定。0 以上にすると色 4 を有効にする。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_mode", &UIShapeComponent::fill_mode)
                    .Display("塗りの種類")
                    .AsEnum({ "単色", "線形", "放射" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_angle", &UIShapeComponent::fill_angle)
                    .Display("塗りの向き").Range(-360.0, 360.0).Step(1.0)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_center", &UIShapeComponent::fill_center)
                    .Display("塗りの中心").Step(0.001)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("stroke_color", &UIShapeComponent::stroke_color)
                    .Display("線色").AsColor());
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("stroke_color_2", &UIShapeComponent::stroke_color_2)
                    .Display("線の終端色").AsColor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("stroke_mode", &UIShapeComponent::stroke_mode)
                    .Display("線の色の種類")
                    .AsEnum({ "単色", "長さに沿う" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("stroke_width", &UIShapeComponent::stroke_width)
                    .Display("線幅").Range(0.0, 512.0).Step(0.5));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("corner_radius", &UIShapeComponent::corner_radius)
                    .Display("角丸").Range(0.0, 512.0).Step(0.5));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("arc_curvature", &UIShapeComponent::arc_curvature)
                    .Display("弧の曲がり").Range(0.0, 1.0).Step(0.01)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("sides", &UIShapeComponent::sides)
                    .Display("角数").Range(3.0, 64.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("superellipse_exponent",
                    &UIShapeComponent::superellipse_exponent)
                    .Display("スーパー楕円の指数").Range(0.25, 16.0).Step(0.01)
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("2 で楕円。大きいほど四角、小さいほど星形へ連続変形する。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("polar_base_radius", &UIShapeComponent::polar_base_radius)
                    .Display("極座標の基本半径").Range(0.05, 1.5).Step(0.001)
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("r(θ) = a + b cos(kθ) の a。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("polar_amplitude", &UIShapeComponent::polar_amplitude)
                    .Display("極座標の振幅").Range(-1.0, 1.0).Step(0.001)
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("r(θ) = a + b cos(kθ) の b。0 なら円。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("polar_lobes", &UIShapeComponent::polar_lobes)
                    .Display("花弁 / 歯数").Range(1.0, 32.0).Step(0.01)
                    .Animation(Animatable::Interpolatable)
                    .Tooltip("r(θ) = a + b cos(kθ) の k。Motion で連続変形できる。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("polar_rotation", &UIShapeComponent::polar_rotation)
                    .Display("極座標の回転").Range(-360.0, 360.0).Step(0.1)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("trim_start", &UIShapeComponent::trim_start)
                    .Display("Trim 開始").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("trim_end", &UIShapeComponent::trim_end)
                    .Display("Trim 終了").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("trim_offset", &UIShapeComponent::trim_offset)
                    .Display("Trim オフセット").Step(0.01));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("dash_length", &UIShapeComponent::dash_length)
                    .Display("破線の長さ").Range(0.0, 4096.0).Step(0.5));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("dash_gap", &UIShapeComponent::dash_gap)
                    .Display("破線の間隔").Range(0.0, 4096.0).Step(0.5));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("dash_offset", &UIShapeComponent::dash_offset)
                    .Display("破線オフセット").Step(0.5));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("path_closed", &UIShapeComponent::path_closed)
                    .Display("Path を閉じる")
                    .Tooltip("自由ベジェ Path の末尾と先頭を接続します。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("path_points", &UIShapeComponent::path_points)
                    .Display("Path Anchor")
                    .Tooltip("正規化 0..1 の Anchor 配列。配列追加/削除は既存 Inspector を使います。"));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("path_in_handles", &UIShapeComponent::path_in_handles)
                    .Display("Path 入力 Handle")
                    .Advanced());
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("path_out_handles", &UIShapeComponent::path_out_handles)
                    .Display("Path 出力 Handle")
                    .Advanced());

            ComponentRegistry::Register<UIPuppetDeformComponent>(
                ComponentTypeInfo::Describe("Puppet Deform", "UI")
                    .WithTooltip("Image を格子 Mesh に細分化し、Pin を Motion から動かして髪・腕・服を局所変形します。")
                    .Requires<RectTransformComponent>()
                    .Recommends<UIImageComponent>()
                    .InModule("RePlayEngine.BuiltIn"));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("enabled_deform", &UIPuppetDeformComponent::enabled_deform)
                    .Display("変形を有効"));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("grid_columns", &UIPuppetDeformComponent::grid_columns)
                    .Display("Mesh 列数").Range(1.0, 32.0).Step(1.0));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("grid_rows", &UIPuppetDeformComponent::grid_rows)
                    .Display("Mesh 行数").Range(1.0, 32.0).Step(1.0));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("global_strength", &UIPuppetDeformComponent::global_strength)
                    .Display("全体強度").Range(-4.0, 4.0).Step(0.001)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("pin_bind_positions", &UIPuppetDeformComponent::pin_bind_positions)
                    .Display("Pin 基準位置").Advanced());
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("pin_positions", &UIPuppetDeformComponent::pin_positions)
                    .Display("Pin 位置")
                    .Tooltip("配列要素を増やすと Pin が増えます。各 Pin は Dynamic Property として Motion に露出します。"));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("pin_radii", &UIPuppetDeformComponent::pin_radii)
                    .Display("Pin 影響半径").Advanced());

            ComponentRegistry::Register<UIButtonComponent>(
                ComponentTypeInfo::Describe("Button", "UI")
                    .WithTooltip("Hover / Pressed / Disabled の状態を持つ UI ボタンです。通知は Phase 7 で C# へ接続します。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("interactable", &UIButtonComponent::interactable)
                    .Display("操作可能"));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("target_image", &UIButtonComponent::target_image)
                    .Display("対象 Image").Animation(Animatable::Step));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("normal_color", &UIButtonComponent::normal_color)
                    .Display("通常色").AsColor());
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("hover_color", &UIButtonComponent::hover_color)
                    .Display("ホバー色").AsColor());
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("pressed_color", &UIButtonComponent::pressed_color)
                    .Display("押下色").AsColor());
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("disabled_color", &UIButtonComponent::disabled_color)
                    .Display("無効色").AsColor());
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("normal_motion", &UIButtonComponent::normal_motion)
                    .Display("通常 Motion").OfAssetType("Motion")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("hover_motion", &UIButtonComponent::hover_motion)
                    .Display("ホバー Motion").OfAssetType("Motion")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("pressed_motion", &UIButtonComponent::pressed_motion)
                    .Display("押下 Motion").OfAssetType("Motion")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("disabled_motion", &UIButtonComponent::disabled_motion)
                    .Display("無効 Motion").OfAssetType("Motion")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("navigation_enabled", &UIButtonComponent::navigation_enabled)
                    .Display("パッド/キーでフォーカス")
                    .Tooltip("有効なら十字キー/方向キーの UI フォーカス対象になる。"));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("navigation_order", &UIButtonComponent::navigation_order)
                    .Display("フォーカス順").Step(1.0)
                    .Tooltip("小さい順にフォーカスを移動する。同値なら Hierarchy 順。"));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("state_blend_seconds", &UIButtonComponent::state_blend_seconds)
                    .Display("状態 Blend 秒").Range(0.0, 5.0).Step(0.01));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("state", &UIButtonComponent::state)
                    .Display("現在状態")
                    .AsEnum({ "通常", "ホバー", "押下", "無効" })
                    .ReadOnly().RuntimeOnly().NotSerializable());

            ComponentRegistry::Register<UISelectableComponent>(
                ComponentTypeInfo::Describe("Selectable", "UI")
                    .WithTooltip("Button / InputField / ScrollView 共通のフォーカスと方向ナビゲーションです。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UISelectableComponent>(
                MakeProperty("interactable", &UISelectableComponent::interactable).Display("操作可能"));
            PropertyRegistry::Register<UISelectableComponent>(
                MakeProperty("navigation_enabled", &UISelectableComponent::navigation_enabled).Display("ナビゲーション有効"));
            PropertyRegistry::Register<UISelectableComponent>(
                MakeProperty("navigation_order", &UISelectableComponent::navigation_order).Display("Tab 順").Step(1.0));
            PropertyRegistry::Register<UISelectableComponent>(
                MakeProperty("navigation_bias", &UISelectableComponent::navigation_bias).Display("方向探索 Bias").Range(0.0, 20.0).Step(0.1));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("navigate_up", &UISelectableComponent::navigate_up).Display("上へ"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("navigate_down", &UISelectableComponent::navigate_down).Display("下へ"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("navigate_left", &UISelectableComponent::navigate_left).Display("左へ"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("navigate_right", &UISelectableComponent::navigate_right).Display("右へ"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("override_focus_style", &UISelectableComponent::override_focus_style).Display("Focus Style 上書き"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("focus_outline_enabled", &UISelectableComponent::focus_outline_enabled).Display("輪郭線を表示"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("focus_outline_color", &UISelectableComponent::focus_outline_color).Display("輪郭線色").AsColor());
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("focus_outline_width", &UISelectableComponent::focus_outline_width).Display("輪郭線幅").Range(0.0, 32.0).Step(0.5));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("focus_corner_radius", &UISelectableComponent::focus_corner_radius).Display("角丸").Range(0.0, 64.0).Step(0.5));
            PropertyRegistry::Register<UISelectableComponent>(
                MakeProperty("focused", &UISelectableComponent::focused).Display("選択中").ReadOnly().RuntimeOnly().NotSerializable());

            ComponentRegistry::Register<UIHorizontalLayoutGroupComponent>(
                ComponentTypeInfo::Describe("Horizontal Layout Group", "UI").Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIHorizontalLayoutGroupComponent>(MakeProperty("padding", &UIHorizontalLayoutGroupComponent::padding).Display("Padding (L,T,R,B)"));
            PropertyRegistry::Register<UIHorizontalLayoutGroupComponent>(MakeProperty("spacing", &UIHorizontalLayoutGroupComponent::spacing).Display("間隔"));
            PropertyRegistry::Register<UIHorizontalLayoutGroupComponent>(MakeProperty("alignment", &UIHorizontalLayoutGroupComponent::alignment).Display("整列").AsEnum({ "開始", "中央", "終端" }));
            PropertyRegistry::Register<UIHorizontalLayoutGroupComponent>(MakeProperty("control_child_width", &UIHorizontalLayoutGroupComponent::control_child_width).Display("子の幅を伸縮"));
            PropertyRegistry::Register<UIHorizontalLayoutGroupComponent>(MakeProperty("control_child_height", &UIHorizontalLayoutGroupComponent::control_child_height).Display("子の高さを伸縮"));

            ComponentRegistry::Register<UIVerticalLayoutGroupComponent>(
                ComponentTypeInfo::Describe("Vertical Layout Group", "UI").Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIVerticalLayoutGroupComponent>(MakeProperty("padding", &UIVerticalLayoutGroupComponent::padding).Display("Padding (L,T,R,B)"));
            PropertyRegistry::Register<UIVerticalLayoutGroupComponent>(MakeProperty("spacing", &UIVerticalLayoutGroupComponent::spacing).Display("間隔"));
            PropertyRegistry::Register<UIVerticalLayoutGroupComponent>(MakeProperty("alignment", &UIVerticalLayoutGroupComponent::alignment).Display("整列").AsEnum({ "開始", "中央", "終端" }));
            PropertyRegistry::Register<UIVerticalLayoutGroupComponent>(MakeProperty("control_child_width", &UIVerticalLayoutGroupComponent::control_child_width).Display("子の幅を伸縮"));
            PropertyRegistry::Register<UIVerticalLayoutGroupComponent>(MakeProperty("control_child_height", &UIVerticalLayoutGroupComponent::control_child_height).Display("子の高さを伸縮"));

            ComponentRegistry::Register<UIGridLayoutGroupComponent>(
                ComponentTypeInfo::Describe("Grid Layout Group", "UI").Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("padding", &UIGridLayoutGroupComponent::padding).Display("Padding (L,T,R,B)"));
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("spacing", &UIGridLayoutGroupComponent::spacing).Display("間隔"));
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("cell_size", &UIGridLayoutGroupComponent::cell_size).Display("セルサイズ"));
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("alignment", &UIGridLayoutGroupComponent::alignment).Display("整列").AsEnum({ "開始", "中央", "終端" }));
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("constraint", &UIGridLayoutGroupComponent::constraint).Display("制約").AsEnum({ "列数固定", "行数固定", "幅から自動" }));
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("constraint_count", &UIGridLayoutGroupComponent::constraint_count).Display("制約数").Range(1.0, 128.0).Step(1.0));

            ComponentRegistry::Register<UIScrollViewComponent>(
                ComponentTypeInfo::Describe("Scroll View", "UI").Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("content", &UIScrollViewComponent::content).Display("Content"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("horizontal", &UIScrollViewComponent::horizontal).Display("横スクロール"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("vertical", &UIScrollViewComponent::vertical).Display("縦スクロール"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("clamp_when_content_fits", &UIScrollViewComponent::clamp_when_content_fits).Display("内容が収まる時は固定"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scroll_sensitivity", &UIScrollViewComponent::scroll_sensitivity).Display("ホイール感度").Range(1.0, 512.0).Step(1.0));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scroll_offset", &UIScrollViewComponent::scroll_offset).Display("スクロール位置"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("show_scrollbars", &UIScrollViewComponent::show_scrollbars).Display("スクロールバー表示"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scrollbar_width", &UIScrollViewComponent::scrollbar_width).Display("バー幅").Range(2.0, 32.0).Step(0.5));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scrollbar_track_color", &UIScrollViewComponent::scrollbar_track_color).Display("トラック色").AsColor());
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scrollbar_thumb_color", &UIScrollViewComponent::scrollbar_thumb_color).Display("つまみ色").AsColor());
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scrollbar_corner_radius", &UIScrollViewComponent::scrollbar_corner_radius).Display("角丸").Range(0.0, 32.0).Step(0.5));

            ComponentRegistry::Register<UIInputFieldComponent>(
                ComponentTypeInfo::Describe("Input Field", "UI")
                    .WithTooltip("UIText を使う Runtime 文字入力。IME / 選択 / Clipboard に対応します。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("text", &UIInputFieldComponent::text).Display("Text"));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("text_target", &UIInputFieldComponent::text_target).Display("Text Target"));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("placeholder", &UIInputFieldComponent::placeholder).Display("Placeholder"));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("text_color", &UIInputFieldComponent::text_color).Display("Text Color").AsColor());
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("placeholder_color", &UIInputFieldComponent::placeholder_color).Display("Placeholder Color").AsColor());
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("selection_color", &UIInputFieldComponent::selection_color).Display("Selection Color").AsColor());
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("caret_color", &UIInputFieldComponent::caret_color).Display("Caret Color").AsColor());
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("caret_width", &UIInputFieldComponent::caret_width).Display("Caret Width").Range(0.5, 8.0).Step(0.1));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("caret_blink_seconds", &UIInputFieldComponent::caret_blink_seconds).Display("Caret Blink").Range(0.05, 2.0).Step(0.05));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("max_characters", &UIInputFieldComponent::max_characters).Display("Max Characters").Range(0.0, 65535.0).Step(1.0));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("password", &UIInputFieldComponent::password).Display("Password"));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("read_only", &UIInputFieldComponent::read_only).Display("Read Only"));

            ComponentRegistry::Register<UIMaskComponent>(
                ComponentTypeInfo::Describe("Mask", "UI")
                    .WithTooltip("矩形は D3D11 scissor、画像と形状は既存 Mask Effect で子孫 UI を切り抜きます。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("enabled_mask", &UIMaskComponent::enabled_mask)
                    .Display("マスク有効"));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("show_mask_graphic", &UIMaskComponent::show_mask_graphic)
                    .Display("自身を表示"));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("mask_mode", &UIMaskComponent::mask_mode)
                    .Display("マスク方式")
                    .AsEnum({ "矩形", "画像", "形状", "Object Alpha", "Object Luma" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("shape_kind", &UIMaskComponent::shape_kind)
                    .Display("形状")
                    .AsEnum({ "矩形", "円", "多角形", "星形", "角丸矩形" })
                    .HiddenInEditor()
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("shape_sides", &UIMaskComponent::shape_sides)
                    .Display("頂点数").Range(3.0, 64.0).Step(1.0)
                    .HiddenInEditor()
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("shape_inner_radius", &UIMaskComponent::shape_inner_radius)
                    .Display("星形の内側").Range(0.05, 0.95).Step(0.01)
                    .HiddenInEditor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("shape_corner_radius", &UIMaskComponent::shape_corner_radius)
                    .Display("角丸量").Range(0.0, 1.0).Step(0.01)
                    .HiddenInEditor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("shape_rotation", &UIMaskComponent::shape_rotation)
                    .Display("形状の回転").Range(-180.0, 180.0).Step(1.0)
                    .HiddenInEditor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("group_scale", &UIMaskComponent::group_scale)
                    .Display("図形イメージの拡大率").Step(0.01)
                    .HiddenInEditor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("mask_image", &UIMaskComponent::mask_image)
                    .Display("マスク画像").OfAssetType("Image"));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("mask_object", &UIMaskComponent::mask_object)
                    .Display("Track Matte Object")
                    .Tooltip("Object Alpha/Luma で最初の Matte に使う GameObject。"));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("matte_objects", &UIMaskComponent::matte_objects)
                    .Display("追加 Track Matte")
                    .Tooltip("2個目以降の Matte。matte_operations と同じ順番で使います。"));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("matte_operations", &UIMaskComponent::matte_operations)
                    .Display("Matte 演算")
                    .HiddenInEditor()
                    .Tooltip("保存用配列。Inspectorでは各 Track Matte の Add/Subtract/Intersect として表示します。"));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("invert", &UIMaskComponent::invert)
                    .Display("反転").Animation(Animatable::Step));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("softness", &UIMaskComponent::softness)
                    .Display("境界の柔らかさ").Range(0.0, 1.0).Step(0.01)
                    .Animation(Animatable::Interpolatable));

            ComponentRegistry::Register<UILanguageSwitchComponent>(
                ComponentTypeInfo::Describe("Language Switch", "UI")
                    .WithTooltip("同じ GameObject の Button が release されたとき、Runtime の表示言語を切り替えます。")
                    .Recommends<UIButtonComponent>());
            PropertyRegistry::Register<UILanguageSwitchComponent>(
                MakeProperty("language", &UILanguageSwitchComponent::language)
                    .Display("言語コード")
                    .Tooltip("例: ja / en。Project Settings の Localization table にある言語を指定します。"));

            ComponentRegistry::Register<UIButtonPropertyToggleComponent>(
                ComponentTypeInfo::Describe("Button Property Toggle", "UI")
                    .WithTooltip("同じ GameObject の Button release で、対象 Component の bool Property を反転します。")
                    .Recommends<UIButtonComponent>());
            PropertyRegistry::Register<UIButtonPropertyToggleComponent>(
                MakeProperty("target", &UIButtonPropertyToggleComponent::target).Display("Target"));
            PropertyRegistry::Register<UIButtonPropertyToggleComponent>(
                MakeProperty("target_property", &UIButtonPropertyToggleComponent::target_property).Display("Property"));

            ComponentRegistry::Register<UIEffectStackComponent>(
                ComponentTypeInfo::Describe("Effect Stack", "UI")
                    .WithTooltip("UI 要素をオフスクリーンに描いて Effect を順に適用します。")
                    .Requires<RectTransformComponent>()
                    .InModule("RePlayEngine.Optional.Effects"));
            PropertyRegistry::Register<UIEffectStackComponent>(
                MakeProperty("enabled", &UIEffectStackComponent::enabled)
                    .Display("有効").Animation(Animatable::Step));
            PropertyRegistry::Register<UIEffectStackComponent>(
                MakeProperty("target_scope", &UIEffectStackComponent::target_scope)
                    .Display("適用対象")
                    .AsEnum({ "自分だけ", "子階層をまとめる (Precompose)" })
                    .Animation(Animatable::Step)
                    .Tooltip("Subtree は親以下を一度 RT に合成して Effect を 1 回だけ適用します。"));
            PropertyRegistry::Register<UIEffectStackComponent>(
                MakeProperty("capture_backdrop", &UIEffectStackComponent::capture_backdrop)
                    .Display("背景を取り込む").Animation(Animatable::Step)
                    .Tooltip("この要素より前に描かれた画素を Effect の入力へ含めます。"
                        "Screen Space Overlay の軸揃え Image / Text だけで使えます。"));
            PropertyRegistry::Register<UIEffectStackComponent>(
                MakeProperty("use_preset", &UIEffectStackComponent::use_preset)
                    .Display("Preset を使用").Animation(Animatable::Step));
            PropertyRegistry::Register<UIEffectStackComponent>(
                MakeProperty("effect_preset", &UIEffectStackComponent::effect_preset)
                    .Display("Effect Preset").OfAssetType("EffectPreset")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIEffectStackComponent>(
                MakeProperty("effect_count", &UIEffectStackComponent::effect_count)
                    .Display("Effect 数").Range(0.0, 16.0).Step(1.0)
                    .Animation(Animatable::Step));

            // ---- 拡張点: UI Effect / Layout Group / Animation ---------------
            //
            // ・Layout Group は RectTransform の保存値を書き換えず、UILayout の一時値だけを変更する。
            // ・Effect のはみ出し量は UIEffect::ExpandBounds() を stack 全体で累積する。
            // ・Motion は static property と DynamicProperties() の両方を解決する。
            //
            // 【壊してはいけない前提】
            //   ・UIText は 1 文字 1 クアッドで character_index を持つ
            //   ・Blend は framework の共有 BLEND_STATE を使う
            //   ・矩形 Mask は既存 scissor、画像/形状 Mask は既存 Effect Stack の RT を使う
        }
}
