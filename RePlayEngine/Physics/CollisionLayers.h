#pragma once

#include <cstdint>

namespace ReplayEngine::Physics
{
    // 衝突レイヤーの固定表。
    //
    // なぜ固定表なのか:
    //   プロジェクト設定でレイヤー名を編集できる仕組みは、Scene ファイルと
    //   プロジェクト設定の二重管理になり、名前を変えた瞬間に既存 Scene の
    //   意味が変わってしまう。まずは固定名で運用し、必要になってから
    //   「名前だけ差し替えられる表」へ広げる。番号の意味は変えない。
    //
    // 保存されるのは常に「番号」であって名前ではない。
    // 名前は Inspector の表示にしか使わない。
    namespace CollisionLayers
    {
        inline constexpr int count = 8;

        enum Layer
        {
            Default = 0,
            Player = 1,
            Enemy = 2,
            Environment = 3,
            PlayerAttack = 4,
            EnemyAttack = 5,
            Projectile = 6,
            Trigger = 7,
        };

        // 添字が層番号に対応する。count と必ず同じ長さにすること。
        inline const char* Name(int layer) noexcept
        {
            switch (layer)
            {
            case Default:      return "Default";
            case Player:       return "Player";
            case Enemy:        return "Enemy";
            case Environment:  return "Environment";
            case PlayerAttack: return "Player Attack";
            case EnemyAttack:  return "Enemy Attack";
            case Projectile:   return "Projectile";
            case Trigger:      return "Trigger";
            default:           break;
            }
            return "(不明)";
        }

        inline bool ValidLayer(int layer) noexcept
        {
            return layer >= 0 && layer < count;
        }

        // 範囲外の層番号は Default へ寄せる。
        // 壊れた Scene ファイルを読んでも判定が例外的な挙動にならないようにする。
        inline int ClampLayer(int layer) noexcept
        {
            return ValidLayer(layer) ? layer : static_cast<int>(Default);
        }

        inline int MaskBit(int layer) noexcept
        {
            return ValidLayer(layer) ? (1 << layer) : 0;
        }

        // すべての層を含むマスク。既定値として使う。
        inline constexpr int all_layers_mask = (1 << count) - 1;

        inline bool MaskContains(int mask, int layer) noexcept
        {
            // 旧データ互換: -1 は「すべてと衝突する」の意味で保存されていた。
            if (mask == -1) return true;
            return (mask & MaskBit(layer)) != 0;
        }

        // 2 つの Collider が互いを見るか。
        //
        // 片側だけが相手を見ている状態を「衝突する」と扱うと、
        // どちらの Collider を主体にして問い合わせたかで結果が変わってしまう。
        // 双方向で一致したときだけ衝突とする。
        inline bool Interact(int layer_a, int mask_a, int layer_b, int mask_b) noexcept
        {
            return MaskContains(mask_a, ClampLayer(layer_b)) &&
                   MaskContains(mask_b, ClampLayer(layer_a));
        }
    }
}
