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
                    .Tooltip(u8"UI矩形の左下アンカー位置を指定します。").Display("アンカー最小").Step(0.001));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("anchor_max", &RectTransformComponent::anchor_max)
                    .Tooltip(u8"UI矩形の右上アンカー位置を指定します。").Display("アンカー最大").Step(0.001));
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
                    .Tooltip(u8"回転や拡縮の基準になる UI 要素内の位置を指定します。").Display("ピボット").Step(0.001));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("rotation", &RectTransformComponent::rotation)
                    .Tooltip(u8"UI 要素の回転角度を度単位で指定します。").Display("回転 (度)").Step(0.5));
            PropertyRegistry::Register<RectTransformComponent>(
                MakeProperty("scale", &RectTransformComponent::scale)
                    .Tooltip(u8"UI 要素の拡大率を指定します。").Display("拡大率").Step(0.01));
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
                    .Tooltip(u8"Canvas が基準にする画面解像度を指定します。").Display("基準解像度").Step(1.0));
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("render_mode", &CanvasComponent::render_mode)
                    .Tooltip(u8"Canvas を画面へ重ねるかワールド空間へ置くかを指定します。").Display("描画モード")
                    .AsEnum({ "Screen Space Overlay", "World Space" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("scale_mode", &CanvasComponent::scale_mode)
                    .Tooltip(u8"画面サイズに対する Canvas の拡縮方法を指定します。").Display("スケール方式")
                    .AsEnum({ "固定ピクセル", "画面サイズに合わせる" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("match_width_or_height", &CanvasComponent::match_width_or_height)
                    .Tooltip(u8"Canvas の基準解像度を幅と高さのどちらへ合わせるかを指定します。").Display("幅/高さの一致").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("sort_order", &CanvasComponent::sort_order)
                    .Tooltip(u8"同じ Canvas 内の描画順を指定します。値が大きいほど手前に描きます。").Display("描画順").Step(1.0));
            PropertyRegistry::Register<CanvasComponent>(
                MakeProperty("opacity", &CanvasComponent::opacity)
                    .Tooltip(u8"表示の不透明度を 0〜1 で指定します。").Display("不透明度").Range(0.0, 1.0).Step(0.01));

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
                    .Tooltip(u8"描画する色を指定します。").Display("色").AsColor());
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_color_2", &UIImageComponent::fill_color_2)
                    .Tooltip(u8"図形の終点側の塗り色を指定します。").Display("塗りの終端色").AsColor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_mode", &UIImageComponent::fill_mode)
                    .Tooltip(u8"図形の塗り方を単色・線形・放射から指定します。").Display("塗りの種類")
                    .AsEnum({ "単色", "線形", "放射" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_angle", &UIImageComponent::fill_angle)
                    .Tooltip(u8"グラデーションの向きを度単位で指定します。").Display("塗りの向き").Range(-360.0, 360.0).Step(1.0)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_center", &UIImageComponent::fill_center)
                    .Tooltip(u8"グラデーションの中心を正規化座標で指定します。").Display("塗りの中心").Step(0.001)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("stroke_mode", &UIImageComponent::stroke_mode)
                    .Tooltip(u8"輪郭線の色の変化方法を指定します。").Display("線の色の種類")
                    .AsEnum({ "単色", "長さに沿う" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("stroke_color_2", &UIImageComponent::stroke_color_2)
                    .Tooltip(u8"図形の輪郭線の終端色を指定します。").Display("線の終端色").AsColor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("opacity", &UIImageComponent::opacity)
                    .Tooltip(u8"表示の不透明度を 0〜1 で指定します。").Display("不透明度").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_amount", &UIImageComponent::fill_amount)
                    .Tooltip(u8"図形の塗り進み具合を 0〜1 で指定します。").Display("塗り量").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_method", &UIImageComponent::fill_method)
                    .Tooltip(u8"塗り進む方向を指定します。").Display("塗り方向")
                    .AsEnum({ "水平", "垂直", "円形 360" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("fill_reverse", &UIImageComponent::fill_reverse)
                    .Tooltip(u8"塗り進む方向を反転します。").Display("塗りを反転")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("blend_mode", &UIImageComponent::blend_mode)
                    .Tooltip(u8"画像と背景を合成する方法を指定します。").Display("ブレンド")
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
                    .Display("9 スライス").Step(1.0).Advanced());
            PropertyRegistry::Register<UIImageComponent>(
                MakeProperty("preserve_aspect", &UIImageComponent::preserve_aspect)
                    .Display("比率を維持")
                    .Tooltip("9 スライスが有効な場合は 9 スライスを優先します。"));

            ComponentRegistry::Register<UIShapeImageComponent>(
                ComponentTypeInfo::Describe("Shape Image", "UI")
                    .WithTooltip("Image を自由な Bezier 輪郭でクリップします。通常の Image とは別の専用コンポーネントです。")
                    .Requires<RectTransformComponent>()
                    .Recommends<UIImageComponent>());
            PropertyRegistry::Register<UIShapeImageComponent>(
                MakeProperty("path_closed", &UIShapeImageComponent::path_closed)
                    .Display("Path を閉じる")
                    .Tooltip("自由形状の末尾と先頭を接続します。Image のクリップには閉じた Path が必要です。"));
            PropertyRegistry::Register<UIShapeImageComponent>(
                MakeProperty("path_points", &UIShapeImageComponent::path_points)
                    .Display("Path Anchor")
                    .Tooltip("正規化 0..1 の頂点配列。UI Scene View の専用コントローラーで編集できます。"));
            PropertyRegistry::Register<UIShapeImageComponent>(
                MakeProperty("path_in_handles", &UIShapeImageComponent::path_in_handles)
                    .Tooltip(u8"自由形状の各頂点へ入る Bezier ハンドルを指定します。").Display("Path 入力 Handle")
                    .Advanced());
            PropertyRegistry::Register<UIShapeImageComponent>(
                MakeProperty("path_out_handles", &UIShapeImageComponent::path_out_handles)
                    .Tooltip(u8"自由形状の各頂点から出る Bezier ハンドルを指定します。").Display("Path 出力 Handle")
                    .Advanced());

            ComponentRegistry::Register<UISpriteAnimatorComponent>(
                ComponentTypeInfo::Describe("Sprite Animator", "UI")
                    .WithTooltip("Sprite Sheet の行列と frame から Image の UV を更新します。")
                    .Requires<RectTransformComponent>()
                    .Recommends<UIImageComponent>());
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("columns", &UISpriteAnimatorComponent::columns)
                    .Tooltip(u8"Sprite Sheet の列数を指定します。").Display("列数").Range(1.0, 256.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("rows", &UISpriteAnimatorComponent::rows)
                    .Tooltip(u8"Sprite Sheet の行数を指定します。").Display("行数").Range(1.0, 256.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("start_frame", &UISpriteAnimatorComponent::start_frame)
                    .Tooltip(u8"再生する Sprite Sheet の開始フレームを指定します。").Display("開始フレーム").Range(0.0, 65535.0).Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("end_frame", &UISpriteAnimatorComponent::end_frame)
                    .Display("終了フレーム").Range(-1.0, 65535.0).Step(1.0)
                    .Animation(Animatable::Step)
                    .Tooltip("-1 なら Sprite Sheet 全体の最後まで使います。"));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("frames_per_second",
                    &UISpriteAnimatorComponent::frames_per_second)
                    .Tooltip(u8"Sprite Sheet の再生速度を 1 秒あたりのフレーム数で指定します。").Display("FPS").Range(0.0, 240.0).Step(0.1));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("play_mode", &UISpriteAnimatorComponent::play_mode)
                    .Tooltip(u8"Sprite Sheet の再生方法を指定します。").Display("再生方式")
                    .AsEnum({ "一回", "ループ", "ピンポン", "逆再生" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("playing", &UISpriteAnimatorComponent::playing)
                    .Tooltip(u8"Sprite Animator を再生するか指定します。").Display("再生中").Animation(Animatable::Step));
            PropertyRegistry::Register<UISpriteAnimatorComponent>(
                MakeProperty("frame", &UISpriteAnimatorComponent::frame)
                    .Tooltip(u8"表示する Sprite Sheet のフレーム番号を指定します。").Display("フレーム").Range(0.0, 65535.0).Step(1.0)
                    .Animation(Animatable::Interpolatable));

            ComponentRegistry::Register<UITextComponent>(
                ComponentTypeInfo::Describe("Text", "UI")
                    .WithTooltip("文字列を 1 文字 1 クアッドで描きます。character_index は Text Animator 用に保持します。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("text", &UITextComponent::text)
                    .Tooltip(u8"表示する文字列を指定します。").Display("テキスト").Animation(Animatable::Step));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("font", &UITextComponent::font)
                    .Display("フォント").OfAssetType("Font")
                    .Animation(Animatable::Step)
                    .Tooltip(u8"Project Browser へ取り込んだ TTF / OTF / TTC を選ぶ。"
                        u8"未指定・Missing の場合は日本語対応のシステムフォントへフォールバックする。"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("font_size", &UITextComponent::font_size)
                    .Tooltip(u8"文字の大きさを指定します。").Display("文字サイズ").Range(1.0, 512.0).Step(1.0));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("color", &UITextComponent::color)
                    .Tooltip(u8"描画する色を指定します。").Display("色").AsColor());
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("opacity", &UITextComponent::opacity)
                    .Tooltip(u8"表示の不透明度を 0〜1 で指定します。").Display("不透明度").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("character_spacing", &UITextComponent::character_spacing)
                    .Tooltip(u8"文字と文字の間隔を指定します。").Display("文字間隔").Step(0.5));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("line_spacing", &UITextComponent::line_spacing)
                    .Tooltip(u8"行と行の間隔倍率を指定します。").Display("行間倍率").Range(0.1, 4.0).Step(0.01));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("horizontal_align", &UITextComponent::horizontal_align)
                    .Tooltip(u8"文字列の横方向の揃え方を指定します。").Display("横揃え")
                    .AsEnum({ "左", "中央", "右" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("vertical_align", &UITextComponent::vertical_align)
                    .Tooltip(u8"文字列の縦方向の揃え方を指定します。").Display("縦揃え")
                    .AsEnum({ "上", "中央", "下" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("word_wrap", &UITextComponent::word_wrap)
                    .Tooltip(u8"矩形幅を超えた文字列を折り返すか指定します。").Display("折り返し"));
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
                    .Tooltip(u8"表示文字列へ接続する数値の取得元を指定します。").Display("数値の接続元"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("number_source_property", &UITextComponent::number_source_property)
                    .Tooltip(u8"数値の取得元に使うプロパティ名を指定します。").Display("数値プロパティ"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("number_format", &UITextComponent::number_format)
                    .Display("数値書式")
                    .Tooltip("例: {0:0}%"));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("number_digits", &UITextComponent::number_digits)
                    .Tooltip(u8"数値の小数点以下の桁数を指定します。").Display("小数桁").Range(0.0, 4.0).Step(1.0));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("outline_width", &UITextComponent::outline_width)
                    .Tooltip(u8"文字の縁取り幅を指定します。").Display("縁取り幅").Range(0.0, 32.0).Step(0.5)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("outline_color", &UITextComponent::outline_color)
                    .Tooltip(u8"文字の縁取り色を指定します。").Display("縁取り色").AsColor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("shadow_offset", &UITextComponent::shadow_offset)
                    .Tooltip(u8"文字の影をずらす距離を指定します。").Display("影オフセット").Step(0.5)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UITextComponent>(
                MakeProperty("shadow_color", &UITextComponent::shadow_color)
                    .Tooltip(u8"文字の影色を指定します。").Display("影色").AsColor()
                    .Animation(Animatable::Interpolatable));

            ComponentRegistry::Register<UITextAnimatorComponent>(
                ComponentTypeInfo::Describe("Text Animator", "UI")
                    .WithTooltip("UIText の文字ごとに Range Selector と Transform を重ねる。")
                    .Requires<RectTransformComponent>()
                    .Recommends<UITextComponent>()
                    .AllowMultipleInstances());
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("range_start", &UITextAnimatorComponent::range_start)
                    .Tooltip(u8"アニメーション対象範囲の開始位置を指定します。").Display("範囲開始").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("range_end", &UITextAnimatorComponent::range_end)
                    .Tooltip(u8"アニメーション対象範囲の終了位置を指定します。").Display("範囲終了").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("range_offset", &UITextAnimatorComponent::range_offset)
                    .Tooltip(u8"アニメーション対象範囲をずらす量を指定します。").Display("範囲オフセット").Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("range_shape", &UITextAnimatorComponent::range_shape)
                    .Tooltip(u8"アニメーション対象範囲の形状を指定します。").Display("範囲形状")
                    .AsEnum({ "矩形", "上り", "下り", "三角", "丸", "滑らか" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("range_smoothness", &UITextAnimatorComponent::range_smoothness)
                    .Tooltip(u8"アニメーション対象範囲の境界を滑らかにする量を指定します。").Display("境界の滑らかさ").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("position_offset", &UITextAnimatorComponent::position_offset)
                    .Tooltip(u8"アニメーション中の文字位置へ加えるずれを指定します。").Display("位置オフセット").Step(0.1));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("rotation", &UITextAnimatorComponent::rotation)
                    .Tooltip(u8"UI 要素の回転角度を度単位で指定します。").Display("回転 (度)").Step(0.5));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("scale", &UITextAnimatorComponent::scale)
                    .Tooltip(u8"UI 要素の拡大率を指定します。").Display("拡大率").Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("opacity", &UITextAnimatorComponent::opacity)
                    .Tooltip(u8"表示の不透明度を 0〜1 で指定します。").Display("不透明度").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("color", &UITextAnimatorComponent::color)
                    .Tooltip(u8"描画する色を指定します。").Display("色").AsColor());
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("character_spacing", &UITextAnimatorComponent::character_spacing)
                    .Tooltip(u8"文字と文字の間隔を指定します。").Display("文字間隔").Step(0.1));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("random_seed", &UITextAnimatorComponent::random_seed)
                    .Tooltip(u8"ランダムな配置や回転を再現するための種を指定します。").Display("乱数の種").Step(1.0)
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("random_position", &UITextAnimatorComponent::random_position)
                    .Tooltip(u8"文字へ加えるランダムな位置ずれを指定します。").Display("ランダム位置").Step(0.1));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("random_rotation", &UITextAnimatorComponent::random_rotation)
                    .Tooltip(u8"文字へ加えるランダムな回転量を指定します。").Display("ランダム回転").Step(0.5));
            PropertyRegistry::Register<UITextAnimatorComponent>(
                MakeProperty("anchor", &UITextAnimatorComponent::anchor)
                    .Tooltip(u8"文字アニメーションの基準点を指定します。").Display("基準点")
                    .AsEnum({ "中央", "ベースライン左", "ベースライン中央", "左上", "下中央" })
                    .Animation(Animatable::Step));

            ComponentRegistry::Register<UIShapeComponent>(
                ComponentTypeInfo::Describe("Shape", "UI")
                    .WithTooltip("矩形・円・線・多角形を RectTransform 上に描く。Trim と Dash は Motion から動かせる。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("shape", &UIShapeComponent::shape)
                    .Tooltip(u8"図形の種類を指定します。").Display("形")
                    .AsEnum({ "矩形", "円", "線", "多角形", "ベジェ",
                        "スーパー楕円", "極座標式", "自由ベジェ Path" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_color", &UIShapeComponent::fill_color)
                    .Tooltip(u8"図形の始点側の塗り色を指定します。").Display("塗り色").AsColor());
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_color_2", &UIShapeComponent::fill_color_2)
                    .Tooltip(u8"図形の終点側の塗り色を指定します。").Display("塗りの終端色").AsColor()
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
                    .Tooltip(u8"図形の塗り方を単色・線形・放射から指定します。").Display("塗りの種類")
                    .AsEnum({ "単色", "線形", "放射" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_angle", &UIShapeComponent::fill_angle)
                    .Tooltip(u8"グラデーションの向きを度単位で指定します。").Display("塗りの向き").Range(-360.0, 360.0).Step(1.0)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("fill_center", &UIShapeComponent::fill_center)
                    .Tooltip(u8"グラデーションの中心を正規化座標で指定します。").Display("塗りの中心").Step(0.001)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("stroke_color", &UIShapeComponent::stroke_color)
                    .Tooltip(u8"図形の輪郭線色を指定します。").Display("線色").AsColor());
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("stroke_color_2", &UIShapeComponent::stroke_color_2)
                    .Tooltip(u8"図形の輪郭線の終端色を指定します。").Display("線の終端色").AsColor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("stroke_mode", &UIShapeComponent::stroke_mode)
                    .Tooltip(u8"輪郭線の色の変化方法を指定します。").Display("線の色の種類")
                    .AsEnum({ "単色", "長さに沿う" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("stroke_width", &UIShapeComponent::stroke_width)
                    .Tooltip(u8"図形の輪郭線幅を指定します。").Display("線幅").Range(0.0, 512.0).Step(0.5));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("corner_radius", &UIShapeComponent::corner_radius)
                    .Tooltip(u8"角を丸める量を指定します。").Display("角丸").Range(0.0, 512.0).Step(0.5));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("arc_curvature", &UIShapeComponent::arc_curvature)
                    .Tooltip(u8"弧の曲がり具合を指定します。").Display("弧の曲がり").Range(0.0, 1.0).Step(0.01)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("sides", &UIShapeComponent::sides)
                    .Tooltip(u8"多角形の角数を指定します。").Display("角数").Range(3.0, 64.0).Step(1.0)
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
                    .Tooltip(u8"極座標形状の回転角度を度単位で指定します。").Display("極座標の回転").Range(-360.0, 360.0).Step(0.1)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("trim_start", &UIShapeComponent::trim_start)
                    .Tooltip(u8"線形状を切り始める位置を 0〜1 で指定します。").Display("Trim 開始").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("trim_end", &UIShapeComponent::trim_end)
                    .Tooltip(u8"線形状を切り終える位置を 0〜1 で指定します。").Display("Trim 終了").Range(0.0, 1.0).Step(0.01));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("trim_offset", &UIShapeComponent::trim_offset)
                    .Tooltip(u8"線形状の切り出し位置をずらします。").Display("Trim オフセット").Step(0.01));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("dash_length", &UIShapeComponent::dash_length)
                    .Tooltip(u8"破線 1 本の長さを指定します。").Display("破線の長さ").Range(0.0, 4096.0).Step(0.5));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("dash_gap", &UIShapeComponent::dash_gap)
                    .Tooltip(u8"破線同士の間隔を指定します。").Display("破線の間隔").Range(0.0, 4096.0).Step(0.5));
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("dash_offset", &UIShapeComponent::dash_offset)
                    .Tooltip(u8"破線パターンの開始位置をずらします。").Display("破線オフセット").Step(0.5));
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
                    .Tooltip(u8"自由形状の各頂点へ入る Bezier ハンドルを指定します。").Display("Path 入力 Handle")
                    .Advanced());
            PropertyRegistry::Register<UIShapeComponent>(
                MakeProperty("path_out_handles", &UIShapeComponent::path_out_handles)
                    .Tooltip(u8"自由形状の各頂点から出る Bezier ハンドルを指定します。").Display("Path 出力 Handle")
                    .Advanced());

            ComponentRegistry::Register<UIPuppetDeformComponent>(
                ComponentTypeInfo::Describe("Puppet Deform", "UI")
                    .WithTooltip("Image を格子 Mesh に細分化し、Pin を Motion から動かして髪・腕・服を局所変形します。")
                    .Requires<RectTransformComponent>()
                    .Recommends<UIImageComponent>()
                    .InModule("RePlayEngine.BuiltIn"));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("enabled_deform", &UIPuppetDeformComponent::enabled_deform)
                    .Tooltip(u8"Puppet 変形を有効にするか指定します。").Display("変形を有効"));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("grid_columns", &UIPuppetDeformComponent::grid_columns)
                    .Tooltip(u8"Puppet メッシュの列数を指定します。").Display("Mesh 列数").Range(1.0, 32.0).Step(1.0));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("grid_rows", &UIPuppetDeformComponent::grid_rows)
                    .Tooltip(u8"Puppet メッシュの行数を指定します。").Display("Mesh 行数").Range(1.0, 32.0).Step(1.0));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("global_strength", &UIPuppetDeformComponent::global_strength)
                    .Tooltip(u8"Puppet 変形全体へ掛ける強さを指定します。").Display("全体強度").Range(-4.0, 4.0).Step(0.001)
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("pin_bind_positions", &UIPuppetDeformComponent::pin_bind_positions)
                    .Tooltip(u8"Puppet Pin の基準姿勢を保持する位置配列です。").Display("Pin 基準位置").Advanced());
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("pin_positions", &UIPuppetDeformComponent::pin_positions)
                    .Display("Pin 位置")
                    .Tooltip("配列要素を増やすと Pin が増えます。各 Pin は Dynamic Property として Motion に露出します。"));
            PropertyRegistry::Register<UIPuppetDeformComponent>(
                MakeProperty("pin_radii", &UIPuppetDeformComponent::pin_radii)
                    .Tooltip(u8"Puppet Pin の影響半径を指定します。").Display("Pin 影響半径").Advanced());

            ComponentRegistry::Register<UIButtonComponent>(
                ComponentTypeInfo::Describe("Button", "UI")
                    .WithTooltip("Hover / Pressed / Disabled の状態を持つ UI ボタンです。通知は Phase 7 で C# へ接続します。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("interactable", &UIButtonComponent::interactable)
                    .Tooltip(u8"UI 要素を入力操作の対象にするか指定します。").Display("操作可能"));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("target_image", &UIButtonComponent::target_image)
                    .Tooltip(u8"状態色や状態 Motion を適用する Image を指定します。").Display("対象 Image").Animation(Animatable::Step));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("normal_color", &UIButtonComponent::normal_color)
                    .Tooltip(u8"通常状態で使う色を指定します。").Display("通常色").AsColor());
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("hover_color", &UIButtonComponent::hover_color)
                    .Tooltip(u8"ホバー状態で使う色を指定します。").Display("ホバー色").AsColor());
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("pressed_color", &UIButtonComponent::pressed_color)
                    .Tooltip(u8"押下状態で使う色を指定します。").Display("押下色").AsColor());
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("disabled_color", &UIButtonComponent::disabled_color)
                    .Tooltip(u8"無効状態で使う色を指定します。").Display("無効色").AsColor());
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("normal_motion", &UIButtonComponent::normal_motion)
                    .Tooltip(u8"通常状態へ入ったときに再生する Motion を指定します。").Display("通常 Motion").OfAssetType("Motion")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("hover_motion", &UIButtonComponent::hover_motion)
                    .Tooltip(u8"ホバー状態へ入ったときに再生する Motion を指定します。").Display("ホバー Motion").OfAssetType("Motion")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("pressed_motion", &UIButtonComponent::pressed_motion)
                    .Tooltip(u8"押下状態へ入ったときに再生する Motion を指定します。").Display("押下 Motion").OfAssetType("Motion")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("disabled_motion", &UIButtonComponent::disabled_motion)
                    .Tooltip(u8"無効状態へ入ったときに再生する Motion を指定します。").Display("無効 Motion").OfAssetType("Motion")
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
                    .Tooltip(u8"状態切り替え時の Motion 混合時間を秒単位で指定します。").Display("状態 Blend 秒").Range(0.0, 5.0).Step(0.01));
            PropertyRegistry::Register<UIButtonComponent>(
                MakeProperty("state", &UIButtonComponent::state)
                    .Tooltip(u8"現在の選択状態を指定します。").Display("現在状態")
                    .AsEnum({ "通常", "ホバー", "押下", "無効" })
                    .ReadOnly().RuntimeOnly().NotSerializable());

            ComponentRegistry::Register<UISelectableComponent>(
                ComponentTypeInfo::Describe("Selectable", "UI")
                    .WithTooltip("Button / InputField / ScrollView 共通のフォーカスと方向ナビゲーションです。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UISelectableComponent>(
                MakeProperty("interactable", &UISelectableComponent::interactable).Tooltip(u8"UI 要素を入力操作の対象にするか指定します。").Display("操作可能"));
            PropertyRegistry::Register<UISelectableComponent>(
                MakeProperty("navigation_enabled", &UISelectableComponent::navigation_enabled).Tooltip(u8"パッドやキーで UI フォーカスを移動できるか指定します。").Display("ナビゲーション有効"));
            PropertyRegistry::Register<UISelectableComponent>(
                MakeProperty("navigation_order", &UISelectableComponent::navigation_order).Tooltip(u8"UI フォーカスの順番を指定します。").Display("Tab 順").Step(1.0));
            PropertyRegistry::Register<UISelectableComponent>(
                MakeProperty("navigation_bias", &UISelectableComponent::navigation_bias).Tooltip(u8"設定値を変更すると、この Component の「navigation_bias」へ反映します。").Display("方向探索 Bias").Range(0.0, 20.0).Step(0.1));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("navigate_up", &UISelectableComponent::navigate_up).Tooltip(u8"設定値を変更すると、この Component の「navigate_up」へ反映します。").Display("上へ"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("navigate_down", &UISelectableComponent::navigate_down).Tooltip(u8"設定値を変更すると、この Component の「navigate_down」へ反映します。").Display("下へ"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("navigate_left", &UISelectableComponent::navigate_left).Tooltip(u8"設定値を変更すると、この Component の「navigate_left」へ反映します。").Display("左へ"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("navigate_right", &UISelectableComponent::navigate_right).Tooltip(u8"設定値を変更すると、この Component の「navigate_right」へ反映します。").Display("右へ"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("override_focus_style", &UISelectableComponent::override_focus_style).Tooltip(u8"設定値を変更すると、この Component の「override_focus_style」へ反映します。").Display("Focus Style 上書き"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("focus_outline_enabled", &UISelectableComponent::focus_outline_enabled).Tooltip(u8"設定値を変更すると、この Component の「focus_outline_enabled」へ反映します。").Display("輪郭線を表示"));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("focus_outline_color", &UISelectableComponent::focus_outline_color).Tooltip(u8"設定値を変更すると、この Component の「focus_outline_color」へ反映します。").Display("輪郭線色").AsColor());
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("focus_outline_width", &UISelectableComponent::focus_outline_width).Tooltip(u8"設定値を変更すると、この Component の「focus_outline_width」へ反映します。").Display("輪郭線幅").Range(0.0, 32.0).Step(0.5));
            PropertyRegistry::Register<UISelectableComponent>(MakeProperty("focus_corner_radius", &UISelectableComponent::focus_corner_radius).Tooltip(u8"設定値を変更すると、この Component の「focus_corner_radius」へ反映します。").Display("角丸").Range(0.0, 64.0).Step(0.5));
            PropertyRegistry::Register<UISelectableComponent>(
                MakeProperty("focused", &UISelectableComponent::focused).Tooltip(u8"設定値を変更すると、この Component の「focused」へ反映します。").Display("選択中").ReadOnly().RuntimeOnly().NotSerializable());

            ComponentRegistry::Register<UIHorizontalLayoutGroupComponent>(
                ComponentTypeInfo::Describe("Horizontal Layout Group", "UI").Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIHorizontalLayoutGroupComponent>(MakeProperty("padding", &UIHorizontalLayoutGroupComponent::padding).Tooltip(u8"設定値を変更すると、この Component の「padding」へ反映します。").Display("Padding (L,T,R,B)"));
            PropertyRegistry::Register<UIHorizontalLayoutGroupComponent>(MakeProperty("spacing", &UIHorizontalLayoutGroupComponent::spacing).Tooltip(u8"設定値を変更すると、この Component の「spacing」へ反映します。").Display("間隔"));
            PropertyRegistry::Register<UIHorizontalLayoutGroupComponent>(MakeProperty("alignment", &UIHorizontalLayoutGroupComponent::alignment).Tooltip(u8"設定値を変更すると、この Component の「alignment」へ反映します。").Display("整列").AsEnum({ "開始", "中央", "終端" }));
            PropertyRegistry::Register<UIHorizontalLayoutGroupComponent>(MakeProperty("control_child_width", &UIHorizontalLayoutGroupComponent::control_child_width).Tooltip(u8"設定値を変更すると、この Component の「control_child_width」へ反映します。").Display("子の幅を伸縮"));
            PropertyRegistry::Register<UIHorizontalLayoutGroupComponent>(MakeProperty("control_child_height", &UIHorizontalLayoutGroupComponent::control_child_height).Tooltip(u8"設定値を変更すると、この Component の「control_child_height」へ反映します。").Display("子の高さを伸縮"));

            ComponentRegistry::Register<UIVerticalLayoutGroupComponent>(
                ComponentTypeInfo::Describe("Vertical Layout Group", "UI").Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIVerticalLayoutGroupComponent>(MakeProperty("padding", &UIVerticalLayoutGroupComponent::padding).Tooltip(u8"設定値を変更すると、この Component の「padding」へ反映します。").Display("Padding (L,T,R,B)"));
            PropertyRegistry::Register<UIVerticalLayoutGroupComponent>(MakeProperty("spacing", &UIVerticalLayoutGroupComponent::spacing).Tooltip(u8"設定値を変更すると、この Component の「spacing」へ反映します。").Display("間隔"));
            PropertyRegistry::Register<UIVerticalLayoutGroupComponent>(MakeProperty("alignment", &UIVerticalLayoutGroupComponent::alignment).Tooltip(u8"設定値を変更すると、この Component の「alignment」へ反映します。").Display("整列").AsEnum({ "開始", "中央", "終端" }));
            PropertyRegistry::Register<UIVerticalLayoutGroupComponent>(MakeProperty("control_child_width", &UIVerticalLayoutGroupComponent::control_child_width).Tooltip(u8"設定値を変更すると、この Component の「control_child_width」へ反映します。").Display("子の幅を伸縮"));
            PropertyRegistry::Register<UIVerticalLayoutGroupComponent>(MakeProperty("control_child_height", &UIVerticalLayoutGroupComponent::control_child_height).Tooltip(u8"設定値を変更すると、この Component の「control_child_height」へ反映します。").Display("子の高さを伸縮"));

            ComponentRegistry::Register<UIGridLayoutGroupComponent>(
                ComponentTypeInfo::Describe("Grid Layout Group", "UI").Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("padding", &UIGridLayoutGroupComponent::padding).Tooltip(u8"設定値を変更すると、この Component の「padding」へ反映します。").Display("Padding (L,T,R,B)"));
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("spacing", &UIGridLayoutGroupComponent::spacing).Tooltip(u8"設定値を変更すると、この Component の「spacing」へ反映します。").Display("間隔"));
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("cell_size", &UIGridLayoutGroupComponent::cell_size).Tooltip(u8"設定値を変更すると、この Component の「cell_size」へ反映します。").Display("セルサイズ"));
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("alignment", &UIGridLayoutGroupComponent::alignment).Tooltip(u8"設定値を変更すると、この Component の「alignment」へ反映します。").Display("整列").AsEnum({ "開始", "中央", "終端" }));
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("constraint", &UIGridLayoutGroupComponent::constraint).Tooltip(u8"設定値を変更すると、この Component の「constraint」へ反映します。").Display("制約").AsEnum({ "列数固定", "行数固定", "幅から自動" }));
            PropertyRegistry::Register<UIGridLayoutGroupComponent>(MakeProperty("constraint_count", &UIGridLayoutGroupComponent::constraint_count).Tooltip(u8"設定値を変更すると、この Component の「constraint_count」へ反映します。").Display("制約数").Range(1.0, 128.0).Step(1.0));

            ComponentRegistry::Register<UIScrollViewComponent>(
                ComponentTypeInfo::Describe("Scroll View", "UI").Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("content", &UIScrollViewComponent::content).Tooltip(u8"設定値を変更すると、この Component の「content」へ反映します。").Display("Content"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("horizontal", &UIScrollViewComponent::horizontal).Tooltip(u8"設定値を変更すると、この Component の「horizontal」へ反映します。").Display("横スクロール"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("vertical", &UIScrollViewComponent::vertical).Tooltip(u8"設定値を変更すると、この Component の「vertical」へ反映します。").Display("縦スクロール"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("clamp_when_content_fits", &UIScrollViewComponent::clamp_when_content_fits).Tooltip(u8"設定値を変更すると、この Component の「clamp_when_content_fits」へ反映します。").Display("内容が収まる時は固定"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scroll_sensitivity", &UIScrollViewComponent::scroll_sensitivity).Tooltip(u8"設定値を変更すると、この Component の「scroll_sensitivity」へ反映します。").Display("ホイール感度").Range(1.0, 512.0).Step(1.0));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scroll_offset", &UIScrollViewComponent::scroll_offset).Tooltip(u8"設定値を変更すると、この Component の「scroll_offset」へ反映します。").Display("スクロール位置"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("show_scrollbars", &UIScrollViewComponent::show_scrollbars).Tooltip(u8"設定値を変更すると、この Component の「show_scrollbars」へ反映します。").Display("スクロールバー表示"));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scrollbar_width", &UIScrollViewComponent::scrollbar_width).Tooltip(u8"設定値を変更すると、この Component の「scrollbar_width」へ反映します。").Display("バー幅").Range(2.0, 32.0).Step(0.5));
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scrollbar_track_color", &UIScrollViewComponent::scrollbar_track_color).Tooltip(u8"設定値を変更すると、この Component の「scrollbar_track_color」へ反映します。").Display("トラック色").AsColor());
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scrollbar_thumb_color", &UIScrollViewComponent::scrollbar_thumb_color).Tooltip(u8"設定値を変更すると、この Component の「scrollbar_thumb_color」へ反映します。").Display("つまみ色").AsColor());
            PropertyRegistry::Register<UIScrollViewComponent>(MakeProperty("scrollbar_corner_radius", &UIScrollViewComponent::scrollbar_corner_radius).Tooltip(u8"設定値を変更すると、この Component の「scrollbar_corner_radius」へ反映します。").Display("角丸").Range(0.0, 32.0).Step(0.5));
            PropertyRegistry::Register<UIScrollViewComponent>(
                MakeAccessorProperty<UIScrollViewComponent>("horizontal_overflow", PropertyType::Bool,
                    [](const UIScrollViewComponent& component) { return PropertyValue::MakeBool(component.horizontal_overflow); },
                    [](UIScrollViewComponent&, const PropertyValue&) {})
                .Display("横 Overflow").ReadOnly().RuntimeOnly().NotSerializable().Advanced());
            PropertyRegistry::Register<UIScrollViewComponent>(
                MakeAccessorProperty<UIScrollViewComponent>("vertical_overflow", PropertyType::Bool,
                    [](const UIScrollViewComponent& component) { return PropertyValue::MakeBool(component.vertical_overflow); },
                    [](UIScrollViewComponent&, const PropertyValue&) {})
                .Display("縦 Overflow").ReadOnly().RuntimeOnly().NotSerializable().Advanced());
            PropertyRegistry::Register<UIScrollViewComponent>(
                MakeAccessorProperty<UIScrollViewComponent>("horizontal_normalized", PropertyType::Float,
                    [](const UIScrollViewComponent& component) { return PropertyValue::MakeFloat(component.horizontal_normalized); },
                    [](UIScrollViewComponent&, const PropertyValue&) {})
                .Display("横 Normalized").ReadOnly().RuntimeOnly().NotSerializable().Advanced());
            PropertyRegistry::Register<UIScrollViewComponent>(
                MakeAccessorProperty<UIScrollViewComponent>("vertical_normalized", PropertyType::Float,
                    [](const UIScrollViewComponent& component) { return PropertyValue::MakeFloat(component.vertical_normalized); },
                    [](UIScrollViewComponent&, const PropertyValue&) {})
                .Display("縦 Normalized").ReadOnly().RuntimeOnly().NotSerializable().Advanced());

            ComponentRegistry::Register<UISliderComponent>(
                ComponentTypeInfo::Describe("Slider", "UI")
                    .WithTooltip("Pointer drag と UI navigation に対応する値入力です。")
                    .Requires<RectTransformComponent>()
                    .Recommends<UISelectableComponent>());
            PropertyRegistry::Register<UISliderComponent>(MakeProperty("minimum", &UISliderComponent::minimum).Tooltip(u8"設定値を変更すると、この Component の「minimum」へ反映します。").Display("最小値"));
            PropertyRegistry::Register<UISliderComponent>(MakeProperty("maximum", &UISliderComponent::maximum).Tooltip(u8"設定値を変更すると、この Component の「maximum」へ反映します。").Display("最大値"));
            // 生メンバーへ直接書くと範囲外の値が残るため、必ず SetValue を通す。
            PropertyRegistry::Register<UISliderComponent>(
                MakeAccessorProperty<UISliderComponent>("value", PropertyType::Float,
                    [](const UISliderComponent& c) { return PropertyValue::MakeFloat(c.value); },
                    [](UISliderComponent& c, const PropertyValue& v) { c.SetValue(v.AsFloat(c.value)); })
                .Display("値"));
            PropertyRegistry::Register<UISliderComponent>(MakeProperty("whole_numbers", &UISliderComponent::whole_numbers).Tooltip(u8"設定値を変更すると、この Component の「whole_numbers」へ反映します。").Display("整数のみ"));
            PropertyRegistry::Register<UISliderComponent>(MakeProperty("direction", &UISliderComponent::direction).Tooltip(u8"設定値を変更すると、この Component の「direction」へ反映します。").Display("方向").AsEnum({ "左から右", "右から左", "下から上", "上から下" }));
            PropertyRegistry::Register<UISliderComponent>(MakeProperty("interactable", &UISliderComponent::interactable).Tooltip(u8"UI 要素を入力操作の対象にするか指定します。").Display("操作可能"));
            PropertyRegistry::Register<UISliderComponent>(MakeProperty("fill_image", &UISliderComponent::fill_image).Tooltip(u8"設定値を変更すると、この Component の「fill_image」へ反映します。").Display("Fill Image"));
            PropertyRegistry::Register<UISliderComponent>(MakeProperty("handle_rect", &UISliderComponent::handle_rect).Tooltip(u8"設定値を変更すると、この Component の「handle_rect」へ反映します。").Display("Handle Rect"));
            PropertyRegistry::Register<UISliderComponent>(MakeProperty("keyboard_step", &UISliderComponent::keyboard_step).Tooltip(u8"設定値を変更すると、この Component の「keyboard_step」へ反映します。").Display("キー操作量").Range(0.0001, 1000000.0));
            PropertyRegistry::Register<UISliderComponent>(
                MakeAccessorProperty<UISliderComponent>("normalized_value", PropertyType::Float,
                    [](const UISliderComponent& component) { return PropertyValue::MakeFloat(component.NormalizedValue()); },
                    [](UISliderComponent&, const PropertyValue&) {})
                .Display("Normalized 値").ReadOnly().RuntimeOnly().NotSerializable().Advanced());

            ComponentRegistry::Register<UIInputFieldComponent>(
                ComponentTypeInfo::Describe("Input Field", "UI")
                    .WithTooltip("UIText を使う Runtime 文字入力。IME / 選択 / Clipboard に対応します。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("text", &UIInputFieldComponent::text).Tooltip(u8"表示する文字列を指定します。").Display("Text"));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("text_target", &UIInputFieldComponent::text_target).Tooltip(u8"設定値を変更すると、この Component の「text_target」へ反映します。").Display("Text Target"));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("placeholder", &UIInputFieldComponent::placeholder).Tooltip(u8"設定値を変更すると、この Component の「placeholder」へ反映します。").Display("Placeholder"));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("text_color", &UIInputFieldComponent::text_color).Tooltip(u8"設定値を変更すると、この Component の「text_color」へ反映します。").Display("Text Color").AsColor());
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("placeholder_color", &UIInputFieldComponent::placeholder_color).Tooltip(u8"設定値を変更すると、この Component の「placeholder_color」へ反映します。").Display("Placeholder Color").AsColor());
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("selection_color", &UIInputFieldComponent::selection_color).Tooltip(u8"設定値を変更すると、この Component の「selection_color」へ反映します。").Display("Selection Color").AsColor());
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("caret_color", &UIInputFieldComponent::caret_color).Tooltip(u8"設定値を変更すると、この Component の「caret_color」へ反映します。").Display("Caret Color").AsColor());
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("caret_width", &UIInputFieldComponent::caret_width).Tooltip(u8"設定値を変更すると、この Component の「caret_width」へ反映します。").Display("Caret Width").Range(0.5, 8.0).Step(0.1));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("caret_blink_seconds", &UIInputFieldComponent::caret_blink_seconds).Tooltip(u8"設定値を変更すると、この Component の「caret_blink_seconds」へ反映します。").Display("Caret Blink").Range(0.05, 2.0).Step(0.05));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("max_characters", &UIInputFieldComponent::max_characters).Tooltip(u8"設定値を変更すると、この Component の「max_characters」へ反映します。").Display("Max Characters").Range(0.0, 65535.0).Step(1.0));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("password", &UIInputFieldComponent::password).Tooltip(u8"設定値を変更すると、この Component の「password」へ反映します。").Display("Password"));
            PropertyRegistry::Register<UIInputFieldComponent>(MakeProperty("read_only", &UIInputFieldComponent::read_only).Tooltip(u8"設定値を変更すると、この Component の「read_only」へ反映します。").Display("Read Only"));
            PropertyRegistry::Register<UIInputFieldComponent>(
                MakeAccessorProperty<UIInputFieldComponent>("caret_index", PropertyType::Int,
                    [](const UIInputFieldComponent& component) { return PropertyValue::MakeInt(component.caret_index); },
                    [](UIInputFieldComponent&, const PropertyValue&) {})
                .Display("Caret Index").ReadOnly().RuntimeOnly().NotSerializable().Advanced());
            PropertyRegistry::Register<UIInputFieldComponent>(
                MakeAccessorProperty<UIInputFieldComponent>("selection_start", PropertyType::Int,
                    [](const UIInputFieldComponent& component) { return PropertyValue::MakeInt(component.SelectionStart()); },
                    [](UIInputFieldComponent&, const PropertyValue&) {})
                .Display("Selection Start").ReadOnly().RuntimeOnly().NotSerializable().Advanced());
            PropertyRegistry::Register<UIInputFieldComponent>(
                MakeAccessorProperty<UIInputFieldComponent>("selection_end", PropertyType::Int,
                    [](const UIInputFieldComponent& component) { return PropertyValue::MakeInt(component.SelectionEnd()); },
                    [](UIInputFieldComponent&, const PropertyValue&) {})
                .Display("Selection End").ReadOnly().RuntimeOnly().NotSerializable().Advanced());
            PropertyRegistry::Register<UIInputFieldComponent>(
                MakeAccessorProperty<UIInputFieldComponent>("ime_composing", PropertyType::Bool,
                    [](const UIInputFieldComponent& component) { return PropertyValue::MakeBool(component.ime_composing); },
                    [](UIInputFieldComponent&, const PropertyValue&) {})
                .Display("IME Composition").ReadOnly().RuntimeOnly().NotSerializable().Advanced());

            ComponentRegistry::Register<UIMaskComponent>(
                ComponentTypeInfo::Describe("Mask", "UI")
                    .WithTooltip("矩形は D3D11 scissor、画像と形状は既存 Mask Effect で子孫 UI を切り抜きます。")
                    .Requires<RectTransformComponent>());
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("enabled_mask", &UIMaskComponent::enabled_mask)
                    .Tooltip(u8"マスク処理を有効にするか指定します。").Display("マスク有効"));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("show_mask_graphic", &UIMaskComponent::show_mask_graphic)
                    .Tooltip(u8"マスク自身の Image を表示するか指定します。").Display("自身を表示"));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("mask_mode", &UIMaskComponent::mask_mode)
                    .Tooltip(u8"マスクに使うアルファ・輝度・形状の方式を指定します。").Display("マスク方式")
                    .AsEnum({ "矩形", "画像", "形状", "Object Alpha", "Object Luma" })
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("shape_kind", &UIMaskComponent::shape_kind)
                    .Tooltip(u8"図形マスクの形状を指定します。").Display("形状")
                    .AsEnum({ "矩形", "円", "多角形", "星形", "角丸矩形" })
                    .HiddenInEditor()
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("shape_sides", &UIMaskComponent::shape_sides)
                    .Tooltip(u8"多角形マスクの頂点数を指定します。").Display("頂点数").Range(3.0, 64.0).Step(1.0)
                    .HiddenInEditor()
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("shape_inner_radius", &UIMaskComponent::shape_inner_radius)
                    .Tooltip(u8"星形マスクの内側半径を指定します。").Display("星形の内側").Range(0.05, 0.95).Step(0.01)
                    .HiddenInEditor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("shape_corner_radius", &UIMaskComponent::shape_corner_radius)
                    .Tooltip(u8"角丸マスクの角の丸さを指定します。").Display("角丸量").Range(0.0, 1.0).Step(0.01)
                    .HiddenInEditor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("shape_rotation", &UIMaskComponent::shape_rotation)
                    .Tooltip(u8"図形マスクの回転角度を度単位で指定します。").Display("形状の回転").Range(-180.0, 180.0).Step(1.0)
                    .HiddenInEditor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("group_scale", &UIMaskComponent::group_scale)
                    .Tooltip(u8"図形マスクと子 Image の拡大率を指定します。").Display("図形イメージの拡大率").Step(0.01)
                    .HiddenInEditor()
                    .Animation(Animatable::Interpolatable));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("mask_image", &UIMaskComponent::mask_image)
                    .Tooltip(u8"切り抜きに使うマスク画像を指定します。").Display("マスク画像").OfAssetType("Image"));
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
                    .Tooltip(u8"マスクの内側と外側を反転します。").Display("反転").Animation(Animatable::Step));
            PropertyRegistry::Register<UIMaskComponent>(
                MakeProperty("softness", &UIMaskComponent::softness)
                    .Tooltip(u8"マスク境界をぼかす量を指定します。").Display("境界の柔らかさ").Range(0.0, 1.0).Step(0.01)
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
                MakeProperty("target", &UIButtonPropertyToggleComponent::target).Tooltip(u8"プロパティを切り替える対象 Component を指定します。").Display("Target"));
            PropertyRegistry::Register<UIButtonPropertyToggleComponent>(
                MakeProperty("target_property", &UIButtonPropertyToggleComponent::target_property).Tooltip(u8"切り替える対象 Component のプロパティ名を指定します。").Display("Property"));

            ComponentRegistry::Register<UIEffectStackComponent>(
                ComponentTypeInfo::Describe("Effect Stack", "UI")
                    .WithTooltip("UI 要素をオフスクリーンに描いて Effect を順に適用します。")
                    .Requires<RectTransformComponent>()
                    .InModule("RePlayEngine.Optional.Effects"));
            PropertyRegistry::Register<UIEffectStackComponent>(
                MakeProperty("enabled", &UIEffectStackComponent::enabled)
                    .Tooltip(u8"Component または Effect Stack を有効にするか指定します。").Display("有効").Animation(Animatable::Step));
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
                    .Tooltip(u8"Effect Preset Asset を使うか指定します。").Display("Preset を使用").Animation(Animatable::Step));
            PropertyRegistry::Register<UIEffectStackComponent>(
                MakeProperty("effect_preset", &UIEffectStackComponent::effect_preset)
                    .Tooltip(u8"適用する Effect Preset Asset を指定します。").Display("Effect Preset").OfAssetType("EffectPreset")
                    .Animation(Animatable::Step));
            PropertyRegistry::Register<UIEffectStackComponent>(
                MakeProperty("effect_count", &UIEffectStackComponent::effect_count)
                    .Tooltip(u8"Effect Stack に並べる Effect の数を指定します。").Display("Effect 数").Range(0.0, 16.0).Step(1.0)
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
