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
            default:
                break;
            }

            push(MakeEffectProperty(i, "custom_shader",
                Reflection::PropertyType::AssetReference,
                Reflection::Animatable::Step)
                .Display("カスタムシェーダー")
                .Tooltip("Shader Composer の PostProcess 出力を UI Effect として使う。")
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
