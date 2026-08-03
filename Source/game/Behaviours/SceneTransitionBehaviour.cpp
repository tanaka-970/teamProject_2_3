#include "SceneTransitionBehaviour.h"

#include "../../../RePlayEngine/Runtime/API/RuntimeContext.h"

namespace Game::Behaviours
{
    using ReplayEngine::Runtime::RuntimeStatus;

    void SceneTransitionBehaviour::OnAwake()
    {
        fired_ = false;
        request_count = 0;
        rejected_count = 0;
        last_diagnostic.clear();

        // 設定漏れはここで一度診断へ出しておく。
        // 触るまで気付かない、という状態にしない。
        if (Mode() == SceneTransitionMode::LoadScene &&
            !destination_scene.IsAssigned())
        {
            last_diagnostic = "遷移先の Scene が設定されていません（Missing Scene Reference）。";
            if (Runtime() != nullptr)
            {
                Runtime()->LogWarning(last_diagnostic, SelfHandle());
            }
        }
    }

    bool SceneTransitionBehaviour::Accepts(const Runtime::TriggerEvent& event) const noexcept
    {
        if (require_trigger_side && !event.self_is_trigger) return false;

        // -1 は「Layer を問わない」。相手の Layer が不明 (-1) の場合も通す。
        if (accepted_layer >= 0 && event.other_layer >= 0 &&
            event.other_layer != accepted_layer)
        {
            return false;
        }
        return true;
    }

    RuntimeStatus SceneTransitionBehaviour::RequestTransition()
    {
        // ---- 1) 締め切り済み ------------------------------------------------
        if (trigger_once && fired_)
        {
            ++rejected_count;
            last_diagnostic = "一度だけの設定のため、この Behaviour は締め切り済みです。";
            return RuntimeStatus::UnsupportedOperation;
        }

        // ---- 2) Runtime 未接続 -----------------------------------------------
        //
        // Editor で置いただけの状態ではここへ来る。
        // 「成功したことにする」も「落ちる」もしない。
        Runtime::RuntimeContext* runtime = Runtime();
        if (runtime == nullptr)
        {
            ++rejected_count;
            last_diagnostic = "Runtime へ接続されていないため遷移できません。";
            return RuntimeStatus::ServiceUnavailable;
        }
        if (!runtime->SceneFlowAvailable())
        {
            ++rejected_count;
            last_diagnostic = "Scene Flow Service が接続されていません。";
            return RuntimeStatus::ServiceUnavailable;
        }

        // ---- 3) 遷移中 --------------------------------------------------------
        //
        // 同じフレームに複数の Trigger が発火しても、
        // 最初の 1 件だけが通り、残りはここで止まる。
        if (runtime->SceneTransitionInProgress())
        {
            ++rejected_count;
            last_diagnostic = "別の Scene 遷移が進行中のため、この要求は無視しました。";
            return RuntimeStatus::TransitionInProgress;
        }

        // ---- 4) 種類ごとの要求 ---------------------------------------------------

        RuntimeStatus status = RuntimeStatus::UnsupportedOperation;
        switch (Mode())
        {
        case SceneTransitionMode::LoadScene:
            if (!destination_scene.IsAssigned())
            {
                ++rejected_count;
                last_diagnostic =
                    "遷移先の Scene が設定されていません（Missing Scene Reference）。";
                runtime->LogWarning(last_diagnostic, SelfHandle());
                return RuntimeStatus::InvalidArgument;
            }
            status = runtime->LoadScene(destination_scene.guid);
            break;

        case SceneTransitionMode::ReloadCurrent:
            status = runtime->ReloadCurrentScene();
            break;

        case SceneTransitionMode::ReturnToPrevious:
            status = runtime->ReturnToPreviousScene();
            break;

        case SceneTransitionMode::QuitApplication:
            status = runtime->QuitApplication("SceneTransitionBehaviour");
            break;

        default:
            ++rejected_count;
            last_diagnostic = "未知の遷移種別が設定されています。";
            return RuntimeStatus::InvalidArgument;
        }

        // ---- 5) 結果の記録 -------------------------------------------------------
        //
        // 締め切りは「受理された」ときだけ立てる。
        // 拒否された要求で締め切ると、原因を直しても二度と遷移できなくなる。
        if (Succeeded(status))
        {
            ++request_count;
            if (trigger_once) fired_ = true;
            last_diagnostic = "遷移を要求しました。実際の切り替えは次の安全点で行われます。";
        }
        else
        {
            ++rejected_count;
            last_diagnostic = std::string("遷移要求が受け付けられませんでした: ") +
                ReplayEngine::Runtime::DescribeJapanese(status);
        }
        return status;
    }

    void SceneTransitionBehaviour::OnTriggerEnter(const Runtime::TriggerEvent& event)
    {
        if (!Accepts(event)) return;

        // ここで World を入れ替えない。要求を出すだけ。
        // 走査中の接触配列も、相手の GameObject も、このフレームの間は生きたまま。
        RequestTransition();
    }
}
