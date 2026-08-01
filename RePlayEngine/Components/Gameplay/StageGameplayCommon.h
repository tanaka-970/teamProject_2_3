#pragma once

#include "../../Core/ObjectID/ObjectID.h"
#include "../../Object/Component/TriggerContact.h"

#include <DirectXMath.h>

#include <cstdint>

namespace ReplayEngine::Core { class GameObject; }
namespace ReplayEngine::Scene { class Scene; }

namespace ReplayEngine::Components
{
    // Stage 用 Gameplay Component（Spawn / Checkpoint / Goal / KillVolume）が
    // 共通で必要とする小さな処理だけをまとめる。
    //
    // 【ここに置く条件】
    //   ・Player 型を一切知らないこと
    //   ・状態を持たないこと（すべて自由関数）
    //
    // 巨大な基底クラスにはしない。Component が「継承しないと使えない」形にすると、
    // 後から別の Component が同じ判定を使いたくなったときに継承を強いてしまう。
    namespace StageGameplay
    {
        // Trigger 接触のうち「自分が Trigger 側である」ものだけを通す。
        //
        // OnTriggerEnter は Trigger 側と入った側の両方へ届く。
        // Checkpoint の GameObject が別の Trigger へ入ったときにも呼ばれるため、
        // これを確かめないと自分の判定が二重に走る。
        bool IsTriggerSide(const Core::GameObject* owner,
            const Core::TriggerContact& contact) noexcept;

        // 接触相手の GameObject。削除済み・別 Scene なら nullptr。
        Core::GameObject* ResolveOther(const Core::GameObject* owner,
            const Core::TriggerContact& contact);

        // 相手の Collider が target_mask に含まれるレイヤーか。
        //
        // Collider の Layer / Mask による絞り込みは SceneCollisionWorld が既に
        // 行っているが、それは「接触を検出するか」の判定。
        // ここでの target_mask は「検出した接触に反応するか」で、目的が違う。
        //   例: Checkpoint は Player にだけ反応したいが、
        //       箱が乗ったことも検出はしたい。
        bool MatchesTargetMask(const Core::GameObject& other,
            std::uint32_t other_collider_id, int target_mask) noexcept;

        // 対象をワールド座標へ移す。
        //
        // CharacterMotor があればそちらの Teleport を使う（速度と接地状態を
        // 正しく作り直すため）。無ければ Transform を直接書き換える。
        // どちらの経路でも Player 型には触れない。
        void TeleportObject(Core::GameObject& target,
            const DirectX::XMFLOAT3& world_position,
            const DirectX::XMFLOAT3& world_rotation_radians,
            bool apply_rotation);
    }
}
