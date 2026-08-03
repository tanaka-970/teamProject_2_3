#pragma once

#include "../../../RePlayEngine/Runtime/Behaviour/BehaviourComponent.h"
#include "../../../RePlayEngine/Runtime/Core/RuntimeResult.h"
#include "../../../RePlayEngine/Reflection/Property/References.h"
#include "../../../RePlayEngine/Reflection/Registry/TypeGUID.h"

#include <cstdint>
#include <string>

namespace Game::Behaviours
{
    namespace Runtime = ReplayEngine::Runtime;
    namespace Reflection = ReplayEngine::Reflection;

    // 何をする遷移か。
    //
    // Scene 名で分岐しない理由:
    //   Scene 名は自由に変えられる。名前で挙動が変わると、
    //   改名しただけでゲームが壊れる。行き先は必ず SceneReference で指す。
    enum class SceneTransitionMode : std::int32_t
    {
        LoadScene = 0,          // destination_scene へ移動する
        ReloadCurrent = 1,      // 現在の Scene を読み直す
        ReturnToPrevious = 2,   // 1 つ前の Scene へ戻る
        QuitApplication = 3,    // 終了要求を出す（プロセスは落とさない）
    };

    // Trigger に入ったら Scene 遷移を要求する Behaviour。
    //
    // ---------------------------------------------------------------------
    // 【その場で切り替えない】
    //
    //   OnTriggerEnter の中で World を入れ替えると、
    //   接触判定を走査している最中に自分も相手も解放されることになる。
    //
    //   ここで行うのは「要求」だけ。RuntimeContext -> SceneFlowService へ渡り、
    //   実際の入れ替えは RuntimeSceneService::Tick() の安全点で起きる。
    //   要求を出したあとも、このフレームの残りは今の World のまま進む。
    //
    // ---------------------------------------------------------------------
    // 【再発火させない】
    //
    //   3 段構えで止める。
    //     1. 遷移中は要求を出さない（SceneTransitionInProgress）
    //     2. trigger_once なら受理された時点で自分を締め切る
    //     3. 締め切りは要求が「受理されたとき」だけ立てる。
    //        拒否された要求で締め切ると、二度と遷移できなくなる。
    //
    // ---------------------------------------------------------------------
    // 【Collision とは混ぜない】
    //
    //   OnCollision 系は override しない。
    //   届く Collision は CharacterMotor の接地・壁接触だけで、
    //   「扉に入った」という意味にはならない。
    //   Trigger と Collision を同じ入口で受けると、
    //   壁に触れただけで Scene が切り替わる事故が起きる。
    class SceneTransitionBehaviour final : public Runtime::BehaviourComponent
    {
        REPLAY_COMPONENT_BODY(SceneTransitionBehaviour)

    public:
        static constexpr Reflection::TypeGUID StaticTypeGUID() noexcept
        {
            return Reflection::MakeTypeGUID("b0000000000000000000000000000003");
        }

        // 遷移先。Scene Asset だけを指せる型なので、
        // Texture や Prefab を行き先に設定できる経路が存在しない。
        Reflection::SceneReference destination_scene;

        // SceneTransitionMode の値。
        int transition_mode =
            static_cast<int>(SceneTransitionMode::LoadScene);

        // 一度発火したら締め切るか。
        bool trigger_once = true;

        // -1 なら Layer を問わない。
        int accepted_layer = -1;

        // 自分が Trigger 側として受けたときだけ反応するか。
        bool require_trigger_side = false;

        // ---- 読み取り専用の実行時値 -------------------------------------------

        int request_count = 0;    // 受理された要求の数
        int rejected_count = 0;   // 拒否・診断で止まった数

        // 直近の結果。Inspector と Runtime 診断へ出す。
        std::string last_diagnostic;

        // ---- 検証・診断用 --------------------------------------------------------

        bool Fired() const noexcept { return fired_; }
        void ResetFired() noexcept { fired_ = false; }

        SceneTransitionMode Mode() const noexcept
        {
            return static_cast<SceneTransitionMode>(transition_mode);
        }

        // Trigger を介さずに遷移要求を出す。
        //
        // Trigger 経路と同じ判定をここへ集約してあるので、
        // 「UI ボタンから呼ぶ」「検証から呼ぶ」でも挙動が食い違わない。
        ReplayEngine::Runtime::RuntimeStatus RequestTransition();

    protected:
        void OnAwake() override;
        void OnTriggerEnter(const Runtime::TriggerEvent& event) override;

    private:
        bool Accepts(const Runtime::TriggerEvent& event) const noexcept;

        bool fired_ = false;
    };
}
