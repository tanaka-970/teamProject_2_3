#pragma once

#include "../../../RePlayEngine/Runtime/Behaviour/BehaviourComponent.h"
#include "../../../RePlayEngine/Reflection/Property/References.h"
#include "../../../RePlayEngine/Reflection/Registry/TypeGUID.h"

#include <DirectXMath.h>

#include <cstdint>

// ゲーム側の Behaviour を置く場所。
//
// なぜ RePlayEngine/ の下ではないのか:
//   Engine Core は「どんなゲームでも使える入れ物」でなければならない。
//   扉・敵・タイトル画面といったゲーム固有の振る舞いを Engine 側へ並べ始めると、
//   別のゲームを作るたびに Engine を書き換えることになる。
//   ゲーム固有の Behaviour は Game Module 側に置き、
//   RegisterGameBehaviours() から明示的に登録する。
namespace Game::Behaviours
{
    namespace Runtime = ReplayEngine::Runtime;
    namespace Reflection = ReplayEngine::Reflection;

    // Behaviour 基盤の動作確認用。GameObject を回し続けるだけ。
    //
    // 検証する範囲:
    //   Property の保存・復元 / OnUpdate / 有効・無効 / Prefab / 複製
    //
    // ゲーム本編のルールは一切持たない。Sample と Validation 専用。
    class RotatorBehaviour final : public Runtime::BehaviourComponent
    {
        REPLAY_COMPONENT_BODY(RotatorBehaviour)

    public:
        static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
        {
            return Reflection::MakeTypeGUID("b0000000000000000000000000000001");
        }

        DirectX::XMFLOAT3 axis{ 0.0f, 1.0f, 0.0f };
        float degrees_per_second = 90.0f;

        // タイムスケール非依存で回すか。
        // 現時点では unscaled と delta が同じ値だが、
        // 後からタイムスケールを入れたときに Behaviour 側を書き換えずに済む。
        bool use_unscaled_time = false;

        // 動作確認用の読み取り専用値。Runtime のみ意味を持つ。
        float accumulated_degrees = 0.0f;

    protected:
        void OnAwake() override;
        void OnUpdate(float delta_time) override;
    };

    // Trigger 配送の動作確認用。接触回数を数えるだけ。
    class TriggerCounterBehaviour final : public Runtime::BehaviourComponent
    {
        REPLAY_COMPONENT_BODY(TriggerCounterBehaviour)

    public:
        static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
        {
            return Reflection::MakeTypeGUID("b0000000000000000000000000000002");
        }

        // -1 なら Layer を問わない。
        int accepted_layer = -1;

        // Trigger 側として受けたぶんだけ数えるか。
        // 同じ接触が両側へ届くことを検証するための切り替え。
        bool count_trigger_side_only = false;

        // 読み取り専用の実行時値。
        int enter_count = 0;
        int stay_count = 0;
        int exit_count = 0;

        // 最後に接触した相手。ObjectReference として保存できることの確認も兼ねる。
        Reflection::ObjectReference last_other;

    protected:
        void OnTriggerEnter(const Runtime::TriggerEvent& event) override;
        void OnTriggerStay(const Runtime::TriggerEvent& event) override;
        void OnTriggerExit(const Runtime::TriggerEvent& event) override;

    private:
        bool Accepts(const Runtime::TriggerEvent& event) const noexcept;
    };
}

namespace Game
{
    // ゲーム固有 Behaviour の登録入口。
    //
    // RegisterBuiltInComponents() と分けている理由:
    //   Engine が提供する型と、ゲームが足した型の境界をコード上で見えるようにするため。
    //   将来 C# Script を載せたときも、Managed 型の登録は
    //   ここと同じ「明示的に呼ぶ関数」として並べる。
    //   静的初期化に頼らないので、初期化順序の問題が起きない。
    //
    // 呼び出しは RegisterBuiltInComponents() の後。
    // Behaviour も普通の Component として ComponentRegistry へ登録するため、
    // 先に組み込み型が入っている必要がある。
    void RegisterGameBehaviours();
}
