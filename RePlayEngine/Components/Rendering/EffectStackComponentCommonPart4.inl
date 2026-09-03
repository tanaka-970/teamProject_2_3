// Effect Stack 共通実装のうち、Effect 種別ごとの DynamicProperties 定義後半を保持する。
// 分割内部専用。EffectStackComponentCommon.inl からだけ include する。

                add_float("progress", "変換量", "0 で元画像、1 で直交座標から極座標へ変換。",
                    0.0, 1.0, 0.001);
                add_float("angle", "回転", "輪へ巻く開始角度（度）。",
                    -360.0, 360.0, 0.1);
                break;
            case UI::UIEffectKind::Scanlines:
                add_float("radius", "線の間隔", "走査線の周期（ピクセル）。",
                    1.0, 128.0, 0.1);
                add_float("intensity", "線の濃さ", "走査線へ重ねる色の量。",
                    0.0, 1.0, 0.01);
                add_float("speed", "流れる速度", "走査線が縦へ動く速度。",
                    -512.0, 512.0, 0.1);
                add_float("angle", "方向", "",
                    -360.0, 360.0, 0.1);
                add_color("線の色", "走査線へ重ねる色。");
                break;
            case UI::UIEffectKind::CRT:
                add_float("progress", "画面の湾曲", "ブラウン管状の樽型歪み。",
                    0.0, 1.0, 0.001);
                add_float("radius", "走査線の間隔", "横走査線の周期（ピクセル）。",
                    1.0, 64.0, 0.1);
                add_float("intensity", "走査線の濃さ", "横線による減光量。",
                    0.0, 1.0, 0.01);
                add_float("threshold", "縁の減光", "画面周辺を暗くする量。",
                    0.0, 1.0, 0.01);
                add_float("softness", "縁の柔らかさ", "画面外周のぼかし幅。",
                    0.0001, 1.0, 0.001);
                break;
            case UI::UIEffectKind::Glitch:
                add_float("radius", "帯の高さ", "横ずれを共有する帯の高さ（ピクセル）。",
                    1.0, 256.0, 0.1);
                add_float("amount", "ずれ量", "帯を横へ動かす最大距離（ピクセル）。",
                    0.0, 512.0, 0.1);
                add_float("threshold", "発生頻度", "ずれる帯の割合。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "チャンネルずれ", "RGB を離す距離（ピクセル）。",
                    0.0, 64.0, 0.1);
                add_float("speed", "変化速度", "帯パターンが切り替わる速さ。",
                    0.0, 64.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::Dither:
                add_float("amount", "階調数", "ディザ後に残す明度段階数。",
                    2.0, 64.0, 1.0);
                add_float("radius", "行列サイズ", "Bayer 行列の大きさ。2 / 4 / 8 を使う。",
                    2.0, 8.0, 2.0);
                add_float("intensity", "適用量", "元画像からディザ画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::VHS:
                add_float("radius", "横揺れ", "走査行を横へ揺らす距離（ピクセル）。",
                    0.0, 128.0, 0.1);
                add_float("amount", "横にじみ", "過去方向へ色を引く距離（ピクセル）。",
                    0.0, 128.0, 0.1);
                add_float("threshold", "色ずれ", "RGB チャンネルを離す距離（ピクセル）。",
                    0.0, 64.0, 0.1);
                add_float("softness", "ノイズ量", "加えるテープノイズの強さ。",
                    0.0, 1.0, 0.001);
                add_float("speed", "変化速度", "揺れとノイズが流れる速さ。",
                    0.0, 64.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::Letterbox:
                add_float("radius", "画面比", "残す表示領域の横 / 縦比。",
                    0.1, 8.0, 0.001);
                add_float("softness", "帯の柔らかさ", "黒帯の内側境界をぼかす幅。",
                    0.0, 0.25, 0.0001);
                add_float("intensity", "帯の濃さ", "帯色の不透明度へ掛ける量。",
                    0.0, 1.0, 0.01);
                add_color("帯の色", "上下または左右へ置く額縁色。");
                break;
            case UI::UIEffectKind::Waveform:
                push(MakeEffectProperty(i, "waveform", Reflection::PropertyType::Enum,
                    Reflection::Animatable::Step)
                    .Display("波形")
                    .Tooltip("波の基本形。鋭さとうねりで変化を加える。")
                    .AsEnum({ "正弦波", "三角波", "のこぎり波", "パルス", "ランダム" }));
                add_float("amount", "振幅",
                    "波の高さ（ピクセル）。推奨: ゆらゆら 6、心電図 10。",
                    0.0, 256.0, 0.1);
                add_float("radius", "波長",
                    "山から山までの距離（ピクセル）。推奨: ゆらゆら 120、心電図 90。",
                    1.0, 2048.0, 0.1);
                add_float("speed", "流れる速度",
                    "波が進む速さ。0 で静止。推奨: ゆらゆら 2、心電図 6。",
                    -32.0, 32.0, 0.01);
                add_float("softness", "波の鋭さ",
                    "値が大きいほど波形の山谷を鋭くする。",
                    0.0, 1.0, 0.001);
                add_float("progress", "うねりの量",
                    "低周波のうねりを重ねる割合。",
                    0.0, 1.0, 0.001);
                add_float("intensity", "太さ変調",
                    "波の山は元の太さのまま、谷で細くする度合い。",
                    0.0, 1.0, 0.001);
                add_float("angle", "波の向き",
                    "波が進む向き（度）。0 で横。",
                    -360.0, 360.0, 0.1);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable)
                    .Display("適用範囲 (開始 / 終了)")
                    .Tooltip("波の向きに沿った適用範囲。既定の 1, -1 は全域。")
                    .Range(0.0, 1.0).Step(0.001));
                add_float("threshold", "境界のぼかし",
                    "適用範囲の端で振幅と太さ変調を 0 へ落とす幅。",
                    0.0, 0.5, 0.001);
                add_seed();
                break;
            case UI::UIEffectKind::DisplacementMap:
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("変位マップ").OfAssetType("Image"));
                add_float("amount", "変位量", "マップのRGをXY変位として使う最大ピクセル量。", -512.0, 512.0, 0.1);
                add_float("intensity", "適用量", "元画像から変位画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("XY 強度").Step(0.01));
                break;
            case UI::UIEffectKind::TurbulentDisplace:
                add_float("amount", "変位量", "乱流で輪郭を動かす最大ピクセル量。", 0.0, 512.0, 0.1);
                add_float("radius", "スケール", "乱流の大きさ。", 4.0, 2048.0, 0.1);
                add_float("speed", "流れる速度", "乱流が時間方向へ流れる速度。", -32.0, 32.0, 0.01);
                add_float("intensity", "適用量", "元画像との混合量。", 0.0, 1.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::FractalNoise:
                add_float("radius", "スケール", "ノイズの基本スケール。", 0.25, 64.0, 0.01);
                add_float("amount", "オクターブ", "重ねるノイズ層数。", 1.0, 8.0, 1.0);
                add_float("speed", "時間速度", "ノイズが変化する速度。", -16.0, 16.0, 0.01);
                add_float("intensity", "適用量", "元画像とノイズ色の混合量。", 0.0, 1.0, 0.01);
                add_color("暗部色", "ノイズ0側の色。");
                add_named_color("color_2", "明部色", "ノイズ1側の色。");
                add_seed();
                break;
            case UI::UIEffectKind::MotionBlur:
                add_float("radius", "ブラー距離", "移動方向へ引く長さ（ピクセル）。", 0.0, 512.0, 0.1);
                add_float("angle", "方向", "ブラー方向（度）。", -360.0, 360.0, 0.1);
                add_float("intensity", "適用量", "元画像との混合量。", 0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Echo:
                add_float("amount", "間隔", "残像1枚ごとの距離（ピクセル）。", -256.0, 256.0, 0.1);
                add_float("angle", "方向", "残像が伸びる方向（度）。", -360.0, 360.0, 0.1);
                add_float("radius", "枚数", "重ねる残像数。", 1.0, 16.0, 1.0);
                add_float("intensity", "減衰", "後ろの残像ほど薄くなる強さ。", 0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::DropShadow:
                add_float("amount", "距離", "影をずらす距離（ピクセル）。", 0.0, 512.0, 0.1);
                add_float("angle", "方向", "影の方向（度）。", -360.0, 360.0, 0.1);
                add_float("radius", "ぼかし", "影のぼかし半径。", 0.0, 128.0, 0.1);
                add_float("intensity", "濃さ", "影の不透明度。", 0.0, 2.0, 0.01);
                add_color("影色", "影へ使う色。");
                break;
            case UI::UIEffectKind::InnerShadow:
                add_float("amount", "距離", "内側影のずれ（ピクセル）。", 0.0, 256.0, 0.1);
                add_float("angle", "方向", "内側影の方向（度）。", -360.0, 360.0, 0.1);
                add_float("radius", "ぼかし", "内側影のぼかし半径。", 0.0, 128.0, 0.1);
                add_float("intensity", "濃さ", "内側影の不透明度。", 0.0, 2.0, 0.01);
                add_color("影色", "内側影へ使う色。");
                break;
            case UI::UIEffectKind::LUT:
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("LUT Texture").OfAssetType("Image"));
                add_float("radius", "LUT サイズ", "2D strip LUT の1辺。通常16/32/64。", 2.0, 64.0, 1.0);
                add_float("intensity", "適用量", "元色からLUT色へ混ぜる割合。", 0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::ToneCurve:
                add_float("radius", "Lift", "暗部の持ち上げ/押し下げ。", -1.0, 1.0, 0.001);
                add_float("intensity", "Gamma", "中間調のガンマ。1が無変更。", 0.05, 8.0, 0.001);
                add_float("threshold", "Gain", "明部のゲイン。1が無変更。", 0.0, 4.0, 0.001);
                add_float("amount", "適用量", "補正前後の混合量。", 0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::MatteComposite:
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("Matte B").OfAssetType("Image"));
                add_float("amount", "演算", "0=Add / 1=Subtract / 2=Intersect。", 0.0, 2.0, 1.0);
                break;
            case UI::UIEffectKind::MatteMorphology:
                push(MakeEffectProperty(i, "waveform", Reflection::PropertyType::Enum,
                    Reflection::Animatable::Step).Display("モード")
                    .Tooltip("アルファの膨張・収縮・3x3局所ホールフィル・エッジ。")
                    .AsEnum({ "膨張", "収縮", "ホールフィル", "エッジ" }));
                add_float("radius", "半径", "近傍を読む半径（ピクセル）。", 0.0, 128.0, 0.1);
                add_float("intensity", "適用量", "元アルファから形態学結果へ混ぜる割合。", 0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::BevelEmboss:
                add_float("radius", "幅", "アルファ勾配を読む幅（ピクセル）。", 0.5, 64.0, 0.1);
                add_float("amount", "深さ", "ハイライト / シャドウの深さ。", 0.0, 4.0, 0.01);
                add_float("angle", "光の方向", "光が当たる方向（度）。", -360.0, 360.0, 0.1);
                add_float("intensity", "適用量", "ベベル色を元色へ混ぜる割合。", 0.0, 2.0, 0.01);
                add_color("ハイライト色", "明るい側の色。濃さはアルファ勾配で決まる。");
                add_named_color("color_2", "シャドウ色", "暗い側の色。");
                break;
            case UI::UIEffectKind::Kaleidoscope:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("万華鏡の中心。0.5, 0.5 が中央。").Range(0.0, 1.0).Step(0.01));
                add_float("radius", "セグメント数", "扇形の分割数。", 2.0, 64.0, 1.0);
                add_float("amount", "スケール", "取り込む画像の倍率。1が等倍。", 0.1, 4.0, 0.01);
                add_float("angle", "回転", "扇形パターンの回転（度）。", -360.0, 360.0, 0.1);
                add_float("intensity", "適用量", "元画像から万華鏡画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                push(MakeEffectProperty(i, "waveform", Reflection::PropertyType::Enum,
                    Reflection::Animatable::Step).Display("タイル方式")
                    .AsEnum({ "ミラー", "リピート" }));
                break;
            case UI::UIEffectKind::PageCurl:
                push(MakeEffectProperty(i, "waveform", Reflection::PropertyType::Enum,
                    Reflection::Animatable::Step).Display("めくりパターン")
                    .Tooltip("直線・コーナー・中央折り・アコーディオンから選ぶ。")
                    .AsEnum({ "直線めくり", "コーナーめくり", "中央折り", "アコーディオン" }));
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("起点 / コーナー")
                    .Tooltip("コーナーめくりの起点。0,0 が左上、1,1 が右下。直線・中央折りでは無視する。")
                    .Range(0.0, 1.0).Step(0.01));
                add_float("progress", "進行", "ページをめくる進行度。0で未変形、1で全体を折る。", 0.0, 1.0, 0.001);
                add_float("radius", "曲率半径", "折り目の円筒半径（ピクセル）。", 4.0, 512.0, 0.1);
                add_float("angle", "方向", "めくる方向（度）。", -360.0, 360.0, 0.1);
                add_float("amount", "裏面の不透明度", "折り返された裏面の不透明度。", 0.0, 1.0, 0.01);
                add_float("softness", "影の柔らかさ", "折り目付近の陰影。", 0.0, 1.0, 0.01);
                add_float("intensity", "適用量", "元画像からカール画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("裏面色", "折り返しの裏面へ掛ける色。");
                break;
            case UI::UIEffectKind::AsciiLedMatrix:
                push(MakeEffectProperty(i, "waveform", Reflection::PropertyType::Enum,
                    Reflection::Animatable::Step).Display("表示方式")
                    .Tooltip("実際の5x7 ASCIIグリフ、丸LED、角LEDから選ぶ。")
                    .AsEnum({ "ASCII文字", "丸LED", "角LED" }));
                add_float("radius", "セルサイズ", "LEDセルの大きさ（ピクセル）。", 2.0, 128.0, 0.1);
                add_float("amount", "字形 / ドットサイズ", "セル内の文字画素またはLEDドットの大きさ。", 0.05, 1.0, 0.01);
                add_float("threshold", "黒レベル", "これ以下の輝度を消す。", 0.0, 1.0, 0.01);
                add_float("softness", "アンチエイリアス", "ドット端の柔らかさ。", 0.0, 1.0, 0.01);
                add_float("intensity", "適用量", "元画像からLED表現へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("LED色", "点灯するドットの色。");
                break;
            case UI::UIEffectKind::FeedbackZoom:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("ズーム中心")
                    .Tooltip("前フレームを拡大する中心。0.5, 0.5 が中央。").Range(0.0, 1.0).Step(0.01));
                add_float("amount", "ズーム量", "前フレームを読む倍率。正でズームイン、負でズームアウト。",
                    -0.5, 0.5, 0.001);
                add_float("angle", "回転", "前フレームを読むときの回転（度）。", -360.0, 360.0, 0.1);
                add_float("radius", "拡散", "履歴サンプルをぼかす半径（ピクセル）。", 0.0, 64.0, 0.1);
                add_float("softness", "減衰", "中心から離れるほど履歴を弱める量。", 0.0, 1.0, 0.01);
                add_float("intensity", "適用量", "現在フレームから履歴へ混ぜる割合。", 0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::LiquidGlass:
                add_float("radius", "リム幅", "アルファ輪郭から法線を求める幅（ピクセル）。", 0.5, 64.0, 0.1);
                add_float("amount", "屈折量", "輪郭で背景サンプルをずらす距離（ピクセル）。", 0.0, 128.0, 0.1);
                add_float("progress", "色分散", "RGBサンプルを分離する割合。", 0.0, 1.0, 0.01);
                add_float("softness", "フレネル", "輪郭ハイライトの広がり。", 0.0, 1.0, 0.01);
                add_float("angle", "光の方向", "リムハイライトの方向（度）。", -360.0, 360.0, 0.1);
                add_float("intensity", "適用量", "ガラス表現の混合量。", 0.0, 1.0, 0.01);
                add_color("ガラス色", "屈折色へ掛ける色。アルファは着色量。");
                add_named_color("color_2", "リム色", "輪郭の反射ハイライト色。アルファは反射強度。");
                break;
            case UI::UIEffectKind::LightSweep:
                add_float("radius", "帯幅", "走査する光帯の幅（ピクセル）。", 1.0, 512.0, 0.1);
                add_float("amount", "光量", "光帯の強さ。", 0.0, 8.0, 0.01);
                add_float("threshold", "輝度しきい値", "この輝度以上を強く照らす。0でアルファ全体。", 0.0, 1.0, 0.01);
                add_float("progress", "位置", "光帯の位置。0から1で画面を横切る。", 0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "光帯の端をぼかす量。", 0.0, 1.0, 0.01);
                add_float("speed", "自動速度", "時間で位置を進める速度。", -4.0, 4.0, 0.001);
                add_float("angle", "方向", "光帯の走査方向（度）。", -360.0, 360.0, 0.1);
                add_float("intensity", "適用量", "光沢結果を元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("主光色", "光帯の主色。");
                add_named_color("color_2", "副光色", "光帯の縁へ混ぜる色。");
                break;
            case UI::UIEffectKind::Shockwave:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心")
                    .Tooltip("波の中心。0.5, 0.5 が中央。").Range(0.0, 1.0).Step(0.01));
                add_float("radius", "波幅", "波のリング幅（ピクセル）。", 1.0, 256.0, 0.1);
                add_float("amount", "歪み量", "波面が画像をずらす最大距離（ピクセル）。", 0.0, 256.0, 0.1);
                add_float("threshold", "波数", "リング内の振動回数。", 0.0, 16.0, 0.1);
                add_float("progress", "進行", "波の位置。0で中心、1で外周。", 0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "リングの端をぼかす量。", 0.0, 1.0, 0.01);
                add_float("speed", "自動速度", "時間で波を進める速度。", -4.0, 4.0, 0.001);
                add_float("intensity", "適用量", "歪みと発光の混合量。", 0.0, 2.0, 0.01);
                add_color("波の色", "リングの発光色。");
                break;
            case UI::UIEffectKind::PixelSort:
                add_float("radius", "ソート範囲", "1方向へ調べる半径（ピクセル）。", 4.0, 128.0, 1.0);
                add_float("amount", "適用量", "元画像からソート結果へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_float("threshold", "下限", "ソート対象にする輝度の下限。", 0.0, 1.0, 0.01);
                add_float("softness", "しきい値の柔らかさ", "ソート対象の境界を滑らかにする。", 0.0, 0.5, 0.001);
                add_float("progress", "オフセット", "ソート窓の位相をずらす。", 0.0, 1.0, 0.001);
                add_float("speed", "自動速度", "ソート窓を動かす速度。", -4.0, 4.0, 0.001);
                add_float("angle", "方向", "ソートする軸（度）。", -360.0, 360.0, 0.1);
                push(MakeEffectProperty(i, "waveform", Reflection::PropertyType::Enum,
                    Reflection::Animatable::Step).Display("ソート方式")
                    .AsEnum({ "輝度昇順", "輝度降順", "彩度昇順", "色相昇順" }));
                push(MakeEffectProperty(i, "color_stop_2", Reflection::PropertyType::Float,
                    Reflection::Animatable::Interpolatable).Display("上限")
                    .Tooltip("ソート対象にする輝度の上限。").Range(0.0, 1.0).Step(0.01));
                break;
            case UI::UIEffectKind::Hologram:
                add_float("radius", "走査線間隔", "ホログラムの水平走査線の間隔（ピクセル）。", 2.0, 128.0, 0.1);
                add_float("amount", "干渉量", "走査線・RGBずれ・ちらつきの強さ。", 0.0, 2.0, 0.01);
                add_float("threshold", "ノイズ量", "信号ノイズの量。", 0.0, 1.0, 0.01);
                add_float("softness", "ちらつき", "時間ちらつきの強さ。", 0.0, 1.0, 0.01);
                add_float("speed", "更新速度", "ホログラムの時間変化速度。", -8.0, 8.0, 0.01);
                add_float("intensity", "適用量", "元画像からホログラム表現へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("ホログラム色", "走査線と信号の基準色。");
                break;
            case UI::UIEffectKind::IridescentFoil:
                add_float("radius", "虹彩スケール", "オイルスリック状の虹彩模様の大きさ。", 0.25, 8.0, 0.01);
                add_float("amount", "虹彩量", "虹色の反射強度。", 0.0, 2.0, 0.01);
                add_float("progress", "位相", "虹彩グラデーションの開始位相。", -1.0, 1.0, 0.001);
                add_float("speed", "流れる速度", "虹彩が時間で移動する速度。", -4.0, 4.0, 0.01);
                add_float("intensity", "適用量", "元画像から虹彩表現へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("虹彩色1", "虹彩グラデーションの色1。");
                add_named_color("color_2", "虹彩色2", "虹彩グラデーションの色2。");
                add_named_color("color_3", "虹彩色3", "虹彩グラデーションの色3。");
                add_named_color("color_4", "虹彩色4", "虹彩グラデーションの色4。");
                break;
            case UI::UIEffectKind::RadarSweep:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心").Range(0.0, 1.0).Step(0.01));
                add_float("radius", "ビーム幅", "レーダー走査ビームの角度幅。", 0.01, 1.0, 0.001);
                add_float("amount", "光量", "走査ビームと残光の強さ。", 0.0, 4.0, 0.01);
                add_float("progress", "位相", "走査角度の開始位相。", 0.0, 1.0, 0.001);
                add_float("speed", "回転速度", "レーダービームの自動回転速度。", -8.0, 8.0, 0.01);
                add_float("intensity", "適用量", "元画像からレーダー表現へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("ビーム色", "レーダー走査の発光色。");
                break;
            case UI::UIEffectKind::EnergyPulse:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心").Range(0.0, 1.0).Step(0.01));
                add_float("radius", "パルス幅", "走るエネルギーリングの幅。", 0.01, 1.0, 0.001);
                add_float("amount", "発光量", "パルスの発光強度。", 0.0, 4.0, 0.01);
                add_float("progress", "進行", "リングの位置。", 0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "リング境界をぼかす量。", 0.0, 1.0, 0.01);
                add_float("speed", "自動速度", "パルスを自動で進める速度。", -8.0, 8.0, 0.01);
                add_float("intensity", "適用量", "元画像からパルス表現へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("パルス色", "パルスの発光色。");
                break;
            case UI::UIEffectKind::CircuitFlow:
                add_float("radius", "回路間隔", "回路グリッドの間隔（ピクセル）。", 4.0, 256.0, 0.1);
                add_float("amount", "発光量", "回路線と流れる信号の強さ。", 0.0, 4.0, 0.01);
                add_float("threshold", "線の細さ", "回路線の太さ。", 0.01, 0.49, 0.001);
                add_float("speed", "流速", "回路上を流れる信号の速度。", -8.0, 8.0, 0.01);
                add_float("intensity", "適用量", "元画像から回路表現へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("回路色", "回路と信号の発光色。");
                break;
            case UI::UIEffectKind::HeatHaze:
                add_float("radius", "ノイズスケール", "熱揺らぎの模様のスケール（ピクセル）。", 2.0, 256.0, 0.1);
                add_float("amount", "歪み量", "熱気で画面をずらす距離（ピクセル）。", 0.0, 128.0, 0.1);
                add_float("threshold", "上昇量", "熱気が上へ流れる量。", 0.0, 2.0, 0.01);
                add_float("speed", "流速", "熱気の時間変化速度。", -8.0, 8.0, 0.01);
                add_float("intensity", "適用量", "歪み結果を元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::WaterCaustics:
                add_float("radius", "波紋スケール", "水面模様のスケール（ピクセル）。", 4.0, 256.0, 0.1);
                add_float("amount", "模様の濃さ", "コースティクスの明暗差。", 0.0, 2.0, 0.01);
                add_float("speed", "流速", "水面模様の移動速度。", -8.0, 8.0, 0.01);
                add_float("intensity", "適用量", "水面色を元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("水面色", "コースティクスへ掛ける色。");
                add_seed();
                break;
            case UI::UIEffectKind::VoronoiShatter:
                add_float("radius", "セルサイズ", "破片セルの大きさ（ピクセル）。", 4.0, 256.0, 0.1);
                add_float("amount", "飛散距離", "破片が動く最大距離（ピクセル）。", 0.0, 256.0, 0.1);
                add_float("progress", "進行", "破片の飛散進行度。", 0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "破片境界のブレンド幅。", 0.0, 0.5, 0.001);
                add_float("speed", "自動速度", "破片を自動で動かす速度。", -4.0, 4.0, 0.01);
                add_float("intensity", "適用量", "破片化結果を元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::InkBleed:
                add_float("radius", "にじみスケール", "インクの広がり模様のスケール。", 2.0, 256.0, 0.1);
                add_float("amount", "にじみ量", "インクがにじむ強さ。", 0.0, 2.0, 0.01);
                add_float("progress", "進行", "にじみの進行度。", 0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "にじみ境界をぼかす量。", 0.0, 0.5, 0.001);
                add_float("speed", "広がる速度", "インクが時間で広がる速度。", -4.0, 4.0, 0.01);
                add_float("intensity", "適用量", "インク表現を元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("インク色", "にじみへ使う色。");
                add_seed();
                break;
            case UI::UIEffectKind::BurnReveal:
                add_float("radius", "ノイズスケール", "焼き付き境界の模様のスケール。", 2.0, 256.0, 0.1);
                add_float("amount", "焼け幅", "境界の発光幅と焼け色の強さ。", 0.0, 2.0, 0.01);
                add_float("progress", "表示進行", "0 で非表示、1 で全表示。", 0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "表示境界をぼかす量。", 0.0, 0.5, 0.001);
                add_float("speed", "揺らぎ速度", "焼け境界の時間変化速度。", -4.0, 4.0, 0.01);
                add_float("intensity", "適用量", "焼け表現の混合量。", 0.0, 1.0, 0.01);
                add_color("焼け色", "焼け境界の主色。");
                add_named_color("color_2", "芯の色", "焼け境界の明るい芯。");
                add_seed();
                break;
            case UI::UIEffectKind::PortalVortex:
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable).Display("中心").Range(0.0, 1.0).Step(0.01));
                add_float("radius", "作用半径", "渦が作用する正規化半径。", 0.05, 1.5, 0.001);
                add_float("amount", "渦の強さ", "サンプルを回転/収束させる量。", -1.0, 1.0, 0.001);
                add_float("threshold", "収束量", "中心へ吸い込む強さ。", 0.0, 1.0, 0.01);
                add_float("angle", "回転角", "渦の回転角度（度）。", -720.0, 720.0, 0.1);
                add_float("softness", "境界の柔らかさ", "渦の外周をぼかす量。", 0.0, 1.0, 0.01);
                add_float("speed", "回転速度", "時間で渦を回す速度。", -8.0, 8.0, 0.01);
                add_float("intensity", "適用量", "渦結果を元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("ポータル色", "渦の発光色。");
                break;
            case UI::UIEffectKind::FrostCrack:
                add_float("radius", "ひびスケール", "霜のひび模様のスケール。", 4.0, 256.0, 0.1);
                add_float("amount", "ひびの濃さ", "ひびと霜の強さ。", 0.0, 2.0, 0.01);
                add_float("threshold", "ひび密度", "ひびを出すしきい値。", 0.0, 1.0, 0.01);
                add_float("progress", "広がり", "霜が広がる進行度。", 0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "ひびの境界をぼかす量。", 0.0, 0.5, 0.001);
                add_float("speed", "成長速度", "ひびが時間で成長する速度。", -4.0, 4.0, 0.01);
                add_float("intensity", "適用量", "霜表現を元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("霜色", "霜とひびへ使う色。");
                add_seed();
                break;
            case UI::UIEffectKind::SpeedLines:
                add_float("progress", "進行", "0 から 1 で線が中心へ寄り画面を覆う。",
                    0.0, 1.0, 0.001);
                add_float("radius", "線の本数", "放射状に並べる線の数。", 8.0, 512.0, 1.0);
                add_float("amount", "線の太さ", "1 本あたりの角度方向の太さ。",
                    0.0, 1.0, 0.001);
                add_float("threshold", "中心の空き", "線を出さない中心の半径。",
                    0.0, 1.0, 0.001);
                add_float("softness", "縁の柔らかさ", "線の内側の端をぼかす量。",
                    0.0001, 1.0, 0.001);
                add_float("angle", "回転", "放射全体の回転角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("speed", "ちらつき速度", "線の長さが時間で揺れる速さ。0 で静止。",
                    -32.0, 32.0, 0.01);
                add_float("intensity", "適用量", "線を元画像へ混ぜる割合。",
                    0.0, 1.0, 0.01);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable)
                    .Display("中心").Tooltip("正規化座標で指定する放射の中心。0.5, 0.5 が中央。")
                    .Step(0.01));
                add_color("線の色", "集中線へ使う色。");
                add_seed();
                break;
            case UI::UIEffectKind::ClockWipe:
                add_float("progress", "進行", "0 から 1 で時計回りに開閉する。",
                    0.0, 1.0, 0.001);
                add_float("angle", "開始角度", "掃き始める角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("amount", "掃引量", "1 で全周。負の値で逆回りにする。",
                    -1.0, 1.0, 0.001);
                add_float("threshold", "中心の空き", "最後まで残す中心の半径。",
                    0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "掃引の境界をぼかす量。",
                    0.0001, 1.0, 0.001);
                add_float("intensity", "適用量", "アルファを抜く割合。",
                    0.0, 1.0, 0.01);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable)
                    .Display("中心").Tooltip("正規化座標で指定する回転中心。0.5, 0.5 が中央。")
                    .Step(0.01));
                break;
            case UI::UIEffectKind::Blinds:
                add_float("progress", "進行", "0 から 1 でブラインドが閉じる。",
                    0.0, 1.0, 0.001);
                add_float("radius", "枚数", "短冊の枚数。", 1.0, 128.0, 1.0);
                add_float("angle", "向き", "短冊の向き（度）。0 で横。",
                    -360.0, 360.0, 0.1);
                add_float("amount", "開きかた",
                    "0 で片側から、1 で各短冊の中央から開く。", 0.0, 1.0, 0.001);
                add_float("softness", "縁の柔らかさ", "短冊の境界をぼかす量。",
                    0.0001, 1.0, 0.001);
                add_float("intensity", "適用量", "アルファを抜く割合。",
                    0.0, 1.0, 0.01);
                break;
            case UI::UIEffectKind::Checkerboard:
                add_float("progress", "進行", "0 から 1 でマスが広がり画面を覆う。",
                    0.0, 1.0, 0.001);
                add_float("radius", "マス数", "画面を割るマスの数。", 1.0, 64.0, 1.0);
                add_float("amount", "市松の遅れ",
                    "市松のもう一方が遅れて開く量。", 0.0, 1.0, 0.001);
                add_float("threshold", "ばらつき",
                    "マスごとに開く時刻を散らす量。", 0.0, 1.0, 0.001);
                add_float("angle", "回転", "市松全体の回転角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("softness", "縁の柔らかさ", "マスの境界をぼかす量。",
                    0.0001, 1.0, 0.001);
                add_float("intensity", "適用量", "アルファを抜く割合。",
                    0.0, 1.0, 0.01);
                add_seed();
                break;
            case UI::UIEffectKind::ShapeWipe:
                add_float("progress", "進行", "0 から 1 で形状が広がり画面を覆う。",
                    0.0, 1.0, 0.001);
                push(MakeEffectProperty(i, "waveform", Reflection::PropertyType::Enum,
                    Reflection::Animatable::Step)
                    .Display("形状")
                    .Tooltip("広がる形。形状画像を指定した場合はそちらを使う。")
                    .AsEnum({ "円", "星", "ハート", "菱形" }));
                add_float("radius", "頂点の数", "星の頂点の数。", 2.0, 24.0, 1.0);
                add_float("threshold", "星の凹み", "星の内側の半径の割合。",
                    0.0, 1.0, 0.001);
                add_float("angle", "回転", "形状の回転角度（度）。",
                    -360.0, 360.0, 0.1);
                add_float("softness", "縁の柔らかさ", "形状の輪郭をぼかす量。",
                    0.0001, 1.0, 0.001);
                add_float("intensity", "適用量", "アルファを抜く割合。",
                    0.0, 1.0, 0.01);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable)
                    .Display("中心").Tooltip("正規化座標で指定する形状の中心。0.5, 0.5 が中央。")
                    .Step(0.01));
                push(MakeEffectProperty(i, "mask", Reflection::PropertyType::AssetReference,
                    Reflection::Animatable::Step).Display("形状画像")
                    .Tooltip("設定した場合は画像の明るい所から開く。")
                    .OfAssetType("Image"));
                break;
            case UI::UIEffectKind::Petals:
                add_float("progress", "量", "0 で降らず、1 で最も多い。",
                    0.0, 1.0, 0.001);
                add_float("radius", "密度", "画面を割る格子の細かさ。", 1.0, 64.0, 1.0);
                add_float("amount", "大きさ", "1 枚あたりの大きさ。", 0.001, 0.5, 0.001);
                add_float("speed", "落下速度", "0 で完全に静止する。", -8.0, 8.0, 0.01);
                add_float("angle", "横流れ", "左右へ揺れる量。", -8.0, 8.0, 0.01);
                add_float("softness", "縁の柔らかさ", "花びらの輪郭をぼかす量。",
                    0.0001, 1.0, 0.001);
                add_float("intensity", "適用量", "元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_color("色", "花びらまたは雪の色。");
                add_seed();
                break;
            case UI::UIEffectKind::GodRays:
                add_float("intensity", "強さ", "光芒の強さ。", 0.0, 8.0, 0.01);
                add_float("threshold", "明るさしきい値", "ここより明るい所だけが伸びる。",
                    0.0, 4.0, 0.01);
                add_float("amount", "減衰", "遠いサンプルほど弱める率。", 0.0, 1.0, 0.001);
                add_float("softness", "伸びる長さ", "光芒を伸ばす距離。", 0.0001, 2.0, 0.001);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable)
                    .Display("光源").Tooltip("正規化座標で指定する光源。0.5, 0.5 が中央。")
                    .Step(0.01));
                add_color("光の色", "光芒へ掛ける色。");
                break;
            case UI::UIEffectKind::LensFlare:
                add_float("intensity", "強さ", "フレアの強さ。", 0.0, 8.0, 0.01);
                add_float("radius", "ゴーストの数", "並べる玉の数。", 1.0, 16.0, 1.0);
                add_float("threshold", "明るさしきい値", "ここより明るい所だけが写り込む。",
                    0.0, 4.0, 0.01);
                add_float("amount", "ゴースト間隔", "玉を並べる間隔。", -2.0, 2.0, 0.001);
                add_float("softness", "ハローの太さ", "光源を囲む輪の太さ。",
                    0.0001, 1.0, 0.001);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable)
                    .Display("光源").Tooltip("正規化座標で指定する光源。0.5, 0.5 が中央。")
                    .Step(0.01));
                add_color("フレアの色", "ゴーストとハローへ掛ける色。");
                break;
            case UI::UIEffectKind::Bokeh:
                add_float("radius", "ボケ半径", "玉ボケの広がり（ピクセル）。",
                    0.0, 128.0, 0.1);
                add_float("intensity", "適用量", "元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_float("threshold", "明るさしきい値", "ここより明るい所が強く玉になる。",
                    0.0, 4.0, 0.01);
                add_float("amount", "羽根の数", "3 未満で円、3 以上で多角形。",
                    0.0, 12.0, 1.0);
                break;
            case UI::UIEffectKind::TiltShift:
                add_float("radius", "ぼかし量", "帯の外をぼかす強さ（ピクセル）。",
                    0.0, 128.0, 0.1);
                add_float("intensity", "適用量", "元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_float("amount", "帯の幅", "くっきり残す帯の半幅。", 0.0, 1.0, 0.001);
                add_float("softness", "境界の柔らかさ", "帯の外へ移る距離。",
                    0.0001, 1.0, 0.001);
                add_float("angle", "帯の角度", "帯を傾ける角度（度）。",
                    -360.0, 360.0, 0.1);
                push(MakeEffectProperty(i, "direction", Reflection::PropertyType::Vector2,
                    Reflection::Animatable::Interpolatable)
                    .Display("帯の中心").Tooltip("正規化座標で指定する帯の中心。0.5, 0.5 が中央。")
                    .Step(0.01));
                break;
            case UI::UIEffectKind::FilmScratch:
                add_float("radius", "傷の数", "縦に走る傷の本数。", 0.0, 24.0, 1.0);
                add_float("intensity", "適用量", "元画像へ混ぜる割合。", 0.0, 1.0, 0.01);
                add_float("threshold", "ホコリの量", "画面へ散らす粒の量。", 0.0, 1.0, 0.001);
                add_float("amount", "傷の太さ", "1 本あたりの太さ。", 0.0001, 0.05, 0.0001);
                add_float("softness", "縁の柔らかさ", "傷の輪郭をぼかす量。",
                    0.0, 1.0, 0.001);
                add_float("speed", "コマ送り速度", "0 で 1 枚に固定する。", 0.0, 8.0, 0.01);
                add_color("傷の色", "傷とホコリの色。");
                add_seed();
                break;
            default:
                break;
            }

            push(MakeEffectProperty(i, "custom_shader",
                Reflection::PropertyType::AssetReference,
                Reflection::Animatable::Step)
                .Display(u8"カスタムシェーダー")
                .Tooltip(u8"DX12 Effect Stack で main を実行し、プロパティを渡します。")
                .OfAssetType("Shader"));

            if (index < custom_schemas_.size() && custom_schemas_[index])
            {
                for (const Rendering::ShaderProperty& property :
                    custom_schemas_[index]->Properties())
                {
                    push(MakeCustomEffectProperty(i, property));
                }
            }
        }
    }
}
