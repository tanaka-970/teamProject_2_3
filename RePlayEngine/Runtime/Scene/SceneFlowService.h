#pragma once

#include "RuntimeSceneService.h"
#include "SceneFlowAsset.h"
#include "../API/RuntimeContext.h"
#include "../Core/RuntimeResult.h"
#include "../../Reflection/Property/References.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ReplayEngine::Runtime
{
    // ゲーム側から見た Scene 遷移の状態。
    //
    // RuntimeSceneService の SceneLoadState と分けている理由:
    //   あちらは「ファイルを読んで World を入れ替える」工程の状態。
    //   こちらは「遷移という 1 つの出来事」の状態で、粒度が違う。
    //   まとめると、Staging World の構築段階のような内部工程が
    //   ゲーム側の UI へそのまま漏れる。
    enum class SceneTransitionState : std::int32_t
    {
        Idle = 0,
        Requested = 1,   // 要求を受理し、下位サービスへ渡した
        Loading = 2,     // 下位サービスが読み込み中
        Completed = 3,   // 直近の遷移が成功した
        Failed = 4,      // 直近の遷移が失敗した（World は元のまま）
    };

    const char* ToString(SceneTransitionState state) noexcept;

    // Startup Scene の診断状態。
    //
    // 「起動できなかった」を無言で握りつぶさないために独立させてある。
    // 空 GUID・存在しない GUID・読み込み失敗は、どれも別の状態として残る。
    enum class StartupSceneState : std::int32_t
    {
        NotStarted = 0,     // まだ起動処理へ入っていない
        NotConfigured = 1,  // Startup Scene が設定されていない
        Loading = 2,
        Ready = 3,          // 起動 Scene の読み込みが完了した
        Failed = 4,         // 設定はあったが読み込めなかった
    };

    const char* ToString(StartupSceneState state) noexcept;

    // どの種類の遷移として要求されたか。履歴の扱いを分けるために持つ。
    enum class SceneTransitionKind : std::int32_t
    {
        None = 0,
        Load = 1,
        Reload = 2,
        Return = 3,
    };

    const char* ToString(SceneTransitionKind kind) noexcept;

    // ゲーム側の Scene 遷移要求と履歴を受け持つ上位サービス。
    //
    // ---------------------------------------------------------------------
    // 【RuntimeSceneService と分ける理由】
    //
    //   RuntimeSceneService は「1 つの Scene を安全に読み替える」ことだけを知る。
    //   どこから来てどこへ戻るか、戻れるのか、終了要求が出ているかは知らない。
    //
    //   履歴と遷移方針を下位へ混ぜると、Scene を 1 枚読むだけの場面
    //   （Editor の Play 開始や、起動 Scene の読み込み）でも
    //   履歴が勝手に増える。責任を分けておけば、履歴を持たない読み込みが書ける。
    //
    //   このクラスはファイルを読まない。Staging World も持たない。
    //   実読込は必ず RuntimeSceneService へ委ねる。
    //
    // ---------------------------------------------------------------------
    // 【履歴の約束】
    //
    //   - 成功した切替だけを積む。失敗した Load は履歴を 1 つも動かさない。
    //   - Reload は履歴を増やさない（戻り先が自分自身になるため）。
    //   - Return は積まずに 1 つ取り出す。往復で無限に伸びない。
    //   - 切替先と同じ GUID は積まない。戻り先が現在の Scene になるループを作らない。
    //   - 空 GUID は積まない。
    //   - 上限 maximum_history 件。超えたら古いものから捨てる。
    //
    // ---------------------------------------------------------------------
    // 所有関係:
    //   RuntimeSceneService を所有しない。参照だけを持つ。
    class SceneFlowService final : public ISceneFlow
    {
    public:
        explicit SceneFlowService(RuntimeSceneService& scenes) noexcept;
        ~SceneFlowService() override;

        SceneFlowService(const SceneFlowService&) = delete;
        SceneFlowService& operator=(const SceneFlowService&) = delete;

        // 履歴の上限。往復を繰り返しても際限なく伸びないようにする。
        static constexpr std::size_t maximum_history = 16;

        // ---- 遷移要求 --------------------------------------------------------
        //
        // どれも「要求」であって、その場では World は変わらない。
        // 実際の入れ替えは Tick() の安全点で起きる。

        RuntimeStatus LoadScene(const Reflection::SceneReference& reference);
        RuntimeStatus LoadScene(const std::string& asset_guid);
        RuntimeStatus ReloadCurrentScene();
        RuntimeStatus ReturnToPreviousScene();

        // ---- Data-driven Scene Flow ---------------------------------------------
        //
        // Asset は ProjectSettings / Editor が読み込み、この Service へ値で渡す。
        // Runtime はファイルパスを知らない。Hot-reload 時も SetFlowAsset() で差し替える。
        void SetFlowAsset(const SceneFlowAsset& asset);
        void ClearFlowAsset() noexcept;
        bool HasFlowAsset() const noexcept { return flow_asset_loaded_; }
        const SceneFlowAsset& FlowAsset() const noexcept { return flow_asset_; }

        RuntimeStatus Trigger(const std::string& event_name);
        RuntimeStatus SetVariableBool(const std::string& key, bool value);
        RuntimeStatus SetVariableInt(const std::string& key, std::int64_t value);
        RuntimeStatus SetVariableFloat(const std::string& key, double value);
        void ClearVariables() noexcept;

        // アプリケーションの終了要求。
        //
        // ここでプロセスを終了しない理由:
        //   検証中に呼ばれたらテストごと落ちる。Editor の Play 中に呼ばれたら
        //   Editor まで落ちる。要求として記録し、受け取る側が判断する。
        RuntimeStatus QuitApplication(const std::string& reason = std::string());

        // ---- 状態 ------------------------------------------------------------

        bool CanReturn() const noexcept;
        const std::string& CurrentSceneGUID() const noexcept;
        const std::string& PendingSceneGUID() const noexcept;
        SceneTransitionState CurrentTransitionState() const noexcept { return state_; }
        SceneTransitionKind CurrentTransitionKind() const noexcept { return kind_; }
        bool TransitionInProgress() const noexcept;

        RuntimeStatus LastResult() const noexcept { return last_result_; }
        const std::string& LastError() const noexcept { return last_error_; }

        const std::vector<std::string>& History() const noexcept { return history_; }
        void ClearHistory() noexcept { history_.clear(); }

        std::uint64_t CompletedTransitionCount() const noexcept { return completed_count_; }
        std::uint64_t FailedTransitionCount() const noexcept { return failed_count_; }

        // ---- Startup Scene -----------------------------------------------------
        //
        // 起動時の流れ:
        //   ProjectSettings 読み込み -> BeginStartupScene(GUID) -> Tick -> Ready
        //
        // 空 GUID / 無効 GUID / 読み込み失敗は、どれも明示的な診断状態になる。
        // 別の Scene を勝手に選んで起動することはしない。
        RuntimeStatus BeginStartupScene(const std::string& startup_scene_guid);

        StartupSceneState StartupState() const noexcept { return startup_state_; }
        const std::string& StartupSceneGUID() const noexcept { return startup_scene_guid_; }

        // 起動が診断状態で止まっているか。UI へ理由を出すために使う。
        bool StartupBlocked() const noexcept
        {
            return startup_state_ == StartupSceneState::NotConfigured ||
                startup_state_ == StartupSceneState::Failed;
        }

        // ---- 終了要求 -----------------------------------------------------------

        bool QuitRequested() const noexcept { return quit_requested_; }
        const std::string& QuitReason() const noexcept { return quit_reason_; }
        std::uint64_t QuitRequestCount() const noexcept { return quit_request_count_; }

        // アプリケーション層が要求を受け取ったあとに呼ぶ。
        void ClearQuitRequest() noexcept;

        // ---- 進行 -----------------------------------------------------------------

        // フレームの安全な同期点で 1 回呼ぶ。
        //
        // 下位の RuntimeSceneService::Tick() もここから呼ぶ。
        // 呼び出し側が両方を別々に呼ぶ形にすると、順序を間違えたときに
        // 「遷移が終わったのに履歴が更新されていない」フレームができる。
        // 入口を 1 つにしておけば、その食い違いが起きない。
        void Tick();

        // ---- ISceneFlow ------------------------------------------------------------
        //
        // Behaviour から見える口。RuntimeContext 経由で呼ばれる。

        RuntimeStatus RequestSceneLoad(const std::string& asset_guid) override;
        RuntimeStatus RequestSceneReload() override;
        RuntimeStatus RequestReturnToPreviousScene() override;
        RuntimeStatus RequestSceneFlowTrigger(const std::string& event_name) override;
        RuntimeStatus SetSceneFlowBool(const std::string& key, bool value) override;
        RuntimeStatus SetSceneFlowInt(const std::string& key, std::int64_t value) override;
        RuntimeStatus SetSceneFlowFloat(const std::string& key, double value) override;
        RuntimeStatus RequestQuitApplication(const std::string& reason) override;
        bool SceneTransitionInProgress() const override;
        const std::string& CurrentSceneGuid() const override;
        float SceneTransitionProgress() const override;
        RuntimeStatus LastSceneTransitionStatus() const override;

    private:
        // 要求を受け付けてよいかを判定し、受け付けられない理由を返す。
        RuntimeStatus BeginTransition(SceneTransitionKind kind, const std::string& target);

        void OnTransitionSucceeded();
        void OnTransitionFailed();

        // 戻り先として履歴へ積む。空・重複・上限をここで一括して処理する。
        void PushHistory(const std::string& guid, const std::string& new_current);

        void SetFailure(RuntimeStatus status, std::string detail);
        bool EvaluateCondition(const SceneFlowCondition& condition) const noexcept;

        RuntimeSceneService* scenes_ = nullptr;

        SceneFlowAsset flow_asset_;
        bool flow_asset_loaded_ = false;
        std::unordered_map<std::string, bool> flow_bools_;
        std::unordered_map<std::string, std::int64_t> flow_ints_;
        std::unordered_map<std::string, double> flow_floats_;

        SceneTransitionState state_ = SceneTransitionState::Idle;
        SceneTransitionKind kind_ = SceneTransitionKind::None;

        // 進行中の遷移の対象と、その直前に居た Scene。
        std::string target_guid_;
        std::string previous_guid_;

        RuntimeStatus last_result_ = RuntimeStatus::Ok;
        std::string last_error_;

        // 戻り先の並び。末尾が「1 つ前」。
        std::vector<std::string> history_;

        std::string startup_scene_guid_;
        StartupSceneState startup_state_ = StartupSceneState::NotStarted;

        bool quit_requested_ = false;
        std::string quit_reason_;
        std::uint64_t quit_request_count_ = 0;

        std::uint64_t completed_count_ = 0;
        std::uint64_t failed_count_ = 0;
    };
}
