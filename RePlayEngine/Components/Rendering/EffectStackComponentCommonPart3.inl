// Effect Stack 共通実装のうち、Effect 種別ごとの DynamicProperties 定義中盤を保持する。
// 分割内部専用。EffectStackComponentCommon.inl からだけ include する。

            //   種類ごとの既定値は 41 種すべての挙動に影響するため、ここでは作らない。
            // 【入れるときにここへ足す】
            //   既存 UIEffect のスロットへ意味を割り当て、必要な項目だけを表示する。
            // 【壊してはいけない前提】
            //   UIEffect の保存形式と UIRenderer の定数パッキングは共有なので変更しない。
            switch (static_cast<UI::UIEffectKind>(effects[index].kind))
            {
            case UI::UIEffectKind::Blur:
                add_float("radius", "ぼかし半径", "ぼかしを広げる距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("intensity", "適用量", "元画像からぼかし画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Glow:
                add_float("radius", "光の半径", "発光を広げる距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("intensity", "光の強さ", "加算する発光の強さ。",
                    0.0, 32.0, 0.01);
                add_float("threshold", "発光しきい値",
                    "この明るさ以上の部分から光を作る。0 なら色を問わず光る。",
                    0.0, 1.0, 0.01);
                add_color("光の色", "発光へ掛ける色と不透明度。");
                break;
            case UI::UIEffectKind::ColorAdjust:
                add_float("radius", "彩度", "1 が元の彩度、0 でグレースケール。",
                    0.0, 8.0, 0.01);
                add_float("intensity", "コントラスト", "1 が元のコントラスト。",
                    0.0, 8.0, 0.01);
                add_float("amount", "明るさ", "RGB へ加える量。0 が中立。",
                    -1.0, 1.0, 0.01);
                add_float("angle", "色相", "色相を回す角度（度）。",
                    -360.0, 360.0, 0.1);
                add_color("色の乗算", "色調整の最後に RGB へ掛ける色。");
                break;
            case UI::UIEffectKind::Noise:
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("ノイズのテクスチャ")
                    .Tooltip("設定した場合は手続き的な粒の代わりにこの模様を使う。")
                    .OfAssetType("Image"));
                add_float("radius", "テクスチャの大きさ",
                    "ノイズテクスチャの繰り返し単位（ピクセル）。", 1.0, 1024.0, 0.1);
                add_float("angle", "テクスチャの向き",
                    "ノイズテクスチャを回す角度（度）。", -360.0, 360.0, 0.1);
                add_float("intensity", "ノイズ量", "加える粒状ノイズの強さ。",
                    0.0, 2.0, 0.01);
                add_float("amount", "粒の大きさ", "同じノイズ値を共有するセルの大きさ。",
                    1.0, 256.0, 0.1);
                add_float("speed", "流れる速度", "時間に対するノイズの変化速度。",
                    -32.0, 32.0, 0.01);
                add_seed();
                add_color("ノイズの色", "ノイズへ掛ける RGB。");
                break;
            case UI::UIEffectKind::Shake:
                add_float("amount", "揺れ幅", "位置をずらす最大距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("intensity", "強度", "揺れ幅へ掛ける倍率。",
                    0.0, 32.0, 0.01);
                add_float("speed", "揺れる速度", "時間に対する揺れの速さ。",
                    -32.0, 32.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::Mask:
                add_float("amount", "形状", "0 で矩形、1 で円。途中の値は連続補間する。",
                    0.0, 1.0, 0.01);
                add_float("angle", "回転", "マスク形状の回転角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("softness", "境界の柔らかさ", "形状または画像の境界をぼかす量。",
                    0.0, 1.0, 0.001);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable)
                    .Display("中心").Tooltip("正規化座標で指定するマスク中心。0.5, 0.5 が中央。")
                    .Step(0.01));
                add_float("seed", "横半径", "矩形または円の横半径（正規化座標）。",
                    0.0001, 1.0, 0.001);
                add_float("speed", "縦半径", "矩形または円の縦半径（正規化座標）。",
                    0.0001, 1.0, 0.001);
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("マスク画像")
                    .Tooltip("設定した場合は画像のアルファで切り抜く。")
                    .OfAssetType("Image"));
                break;
            case UI::UIEffectKind::Wipe:
                add_float("progress", "進行", "0 から 1 でワイプを進める。",
                    0.0, 1.0, 0.001);
                add_float("angle", "方向", "ワイプ境界の角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("softness", "境界の柔らかさ", "ワイプ境界をぼかす量。",
                    0.0001, 1.0, 0.001);
                break;
            case UI::UIEffectKind::Dissolve:
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("溶けかたのテクスチャ")
                    .Tooltip("設定した場合は明るい所から先に消える。")
                    .OfAssetType("Image"));
                add_float("radius", "テクスチャの大きさ",
                    "溶けかたテクスチャの繰り返し単位（ピクセル）。", 1.0, 1024.0, 0.1);
                add_float("angle", "テクスチャの向き",
                    "溶けかたテクスチャを回す角度（度）。", -360.0, 360.0, 0.1);
                add_float("progress", "進行", "0 から 1 で消失を進める。",
                    0.0, 1.0, 0.001);
                add_float("threshold", "縁の幅", "消え際に着色する帯の幅。",
                    0.0001, 1.0, 0.001);
                add_seed();
                add_color("縁の色", "消え際の帯へ使う色。");
                break;
            case UI::UIEffectKind::Distortion:
                add_float("threshold", "波の周波数", "画面内に作る波の細かさ。",
                    0.001, 128.0, 0.01);
                add_float("amount", "歪み幅", "UV をずらす距離（ピクセル）。",
                    -512.0, 512.0, 0.1);
                add_float("intensity", "強度", "歪み幅へ掛ける倍率。",
                    0.0, 32.0, 0.01);
                add_float("speed", "流れる速度", "波が時間方向へ進む速さ。",
                    -32.0, 32.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::ChromaticAberration:
                add_float("amount", "色ずれ距離", "RGB チャンネルを離す距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("intensity", "強度", "色ずれ距離へ掛ける倍率。",
                    0.0, 32.0, 0.01);
                break;
            case UI::UIEffectKind::Kuwahara:
                add_float("radius", "筆面の半径", "8 セクタを調べる広がり（ピクセル）。",
                    0.0, 128.0, 0.1);
                add_float("intensity", "適用量", "元画像から平坦化画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_float("softness", "面の硬さ",
                    "0 でぼかし寄り、1 で分散の小さい面を強く選ぶ。",
                    0.0, 1.0, 0.001);
                break;
            case UI::UIEffectKind::Halftone:
                add_float("radius", "網の間隔", "網点セルの間隔（ピクセル）。",
                    1.0, 256.0, 0.1);
                add_float("intensity", "適用量", "元画像から網点画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_float("angle", "網の角度", "RGB 各版へ加える全体回転（度）。",
                    -360.0, 360.0, 0.1);
                add_float("softness", "点の柔らかさ", "網点の縁をぼかす量。",
                    0.0, 1.0, 0.001);
                add_color("網点の色", "白なら元の色相を保つ。");
                break;
            case UI::UIEffectKind::DirectionalBlur:
                add_float("angle", "流す角度", "ぼかしを伸ばす方向（度）。",
                    -360.0, 360.0, 0.1);
                add_float("amount", "距離", "進行方向へ流す距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("intensity", "適用量", "元画像から方向ブラーへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::RadialBlur:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("放射の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("amount", "強さ", "中心から伸ばす最大距離（ピクセル）。",
                    -512.0, 512.0, 0.1);
                add_float("intensity", "適用量", "元画像から放射ブラーへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::RotationalBlur:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("回転の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("angle", "回転量", "ブラーが覆う角度（度）。",
                    -180.0, 180.0, 0.1);
                add_float("intensity", "適用量", "元画像から回転ブラーへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Vignette:
                add_float("radius", "中心の半径", "効果が始まる中心領域の半径。",
                    0.0, 1.5, 0.001);
                add_float("softness", "境界の柔らかさ", "中心から周辺へ移る幅。",
                    0.0001, 1.0, 0.001);
                add_float("intensity", "強さ", "周辺へ重ねる色の量。",
                    0.0, 1.0, 0.01);
                add_color("周辺の色", "黒で周辺減光、明色で中心を強調できる。");
                break;
            case UI::UIEffectKind::LightStreaks:
                add_float("amount", "光条の本数", "中心から伸ばす方向の数。最大 8 本。",
                    1.0, 8.0, 1.0);
                add_float("radius", "光条の長さ", "明部を伸ばす距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("angle", "基準角度", "最初の光条の角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("threshold", "明部しきい値", "光条の発生元にする明るさ。",
                    0.0, 1.0, 0.01);
                add_float("intensity", "光の強さ", "光条を加算する強さ。",
                    0.0, 16.0, 0.01);
                add_color("光条の色", "光条へ掛ける色。");
                break;
            case UI::UIEffectKind::LensDistortion:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("レンズ歪みの中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("amount", "歪み量", "正で樽型、負で糸巻き型。",
                    -2.0, 2.0, 0.001);
                add_float("intensity", "適用量", "元画像から歪み画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Posterize:
                add_float("amount", "階調数", "各 RGB チャンネルに残す段階数。",
                    2.0, 64.0, 1.0);
                add_float("intensity", "適用量", "元画像から減色画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Threshold:
                add_float("threshold", "しきい値", "白へ切り替える明るさ。",
                    0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "二値境界を連続的にする幅。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像から二値画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_color("白側の色", "しきい値以上の領域に使う色。");
                break;
            case UI::UIEffectKind::ColorRamp:
                add_color("色 1", "明度 0 に割り当てる色。");
                add_named_color("color_2", "色 2", "第 2 色。");
                add_named_color("color_3", "色 3", "第 3 色。");
                add_named_color("color_4", "色 4", "明度 1 側の第 4 色。");
                add_float("color_stop_2", "色 2 の位置", "色 2 を置く明度位置。",
                    0.0, 1.0, 0.001);
                add_float("color_stop_3", "色 3 の位置", "色 3 を置く明度位置。",
                    0.0, 1.0, 0.001);
                add_float("color_stop_4", "色 4 の位置", "色 4 を置く明度位置。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像からカラーランプへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Levels:
                add_float("threshold", "入力の黒点", "これ以下の入力を黒へ寄せる。",
                    0.0, 1.0, 0.001);
                add_float("amount", "入力の白点", "これ以上の入力を白へ寄せる。",
                    0.0, 1.0, 0.001);
                add_float("angle", "ガンマ補正", "0 が中立。正で明るく、負で暗くする。",
                    -4.0, 4.0, 0.001);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("出力の黒点 / 白点")
                    .Tooltip("補正後の出力範囲。既定は 0, 1。").Step(0.001));
                add_float("intensity", "適用量", "元画像からレベル補正へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Temperature:
                add_float("angle", "色温度", "負で寒色、正で暖色へ寄せる。",
                    -1.0, 1.0, 0.001);
                add_float("progress", "ティント", "負で緑、正でマゼンタへ寄せる。",
                    -1.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像から色温度補正へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::EdgeDetect:
                add_float("radius", "輪郭の太さ", "Sobel のサンプル間隔（ピクセル）。",
                    0.25, 16.0, 0.01);
                add_float("intensity", "輪郭の強さ", "検出した勾配へ掛ける強さ。",
                    0.0, 32.0, 0.01);
                add_color("輪郭の色", "検出した輪郭へ使う色。");
                break;
            case UI::UIEffectKind::Outline:
                add_float("radius", "線の太さ", "元画像の外へ広げる距離（ピクセル）。",
                    0.0, 64.0, 0.1);
                add_float("intensity", "線の濃さ", "輪郭線の不透明度へ掛ける量。",
                    0.0, 4.0, 0.01);
                add_color("線の色", "元画像へ重ねる輪郭線の色。");
                break;
            case UI::UIEffectKind::LongShadow:
                add_float("angle", "影の角度", "影を押し出す方向（度）。",
                    -360.0, 360.0, 0.1);
                add_float("amount", "影の長さ", "影を伸ばす距離（ピクセル）。",
                    0.0, 1024.0, 0.1);
                add_float("intensity", "影の濃さ", "影の不透明度へ掛ける量。",
                    0.0, 4.0, 0.01);
                add_color("影の色", "押し出した影へ使う色。");
                break;
            case UI::UIEffectKind::CrossHatch:
                add_float("radius", "線の間隔", "ハッチ線どうしの間隔（ピクセル）。",
                    2.0, 128.0, 0.1);
                add_float("angle", "線の角度", "基準となる斜線の角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("amount", "濃度の段数", "明度に応じて重ねる斜線方向の数。",
                    1.0, 4.0, 1.0);
                add_float("softness", "線の柔らかさ", "線の縁をぼかす量。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像から線画へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_color("線の色", "ハッチ線へ使う色。");
                break;
            case UI::UIEffectKind::BrushStroke:
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("筆跡のテクスチャ")
                    .Tooltip("通常の筆跡画像、または 4 x 4 の筆跡アトラスを指定する。")
                    .OfAssetType("Image"));
                push(MakeEffectProperty(i, "brush_atlas_enabled",
                    Reflection::PropertyType::Bool, Reflection::Animatable::Step)
                    .Display("筆跡アトラスを使う")
                    .Tooltip("有効ならテクスチャを 4 x 4 の16種類アトラスとして読む。"
                        "画像欄が空なら標準の brush_masks_atlas を使う。"
                        "無効なら従来どおり画像全体を1種類の筆跡として使う。"));
                push(MakeEffectProperty(i, "brush_instanced_renderer_enabled",
                    Reflection::PropertyType::Bool, Reflection::Animatable::Step)
                    .Display("独立ストローク描画（試験）")
                    .Tooltip("調整中のインスタンス筆描画を使う。無効なら安定済みの"
                        "アトラスフィルター経路を使う。"));
                push(MakeEffectProperty(i, "brush_pattern_mode",
                    Reflection::PropertyType::Enum, Reflection::Animatable::Step)
                    .Display("アトラスの選び方")
                    .Tooltip("単体は指定番号だけ、重み付きランダムはスタンプごとに比率で選ぶ。")
                    .AsEnum({ "単体", "重み付きランダム" }));
                push(MakeEffectProperty(i, "brush_pattern_index",
                    Reflection::PropertyType::Enum, Reflection::Animatable::Step)
                    .Display("単体の筆跡")
                    .Tooltip("単体モードで使うアトラス内の筆跡。重みがすべて0の時の予備選択でもある。")
                    .AsEnum({
                        "ドライ平刷毛（右細り）", "細い横筋", "熊手ブラシ（右細り）",
                        "乾いた角平刷毛", "曲線スウィッシュ", "絵の具多めの平刷毛",
                        "粒状スカンブル", "滑らかな楕円筆", "乾いた散点ブラシ",
                        "細いドライドラッグ", "絵の具多めの熊手筆", "短い乾いた払い",
                        "粗い散布ブラシ", "平行ストリーク", "太い扇形ブラシ",
                        "広い単独テーパーブラシ" }));
                for (int brush_index = 0; brush_index < brush_pattern_count; ++brush_index)
                {
                    push(MakeEffectProperty(i,
                        "brush_pattern_weight_" + std::to_string(brush_index),
                        Reflection::PropertyType::Float,
                        Reflection::Animatable::Interpolatable)
                        .Display(std::string("比率: ") + brush_pattern_labels[
                            static_cast<std::size_t>(brush_index)])
                        .Tooltip("重み付きランダム時の相対的な選ばれやすさ。"
                            "1 と 3 なら、おおむね 1:3 で選ばれる。")
                        .Range(0.0, 100.0).Step(0.1));
                }
                add_float("progress", "筆跡の大きさ",
                    "筆跡テクスチャを貼る単位（ピクセル）。", 1.0, 1024.0, 0.1);
                add_float("softness", "筆跡のばらつき",
                    "筆跡ごとの大きさと向きの散らばり。", 0.0, 1.0, 0.001);
                add_float("angle", "筆致の輪郭",
                    "0 で柔らかく、1 で色面と毛束の縁をくっきり出す。",
                    0.0, 1.0, 0.001);
                add_float("radius", "筆の長さ", "構造に沿って平均する長軸（ピクセル）。",
                    1.0, 128.0, 0.1);
                add_float("amount", "筆の幅", "構造をまたいで平均する短軸（ピクセル）。",
                    0.5, 64.0, 0.1);
                add_float("threshold", "向きの乱れ", "輪郭方向へ加える微小な乱れ。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像から筆致画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::Mosaic:
                add_float("radius", "セル幅", "正方セルの大きさ（ピクセル）。",
                    1.0, 512.0, 0.1);
                add_float("intensity", "適用量", "元画像からモザイクへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Crystallize:
                add_float("radius", "セル幅", "ボロノイセルの大きさ（ピクセル）。",
                    2.0, 512.0, 0.1);
                add_float("threshold", "セルの乱れ", "セル中心を格子からずらす割合。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像から結晶化画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_float("progress", "面の陰影",
                    "セル中心からの向きで付ける明暗の量。", 0.0, 1.0, 0.001);
                add_float("angle", "光の向き", "面の陰影を付ける方向（度）。",
                    -360.0, 360.0, 0.1);
                add_float("softness", "縁の輝き", "セル境界を光らせる量。",
                    0.0, 1.0, 0.001);
                add_color("縁の色", "セル境界の輝きへ使う色。");
                add_seed();
                break;
            case UI::UIEffectKind::StainedGlass:
                add_float("radius", "セル幅", "ステンドグラス片の大きさ（ピクセル）。",
                    2.0, 512.0, 0.1);
                add_float("threshold", "縁の幅", "セル境界へ描く線の太さ。",
                    0.0, 0.5, 0.001);
                add_float("softness", "縁の柔らかさ", "境界線のアンチエイリアス幅。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像からステンドグラスへ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_seed();
                add_color("縁の色", "セル境界へ使う色。");
                break;
            case UI::UIEffectKind::Twirl:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("渦の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("angle", "渦の強さ", "中心で加える回転角度（度）。",
                    -1440.0, 1440.0, 0.1);
                add_float("radius", "渦の半径", "効果が消える中心からの半径。",
                    0.001, 1.5, 0.001);
                add_float("intensity", "適用量", "元画像から渦画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Spherize:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("球面効果の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("angle", "膨らみ量", "正で膨張、負で収縮。",
                    -1.0, 1.0, 0.001);
                add_float("radius", "球面の半径", "効果が消える中心からの半径。",
                    0.001, 1.5, 0.001);
                add_float("intensity", "適用量", "元画像から球面画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Ripple:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("波紋の中心。0.5, 0.5 が中央。").Step(0.01));
                add_float("radius", "波長", "波 1 周期の長さ（ピクセル）。",
                    1.0, 512.0, 0.1);
                add_float("amount", "振幅", "UV をずらす最大距離（ピクセル）。",
                    -128.0, 128.0, 0.1);
                add_float("speed", "広がる速度", "時間に対して位相を進める速さ。",
                    -32.0, 32.0, 0.01);
                add_float("intensity", "適用量", "元画像から波紋画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::PolarCoordinates:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("極座標の中心。0.5, 0.5 が中央。").Step(0.01));
