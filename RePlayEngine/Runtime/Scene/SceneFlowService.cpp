#include "SceneFlowService.h"

#include "../Events/EventBus.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ReplayEngine::Runtime
{
    namespace
    {
        // 参照を返す API 用の空文字列。
        // 未接続・未設定でも「空の参照」を返せるようにするために置く。
        const std::string empty_guid;
    }

    const char* ToString(SceneTransitionState state) noexcept
    {
        switch (state)
        {
        case SceneTransitionState::Idle:      return "Idle";
        case SceneTransitionState::Requested: return "Requested";
        case SceneTransitionState::Loading:   return "Loading";
        case SceneTransitionState::Completed: return "Completed";
        case SceneTransitionState::Failed:    return "Failed";
        }
        return "Unknown";
    }

    const char* ToString(StartupSceneState state) noexcept
    {
        switch (state)
        {
        case StartupSceneState::NotStarted:    return "NotStarted";
        case StartupSceneState::NotConfigured: return "NotConfigured";
        case StartupSceneState::Loading:       return "Loading";
        case StartupSceneState::Ready:         return "Ready";
        case StartupSceneState::Failed:        return "Failed";
        }
        return "Unknown";
    }

    const char* ToString(SceneTransitionKind kind) noexcept
    {
        switch (kind)
        {
        case SceneTransitionKind::None:   return "None";
        case SceneTransitionKind::Load:   return "Load";
        case SceneTransitionKind::Reload: return "Reload";
        case SceneTransitionKind::Return: return "Return";
        }
        return "Unknown";
    }

    SceneFlowService::SceneFlowService(RuntimeSceneService& scenes) noexcept
        : scenes_(&scenes)
    {
    }

    SceneFlowService::~SceneFlowService() = default;

    void SceneFlowService::SetFlowAsset(const SceneFlowAsset& asset)
    {
        flow_asset_ = asset;
        flow_asset_loaded_ = true;
    }

    void SceneFlowService::ClearFlowAsset() noexcept
    {
        flow_asset_.Clear();
        flow_asset_loaded_ = false;
    }

    void SceneFlowService::ClearVariables() noexcept
    {
        flow_bools_.clear();
        flow_ints_.clear();
        flow_floats_.clear();
    }

    RuntimeStatus SceneFlowService::SetVariableBool(const std::string& key, bool value)
    {
        if (key.empty()) return RuntimeStatus::InvalidArgument;
        flow_bools_[key] = value;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus SceneFlowService::SetVariableInt(const std::string& key, std::int64_t value)
    {
        if (key.empty()) return RuntimeStatus::InvalidArgument;
        flow_ints_[key] = value;
        return RuntimeStatus::Ok;
    }

    RuntimeStatus SceneFlowService::SetVariableFloat(const std::string& key, double value)
    {
        if (key.empty() || !std::isfinite(value)) return RuntimeStatus::InvalidArgument;
        flow_floats_[key] = value;
        return RuntimeStatus::Ok;
    }

    bool SceneFlowService::EvaluateCondition(const SceneFlowCondition& condition) const noexcept
    {
        double actual = 0.0;
        switch (condition.type)
        {
        case SceneFlowConditionType::Bool:
        {
            const auto found = flow_bools_.find(condition.key);
            if (found == flow_bools_.end()) return false;
            actual = found->second ? 1.0 : 0.0;
            break;
        }
        case SceneFlowConditionType::Int:
        {
            const auto found = flow_ints_.find(condition.key);
            if (found == flow_ints_.end()) return false;
            actual = static_cast<double>(found->second);
            break;
        }
        case SceneFlowConditionType::Float:
        {
            const auto found = flow_floats_.find(condition.key);
            if (found == flow_floats_.end()) return false;
            actual = found->second;
            break;
        }
        default:
            return false;
        }

        const double expected = condition.value;
        switch (condition.op)
        {
        case SceneFlowCompareOp::Equal:
            return condition.type == SceneFlowConditionType::Float
                ? std::fabs(actual - expected) <= 1.0e-6 : actual == expected;
        case SceneFlowCompareOp::NotEqual:
            return condition.type == SceneFlowConditionType::Float
                ? std::fabs(actual - expected) > 1.0e-6 : actual != expected;
        case SceneFlowCompareOp::Less:         return actual < expected;
        case SceneFlowCompareOp::LessEqual:    return actual <= expected;
        case SceneFlowCompareOp::Greater:      return actual > expected;
        case SceneFlowCompareOp::GreaterEqual: return actual >= expected;
        }
        return false;
    }

    RuntimeStatus SceneFlowService::Trigger(const std::string& event_name)
    {
        if (event_name.empty()) return RuntimeStatus::InvalidArgument;
        if (!flow_asset_loaded_) return RuntimeStatus::ServiceUnavailable;
        if (TransitionInProgress()) return RuntimeStatus::TransitionInProgress;

        const std::string current = CurrentSceneGUID();
        const SceneFlowTransition* best = nullptr;
        for (const SceneFlowTransition& transition : flow_asset_.transitions)
        {
            if (!transition.enabled || transition.event_name != event_name ||
                transition.to_scene_guid.empty())
                continue;
            if (!transition.from_scene_guid.empty() && transition.from_scene_guid != current)
                continue;

            bool conditions_ok = true;
            for (const SceneFlowCondition& condition : transition.conditions)
            {
                if (!EvaluateCondition(condition))
                {
                    conditions_ok = false;
                    break;
                }
            }
            if (!conditions_ok) continue;

            // 同優先度なら Asset 内で前に書かれたものを優先し、結果を決定的にする。
            if (best == nullptr || transition.priority > best->priority)
                best = &transition;
        }

        if (best == nullptr) return RuntimeStatus::SceneMissing;
        return LoadScene(best->to_scene_guid);
    }

    // -----------------------------------------------------------------------
    // 状態
    // -----------------------------------------------------------------------

    bool SceneFlowService::TransitionInProgress() const noexcept
    {
        // 自分が要求を出している最中か、下位が読み込み中ならどちらも「遷移中」。
        // 片方だけを見ると、要求直後の 1 フレームだけ「遷移していない」ことになる。
        return kind_ != SceneTransitionKind::None ||
            (scenes_ != nullptr && scenes_->IsBusy());
    }

    bool SceneFlowService::CanReturn() const noexcept
    {
        return !history_.empty() && !TransitionInProgress();
    }

    const std::string& SceneFlowService::CurrentSceneGUID() const noexcept
    {
        return scenes_ != nullptr ? scenes_->CurrentSceneGUID() : empty_guid;
    }

    const std::string& SceneFlowService::PendingSceneGUID() const noexcept
    {
        return kind_ != SceneTransitionKind::None ? target_guid_ : empty_guid;
    }

    void SceneFlowService::SetFailure(RuntimeStatus status, std::string detail)
    {
        state_ = SceneTransitionState::Failed;
        last_result_ = status;
        last_error_ = std::string(ToString(status)) + ": " + detail;
        kind_ = SceneTransitionKind::None;
        target_guid_.clear();
        ++failed_count_;

        if (startup_state_ == StartupSceneState::Loading)
        {
            startup_state_ = StartupSceneState::Failed;
        }
    }

    // -----------------------------------------------------------------------
    // 遷移要求
    // -----------------------------------------------------------------------

    RuntimeStatus SceneFlowService::BeginTransition(SceneTransitionKind kind,
        const std::string& target)
    {
        if (scenes_ == nullptr) return RuntimeStatus::ServiceUnavailable;

        // 進行中の遷移は上書きしない。
        // 上書きを許すと、同じフレームに 2 つの Trigger が発火しただけで
        // どちらの Scene へ行くかが不定になる。
        if (TransitionInProgress()) return RuntimeStatus::TransitionInProgress;

        if (target.empty()) return RuntimeStatus::InvalidArgument;

        previous_guid_ = scenes_->CurrentSceneGUID();
        target_guid_ = target;
        kind_ = kind;

        const SceneRequestResult request = kind == SceneTransitionKind::Reload
            ? scenes_->RequestReload()
            : scenes_->RequestLoad(target);

        if (request == SceneRequestResult::Accepted)
        {
            state_ = SceneTransitionState::Requested;
            last_result_ = RuntimeStatus::Ok;
            last_error_.clear();
            return RuntimeStatus::Ok;
        }

        // 受理されなかった。履歴も現在の Scene も一切触らない。
        const RuntimeStatus status = request == SceneRequestResult::Busy
            ? RuntimeStatus::TransitionInProgress
            : RuntimeStatus::InvalidArgument;
        SetFailure(status, "Scene の読み込み要求が受理されませんでした。");
        return status;
    }

    RuntimeStatus SceneFlowService::LoadScene(const Reflection::SceneReference& reference)
    {
        // SceneReference は Scene Asset 専用の型。
        // AssetReference をそのまま受け取らないので、Texture などを
        // 遷移先として渡せる経路が構造的に存在しない。
        return LoadScene(reference.guid);
    }

    RuntimeStatus SceneFlowService::LoadScene(const std::string& asset_guid)
    {
        if (asset_guid.empty())
        {
            SetFailure(RuntimeStatus::InvalidArgument,
                "遷移先の Scene が設定されていません。");
            return RuntimeStatus::InvalidArgument;
        }
        return BeginTransition(SceneTransitionKind::Load, asset_guid);
    }

    RuntimeStatus SceneFlowService::ReloadCurrentScene()
    {
        if (scenes_ == nullptr) return RuntimeStatus::ServiceUnavailable;

        const std::string current = scenes_->CurrentSceneGUID();
        if (current.empty())
        {
            SetFailure(RuntimeStatus::SceneMissing,
                "読み込み済みの Scene が無いため再読み込みできません。");
            return RuntimeStatus::SceneMissing;
        }
        return BeginTransition(SceneTransitionKind::Reload, current);
    }

    RuntimeStatus SceneFlowService::ReturnToPreviousScene()
    {
        if (scenes_ == nullptr) return RuntimeStatus::ServiceUnavailable;

        if (history_.empty())
        {
            SetFailure(RuntimeStatus::SceneMissing, "戻り先の履歴がありません。");
            return RuntimeStatus::SceneMissing;
        }

        // ここでは取り出さない。
        // 失敗したときに戻り先が消えていると、二度と戻れなくなる。
        // 実際に取り出すのは成功が確定したとき。
        return BeginTransition(SceneTransitionKind::Return, history_.back());
    }

    RuntimeStatus SceneFlowService::QuitApplication(const std::string& reason)
    {
        quit_requested_ = true;
        quit_reason_ = reason;
        ++quit_request_count_;

        // 受け取る側が Scene をまたいで購読できるよう Global Bus へ出す。
        EventRecord record;
        record.type = EngineEvents::ApplicationQuitRequested;
        record.type_name = "ApplicationQuitRequested";
        record.payload.Set("reason", Reflection::PropertyValue::MakeString(reason));
        EventBus::Global().Publish(std::move(record));

        // プロセスは落とさない。落とす判断はアプリケーション層が行う。
        return RuntimeStatus::Ok;
    }

    void SceneFlowService::ClearQuitRequest() noexcept
    {
        quit_requested_ = false;
        quit_reason_.clear();
    }

    // -----------------------------------------------------------------------
    // Startup Scene
    // -----------------------------------------------------------------------

    RuntimeStatus SceneFlowService::BeginStartupScene(const std::string& startup_scene_guid)
    {
        startup_scene_guid_ = startup_scene_guid;

        if (startup_scene_guid.empty())
        {
            // 未設定は「エラー」ではなく「設定されていない」という状態。
            // 適当な Scene を選んで起動したように見せることはしない。
            startup_state_ = StartupSceneState::NotConfigured;
            last_result_ = RuntimeStatus::SceneMissing;
            last_error_ = "Startup Scene が設定されていません。";
            return RuntimeStatus::SceneMissing;
        }

        // 起動 Scene は履歴の起点。戻り先を作らない。
        history_.clear();

        startup_state_ = StartupSceneState::Loading;
        const RuntimeStatus status =
            BeginTransition(SceneTransitionKind::Load, startup_scene_guid);
        if (Failed(status))
        {
            startup_state_ = StartupSceneState::Failed;
        }
        return status;
    }

    // -----------------------------------------------------------------------
    // 履歴
    // -----------------------------------------------------------------------

    void SceneFlowService::PushHistory(const std::string& guid,
        const std::string& new_current)
    {
        if (guid.empty()) return;

        // 切替先と同じものは積まない。
        // 積むと「戻る」で同じ Scene へ行き、履歴が減らないループになる。
        if (guid == new_current) return;

        history_.push_back(guid);

        // 上限を超えたら古い方から捨てる。
        // 新しい方を捨てると「1 つ前へ戻る」が壊れるので、必ず先頭から削る。
        while (history_.size() > maximum_history)
        {
            history_.erase(history_.begin());
        }
    }

    void SceneFlowService::OnTransitionSucceeded()
    {
        const std::string current = scenes_->CurrentSceneGUID();

        switch (kind_)
        {
        case SceneTransitionKind::Load:
            PushHistory(previous_guid_, current);
            break;

        case SceneTransitionKind::Reload:
            // 履歴を増やさない。同じ Scene へ戻る項目が積み上がるだけになる。
            break;

        case SceneTransitionKind::Return:
            // 戻り先へ着いたので、その項目を取り除く。
            // 積み直さないので、往復を繰り返しても履歴は伸びない。
            if (!history_.empty()) history_.pop_back();
            break;

        case SceneTransitionKind::None:
        default:
            break;
        }

        state_ = SceneTransitionState::Completed;
        last_result_ = RuntimeStatus::Ok;
        last_error_.clear();
        kind_ = SceneTransitionKind::None;
        target_guid_.clear();
        ++completed_count_;

        if (startup_state_ == StartupSceneState::Loading)
        {
            startup_state_ = StartupSceneState::Ready;
        }
    }

    void SceneFlowService::OnTransitionFailed()
    {
        // 失敗した Load は履歴を 1 つも動かさない。
        // 現在の World も RuntimeSceneService 側で維持されている。
        last_error_ = scenes_->LastError();
        const RuntimeStatus status = scenes_->LastStatus();

        state_ = SceneTransitionState::Failed;
        last_result_ = Failed(status) ? status : RuntimeStatus::SceneLoadFailed;
        kind_ = SceneTransitionKind::None;
        target_guid_.clear();
        ++failed_count_;

        if (startup_state_ == StartupSceneState::Loading)
        {
            startup_state_ = StartupSceneState::Failed;
        }
    }

    // -----------------------------------------------------------------------
    // 進行
    // -----------------------------------------------------------------------

    void SceneFlowService::Tick()
    {
        if (scenes_ == nullptr) return;

        // 実読込は下位サービスが行う。ここは進行を追うだけ。
        scenes_->Tick();

        if (kind_ == SceneTransitionKind::None) return;

        switch (scenes_->State())
        {
        case SceneLoadState::Loading:
        case SceneLoadState::ReadyToSwap:
        case SceneLoadState::Swapping:
            state_ = SceneTransitionState::Loading;
            break;

        case SceneLoadState::Completed:
            OnTransitionSucceeded();
            break;

        case SceneLoadState::Failed:
            OnTransitionFailed();
            break;

        case SceneLoadState::Idle:
            // CancelPending などで下位が取り消された。
            // 遷移も無かったことにする。履歴は動かさない。
            state_ = SceneTransitionState::Idle;
            kind_ = SceneTransitionKind::None;
            target_guid_.clear();
            if (startup_state_ == StartupSceneState::Loading)
            {
                startup_state_ = StartupSceneState::Failed;
            }
            break;
        }
    }

    // -----------------------------------------------------------------------
    // ISceneFlow
    // -----------------------------------------------------------------------

    RuntimeStatus SceneFlowService::RequestSceneLoad(const std::string& asset_guid)
    {
        return LoadScene(asset_guid);
    }

    RuntimeStatus SceneFlowService::RequestSceneReload()
    {
        return ReloadCurrentScene();
    }

    RuntimeStatus SceneFlowService::RequestReturnToPreviousScene()
    {
        return ReturnToPreviousScene();
    }

    RuntimeStatus SceneFlowService::RequestSceneFlowTrigger(const std::string& event_name)
    {
        return Trigger(event_name);
    }

    RuntimeStatus SceneFlowService::SetSceneFlowBool(const std::string& key, bool value)
    {
        return SetVariableBool(key, value);
    }

    RuntimeStatus SceneFlowService::SetSceneFlowInt(const std::string& key, std::int64_t value)
    {
        return SetVariableInt(key, value);
    }

    RuntimeStatus SceneFlowService::SetSceneFlowFloat(const std::string& key, double value)
    {
        return SetVariableFloat(key, value);
    }

    RuntimeStatus SceneFlowService::RequestQuitApplication(const std::string& reason)
    {
        return QuitApplication(reason);
    }

    bool SceneFlowService::SceneTransitionInProgress() const
    {
        return TransitionInProgress();
    }

    const std::string& SceneFlowService::CurrentSceneGuid() const
    {
        return CurrentSceneGUID();
    }
}
