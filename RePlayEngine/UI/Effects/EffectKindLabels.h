#pragma once

#include "UIEffect.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ReplayEngine::UI
{
    inline constexpr std::array<const char*,
        static_cast<std::size_t>(UIEffectKind::Count)> effect_kind_labels{
        "ぼかし", "発光", "色調補正", "ノイズ",
        "揺れ", "マスク", "ワイプ", "ディゾルブ", "歪み",
        "色収差", "クワハラ", "網点",
        "方向ブラー", "放射ブラー", "回転ブラー",
        "ビネット", "光条", "レンズ歪み",
        "ポスタライズ", "二値化", "カラーランプ", "レベル補正",
        "色温度", "エッジ検出", "輪郭線", "ロングシャドウ",
        "クロスハッチング", "ブラシストローク", "モザイク", "結晶化",
        "ステンドグラス", "渦巻き", "球面化", "波紋",
        "極座標", "走査線", "CRT", "グリッチ",
        "ディザ", "VHS", "レターボックス", "波形",
        "ディスプレイスメントマップ", "タービュレント変形", "フラクタルノイズ",
        "モーションブラー", "エコー / 残像", "ドロップシャドウ", "インナーシャドウ",
        "LUT", "トーンカーブ", "Matte Composite",
        "マット形態学", "ベベル / エンボス", "万華鏡 / ミラータイル",
        "ページカール / フォールド", "ASCII / LED マトリクス",
        "フィードバックズーム", "Liquid Glass", "ライトスイープ",
        "ショックウェーブ", "ピクセルソート",
        "ホログラム", "イリデッセントフォイル", "レーダースイープ",
        "エネルギーパルス", "サーキットフロー", "ヒートヘイズ",
        "ウォーターコースティクス", "ボロノイシャッター",
        "インクブリード", "バーンリビール", "ポータルヴォルテックス",
        "フロストクラック", "泡マスク", "集中線", "時計ワイプ"
    };

    inline const char* EffectKindLabel(UIEffectKind kind) noexcept
    {
        const int index = static_cast<int>(kind);
        if (index < 0 || index >= static_cast<int>(effect_kind_labels.size())) return "不明";
        return effect_kind_labels[static_cast<std::size_t>(index)];
    }

    inline std::vector<std::string> MakeEffectKindLabels()
    {
        std::vector<std::string> labels;
        labels.reserve(effect_kind_labels.size());
        for (std::size_t index = 0; index < effect_kind_labels.size(); ++index)
        {
            labels.emplace_back(effect_kind_labels[index]);
            if (IsTimeDrivenEffect(static_cast<UIEffectKind>(index)))
                labels.back() += "  [M]";
        }
        return labels;
    }
}
